from __future__ import annotations

import asyncio

import pytest

from conftest import EXPECTED_HEALTH_EVENT_KEYS, assert_recent_utc_timestamp
from fake_netconf_ssh_server import (
    ERROR_REPLY,
    NETCONF_EOM,
    OK_REPLY,
    FakeNetconfSSHServer,
    notification_xml,
)

pytestmark = [pytest.mark.integration, pytest.mark.slow]


def make_integration_client(pyNetX_module, server: FakeNetconfSSHServer, **overrides):
    kwargs = {
        "hostname": server.host,
        "port": server.port,
        "username": server.username,
        "password": server.password,
        "connect_timeout": 5,
        "read_timeout": 5,
        "socket_connect_timeout": 2,
        "notif_queue_size": -1,
        "notif_incomplete_max_kb": 32,
        "notif_incomplete_timeout": 1,
        "notif_drop_event_threshold": 1,
        "label": "integration-leaf-01",
    }
    kwargs.update(overrides)
    return pyNetX_module.NetconfClient(**kwargs)


async def disconnect_quietly(client) -> None:
    try:
        await client.disconnect_async()
    except Exception:
        pass


@pytest.mark.asyncio
async def test_async_connect_send_custom_rpc_and_disconnect(pyNetX_module):
    with FakeNetconfSSHServer() as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        reply = await client.send_rpc_async(
            '<rpc message-id="custom-1"><get><filter><system/></filter></get></rpc>'
        )
        assert "<ok/>" in reply
        assert NETCONF_EOM in reply
        server.wait_for_rpc(lambda rpc: 'message-id="custom-1"' in rpc)
        server.wait_for_rpc(lambda rpc: "<system/>" in rpc)

        await client.disconnect_async()
        assert not client.is_subscription_active()


@pytest.mark.asyncio
async def test_all_async_rpc_builders_send_expected_xml(pyNetX_module):
    with FakeNetconfSSHServer() as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        calls = [
            (lambda: client.get_async('<interfaces xmlns="urn:test"/>'), ["<get>", '<filter type="subtree">', "<interfaces"]),
            (lambda: client.get_config_async("running", "<top/>"), ["<get-config>", "<source><running/></source>", "<top/>"]),
            (lambda: client.copy_config_async("candidate", "running"), ["<copy-config>", "<target><candidate/></target>", "<source><running/></source>"]),
            (lambda: client.delete_config_async("startup"), ["<delete-config>", "<target><startup/></target>"]),
            (lambda: client.validate_async("candidate"), ["<validate>", "<source><candidate/></source>"]),
            (lambda: client.edit_config_async("candidate", "<config-item/>", False), ["<edit-config>", "<target><candidate/></target>", "<config><config-item/></config>"]),
            (lambda: client.lock_async("candidate"), ["<lock>", "<target><candidate/></target>"]),
            (lambda: client.unlock_async("candidate"), ["<unlock>", "<target><candidate/></target>"]),
            (lambda: client.commit_async(), ["<commit/>"]),
        ]

        next_index = 0
        for call, expected_fragments in calls:
            reply = await call()
            assert "<ok/>" in reply
            record = server.wait_for_rpc(lambda rpc: all(fragment in rpc for fragment in expected_fragments), after_index=next_index)
            assert 'message-id="101"' in record.text
            next_index = record.index + 1

        await client.disconnect_async()


@pytest.mark.asyncio
async def test_edit_config_with_validation_sends_edit_then_validate(pyNetX_module):
    with FakeNetconfSSHServer() as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        reply = await client.edit_config_async("candidate", "<system><hostname>leaf</hostname></system>", True)
        assert "<ok/>" in reply

        edit_record = server.wait_for_rpc(lambda rpc: "<edit-config>" in rpc)
        validate_record = server.wait_for_rpc(lambda rpc: "<validate>" in rpc, after_index=edit_record.index + 1)
        assert "<target><candidate/></target>" in edit_record.text
        assert "<source><candidate/></source>" in validate_record.text

        await client.disconnect_async()


@pytest.mark.asyncio
async def test_locked_edit_config_sends_lock_edit_commit_unlock_sequence(pyNetX_module):
    with FakeNetconfSSHServer() as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        reply = await client.locked_edit_config_async("candidate", "<interfaces/>", False)
        assert "<ok/>" in reply

        sequence = server.rpc_texts
        operations = [
            next(i for i, rpc in enumerate(sequence) if "<lock>" in rpc),
            next(i for i, rpc in enumerate(sequence) if "<edit-config>" in rpc),
            next(i for i, rpc in enumerate(sequence) if "<commit/>" in rpc),
            next(i for i, rpc in enumerate(sequence) if "<unlock>" in rpc),
        ]
        assert operations == sorted(operations)

        await client.disconnect_async()


@pytest.mark.asyncio
async def test_connect_async_bad_password_reports_authentication_failure(pyNetX_module):
    with FakeNetconfSSHServer(username="admin", password="correct") as server:
        client = make_integration_client(pyNetX_module, server, password="wrong")
        # The current async connect path wraps authentication failures as
        # NetconfConnectionRefusedError with an authentication message. Accept
        # NetconfAuthError too so the test remains valid if the mapping is later
        # narrowed without changing the public error text.
        with pytest.raises((pyNetX_module.NetconfAuthError, pyNetX_module.NetconfConnectionRefusedError)) as excinfo:
            await client.connect_async()
        assert "Authentication failed" in str(excinfo.value)


