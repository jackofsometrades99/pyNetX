API Reference
=============

This section describes all methods provided by ``NetconfClient`` in pyNetX.
The methods are organized into two main groups:

1. **Synchronous Methods**  
2. **Asynchronous Methods**
3. **Common Methods**

Usage Examples:
---------------
.. code-block:: python

   from pyNetX import NetconfClient

   # Synchronous usage
   client = NetconfClient("192.168.1.1", 830, "admin", "admin")
   client.connect_sync()
   response = client.get_config_sync()
   client.disconnect_sync()

   # Asynchronous usage
   import asyncio

   async def main():
       client = NetconfClient("192.168.1.1", 830, "admin", "admin")
       await client.connect_async()
       response = await client.get_config_async()
       await client.disconnect_async()

   asyncio.run(main())

Synchronous Methods
-------------------

connect_sync()
~~~~~~~~~~~~~~

**Description**  
Establishes a synchronous NETCONF session with the remote device.

**Parameters**  
- None (relies on constructor settings like ``hostname``, ``port``, etc.)

**Returns**  
- Typically returns a boolean status or raises a connection-related exception.

**Example**  

.. code-block:: python

   from pyNetX import NetconfClient

   client = NetconfClient("192.168.1.1", 830, "admin", "admin")
   status = client.connect_sync()
   print("Connection status:", status)


disconnect_sync()
~~~~~~~~~~~~~~~~

**Description**  
Closes the NETCONF session.

**Parameters**  
- None

**Returns**  
- None

**Example**  

.. code-block:: python

   client.disconnect_sync()
   print("Disconnected.")


send_rpc_sync(rpc)
~~~~~~~~~~~~~~~~~~

**Description**  
Sends a custom RPC command (XML string) to the device and returns the response.

**Parameters**  
- **rpc** (str): The XML snippet representing the RPC command.

**Returns**  
- A string containing the device’s RPC reply.

**Example**  

.. code-block:: python

   rpc_command = \"\"\"<get><filter type='subtree'><interfaces/></filter></get>\"\"\"
   response = client.send_rpc_sync(rpc_command)
   print("RPC Response:\n", response)


get_sync(filter="")
~~~~~~~~~~~~~~~~~~

**Description**  
Retrieves data from the device (equivalent to NETCONF ``<get>`` operation). An optional XML filter can be provided.

**Parameters**  
- **filter** (str): An XML filter string. Defaults to an empty string (meaning retrieve all).

**Returns**  
- A string containing the XML data from the device.

**Example**  

.. code-block:: python

   response = client.get_sync(filter="<interfaces/>")
   print("Interfaces:\n", response)


get_config_sync(source="running", filter="")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Retrieves the configuration from the specified datastore (e.g., ``running``, ``candidate``, or ``startup``).

**Parameters**  
- **source** (str): Defaults to ``"running"``.
- **filter** (str): Optional XML filter string.

**Returns**  
- A string containing the XML configuration.

**Example**  

.. code-block:: python

   config = client.get_config_sync(source="running", filter="<interfaces/>")
   print("Running Config:\n", config)


copy_config_sync(target, source)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Copies the configuration from one datastore (``source``) to another (``target``).

**Parameters**  
- **target** (str): Target datastore (e.g., ``"candidate"``).
- **source** (str): Source datastore or XML config.

**Returns**  
- None or a status string. Raises an exception on failure.

**Example**  

.. code-block:: python

   # Copy running config to candidate
   client.copy_config_sync(target="candidate", source="running")
   print("Successfully copied configuration.")


delete_config_sync(target)
~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Deletes the configuration from the specified datastore (``target``).

**Parameters**  
- **target** (str): Datastore to delete (e.g., ``"candidate"``).

**Returns**  
- None or a success message. Raises an exception on failure.

**Example**  

.. code-block:: python

   client.delete_config_sync("candidate")
   print("Candidate datastore deleted.")


