"""Record comparable dependency/configure/build timings without deleting caches."""

import argparse
import json
import platform
import shlex
import shutil
import subprocess
import time
from pathlib import Path


def command_arguments(value):
    try:
        command = shlex.split(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    if not command:
        raise argparse.ArgumentTypeError("Command must contain an executable")
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=("cold", "warm", "incremental"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--install", type=command_arguments, required=True, help="Shell-quoted vcpkg arguments (no shell execution)")
    parser.add_argument("--configure", type=command_arguments, required=True, help="Shell-quoted CMake configure arguments")
    parser.add_argument("--build", type=command_arguments, required=True, help="Shell-quoted CMake build arguments")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    result = {
        "scenario": args.scenario,
        "platform": platform.platform(),
        "working_directory": str(Path.cwd()),
        "free_disk_bytes": shutil.disk_usage(Path.cwd()).free,
        "stages": [],
    }
    for stage in ("install", "configure", "build"):
        command = getattr(args, stage)
        log = args.output.with_name(f"{args.output.stem}-{stage}.log")
        start = time.perf_counter()
        with log.open("w", encoding="utf-8") as stream:
            try:
                completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=False)
                exit_code = completed.returncode
            except OSError as error:
                stream.write(f"{error}\n")
                exit_code = 127
        result["stages"].append({
            "stage": stage,
            "command": command,
            "seconds": time.perf_counter() - start,
            "exit_code": exit_code,
            "log": str(log),
        })
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"{stage}: {result['stages'][-1]['seconds']:.2f}s, exit {exit_code}, log {log}")
        if exit_code != 0:
            return exit_code
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
