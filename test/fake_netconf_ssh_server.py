from __future__ import annotations

import queue
import socket
import threading
import time
from dataclasses import dataclass
from typing import Callable, Iterable

import pytest

paramiko = pytest.importorskip("paramiko")

NETCONF_EOM = "]]>]]>"

SERVER_HELLO = (
    '<?xml version="1.0" encoding="UTF-8"?>'
    '<hello xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">'
    '<capabilities>'
    '<capability>urn:ietf:params:netconf:base:1.0</capability>'
    '<capability>urn:ietf:params:netconf:capability:writable-running:1.0</capability>'
    '<capability>urn:ietf:params:netconf:capability:candidate:1.0</capability>'
    '<capability>urn:ietf:params:xml:ns:netconf:notification:1.0</capability>'
    '</capabilities>'
    '<session-id>101</session-id>'
    '</hello>'
    + NETCONF_EOM
)

OK_REPLY = '<rpc-reply message-id="101"><ok/></rpc-reply>' + NETCONF_EOM

ERROR_REPLY = (
    '<rpc-reply message-id="101">'
    '<rpc-error>'
    '<error-type>application</error-type>'
    '<error-tag>operation-failed</error-tag>'
    '<error-severity>error</error-severity>'
    '<error-message>fake server forced RPC error</error-message>'
    '</rpc-error>'
    '</rpc-reply>'
    + NETCONF_EOM
)


@dataclass(frozen=True)
class RpcRecord:
    index: int
    text: str


class _NetconfSSHServerInterface(paramiko.ServerInterface):
    def __init__(self, username: str, password: str):
        self.username = username
        self.password = password
        self.subsystem_requested = threading.Event()

    def check_auth_password(self, username: str, password: str):
        if username == self.username and password == self.password:
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def get_allowed_auths(self, username: str) -> str:
        return "password"

    def check_channel_request(self, kind: str, chanid: int):
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_subsystem_request(self, channel, name: str) -> bool:
        if name == "netconf":
            self.subsystem_requested.set()
            return True
        return False


