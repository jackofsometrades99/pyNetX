# async_netconf_client.py

import netconf_pybind

class AsyncNetconfClient:
    def __init__(self, hostname, port=830, username="", password="", key_path=""):
        self.client = netconf_pybind.NetconfClient(hostname, port, username, password, key_path)

    async def connect(self):
        print(self.client)
        return await self.client.connect_async()

    async def disconnect(self):
        return await self.client.disconnect_async()

    async def send_rpc(self, rpc):
        return await self.client.send_rpc_async(rpc)

    async def get(self, filter_xml=""):
        return await self.client.get_async(filter_xml)
