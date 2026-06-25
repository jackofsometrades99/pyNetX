from __future__ import annotations

import asyncio

import pytest

from fake_netconf_ssh_server import FakeNetconfSSHServer, notification_xml
from test_integration_fake_netconf_server import disconnect_quietly, make_integration_client

pytestmark = [pytest.mark.integration, pytest.mark.slow]


@pytest.mark.asyncio
async def test_concurrent_raw_rpcs_on_one_client_all_complete_and_are_serialized(pyNetX_module):
    """Concurrent asyncio callers should not corrupt a single NETCONF channel.

    The C++ client serializes operations with the session mutex. This test does
    not require a strict ordering, but every concurrent caller must receive a
    complete reply and every RPC must arrive at the fake server exactly once.
    """
    pyNetX_module.set_threadpool_size(4)

    with FakeNetconfSSHServer() as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        async def send_one(index: int) -> str:
            return await client.send_rpc_async(
                f'<rpc message-id="concurrent-{index}"><get><marker>{index}</marker></get></rpc>'
            )

        replies = await asyncio.gather(*(send_one(i) for i in range(10)))
        assert all("<ok/>" in reply for reply in replies)

        texts = server.rpc_texts
        for i in range(10):
            matching = [rpc for rpc in texts if f'message-id="concurrent-{i}"' in rpc]
            assert len(matching) == 1, f"RPC concurrent-{i} was not recorded exactly once"
            assert f"<marker>{i}</marker>" in matching[0]

        await client.disconnect_async()


@pytest.mark.asyncio
async def test_primary_rpc_session_remains_usable_after_notification_subscription(pyNetX_module):
    """Notifications use a separate session; primary RPCs should still work."""
    with FakeNetconfSSHServer(notifications=[notification_xml(1)]) as server:
        client = make_integration_client(pyNetX_module, server, notif_queue_size=10)
        assert await client.connect_async() is True
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")
        assert client.is_subscription_active()

        reply = await client.get_config_async("running", "<interfaces/>")
        assert "<ok/>" in reply

        server.wait_for_rpc(lambda rpc: "<create-subscription" in rpc)
        server.wait_for_rpc(lambda rpc: "<get-config>" in rpc and "<interfaces/>" in rpc)

        notification = await client.next_notification_async(timeout_ms=3000)
        assert "<sequence>1</sequence>" in notification

        await client.disconnect_async()
        assert not client.is_subscription_active()


@pytest.mark.asyncio
async def test_disconnect_async_after_subscription_is_idempotent_enough_for_cleanup(pyNetX_module):
    with FakeNetconfSSHServer(notifications=[notification_xml(1), notification_xml(2)]) as server:
        client = make_integration_client(pyNetX_module, server, notif_queue_size=10)
        assert await client.connect_async() is True
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")
        assert client.is_subscription_active()

        await client.disconnect_async()
        assert not client.is_subscription_active()

        # A second disconnect is the documented unconnected error path, but it
        # must not crash or leave the object in an active subscription state.
        with pytest.raises(pyNetX_module.NetconfException):
            await client.disconnect_async()
        assert not client.is_subscription_active()


@pytest.mark.asyncio
async def test_two_clients_receive_notifications_and_health_events_independently(pyNetX_module):
    with FakeNetconfSSHServer(notifications=[notification_xml(1, "<device>A</device>")]) as server_a:
        with FakeNetconfSSHServer(notifications=[notification_xml(1, "<device>B</device>")]) as server_b:
            client_a = make_integration_client(
                pyNetX_module,
                server_a,
                label="leaf-A",
                notif_queue_size=10,
            )
            client_b = make_integration_client(
                pyNetX_module,
                server_b,
                label="leaf-B",
                notif_queue_size=10,
            )

            assert "<ok/>" in await client_a.subscribe_async(stream="NETCONF")
            assert "<ok/>" in await client_b.subscribe_async(stream="NETCONF")

            notif_a, notif_b = await asyncio.gather(
                client_a.next_notification_async(timeout_ms=3000),
                client_b.next_notification_async(timeout_ms=3000),
            )
            assert "<device>A</device>" in notif_a
            assert "<device>B</device>" in notif_b

            client_a.delete_subscription()
            client_b.delete_subscription()


@pytest.mark.asyncio
async def test_delete_subscription_then_resubscribe_on_same_client(pyNetX_module):
    with FakeNetconfSSHServer(
        notifications=[notification_xml(1), notification_xml(2)],
        notification_interval=0.02,
    ) as server:
        client = make_integration_client(pyNetX_module, server, notif_queue_size=10)

        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")
        assert client.is_subscription_active()
        first = await client.next_notification_async(timeout_ms=3000)
        assert "<sequence>1</sequence>" in first

        client.delete_subscription()
        assert not client.is_subscription_active()

        # Resubscribe creates a new notification session and should not depend on
        # stale state from the deleted subscription.
        assert "<ok/>" in await client.subscribe_async(stream="NETCONF")
        assert client.is_subscription_active()
        server.wait_for_rpc(lambda rpc: "<create-subscription" in rpc, after_index=1)

        client.delete_subscription()
        await disconnect_quietly(client)