validate_sync(source="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Validates the contents of the specified datastore.

**Parameters**  
- **source** (str): The datastore to validate. Defaults to ``"running"``.

**Returns**  
- None or a validation result. Raises an exception on failure.

**Example**  

.. code-block:: python

   client.validate_sync(source="candidate")
   print("Candidate datastore validated.")


edit_config_sync(target, config, do_validate=False)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Edits the specified datastore with the provided config string. Optionally validates after editing.

**Parameters**  
- **target** (str): Datastore to edit (e.g., ``"running"``, ``"candidate"``).
- **config** (str): The XML configuration snippet to apply.
- **do_validate** (bool): Whether to automatically validate after editing. Defaults to ``False``.

**Returns**  
- None or a response message. Raises an exception on failure.

**Example**  

.. code-block:: python

   new_config = \"\"\"<config> ... </config>\"\"\"
   client.edit_config_sync("candidate", new_config, do_validate=True)
   print("Configuration applied and validated.")


subscribe_sync(stream="NETCONF", filter="")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Subscribes to NETCONF notifications on the given stream.

**Parameters**  
- **stream** (str): Defaults to ``"NETCONF"``.
- **filter** (str): Optional XML filter to scope the notifications.

**Returns**  
- None or a subscription ID. Raises an exception on failure.

**Example**  

.. code-block:: python

   client.subscribe_sync(stream="NETCONF")
   print("Subscribed to NETCONF notifications.")


receive_notification_sync()
~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Receives a single notification from the device. Blocks until a notification is received or a timeout occurs.

**Parameters**  
- None

**Returns**  
- A string containing the notification XML or None on timeout.

**Example**  

.. code-block:: python

   notification = client.receive_notification_sync()
   print("Received notification:\n", notification)


lock_sync(target="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Locks the specified datastore.

**Parameters**  
- **target** (str): Defaults to ``"running"``.

**Returns**  
- None or a status message.

**Example**  

.. code-block:: python

   client.lock_sync("candidate")
   print("Candidate datastore locked.")


unlock_sync(target="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Unlocks the specified datastore.

**Parameters**  
- **target** (str): Defaults to ``"running"``.

**Returns**  
- None or a status message.

**Example**  

.. code-block:: python

   client.unlock_sync("candidate")
   print("Candidate datastore unlocked.")


commit_sync()
~~~~~~~~~~~~~

**Description**  
Commits any configuration changes from the candidate datastore to the running datastore (if your device supports two-phase commit or similar).

**Parameters**  
- None

**Returns**  
- None or a commit result. Raises an exception on failure.

**Example**  

.. code-block:: python

   client.commit_sync()
   print("Committed changes to the running datastore.")


locked_edit_config_sync(target, config, do_validate=False)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Acquires a lock on the specified datastore, edits the configuration, and unlocks automatically. Optionally validates the new config.

**Parameters**  
- **target** (str): Datastore (e.g., ``"running"``, ``"candidate"``).
- **config** (str): XML config snippet to apply.
- **do_validate** (bool): Validate after editing. Defaults to ``False``.

**Returns**  
- None or a status message.

**Example**  

.. code-block:: python

   config_snippet = \"\"\"<config> ... </config>\"\"\"
   client.locked_edit_config_sync("candidate", config_snippet)
   print("Successfully edited configuration with lock.")


Asynchronous Methods
--------------------
Below are the asynchronous counterparts, returning awaitable tasks that integrate
with Python’s ``asyncio``.

connect_async()
~~~~~~~~~~~~~~~

**Description**  
Asynchronously establishes a NETCONF session.

**Parameters**  
- None

**Returns**  
- An awaitable. Raises an exception on failure.

**Example**  

.. code-block:: python

   import asyncio
   from pyNetX import NetconfClient

   async def main():
       client = NetconfClient("192.168.1.1", 830, "admin", "admin")
       await client.connect_async()
       print("Async connection established.")
       await client.disconnect_async()

   asyncio.run(main())


disconnect_async()
~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously disconnects the NETCONF session.

**Parameters**  
- None

**Returns**  
- An awaitable that completes once the session closes.

**Example**  

.. code-block:: python

   await client.disconnect_async()
   print("Disconnected async.")


send_rpc_async(rpc="")
~~~~~~~~~~~~~~~~~~~~~

**Description**  
Sends a custom RPC asynchronously and awaits the response.

**Parameters**  
- **rpc** (str): The XML snippet for the RPC.

**Returns**  
- The RPC reply as a string.

**Example**  

.. code-block:: python

   rpc_command = \"\"\"<get><filter type='subtree'><interfaces/></filter></get>\"\"\"
   reply = await client.send_rpc_async(rpc_command)
   print("Async RPC reply:", reply)


receive_notification_async()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously waits for a single notification from the device.

**Parameters**  
- None

**Returns**  
- A string with the notification XML or None on timeout.

**Example**  

.. code-block:: python

   notification = await client.receive_notification_async()
   print("Async notification received:", notification)


get_async(filter="")
~~~~~~~~~~~~~~~~~~~

**Description**  
Performs an asynchronous ``<get>`` operation.

**Parameters**  
- **filter** (str): XML filter snippet.

**Returns**  
- A string containing the requested data.

**Example**  

.. code-block:: python

   data = await client.get_async("<interfaces/>")
   print("Async data:\n", data)


get_config_async(source="running", filter="")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously retrieves the specified datastore configuration.

**Parameters**  
- **source** (str): e.g., ``"running"`` or ``"candidate"``. Defaults to ``"running"``.
- **filter** (str): Optional filter.

**Returns**  
- The config data as a string.

**Example**  

.. code-block:: python

   running_config = await client.get_config_async(source="running")
   print("Async running config:\n", running_config)


copy_config_async(target, source)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously copies the configuration from ``source`` to ``target``.

**Parameters**  
- **target** (str)
- **source** (str)

**Returns**  
- None or a status message.

**Example**  

.. code-block:: python

   await client.copy_config_async(target="candidate", source="running")


delete_config_async(target)
~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously deletes the specified datastore.

**Parameters**  
- **target** (str): e.g., ``"candidate"``.

**Returns**  
- None or a success/failure message.

**Example**  

.. code-block:: python

   await client.delete_config_async("candidate")


validate_async(source="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously validates the configuration in the specified datastore.

**Parameters**  
- **source** (str): defaults to ``"running"``.

**Returns**  
- None or a validation message.

**Example**  

.. code-block:: python

   await client.validate_async("candidate")


edit_config_async(target, config, do_validate=False)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously edits a datastore with the provided configuration. Optionally validates.

**Parameters**  
- **target** (str): e.g., ``"running"`` or ``"candidate"``.
- **config** (str): The XML config snippet.
- **do_validate** (bool): Whether to validate after editing.

**Returns**  
- None or a status message.

**Example**  

.. code-block:: python

   config_snippet = \"\"\"<config> ... </config>\"\"\"
   await client.edit_config_async("candidate", config_snippet, do_validate=True)


subscribe_async(stream="NETCONF", filter="")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously subscribes to notifications from a given stream.

**Parameters**  
- **stream** (str): Defaults to ``"NETCONF"``.
- **filter** (str): Optional XML filter.

**Returns**  
- None or a subscription identifier.

**Example**  

.. code-block:: python

   await client.subscribe_async("NETCONF")
   print("Subscribed to NETCONF notifications asynchronously.")


lock_async(target="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously locks the specified datastore.

**Parameters**  
- **target** (str): Defaults to ``"running"``.

**Returns**  
- None or a status message.

**Example**  

.. code-block:: python

   await client.lock_async("candidate")
   print("Locked candidate datastore asynchronously.")


unlock_async(target="running")
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously unlocks the specified datastore.

**Parameters**  
- **target** (str): Defaults to ``"running"``.

**Returns**  
- None or a status message.

**Example**  

.. code-block:: python

   await client.unlock_async("candidate")
   print("Unlocked candidate datastore asynchronously.")


commit_async()
~~~~~~~~~~~~~~

**Description**  
Asynchronously commits changes from the candidate datastore to the running datastore.

**Parameters**  
- None

**Returns**  
- None or a commit message.

**Example**  

.. code-block:: python

   await client.commit_async()
   print("Asynchronous commit completed.")


locked_edit_config_async(target, config, do_validate=False)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Description**  
Asynchronously locks the specified datastore, applies an edit-config, 
optionally validates, then unlocks.

**Parameters**  
- **target** (str)
- **config** (str)
- **do_validate** (bool): whether to validate after editing.

**Returns**  
- None or a status message.

**Example**  

.. code-block:: python

   config_snippet = \"\"\"<config> ... </config>\"\"\"
   await client.locked_edit_config_async("candidate", config_snippet, do_validate=True)

Common Methods
--------------------
Below are the some common methods that can be used in both sync and async structures.

delete_subscription()
~~~~~~~~~~~~~~

**Description**  
Deletes an established NETCONF notifications subscription for a client.

**Parameters**  
- None

**Returns**  
- None.

**Example**  

.. code-block:: python

   client.delete_subscription()


set_threadpool_size(nThreads)
~~~~~~~~~~~~~~

**Description**  
Sets the number of threads in the shared task pool.

**Parameters**  
- **target** (int): Number of threads present in thread pool.

**Returns**  
- None.

**Example**  

.. code-block:: python

   import pyNetX
   pyNetX.set_threadpool_size(10) # Creates 10 threads in the shared task pool.


set_notification_reactor_count(nThreads)
~~~~~~~~~~~~~~

**Description**  
Sets the number of threads in the notification reactor pool.

**Parameters**  
- **target** (int): Number of threads present in reactor pool.

**Returns**  
- None.

**Example**  

.. code-block:: python

   import pyNetX
   pyNetX.set_notification_reactor_count(10) # Creates 10 threads in the notification reactor pool.


Common Exceptions
-----------------
All methods may raise one of the following custom exceptions upon failure:

- **NetconfConnectionRefusedError**  
- **NetconfAuthError**  
- **NetconfChannelError**  
- **NetconfException**  

For details, see :doc:`introduction` or check out the usage examples in :doc:`usage`.

