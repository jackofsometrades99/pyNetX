Architecture
============

pyNetX has a Python API backed by a C++ extension module.

Async RPC flow
--------------

.. code-block:: text

   Python await
     -> pybind11 binding
     -> C++ std::future
     -> global C++ ThreadPool
     -> libssh2 NETCONF channel
     -> C++ result/exception
     -> AsyncFutureDispatcher
     -> Python event loop
     -> asyncio.Future resolved

Async methods are asyncio-friendly wrappers around C++ worker-thread tasks. The
Python event loop is not blocked while the C++ backend performs NETCONF work.

Per-client RPC serialization
----------------------------

Operations on the same ``NetconfClient`` primary RPC session are serialized to
preserve request/reply ordering on the NETCONF channel.

Use separate ``NetconfClient`` objects for separate devices or independent
sessions.

Notification flow
-----------------

.. code-block:: text

   NETCONF device
     -> notification SSH/NETCONF session
     -> epoll notification reactor
     -> per-client notification queue
     -> next_notification() / next_notification_async()
     -> NotificationHealthEvent stream when pressure or errors occur

Normal RPC traffic and notification traffic use separate SSH/NETCONF sessions.

Global components
-----------------

Thread pool
~~~~~~~~~~~

The global C++ thread pool executes async NETCONF operations.

.. code-block:: python

   pyNetX.set_threadpool_size(16)

Configure this during process startup.

Async future dispatcher
~~~~~~~~~~~~~~~~~~~~~~~

The dispatcher bridges completed C++ futures back into the Python event loop.
This avoids one watcher thread per async operation.

Notification reactors
~~~~~~~~~~~~~~~~~~~~~

Notification reactors use epoll to monitor notification sockets.

.. code-block:: python

   pyNetX.set_notification_reactor_count(8)

Configure this during process startup before creating subscriptions.

Event bus
~~~~~~~~~

The process-wide ``NotificationEventBus`` stores bounded health events. Python
consumers read events with ``next_notification_event()`` or
``next_notification_event_async()``.

NETCONF framing
---------------

pyNetX v2.0.6 uses NETCONF 1.0 EOM framing with ``]]>]]>``.

Custom RPC callers should provide only the XML RPC payload. pyNetX appends the
EOM marker internally.
