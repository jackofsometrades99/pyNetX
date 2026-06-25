Examples
========

Async multi-device get-config
-----------------------------

.. code-block:: python

   import asyncio
   import pyNetX

   DEVICES = [
       {"hostname": "192.168.1.1", "username": "admin", "password": "admin", "label": "leaf-01"},
       {"hostname": "192.168.1.2", "username": "admin", "password": "admin", "label": "leaf-02"},
   ]

   async def collect_running_config(device):
       client = pyNetX.NetconfClient(
           hostname=device["hostname"],
           username=device["username"],
           password=device["password"],
           label=device["label"],
           connect_timeout=30,
           read_timeout=30,
           socket_connect_timeout=5,
       )
       await client.connect_async()
       try:
           return await client.get_config_async(source="running")
       finally:
           await client.disconnect_async()

   async def main():
       pyNetX.set_threadpool_size(8)
       results = await asyncio.gather(*(collect_running_config(d) for d in DEVICES))
       for result in results:
           print(result)

   asyncio.run(main())

Notification consumer with health monitor
-----------------------------------------

.. code-block:: python

   import asyncio
   import pyNetX

   async def monitor_health():
       while True:
           event = await pyNetX.next_notification_event_async(timeout_ms=-1)
           print("health:", event.as_dict())

   async def consume_device(device):
       client = pyNetX.NetconfClient(
           hostname=device["hostname"],
           username=device["username"],
           password=device["password"],
           notif_queue_size=1000,
           notif_drop_event_threshold=10,
           label=device["label"],
       )

       await client.connect_async()
       try:
           await client.subscribe_async(stream="NETCONF")
           while client.is_subscription_active():
               notification = await client.next_notification_async(timeout_ms=1000)
               if notification:
                   print(device["label"], notification)
       finally:
           try:
               client.delete_subscription()
           finally:
               await client.disconnect_async()

   async def main():
       pyNetX.set_threadpool_size(16)
       pyNetX.set_notification_reactor_count(4)

       device = {
           "hostname": "192.168.1.1",
           "username": "admin",
           "password": "admin",
           "label": "leaf-01",
       }

       await asyncio.gather(
           monitor_health(),
           consume_device(device),
       )

   asyncio.run(main())

Safe edit-config pattern
------------------------

.. code-block:: python

   async def update_config(client, config_xml):
       await client.lock_async("candidate")
       try:
           reply = await client.edit_config_async("candidate", config_xml, do_validate=True)
           commit = await client.commit_async()
           return reply, commit
       finally:
           await client.unlock_async("candidate")

Custom RPC helper
-----------------

.. code-block:: python

   async def get_with_custom_rpc(client):
       rpc = """
       <rpc message-id="101" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
         <get/>
       </rpc>
       """
       return await client.send_rpc_async(rpc)

Queue inspection
----------------

.. code-block:: python

   size = client.notification_queue_size()
   preview = client.peek_notifications(max_items=5)
   all_current = client.peek_notifications(max_items=-1)

Deprecated sync APIs
--------------------

Explicit sync-flow APIs are deprecated. Do not use them in new examples or new
applications. Use the async APIs shown above.