class FakeNetconfSSHServer:
    """Minimal NETCONF-over-SSH server for exercising the pyNetX libssh2 client.

    The server accepts password auth, the `netconf` subsystem, exchanges NETCONF
    1.0 hello messages, records each RPC, replies `<ok/>` by default, and can
    push notifications after `<create-subscription>`.
    """

    def __init__(
        self,
        *,
        username: str = "admin",
        password: str = "admin",
        rpc_responder: Callable[[str], str] | None = None,
        notifications: Iterable[str] | None = None,
        notification_raw_chunks: Iterable[str] | None = None,
        incomplete_notification: str | None = None,
        notification_start_delay: float = 0.20,
        notification_interval: float = 0.05,
        reply_chunk_size: int | None = None,
        reply_chunk_delay: float = 0.0,
    ):
        self.username = username
        self.password = password
        self.rpc_responder = rpc_responder or (lambda rpc: OK_REPLY)
        self.notifications = list(notifications or [])
        self.notification_raw_chunks = list(notification_raw_chunks or [])
        self.incomplete_notification = incomplete_notification
        self.notification_start_delay = notification_start_delay
        self.notification_interval = notification_interval
        self.reply_chunk_size = reply_chunk_size
        self.reply_chunk_delay = reply_chunk_delay

        self._host_key = paramiko.RSAKey.generate(2048)
        self._stop = threading.Event()
        self._ready = threading.Event()
        self._records_lock = threading.Lock()
        self._records: list[RpcRecord] = []
        self._records_queue: queue.Queue[RpcRecord] = queue.Queue()
        self._threads: list[threading.Thread] = []
        self._transports: list[paramiko.Transport] = []

        self._listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listen_sock.bind(("127.0.0.1", 0))
        self._listen_sock.listen(50)
        self._listen_sock.settimeout(0.25)
        self.host, self.port = self._listen_sock.getsockname()

    def __enter__(self) -> "FakeNetconfSSHServer":
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    @property
    def records(self) -> list[RpcRecord]:
        with self._records_lock:
            return list(self._records)

    @property
    def rpc_texts(self) -> list[str]:
        return [record.text for record in self.records]

    def start(self) -> None:
        thread = threading.Thread(target=self._accept_loop, name="fake-netconf-accept", daemon=True)
        thread.start()
        self._threads.append(thread)
        assert self._ready.wait(2.0), "fake NETCONF server did not start"

    def close(self) -> None:
        self._stop.set()
        try:
            self._listen_sock.close()
        except OSError:
            pass
        for transport in list(self._transports):
            try:
                transport.close()
            except Exception:
                pass
        for thread in list(self._threads):
            if thread.is_alive():
                thread.join(timeout=1.0)

    def wait_for_rpc(
        self,
        predicate: Callable[[str], bool],
        *,
        timeout: float = 5.0,
        after_index: int = 0,
    ) -> RpcRecord:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            records = self.records
            for record in records:
                if record.index >= after_index and predicate(record.text):
                    return record
            time.sleep(0.02)
        raise AssertionError(f"timed out waiting for RPC; current RPCs={self.rpc_texts!r}")

    def _accept_loop(self) -> None:
        self._ready.set()
        while not self._stop.is_set():
            try:
                client_sock, _addr = self._listen_sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            thread = threading.Thread(
                target=self._handle_client,
                args=(client_sock,),
                name="fake-netconf-client",
                daemon=True,
            )
            thread.start()
            self._threads.append(thread)

    def _handle_client(self, client_sock: socket.socket) -> None:
        transport = None
        try:
            transport = paramiko.Transport(client_sock)
            self._transports.append(transport)
            transport.add_server_key(self._host_key)
            server = _NetconfSSHServerInterface(self.username, self.password)
            transport.start_server(server=server)

            channel = transport.accept(10.0)
            if channel is None:
                return
            if not server.subsystem_requested.wait(10.0):
                return

            channel.settimeout(0.25)
            self._send_text(channel, SERVER_HELLO)
            self._read_until_eom(channel, timeout=10.0)  # client hello

            while not self._stop.is_set() and transport.is_active():
                rpc = self._read_until_eom(channel, timeout=0.50)
                if not rpc:
                    continue

                cleaned = self._strip_eom(rpc)
                record = self._record_rpc(cleaned)

                reply = self.rpc_responder(cleaned)
                self._send_text(channel, reply)

                if "<create-subscription" in cleaned:
                    sender = threading.Thread(
                        target=self._send_notifications,
                        args=(channel,),
                        name=f"fake-netconf-notifications-{record.index}",
                        daemon=True,
                    )
                    sender.start()
                    self._threads.append(sender)
        except Exception:
            # The client may close sockets aggressively during disconnect. Tests
            # assert through the client-facing behavior and recorded RPCs.
            return
        finally:
            if transport is not None:
                try:
                    transport.close()
                except Exception:
                    pass
            try:
                client_sock.close()
            except Exception:
                pass

    def _record_rpc(self, rpc: str) -> RpcRecord:
        with self._records_lock:
            record = RpcRecord(index=len(self._records), text=rpc)
            self._records.append(record)
            self._records_queue.put(record)
            return record

    def _send_notifications(self, channel) -> None:
        time.sleep(self.notification_start_delay)

        # Raw chunks are sent exactly as provided. These are used by stream-parser
        # tests to simulate bad devices and TCP/SSH coalescing/splitting cases:
        # multiple EOM-delimited notifications in one read, trailing partial
        # frames, missing EOM between notifications, empty EOM frames, etc.
        for chunk in self.notification_raw_chunks:
            if self._stop.is_set():
                return
            try:
                self._send_all(channel, chunk)
            except Exception:
                return
            time.sleep(self.notification_interval)

        for payload in self.notifications:
            if self._stop.is_set():
                return
            message = payload if payload.endswith(NETCONF_EOM) else payload + NETCONF_EOM
            try:
                self._send_all(channel, message)
            except Exception:
                return
            time.sleep(self.notification_interval)

        if self.incomplete_notification is not None and not self._stop.is_set():
            try:
                # Intentionally no EOM. This exercises the client's incomplete
                # notification guard path.
                self._send_all(channel, self.incomplete_notification)
            except Exception:
                return

    def _send_text(self, channel, text: str) -> None:
        if self.reply_chunk_size is None or self.reply_chunk_size <= 0:
            self._send_all(channel, text)
            return

        for offset in range(0, len(text), self.reply_chunk_size):
            self._send_all(channel, text[offset : offset + self.reply_chunk_size])
            if self.reply_chunk_delay > 0:
                time.sleep(self.reply_chunk_delay)

    @staticmethod
    def _send_all(channel, text: str) -> None:
        data = text.encode("utf-8")
        view = memoryview(data)
        total = 0
        while total < len(data):
            sent = channel.send(view[total:])
            if sent <= 0:
                raise OSError("channel send returned 0")
            total += sent

    @staticmethod
    def _strip_eom(text: str) -> str:
        return text.replace(NETCONF_EOM, "").strip()

    @staticmethod
    def _read_until_eom(channel, *, timeout: float) -> str:
        deadline = time.monotonic() + timeout
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            try:
                chunk = channel.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                return ""
            chunks.append(chunk)
            data = b"".join(chunks)
            if NETCONF_EOM.encode("ascii") in data:
                return data.decode("utf-8", errors="replace")
        return ""


def notification_xml(sequence: int, body: str = "<event>changed</event>") -> str:
    return (
        '<notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0">'
        f'<eventTime>2026-06-25T00:00:{sequence:02d}Z</eventTime>'
        f'<sequence>{sequence}</sequence>'
        f'{body}'
        '</notification>'
    )
