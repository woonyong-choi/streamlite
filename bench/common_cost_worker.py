from __future__ import annotations

import argparse
import ctypes
import importlib
import json
import resource
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
PHASE_FIELDS = (
    "wall_ns",
    "cpu_ns",
    "exclusive_wall_ns",
    "exclusive_cpu_ns",
    "attempts",
    "bytes",
    "errors",
    "short_io",
    "eintr",
)


class _Cost(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in PHASE_FIELDS]


class _Record(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in ("sequence", "pid", "tid", "incomplete", "overflow", "dropped")]
    _fields_ += [("operation", ctypes.c_char * 32), ("phases", _Cost * 12)]


class _Diagnostics:
    def __init__(self, library: Any) -> None:
        self.library = library
        self.is_enabled = hasattr(library, "tb_bench_drain")
        self.result: dict[str, Any] = {
            "records": 0,
            "incomplete": 0,
            "overflow": 0,
            "dropped": 0,
            "phases": [{name: 0 for name in PHASE_FIELDS} for _ in range(12)],
            "sql_count": None,
        }
        if self.is_enabled:
            library.tb_bench_drain.argtypes = [ctypes.POINTER(_Record), ctypes.c_size_t]
            library.tb_bench_drain.restype = ctypes.c_size_t

    def drain(self) -> None:
        if not self.is_enabled:
            return
        records = (_Record * 64)()
        count = self.library.tb_bench_drain(records, 64)
        self.result["records"] += count
        for record in records[:count]:
            for name in ("incomplete", "overflow", "dropped"):
                self.result[name] += getattr(record, name)
            for target, phase in zip(self.result["phases"], record.phases):
                for name in PHASE_FIELDS:
                    target[name] += getattr(phase, name)


def _require(valid: bool, message: str) -> None:
    if not valid:
        raise ValueError(message)


def _rss_peak() -> int:
    value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return int(value if sys.platform == "darwin" else value * 1024)


def _database_class(condition: str) -> Any:
    module = importlib.import_module("totedb.database")
    if condition == "eager":
        source = subprocess.run(
            ["git", "show", "54b43509:python/totedb/database.py"], cwd=ROOT, text=True, capture_output=True, check=True
        ).stdout
        exec(compile(source, "54b43509:python/totedb/database.py", "exec"), module.__dict__)
    return module.Database


def _sql_work(db: Any) -> None:
    db.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY, value INTEGER)")
    with db.begin() as txn:
        txn.execute("INSERT INTO notes VALUES (1,0)")
        txn.execute("UPDATE notes SET value=value+1 WHERE id=1")
        txn.commit()
    _require(db.execute("SELECT id,value FROM notes").rows == ((1, 1),), "SQL state mismatch")


def _queue_work(db: Any, count: int, payload: int, diagnostics: _Diagnostics) -> dict[str, Any]:
    from totedb import ItemState, Queue, StaleError  # noqa: PLC0415
    from totedb.queue import ClaimToken  # noqa: PLC0415

    queue = Queue(db, "jobs", clock=lambda: 1000)
    body = b"x" * payload
    completed = 0
    latencies = []
    active_ns_guard = 0
    start = time.perf_counter_ns()
    for _ in range(count):
        begin = time.perf_counter_ns()
        item = queue.put(body)
        claim = queue.get(1000)
        _require(claim is not None, "missing eligible item")
        _require((claim.id, claim.payload, claim.attempts) == (item.id, body, 1), "claim result mismatch")
        queue.ack(claim)
        normal_ns = time.perf_counter_ns() - begin
        guard_start = time.perf_counter_ns()
        if completed == 0:
            queue.ack(claim)
            try:
                queue.ack(ClaimToken(claim.id, claim.fence + 1))
            except StaleError:
                pass
            else:
                raise ValueError("stale ACK accepted")
            page = queue.list_items(limit=1, with_payload=True)
            _require(len(page) == 1 and page[0].state == ItemState.ACKED, "terminal state mismatch")
        guard_ns = time.perf_counter_ns() - guard_start
        purge_start = time.perf_counter_ns()
        _require(queue.purge(limit=1) == 1, "purge mismatch")
        latencies.append(normal_ns + time.perf_counter_ns() - purge_start)
        active_ns_guard += guard_ns
        completed += 1
        diagnostics.drain()
    active_ns = time.perf_counter_ns() - start
    _require(queue.get(1000) is None, "empty queue returned item")
    _require(queue.stats().ready == 0 and queue.list_items() == [], "queue cleanup incomplete")
    return {"completed": completed, "active_ns": active_ns, "guard_ns": active_ns_guard, "latencies_ns": latencies}


