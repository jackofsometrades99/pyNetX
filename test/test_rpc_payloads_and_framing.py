from __future__ import annotations

import pytest

from fake_netconf_ssh_server import FakeNetconfSSHServer, NETCONF_EOM
from test_integration_fake_netconf_server import make_integration_client

pytestmark = [pytest.mark.integration]


@pytest.mark.asyncio
async def test_fragmented_server_reply_is_read_until_netconf_eom(pyNetX_module):
    with FakeNetconfSSHServer(reply_chunk_size=3, reply_chunk_delay=0.001) as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        reply = await client.send_rpc_async('<rpc message-id="fragmented"><get/></rpc>')
        assert reply.endswith(NETCONF_EOM)
        assert "<rpc-reply" in reply
        assert "<ok/>" in reply

        await client.disconnect_async()


@pytest.mark.asyncio
@pytest.mark.slow
async def test_large_custom_rpc_payload_is_sent_completely(pyNetX_module):
    payload = "x" * (256 * 1024)
    with FakeNetconfSSHServer() as server:
        client = make_integration_client(pyNetX_module, server, read_timeout=10)
        assert await client.connect_async() is True

        reply = await client.send_rpc_async(
            f'<rpc message-id="large-payload"><edit-config><config><blob>{payload}</blob></config></edit-config></rpc>'
        )
        assert "<ok/>" in reply

        record = server.wait_for_rpc(lambda rpc: 'message-id="large-payload"' in rpc)
        assert f"<blob>{payload}</blob>" in record.text
        assert not record.text.endswith(NETCONF_EOM), "fake server stores RPCs without EOM"

        await client.disconnect_async()


@pytest.mark.asyncio
async def test_custom_rpc_with_unicode_payload_round_trips_through_utf8_channel(pyNetX_module):
    with FakeNetconfSSHServer() as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        marker = "Bengaluru-ಬೆಂಗಳೂರು-東京-🚀"
        reply = await client.send_rpc_async(
            f'<rpc message-id="unicode"><get><marker>{marker}</marker></get></rpc>'
        )
        assert "<ok/>" in reply

        record = server.wait_for_rpc(lambda rpc: 'message-id="unicode"' in rpc)
        assert marker in record.text

        await client.disconnect_async()


@pytest.mark.asyncio
async def test_rpc_builder_output_uses_netconf_1_0_eom_on_wire(pyNetX_module):
    # The fake server strips EOM in recorded RPCs. Receiving a complete record is
    # evidence that the client sent the NETCONF 1.0 delimiter expected today.
    with FakeNetconfSSHServer() as server:
        client = make_integration_client(pyNetX_module, server)
        assert await client.connect_async() is True

        assert "<ok/>" in await client.get_async("<system/>")
        record = server.wait_for_rpc(lambda rpc: "<get>" in rpc and "<system/>" in rpc)
        assert "]]>]]>" not in record.text
        assert "<filter type=\"subtree\">" in record.text

        await client.disconnect_async()
