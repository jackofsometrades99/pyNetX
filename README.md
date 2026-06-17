# pyNetX

**pyNetX** is a Python library that facilitates both synchronous and asynchronous client-side scripting and application development around the NETCONF protocol. Developed by **Sambhu Nampoothiri G**, pyNetX provides a modern, efficient interface for interacting with NETCONF-enabled network devices — with truly asynchronous capabilities using non blocking connections.

> **Current Versions:**
> Stable: **v2.0.4** 
---

## v2.0.4 — 2026-06-09

### Highlights

* **User-configurable socket connect timeout**

  * `NetconfClient` now accepts `socket_connect_timeout` as an optional constructor parameter.
  * This timeout controls how long the client waits for the underlying TCP socket connection attempt to complete.
  * Default: `5` seconds.
  * Validation rules:
    * `socket_connect_timeout` must be greater than `0`.
    * `socket_connect_timeout` cannot be greater than `connect_timeout`.

* **Lower CPU usage for non-blocking reads**

  * Non-blocking NETCONF channel reads now wait on socket readiness with `poll()` when libssh2 returns `EAGAIN`.
  * This replaces the previous retry/yield loop and avoids busy-spinning while waiting for device replies or notifications.
  * The configured `read_timeout` behavior is preserved.

* **More scalable asyncio bridge**

  * Async operations no longer create one detached watcher thread per operation.
  * pyNetX now uses a shared async future dispatcher to complete Python `asyncio.Future` objects.
  * ```text
        async API call
            -> submit NETCONF work to the shared worker thread pool
            -> register the returned C++ future with the shared async dispatcher
            -> dispatcher schedules set_result()/set_exception() on the Python event loop
  * This means `set_threadpool_size(10)` still allows up to 10 NETCONF worker operations to run concurrently across clients/devices, while async completion is
  handled by a shared dispatcher thread instead of one watcher thread per async call. This change is internal and does not change how async methods are called from Python.
  * Operations on the same NetconfClient RPC channel remain serialized: pyNetX sends one RPC, waits for its reply, and only then sends the next RPC on that same channel. To run requests truly in parallel, use separate NetconfClient instances, typically one per device/session.
  * `set_threadpool_size(n)` still controls how many NETCONF worker operations can run concurrently across clients.
  * Operations on the same `NetconfClient` RPC channel remain serialized to protect request/reply ordering.

* **Async exceptions now match sync exceptions**

  * Async methods now preserve pyNetX custom exception types instead of converting all async failures to `ValueError`.
  * `await client.connect_async()` and other async calls can now raise the same public exceptions as the sync API:
    * `NetconfConnectionRefusedError`
    * `NetconfAuthError`
    * `NetconfChannelError`
    * `NetconfException`

### Constructor update

```python
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
)
```

### Upgrade notes

* This is intended to be a backward-compatible stability and performance release.
* Existing code does not need to pass `socket_connect_timeout`; the default is `5` seconds.
* For slow TCP connection establishment, increase `socket_connect_timeout`, but keep it less than or equal to `connect_timeout`.
* Async exception handling may now catch more specific pyNetX exceptions where older versions raised `ValueError`. Update broad `except ValueError` handlers if they were used for NETCONF async failures.
* `set_threadpool_size(n)` limits NETCONF worker concurrency, not total process threads. pyNetX also uses a shared async dispatcher thread and optional notification reactor threads.

```bash
pip install pyNetX==2.0.4
```

## v2.0.3 — 2026-05-28

### Highlights

* **Hardened NETCONF notification handling**

  * Fixed a crash path where exceptions from the notification reactor thread could escape into C++ `std::thread` and terminate the Python process.
  * Notification reactor callbacks now log read failures, unregister the affected file descriptor, mark the subscription inactive, and allow the Python process to continue running.
  * The notification reactor now stores weak references to `NetconfClient` instances, preventing stale raw-pointer access if a Python client object is destroyed while a notification FD is still registered.

* **Safer notification subscription startup**

  * Notification sockets are now registered with the epoll reactor only after the `<create-subscription>` RPC has completed successfully.
  * This prevents the reactor thread from accidentally reading the subscription `<rpc-reply>` before `subscribe_async()` / `subscribe_sync()` receives it.

* **Improved notification cleanup**

  * Added mutex protection around notification resources, including the notification session, channel, socket, and subscription state flags.
  * Cleanup paths now unregister notification FDs before resetting notification resources.
  * `is_subscription_active()` now safely reports whether the notification subscription is still usable.

* **Hardened async bridge and worker threads**

  * Detached pybind11 watcher threads now have top-level exception guards, so Python event-loop shutdown or callback scheduling errors are logged instead of terminating the process.
  * Thread-pool worker tasks now have defensive exception guards to prevent unexpected C++ exceptions from escaping worker threads.

* **Notification queue behavior**

  * `notif_queue_size=-1` means the notification queue is unbounded.
  * A non-negative `notif_queue_size` limits the number of queued notifications.
  * When the queue is full, new notifications are dropped and a message is logged.

### Upgrade notes

* This release is intended to be a drop-in stability update.
* `set_notification_reactor_count(n)` is optional. If it is not called, pyNetX creates one notification reactor automatically when the first subscription is registered.
* For large deployments, call `set_notification_reactor_count(n)` before creating many subscriptions.
* `next_notification()` is a synchronous polling method. Do not use `await client.next_notification()`. Use `client.next_notification()` directly, even inside an async function.
* `set_threadpool_size(n)` should be called before starting async NETCONF operations. Runtime resizing during active operations is not recommended.
* If the Python event loop closes before an async operation completes, pyNetX logs the callback scheduling failure instead of aborting the interpreter.

```bash
pip install pyNetX==2.0.3
```

## v2.0.2 — 2026-04-01

### Highlights

* **Improved exception handling to prevent Python process crashes**
  * Fixes a critical issue introduced in **v1.0.9** where, under high load, destructors did not reliably release memory objects. In some cases this raised an exception, triggered `std::terminate`, and caused Python processes to crash.
  * This release improves memory cleanup and adds safer exception handling across the API surface to prevent those crashes.

* **Added `notif_queue_size` for internal notification queues**
  * A new `notif_queue_size` parameter is available when creating the internal notification queue for each device.
  * This setting controls how many notifications are buffered until they are consumed.
  * If the queue exceeds that limit, newer notifications are discarded and a message is logged to the console.
  * The default value is `-1`, which means the queue size is unbounded.
  * This parameter must be specified when creating the `NetconfClient` object.

* **Global release build with builds for 3.11, 3.12, 3.13 and 3.14**
  * This version of pyNetX supports multiple python versions upto 3.14.

* **Why it matters**
  * This release improves runtime stability under load, reduces the risk of unexpected Python process termination, and gives users better control over notification queue growth to help prevent memory pressure and queue overflows.

### Internal changes

* Minor cleanup and implementation updates in the pybind11 wrapper lambdas.

### Bug fixes

* Fixes the crash behavior introduced in **v1.0.9**.
* No new functional regressions were introduced in **v2.0.2**.

### Upgrade notes

* **Safe drop-in upgrade.** There are no API-breaking changes compared with **v1.0.9**.
* If you previously installed pyNetX from Test PyPI, install the updated wheel with:

```bash
pip install pyNetX==2.0.2
```

## v1.0.9 — 2025-07-03
### Highlights
* **Cancellation-safe asyncio bridge**  
  * Added a guard (`fut_pending()`) in the C++ wrapper so callbacks **skip
    `set_result()`/`set_exception()` if the Python `asyncio.Future` has already been cancelled or finished**.  
  * **Why it matters:** eliminates sporadic  
    `asyncio.exceptions.InvalidStateError: invalid state` seen when a running
    task is cancelled or times out while waiting for an RPC reply.
### Internal changes 
* Minor code changes in the pybind11 wrapper lambdas.
### Bug fixes
* No functional regressions introduced by v1.0.8.

### Upgrade notes
* **Safe to drop-in.** There are no API changes compared with v1.0.8.
* If you previously installed pyNetX from Test PyPI, grab the new wheel with  
  ```bash
  pip install pyNetX==1.0.9


