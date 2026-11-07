"""Summarise an uncompressed CPU Optick capture (see optick_capture.py).

Reports frame-time statistics, per-thread-group inclusive/self/call tables and
per-RenderStage record/submit tables built from the "Record stage: <STAGE>" and
"Submit: <buffer>" dynamic scopes. Times are medians over the captured frames.
"""

# Copyright (c) 2026 DIVIDE-Studio
import argparse
import bisect
import collections
import json
import mmap
import re
import statistics
import struct
from pathlib import Path

RECORD_PREFIX = "Record stage: "
SUBMIT_PREFIX = "Submit: "
# Pool idle/dequeue scopes: workers spend most of their life here, which hides real work.
IDLE_SCOPES = ("TaskPool::dequeInternal", "TaskPool::deque(", "TaskPool::executeOneTask")


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, fmt):
        size = struct.calcsize("<" + fmt)
        values = struct.unpack_from("<" + fmt, self.data, self.pos)
        self.pos += size
        return values[0] if len(values) == 1 else values

    def text(self):
        size = self.take("I")
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        return value.decode("utf-8", errors="replace")


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = min(len(ordered) - 1, max(0, round(fraction * (len(ordered) - 1))))
    return ordered[index]


def thread_group(name):
    # Collapse numbered pool workers ("Worker_3", "TaskPool 12") into one group.
    return re.sub(r"[\s_#-]*\d+$", "", name).strip() or name


def load_capture(path):
    file = path.open("rb")
    data = mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ)
    magic, version, flags = struct.unpack_from("<IHH", data)
    if (magic, version, flags) != (0xB50FB50F, 0, 0):
        raise RuntimeError("Expected an uncompressed Optick capture")

    responses = []
    offset = 8
    while offset < len(data):
        response_version, size, kind, app = struct.unpack_from("<IIHH", data, offset)
        if response_version != 26 or app != 0xB50F:
            raise RuntimeError(f"Unexpected response at offset {offset}")
        offset += 12
        responses.append((kind, offset, size))
        offset += size
    if offset != len(data):
        raise RuntimeError("Truncated capture")
    return data, responses


def parse_board(data, responses):
    board_offset, board_size = next((start, size) for kind, start, size in responses if kind == 0)
    board = Reader(data[board_offset:board_offset + board_size])
    board_id = board.take("i")
    frequency = board.take("q")
    origin = board.take("q")
    precision = board.take("i")
    if origin != 0 or precision != 0:
        raise RuntimeError("Compressed timestamps are not supported")
    board.take("qq")
    threads = []
    for _ in range(board.take("I")):
        thread_id, _process_id = board.take("QI")
        name = board.text()
        board.take("iii")
        threads.append((thread_id, name))
    for _ in range(board.take("I")):
        board.take("Q")
    main_index = board.take("i")
    descriptions = []
    for _ in range(board.take("I")):
        name, file_name = board.text(), board.text()
        line = board.take("IIIfB")[0]
        descriptions.append((name, file_name, line))
    return board_id, frequency, threads, main_index, descriptions


def parse_frames(data, responses, board_id):
    frames_offset, frames_size = next((start, size) for kind, start, size in responses if kind == 259)
    reader = Reader(data[frames_offset:frames_offset + frames_size])
    if reader.take("i") != board_id:
        raise RuntimeError("Mismatched frame board")
    frame_lists = []
    for _ in range(reader.take("I")):
        frame_lists.append([reader.take("qqiQ") for _ in range(reader.take("I"))])
    cpu_frames = [frame for frame in frame_lists[0] if frame[1] > frame[0] >= 0]
    if not cpu_frames:
        raise RuntimeError("No complete CPU frames")
    return cpu_frames


