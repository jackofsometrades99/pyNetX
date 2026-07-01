from __future__ import annotations

import datetime as _dt
import importlib
import re
from pathlib import Path
from typing import Any

import pytest


ISO_UTC_MILLIS_RE = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$"
)


@pytest.fixture(scope="session")
def pyNetX_module():
    """Import the installed/built pyNetX extension once for the test session."""
    try:
        return importlib.import_module("pyNetX")
    except Exception as exc:  # pragma: no cover - only used for setup failures
        pytest.fail(
            "Could not import pyNetX. Build/install the package before running tests. "
            "Example: python -m pip install -e .\n"
            f"Original import error: {exc!r}"
        )


@pytest.fixture(autouse=True)
def clear_global_notification_events(request):
    """Keep the process-wide health event bus isolated between tests.

    Static source-contract tests do not need the compiled pyNetX extension, so
    avoid importing it unless the test actually requested ``pyNetX_module``.
    """
    if "pyNetX_module" not in request.fixturenames:
        yield
        return

    pyNetX_module = request.getfixturevalue("pyNetX_module")
    pyNetX_module.clear_notification_events()
    yield
    pyNetX_module.clear_notification_events()


@pytest.fixture
def make_client(pyNetX_module):
    def _make_client(**overrides: Any):
        kwargs: dict[str, Any] = {
            "hostname": "127.0.0.1",
            "port": 830,
            "username": "admin",
            "password": "admin",
            "connect_timeout": 1,
            "read_timeout": 1,
            "notif_queue_size": -1,
            "socket_connect_timeout": 1,
            "notif_incomplete_max_kb": 1024,
            "notif_incomplete_timeout": 1,
            "notif_drop_event_threshold": 1,
            "label": "None",
        }
        kwargs.update(overrides)
        return pyNetX_module.NetconfClient(**kwargs)

    return _make_client


@pytest.fixture(scope="session")
def project_root() -> Path | None:
    """Return the source repository root when tests are run from a checkout."""
    here = Path(__file__).resolve()
    candidates = [here.parent, *here.parents]
    for candidate in candidates:
        if (candidate / "src" / "bindings.cpp").exists() and (
            candidate / "include" / "netconf_client.hpp"
        ).exists():
            return candidate
    return None


def parse_utc_millis_timestamp(value: str) -> _dt.datetime:
    assert isinstance(value, str), "timestamp must be a string"
    assert ISO_UTC_MILLIS_RE.match(value), (
        "timestamp must be UTC ISO-8601 with millisecond precision, "
        f"got {value!r}"
    )
    return _dt.datetime.fromisoformat(value.replace("Z", "+00:00"))


def assert_recent_utc_timestamp(value: str, *, max_skew_seconds: int = 60) -> None:
    parsed = parse_utc_millis_timestamp(value)
    now = _dt.datetime.now(_dt.timezone.utc)
    delta = abs((now - parsed).total_seconds())
    assert delta <= max_skew_seconds, (
        f"timestamp {value!r} is not within {max_skew_seconds}s of now"
    )


async def assert_await_raises(awaitable, expected_exception):
    with pytest.raises(expected_exception) as excinfo:
        await awaitable
    return str(excinfo.value)


EXPECTED_HEALTH_EVENT_KEYS = {
    "valid",
    "type",
    "timestamp",
    "label",
    "hostname",
    "port",
    "fd",
    "message",
    "queue_size",
    "queue_max_size",
    "queue_high_watermark",
    "notifications_enqueued",
    "notifications_dropped_queue_full",
    "notifications_dropped_delta",
    "incomplete_notifications_received",
    "partial_bytes",
    "health_events_dropped",
}
