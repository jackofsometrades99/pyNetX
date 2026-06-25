Introduction
============

pyNetX is a Python NETCONF client library designed for async network automation
and notification-heavy applications. The public API is Python, while the core
NETCONF/SSH work is implemented in C++ and exposed through pybind11.

What pyNetX provides
--------------------

- Async-friendly NETCONF RPC methods such as ``connect_async()``,
  ``get_config_async()``, ``edit_config_async()``, and ``subscribe_async()``.
- A custom C++ worker pool used by async operations.
- A shared async result dispatcher that resolves Python ``asyncio.Future``
  objects from completed C++ futures.
- A separate SSH/NETCONF notification session so long-running subscriptions do
  not block normal RPC traffic.
- epoll-backed notification reactor threads.
- Per-client notification queues.
- A process-wide notification health event stream.
- Device labels and UTC timestamps in health events starting in v2.0.6.

Current protocol behavior
-------------------------

pyNetX currently uses NETCONF 1.0 end-of-message framing with the ``]]>]]>``
marker. Do not append this marker in custom RPC payloads; pyNetX appends it
internally.

NETCONF 1.1 chunked framing is not part of v2.0.6.

Async-first direction
---------------------

Starting with v2.0.5, explicit synchronous flow APIs are deprecated. They remain
available for compatibility, but new code should use async methods for session
management, RPC execution, configuration operations, and subscriptions.

The following helper APIs are not deprecated:

- ``next_notification()``
- ``next_notification_async()``
- ``peek_notifications()``
- ``notification_queue_size()``
- ``is_subscription_active()``
- ``delete_subscription()``
- notification health event APIs
- global thread-pool and reactor configuration APIs

New in v2.0.6
-------------

- ``NetconfClient(..., label="...")`` stores a user-defined device label.
- ``NotificationHealthEvent.timestamp`` contains the event creation time as a
  UTC ISO-8601 string with millisecond precision.
- ``NotificationHealthEvent.label`` helps users identify the device that
  produced the event.
- ``event.as_dict()`` includes both ``timestamp`` and ``label``.
- Timeout events explicitly use ``label == "None"`` because they are generated
  by the global event bus rather than a specific device.
- The test suite includes deeper fake NETCONF integration tests and optional
  real Netopeer2/Sysrepo integration tests.
