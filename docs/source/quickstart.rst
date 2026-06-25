Quick start
===========

Async get-config
----------------

.. code-block:: python

   import asyncio
   import pyNetX

   async def main():
       client = pyNetX.NetconfClient(
           hostname="192.168.1.1",
           port=830,
           username="admin",
           password="admin",
           connect_timeout=30,
           read_timeout=30,
           socket_connect_timeout=5,
           notif_queue_size=1000,
           label="leaf-01",
       )

       await client.connect_async()
       try:
           running = await client.get_config_async(source="running")
           print(running)
       finally:
           await client.disconnect_async()

   asyncio.run(main())

Custom RPC
----------

.. code-block:: python

   import asyncio
   import pyNetX

   async def main():
       client = pyNetX.NetconfClient(
           hostname="192.168.1.1",
           username="admin",
           password="admin",
           label="leaf-01",
       )

       await client.connect_async()
       try:
           rpc = """
           <rpc message-id="101" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
             <get/>
           </rpc>
           """
           reply = await client.send_rpc_async(rpc)
           print(reply)
       finally:
           await client.disconnect_async()

   asyncio.run(main())

Do not include the NETCONF ``]]>]]>`` end marker. pyNetX appends it internally.

Concurrent devices
------------------

Use one ``NetconfClient`` per device. Operations on the same client are
serialized on the primary RPC channel, but different clients can run in
parallel.

.. code-block:: python

   import asyncio
   import pyNetX

   DEVICES = [
       {"hostname": "192.168.1.1", "username": "admin", "password": "admin", "label": "leaf-01"},
       {"hostname": "192.168.1.2", "username": "admin", "password": "admin", "label": "leaf-02"},
   ]

   async def collect(device):
       client = pyNetX.NetconfClient(**device)
       await client.connect_async()
       try:
           return await client.get_config_async(source="running")
       finally:
           await client.disconnect_async()

   async def main():
       pyNetX.set_threadpool_size(8)
       replies = await asyncio.gather(*(collect(d) for d in DEVICES))
       for reply in replies:
           print(reply)

   asyncio.run(main())

Basic error handling
--------------------

.. code-block:: python

   try:
       await client.connect_async()
   except pyNetX.NetconfConnectionRefusedError:
       print("Connection refused or unreachable")
   except pyNetX.NetconfAuthError:
       print("Authentication failed")
   except pyNetX.NetconfChannelError:
       print("NETCONF channel setup failed")
   except pyNetX.NetconfException as exc:
       print("NETCONF failure:", exc)
