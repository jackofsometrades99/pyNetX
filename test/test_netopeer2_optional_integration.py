from __future__ import annotations

import os
import shutil
import socket
import subprocess
import time
from dataclasses import dataclass

import pytest

pytestmark = [pytest.mark.netopeer, pytest.mark.integration, pytest.mark.slow]


@dataclass(frozen=True)
class NetopeerTarget:
    host: str
    port: int
    username: str
    password: str
    container_id: str | None = None


def _tcp_port_open(host: str, port: int, *, timeout: float = 1.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def _docker(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["docker", *args],
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


@pytest.fixture(scope="session")
def netopeer_target() -> NetopeerTarget:
    """Return either an external Netopeer2 target or a temporary Docker target.

    By default these tests are skipped. Enable one of these modes:

    1. Existing NETCONF server:
       PYNETX_NETOPEER_HOST=127.0.0.1 PYNETX_NETOPEER_PORT=830 pytest -m netopeer

    2. Test-managed Docker container:
       PYNETX_RUN_NETOPEER=1 pytest -m netopeer
    """
    username = os.getenv("PYNETX_NETOPEER_USERNAME", "netconf")
    password = os.getenv("PYNETX_NETOPEER_PASSWORD", "netconf")

    external_host = os.getenv("PYNETX_NETOPEER_HOST")
    if external_host:
        target = NetopeerTarget(
            host=external_host,
            port=int(os.getenv("PYNETX_NETOPEER_PORT", "830")),
            username=username,
            password=password,
        )
        if not _tcp_port_open(target.host, target.port, timeout=2.0):
            pytest.skip(f"external Netopeer2 target {target.host}:{target.port} is not reachable")
        yield target
        return

    if os.getenv("PYNETX_RUN_NETOPEER", "0") != "1":
        pytest.skip(
            "Netopeer2 tests are opt-in. Set PYNETX_RUN_NETOPEER=1 to start a Docker container, "
            "or set PYNETX_NETOPEER_HOST/PYNETX_NETOPEER_PORT for an existing target."
        )

    if shutil.which("docker") is None:
        pytest.skip("docker CLI is not available")

    image = os.getenv("PYNETX_NETOPEER_IMAGE", "sysrepo/sysrepo-netopeer2:latest")
    name = f"pynetx-netopeer2-test-{os.getpid()}"

    _docker("pull", image)
    run = _docker(
        "run",
        "-d",
        "--rm",
        "--name",
        name,
        "-p",
        "127.0.0.1::830",
        image,
    )
    container_id = run.stdout.strip()

    try:
        port_out = _docker("port", name, "830/tcp").stdout.strip()
        # Docker prints values such as "127.0.0.1:49153".
        mapped_port = int(port_out.rsplit(":", 1)[1])

        deadline = time.monotonic() + float(os.getenv("PYNETX_NETOPEER_STARTUP_TIMEOUT", "45"))
        while time.monotonic() < deadline:
            if _tcp_port_open("127.0.0.1", mapped_port, timeout=1.0):
                break
            time.sleep(0.5)
        else:
            logs = _docker("logs", name, check=False).stdout
            pytest.skip(f"Netopeer2 container did not open port 830 in time. Logs:\n{logs}")

        yield NetopeerTarget(
            host="127.0.0.1",
            port=mapped_port,
            username=username,
            password=password,
            container_id=container_id,
        )
    finally:
        _docker("rm", "-f", name, check=False)


def make_netopeer_client(pyNetX_module, target: NetopeerTarget, **overrides):
    kwargs = {
        "hostname": target.host,
        "port": target.port,
        "username": target.username,
        "password": target.password,
        "connect_timeout": 20,
        "read_timeout": 20,
        "socket_connect_timeout": 10,
        "notif_queue_size": 10,
        "notif_incomplete_max_kb": 1024,
        "notif_incomplete_timeout": 5,
        "notif_drop_event_threshold": 1,
        "label": "netopeer2-real-device",
    }
    kwargs.update(overrides)
    return pyNetX_module.NetconfClient(**kwargs)


@pytest.mark.asyncio
async def test_netopeer2_connect_and_get_running_config(pyNetX_module, netopeer_target):
    client = make_netopeer_client(pyNetX_module, netopeer_target)
    try:
        assert await client.connect_async() is True
        reply = await client.get_config_async("running")
        assert "<rpc-reply" in reply
        assert "</rpc-reply>" in reply
        assert "<rpc-error>" not in reply
    finally:
        try:
            await client.disconnect_async()
        except Exception:
            pass


@pytest.mark.asyncio
async def test_netopeer2_basic_get_rpc(pyNetX_module, netopeer_target):
    client = make_netopeer_client(pyNetX_module, netopeer_target)
    try:
        assert await client.connect_async() is True
        reply = await client.get_async()
        assert "<rpc-reply" in reply
        assert "</rpc-reply>" in reply
        assert "<rpc-error>" not in reply
    finally:
        try:
            await client.disconnect_async()
        except Exception:
            pass


@pytest.mark.asyncio
async def test_netopeer2_lock_unlock_running_datastore_when_allowed(pyNetX_module, netopeer_target):
    client = make_netopeer_client(pyNetX_module, netopeer_target)
    try:
        assert await client.connect_async() is True
        lock_reply = await client.lock_async("running")
        if "<rpc-error>" in lock_reply:
            pytest.skip(f"target rejected lock RPC: {lock_reply[:300]}")
        assert "<ok/>" in lock_reply or "<ok />" in lock_reply

        unlock_reply = await client.unlock_async("running")
        assert "<rpc-error>" not in unlock_reply
        assert "<ok/>" in unlock_reply or "<ok />" in unlock_reply
    finally:
        try:
            await client.disconnect_async()
        except Exception:
            pass


@pytest.mark.asyncio
async def test_netopeer2_subscription_creation_when_supported(pyNetX_module, netopeer_target):
    client = make_netopeer_client(pyNetX_module, netopeer_target)
    try:
        reply = await client.subscribe_async(stream="NETCONF")
        if "<rpc-error>" in reply:
            pytest.skip(f"target rejected create-subscription RPC: {reply[:300]}")
        assert "<rpc-reply" in reply
        assert "<ok/>" in reply or "<ok />" in reply
        assert client.is_subscription_active()
    finally:
        try:
            client.delete_subscription()
        except Exception:
            pass
