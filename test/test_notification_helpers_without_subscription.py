from __future__ import annotations

import pytest

from conftest import assert_await_raises


def test_queue_helpers_are_safe_before_subscription(make_client):
    client = make_client()
    assert client.notification_queue_size() == 0
    assert client.peek_notifications() == []
    assert client.peek_notifications(0) == []
    assert client.peek_notifications(1) == []
    assert client.peek_notifications(-1) == []
    assert not client.is_subscription_active()


def test_peek_notifications_rejects_invalid_max_items(make_client, pyNetX_module):
    client = make_client()
    # Synchronous pybind validation failures are currently exposed as RuntimeError.
    with pytest.raises(RuntimeError) as excinfo:
        client.peek_notifications(-2)
    assert "max_items must be -1" in str(excinfo.value)


def test_next_notification_requires_active_subscription(make_client, pyNetX_module):
    client = make_client()
    # Synchronous queue-read failures are currently exposed as RuntimeError.
    with pytest.raises(RuntimeError) as excinfo:
        client.next_notification(timeout_ms=0)
    message = str(excinfo.value)
    assert "Unable to read from queue" in message
    assert "Notification channel not open" in message


@pytest.mark.asyncio
async def test_next_notification_async_requires_active_subscription(make_client, pyNetX_module):
    client = make_client()
    message = await assert_await_raises(
        client.next_notification_async(timeout_ms=0),
        pyNetX_module.NetconfException,
    )
    assert "Unable to read from queue" in message
    assert "Notification channel not open" in message


def test_delete_subscription_is_non_deprecated_and_idempotent(make_client):
    client = make_client()
    assert not client.is_subscription_active()
    client.delete_subscription()
    client.delete_subscription()
    assert not client.is_subscription_active()
    assert client.notification_queue_size() == 0
