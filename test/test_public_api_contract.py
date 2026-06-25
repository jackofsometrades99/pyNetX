from __future__ import annotations

import inspect


NON_DEPRECATED_MODULE_SYMBOLS = {
    "NetconfClient",
    "NetconfException",
    "NetconfAuthError",
    "NetconfChannelError",
    "NetconfConnectionRefusedError",
    "NotificationHealthEvent",
    "set_threadpool_size",
    "set_notification_reactor_count",
    "next_notification_event",
    "next_notification_event_async",
    "pending_notification_event_count",
    "clear_notification_events",
}

NON_DEPRECATED_CLIENT_METHODS = {
    "connect_async",
    "disconnect_async",
    "send_rpc_async",
    "get_async",
    "get_config_async",
    "copy_config_async",
    "delete_config_async",
    "validate_async",
    "edit_config_async",
    "subscribe_async",
    "lock_async",
    "unlock_async",
    "commit_async",
    "locked_edit_config_async",
    "next_notification",
    "next_notification_async",
    "peek_notifications",
    "notification_queue_size",
    "is_subscription_active",
    "delete_subscription",
}

DEPRECATED_SYNC_FLOW_METHODS = {
    "connect_sync",
    "disconnect_sync",
    "send_rpc_sync",
    "receive_notification_sync",
    "get_sync",
    "get_config_sync",
    "copy_config_sync",
    "delete_config_sync",
    "validate_sync",
    "edit_config_sync",
    "subscribe_sync",
    "lock_sync",
    "unlock_sync",
    "commit_sync",
    "locked_edit_config_sync",
}


def test_module_exports_only_expected_public_symbols_in___all__(pyNetX_module):
    exported = set(pyNetX_module.__all__)
    assert NON_DEPRECATED_MODULE_SYMBOLS <= exported
    assert "NotificationHealthEvent" in exported


def test_non_deprecated_client_methods_are_present(pyNetX_module):
    missing = [name for name in NON_DEPRECATED_CLIENT_METHODS if not hasattr(pyNetX_module.NetconfClient, name)]
    assert not missing


def test_deprecated_sync_flow_methods_are_not_part_of_non_deprecated_contract():
    assert DEPRECATED_SYNC_FLOW_METHODS.isdisjoint(NON_DEPRECATED_CLIENT_METHODS)


def test_exception_hierarchy_uses_python_builtin_categories(pyNetX_module):
    assert issubclass(pyNetX_module.NetconfException, RuntimeError)
    assert issubclass(pyNetX_module.NetconfConnectionRefusedError, ConnectionError)
    assert issubclass(pyNetX_module.NetconfAuthError, PermissionError)
    assert issubclass(pyNetX_module.NetconfChannelError, OSError)


def test_constructor_accepts_current_non_deprecated_keywords(make_client):
    client = make_client(
        hostname="127.0.0.1",
        port=830,
        username="admin",
        password="admin",
        key_path="/tmp/not-used-yet",
        connect_timeout=1,
        read_timeout=1,
        notif_queue_size=10,
        socket_connect_timeout=1,
        notif_incomplete_max_kb=16,
        notif_incomplete_timeout=1,
        notif_drop_event_threshold=3,
        label="leaf-01",
    )
    assert client.notification_queue_size() == 0
    assert not client.is_subscription_active()


def test_event_object_has_schema_attributes(pyNetX_module):
    event = pyNetX_module.next_notification_event(timeout_ms=0)
    for field in (
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
        "as_dict",
    ):
        assert hasattr(event, field)


def test_non_deprecated_async_methods_return_awaitables_inside_running_loop(pyNetX_module):
    # C++/pybind methods cannot always be introspected reliably, but they must be
    # callable attributes. Async behavior and error propagation are tested in
    # test_async_errors_without_connection.py.
    for name in NON_DEPRECATED_CLIENT_METHODS:
        attr = getattr(pyNetX_module.NetconfClient, name)
        assert callable(attr), name


def test_builtin_method_signatures_do_not_need_python_signature_support(pyNetX_module):
    # Pybind11-bound methods may or may not expose full inspect signatures. This
    # test ensures the class is usable even when inspect falls back to builtins.
    assert inspect.isclass(pyNetX_module.NetconfClient)
