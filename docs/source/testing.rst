Testing
=======

The pyNetX test suite is located in ``test/`` and is designed to cover only the
non-deprecated public API. Deprecated sync-flow methods are intentionally not
part of the supported API contract tests.

Test dependency installation
----------------------------

From the repository root after building or installing pyNetX:

.. code-block:: bash

   python -m pip install -U pip
   python -m pip install -r test/requirements.txt

Default test suite
------------------

Run all normal tests, excluding optional Netopeer2 tests:

.. code-block:: bash

   pytest -c test/pytest.ini test -m "not netopeer" -ra --tb=short

Fast-only tests:

.. code-block:: bash

   pytest -c test/pytest.ini test -m "not integration and not netopeer" -ra --tb=short

Fake NETCONF integration tests:

.. code-block:: bash

   pytest -c test/pytest.ini test -m "integration and not netopeer" -ra --tb=short

Optional Netopeer2/Sysrepo tests:

.. code-block:: bash

   PYNETX_RUN_NETOPEER=1 pytest -c test/pytest.ini test -m netopeer -ra --tb=short

Coverage map
------------

``test_public_api_contract.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checks public non-deprecated API symbols, constructor keyword support, exception
classes, and ``NotificationHealthEvent`` fields including ``timestamp`` and
``label``.

``test_constructor_validation.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checks constructor validation for hostname, port, username, timeouts,
notification queue size, incomplete-notification guards, drop threshold, and
``label`` type handling.

``test_event_bus.py``
~~~~~~~~~~~~~~~~~~~~~

Checks process-wide health event behavior, timeout events, timestamp format,
default timeout label, ``as_dict()`` schema, event queue count/clear behavior,
invalid timeouts, and global setter validation.

``test_notification_helpers_without_subscription.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checks helper behavior before subscription: queue size, peek behavior, invalid
peek values, clear failure for no subscription, and idempotent subscription
cleanup.

``test_async_errors_without_connection.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checks async methods on unconnected clients and connection failures against a
closed local TCP port.

``test_integration_fake_netconf_server.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Uses a local Paramiko-based fake NETCONF-over-SSH server. It tests async
connect/RPC/disconnect, built-in RPC builders, subscription flow, notifications,
queue-full events, drop summaries, queue recovery, labels, timestamps, default
label behavior, and incomplete-notification events.

``test_client_lifecycle_and_concurrency.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checks concurrent ``send_rpc_async()`` calls on one client, primary RPC usability
while a notification session is active, disconnect cleanup after subscription,
two independent clients, and delete-subscription/resubscribe behavior.

``test_notification_queue_deep_integration.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checks queue peek non-destructiveness, FIFO ordering, ``peek_notifications(-1)``,
zero-sized bounded queues, high-watermark behavior, and incomplete notification
size guards.

``test_rpc_payloads_and_framing.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checks fragmented replies, large RPC payloads, Unicode payloads, and NETCONF 1.0
EOM framing behavior.

``test_static_source_contracts.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Runs source-level checks when the source checkout is available. It verifies that
source code, bindings, type stubs, and event construction contain current
``timestamp`` and ``label`` contracts.

``test_netopeer2_optional_integration.py``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Optional real NETCONF server tests against Netopeer2/Sysrepo. They check real
SSH NETCONF connection, ``get-config``, ``get``, lock/unlock when supported,
and notification subscription creation when supported.

Manylinux wheel gate
--------------------

Inside the manylinux container, build and repair wheels, then install each
repaired wheel into a clean virtual environment and run:

.. code-block:: bash

   python -m pytest -c /io/test/pytest.ini /io/test -m "not netopeer" -ra --tb=short

Exclude ``netopeer`` inside the manylinux upload loop unless Docker or an
external Netopeer2 target is intentionally provided. Manylinux containers often
do not have access to a Docker daemon.

Testing repaired wheels on the host
-----------------------------------

If wheels are built inside a running manylinux container, copy them to the host:

.. code-block:: bash

   cd ~/Documents/pyNetX/netconf_pybind
   mkdir -p wheelhouse dist
   sudo docker cp <container-id>:/io/wheelhouse/. ./wheelhouse/
   sudo docker cp <container-id>:/io/dist/. ./dist/

Create a clean Python 3.11 environment and install the matching wheel:

.. code-block:: bash

   python3.11 -m venv /tmp/pynetx-netopeer-test311
   source /tmp/pynetx-netopeer-test311/bin/activate
   python -m pip install -U pip
   python -m pip install pytest pytest-asyncio paramiko
   python -m pip install -r /home/sambhu/Documents/pyNetX/netconf_pybind/test/requirements.txt
   python -m pip install /home/sambhu/Documents/pyNetX/netconf_pybind/wheelhouse/pynetx-2.0.6-cp311-cp311-manylinux2014_x86_64.manylinux_2_17_x86_64.whl

Verify from outside the repository root:

.. code-block:: bash

   cd /tmp/pynetx-netopeer-test311
   python - <<'PY'
   import pyNetX
   print("pyNetX imported from:", pyNetX.__file__)
   PY

The path should point into the virtual environment's ``site-packages``.

Netopeer2/Sysrepo testing
-------------------------

Start a Netopeer2/Sysrepo container on the host:

.. code-block:: bash

   sudo docker rm -f pynetx-netopeer2 2>/dev/null || true
   sudo docker run -d \
     --name pynetx-netopeer2 \
     -p 830:830 \
     sysrepo/sysrepo-netopeer2:latest

Run the optional tests from outside the source tree using absolute paths:

.. code-block:: bash

   cd /tmp/pynetx-netopeer-test311

   PYNETX_RUN_NETOPEER=1 \
   PYNETX_NETOPEER_HOST=127.0.0.1 \
   PYNETX_NETOPEER_PORT=830 \
   PYNETX_NETOPEER_USERNAME=netconf \
   PYNETX_NETOPEER_PASSWORD=netconf \
   python -m pytest \
     -c /home/sambhu/Documents/pyNetX/netconf_pybind/test/pytest.ini \
     /home/sambhu/Documents/pyNetX/netconf_pybind/test \
     -m netopeer \
     -ra --tb=short

Expected successful result:

.. code-block:: text

   4 passed, 102 deselected in 1.67s

If the container is still starting, the same command may initially skip tests
with a message similar to:

.. code-block:: text

   external Netopeer2 target 127.0.0.1:830 is not reachable

Run the command again after the container is ready.

Cleanup:

.. code-block:: bash

   sudo docker rm -f pynetx-netopeer2
   deactivate

Recommended release gate
------------------------

1. Build and repair wheels inside manylinux.
2. For each repaired wheel, install into a clean venv and run ``pytest -m "not netopeer"``.
3. Copy the repaired wheel to the host.
4. Install the repaired wheel into a clean matching-Python venv.
5. Start Netopeer2/Sysrepo.
6. Run ``pytest -m netopeer``.
7. Run ``twine check``.
8. Upload to PyPI only after all required gates pass.
