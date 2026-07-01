from __future__ import annotations

from pathlib import Path

import pytest


def require_source_root(project_root: Path | None) -> Path:
    if project_root is None:
        pytest.skip("source checkout not available; static source contract tests skipped")
    return project_root


def read(project_root: Path, relative: str) -> str:
    return (project_root / relative).read_text(encoding="utf-8")


def test_health_event_source_schema_includes_label_and_timestamp(project_root):
    root = require_source_root(project_root)
    event_hpp = read(root, "include/notification_event_bus.hpp")
    bindings = read(root, "src/bindings.cpp")
    event_bus_cpp = read(root, "src/notification_event_bus.cpp")
    non_blocking_cpp = read(root, "src/netconf_client_non_blocking.cpp")

    assert "std::string timestamp" in event_hpp
    assert 'std::string label = "None"' in event_hpp
    assert "current_notification_event_timestamp_utc" in event_hpp

    assert '.def_readonly("timestamp", &NotificationHealthEvent::timestamp)' in bindings
    assert '.def_readonly("label", &NotificationHealthEvent::label)' in bindings
    assert 'doc["timestamp"] = event.timestamp' in bindings
    assert 'doc["label"] = event.label' in bindings

    assert "event.timestamp = current_notification_event_timestamp_utc();" in non_blocking_cpp
    assert "event.label = label_;" in non_blocking_cpp
    assert "timeout_event.timestamp = current_notification_event_timestamp_utc();" in event_bus_cpp
    assert 'timeout_event.label = "None";' in event_bus_cpp


def test_constructor_source_contract_includes_label(project_root):
    root = require_source_root(project_root)
    netconf_hpp = read(root, "include/netconf_client.hpp")
    common_cpp = read(root, "src/netconf_client_common.cpp")
    bindings = read(root, "src/bindings.cpp")
    stub = read(root, "pyNetX/__init__.pyi")

    assert 'const std::string& label = "None"' in netconf_hpp
    assert "std::string label_;" in netconf_hpp
    assert "const std::string& label" in common_cpp
    assert "label_(label)" in common_cpp
    assert 'py::arg("label") = "None"' in bindings
    assert 'label: str = "None"' in stub


def test_event_timestamp_helper_uses_utc_iso_8601_milliseconds(project_root):
    root = require_source_root(project_root)
    event_bus_cpp = read(root, "src/notification_event_bus.cpp")

    assert "gmtime_r" in event_bus_cpp
    assert 'std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S")' in event_bus_cpp
    assert "std::setw(3)" in event_bus_cpp
    assert "std::setfill('0')" in event_bus_cpp
    assert "<< 'Z'" in event_bus_cpp


def test_type_stub_lists_only_current_health_event_schema(project_root):
    root = require_source_root(project_root)
    stub = read(root, "pyNetX/__init__.pyi")

    expected_lines = [
        "valid: bool",
        "type: str",
        "timestamp: str",
        "label: str",
        "hostname: str",
        "port: int",
        "fd: int",
        "message: str",
        "queue_size: int",
        "queue_max_size: int",
        "queue_high_watermark: int",
        "notifications_enqueued: int",
        "notifications_dropped_queue_full: int",
        "notifications_dropped_delta: int",
        "incomplete_notifications_received: int",
        "partial_bytes: int",
        "health_events_dropped: int",
    ]
    for line in expected_lines:
        assert line in stub


def test_notification_stream_parser_source_contract_includes_today_fix(project_root):
    root = require_source_root(project_root)
    netconf_hpp = read(root, "include/netconf_client.hpp")
    non_blocking_cpp = read(root, "src/netconf_client_non_blocking.cpp")
    blocking_cpp = read(root, "src/netconf_client_blocking.cpp")

    assert "std::string _notif_rx_buffer" in netconf_hpp
    assert "_notif_rx_partial_timer_active" in netconf_hpp
    assert "_notif_rx_partial_started_at" in netconf_hpp

    assert 'NETCONF_NOTIFICATION_EOM = "]]>]]>"' in non_blocking_cpp
    assert "read_available_notification_bytes" in non_blocking_cpp
    assert "process_rx_buffer_locked" in non_blocking_cpp
    assert "process_eom_frame_locked" in non_blocking_cpp
    assert "process_recovered_missing_eom_locked" in non_blocking_cpp
    assert "find_notification_start_tag(_notif_rx_buffer, notification_start + 1)" in non_blocking_cpp
    assert "Received a new notification start before the previous notification was completed" in non_blocking_cpp
    assert '"malformed_notification"' in non_blocking_cpp
    assert '"incomplete_notification"' in non_blocking_cpp
    assert "_notif_rx_buffer.clear();" in blocking_cpp


def test_notification_stream_parser_drops_orphan_prefix_before_eom_frames(project_root):
    root = require_source_root(project_root)
    non_blocking_cpp = read(root, "src/netconf_client_non_blocking.cpp")

    orphan_message = "Received orphan notification bytes before a notification start tag; dropped orphan fragment"
    eom_loop = "eom_pos = _notif_rx_buffer.find(NETCONF_NOTIFICATION_EOM)"

    assert orphan_message in non_blocking_cpp
    assert eom_loop in non_blocking_cpp
    assert non_blocking_cpp.index(orphan_message) < non_blocking_cpp.index(eom_loop)
