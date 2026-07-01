Installation
============

Install from PyPI
-----------------

.. code-block:: bash

   pip install pyNetX==2.0.7

Or install the latest available release:

.. code-block:: bash

   pip install pyNetX

Python support
--------------

pyNetX v2.0.7 requires Python 3.11 or newer.

The release workflow builds manylinux wheels for Python 3.11, 3.12, 3.13, and
3.14.

Build from source
-----------------

Source builds require Python build tooling and native libraries.

Typical Python build dependencies:

.. code-block:: bash

   python -m pip install -U pip setuptools wheel build cmake scikit-build pybind11

Typical Debian/Ubuntu native dependencies:

.. code-block:: bash

   sudo apt-get update
   sudo apt-get install -y cmake build-essential libssh2-1-dev libtinyxml2-dev

Then install from the repository root:

.. code-block:: bash

   python -m pip install -e .

Verify installation
-------------------

Run this from outside the repository root when verifying an installed wheel:

.. code-block:: bash

   cd /tmp
   python - <<'PY'
   import pyNetX
   print("pyNetX imported from:", pyNetX.__file__)
   PY

If you run from the repository root, Python may import the local ``pyNetX/``
source directory instead of the installed wheel. That can produce this error if
the compiled extension is not present in the source tree:

.. code-block:: text

   ModuleNotFoundError: No module named 'pyNetX.pyNetX'

The fix is to run verification and wheel tests from a directory outside the
source checkout.
