"""Create synthetic fixtures or emit one fresh-process pending-query observation as JSON."""

from __future__ import annotations

__all__ = ["CountingSerializer", "create_fixture", "observe", "expected_digest"]

import argparse
import ctypes
import gc
import hashlib
import importlib
import json
import logging
import os
import resource
import subprocess
import sys
import time
import tracemalloc
import weakref
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)


class CountingSerializer:
    def __init__(self, *, is_diagnostic: bool = False, fail_at: int = 0) -> None:
        self.loads_count = 0
        self.loaded_bytes = 0
        self.is_diagnostic = is_diagnostic
        self.fail_at = fail_at

    def dumps(self, value: Any) -> bytes:
        return json.dumps(value).encode("utf-8")

    def loads(self, data: bytes) -> Any:
        if self.is_diagnostic:
            self.loads_count += 1
            self.loaded_bytes += len(data)
            if self.fail_at == self.loads_count:
                raise MemoryError("injected serializer allocation failure")
        return json.loads(data.decode("utf-8"))


class _NativeCopies:
    def __init__(self) -> None:
        self.calls = 0
        self.returned_rows = 0
        self.payload_bytes = 0
        self.payload_peak = 0
        self.freed_rows = 0
        self.python_copy_bytes = 0
        self.info_peak = 0
        self.infos: weakref.WeakSet[Any] = weakref.WeakSet()

    def install(self) -> None:
        module: Any = importlib.import_module("streamlite.queue")
        original_list = module._lib.streamlite_queue_list
        original_free = module._lib.streamlite_queue_list_free
        original_info = module._to_info

        def list_items(*args: Any) -> Any:
            status = original_list(*args)
            self.calls += 1
            count = ctypes.cast(args[-1], ctypes.POINTER(ctypes.c_size_t)).contents.value
            self.returned_rows += count
            size = sum(args[-2][i].payload_size for i in range(count))
            self.payload_bytes += size
            self.payload_peak = max(self.payload_peak, size)
            return status

        def free_items(items: Any, count: int) -> Any:
            self.freed_rows += count
            return original_free(items, count)

        def to_info(raw: Any, *, with_payload: bool) -> Any:
            info = original_info(raw, with_payload=with_payload)
            if with_payload:
                self.python_copy_bytes += raw.payload_size
            self.infos.add(info)
            self.info_peak = max(self.info_peak, len(self.infos))
            return info

        module._lib.streamlite_queue_list = list_items
        module._lib.streamlite_queue_list_free = free_items
        module._to_info = to_info

    def snapshot(self) -> dict[str, int]:
        return {
            **{key: value for key, value in vars(self).items() if isinstance(value, int)},
            "live_infos": len(self.infos),
        }


def create_fixture(settings: dict[str, Any]) -> dict[str, Any]:
    path = Path(settings["path"])
    if path.exists():
        raise FileExistsError(path)
    path.mkdir()
    serializer = CountingSerializer()
    if settings["condition"] == "vendor":
        queue = _open_vendor(path, serializer)
        for item_id in range(1, settings["backlog"] + 1):
            queue.put(_value(item_id, settings["body"]))
        queue._getter.execute(f"UPDATE {queue._table_name} SET timestamp=1.0")
        queue._getter.commit()
        queue.close()
    else:
        module = importlib.import_module("streamlite")
        db = module.Database(path / "data.streamlite")
        queue = module.Queue(db, "ack_queue", clock=lambda: 1000)
        for start in range(1, settings["backlog"] + 1, settings["batch"]):
            with db.begin() as txn:
                for item_id in range(start, min(start + settings["batch"], settings["backlog"] + 1)):
                    queue.put(serializer.dumps(_value(item_id, settings["body"])), txn=txn)
                txn.commit()
        queue.close()
        db.close()
    return {"backlog": settings["backlog"], "body": settings["body"], "created": True}


def expected_digest(settings: dict[str, Any]) -> str:
    count = settings["backlog"]
    if settings["query"] in ("metadata", "payload"):
        count = min(count, settings["page"])
    digest = hashlib.sha256()
    for item_id in range(1, count + 1):
        row: dict[str, Any] = {"id": item_id, "timestamp": 1.0, "status": 0}
        if settings["query"] != "metadata":
            row["data"] = _value(item_id, settings["body"])
        _digest_row(digest, row)
    return digest.hexdigest()


