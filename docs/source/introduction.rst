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
3. **Thread Pool Management**  
    - A global thread pool manages concurrent tasks, ensuring thread safety 
    and efficient resource usage.
4. **Error Handling with Custom Exceptions**  
    - pyNetX defines Python exceptions for common NETCONF errors, such as 
    authentication failure, connection refusal, or channel problems.
5. **C++ Speed Underneath**  
    - Uses C++ (via pybind11) for heavy lifting (SSH communication, XML parsing, etc.), 
    while exposing a Pythonic interface.

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

      pip install pyNetX

   Or from source:

   .. code-block:: bash

      git clone https://github.com/jackofsometrades99/pyNetX.git
      cd pyNetX
      python setup.py install

2. **Basic Usage**:

   .. code-block:: python

      from pyNetX import NetconfClient

      # Synchronous example
      client = NetconfClient(hostname="192.168.1.1", port=830,
                             username="admin", password="admin")
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
