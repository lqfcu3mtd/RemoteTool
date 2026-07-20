#!/usr/bin/env python3
"""End-to-end smoke test for RemoteTool.

Topology (all on loopback):

    python client --data--> remote_tool :10099 (MappingListener)
        --OPEN_SESSION/SESSION_DATA--> agent_windows --tcp--> python echo :19001

The script:
  1. starts an in-process TCP echo server on 127.0.0.1:19001
  2. writes rt/ and ag/ config dirs (mapping enabled at startup, whitelist
     allowing the echo target)
  3. launches remote_tool.exe and agent_windows.exe
  4. waits for the tunnel + mapping to come up
  5. sends random payloads through 127.0.0.1:10099 and verifies the echo
     bytes are identical (sha256), plus a second connection for good measure
  6. kills both exe processes and cleans up (no background processes left)

Run:  python tools/smoke_e2e.py
Exit code 0 = pass, 1 = fail.
"""

import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build-dev", "bin")
WORK = os.path.join(ROOT, ".smoke-e2e")
ECHO_PORT = 19001
MAP_PORT = 10099
CTRL_PORT = 4433


def echo_server(stop):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", ECHO_PORT))
    srv.listen(8)
    srv.settimeout(0.5)

    def handle(conn):
        with conn:
            while not stop.is_set():
                try:
                    data = conn.recv(65536)
                except (ConnectionResetError, OSError):
                    return
                if not data:
                    return
                try:
                    conn.sendall(data)
                except OSError:
                    return

    while not stop.is_set():
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        threading.Thread(target=handle, args=(conn,), daemon=True).start()
    srv.close()


def wait_port(port, timeout_s, proc_watch=()):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        for p in proc_watch:
            if p.poll() is not None:
                raise RuntimeError(f"process {p.pid} exited early (rc={p.returncode})")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.25)
    return False


def roundtrip(payload, tag):
    with socket.create_connection(("127.0.0.1", MAP_PORT), timeout=5) as s:
        s.sendall(payload)
        s.shutdown(socket.SHUT_WR)
        chunks = []
        while True:
            data = s.recv(65536)
            if not data:
                break
            chunks.append(data)
    got = b"".join(chunks)
    if got != payload:
        raise RuntimeError(
            f"{tag}: echo mismatch (sent {len(payload)}B "
            f"sha256={hashlib.sha256(payload).hexdigest()[:16]}, "
            f"got {len(got)}B sha256={hashlib.sha256(got).hexdigest()[:16]})")
    print(f"[smoke] {tag}: {len(payload)} bytes echoed, sha256 "
          f"{hashlib.sha256(got).hexdigest()[:16]} OK")


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    rt_dir = os.path.join(WORK, "rt")
    ag_dir = os.path.join(WORK, "ag")
    os.makedirs(rt_dir)
    os.makedirs(ag_dir)

    with open(os.path.join(rt_dir, "mappings.json"), "w") as f:
        json.dump({
            "schema_version": 1,
            "mappings": [{
                "id": "map-echo",
                "device_id": "AGENT001",
                "name": "echo",
                "local_port": MAP_PORT,
                "target_host": "127.0.0.1",
                "target_port": ECHO_PORT,
                "enabled": True,
                "connect_timeout_ms": 5000,
            }],
        }, f, indent=2)

    with open(os.path.join(ag_dir, "agent.json"), "w") as f:
        json.dump({
            "schema_version": 1,
            "server": {"host": "127.0.0.1", "port": CTRL_PORT},
            "device": {"id": "AGENT001", "pairing_code": "",
                       "device_key_dpapi": ""},
            "target_policy": {
                "allowed_cidrs": ["127.0.0.0/8"],
                "allowed_ports": [ECHO_PORT],
                "allow_ipv6": False,
            },
            "logging": {"level": "info", "max_file_bytes": 2097152,
                        "retained_files": 2},
        }, f, indent=2)

    stop = threading.Event()
    threading.Thread(target=echo_server, args=(stop,), daemon=True).start()

    env = dict(os.environ)
    env["PATH"] = r"D:\tools\mingw64\bin" + os.pathsep + env.get("PATH", "")

    rt = subprocess.Popen([os.path.join(BIN, "remote_tool.exe")],
                          cwd=rt_dir, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors="replace")
    ag = None
    try:
        # Wait for the control listener, then bring the agent up.
        if not wait_port(CTRL_PORT, 10, [rt]):
            raise RuntimeError("remote_tool did not start listening on 4433")
        ag = subprocess.Popen([os.path.join(BIN, "agent_windows.exe")],
                              cwd=ag_dir, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace")
        # Mapping listener becomes usable once the agent is online.
        time.sleep(2.5)
        if not wait_port(MAP_PORT, 10, [rt, ag]):
            raise RuntimeError("mapping listener did not come up on 10099")

        payload1 = os.urandom(256 * 1024)
        roundtrip(payload1, "conn#1 256KiB random")
        payload2 = b"hello remote tunnel\n" * 1000
        roundtrip(payload2, "conn#2 20KiB text")

        print("[smoke] PASS: end-to-end forwarding verified")
        rc = 0
    finally:
        for p in (ag, rt):
            if p and p.poll() is None:
                p.terminate()
        for p in (ag, rt):
            if p:
                try:
                    out, _ = p.communicate(timeout=8)
                    print(f"----- {os.path.basename(p.args[0])} log -----")
                    print(out.strip()[-3000:])
                except subprocess.TimeoutExpired:
                    p.kill()
        stop.set()
        time.sleep(0.6)
    return rc


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:  # noqa: BLE001
        print(f"[smoke] FAIL: {e}")
        sys.exit(1)
