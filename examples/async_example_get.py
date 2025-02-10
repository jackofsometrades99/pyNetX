# # examples/async_example.py

import asyncio
import os
import sys
from datetime import datetime
build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'build'))
if build_dir not in sys.path:
    sys.path.insert(0, build_dir)

from netconf_pybind import NetconfClient

COMPONENTS_GLOBAL_DASH_FIL = """
<components xmlns="http://www.ipinfusion.com/yang/ocnos/ipi-platform">
   <component>
      <state>
         <type>storage</type>
         <memory />
      </state>
   </component>
   <component>
      <state>
         <type>ram</type>
         <memory />
      </state>
   </component>
   <component>
      <cpu>
         <state>
            <cpu-utilization />
         </state>
      </cpu>
   </component>
</components>
"""

connection_cache = {}  # (hostname, port, username) -> NetconfClient

async def fetch_data(client, filter_xml=None):
    try:
        await client.connect_async()
        response = await client.get_async(COMPONENTS_GLOBAL_DASH_FIL)
        print(response)
        print("###\n")
        print(str(datetime.now()))
        print("###\n")
    # except netconf_pybind.NETCONFError as e:
    #     print(f"NETCONF Error: {e}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
    finally:
        await client.disconnect_async()

async def main():
    devices = [
        {"hostname": "172.24.30.206", "port": 830, "username": "ocnos", "password": "ocnos"},
        {"hostname": "172.24.30.219", "port": 830, "username": "ocnos", "password": "ocnos"},
        {"hostname": "172.24.30.203", "port": 830, "username": "ocnos", "password": "ocnos"},
        {"hostname": "172.24.30.217", "port": 830, "username": "ocnos", "password": "ocnos"},
        {"hostname": "172.24.30.210", "port": 830, "username": "ocnos", "password": "ocnos"},
        {"hostname": "172.24.30.218", "port": 830, "username": "ocnos", "password": "ocnos"},
        # Add more devices as needed
    ]

    # Create AsyncNetconfClient instances
    clients = [NetconfClient(**device) for device in devices]
    # Create tasks for concurrent execution
    tasks = [fetch_data(client) for client in clients]
    # Run tasks concurrently
    await asyncio.gather(*tasks)
    # client = NetconfClient("172.24.30.206", 830, "ocnos", "ocnos")
    # res = await client.connect_async()        # now it's an awaitable
    # print(res)
    # response = await client.get_async() # also awaitable
    # print("Response:", response)
    # await client.disconnect_async()

if __name__ == "__main__":
    asyncio.run(main())

# import asyncio
# import sys
# import os

# # Add the build directory to sys.path
# build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'build'))
# if build_dir not in sys.path:
#     sys.path.insert(0, build_dir)

# from async_netconf_client import AsyncNetconfClient
# import netconf_pybind

# async def fetch_data(client, filter_xml=None):
#     try:
#         print("4")
#         await client.connect()
#         print("5")
#         print(f"Connected to {client.client.hostname}")
#         response = await client.get(filter_xml)
#         print(f"Response from {client.client.hostname_}:")
#         print(response)
#     except netconf_pybind.NETCONFError as e:
#         print(f"NETCONF Error: {e}")
#     except Exception as e:
#         print(f"An unexpected error occurred: {e}")
#     finally:
#         await client.disconnect()
#         print(f"Disconnected from {client.client.hostname_}")

# async def main():
#     # Define a list of devices to connect to
#     print("1")
#     devices = [
#         {"hostname": "172.24.30.206", "port": 830, "username": "ocnos", "password": "ocnos", "key_path": "a/a/a"},
#         # {"hostname": "172.24.30.250", "port": 830, "username": "ocnos", "password": "ocnos"},
#         # {"hostname": "172.24.30.212", "port": 830, "username": "ocnos", "password": "ocnos"},
#         # Add more devices as needed
#     ]

#     # Create AsyncNetconfClient instances
#     clients = [AsyncNetconfClient(**device) for device in devices]
#     print("2")
#     # Create tasks for concurrent execution
#     tasks = [fetch_data(client) for client in clients]
#     print("3")
#     # Run tasks concurrently
#     await asyncio.gather(*tasks)

# if __name__ == "__main__":
#     asyncio.run(main())
