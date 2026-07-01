<div align="center">

# pyNetX

### Async-first NETCONF automation for Python

**pyNetX** is a high-performance, production level, Python NETCONF client library with a native C++/pybind11 backend. It provides asyncio-friendly NETCONF RPCs, SSH-based NETCONF sessions, notification subscriptions, epoll-backed notification reactors, bounded notification queues, and process-wide notification health events. It is battle-tested to manage **1,000+ live network devices** concurrently per instance.

<br />

[![PyPI](https://img.shields.io/pypi/v/pyNetX?label=PyPI&color=2563eb)](https://pypi.org/project/pyNetX/)
[![Python](https://img.shields.io/badge/Python-3.11%2B-3776ab)](#requirements)
[![Version](https://img.shields.io/badge/Stable-v2.0.7-16a34a)](#v207--latest)
[![Protocol](https://img.shields.io/badge/Protocol-NETCONF-orange)](#why-pynetx)

<br />

**Website:** https://jackofsometrades99.github.io/pynetx-website/  
**Docs:** https://pynetx.readthedocs.io/en/latest/  
**PyPI:** https://pypi.org/project/pyNetX/  
**GitHub:** https://github.com/jackofsometrades99/pyNetX

</div>

---

## Why pyNetX?

pyNetX is built for users who need more than a simple blocking NETCONF script. It is designed for async applications, multi-device automation, notification-heavy workloads, and production observability.

| Capability | What it gives you |
|---|---|
| **Async-first NETCONF API** | Use `await client.connect_async()`, `await client.get_config_async()`, `await client.edit_config_async()`, and other asyncio-friendly methods. |
| **C++ core with pybind11** | NETCONF work runs in a native backend while exposing a clean Python API. |
| **Shared worker pool** | Async NETCONF operations are submitted to a configurable C++ worker pool. |
| **Async result dispatcher** | C++ futures are bridged safely back to Python `asyncio.Future` objects. |
| **Separate notification session** | Notification subscriptions use a separate SSH/NETCONF session from normal RPC traffic. |
| **epoll notification reactors** | Notification sockets are monitored by background reactors instead of one Python thread per device. |
| **Bounded notification queues** | Per-client queues can be unbounded or bounded for controlled memory usage. |
| **Health event stream** | Queue-full, drops, recovery, incomplete notifications, and timeout events are observable through `NotificationHealthEvent`. |
| **Malformed notification diagnostics** | v2.0.7 reports malformed notification frames, empty EOM frames, orphan fragments, and missing-EOM recovery through health events. |
| **Device labels** | Health events include a user-provided `label` field so devices can be identified beyond hostname/IP. |
| **Event timestamps** | Health events include UTC ISO-8601 millisecond timestamps. |
| **Hardened notification parser** | v2.0.7 adds persistent stream parsing for coalesced, fragmented, and malformed notification payloads. |

> Current protocol framing: pyNetX uses NETCONF 1.0 end-of-message framing (`]]>]]>`). NETCONF 1.1 chunked framing is not part of this release.

---

## Install

```bash
pip install pyNetX==2.0.7
```

Or install the latest available release:

```bash
pip install pyNetX
```

### Requirements

- Python **3.11+**
- Build dependencies when building from source: `setuptools`, `wheel`, `cmake`, `scikit-build`, `pybind11`
- Native libraries when building from source: `libssh2`, `tinyxml2`, OpenSSL-compatible TLS libraries

On Debian/Ubuntu, source builds usually require development packages similar to:

```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential libssh2-1-dev libtinyxml2-dev
```

When installing a manylinux wheel from PyPI, the required native dependencies are bundled into the repaired wheel where applicable.

---

## Quick start: async NETCONF RPCs

```python
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
        notif_incomplete_max_kb=1024,
        notif_incomplete_timeout=5,
        notif_drop_event_threshold=1,
        label="leaf-01",
    )

    await client.connect_async()

    try:
        reply = await client.get_config_async(source="running")
        print(reply)
    finally:
        await client.disconnect_async()

asyncio.run(main())
```

---

## Quick start: custom RPC

```python
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
```

Do not append the NETCONF end marker yourself. pyNetX appends `]]>]]>` internally.

---

## Quick start: notifications

```python
import asyncio
import pyNetX

async def consume_notifications():
    client = pyNetX.NetconfClient(
        hostname="192.168.1.1",
        username="admin",
        password="admin",
        notif_queue_size=1000,
        label="leaf-01",
    )

    await client.connect_async()
    try:
        await client.subscribe_async(stream="NETCONF")

        while client.is_subscription_active():
            notification = await client.next_notification_async(timeout_ms=1000)
            if notification:
                print("Notification:", notification)
    finally:
        try:
            client.delete_subscription()
        finally:
            await client.disconnect_async()

asyncio.run(consume_notifications())
```

pyNetX uses a separate notification SSH/NETCONF session for subscriptions, so normal RPCs can continue on the primary session while notifications are being received.

### Notification stream parser behavior in v2.0.7

NETCONF notifications arrive over SSH as a byte stream. One socket read can contain multiple notifications, part of one notification, one complete notification plus the start of the next one, or malformed device data. pyNetX v2.0.7 keeps a persistent receive buffer for each notification subscription and parses that stream instead of assuming one read equals one notification.

Handled cases include:

| Device stream case | pyNetX behavior | Health event |
|---|---|---|
| One complete notification ending in `]]>]]>` | Queues the notification with the EOM marker. | None |
| Multiple complete notifications in one read | Splits and queues each notification separately. | None |
| Complete notification followed by a partial next notification | Queues the complete notification and keeps the partial bytes until more data or a guard fires. | None immediately |
| Partial notification completed by a later read | Combines the saved partial bytes with the later bytes and queues one complete notification. | None |
| New `<notification>` starts before the previous notification completed | Queues the abandoned previous fragment for inspection and continues from the new notification. | `incomplete_notification` |
| EOM-delimited data is not valid notification XML | Queues the malformed frame for inspection. | `malformed_notification` |
| Empty EOM-only frame | Drops the empty frame. | `malformed_notification` |
| Orphan bytes before a notification start tag | Drops the orphan prefix and continues parsing the notification. | `malformed_notification` |
| Complete notification XML followed by another notification but missing EOM between them | Recovers and queues the first notification without adding a synthetic EOM. | `malformed_notification` |
| Partial data never receives EOM before timeout/size guard | Queues the partial bytes without EOM. | `incomplete_notification` |

Valid complete notifications remain backward-compatible: EOM-delimited notifications are returned with `]]>]]>` included. Partial and recovered missing-EOM fragments are returned exactly as received, without adding a synthetic EOM.

---

## Quick start: notification health events

pyNetX health events include both `timestamp` and `label`:

```python
import asyncio
import pyNetX

async def monitor_health_events():
    while True:
        event = await pyNetX.next_notification_event_async(timeout_ms=-1)
        print(event.as_dict())

asyncio.run(monitor_health_events())
```

Example health event:

```python
{
    "valid": True,
    "type": "notification_queue_full",
    "timestamp": "2026-06-25T05:35:15.123Z",
    "label": "leaf-01",
    "hostname": "172.24.30.116",
    "port": 830,
    "fd": 97,
    "message": "Notification queue is full; dropping notifications",
    "queue_size": 2,
    "queue_max_size": 2,
    "queue_high_watermark": 2,
    "notifications_enqueued": 32,
    "notifications_dropped_queue_full": 3,
    "notifications_dropped_delta": 1,
    "incomplete_notifications_received": 0,
    "partial_bytes": 0,
    "health_events_dropped": 0,
}
```

Common event types:

| Event type | Meaning |
|---|---|
| `notification_queue_full` | A bounded per-client notification queue became full and a notification was dropped. |
| `notification_drops_summary` | More notifications were dropped while the queue remained full. Frequency is controlled by `notif_drop_event_threshold`. |
| `notification_queue_recovered` | A previously full queue has free capacity again. |
| `malformed_notification` | Malformed notification stream data was detected, such as empty EOM frames, invalid XML, orphan bytes before a notification, or recovered missing-EOM frames. |
| `incomplete_notification` | Partial notification data was received without the NETCONF `]]>]]>` EOM marker and a guard fired. |
| `timeout` | No health event was available before the requested timeout. This event has `valid == False` and `label == "None"`. |

---

## Constructor reference

```python
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
```

| Parameter | Default | Description |
|---|---:|---|
| `hostname` | required | NETCONF device hostname or IP address. |
| `port` | `830` | NETCONF SSH port. |
| `username` | required | SSH username. |
| `password` | required | SSH password. |
| `key_path` | `""` | Reserved for key-based authentication. Current authentication path uses password auth. |
| `connect_timeout` | `60` | Overall timeout for connection/session setup. |
| `read_timeout` | `60` | Timeout while waiting for device RPC replies or NETCONF messages. Must be greater than zero in the current public constructor. |
| `notif_queue_size` | `-1` | Per-client notification queue size. `-1` means unbounded. Non-negative values bound the queue. |
| `socket_connect_timeout` | `5` | TCP socket connection timeout. Must be greater than `0` and less than or equal to `connect_timeout`. |
| `notif_incomplete_max_kb` | `1024` | Maximum partial notification size, in KiB, before pyNetX returns the partial notification and emits a health event. Use `-1` to disable this guard. |
| `notif_incomplete_timeout` | `5` | Maximum time, in seconds, to wait for a notification EOM marker after partial data starts arriving. Use `-1` to disable this guard. |
| `notif_drop_event_threshold` | `1` | Number of additional queue-full drops before another queue-full health event is emitted. Must be greater than `0`. |
| `label` | `"None"` | User-defined string copied into notification health events for easier device identification. |

Use keyword arguments when constructing clients. This avoids positional-order confusion and makes new release parameters safer to adopt.

At least one incomplete-notification guard must remain enabled. Do not set both `notif_incomplete_max_kb=-1` and `notif_incomplete_timeout=-1`.

---

## Public API overview

### Recommended async flow APIs

| Method | Purpose |
|---|---|
| `await client.connect_async()` | Open a NETCONF session. |
| `await client.disconnect_async()` | Close the primary session and clean up. |
| `await client.send_rpc_async(rpc)` | Send a custom NETCONF RPC. |
| `await client.get_async(filter="")` | Run NETCONF `<get>`. |
| `await client.get_config_async(source="running", filter="")` | Read configuration. |
| `await client.copy_config_async(target, source)` | Copy datastore/configuration. |
| `await client.delete_config_async(target)` | Delete datastore. |
| `await client.validate_async(source="running")` | Validate datastore. |
| `await client.edit_config_async(target, config, do_validate=False)` | Edit configuration. |
| `await client.subscribe_async(stream="NETCONF", filter="")` | Create notification subscription on a separate notification session. |
| `await client.lock_async(target="running")` | Lock datastore. |
| `await client.unlock_async(target="running")` | Unlock datastore. |
| `await client.commit_async()` | Commit candidate changes where supported. |
| `await client.locked_edit_config_async(target, config, do_validate=False)` | Lock, edit, optionally validate, commit, and unlock. |

### Supported notification helper APIs

These helpers are **not deprecated**.

| Method | Purpose |
|---|---|
| `client.next_notification(timeout_ms=10)` | Synchronous helper for queued notifications. Releases the GIL while waiting. |
| `await client.next_notification_async(timeout_ms=10)` | Awaitable notification queue read. |
| `client.peek_notifications(max_items=100)` | Inspect queued notifications without consuming them. Use `-1` for all currently queued notifications. |
| `client.notification_queue_size()` | Return current notification queue depth. |
| `client.is_subscription_active()` | Check whether the notification subscription is active. |
| `client.delete_subscription()` | Delete the notification subscription/session. |

### Process-wide notification health event APIs

| Function | Purpose |
|---|---|
| `pyNetX.next_notification_event(timeout_ms=-1)` | Read next health event synchronously. |
| `await pyNetX.next_notification_event_async(timeout_ms=-1)` | Await next health event. |
| `pyNetX.pending_notification_event_count()` | Count queued health events. |
| `pyNetX.clear_notification_events()` | Clear queued health events and reset dropped counter. |

### Global configuration

| Function | Purpose |
|---|---|
| `pyNetX.set_threadpool_size(n)` | Configure shared NETCONF worker pool size. |
| `pyNetX.set_notification_reactor_count(n)` | Configure background epoll notification reactor count. |

Set these during process startup before active operations.

---

## Deprecation notice: explicit sync-flow APIs

Starting with v2.0.5, explicit synchronous flow APIs are deprecated and will be removed in a future major release. pyNetX is moving toward an async-focused API for connection handling, RPC execution, configuration operations, and subscriptions.

Deprecated methods:

```text
connect_sync
disconnect_sync
send_rpc_sync
receive_notification_sync
get_sync
get_config_sync
copy_config_sync
delete_config_sync
validate_sync
edit_config_sync
subscribe_sync
lock_sync
unlock_sync
commit_sync
locked_edit_config_sync
```

Common/helper APIs are **not** deprecated. This includes `next_notification`, `next_notification_async`, queue inspection, event-stream APIs, `delete_subscription`, `set_threadpool_size`, and `set_notification_reactor_count`.

Migration example:

```python
# Deprecated
client.connect_sync()
reply = client.get_config_sync(source="running")
client.disconnect_sync()

# Recommended
await client.connect_async()
reply = await client.get_config_async(source="running")
await client.disconnect_async()
```

---

## Error handling

```python
try:
    await client.connect_async()
except pyNetX.NetconfConnectionRefusedError:
    print("Device refused the NETCONF connection")
except pyNetX.NetconfAuthError:
    print("Authentication failed")
except pyNetX.NetconfChannelError:
    print("NETCONF channel setup failed")
except pyNetX.NetconfException as exc:
    print("NETCONF error:", exc)
```

Async methods preserve public pyNetX exception classes where the C++ backend emits them. Some low-level validation failures from the binding layer may be raised as `RuntimeError` or Python type-conversion exceptions.

---

## Scaling recommendations

```python
import pyNetX

pyNetX.set_threadpool_size(16)
pyNetX.set_notification_reactor_count(8)
```

Guidelines:

- Configure global thread-pool and reactor counts once during process startup.
- Operations on the same `NetconfClient` RPC channel are serialized to preserve request/reply ordering.
- Use separate `NetconfClient` objects for independent devices.
- Use bounded queues when you need strict memory control.
- Monitor `NotificationHealthEvent` when using bounded queues or high notification rates.
- `next_notification_event_async(timeout_ms=-1)` waits indefinitely and can occupy a worker. Size the worker pool accordingly.

---

## Testing

The test suite is in `test/` and focuses on the non-deprecated public API.

Run all default tests, excluding optional Netopeer2 tests:

```bash
pytest -c test/pytest.ini test -m "not netopeer" -ra --tb=short
```

Run only fast tests:

```bash
pytest -c test/pytest.ini test -m "not integration and not netopeer" -ra --tb=short
```

Run optional real Netopeer2/Sysrepo tests:

```bash
PYNETX_RUN_NETOPEER=1 pytest -c test/pytest.ini test -m netopeer -ra --tb=short
```

When testing wheels, run pytest from outside the source tree so Python imports the installed wheel instead of the local `pyNetX/` source directory.

See `docs/source/testing.rst` or the generated documentation for the full test coverage map and manylinux/Netopeer2 release workflow.

---

## v2.0.7 — latest

v2.0.7 focuses on hardened notification stream parsing for devices that coalesce, fragment, or corrupt notification payloads.

Highlights:

- Added a persistent per-subscription notification receive buffer.
- Split multiple `]]>]]>`-delimited notifications received in one SSH read into separate queue entries.
- Preserved trailing partial notification bytes across reactor callbacks.
- Detected and reported abandoned partial notifications when a new `<notification>` starts before the previous one completed.
- Added `malformed_notification` health events for malformed EOM-delimited frames, empty EOM frames, orphan bytes before a notification, and recovered missing-EOM frames.
- Reset notification parser state during subscription cleanup, dead-session cleanup, and queue clearing.

## v2.0.6

v2.0.6 focuses on better device identification, event timing, and stronger release testing.

Highlights:

- Added `label` constructor argument to `NetconfClient`.
- Added `label` field to `NotificationHealthEvent` and `event.as_dict()`.
- Added UTC ISO-8601 millisecond `timestamp` field to `NotificationHealthEvent` and `event.as_dict()`.
- Timeout events explicitly set `label == "None"`.
- Expanded pytest coverage for non-deprecated API contracts, constructor validation, event bus behavior, notification queues, fake NETCONF SSH integration, RPC payload/framing behavior, lifecycle/concurrency, and static source contracts.
- Added optional real Netopeer2/Sysrepo integration tests.
- Added release workflow documentation for testing repaired manylinux wheels before PyPI upload.

See `docs/source/release_notes.rst` for full release history.

---

## Documentation and links

| Resource | Link |
|---|---|
| Website | https://jackofsometrades99.github.io/pynetx-website/ |
| ReadTheDocs | https://pynetx.readthedocs.io/en/latest/ |
| PyPI | https://pypi.org/project/pyNetX/ |
| GitHub | https://github.com/jackofsometrades99/pyNetX |
