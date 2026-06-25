Notifications
=============

pyNetX supports NETCONF notification subscriptions through ``subscribe_async()``.
Notifications are received on a separate SSH/NETCONF session from the primary
RPC session.

Basic subscription
------------------

.. code-block:: python

   import asyncio
   import pyNetX

   async def main():
       client = pyNetX.NetconfClient(
           hostname="192.168.1.1",
           username="admin",
           password="admin",
           notif_queue_size=1000,
           label="leaf-01",
       )

       await client.connect_async()
       try:
           reply = await client.subscribe_async(stream="NETCONF")
           print("subscription reply:", reply)

           while client.is_subscription_active():
               notification = await client.next_notification_async(timeout_ms=1000)
               if notification:
                   print(notification)
       finally:
           try:
               client.delete_subscription()
           finally:
               await client.disconnect_async()

   asyncio.run(main())

Queue helpers
-------------

.. list-table::
   :header-rows: 1

   * - API
     - Description
   * - ``client.next_notification(timeout_ms=10)``
     - Synchronous queue read. Releases the Python GIL while waiting.
   * - ``await client.next_notification_async(timeout_ms=10)``
     - Awaitable queue read.
   * - ``client.peek_notifications(max_items=100)``
     - Inspect queued notifications without consuming them. Use ``-1`` for all queued items.
   * - ``client.notification_queue_size()``
     - Return the current queue depth.
   * - ``client.is_subscription_active()``
     - Return whether the subscription is active.
   * - ``client.delete_subscription()``
     - Close the notification session/subscription. Safe to call during cleanup.

Bounded and unbounded queues
----------------------------

``notif_queue_size=-1`` creates an unbounded notification queue. This is the
default.

A non-negative ``notif_queue_size`` creates a bounded queue. If the queue is
full, pyNetX drops incoming notifications and emits health events such as
``notification_queue_full`` and ``notification_drops_summary``.

Example bounded queue:

.. code-block:: python

   client = pyNetX.NetconfClient(
       hostname="192.168.1.1",
       username="admin",
       password="admin",
       notif_queue_size=1000,
       notif_drop_event_threshold=10,
       label="leaf-01",
   )

Incomplete notification guards
------------------------------

Some devices or broken test targets may send partial notification XML without
the NETCONF ``]]>]]>`` end marker. pyNetX has two guards:

- ``notif_incomplete_max_kb``: maximum partial notification size before a guard fires.
- ``notif_incomplete_timeout``: maximum seconds to wait for EOM after partial data starts.

At least one guard must remain enabled. Do not set both to ``-1``.

When a guard fires, pyNetX emits an ``incomplete_notification`` health event.
