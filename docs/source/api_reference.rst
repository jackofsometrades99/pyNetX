API reference
=============

This page documents the public Python API exposed by the ``pyNetX`` package in
v2.0.6.

Constructor
-----------

.. code-block:: python

   client = pyNetX.NetconfClient(
       hostname="192.168.1.1",
       port=830,
       username="admin",
       password="admin",
       key_path="",
       connect_timeout=60,
       read_timeout=60,
       notif_queue_size=-1,
       socket_connect_timeout=5,
       notif_incomplete_max_kb=1024,
       notif_incomplete_timeout=5,
       notif_drop_event_threshold=1,
       label="None",
   )

Parameters
~~~~~~~~~~

.. list-table::
   :header-rows: 1

   * - Parameter
     - Default
     - Description
   * - ``hostname``
     - required
     - NETCONF device hostname or IP address.
   * - ``port``
     - ``830``
     - NETCONF SSH port.
   * - ``username``
     - required
     - SSH username.
   * - ``password``
     - required
     - SSH password.
   * - ``key_path``
     - ``""``
     - Reserved for key-based authentication. Password authentication is the implemented path in v2.0.6.
   * - ``connect_timeout``
     - ``60``
     - Overall connection/session setup timeout.
   * - ``read_timeout``
     - ``60``
     - Read timeout for RPC replies and NETCONF messages. The public constructor requires this to be greater than zero.
   * - ``notif_queue_size``
     - ``-1``
     - Per-client notification queue size. ``-1`` means unbounded; non-negative values bound the queue.
   * - ``socket_connect_timeout``
     - ``5``
     - TCP socket connect timeout. Must be greater than zero and less than or equal to ``connect_timeout``.
   * - ``notif_incomplete_max_kb``
     - ``1024``
     - Maximum partial notification size, in KiB. Use ``-1`` to disable this guard.
   * - ``notif_incomplete_timeout``
     - ``5``
     - Maximum seconds to wait for notification EOM after partial data starts. Use ``-1`` to disable this guard.
   * - ``notif_drop_event_threshold``
     - ``1``
     - Additional queue-full drops before another summary health event is emitted. Must be greater than zero.
   * - ``label``
     - ``"None"``
     - User-defined string copied into notification health events.

At least one incomplete-notification guard must remain enabled.

Recommended async methods
-------------------------

``connect_async()``
~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   await client.connect_async()

Opens the primary NETCONF-over-SSH session.

Returns ``True`` on success.

``disconnect_async()``
~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   await client.disconnect_async()

Closes the primary session and performs cleanup.

``send_rpc_async(rpc)``
~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   reply = await client.send_rpc_async(rpc)

Sends a raw NETCONF RPC XML payload. Do not include the NETCONF ``]]>]]>`` end
marker.

``get_async(filter="")``
~~~~~~~~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<get>``.

``get_config_async(source="running", filter="")``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<get-config>``.

``copy_config_async(target, source)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<copy-config>``.

``delete_config_async(target)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<delete-config>``.

``validate_async(source="running")``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<validate>``.

``edit_config_async(target, config, do_validate=False)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<edit-config>``. When ``do_validate`` is true, pyNetX also runs
validation according to the library implementation.

``subscribe_async(stream="NETCONF", filter="")``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Creates a NETCONF notification subscription on a separate notification session.
Returns the subscription RPC reply.

``lock_async(target="running")``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<lock>``.

``unlock_async(target="running")``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<unlock>``.

``commit_async()``
~~~~~~~~~~~~~~~~~~

Runs NETCONF ``<commit>``.

``locked_edit_config_async(target, config, do_validate=False)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs a lock/edit/optional-validate/commit/unlock style flow according to the
library implementation.

Notification helper methods
---------------------------

These methods are supported and are not part of the sync-flow deprecation.

``next_notification(timeout_ms=10)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Synchronous helper that reads from the internal notification queue. It releases
the Python GIL while waiting.

``next_notification_async(timeout_ms=10)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Awaitable notification queue read.

``peek_notifications(max_items=100)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns queued notifications without consuming them. Use ``max_items=-1`` for
all currently queued notifications.

``notification_queue_size()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns the current queued notification count.

``is_subscription_active()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns whether the notification subscription/session is active.

``delete_subscription()``
~~~~~~~~~~~~~~~~~~~~~~~~~

Deletes/closes the notification subscription session. This is useful in cleanup
blocks and is not deprecated.

Notification health event APIs
------------------------------

``next_notification_event(timeout_ms=-1)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Synchronously reads the next process-wide health event. ``-1`` waits
indefinitely.

``next_notification_event_async(timeout_ms=-1)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Awaitable health event read. ``-1`` waits indefinitely.

``pending_notification_event_count()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns the number of queued health events.

``clear_notification_events()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Clears queued health events and resets the health-event dropped counter.

Global configuration APIs
-------------------------

``set_threadpool_size(n)``
~~~~~~~~~~~~~~~~~~~~~~~~~~

Configures the shared C++ worker pool size. Call during process startup before
active operations.

``set_notification_reactor_count(n)``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Configures the number of epoll notification reactor threads. Call during process
startup before active subscriptions.

NotificationHealthEvent
-----------------------

Readonly attributes:

.. code-block:: python

   event.valid
   event.type
   event.timestamp
   event.label
   event.hostname
   event.port
   event.fd
   event.message
   event.queue_size
   event.queue_max_size
   event.queue_high_watermark
   event.notifications_enqueued
   event.notifications_dropped_queue_full
   event.notifications_dropped_delta
   event.incomplete_notifications_received
   event.partial_bytes
   event.health_events_dropped

Dictionary conversion:

.. code-block:: python

   data = event.as_dict()

Exceptions
----------

Public exception classes:

.. code-block:: python

   pyNetX.NetconfException
   pyNetX.NetconfConnectionRefusedError
   pyNetX.NetconfAuthError
   pyNetX.NetconfChannelError

Deprecated sync-flow methods
----------------------------

The following methods remain available for compatibility but are deprecated:

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

Use the async equivalents for new code.
