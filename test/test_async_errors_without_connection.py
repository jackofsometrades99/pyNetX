from __future__ import annotations

import pytest

from conftest import assert_await_raises


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("method_name", "args", "expected_message"),
    [
        ("disconnect_async", (), "already not connected"),
        ("send_rpc_async", ("<rpc><get/></rpc>",), "already not connected"),
        ("get_async", (), "already not connected"),
        ("get_config_async", (), "already not connected"),
        ("copy_config_async", ("candidate", "running"), "already not connected"),
        ("delete_config_async", ("startup",), "already not connected"),
        ("validate_async", (), "already not connected"),
        ("edit_config_async", ("candidate", "<config/>", False), "already not connected"),
        ("lock_async", (), "already not connected"),
        ("unlock_async", (), "already not connected"),
        ("commit_async", (), "already not connected"),
        (
            "locked_edit_config_async",
            ("candidate", "<config/>", False),
            "already not connected",
        ),
    ],
)
async def test_async_rpc_flow_methods_reject_unconnected_client(
    make_client,
    pyNetX_module,
    method_name,
    args,
    expected_message,
):
    client = make_client()
    method = getattr(client, method_name)
    message = await assert_await_raises(method(*args), pyNetX_module.NetconfException)
    assert expected_message in message


@pytest.mark.asyncio
async def test_connect_async_to_closed_local_port_raises_connection_refused(
    make_client,
    pyNetX_module,
    unused_tcp_port,
):
    client = make_client(port=unused_tcp_port)
    message = await assert_await_raises(
        client.connect_async(),
        pyNetX_module.NetconfConnectionRefusedError,
    )
    assert "Unable to connect to device" in message


@pytest.mark.asyncio
async def test_subscribe_async_to_closed_local_port_raises_netconf_exception(
    make_client,
    pyNetX_module,
    unused_tcp_port,
):
    client = make_client(port=unused_tcp_port)
    message = await assert_await_raises(
        client.subscribe_async(stream="NETCONF", filter=""),
        pyNetX_module.NetconfException,
    )
    assert "Unable to Subscribe to device" in message
    assert not client.is_subscription_active()