def accumulate(data, responses, board_id, descriptions, cpu_frames, callers=None):
    """Returns {thread_index: {description_index: [[inclusive, children, calls] per frame]}}.

    If callers is a dict, it is filled with {(child_index, parent_index): [inclusive, calls]} totals over all frames.
    """
    frame_starts = [frame[0] for frame in cpu_frames]
    per_thread = collections.defaultdict(
        lambda: collections.defaultdict(lambda: [[0, 0, 0] for _ in cpu_frames]))
    stacks = collections.defaultdict(lambda: [[] for _ in cpu_frames])
    for kind, start_offset, size in responses:
        if kind != 1:
            continue
        event_board, thread_index = struct.unpack_from("<ii", data, start_offset)
        if event_board != board_id:
            raise RuntimeError("Mismatched event board")
        categories = struct.unpack_from("<I", data, start_offset + 32)[0]
        count_offset = start_offset + 36 + categories * 20
        count = struct.unpack_from("<I", data, count_offset)[0]
        event_offset = count_offset + 4
        if event_offset + count * 20 != start_offset + size:
            raise RuntimeError("Unexpected event payload size")
        totals_for_thread = per_thread[thread_index]
        thread_stacks = stacks[thread_index]
        for start, finish, index in struct.iter_unpack("<qqi", memoryview(data)[event_offset:event_offset + count * 20]):
            frame_index = bisect.bisect_right(frame_starts, start) - 1
            if frame_index < 0 or finish > cpu_frames[frame_index][1] or finish < start or not 0 <= index < len(descriptions):
                continue
            duration = finish - start
            totals = totals_for_thread[index][frame_index]
            totals[0] += duration
            totals[2] += 1
            stack = thread_stacks[frame_index]
            while stack and (start >= stack[-1][0] or finish > stack[-1][0]):
                stack.pop()
            if stack:
                totals_for_thread[stack[-1][1]][frame_index][1] += duration
            if callers is not None:
                entry = callers.setdefault((index, stack[-1][1] if stack else -1), [0, 0])
                entry[0] += duration
                entry[1] += 1
            stack.append((finish, index))
    return per_thread


def merge_frames(target, source):
    for index, frames in source.items():
        merged = target[index]
        for frame_index, values in enumerate(frames):
            for slot in range(3):
                merged[frame_index][slot] += values[slot]


def rows_for(table, descriptions, scale, top, name_filter=None):
    rows = []
    for index, frames in table.items():
        name, file_name, line = descriptions[index]
        if name_filter and not name_filter(name):
            continue
        rows.append({
            "name": name,
            "location": f"{Path(file_name).name}:{line}" if file_name else "",
            "inclusive_ms": statistics.median(v[0] * scale for v in frames),
            "self_ms": statistics.median(max(0, v[0] - v[1]) * scale for v in frames),
            "calls": sum(v[2] for v in frames) / len(frames),
        })
    rows.sort(key=lambda row: row["self_ms"], reverse=True)
    return rows[:top] if top else rows


