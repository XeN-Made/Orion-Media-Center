#!/usr/bin/env python3
# ORION Media Center - Asks ORION on a console to check itself, and brings the report back.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs ORION's checks on a console (src/orion/self_test.hpp) and prints them.

usage: PS5_HOST=<address> tools/console-check.py <results dir> [design id|-] [--quit]

Uses the ORION already installed (make deploy first); it uploads nothing but
two request files. Steps: check the console's services; start ORION through
its launch helper unless it is running already; write the check request
to /data/orion/dev-request.txt; wait for /data/orion/selftest/report.txt
with this run's token; download it with ORION's logs; with a design id, wait
for that design's tour too and download its pictures; check from the PC
that the console's share answers when sharing is on. ORION stays open
unless --quit is given (or a tour ran: it closes itself a few minutes after
one). The exit status is 1 when a check failed.

It never kills the app and never retries. If something is wrong it stops
and says what it saw. Ask the console's owner before every run.

Environment:
  PS5_HOST       console address (required)
  FTP_PORT       default 2121
  ELF_PORT       default 9021 (ELF loader, for the launch helper)
  CHECK_TIMEOUT  seconds to wait for the report (default 180)
  TOUR_TIMEOUT   seconds to wait for a tour after the checks (default 900)
"""

import io
import json
import os
import socket
import sys
import time
from ftplib import FTP, all_errors
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATE = "/data/orion"


def say(text):
    print(f"==> {text}", flush=True)


def stop(text):
    raise SystemExit(f"STOPPED: {text}")


def port_open(host, port):
    try:
        with socket.create_connection((host, port), timeout=5):
            return True
    except OSError:
        return False


class Console:
    """One FTP session (the console's server lists only the current folder)."""

    def __init__(self, host, port):
        try:
            self.ftp = FTP()
            self.ftp.connect(host, port, timeout=30)
            self.ftp.login("anonymous", "orion-media-center")
        except all_errors as error:
            stop(f"FTP at {host}:{port} does not answer ({error}); do not retry")

    def close(self):
        try:
            self.ftp.quit()
        except all_errors:
            pass

    def names(self, path):
        try:
            self.ftp.cwd(path)
            return [name for name, _ in self.ftp.mlsd() if name not in (".", "..")]
        except all_errors:
            return None
        finally:
            try:
                self.ftp.cwd("/")
            except all_errors:
                pass

    def read(self, path):
        data = io.BytesIO()
        try:
            self.ftp.retrbinary(f"RETR {path}", data.write)
        except all_errors:
            return None
        return data.getvalue()

    def write(self, path, data):
        self.ftp.storbinary(f"STOR {path}", io.BytesIO(data))
        if self.read(path) != data:
            stop(f"{path} did not read back as written")

    def remove(self, path):
        try:
            self.ftp.sendcmd(f"DELE {path}")
        except all_errors:
            pass


def running(console, title):
    return any(name.startswith(title) for name in console.names("/mnt/sandbox") or [])


def launch(host, elf_port, console, title):
    """What ORION does to start a title: the request, then the helper to the ELF loader."""
    elf = ROOT / "dist" / title / "assets/payloads/orion-launcher.elf"
    if not elf.is_file():
        stop(f"{elf} is not built (run make)")
    console.write(f"{STATE}/launch-request.txt", f"{title}\n".encode())
    with socket.create_connection((host, elf_port), timeout=10) as link:
        link.sendall(elf.read_bytes())


def main():
    args = [arg for arg in sys.argv[1:] if arg != "--quit"]
    quit_after = "--quit" in sys.argv[1:]
    if len(args) not in (1, 2):
        raise SystemExit(__doc__)
    results = Path(args[0])
    design = args[1] if len(args) == 2 else "-"
    host = os.environ.get("PS5_HOST") or stop("set PS5_HOST")
    ftp_port = int(os.environ.get("FTP_PORT", "2121"))
    elf_port = int(os.environ.get("ELF_PORT", "9021"))
    check_timeout = int(os.environ.get("CHECK_TIMEOUT", "180"))
    tour_timeout = int(os.environ.get("TOUR_TIMEOUT", "900"))
    title = json.loads((ROOT / "sce_sys/param.json").read_text())["titleId"]
    results.mkdir(parents=True, exist_ok=True)
    dev = f"/mnt/sandbox/{title}_000/download0/hui/dev"

    say(f"Checking the console's services at {host}")
    if not port_open(host, ftp_port):
        stop("FTP does not answer")
    console = Console(host, ftp_port)
    if console.names(f"/data/homebrew/{title}") is None:
        stop(f"{title} is not installed in /data/homebrew (make deploy first)")
    token = str(int(time.time()))
    # Written before a launch, so ORION finds it on its first look.
    console.write(f"{STATE}/dev-request.txt", f"check {design} {token}\n".encode())
    if running(console, title):
        say(f"{title} is running; it takes the request within a second or two")
    else:
        if not port_open(host, elf_port):
            console.remove(f"{STATE}/dev-request.txt")
            stop("the ELF loader does not answer, so ORION cannot be started from here; "
                 "open ORION on the console and run this again")
        say(f"Starting {title} through its launch helper")
        launch(host, elf_port, console, title)
    console.close()

    started = time.time()
    report = None
    while time.time() - started < check_timeout:
        time.sleep(3)
        console = Console(host, ftp_port)
        for path in (f"{STATE}/selftest/report.txt", f"{dev}/selftest.txt"):
            data = console.read(path)
            if data and data.decode("utf-8", "replace").startswith(f"token {token}\n"):
                report = data
                break
        console.close()
        if report:
            break
    if report is None:
        stop(f"no report after {check_timeout} s; look at {STATE}/launcher.txt and "
             f"{STATE}/boot.txt on the console (ORION was left as it is)")
    say(f"Checks finished after {time.time() - started:.0f} s")
    (results / "report.txt").write_bytes(report)

    console = Console(host, ftp_port)
    for name in ("launcher.txt", "boot.txt", "launch-helper.txt", "smb-share-status.txt",
                 "shares.txt", "opened-up.txt"):
        data = console.read(f"{STATE}/{name}")
        if data is not None:
            (results / name).write_bytes(data[-512 * 1024:])
    share = (console.read(f"{STATE}/smb-share.txt") or b"").decode("utf-8", "replace")
    console.close()

    lines = report.decode("utf-8", "replace").splitlines()[1:]
    # The console's share as a PC sees it: the port, from outside.
    if "on\t1" in share.splitlines():
        reached = port_open(host, 445)
        lines.insert(-1, f"{'PASS' if reached else 'FAIL'} pc-reaches-share: port 445 "
                         f"{'answers' if reached else 'does not answer'} from this PC")

    if design != "-":
        say(f"Waiting for the {design} tour")
        toured = False
        while time.time() - started < check_timeout + tour_timeout:
            time.sleep(5)
            console = Console(host, ftp_port)
            toured = console.read(f"{dev}/tour/report.txt") is not None
            if toured:
                (results / "tour").mkdir(exist_ok=True)
                try:
                    console.ftp.cwd(f"{dev}/tour")
                    pictures = sorted(n for n, _ in console.ftp.mlsd() if n.endswith(".bmp"))
                    console.ftp.cwd("/")
                except all_errors:
                    pictures = []
                for name in pictures:
                    data = console.read(f"{dev}/tour/{name}")
                    if data:
                        (results / "tour" / name).write_bytes(data)
                say(f"Downloaded {len(pictures)} pictures; ORION closes itself in a few minutes")
            console.close()
            if toured:
                break
        lines.insert(-1, f"{'PASS' if toured else 'FAIL'} tour {design}: "
                         f"{'pictures in tour/' if toured else 'no tour report in time'}")
        try:
            from PIL import Image
            for bmp in sorted((results / "tour").glob("*.bmp")):
                Image.open(bmp).save(bmp.with_suffix(".png"))
                bmp.unlink()
        except ImportError:
            pass

    console = Console(host, ftp_port)
    console.remove(f"{STATE}/dev-request.txt")
    if quit_after and design == "-":
        console.write(f"{STATE}/dev-request.txt", f"quit - {token}q\n".encode())
        say("Asked ORION to close itself")
    console.close()

    failed = [line for line in lines if line.startswith("FAIL")]
    passed = sum(line.startswith("PASS") for line in lines)
    skipped = sum(line.startswith("SKIP") for line in lines)
    print("\n".join(line for line in lines if line[:4] in ("PASS", "FAIL", "SKIP")))
    (results / "summary.txt").write_text("\n".join(lines) + "\n")
    say(f"{passed} passed, {len(failed)} failed, {skipped} skipped; evidence in {results}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
