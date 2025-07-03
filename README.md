# pyNetX

**pyNetX** is a Python library that facilitates both synchronous and asynchronous client-side scripting and application development around the NETCONF protocol. Developed by **Sambhu Nampoothiri G**, pyNetX provides a modern, efficient interface for interacting with NETCONF-enabled network devices — with truly asynchronous capabilities using non blocking connections.

> **Current Versions:**
> Stable: **v1.0.9** 
---

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

The full documentation (with detailed API references and more usage examples) is here.

[pyNetX Official Documentation](https://pynetx.readthedocs.io/en/latest/)
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
from pyNetX import NetconfClient

# Create a NETCONF client instance
client = NetconfClient(
    hostname="192.168.1.1",
    port=830,
    username="admin",
    password="admin",
    connect_timeout=30, # CONNECT TIMEOUT FROM CHANNEL. DEFAULT IS 60 SECONDS
    read_timeout=30 # READ TIMEOUT FROM CHANNEL. DEFAULT IS 60 SECONDS
)

# Establish a connection
status = client.connect_sync()

# Retrieve the running configuration
config = client.get_config_sync(source="running")
print("Running Configuration:")
print(config)

# Disconnect from the device
client.disconnect_sync()
```
### Asynchronous Usage

The asynchronous API methods are provided with an *_async* suffix and integrate with Python’s asyncio. For example:

```python

import asyncio
from pyNetX import NetconfClient

async def main():
    client = NetconfClient(
        hostname="192.168.1.1",
        port=830,
        username="admin",
        password="admin",
        connect_timeout=30, # CONNECT TIMEOUT FROM CHANNEL. DEFAULT IS 60 SECONDS
        read_timeout=30 # READ TIMEOUT FROM CHANNEL. DEFAULT IS 60 SECONDS
    )
    
    # Asynchronously connect to the device
    await status = client.connect_async()
    
    # Retrieve configuration asynchronously
    config = await client.get_config_async(source="running")
    print("Running Configuration:")
    print(config)
    
    # Asynchronously disconnect from the device
    await client.disconnect_async()

# Run the asynchronous main function
asyncio.run(main())
```

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
- **`next_notificaiton()`**
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
  Sets the number of threads in the shared task pool. The default is 4 threads. The number of threads in the pool determines how many tasks or operations can run concurrently. Note that for each device, operations (such as get_async, edit_config_async, etc.) are executed sequentially using a lock to avoid channel corruption. This nThreads value controls the total number of concurrent operations across all clients (devices) in the application. To use this, you can simply:

  ```python
  import pyNetX
  pyNetX.set_threadpool_size(10)
  ```

- **`set_notification_reactor_count(nThreads)`**
    Reconfigure how many background epoll-reactor threads PyNetX
    will run to monitor notification sockets.

    By default you should first call init(total_devices) or pass
    your preferred count here.  Each reactor thread will manage
    roughly FD_count / nThreads file descriptors.  Calling this
    at any time will tear down and rebuild the pool, then
    rebalance all existing subscriptions evenly across the new
    set of threads.

  ```python
  import pyNetX
  # Create 8 epoll‐based reactors to handle your notification streams
  pyNetX.set_notification_reactor_count(8)
  ```

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
