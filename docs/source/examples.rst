Examples
========

Below are two extended examples demonstrating how to interact with
pyNetX both **synchronously** and **asynchronously** across multiple devices.

Synchronous Multi-Device Example
--------------------------------

In this example, we iterate over a list of devices, connect to each using
the synchronous API, fetch the ``running`` configuration, then subscribe
to notifications. We listen for notifications for a short period and
finally disconnect. This entire process repeats every 60 seconds in
an infinite loop.

.. code-block:: python

   import time
   from pyNetX import NetconfClient

   def sync_multi_device_example():
       # A sample list of devices to manage
       devices = [
           {"hostname": "192.168.1.1", "port": 830, "username": "admin", "password": "admin"},
           {"hostname": "192.168.1.2", "port": 830, "username": "admin", "password": "admin"},
       ]

       while True:
           for dev in devices:
               client = NetconfClient(
                   hostname=dev["hostname"],
                   port=dev["port"],
                   username=dev["username"],
                   password=dev["password"]
               )

               # Connect to the NETCONF server
               client.connect_sync()
               print(f"Connected to {dev['hostname']}")

               # Retrieve the running configuration
               running_config = client.get_config_sync(source="running")
               print(f"[{dev['hostname']}] Running Config:\n{running_config}\n")

               # Subscribe to NETCONF notifications
               client.subscribe_sync(stream="NETCONF")
               print(f"Subscribed to notifications on {dev['hostname']}")

               # Receive notifications for a short duration (e.g., 5 seconds)
               start_time = time.time()
               while (time.time() - start_time) < 5:
                   notification = client.receive_notification_sync()
                   if notification:
                       print(f"[{dev['hostname']}] Notification:\n{notification}\n")

               # Disconnect
               client.disconnect_sync()
               print(f"Disconnected from {dev['hostname']}\n")

           # Wait for 60 seconds before reconnecting
           print("Waiting 60 seconds before next iteration...\n")
           time.sleep(60)

   if __name__ == "__main__":
       sync_multi_device_example()


Asynchronous Multi-Device Example
---------------------------------

This example uses Python’s ``asyncio`` to connect to multiple devices
concurrently. We fetch each device’s running configuration, subscribe
to notifications, and continuously read them for a short duration.
Then we disconnect and repeat the cycle every 60 seconds. Because it is
async, you can potentially expand this to handle hundreds of devices
in parallel (subject to resource and performance constraints).

.. code-block:: python

   import asyncio
   from pyNetX import NetconfClient

   async def async_multi_device_example():
       # A sample list of devices to manage
       devices = [
           {"hostname": "192.168.1.1", "port": 830, "username": "admin", "password": "admin"},
           {"hostname": "192.168.1.2", "port": 830, "username": "admin", "password": "admin"},
       ]

       while True:
           # Connect to each device, fetch config, subscribe, receive notifications
           for dev in devices:
               client = NetconfClient(
                   hostname=dev["hostname"],
                   port=dev["port"],
                   username=dev["username"],
                   password=dev["password"]
               )

               # Asynchronously connect
               await client.connect_async()
               print(f"Connected to {dev['hostname']}")

               # Retrieve the running configuration
               running_config = await client.get_config_async(source="running")
               print(f"[{dev['hostname']}] Running Config:\n{running_config}\n")

               # Subscribe to NETCONF notifications
               await client.subscribe_async(stream="NETCONF")
               print(f"Subscribed to notifications on {dev['hostname']}")

               # Asynchronously receive notifications for a short duration
               end_time = asyncio.get_event_loop().time() + 5  # e.g., 5 seconds
               while asyncio.get_event_loop().time() < end_time:
                   notification = await client.next_notification()
                   if notification:
                       print(f"[{dev['hostname']}] Notification:\n{notification}\n")

               # Disconnect
               await client.disconnect_async()
               print(f"Disconnected from {dev['hostname']}\n")

           # Wait 60 seconds before next iteration
           print("Waiting 60 seconds before next iteration...\n")
           await asyncio.sleep(60)

   if __name__ == "__main__":
       asyncio.run(async_multi_device_example())


Notes & Customization
---------------------
- **Device List**: Update the hostname, port, username, and password in
  these scripts to match your environment.  
- **Timing & Retries**: Adjust how long you listen for notifications
  (in these examples, 5 seconds) or how long you wait (60 seconds) between
  cycles.  
- **Notification Handling**: In a real production scenario, you might
  parse or log notifications to a database rather than just printing them.  
- **Error Handling**: Consider catching exceptions like
  ``NetconfAuthError`` or ``NetconfConnectionRefusedError`` around
  the connect statements.  
- **Thread Pool Size**: If you need to handle many devices concurrently
  in the async example, you might want to increase the global thread pool
  size by calling ``pyNetX.set_threadpool_size(n)`` before creating
  any clients.

These examples serve as a starting point for building more robust
network management tools using **pyNetX**.
