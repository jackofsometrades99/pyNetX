from __future__ import annotations

import pytest


BASE_CLIENT_KWARGS = {
    "hostname": "127.0.0.1",
    "port": 830,
    "username": "admin",
    "password": "admin",
    "connect_timeout": 5,
    "read_timeout": 5,
    "notif_queue_size": -1,
    "socket_connect_timeout": 1,
    "notif_incomplete_max_kb": 1024,
    "notif_incomplete_timeout": 5,
    "notif_drop_event_threshold": 1,
    "label": "None",
}


@pytest.mark.parametrize(
    ("override", "message"),
    [
        ({"hostname": ""}, "hostname cannot be empty"),
        ({"port": 0}, "port must be between 1 and 65535"),
        ({"port": -1}, "port must be between 1 and 65535"),
        ({"port": 65536}, "port must be between 1 and 65535"),
        ({"username": ""}, "username cannot be empty"),
        ({"connect_timeout": 0}, "connect_timeout must be greater than 0"),
        ({"connect_timeout": -1}, "connect_timeout must be greater than 0"),
        ({"read_timeout": 0}, "read_timeout must be greater than 0"),
        ({"read_timeout": -1}, "read_timeout must be greater than 0"),
        ({"socket_connect_timeout": 0}, "socket_connect_timeout must be greater than 0"),
        ({"socket_connect_timeout": -1}, "socket_connect_timeout must be greater than 0"),
        (
            {"connect_timeout": 1, "socket_connect_timeout": 2},
            "socket_connect_timeout cannot be greater than connect_timeout",
        ),
        (
            {"notif_queue_size": -2},
            "notif_queue_size must be -1 for unbounded or >= 0 for bounded queue",
        ),
        (
            {"notif_incomplete_max_kb": 0},
            "notif_incomplete_max_kb must be -1 to disable or greater than 0",
        ),
        (
            {"notif_incomplete_max_kb": -2},
            "notif_incomplete_max_kb must be -1 to disable or greater than 0",
        ),
        (
            {"notif_incomplete_timeout": 0},
            "notif_incomplete_timeout must be -1 to disable or greater than 0",
        ),
        (
            {"notif_incomplete_timeout": -2},
            "notif_incomplete_timeout must be -1 to disable or greater than 0",
        ),
        (
            {"notif_incomplete_max_kb": -1, "notif_incomplete_timeout": -1},
            "At least one of notif_incomplete_max_kb or notif_incomplete_timeout must be enabled",
        ),
        (
            {"notif_drop_event_threshold": 0},
            "notif_drop_event_threshold must be greater than 0",
        ),
        (
            {"notif_drop_event_threshold": -1},
            "notif_drop_event_threshold must be greater than 0",
        ),
    ],
)
def test_constructor_rejects_invalid_values(pyNetX_module, override, message):
    kwargs = dict(BASE_CLIENT_KWARGS)
    kwargs.update(override)
    with pytest.raises(Exception) as excinfo:
        pyNetX_module.NetconfClient(**kwargs)
    assert message in str(excinfo.value)


@pytest.mark.parametrize(
    "override",
    [
        {},
        {"password": ""},
        {"key_path": ""},
        {"key_path": "/tmp/key-that-is-not-used-yet"},
        {"notif_queue_size": -1},
        {"notif_queue_size": 0},
        {"notif_queue_size": 1},
        {"notif_incomplete_max_kb": -1, "notif_incomplete_timeout": 1},
        {"notif_incomplete_max_kb": 1, "notif_incomplete_timeout": -1},
        {"label": "leaf-01"},
        {"label": "spine/दिल्ली/東京"},
    ],
)
def test_constructor_accepts_valid_boundary_values(pyNetX_module, override):
    kwargs = dict(BASE_CLIENT_KWARGS)
    kwargs.update(override)
    client = pyNetX_module.NetconfClient(**kwargs)
    assert client.notification_queue_size() == 0
    assert client.peek_notifications(-1) == []
    assert not client.is_subscription_active()


@pytest.mark.parametrize("bad_label", [None, 123, 1.5, object()])
def test_label_parameter_is_string_only(pyNetX_module, bad_label):
    kwargs = dict(BASE_CLIENT_KWARGS)
    kwargs["label"] = bad_label
    with pytest.raises(TypeError):
        pyNetX_module.NetconfClient(**kwargs)
