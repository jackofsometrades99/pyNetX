from __future__ import annotations

import pytest

from conftest import EXPECTED_HEALTH_EVENT_KEYS, assert_recent_utc_timestamp, assert_await_raises


INTEGER_HEALTH_FIELDS = {
    "port",
    "fd",
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


def assert_timeout_event(event):
    data = event.as_dict()
    assert set(data) == EXPECTED_HEALTH_EVENT_KEYS

    assert event.valid is False
    assert data["valid"] is False
    assert event.type == "timeout"
    assert data["type"] == "timeout"
    assert event.label == "None"
    assert data["label"] == "None"
    assert event.hostname == ""
    assert data["hostname"] == ""
    assert event.port == 0
    assert data["port"] == 0
    assert event.fd == -1
    assert data["fd"] == -1
    assert "No notification health event available" in event.message
    assert data["message"] == event.message

    assert_recent_utc_timestamp(event.timestamp)
    assert data["timestamp"] == event.timestamp

    for field in INTEGER_HEALTH_FIELDS:
        assert isinstance(data[field], int), field

    assert data["queue_size"] == 0
    assert data["queue_max_size"] == -1
    assert data["queue_high_watermark"] == 0
    assert data["notifications_enqueued"] == 0
    assert data["notifications_dropped_queue_full"] == 0
    assert data["notifications_dropped_delta"] == 0
    assert data["incomplete_notifications_received"] == 0
    assert data["partial_bytes"] == 0


def test_next_notification_event_zero_timeout_returns_structured_timeout(pyNetX_module):
    assert pyNetX_module.pending_notification_event_count() == 0
    event = pyNetX_module.next_notification_event(timeout_ms=0)
    assert_timeout_event(event)
    assert pyNetX_module.pending_notification_event_count() == 0


def test_next_notification_event_positive_timeout_returns_structured_timeout(pyNetX_module):
    event = pyNetX_module.next_notification_event(timeout_ms=5)
    assert_timeout_event(event)


@pytest.mark.asyncio
async def test_next_notification_event_async_zero_timeout_returns_structured_timeout(pyNetX_module):
    event = await pyNetX_module.next_notification_event_async(timeout_ms=0)
    assert_timeout_event(event)
    assert pyNetX_module.pending_notification_event_count() == 0


@pytest.mark.asyncio
async def test_next_notification_event_async_positive_timeout_returns_structured_timeout(pyNetX_module):
    event = await pyNetX_module.next_notification_event_async(timeout_ms=5)
    assert_timeout_event(event)


@pytest.mark.parametrize("timeout_ms", [-2, -100])
def test_next_notification_event_rejects_invalid_negative_timeout(pyNetX_module, timeout_ms):
    # Synchronous pybind validation failures are currently exposed as RuntimeError.
    with pytest.raises(RuntimeError) as excinfo:
        pyNetX_module.next_notification_event(timeout_ms=timeout_ms)
    assert "timeout_ms must be -1" in str(excinfo.value)


@pytest.mark.asyncio
@pytest.mark.parametrize("timeout_ms", [-2, -100])
async def test_next_notification_event_async_rejects_invalid_negative_timeout(pyNetX_module, timeout_ms):
    message = await assert_await_raises(
        pyNetX_module.next_notification_event_async(timeout_ms=timeout_ms),
        pyNetX_module.NetconfException,
    )
    assert "timeout_ms must be -1" in message


def test_clear_notification_events_is_idempotent(pyNetX_module):
    pyNetX_module.clear_notification_events()
    pyNetX_module.clear_notification_events()
    assert pyNetX_module.pending_notification_event_count() == 0


def test_event_bus_as_dict_matches_readonly_attributes(pyNetX_module):
    event = pyNetX_module.next_notification_event(timeout_ms=0)
    data = event.as_dict()
    for key in EXPECTED_HEALTH_EVENT_KEYS:
        assert data[key] == getattr(event, key)


def test_reactor_count_rejects_zero(pyNetX_module):
    with pytest.raises(Exception) as excinfo:
        pyNetX_module.set_notification_reactor_count(0)
    assert "greater than 0" in str(excinfo.value)


def test_reactor_count_accepts_positive_value(pyNetX_module):
    pyNetX_module.set_notification_reactor_count(1)


def test_threadpool_size_rejects_non_positive_values(pyNetX_module):
    for value in (0, -1):
        with pytest.raises(RuntimeError) as excinfo:
            pyNetX_module.set_threadpool_size(value)
        assert "Invalid thread pool size" in str(excinfo.value)
