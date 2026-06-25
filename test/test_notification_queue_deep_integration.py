from __future__ import annotations

import asyncio

import pytest

from conftest import assert_recent_utc_timestamp
from fake_netconf_ssh_server import FakeNetconfSSHServer, notification_xml
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
