Introduction
============

What is pyNetX?
---------------
**pyNetX** is a Python library for managing NETCONF sessions with network devices. 
It provides a clean, high-level API for performing operations such as connecting 
to devices, retrieving or editing configurations, and subscribing to notifications. 
Both **synchronous** and **asynchronous** methods are available, enabling pyNetX to 
be used in a variety of application architectures, from simple one-off scripts to 
large-scale event-driven frameworks.

Key Features
------------
1. **NETCONF Support**  
    - Full suite of NETCONF operations, including ``get()``/``get-config()`` 
    for retrieval, ``edit-config()`` for updates, and more.
2. **Synchronous & Asynchronous API**  
   - Choose either blocking (sync) or non-blocking (async) methods for each operation. 
   - Async support integrates with Python’s built-in ``asyncio`` library.
3. **Thread Pool and Async Completion Management**  
    - A global thread pool manages NETCONF worker tasks across clients/devices.
    - Starting with v2.0.4, async completion uses one shared dispatcher instead
      of creating one detached watcher thread per async operation.
4. **Connection and Read Timeout Controls**
    - Configure total connect timeout, read timeout, notification queue size,
      and socket-level connect timeout from the ``NetconfClient`` constructor.
5. **Error Handling with Custom Exceptions**  
    - pyNetX defines Python exceptions for common NETCONF errors, such as 
      authentication failure, connection refusal, or channel problems.
    - Starting with v2.0.4, async methods preserve these custom exception types
      instead of converting failures to ``ValueError``.
6. **C++ Speed Underneath**  
    - Uses C++ (via pybind11) for heavy lifting (SSH communication, XML parsing, etc.), 
    while exposing a Pythonic interface.


What Changed in v2.0.4
----------------------
- Added public documentation for ``socket_connect_timeout`` in the
  ``NetconfClient`` constructor.
- Non-blocking reads now wait on socket readiness with ``poll()`` instead of
  repeatedly retrying on ``EAGAIN``. This reduces CPU usage when devices delay
  replies or notifications.
- Async wrappers now use one shared completion dispatcher instead of one
  detached watcher thread per async operation.
- Async exceptions now preserve pyNetX exception classes such as
  ``NetconfAuthError`` and ``NetconfConnectionRefusedError``.
- Normal RPC calls on the same ``NetconfClient`` channel remain serialized:
  pyNetX sends one RPC, waits for its reply, and then sends the next RPC.

System Requirements
-------------------
- **Python** 3.11+  
- **Libraries**:  
  - ``libxml2``, ``libxslt`` for XML parsing  
  - ``libssh2``, ``tinyxml2``, and possibly ``audit`` (depends on your OS)  
- **Build Dependencies** (if installing from source):
  - ``setuptools``, ``wheel``, ``cmake``, ``scikit-build``, ``pybind11``

On Debian or Ubuntu, for example, you might install required packages with:

.. code-block:: bash

   sudo apt-get update
   sudo apt-get install libxml2-dev libxslt1-dev libssh2-1-dev tinyxml2-dev audit

Why Use pyNetX?
---------------
1. **One-stop**: Instead of piecing together multiple libraries for SSH and XML, 
   pyNetX offers a single library specifically tailored for NETCONF.
2. **Performance**: Written in C++ with Python bindings, providing low-level 
   performance without losing Python’s ease of use.
3. **Async-Ready**: Perfect for large-scale or event-driven architectures— 
   run multiple device interactions in parallel with full asyncio support.

Getting Started
---------------
1. **Install pyNetX** from PyPI:

   .. code-block:: bash

      pip install pyNetX==2.0.4

   Or from source:

   .. code-block:: bash

      git clone https://github.com/jackofsometrades99/pyNetX.git
      cd pyNetX
      python setup.py install

2. **Basic Usage**:

   .. code-block:: python

      from pyNetX import NetconfClient

      # Synchronous example
      client = NetconfClient(
          hostname="192.168.1.1",
          port=830,
          username="admin",
          password="admin",
          connect_timeout=30,
          read_timeout=30,
          socket_connect_timeout=5,
      )
      client.connect_sync()
      running_config = client.get_config_sync(source="running")
      print(running_config)
      client.disconnect_sync()

3. **Check Out the API**:
   - See :doc:`examples` for more examples and usage patterns.
   - Refer to :doc:`api_reference` for a complete list of methods and parameters.

Contributing
------------
We welcome contributions! If you would like to fix bugs, 
improve documentation, or add new features:

1. Fork the GitHub repository.
2. Create a new branch and make your changes.
3. Submit a pull request and wait for feedback.

Next Steps
----------
Ready to dive deeper? Explore the next sections for detailed usage 
instructions, advanced features, and examples:

- :doc:`api_reference`
- :doc:`examples`
