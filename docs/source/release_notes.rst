Release notes
=============

v2.0.6 — latest
---------------

Focus: device-identifiable health events, event timing, and expanded release
testing.

Added
~~~~~

- Added ``label`` constructor argument to ``NetconfClient``.
- Added ``NotificationHealthEvent.label``.
- Added ``NotificationHealthEvent.timestamp``.
- Added ``label`` and ``timestamp`` to ``NotificationHealthEvent.as_dict()``.
- Added explicit ``label == "None"`` for global event-bus timeout events.
- Added deeper test coverage for non-deprecated public APIs.
- Added optional real Netopeer2/Sysrepo integration tests.
- Added documentation for manylinux wheel testing and Netopeer2 release testing.

Changed
~~~~~~~

- Health events are now easier to map to user inventory through ``label``.
- Health events now carry UTC creation time through ``timestamp``.
- Documentation is updated to present the async API as the primary supported
  usage path.

v2.0.5
------

Focus: notification observability and safer reactor behavior.

- Added process-wide notification health event stream.
- Added ``NotificationHealthEvent`` and ``event.as_dict()``.
- Added ``next_notification_event`` and ``next_notification_event_async``.
- Added ``pending_notification_event_count`` and ``clear_notification_events``.
- Added ``next_notification_async``.
- Added GIL release around ``next_notification()`` waits.
- Added incomplete-notification guards: ``notif_incomplete_max_kb`` and
  ``notif_incomplete_timeout``.
- Added ``notif_drop_event_threshold``.
- Added queue inspection helpers: ``peek_notifications`` and
  ``notification_queue_size``.
- Deprecated explicit synchronous flow APIs.

v2.0.4
------

- Added user-configurable ``socket_connect_timeout``.
- Reduced CPU usage for non-blocking reads by waiting on socket readiness.
- Replaced one watcher thread per async operation with a shared async future
  dispatcher.
- Improved async exception preservation.

v2.0.3
------

- Hardened notification reactor exception handling.
- Registered notification sockets only after subscription RPC success.
- Improved notification cleanup and subscription state safety.
- Added safer weak-reference handling in notification reactors.

v2.0.2
------

- Improved exception handling to prevent Python process crashes.
- Added ``notif_queue_size`` for internal notification queues.
- Added release builds for multiple Python versions.

v1.0.9
------

- Added cancellation-safe asyncio bridge guard to avoid ``InvalidStateError``
  after Python future cancellation.

v1.0.8
------

- Reimplemented notification monitoring with an epoll-based notification subsystem.
- Added ``set_notification_reactor_count()``.
- Removed ``receive_notification_async()``; use ``next_notification()`` or
  ``next_notification_async()``.
