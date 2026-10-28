# Copyright (c) 2026 EyeCrasher.
# SPDX-License-Identifier: MIT

"""Linux process discovery and stable handles for a JVM started by a launcher."""

from __future__ import annotations

import os
import select
import shlex
import signal
import subprocess
from pathlib import Path


def command_lines(names: set[str], *, required: bool = False) -> list[tuple[int, str]]:
    processes = []
    try:
        entries = list(Path("/proc").iterdir())
    except OSError:
        if required:
            raise RuntimeError("Cannot inspect Linux processes through /proc") from None
        return []
    for entry in entries:
        if not entry.name.isdigit():
            continue
        try:
            if (entry / "comm").read_text().strip() not in names:
                continue
            args = (entry / "cmdline").read_bytes().rstrip(b"\0").split(b"\0")
            line = shlex.join(os.fsdecode(arg) for arg in args)
        except OSError:
            continue
        if line:
            processes.append((int(entry.name), line))
    return processes


class PidProcess:
    """Follow a non-child through pidfd, without ever signaling a reused PID.

    Linux cannot report a non-child's exit status, so an exited process returns -1.
    """

    def __init__(self, pid: int) -> None:
        self.pid = pid
        self._fd: int | None = None
        try:
            self._fd = os.pidfd_open(pid)
        except ProcessLookupError:
            pass

    def poll(self) -> int | None:
        return self.wait(0) if self._ready(0) else None

    def _ready(self, milliseconds: int) -> bool:
        if self._fd is None:
            return True
        poller = select.poll()
        poller.register(self._fd, select.POLLIN)
        return bool(poller.poll(milliseconds))

    def wait(self, timeout: float | None = None) -> int:
        if not self._ready(-1 if timeout is None else max(0, int(timeout * 1000))):
            raise subprocess.TimeoutExpired(f"process {self.pid}", timeout or 0.0)
        self._close()
        return -1

    def _signal(self, number: int) -> None:
        if self.poll() is None and self._fd is not None:
            try:
                signal.pidfd_send_signal(self._fd, number)
            except ProcessLookupError:
                self._close()

    def terminate(self) -> None:
        self._signal(signal.SIGTERM)

    def kill(self) -> None:
        self._signal(signal.SIGKILL)

    def _close(self) -> None:
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    def __del__(self) -> None:
        self._close()
