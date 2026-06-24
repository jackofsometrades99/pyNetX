API Reference
=============

This section documents the public pyNetX API exposed by the ``pyNetX`` Python
package. The main class is ``NetconfClient``. Module-level functions configure
shared worker/reactor behavior and expose the notification health event stream.

Use keyword arguments when constructing clients. The C++ binding exposes
``port`` before ``username`` and ``password``, so keyword arguments avoid
positional-order confusion.

Usage Examples
--------------

Deprecated synchronous usage:

.. warning::

   Deprecated since pyNetX 2.0.5. The explicit synchronous flow APIs remain
   available for compatibility but will be removed in a future major release.
   New code should use the async APIs.

.. code-block:: python

   from pyNetX import NetconfClient

   client = NetconfClient(
       hostname="192.168.1.1",
       port=830,
       username="admin",
       password="admin",
       connect_timeout=30,
       read_timeout=30,
       notif_queue_size=100,
       socket_connect_timeout=5,
       notif_incomplete_max_kb=1024,
       notif_incomplete_timeout=5,
       notif_drop_event_threshold=1,
   )

   client.connect_sync()
   response = client.get_config_sync(source="running")
   client.disconnect_sync()

Asynchronous usage:

.. code-block:: python

   import asyncio
   from pyNetX import NetconfClient

   async def main():
       client = NetconfClient(
           hostname="192.168.1.1",
           port=830,
           username="admin",
           password="admin",
           connect_timeout=30,
           read_timeout=30,
           notif_queue_size=100,
           socket_connect_timeout=5,
           notif_incomplete_max_kb=1024,
           notif_incomplete_timeout=5,
           notif_drop_event_threshold=1,
       )

       await client.connect_async()
       response = await client.get_config_async(source="running")
       await client.disconnect_async()

   asyncio.run(main())

Deprecation Notice: Synchronous Flow APIs
----------------------------------------

Starting with pyNetX 2.0.5, the explicit synchronous flow APIs are deprecated
and will be removed in a future major release. pyNetX is moving toward an
async-focused API for connection handling, RPC execution, configuration
operations, and subscriptions.

Deprecated methods include:

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

Use the corresponding async methods instead:

- ``connect_async``
- ``disconnect_async``
- ``send_rpc_async``
- ``get_async``
- ``get_config_async``
- ``copy_config_async``
- ``delete_config_async``
- ``validate_async``
- ``edit_config_async``
- ``subscribe_async``
- ``lock_async``
- ``unlock_async``
- ``commit_async``
- ``locked_edit_config_async``

The following common/helper APIs are not part of this deprecation:

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

Example migration:

.. code-block:: python

   # Deprecated
   client.connect_sync()
   reply = client.get_config_sync(source="running")
   client.disconnect_sync()

   # Recommended
   await client.connect_async()
   reply = await client.get_config_async(source="running")
   await client.disconnect_async()

Release Notes for v2.0.5
------------------------

The v2.0.5 release adds notification observability and safer notification
reactor behavior while keeping existing NETCONF RPC APIs backward compatible.

Highlights:

- Added a process-wide notification health event stream.
- Added ``NotificationHealthEvent``.
- Added ``next_notification_event(timeout_ms=-1)``.
- Added ``next_notification_event_async(timeout_ms=-1)``.
- Added ``pending_notification_event_count()``.
- Added ``clear_notification_events()``.
- Added ``NetconfClient.next_notification_async(timeout_ms=10)``.
- ``NetconfClient.next_notification(timeout_ms=10)`` now releases the Python GIL
  while waiting on the internal notification queue.
- Added incomplete-notification guards:
  ``notif_incomplete_max_kb`` and ``notif_incomplete_timeout``.
- Added queue inspection helpers:
  ``peek_notifications(max_items=100)`` and ``notification_queue_size()``.
- Added ``notif_drop_event_threshold`` to control queue-full health event
  frequency. The default is ``1``.
- Deprecated the explicit synchronous flow APIs. They remain available for
  compatibility in v2.0.5, but new code should use the async APIs.

The incomplete-notification guards prevent the notification reactor from being
stuck indefinitely when a device sends partial notification data without the
NETCONF ``]]>]]>`` end marker. When a guard fires, pyNetX returns the partial
notification to the queue and emits an ``incomplete_notification`` health event.

Constructor
-----------

.. code-block:: python

   client = NetconfClient(
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
   )

**Parameters**

- **hostname** (str): NETCONF device hostname or IP address.
- **port** (int): NETCONF SSH port. Defaults to ``830``.
- **username** (str): SSH username.
- **password** (str): SSH password.
- **key_path** (str): Reserved for key-based authentication. Password
  authentication is the currently implemented authentication path.