def _run(args: argparse.Namespace) -> dict[str, Any]:
    database = _database_class(args.condition)
    diagnostics = _Diagnostics(importlib.import_module("totedb.database")._lib)
    baseline_rss = _rss_peak()
    times = {name: 0 for name in ("open_ns", "execute_ns", "close_ns", "reopen_verify_ns")}
    latencies = []
    completed = 0
    normal_queue_ns, guard_queue_ns = 0, 0
    start, cpu = time.perf_counter_ns(), time.process_time_ns()
    instances = args.instances if args.scenario != "conditional" else 1
    for index in range(instances):
        instance_start = time.perf_counter_ns()
        path = Path(f"{args.prefix}-{index}.db")
        _require(not path.exists(), "worker destination exists")
        begin = time.perf_counter_ns()
        db = database(path)
        times["open_ns"] += time.perf_counter_ns() - begin
        begin = time.perf_counter_ns()
        _sql_work(db)
        diagnostics.drain()
        if args.scenario != "sql":
            count = args.operations if args.scenario == "conditional" else 1
            queue_sample = _queue_work(db, count, args.payload, diagnostics)
            completed += queue_sample["completed"]
            normal_queue_ns += sum(queue_sample["latencies_ns"])
            guard_queue_ns += queue_sample["guard_ns"]
            if args.scenario == "conditional":
                latencies.extend(queue_sample["latencies_ns"])
        else:
            completed += 1
        times["execute_ns"] += time.perf_counter_ns() - begin
        begin = time.perf_counter_ns()
        db.close()
        times["close_ns"] += time.perf_counter_ns() - begin
        begin = time.perf_counter_ns()
        with database(path, create=False) as reopened:
            _require(reopened.execute("SELECT id,value FROM notes").rows == ((1, 1),), "reopen mismatch")
            if args.scenario != "sql":
                from totedb import Queue  # noqa: PLC0415

                _require(Queue(reopened, "jobs", install=False).list_items() == [], "reopened queue not empty")
        times["reopen_verify_ns"] += time.perf_counter_ns() - begin
        if args.scenario != "conditional":
            latencies.append(time.perf_counter_ns() - instance_start)
        diagnostics.drain()
    return {
        "schema": 1,
        "completed": completed,
        "instances": instances,
        "condition": args.condition,
        "scenario": args.scenario,
        "wall_ns": time.perf_counter_ns() - start,
        "cpu_ns": time.process_time_ns() - cpu,
        "baseline_rss_bytes": baseline_rss,
        "peak_rss_bytes": _rss_peak(),
        "latencies_ns": latencies,
        "phases": times,
        "normal_queue_ns": normal_queue_ns,
        "guard_queue_ns": guard_queue_ns,
        "instrumented": int(diagnostics.is_enabled),
        "diagnostics": diagnostics.result if diagnostics.is_enabled else None,
        "oracle": "SQL exact rows; queue ID/payload/attempt/fence/terminal/purge/empty/reopen",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--condition", choices=("eager", "lazy", "baseline", "candidate"), required=True)
    parser.add_argument("--scenario", choices=("sql", "queue", "conditional"), required=True)
    parser.add_argument("--instances", type=int, required=True)
    parser.add_argument("--operations", type=int, required=True)
    parser.add_argument("--payload", type=int, required=True)
    args = parser.parse_args()
    _require(1 <= args.instances <= 32 and 1 <= args.operations <= 1024, "invalid worker size")
    print(json.dumps(_run(args), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
