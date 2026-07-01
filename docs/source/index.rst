pyNetX 2.0.7 documentation
=============================

pyNetX is an async-first Python NETCONF client with a C++/pybind11 backend,
libssh2 SSH transport, asyncio-friendly APIs, epoll-backed notification
reactors, bounded notification queues, and structured notification health
events.

Version 2.0.7 hardens notification stream parsing for devices that coalesce,
fragment, or corrupt notification payloads. It preserves the v2.0.6 health-event
``label`` and ``timestamp`` fields and adds clearer diagnostics for malformed
notification stream data.

.. toctree::
   :maxdepth: 2
   :caption: User guide

   introduction
   installation
   quickstart
   notifications
   health_events
   examples

.. toctree::
   :maxdepth: 2
   :caption: Reference

   api_reference
   architecture
   migration
   testing
   release_notes

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
