# pyNetX test suite

This folder contains pytest tests for the **non-deprecated** pyNetX API. The suite is split into fast contract/unit-style tests, fake NETCONF-over-SSH integration tests, and optional real-device tests against Netopeer2/Sysrepo.

The tests intentionally avoid deprecated sync flow methods such as `connect_sync()`, `send_rpc_sync()`, `get_sync()`, `subscribe_sync()`, etc. Non-deprecated synchronous helper methods such as `next_notification()`, `peek_notifications()`, `notification_queue_size()`, `is_subscription_active()`, and `delete_subscription()` are tested because they are still part of the supported API.

## Install test dependencies

From the repository root after building/installing pyNetX:

```bash
python -m pip install -U pip
python -m pip install -e .
python -m pip install -r test/requirements.txt
```

When testing repaired wheels, install the wheel into a clean virtual environment and run pytest from outside the source tree so Python imports the installed wheel, not the checkout.

## Run commands

Run the full default suite, excluding optional Netopeer2 tests:

```bash
pytest -c test/pytest.ini test -m "not netopeer" -ra --tb=short
```

Run only fast tests that do not start a fake NETCONF server:

```bash
pytest -c test/pytest.ini test -m "not integration and not netopeer" -ra --tb=short
```

Run fake NETCONF-over-SSH integration tests:

```bash
pytest -c test/pytest.ini test -m "integration and not netopeer" -ra --tb=short
```

Run optional real Netopeer2/Sysrepo tests using a temporary Docker container:

```bash
PYNETX_RUN_NETOPEER=1 pytest -c test/pytest.ini test -m netopeer -ra --tb=short
```

Or use the helper script:

```bash
test/scripts/run_netopeer_tests.sh
```

Run optional real Netopeer2/Sysrepo tests against an already-running target:

```bash
PYNETX_NETOPEER_HOST=127.0.0.1 \
PYNETX_NETOPEER_PORT=830 \
PYNETX_NETOPEER_USERNAME=netconf \
PYNETX_NETOPEER_PASSWORD=netconf \
pytest -c test/pytest.ini test -m netopeer -ra --tb=short
```

Pytest prints the pass/fail/skip summary at the end, for example:

```text
120 passed, 0 failed, 4 skipped in 18.42s
```

## Optional Netopeer2 container

The optional Netopeer2 tests can start this image by default:

```text
sysrepo/sysrepo-netopeer2:latest
```

Override it if needed:

```bash
PYNETX_NETOPEER_IMAGE=sysrepo/sysrepo-netopeer2:latest \
PYNETX_RUN_NETOPEER=1 \
pytest -c test/pytest.ini test -m netopeer -ra --tb=short
```

A minimal compose file is included at:

```text
test/netopeer/docker-compose.yml
```

Manual usage:

```bash
cd test/netopeer
docker compose up -d
cd ../..
PYNETX_NETOPEER_HOST=127.0.0.1 \
PYNETX_NETOPEER_PORT=830 \
PYNETX_NETOPEER_USERNAME=netconf \
PYNETX_NETOPEER_PASSWORD=netconf \
pytest -c test/pytest.ini test -m netopeer -ra --tb=short
docker compose down
```

The default Netopeer2 credentials used by the tests are:

```text
username: netconf
password: netconf
```

## Test coverage map

### `test_public_api_contract.py`

Checks the public non-deprecated API surface:

- `pyNetX.__all__` contains the expected public symbols.
- `NetconfClient` exposes current async RPC methods.
- Supported notification helper methods exist.
- Deprecated sync flow methods are not treated as part of the supported contract.
- Exception classes inherit from appropriate Python built-in exception categories.
- The constructor accepts current keywords, including `label`.
- `NotificationHealthEvent` exposes all expected readonly fields and `as_dict()`.

### `test_constructor_validation.py`

Checks constructor boundary validation:

- Empty hostname rejected.
- Invalid ports rejected.
- Empty username rejected.
- Invalid connect/read/socket timeouts rejected.
- `socket_connect_timeout > connect_timeout` rejected.
- Invalid notification queue size rejected.
- Invalid incomplete-notification guard settings rejected.
- Invalid drop-event threshold rejected.
- Valid boundary values accepted.
- `label` accepts strings, including Unicode labels.
- Non-string labels are rejected by pybind type conversion.

### `test_event_bus.py`

Checks the process-wide notification health event bus:

- Timeout events have `valid=False` and `type="timeout"`.
- Timeout events include UTC millisecond timestamps.
- Timeout events include default `label="None"`.
- `as_dict()` has exactly the expected schema keys.
- `pending_notification_event_count()` and `clear_notification_events()` are stable.
- Sync and async invalid timeouts are rejected.
- Reactor/thread-pool size setters validate invalid values.

### `test_notification_helpers_without_subscription.py`

Checks non-deprecated notification helper behavior before subscription:

- Queue size is zero.
- `peek_notifications()` is safe and returns an empty list.
- Invalid `peek_notifications(max_items)` values are rejected.
- `next_notification()` and `next_notification_async()` fail clearly without an active subscription.
- `delete_subscription()` is idempotent before subscription.

