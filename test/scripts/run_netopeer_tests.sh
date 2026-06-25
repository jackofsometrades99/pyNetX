#!/usr/bin/env bash
set -euo pipefail

# Starts a temporary Netopeer2/Sysrepo container through the pytest fixture and
# runs only the tests marked `netopeer`.
export PYNETX_RUN_NETOPEER="${PYNETX_RUN_NETOPEER:-1}"
export PYNETX_NETOPEER_USERNAME="${PYNETX_NETOPEER_USERNAME:-netconf}"
export PYNETX_NETOPEER_PASSWORD="${PYNETX_NETOPEER_PASSWORD:-netconf}"
export PYNETX_NETOPEER_IMAGE="${PYNETX_NETOPEER_IMAGE:-sysrepo/sysrepo-netopeer2:latest}"

python -m pytest -c test/pytest.ini test -m netopeer -ra --tb=short
