Migration guide
===============

Migrating from sync-flow APIs
-----------------------------

Starting with pyNetX v2.0.5, explicit synchronous flow APIs are deprecated.
They remain available for compatibility but should not be used in new code.

Before:

.. code-block:: python

   client.connect_sync()
   reply = client.get_config_sync(source="running")
   client.disconnect_sync()

After:

.. code-block:: python

   await client.connect_async()
   reply = await client.get_config_async(source="running")
   await client.disconnect_async()

Deprecated methods
~~~~~~~~~~~~~~~~~~

- ``connect_sync``
- ``disconnect_sync``
- ``send_rpc_sync``
- ``receive_notification_sync``
- ``get_sync``
- ``get_config_sync``
- ``copy_config_sync``
- ``delete_config_sync``
- ``validate_sync``
- ``edit_config_sync``
- ``subscribe_sync``
- ``lock_sync``
- ``unlock_sync``
- ``commit_sync``
- ``locked_edit_config_sync``

Still-supported helper APIs
~~~~~~~~~~~~~~~~~~~~~~~~~~~

These APIs are not deprecated:

- ``next_notification``
- ``next_notification_async``
- ``peek_notifications``
- ``notification_queue_size``
- ``is_subscription_active``
- ``delete_subscription``
- ``next_notification_event``
- ``next_notification_event_async``
- ``pending_notification_event_count``
- ``clear_notification_events``
- ``set_threadpool_size``
- ``set_notification_reactor_count``

Adopting labels in v2.0.6
-------------------------

Before v2.0.6, health events identified devices by hostname and port. Starting
with v2.0.6, pass a label during client construction:

.. code-block:: python

   client = pyNetX.NetconfClient(
       hostname="172.24.30.116",
       username="admin",
       password="admin",
       label="blr-dc1-leaf-01",
   )

Health events now include:

.. code-block:: python

   {
       "label": "blr-dc1-leaf-01",
       "hostname": "172.24.30.116",
       "timestamp": "2026-06-25T05:35:15.123Z",
   }

If no label is provided, the default label is ``"None"``.

Adopting timestamps in v2.0.6
-----------------------------

Consumers of ``event.as_dict()`` should expect a new ``timestamp`` key. The
value is a UTC ISO-8601 string with millisecond precision and ``Z`` suffix.

Timeout events are not associated with a specific client and use
``label == "None"``.