def observe(settings: dict[str, Any]) -> dict[str, Any]:
    if settings["diagnostic"]:
        tracemalloc.start()
    serializer = CountingSerializer(is_diagnostic=settings["diagnostic"], fail_at=settings.get("fail_at", 0))
    queue, database = _open_query(settings, serializer)
    sqlite_settings = (
        {
            name: queue._getter.execute(f"PRAGMA {name}").fetchone()[0]
            for name in (
                "journal_mode",
                "synchronous",
                "cache_size",
                "page_size",
                "wal_autocheckpoint",
            )
        }
        if settings["condition"] == "vendor"
        else None
    )
    copies = _NativeCopies()
    if settings["diagnostic"] and settings["condition"] != "vendor":
        copies.install()
    gc.collect()
    baseline = _memory()
    if settings["diagnostic"]:
        tracemalloc.reset_peak()
    start_wall, start_cpu = time.perf_counter_ns(), time.process_time_ns()
    result = _consume(queue, settings, serializer)
    wall_ns, cpu_ns = (
        time.perf_counter_ns() - start_wall,
        time.process_time_ns() - start_cpu,
    )
    peak = tracemalloc.get_traced_memory()[1] if tracemalloc.is_tracing() else None
    held = _memory()
    digest, rows = _audit(result, settings)
    del result
    gc.collect()
    released = _memory()
    queue.close()
    if database is not None:
        database.close()
    gc.collect()
    closed = _memory()
    native = copies.snapshot() if settings["diagnostic"] and settings["condition"] != "vendor" else None
    return {
        "rows": rows,
        "digest": digest,
        "wall_ns": wall_ns,
        "cpu_ns": cpu_ns,
        "baseline": baseline,
        "held": held,
        "released": released,
        "closed": closed,
        "python_peak_bytes": peak,
        "python_query_peak_increment_bytes": peak - baseline["python_bytes"] if peak is not None else None,
        "copies": native,
        "loads": serializer.loads_count if settings["diagnostic"] else None,
        "loaded_bytes": serializer.loaded_bytes if settings["diagnostic"] else None,
        "sqlite_version": importlib.import_module("sqlite3").sqlite_version,
        "sqlite_settings": sqlite_settings,
    }


def _open_vendor(path: Path, serializer: CountingSerializer) -> Any:
    module = importlib.import_module("persistqueue")
    return module.SQLiteAckQueue(str(path), auto_resume=False, multithreading=False, serializer=serializer)


def _open_query(settings: dict[str, Any], serializer: CountingSerializer) -> tuple[Any, Any]:
    path = Path(settings["path"])
    if settings["condition"] == "vendor":
        return _open_vendor(path, serializer), None
    if settings["condition"] == "compat":
        module = importlib.import_module("streamlite.persistqueue_compat")
        return module.SQLiteAckQueue(path, auto_resume=False, serializer=serializer), None
    module = importlib.import_module("streamlite")
    db = module.Database(path / "data.streamlite")
    return module.Queue(db, "ack_queue", install=False), db


def _value(item_id: int, body: int) -> dict[str, Any]:
    return {"id": item_id, "body": "x" * body}


def _row(info: Any, serializer: CountingSerializer, *, with_payload: bool) -> dict[str, Any]:
    state = {0: 0, 1: 2, 2: 5, 3: 9}[int(info.state)]
    if state == 0 and info.attempts > 0:
        state = 1
    row = {"id": info.id, "timestamp": info.created_at_ms / 1000, "status": state}
    if with_payload:
        row["data"] = serializer.loads(info.payload or b"")
    return row


def _consume(queue: Any, settings: dict[str, Any], serializer: CountingSerializer) -> Any:
    query, page = settings["query"], settings["page"]
    if settings["condition"] != "native":
        source = queue.queue()
        if query in ("metadata", "payload"):
            source = source[:page]
        for row in source:
            row["status"] = int(row["status"])
        if query == "sequential":
            return _digest_rows(source)
        if query == "metadata":
            return [{key: row[key] for key in ("id", "status", "timestamp")} for row in source]
        return source
    if query in ("metadata", "payload"):
        return [
            _row(info, serializer, with_payload=query == "payload")
            for info in queue.list_items(limit=page, with_payload=query == "payload")
        ]
    iterator = (_row(info, serializer, with_payload=True) for info in queue.iter_items(page, with_payload=True))
    if query == "sequential":
        return _digest_rows(iterator)
    return list(iterator)


def _digest_row(digest: Any, row: dict[str, Any]) -> None:
    digest.update(json.dumps(row, sort_keys=True, separators=(",", ":")).encode("utf-8"))
    digest.update(b"\n")


def _digest_rows(rows: Any) -> tuple[str, int]:
    digest, count = hashlib.sha256(), 0
    for row in rows:
        _digest_row(digest, row)
        count += 1
    return digest.hexdigest(), count


def _audit(result: Any, settings: dict[str, Any]) -> tuple[str, int]:
    if settings["query"] == "sequential":
        return result
    return _digest_rows(result)


def _memory() -> dict[str, Any]:
    python_bytes = tracemalloc.get_traced_memory()[0] if tracemalloc.is_tracing() else None
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    current_rss = int(subprocess.check_output(["ps", "-o", "rss=", "-p", str(os.getpid())], text=True).strip()) * 1024
    return {
        "rss_bytes": current_rss,
        "rss_highwater_bytes": int(peak if sys.platform == "darwin" else peak * 1024),
        "python_bytes": python_bytes,
        "gc_objects": len(gc.get_objects()) if tracemalloc.is_tracing() else None,
    }


def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("fixture", "query"))
    parser.add_argument("settings", type=json.loads)
    args = parser.parse_args()
    try:
        result = create_fixture(args.settings) if args.mode == "fixture" else observe(args.settings)
        print(json.dumps(result, sort_keys=True))
        return 0
    except Exception:
        logger.exception("pending worker failed")
        return 1


if __name__ == "__main__":
    logging.basicConfig(level=logging.WARNING)
    raise SystemExit(_main())
