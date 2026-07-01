from __future__ import annotations

import asyncio

import pytest

from conftest import assert_recent_utc_timestamp
from fake_netconf_ssh_server import NETCONF_EOM, FakeNetconfSSHServer, notification_xml
from test_integration_fake_netconf_server import make_integration_client

pytestmark = [pytest.mark.integration, pytest.mark.slow]


async def wait_for_queue_size(client, expected_min: int, *, timeout: float = 4.0) -> None:
    deadline = asyncio.get_running_loop().time() + timeout
    while asyncio.get_running_loop().time() < deadline:
        if client.notification_queue_size() >= expected_min:
            return
        await asyncio.sleep(0.02)
    raise AssertionError(
        f"queue size did not reach {expected_min}; current={client.notification_queue_size()}"
    )


@pytest.mark.asyncio
async def test_peek_notifications_after_subscription_is_non_destructive(pyNetX_module):
    notifications = [notification_xml(i) for i in range(1, 4)]
    with FakeNetconfSSHServer(notifications=notifications, notification_interval=0.02) as server:
        client = make_integration_client(pyNetX_module, server, notif_queue_size=10)
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        await wait_for_queue_size(client, 3)
        assert client.notification_queue_size() == 3

        first_peek = client.peek_notifications(2)
        second_peek = client.peek_notifications(2)
        assert first_peek == second_peek
        assert len(first_peek) == 2
        assert "<sequence>1</sequence>" in first_peek[0]
        assert "<sequence>2</sequence>" in first_peek[1]
        assert client.notification_queue_size() == 3

        consumed = await client.next_notification_async(timeout_ms=1000)
        assert consumed == first_peek[0]
        assert client.notification_queue_size() == 2

        all_remaining = client.peek_notifications(-1)
        assert len(all_remaining) == 2
        assert "<sequence>2</sequence>" in all_remaining[0]
        assert "<sequence>3</sequence>" in all_remaining[1]

        client.delete_subscription()


@pytest.mark.asyncio
async def test_bounded_zero_notification_queue_drops_everything_and_reports_health(pyNetX_module):
    notifications = [notification_xml(i) for i in range(1, 5)]
    with FakeNetconfSSHServer(notifications=notifications, notification_interval=0.01) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=0,
            notif_drop_event_threshold=1,
            label="zero-queue-leaf",
        )
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        event = None
        deadline = asyncio.get_running_loop().time() + 5.0
        while asyncio.get_running_loop().time() < deadline:
            candidate = await pyNetX_module.next_notification_event_async(timeout_ms=1000)
            if candidate.type in {"notification_queue_full", "notification_drops_summary"}:
                event = candidate
                break

        assert event is not None, "expected a queue-full event for zero-sized queue"
        data = event.as_dict()
        assert data["label"] == "zero-queue-leaf"
        assert data["queue_size"] == 0
        assert data["queue_max_size"] == 0
        assert data["notifications_enqueued"] == 0
        assert data["notifications_dropped_queue_full"] >= 1
        assert data["notifications_dropped_delta"] >= 1
        assert_recent_utc_timestamp(data["timestamp"])
        assert client.notification_queue_size() == 0
        assert client.peek_notifications(-1) == []

        client.delete_subscription()


@pytest.mark.asyncio
async def test_queue_high_watermark_tracks_peak_depth(pyNetX_module):
    notifications = [notification_xml(i) for i in range(1, 6)]
    with FakeNetconfSSHServer(notifications=notifications, notification_interval=0.01) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=2,
            notif_drop_event_threshold=1,
            label="watermark-leaf",
        )
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        event = None
        deadline = asyncio.get_running_loop().time() + 5.0
        while asyncio.get_running_loop().time() < deadline:
            candidate = await pyNetX_module.next_notification_event_async(timeout_ms=1000)
            if candidate.type in {"notification_queue_full", "notification_drops_summary"}:
                event = candidate
                break

        assert event is not None
        assert event.label == "watermark-leaf"
        assert event.queue_max_size == 2
        assert event.queue_high_watermark == 2
        assert event.queue_size == 2

        # Draining should preserve FIFO order for the notifications that made it
        # into the bounded queue.
        first = await client.next_notification_async(timeout_ms=1000)
        second = await client.next_notification_async(timeout_ms=1000)
        assert "<sequence>1</sequence>" in first
        assert "<sequence>2</sequence>" in second

        client.delete_subscription()