- **connect_timeout** (int): Maximum time, in seconds, allowed for full
  connection setup. Defaults to ``60``.
- **read_timeout** (int): Maximum inactivity time, in seconds, while waiting
  for NETCONF RPC replies. Defaults to ``60``.
- **notif_queue_size** (int): Maximum queued notifications per client.
  ``-1`` means unbounded. A non-negative value bounds the queue. Defaults to
  ``-1``.
- **socket_connect_timeout** (int): Socket-level TCP connect timeout, in
  seconds. Defaults to ``5``. It must be greater than ``0`` and cannot be
  greater than ``connect_timeout``.
- **notif_incomplete_max_kb** (int): Maximum partial notification size, in KiB,
  before pyNetX returns the partial notification and emits a health event.
  Defaults to ``1024``. Use ``-1`` to disable the size guard.
- **notif_incomplete_timeout** (int): Maximum time, in seconds, to wait for a
  NETCONF notification EOM marker after partial notification data starts
  arriving. Defaults to ``5``. Use ``-1`` to disable the time guard.
- **notif_drop_event_threshold** (int): Number of additional queue-full drops
  before another queue-full health event is emitted while the queue remains
  full. Defaults to ``1``. Must be greater than ``0``.

At least one incomplete-notification guard must remain enabled. Therefore,
``notif_incomplete_max_kb`` and ``notif_incomplete_timeout`` cannot both be
``-1``.

Synchronous Methods Deprecated
-------------------------------

The methods in this section are deprecated since pyNetX 2.0.5. They remain
available for compatibility, but they will be removed in a future major release.
Use the corresponding async methods for new code. Common/helper APIs such as
``next_notification()`` are not part of this deprecation.

connect_sync()
~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``connect_async()`` instead.

Establishes a synchronous NETCONF session.

**Returns**
- ``bool``: ``True`` on success.


disconnect_sync()
~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``disconnect_async()`` instead.

Closes the active NETCONF session.

**Returns**
- ``None``.


send_rpc_sync(rpc)
~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``send_rpc_async(rpc)`` instead.

Sends a custom RPC XML string and returns the RPC reply.

**Parameters**
- **rpc** (str): Full NETCONF ``<rpc>...</rpc>`` XML payload. Do not include
  the NETCONF end marker; pyNetX appends it internally.

**Returns**
- ``str``: RPC reply XML.


get_sync(filter="")
~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``get_async(filter="")`` instead.

Performs a NETCONF ``<get>`` operation.

**Parameters**
- **filter** (str): Optional XML filter.

**Returns**
- ``str``: Reply XML.


get_config_sync(source="running", filter="")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``get_config_async(source="running", filter="")`` instead.

Retrieves configuration from a datastore.

**Parameters**
- **source** (str): Datastore name. Defaults to ``"running"``.
- **filter** (str): Optional XML filter.

**Returns**
- ``str``: Reply XML.


copy_config_sync(target, source)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``copy_config_async(target, source)`` instead.

Copies configuration from ``source`` to ``target``.

**Parameters**
- **target** (str): Target datastore.
- **source** (str): Source datastore or source config.

**Returns**
- ``str``: RPC reply XML.


delete_config_sync(target)
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``delete_config_async(target)`` instead.

Deletes the specified datastore.

**Parameters**
- **target** (str): Datastore to delete.

**Returns**
- ``str``: RPC reply XML.


