"""Request an uncompressed CPU-only Optick capture from a supervised smoke-test renderer."""

# Copyright (c) 2026 DIVIDE-Studio
import argparse
import re
import socket
import struct
import subprocess
import time
from pathlib import Path


def receive_exact(sock, size):
    chunks = []
    while size:
        chunk = sock.recv(min(size, 1024 * 1024))
        if not chunk:
            raise RuntimeError("Optick disconnected before the capture finished")
        chunks.append(chunk)
        size -= len(chunk)
    return b"".join(chunks)


parser = argparse.ArgumentParser()
parser.add_argument("--attempt", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--start-after", type=int, default=20)
parser.add_argument("--frames", type=int, default=8)
parser.add_argument("--expected-gpu", default=None, help="Optional GPU UUID the renderer must report")
args = parser.parse_args()

deadline = time.monotonic() + 240
events_path = args.attempt / "events.log"
failure_path = args.attempt / "failure.txt"
while time.monotonic() < deadline:
    if failure_path.exists():
        raise RuntimeError(f"Smoke attempt failed before profiling: {failure_path}")
    if events_path.exists():
        events = events_path.read_text()
        presents = re.findall(r"^PRESENT count=(\d+)$", events, re.M)
        has_renderer = re.search(r"^SUPERVISOR_WAIT pid=(\d+)$", events, re.M)
        has_gpu = re.search(r"^GPU uuid=([0-9a-fA-F]{32}) ", events, re.M)
        if args.start_after == 0 and has_renderer and has_gpu:
            break
        if args.start_after > 0 and presents and int(presents[-1]) >= args.start_after:
            break
    time.sleep(0.5)
else:
    raise TimeoutError("Timed out waiting for the guarded renderer")

pid_match = re.search(r"^SUPERVISOR_WAIT pid=(\d+)$", events, re.M)
gpu_match = re.search(r"^GPU uuid=([0-9a-fA-F]{32}) ", events, re.M)
if not pid_match or not gpu_match or (args.expected_gpu and gpu_match.group(1).lower() != args.expected_gpu.lower()):
    raise RuntimeError("The supervised renderer PID or GPU UUID does not match")
renderer_pid = int(pid_match.group(1))
while time.monotonic() < deadline:
    if failure_path.exists():
        raise RuntimeError(f"Smoke attempt failed before profiling: {failure_path}")
    listener = subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
         "Get-NetTCPConnection -LocalPort 31318 -ErrorAction SilentlyContinue | "
         "Select-Object -ExpandProperty OwningProcess"],
        check=False, capture_output=True, text=True,
    )
    if listener.returncode != 0 and listener.stderr.strip():
        raise RuntimeError(f"Optick listener lookup failed: {listener.stderr.strip()}")
    listener_pids = {int(line.strip()) for line in listener.stdout.splitlines() if line.strip()}
    if listener_pids:
        if listener_pids != {renderer_pid}:
            raise RuntimeError(f"Optick listener {listener_pids} does not belong to renderer {renderer_pid}")
        break
    time.sleep(0.5)
else:
    raise TimeoutError("Timed out waiting for the supervised renderer's Optick listener")

mode = (1 << 0) | (1 << 1) | (1 << 3)
settings = struct.pack("<HH6IQI", 0, 0, mode, 0xFFFFFFFF, 0, args.frames, 0, 0, 0, 0)
message = struct.pack("<II", 0xB50FB50F, len(settings)) + settings
responses = []
counts = {}

with socket.create_connection(("127.0.0.1", 31318), timeout=10) as client:
    client.settimeout(120)
    client.sendall(message)
    milestone = f"after present {presents[-1]}" if presents else "during startup"
    print(f"CPU-only capture requested from PID {renderer_pid} on GPU {gpu_match.group(1)} "
          f"{milestone} ({args.frames} frames)", flush=True)
    while True:
        header = receive_exact(client, 12)
        version, length, kind, app = struct.unpack("<IIHH", header)
        if version != 26 or app != 0xB50F or length > 256 * 1024 * 1024:
            raise RuntimeError(f"Unexpected Optick response: {version=} {length=} {kind=} {app=}")
        payload = receive_exact(client, length)
        counts[kind] = counts.get(kind, 0) + 1
        if kind == 5:
            status = struct.unpack_from("<I", payload)[0]
            print(f"Handshake status={status}; ETW was not requested", flush=True)
        elif kind == 4:
            continue
        else:
            responses.append(header + payload)
        if kind == 3:
            break

if not counts.get(0) or not counts.get(1):
    raise RuntimeError(f"Capture contains no description board or event frames: {counts}")
args.output.parent.mkdir(parents=True, exist_ok=True)
partial = args.output.with_suffix(".partial")
with partial.open("wb") as file:
    file.write(struct.pack("<IHH", 0xB50FB50F, 0, 0))
    for response in responses:
        file.write(response)
partial.replace(args.output)
print(f"Saved {args.output} ({args.output.stat().st_size} bytes), responses={counts}", flush=True)