@pytest.mark.asyncio
async def test_incomplete_notification_size_limit_generates_health_event(pyNetX_module):
    partial = "<notification>" + ("x" * 4096)
    with FakeNetconfSSHServer(
        incomplete_notification=partial,
        notification_start_delay=0.10,
    ) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=10,
            notif_incomplete_max_kb=1,
            notif_incomplete_timeout=5,
            label="size-guard-leaf",
        )
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        event = None
        deadline = asyncio.get_running_loop().time() + 5.0
        while asyncio.get_running_loop().time() < deadline:
            candidate = await pyNetX_module.next_notification_event_async(timeout_ms=1000)
            if candidate.type == "incomplete_notification":
                event = candidate
                break

        assert event is not None
        assert event.label == "size-guard-leaf"
        assert event.partial_bytes >= 1024
        assert event.incomplete_notifications_received >= 1
        assert_recent_utc_timestamp(event.timestamp)

        queued = await client.next_notification_async(timeout_ms=1000)
        assert queued.startswith("<notification>")

        client.delete_subscription()


async def wait_for_health_event(pyNetX_module, expected_types: set[str], *, predicate=None, timeout: float = 5.0):
    deadline = asyncio.get_running_loop().time() + timeout
    while asyncio.get_running_loop().time() < deadline:
        event = await pyNetX_module.next_notification_event_async(timeout_ms=500)
        if event.valid and event.type in expected_types and (predicate is None or predicate(event)):
            return event
    raise AssertionError(f"did not receive health event with type in {expected_types!r}")


@pytest.mark.asyncio
async def test_coalesced_eom_delimited_notifications_are_split_into_fifo_entries(pyNetX_module):
    notifications = [notification_xml(i) for i in range(1, 4)]
    combined_chunk = "".join(notification + NETCONF_EOM for notification in notifications)

    with FakeNetconfSSHServer(
        notification_raw_chunks=[combined_chunk],
        notification_start_delay=0.10,
        notification_interval=0.0,
    ) as server:
        client = make_integration_client(pyNetX_module, server, notif_queue_size=10)
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        received = [await client.next_notification_async(timeout_ms=3000) for _ in range(3)]

        assert ["<sequence>1</sequence>" in item for item in received] == [True, False, False]
        assert ["<sequence>2</sequence>" in item for item in received] == [False, True, False]
        assert ["<sequence>3</sequence>" in item for item in received] == [False, False, True]
        assert all(item.endswith(NETCONF_EOM) for item in received)
        assert client.notification_queue_size() == 0

        client.delete_subscription()


@pytest.mark.asyncio
async def test_complete_notification_plus_trailing_partial_is_buffered_until_eom_arrives(pyNetX_module):
    first = notification_xml(1)
    second = notification_xml(2)
    split_at = second.index("<event>")

    with FakeNetconfSSHServer(
        notification_raw_chunks=[
            first + NETCONF_EOM + second[:split_at],
            second[split_at:] + NETCONF_EOM,
        ],
        notification_start_delay=0.10,
        notification_interval=0.20,
    ) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=10,
            notif_incomplete_timeout=2,
        )
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        received_first = await client.next_notification_async(timeout_ms=3000)
        received_second = await client.next_notification_async(timeout_ms=3000)

        assert "<sequence>1</sequence>" in received_first
        assert "<sequence>2</sequence>" in received_second
        assert received_first.endswith(NETCONF_EOM)
        assert received_second.endswith(NETCONF_EOM)
        assert client.notification_queue_size() == 0

        client.delete_subscription()