validate_sync(source="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``validate_async(source="running")`` instead.

Validates the specified datastore.

**Parameters**
- **source** (str): Datastore to validate. Defaults to ``"running"``.

**Returns**
- ``str``: RPC reply XML.


edit_config_sync(target, config, do_validate=False)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``edit_config_async(target, config, do_validate=False)`` instead.

Edits the specified datastore.

**Parameters**
- **target** (str): Target datastore.
- **config** (str): XML configuration snippet.
- **do_validate** (bool): Validate after edit. Defaults to ``False``.

**Returns**
- ``str``: RPC reply XML.


subscribe_sync(stream="NETCONF", filter="")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``subscribe_async(stream="NETCONF", filter="")`` instead.

Creates a NETCONF notification subscription.

**Parameters**
- **stream** (str): Notification stream. Defaults to ``"NETCONF"``.
- **filter** (str): Optional subtree XML filter.

**Returns**
- ``str``: Subscription RPC reply XML.


receive_notification_sync()
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``next_notification_async()`` for
   awaitable notification consumption, or ``next_notification()`` for the
   common helper API that releases the GIL while waiting.

Receives a notification using the legacy synchronous notification receive path.
For new code using the reactor-backed notification queue, prefer
``next_notification()`` or ``next_notification_async()`` after subscribing.

**Returns**
- ``str``: Notification XML.


lock_sync(target="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``lock_async(target="running")`` instead.

Locks a datastore.

**Parameters**
- **target** (str): Datastore to lock. Defaults to ``"running"``.

**Returns**
- ``str``: RPC reply XML.


unlock_sync(target="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``unlock_async(target="running")`` instead.

Unlocks a datastore.

**Parameters**
- **target** (str): Datastore to unlock. Defaults to ``"running"``.

**Returns**
- ``str``: RPC reply XML.


commit_sync()
~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``commit_async()`` instead.

Commits pending candidate datastore changes where supported by the device.

**Returns**
- ``str``: RPC reply XML.


locked_edit_config_sync(target, config, do_validate=False)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. warning::

   Deprecated since pyNetX 2.0.5. Use ``locked_edit_config_async(target, config, do_validate=False)`` instead.

Locks the target datastore, edits configuration, optionally validates, and then
unlocks.

**Parameters**
- **target** (str): Target datastore.
- **config** (str): XML configuration snippet.
- **do_validate** (bool): Validate after edit. Defaults to ``False``.

**Returns**
- ``str``: RPC reply XML.

Asynchronous Methods
--------------------

Async methods return awaitable Python ``asyncio.Future`` objects. NETCONF work
runs in the shared C++ worker thread pool. Python future completion is handled
by the shared async dispatcher thread.

Operations on the same ``NetconfClient`` RPC channel remain serialized to
protect NETCONF request/reply ordering. Use separate client instances for true
parallelism across sessions or devices.

Async methods raise the same public pyNetX exception classes as synchronous
methods.

connect_async()
~~~~~~~~~~~~~~~

Asynchronously establishes a NETCONF session.

.. code-block:: python

   await client.connect_async()


disconnect_async()
~~~~~~~~~~~~~~~~~~

Asynchronously closes the NETCONF session.

.. code-block:: python

   await client.disconnect_async()


send_rpc_async(rpc)
~~~~~~~~~~~~~~~~~~~

Asynchronously sends a custom RPC XML string.

.. code-block:: python

   reply = await client.send_rpc_async(rpc_xml)


next_notification(timeout_ms=10)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Synchronously polls or waits on the internal notification queue and returns the
next queued notification. This method is not awaitable.

Starting with v2.0.5, the pybind binding releases the Python GIL while this
method waits on the queue.

**Parameters**
- **timeout_ms** (int): Maximum time to wait in milliseconds. Defaults to
  ``10``. Use ``0`` for immediate polling. Values below ``0`` are invalid.

**Returns**
- ``str``: Next notification XML.
- ``""``: No notification was available before timeout.

.. code-block:: python

   notification = client.next_notification(timeout_ms=100)
   if notification:
       print(notification)


next_notification_async(timeout_ms=10)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Awaitable version of ``next_notification()``. Internally, it waits on the
notification queue in the shared C++ worker pool and completes a Python
``asyncio.Future`` when a notification is available or the timeout expires.

**Parameters**
- **timeout_ms** (int): Maximum time to wait in milliseconds. Defaults to
  ``10``. Use ``0`` for immediate polling.

**Returns**
- ``str``: Next notification XML.
- ``""``: No notification was available before timeout.

.. code-block:: python

   notification = await client.next_notification_async(timeout_ms=1000)
   if notification:
       print(notification)


peek_notifications(max_items=100)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns a snapshot of queued notifications without removing them.

**Parameters**
- **max_items** (int): Maximum number of notifications to return. Defaults to
  ``100``. Use ``-1`` to return all queued notifications.

**Returns**
- ``list[str]``: Queued notification XML strings.


notification_queue_size()
~~~~~~~~~~~~~~~~~~~~~~~~~

Returns the current number of queued notifications for this client.

**Returns**
- ``int``: Queue size.


is_subscription_active()
~~~~~~~~~~~~~~~~~~~~~~~~

Returns whether the client currently has an active notification subscription.

**Returns**
- ``bool``.


get_async(filter="")
~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``get_sync()``.

.. code-block:: python

   reply = await client.get_async(filter="<interfaces/>")


get_config_async(source="running", filter="")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``get_config_sync()``.

.. code-block:: python

   reply = await client.get_config_async(source="running")


copy_config_async(target, source)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``copy_config_sync()``.


delete_config_async(target)
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``delete_config_sync()``.


validate_async(source="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``validate_sync()``.


edit_config_async(target, config, do_validate=False)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``edit_config_sync()``.


subscribe_async(stream="NETCONF", filter="")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronously creates a NETCONF notification subscription.

.. code-block:: python

   reply = await client.subscribe_async(stream="NETCONF", filter="")


lock_async(target="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``lock_sync()``.


unlock_async(target="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``unlock_sync()``.


commit_async()
~~~~~~~~~~~~~~

Asynchronous version of ``commit_sync()``.


locked_edit_config_async(target, config, do_validate=False)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Asynchronous version of ``locked_edit_config_sync()``.

Common Methods and Module-Level Functions
-----------------------------------------


delete_subscription()
~~~~~~~~~~~~~~~~~~~~~

Deletes the client's notification subscription and notification session.

.. code-block:: python

   client.delete_subscription()


set_threadpool_size(nThreads)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Configures the size of the shared C++ worker thread pool used by async NETCONF
operations and async queue waits.

Set this during process startup, before starting active operations.

.. code-block:: python

   import pyNetX
   pyNetX.set_threadpool_size(10)


set_notification_reactor_count(nThreads)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Configures how many background epoll reactor threads pyNetX uses to monitor
notification sockets.

If this function is not called, pyNetX automatically creates one notification
reactor when the first subscription is registered. For large deployments,
configure this once during startup before creating subscriptions.

.. code-block:: python

   import pyNetX
   pyNetX.set_notification_reactor_count(8)

Notification Health Event Stream
--------------------------------

pyNetX 2.0.5 adds a process-wide notification health event stream. It is useful
for observing bounded queue pressure, notification drops, queue recovery, and
incomplete notification reads.

Events are represented by ``NotificationHealthEvent``.

NotificationHealthEvent
~~~~~~~~~~~~~~~~~~~~~~~

Fields:

- ``valid``
- ``type``
- ``hostname``
- ``port``
- ``fd``
- ``message``
- ``queue_size``
- ``queue_max_size``
- ``queue_high_watermark``
- ``notifications_enqueued``
- ``notifications_dropped_queue_full``
- ``notifications_dropped_delta``
- ``incomplete_notifications_received``
- ``partial_bytes``
- ``health_events_dropped``

Use ``event.as_dict()`` to convert an event to a Python dictionary.

Common event types:

- ``notification_queue_full``: A bounded notification queue first became full.
- ``notification_drops_summary``: Additional drops occurred while the queue
  remained full. Frequency is controlled by ``notif_drop_event_threshold``.
- ``notification_queue_recovered``: A previously full queue has free capacity.
- ``incomplete_notification``: Partial notification data was received without
  the NETCONF EOM marker, and an incomplete-notification guard returned it.
- ``timeout``: No event was available before timeout. ``valid`` is ``False``.

next_notification_event(timeout_ms=-1)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns the next notification health event.

**Parameters**
- **timeout_ms** (int): ``-1`` waits indefinitely, ``0`` polls immediately,
  and positive values wait up to that many milliseconds.

**Returns**
- ``NotificationHealthEvent``. If no event is available before timeout,
  ``event.valid`` is ``False`` and ``event.type`` is ``"timeout"``.

.. code-block:: python

   event = pyNetX.next_notification_event(timeout_ms=0)
   if event.valid:
       print(event.as_dict())


next_notification_event_async(timeout_ms=-1)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Awaitable version of ``next_notification_event()``.

.. code-block:: python

   event = await pyNetX.next_notification_event_async(timeout_ms=1000)
   if event.valid:
       print(event.as_dict())


pending_notification_event_count()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns the number of queued notification health events.

.. code-block:: python

   count = pyNetX.pending_notification_event_count()


clear_notification_events()
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Clears queued notification health events and resets the dropped-health-event
counter.

.. code-block:: python

   pyNetX.clear_notification_events()

Event monitor example
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: python

   import asyncio
   import pyNetX

   async def monitor_notification_health():
       while True:
           event = await pyNetX.next_notification_event_async(timeout_ms=-1)
           if event.valid:
               print(event.as_dict())

   asyncio.create_task(monitor_notification_health())

Common Exceptions
-----------------

All synchronous and asynchronous methods may raise one of the following public
exception classes:

- ``NetconfConnectionRefusedError``
- ``NetconfAuthError``
- ``NetconfChannelError``
- ``NetconfException``

Async methods preserve these exception classes. Earlier versions could convert
async failures to ``ValueError``.