## v1.0.8 — 2025-06-30
### Highlights
* **Epoll-based Notification Subsystem**  
  * Re-implemented the internal notification reactor on top of Linux `epoll`, eliminating the legacy **select-based** notification loop.  
  * **Why it matters:**
    * Scales linearly with the number of active NETCONF notification streams.  
    * Dramatically reduces CPU wake-ups under heavy load (measured ~85 % drop at 500 FDs).  
    * Lower latency for bursts of notifications, especially when many devices are idle most of the time.
    * No new threads are created for each notification arrival; a fixed pool started at program launch can handle hundreds of devices per thread.

* **Smarter Task-Pool Sharing**  
  * The global task pool now assigns workers to devices **dynamically** based on real-time queue depth rather than static round-robin.  
  * This allows tasks to be spread across queues more efficiently as per current load,
  minimizing task queue depth and improving aggregate throughput by up to 40 % in mixed-traffic scenarios.

### Internal changes
* Added `set_notification_reactor_count()` to let applications resize the epoll reactor pool on the fly.  
* Reworked `set_threadpool_size()` so the pool can grow or shrink without restarting clients; existing futures stay intact.  


### Bug fixes
* Fixed a hard-coded NETCONF base 1.0 header in `send_rpc_async(rpc="…")`; the call now follows the user mentioned version.