def print_rows(title, rows, sort_inclusive=False):
    print(f"\n## {title}")
    if sort_inclusive:
        rows = sorted(rows, key=lambda row: row["inclusive_ms"], reverse=True)
    print(f"{'self ms':>10} {'incl ms':>10} {'calls':>8}  scope")
    for row in rows:
        print(f"{row['self_ms']:10.3f} {row['inclusive_ms']:10.3f} {row['calls']:8.1f}  {row['name']} [{row['location']}]")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--top", type=int, default=30, help="Rows per thread-group table (0 = all)")
    parser.add_argument("--json", type=Path, help="Also write the full summary as JSON")
    parser.add_argument("--include-idle", action="store_true", help="Keep task-pool idle/dequeue scopes in thread tables")
    parser.add_argument("--callers", action="append", default=[], metavar="SUBSTRING",
                        help="Print the immediate parent scopes of scopes whose name contains SUBSTRING (repeatable)")
    args = parser.parse_args()
    not_idle = None if args.include_idle else (lambda name: not any(idle in name for idle in IDLE_SCOPES))

    data, responses = load_capture(args.capture)
    board_id, frequency, threads, main_index, descriptions = parse_board(data, responses)
    cpu_frames = parse_frames(data, responses, board_id)
    if main_index < 0:
        main_id = collections.Counter(frame[3] for frame in cpu_frames).most_common(1)[0][0]
        main_index = next(i for i, thread in enumerate(threads) if thread[0] == main_id)
    scale = 1000.0 / frequency

    frame_ms = [(end - begin) * scale for begin, end, _, _ in cpu_frames]
    summary = {
        "capture": str(args.capture),
        "frames": len(frame_ms),
        "frame_ms": {
            "median": statistics.median(frame_ms),
            "mean": statistics.fmean(frame_ms),
            "p95": percentile(frame_ms, 0.95),
            "min": min(frame_ms),
            "max": max(frame_ms),
            "all": frame_ms,
        },
        "groups": {},
        "stages": {},
    }
    print(f"capture={args.capture} frames={len(frame_ms)} main thread={threads[main_index][1]}")
    fm = summary["frame_ms"]
    print(f"CPU frame ms: median={fm['median']:.2f} mean={fm['mean']:.2f} p95={fm['p95']:.2f} "
          f"min={fm['min']:.2f} max={fm['max']:.2f}")

    callers = {} if args.callers else None
    per_thread = accumulate(data, responses, board_id, descriptions, cpu_frames, callers)

    groups = collections.OrderedDict()
    groups["Main"] = [main_index]
    for thread_index in sorted(per_thread):
        if thread_index == main_index or thread_index >= len(threads):
            continue
        groups.setdefault(thread_group(threads[thread_index][1]), []).append(thread_index)

    all_threads = collections.defaultdict(lambda: [[0, 0, 0] for _ in cpu_frames])
    for group_name, members in groups.items():
        merged = collections.defaultdict(lambda: [[0, 0, 0] for _ in cpu_frames])
        for thread_index in members:
            merge_frames(merged, per_thread.get(thread_index, {}))
            merge_frames(all_threads, per_thread.get(thread_index, {}))
        if not merged:
            continue
        rows = rows_for(merged, descriptions, scale, args.top, not_idle)
        summary["groups"][group_name] = {"threads": len(members), "scopes": rows}
        print_rows(f"{group_name} ({len(members)} thread(s)), top by self time", rows)

    record = rows_for(all_threads, descriptions, scale, 0, lambda name: name.startswith(RECORD_PREFIX))
    submit = rows_for(all_threads, descriptions, scale, 0, lambda name: name.startswith(SUBMIT_PREFIX))
    summary["stages"] = {"record": record, "submit": submit}
    if record:
        print_rows("Per-stage record (all threads, inclusive)", record, sort_inclusive=True)
    else:
        print("\nNo 'Record stage:' scopes found; capture predates the per-stage scopes.")
    if submit:
        print_rows("Per-command-buffer submit (main thread, inclusive)", submit, sort_inclusive=True)

    if callers is not None:
        frame_count = len(cpu_frames)
        caller_rows = summary.setdefault("callers", {})
        for pattern in args.callers:
            rows = []
            for (child, parent), (inclusive, calls) in callers.items():
                if pattern not in descriptions[child][0]:
                    continue
                parent_name = descriptions[parent][0] if parent >= 0 else "<root>"
                parent_file = descriptions[parent][1] if parent >= 0 else ""
                parent_line = descriptions[parent][2] if parent >= 0 else 0
                rows.append({
                    "name": descriptions[child][0],
                    "caller": parent_name,
                    "caller_location": f"{Path(parent_file).name}:{parent_line}" if parent_file else "",
                    "inclusive_ms_per_frame": inclusive * scale / frame_count,
                    "calls_per_frame": calls / frame_count,
                })
            rows.sort(key=lambda row: row["inclusive_ms_per_frame"], reverse=True)
            caller_rows[pattern] = rows
            print(f"\n## Callers of '{pattern}' (all threads, mean per frame)")
            print(f"{'incl ms':>10} {'calls':>8}  scope <- caller")
            for row in rows[:args.top] if args.top else rows:
                print(f"{row['inclusive_ms_per_frame']:10.3f} {row['calls_per_frame']:8.1f}  "
                      f"{row['name']} <- {row['caller']} [{row['caller_location']}]")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(summary, indent=2) + "\n")
        print(f"\nWrote {args.json}")


if __name__ == "__main__":
    main()
