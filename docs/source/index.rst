pyNetX 2.0.6 documentation
=============================

pyNetX is an async-first Python NETCONF client with a C++/pybind11 backend,
libssh2 SSH transport, asyncio-friendly APIs, epoll-backed notification
reactors, bounded notification queues, and structured notification health
events.

Version 2.0.6 adds device labels and UTC timestamps to notification health
events, expands non-deprecated API test coverage, and documents the release
validation workflow for repaired manylinux wheels and optional Netopeer2/Sysrepo
integration testing.

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