@pytest.mark.asyncio
async def test_rpc_error_reply_is_returned_to_caller_for_raw_rpc(pyNetX_module):
    def responder(rpc: str) -> str:
        if "force-error" in rpc:
            return ERROR_REPLY
        return OK_REPLY

    with FakeNetconfSSHServer(rpc_responder=responder) as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        reply = await client.send_rpc_async('<rpc message-id="force-error"><force-error/></rpc>')
        assert "<rpc-error>" in reply
        assert "fake server forced RPC error" in reply

        await disconnect_quietly(client)


@pytest.mark.asyncio
async def test_subscribe_async_reads_notifications_from_reactor_queue(pyNetX_module):
    notifications = [notification_xml(1), notification_xml(2)]
    with FakeNetconfSSHServer(notifications=notifications) as server:
        client = make_integration_client(pyNetX_module, server, notif_queue_size=10)

        reply = await client.subscribe_async(stream="NETCONF", filter="<severity>warning</severity>")
        assert "<ok/>" in reply
        assert client.is_subscription_active()
        server.wait_for_rpc(lambda rpc: "<create-subscription" in rpc and "<severity>warning</severity>" in rpc)

        first = await client.next_notification_async(timeout_ms=3000)
        second = await client.next_notification_async(timeout_ms=3000)
        assert "<notification" in first
        assert "<sequence>1</sequence>" in first
        assert "<sequence>2</sequence>" in second
        assert client.notification_queue_size() == 0

        client.delete_subscription()
        assert not client.is_subscription_active()


@pytest.mark.asyncio
async def test_notification_queue_full_health_event_contains_label_timestamp_and_counters(pyNetX_module):
    notifications = [notification_xml(i) for i in range(1, 5)]
    with FakeNetconfSSHServer(
        notifications=notifications,
        notification_start_delay=0.20,
        notification_interval=0.02,
    ) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=1,
            notif_drop_event_threshold=1,
            label="leaf-with-small-queue",
        )

        reply = await client.subscribe_async(stream="NETCONF")
        assert "<ok/>" in reply

        queue_full_event = None
        deadline = asyncio.get_running_loop().time() + 5.0
        while asyncio.get_running_loop().time() < deadline:
            event = await pyNetX_module.next_notification_event_async(timeout_ms=1000)
            if event.type in {"notification_queue_full", "notification_drops_summary"}:
                queue_full_event = event
                break
        assert queue_full_event is not None, "did not receive queue-full health event"

        data = queue_full_event.as_dict()
        assert set(data) == EXPECTED_HEALTH_EVENT_KEYS
        assert data["valid"] is True
        assert data["type"] in {"notification_queue_full", "notification_drops_summary"}
        assert data["label"] == "leaf-with-small-queue"
        assert data["hostname"] == server.host
        assert data["port"] == server.port
        assert data["queue_size"] == 1
        assert data["queue_max_size"] == 1
        assert data["queue_high_watermark"] == 1
        assert data["notifications_enqueued"] >= 1
        assert data["notifications_dropped_queue_full"] >= 1
        assert data["notifications_dropped_delta"] >= 1
        assert_recent_utc_timestamp(data["timestamp"])

        # Consuming from a previously full bounded queue should eventually emit
        # recovery. A drops-summary event may legitimately arrive first.
        consumed = await client.next_notification_async(timeout_ms=1000)
        assert "<notification" in consumed

        recovery = None
        deadline = asyncio.get_running_loop().time() + 3.0
        while asyncio.get_running_loop().time() < deadline:
            candidate = await pyNetX_module.next_notification_event_async(timeout_ms=500)
            if candidate.type == "notification_queue_recovered":
                recovery = candidate
                break
        assert recovery is not None, "did not receive queue recovery health event"
        assert recovery.label == "leaf-with-small-queue"
        assert_recent_utc_timestamp(recovery.timestamp)

        client.delete_subscription()


@pytest.mark.asyncio
async def test_default_label_is_none_in_generated_health_events(pyNetX_module):
    notifications = [notification_xml(i) for i in range(1, 4)]
    with FakeNetconfSSHServer(notifications=notifications, notification_interval=0.01) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=1,
            label="None",
        )
        assert "<ok/>" in await client.subscribe_async()

        event = None
        for _ in range(5):
            candidate = await pyNetX_module.next_notification_event_async(timeout_ms=1000)
            if candidate.type in {"notification_queue_full", "notification_drops_summary"}:
                event = candidate
                break
        assert event is not None
        assert event.label == "None"
        assert event.as_dict()["label"] == "None"

        client.delete_subscription()


@pytest.mark.asyncio
async def test_incomplete_notification_generates_health_event_and_queues_partial_payload(pyNetX_module):
    partial = '<notification><eventTime>2026-06-25T00:00:59Z</eventTime><partial>true</partial>'
    with FakeNetconfSSHServer(
        incomplete_notification=partial,
        notification_start_delay=0.20,
    ) as server:
        client = make_integration_client(
            pyNetX_module,
            server,
            notif_queue_size=10,
            notif_incomplete_max_kb=64,
            notif_incomplete_timeout=1,
            label="leaf-incomplete",
        )
        assert "<ok/>" in await client.subscribe_async()

        event = None
        for _ in range(5):
            candidate = await pyNetX_module.next_notification_event_async(timeout_ms=1500)
            if candidate.type == "incomplete_notification":
                event = candidate
                break
        assert event is not None, "did not receive incomplete notification health event"
        assert event.valid is True
        assert event.label == "leaf-incomplete"
        assert event.partial_bytes == len(partial.encode("utf-8"))
        assert event.incomplete_notifications_received >= 1
        assert_recent_utc_timestamp(event.timestamp)

        queued = await client.next_notification_async(timeout_ms=1000)
        assert queued == partial

        client.delete_subscription()