@pytest.mark.asyncio
async def test_new_notification_start_before_previous_completion_queues_abandoned_partial(pyNetX_module):
    first = notification_xml(1)
    second = notification_xml(2)
    third = notification_xml(3)

    second_partial = second[: second.index("<event>")]
    third_split_at = third.index("<event>")
    third_partial = third[:third_split_at]
    third_rest = third[third_split_at:]

    with FakeNetconfSSHServer(
        notification_raw_chunks=[
            first + NETCONF_EOM + second_partial + third_partial,
            third_rest + NETCONF_EOM,
        ],
        notification_start_delay=0.10,
        notification_interval=0.25,
    ) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=10,
            notif_incomplete_timeout=2,
            label="abandoned-partial-leaf",
        )
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        event_task = asyncio.create_task(
            wait_for_health_event(
                pyNetX_module,
                {"incomplete_notification"},
                predicate=lambda event: "new notification start" in event.message,
            )
        )

        received_first = await client.next_notification_async(timeout_ms=3000)
        abandoned_second = await client.next_notification_async(timeout_ms=3000)
        recovered_third = await client.next_notification_async(timeout_ms=3000)
        event = await event_task

        assert "<sequence>1</sequence>" in received_first
        assert received_first.endswith(NETCONF_EOM)

        assert "<sequence>2</sequence>" in abandoned_second
        assert "<sequence>3</sequence>" not in abandoned_second
        assert not abandoned_second.endswith(NETCONF_EOM)

        assert "<sequence>3</sequence>" in recovered_third
        assert recovered_third.endswith(NETCONF_EOM)

        assert event.label == "abandoned-partial-leaf"
        assert event.partial_bytes == len(second_partial)
        assert event.incomplete_notifications_received >= 1
        assert_recent_utc_timestamp(event.timestamp)

        client.delete_subscription()


@pytest.mark.asyncio
async def test_malformed_eom_delimited_frame_is_queued_and_reported(pyNetX_module):
    malformed = "<notification><eventTime>bad</notification>"

    with FakeNetconfSSHServer(
        notification_raw_chunks=[malformed + NETCONF_EOM],
        notification_start_delay=0.10,
        notification_interval=0.0,
    ) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=10,
            label="malformed-frame-leaf",
        )
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        received = await client.next_notification_async(timeout_ms=3000)
        event = await wait_for_health_event(
            pyNetX_module,
            {"malformed_notification"},
            predicate=lambda candidate: "EOM-delimited data" in candidate.message,
        )

        assert received == malformed + NETCONF_EOM
        assert event.label == "malformed-frame-leaf"
        assert event.partial_bytes == len(malformed)
        assert_recent_utc_timestamp(event.timestamp)

        client.delete_subscription()


@pytest.mark.asyncio
async def test_orphan_bytes_before_notification_are_dropped_and_reported(pyNetX_module):
    orphan = "bad-device-prefix"
    notification = notification_xml(1)

    with FakeNetconfSSHServer(
        notification_raw_chunks=[orphan + notification + NETCONF_EOM],
        notification_start_delay=0.10,
        notification_interval=0.0,
    ) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=10,
            label="orphan-prefix-leaf",
        )
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        received = await client.next_notification_async(timeout_ms=3000)
        event = await wait_for_health_event(
            pyNetX_module,
            {"malformed_notification"},
            predicate=lambda candidate: "orphan notification bytes" in candidate.message,
        )

        assert orphan not in received
        assert "<sequence>1</sequence>" in received
        assert received.endswith(NETCONF_EOM)
        assert event.label == "orphan-prefix-leaf"
        assert event.partial_bytes == len(orphan)

        client.delete_subscription()


@pytest.mark.asyncio
async def test_empty_eom_frame_is_reported_and_dropped(pyNetX_module):
    with FakeNetconfSSHServer(
        notification_raw_chunks=[NETCONF_EOM],
        notification_start_delay=0.10,
        notification_interval=0.0,
    ) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=10,
            label="empty-eom-leaf",
        )
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")

        event = await wait_for_health_event(
            pyNetX_module,
            {"malformed_notification"},
            predicate=lambda candidate: "EOM marker without notification payload" in candidate.message,
        )

        assert event.label == "empty-eom-leaf"
        assert client.notification_queue_size() == 0
        assert client.peek_notifications(-1) == []

        client.delete_subscription()