### Deprecations
* `receive_notification_async()` has been **removed**; migrate to `next_notification()` before v1.0.8.

---

*Upgrade tip:* If you scaled your own thread/reactor counts manually, call the new setters **after** creating all client objects to rebalance existing connections.


## Documentation

Full documentation: [pyNetX Official Documentation](https://pynetx.readthedocs.io/en/latest/)  
Website: [pyNetX Website](https://jackofsometrades99.github.io/pynetx-website/)
Source code: [GitHub Repository](https://github.com/jackofsometrades99/pyNetX)  
Package: [PyPI](https://pypi.org/project/pyNetX/)  
Article: [Medium](https://medium.com/@get4sambhugn/i-created-a-new-python-library-for-netconf-f9f27475433c)
---

## Requirements

- **Python:** 3.11+
- **Build Dependencies:** `setuptools`, `wheel`, `cmake`, `scikit-build`, and `pybind11`
- **System Libraries:**  
  - `libxml2`, `libxslt` (for XML processing)  
  - `libssh2`, `tinyxml2`, and audit tools (if required, install via your system’s package manager)

> **Note:** On Debian/Ubuntu, you might install the system libraries with:  
> ```bash
> sudo apt-get install libxml2-dev libxslt1-dev libssh2-dev tinyxml2-dev audit
> ```

---

## Installation

You can install **pyNetX** in either of the following ways:

1. **From PyPI:**

   ```bash
   pip install pyNetX
   ```
2. **From Source:**

   ```bash
   git clone https://github.com/jackofsometrades99/pyNetX.git
   cd pyNetX
   python setup.py install
   ```
## Examples

### Synchronous Usage

Below is an example of how to retrieve a device’s running configuration synchronously:

```python
from pyNetX import (
  NetconfClient,
  NetconfConnectionRefusedError,
  NetconfAuthError,
  NetconfChannelError,
  NetconfException
)
try:
  # Create a NETCONF client instance
  client = NetconfClient(
      hostname="192.168.1.1",
      port=830,
      username="admin",
      password="admin",
      connect_timeout=30, # CONNECT TIMEOUT FROM CHANNEL. DEFAULT IS 60 SECONDS
      read_timeout=30, # READ TIMEOUT FROM CHANNEL. DEFAULT IS 60 SECONDS
      socket_connect_timeout=5, # TCP SOCKET CONNECT TIMEOUT. DEFAULT IS 5 SECONDS
      notif_queue_size=-1 # NOTIFICATION QUEUE SIZE. -1 MEANS UNBOUNDED
  )

  # Establish a connection
  status = client.connect_sync()

  # Retrieve the running configuration
  config = client.get_config_sync(source="running")
  print("Running Configuration:")
  print(config)

  # Disconnect from the device
  client.disconnect_sync()
except (Exception, NetconfConnectionRefusedError, NetconfAuthError, NetconfChannelError, NetconfException) as error:
  pass
```
### Asynchronous Usage

The asynchronous API methods are provided with an *_async* suffix and integrate with Python’s asyncio. For example:

```python

import asyncio
from pyNetX import (
  NetconfClient,
  NetconfConnectionRefusedError,
  NetconfAuthError,
  NetconfChannelError,
  NetconfException
)

async def main():
    try:
      client = NetconfClient(
          hostname="192.168.1.1",
          port=830,
          username="admin",
          password="admin",
          connect_timeout=30, # CONNECT TIMEOUT FROM CHANNEL. DEFAULT IS 60 SECONDS
          read_timeout=30 # READ TIMEOUT FROM CHANNEL. DEFAULT IS 60 SECONDS
          socket_connect_timeout=5, # TCP SOCKET CONNECT TIMEOUT. DEFAULT IS 5 SECONDS
          notif_queue_size=-1 # NOTIFICATION QUEUE SIZE. -1 MEANS UNBOUNDED
      )
      
      # Asynchronously connect to the device
      status = await client.connect_async()
      
      # Retrieve configuration asynchronously
      config = await client.get_config_async(source="running")
      print("Running Configuration:")
      print(config)
      
      # Asynchronously disconnect from the device
      await client.disconnect_async()
    except (Exception, NetconfConnectionRefusedError, NetconfAuthError, NetconfChannelError, NetconfException) as error:
      pass

# Run the asynchronous main function
asyncio.run(main())
```

## NetconfClient Constructor Parameters

`NetconfClient` supports the following constructor parameters:

```python
NetconfClient(
    hostname: str,
    port: int = 830,
    username: str,
    password: str,
    key_path: str = "",
    connect_timeout: int = 60,
    read_timeout: int = 60,
    notif_queue_size: int = -1,
    socket_connect_timeout: int = 5,
)
```

| Parameter | Default | Description |
|---|---:|---|
| `hostname` | required | NETCONF device hostname or IP address. |
| `port` | `830` | NETCONF SSH port. |
| `username` | required | SSH username. |
| `password` | required | SSH password. |
| `key_path` | `""` | Reserved for key-based authentication. Current authentication uses password auth only, will come to support in future. |
| `connect_timeout` | `60` | Overall timeout for connection/session setup. |
| `read_timeout` | `60` | Timeout while waiting for device RPC replies or NETCONF messages. Use a negative value to wait indefinitely. |
| `notif_queue_size` | `-1` | Internal notification queue size. `-1` means unbounded; non-negative values bound the queue and drop new notifications when full. |
| `socket_connect_timeout` | `5` | Timeout for the underlying TCP socket connection attempt. Must be greater than `0` and less than or equal to `connect_timeout`. |

Use keyword arguments when constructing clients. This avoids confusion between the Python type stub order and the pybind11 constructor binding order.


## API Overview

The main class provided by **pyNetX** is `NetconfClient`, which offers both synchronous and asynchronous methods for NETCONF operations.

### Synchronous Methods

- **`connect_sync()`**  
  Establishes a NETCONF session with the target device.

- **`disconnect_sync()`**  
  Closes the NETCONF session.

- **`send_rpc_sync(rpc)`**  
  Sends a custom RPC command.

- **`get_sync(filter="")`**  
  Retrieves device information using an optional filter.

- **`get_config_sync(source="running", filter="")`**  
  Retrieves the device configuration.

- **`copy_config_sync(target, source)`**  
  Copies configuration from one datastore to another.

- **`delete_config_sync(target)`**  
  Deletes configuration from the specified target.

- **`validate_sync(source="running")`**  
  Validates the configuration.

- **`edit_config_sync(target, config, do_validate=False)`**  
  Edits the device configuration.

- **`subscribe_sync(stream="NETCONF", filter="")`**  
  Subscribes to NETCONF notifications.

- **`receive_notification_sync()`**
  Fetches a single received notification from the notification channel.

- **`lock_sync(target="running")`** and **`unlock_sync(target="running")`**  
  Lock and unlock a configuration datastore, respectively.

- **`commit_sync()`**  
  Commits any configuration changes.

- **`locked_edit_config_sync(target, config, do_validate=False)`**  
  Performs an edit configuration operation while holding a lock.

### Asynchronous Methods

For every synchronous method, there is an asynchronous counterpart that returns an asyncio Future:

- **`connect_async()`**
- **`disconnect_async()`**
- **`send_rpc_async(rpc="")`**
- **`next_notification()`**
  Polls the internal notification queue. This method is not awaitable; call it directly.
- **`get_async(filter="")`**
- **`get_config_async(source="running", filter="")`**
- **`copy_config_async(target, source)`**
- **`delete_config_async(target)`**
- **`validate_async(source="running")`**
- **`edit_config_async(target, config, do_validate=False)`**
- **`subscribe_async(stream="NETCONF", filter="")`**
- **`lock_async(target="running")`**
- **`unlock_async(target="running")`**
- **`commit_async()`**
- **`locked_edit_config_async(target, config, do_validate=False)`**

## Common Methods.

These methods can be used in both synchronous and asynchronous operations:

- **`delete_subscription()`**
  Unsubscribe from recieving notifications.

- **`set_threadpool_size(nThreads)`**
  Sets the number of threads in the shared NETCONF worker task pool. The default is 4 threads. This controls how many NETCONF operations can run concurrently across all clients/devices in the application. Operations on the same `NetconfClient` RPC channel are still serialized using an internal lock, so a single device channel follows request → reply → next request ordering. Async completion is handled by a shared dispatcher thread and no longer creates one watcher thread per async call. To use this, you can simply:

  ```python
  import pyNetX
  pyNetX.set_threadpool_size(10)
  ```
  As of version `2.0.4`, Async completion is handled by a shared dispatcher thread. pyNetX no longer creates one watcher thread per async call.

- **`set_notification_reactor_count(nThreads)`**
  Configures how many background epoll reactor threads pyNetX
  uses to monitor notification sockets.

  If this function is not called, pyNetX automatically
  creates one reactor when the first notification
  subscription is registered.

  For large deployments, call this before
  creating many subscriptions:

  ```python
  import pyNetX
  # Create 8 epoll‐based reactors to handle your notification streams
  pyNetX.set_notification_reactor_count(8)
  ```

  Existing subscriptions are rebalanced when the reactor count is changed,
  but applications should prefer configuring this once during startup.

## Exception Handling

**pyNetX** defines custom exceptions to handle various NETCONF-related errors:

- **`NetconfConnectionRefusedError`**  
  Raised when a connection attempt is refused.

- **`NetconfAuthError`**  
  Raised when authentication fails.

- **`NetconfChannelError`**  
  Raised for channel-related errors.

- **`NetconfException`**  
  The base exception for NETCONF-related issues.

Synchronous and asynchronous methods raise the same public exception types. In earlier versions, async failures could be converted to ValueError; this is fixed in v2.0.4.