### `test_async_errors_without_connection.py`

Checks async error propagation on an unconnected client:

- RPC APIs reject unconnected usage.
- `disconnect_async()` rejects when already disconnected.
- `connect_async()` to a closed local TCP port maps to connection-refused error.
- `subscribe_async()` to a closed local TCP port reports subscription failure and leaves subscription inactive.

### `test_integration_fake_netconf_server.py`

Starts a local Paramiko-based fake NETCONF-over-SSH server and tests:

- Async connect, custom raw RPC, and disconnect.
- Built-in RPC XML builders: `get`, `get-config`, `copy-config`, `delete-config`, `validate`, `edit-config`, `lock`, `unlock`, `commit`.
- `edit_config_async(..., do_validate=True)` sends edit then validate.
- `locked_edit_config_async()` sends lock, edit, commit, unlock in order.
- Bad password errors contain authentication failure details.
- Raw RPC error replies are returned to the caller according to current library behavior.
- `subscribe_async()` creates a notification subscription.
- Notifications are read from the reactor queue.
- Queue-full/drops health events include `label`, `timestamp`, hostname, port, queue sizes, counters, and high watermark.
- Queue recovery health events are emitted after draining a bounded queue.
- Default label is `"None"` when no custom label is used.
- Incomplete notification health events include partial-byte counters and timestamps.

### `test_client_lifecycle_and_concurrency.py`

Adds deeper lifecycle and concurrency integration tests:

- Multiple concurrent `send_rpc_async()` calls on one client all complete and are serialized safely on the single NETCONF channel.
- Primary RPC session remains usable while the separate notification session is subscribed.
- `disconnect_async()` cleans up active notification subscriptions.
- Two independent clients can subscribe to different fake devices without cross-contaminating notification payloads.
- A client can delete a subscription and subscribe again on a fresh notification session.

### `test_notification_queue_deep_integration.py`

Adds deeper notification queue tests:

- `peek_notifications()` is non-destructive after subscription.
- FIFO order is preserved for queued notifications.
- `peek_notifications(-1)` returns all queued notifications.
- A zero-sized bounded notification queue drops all incoming notifications and emits health events.
- Queue high-watermark tracks the peak queue depth.
- Incomplete notification size guard emits `incomplete_notification` events.

### `test_rpc_payloads_and_framing.py`

Adds payload and NETCONF 1.0 framing tests:

- Fragmented server replies are read until the NETCONF 1.0 EOM marker.
- Large custom RPC payloads are sent completely.
- Unicode RPC payloads survive UTF-8 channel transport.
- Built-in RPC builders put complete RPCs on the wire using NETCONF 1.0 EOM framing.

### `test_static_source_contracts.py`

Runs only when the source checkout is available. These tests verify code-level contracts:

- `NotificationHealthEvent` includes `timestamp` and `label`.
- Bindings expose `timestamp` and `label` as readonly fields.
- `as_dict()` includes `timestamp` and `label`.
- Generated client-side health events set `event.timestamp` and `event.label`.
- Timeout events set `timestamp` and `label="None"`.
- Constructor source, pybind args, and type stubs include `label`.
- Timestamp helper uses UTC ISO-8601 millisecond formatting.
- Type stubs list the current health-event schema.

### `test_netopeer2_optional_integration.py`

Optional tests against a real Netopeer2/Sysrepo NETCONF server:

- Connect to a real NETCONF server over SSH.
- Run `get-config` against the `running` datastore.
- Run a basic `get` RPC.
- Lock/unlock the `running` datastore when the target allows it.
- Create a NETCONF notification subscription when supported.

These tests are skipped by default because they require Docker or an external NETCONF target.

## Why there are two integration layers

The fake Paramiko server is deterministic and fast. It lets the suite trigger exact edge cases that are hard to reproduce on a real device, such as fragmented replies, queue overflow, incomplete notifications, and controlled authentication failures.

The optional Netopeer2/Sysrepo tests validate real NETCONF server interoperability. They are intentionally opt-in because Docker availability, image version, host networking, and target capabilities vary across CI systems and developer machines.

## Suggested CI gates

Fast PR gate:

```bash
pytest -c test/pytest.ini test -m "not integration and not netopeer" -ra --tb=short
```

Pre-release wheel gate:

```bash
pytest -c test/pytest.ini test -m "not netopeer" -ra --tb=short
```

Nightly or manual real-server gate:

```bash
PYNETX_RUN_NETOPEER=1 pytest -c test/pytest.ini test -m netopeer -ra --tb=short
```

## Notes for wheel testing

When building manylinux wheels, test the repaired wheel, not just the source checkout:

1. Build the wheel.
2. Repair it with `auditwheel repair`.
3. Create a fresh virtual environment.
4. Install the repaired wheel.
5. Install `test/requirements.txt`.
6. Run pytest from a directory outside the source checkout.

This catches missing shared-library bundling issues before upload.
