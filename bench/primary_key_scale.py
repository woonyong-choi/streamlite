"""Build, register, collect, verify and summarize the retained #158 scale requirement."""

from __future__ import annotations

__all__ = ["build", "register", "collect", "verify", "analyze"]

import argparse
import contextlib
import hashlib
import json
import logging
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time
from typing import Any

import sqlite_workload as original

ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "bench/primary_key_scale_plan.json"
SOURCES = [
    "bench/primary_key_scale.py",
    "bench/primary_key_scale_plan.json",
    "bench/sqlite_workload.py",
    "bench/sqlite_workload.json",
    "bench/sqlite_driver.c",
    "tests/test_primary_key_scale.py",
]
IDENTITY_KEYS = ("driver", "library", "python", "library_fingerprint")
logger = logging.getLogger(__name__)


class _Worker(original.Worker):
    def __init__(self, engine: str, paths: tuple[Path, Path, Path]) -> None:
        driver, database, stderr = paths
        self.engine, self.path = engine, database
        self.pending = bytearray()
        self._stderr = stderr.open("xb")
        try:
            self.process = subprocess.Popen(
                [str(driver), engine, str(database)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=self._stderr,
            )
        except OSError:
            self._stderr.close()
            raise

    def close(self) -> None:
        try:
            if self.process.stdin is not None:
                with contextlib.suppress(BrokenPipeError):
                    self.process.stdin.close()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            if self.process.returncode:
                raise RuntimeError(f"{self.engine} worker exit {self.process.returncode}")
        finally:
            if self.process.stdout is not None:
                self.process.stdout.close()
            self._stderr.close()


def build() -> dict[str, Any]:
    plan = _settings(control=False)
    driver, library = ROOT / plan["driver"], ROOT / plan["library"]
    if driver.exists():
        raise FileExistsError(f"driver exists; preserve it before rebuilding: {driver}")
    if _digest(library) != plan["library_sha256"]:
        raise ValueError("runtime library differs from pinned comparison build")
    command = [
        "clang",
        *plan["build_flags"],
        "-Iinclude",
        "bench/sqlite_driver.c",
        str(library),
        "-lsqlite3",
        f"-Wl,-rpath,{library.parent}",
        "-o",
        str(driver),
    ]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
    evidence = {
        "command": command,
        "exit_code": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "source_sha256": _digest(ROOT / "bench/sqlite_driver.c"),
        "library_sha256": _digest(library),
    }
    if result.returncode == 0:
        evidence["driver_sha256"] = _digest(driver)
    _write(Path(f"{driver}.build.json"), evidence)
    if result.returncode or result.stderr:
        raise ValueError("driver build failed; build evidence retained")
    return {**evidence, "driver_sha256": _digest(driver)}


def register(prefix: Path, *, control: bool, plan_commit: str) -> dict[str, Any]:
    prefix = prefix.resolve()
    if list(prefix.parent.glob(prefix.name + "*")):
        raise FileExistsError(f"prefix exists: {prefix}")
    plan = _settings(control=control)
    hashes = {p: _digest(ROOT / p) for p in SOURCES}
    commit = _git("rev-parse", plan_commit + "^{commit}").strip()
    if not control:
        if _git("status", "--porcelain"):
            raise ValueError("formal registration requires clean sources")
        _check_sources(commit, hashes)
    runtime = _runtime(plan["runtime_commit"])
    identities = {plan[k]: _digest(ROOT / plan[k]) for k in IDENTITY_KEYS}
    if identities[plan["library"]] != plan["library_sha256"]:
        raise ValueError("library identity mismatch")
    if identities[plan["library_fingerprint"]] != plan["library_fingerprint_sha256"]:
        raise ValueError("library fingerprint identity mismatch")
    built = _read(Path(f"{ROOT / plan['driver']}.build.json"))
    if built["source_sha256"] != hashes["bench/sqlite_driver.c"] or built["exit_code"]:
        raise ValueError("driver build source mismatch")
    if built["driver_sha256"] != identities[plan["driver"]] or built["library_sha256"] != identities[plan["library"]]:
        raise ValueError("driver build binary identity mismatch")
    result = {
        "schema": 1,
        "head": _git("rev-parse", "HEAD").strip(),
        "plan_commit": commit,
        "control": control,
        "plan": plan,
        "sources": hashes,
        "runtime": runtime,
        "identities": identities,
        "schedule": _schedule(plan),
        "environment": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "memory_bytes": os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES"),
            "compiler": subprocess.check_output(["clang", "--version"], text=True),
            "python": platform.python_version(),
            "sqlite": subprocess.check_output([str(ROOT / plan["driver"]), "--version"], text=True).strip(),
        },
        "build_evidence": built,
    }
    _write(Path(f"{prefix}.registration.json"), result)
    return result


def collect(registration: dict[str, Any], prefix: Path) -> None:
    prefix = prefix.resolve()
    _validate_registration(registration)
    if not registration["control"] and _git("status", "--porcelain"):
        raise ValueError("formal collection requires clean sources")
    if set(prefix.parent.glob(prefix.name + "*")) != {Path(f"{prefix}.registration.json")}:
        raise FileExistsError("collection prefix overlaps prior artifacts")
    if _read(Path(f"{prefix}.registration.json")) != registration:
        raise ValueError("registration file differs from supplied registration")
    plan = registration["plan"]
    start = time.monotonic()
    deadline = start + plan["campaign_seconds"]
    manifest: dict[str, Any] = {"completed": 0, "attempted": 0, "failures": [], "artifacts": {}}
    for index, case in enumerate(registration["schedule"]):
        manifest["attempted"] += 1
        try:
            _remaining(deadline)
            _check_storage(prefix, plan)
            record = _run_case(case, (prefix, index, plan, deadline))
            if record.get("failure"):
                raise ValueError(record["failure"])
            manifest["completed"] += 1
            print(
                json.dumps({"case": index, "completed_rows": record["loaded"], "load_status": record["load_status"]}),
                flush=True,
            )
            _remaining(deadline)
        except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
            manifest["failures"].append({"case": index, "error": repr(error)})
            break
    manifest["unexecuted"] = len(registration["schedule"]) - manifest["attempted"]
    try:
        _validate_registration(registration)
        _check_storage(prefix, plan)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        manifest["failures"].append({"case": None, "error": repr(error)})
    for file in _files(prefix):
        manifest["artifacts"][str(file)] = _digest(file)
    manifest["elapsed_seconds"] = time.monotonic() - start
    if manifest["elapsed_seconds"] >= plan["campaign_seconds"]:
        manifest["failures"].append({"case": None, "error": "campaign deadline exceeded"})
    _write(Path(f"{prefix}.manifest.json"), manifest)
    if time.monotonic() >= deadline:
        manifest["failures"].append({"case": None, "error": "manifest persistence deadline exceeded"})
        manifest["elapsed_seconds"] = time.monotonic() - start
        with Path(f"{prefix}.manifest.json").open("w") as output:
            json.dump(manifest, output, sort_keys=True, indent=2, allow_nan=False)
            output.flush()
            os.fsync(output.fileno())
    if manifest["failures"]:
        raise ValueError("campaign failed; partial data retained")


def verify(registration: dict[str, Any], prefix: Path) -> list[dict[str, Any]]:
    prefix = prefix.resolve()
    _validate_registration(registration)
    manifest = _read(Path(f"{prefix}.manifest.json"))
    required = _required(prefix, registration["schedule"])
    hashes = manifest["artifacts"]
    if required - hashes.keys():
        raise ValueError("missing required artifact hashes")
    allowed = required | {name + "-lock" for name in required if name.endswith(".mini.db")}
    present = {p for p in allowed - required if Path(p).exists() or Path(p).is_symlink()}
    if present - hashes.keys():
        raise ValueError("missing required artifact hashes")
    for path, digest in hashes.items():
        if path not in allowed or path != str(Path(path).resolve()):
            raise ValueError("invalid artifact path")
        if _digest(Path(path)) != digest:
            raise ValueError("artifact changed")
        if path.endswith(".stderr") and Path(path).stat().st_size:
            raise ValueError("worker stderr not empty")
    if _read(Path(f"{prefix}.registration.json")) != registration:
        raise ValueError("registration identity mismatch")
    elapsed = manifest["elapsed_seconds"]
    if not math.isfinite(elapsed) or not 0 < elapsed < registration["plan"]["campaign_seconds"]:
        raise ValueError("invalid campaign elapsed time")
    expected = len(registration["schedule"])
    if manifest["failures"] or (manifest["completed"], manifest["attempted"], manifest["unexecuted"]) != (
        expected,
        expected,
        0,
    ):
        raise ValueError("incomplete campaign")
    records = []
    for index, case in enumerate(registration["schedule"]):
        record = _read(Path(f"{prefix}-{index:02d}.json"))
        _validate_record(record, case, registration["plan"])
        records.append(record)
    return records


def analyze(registration: dict[str, Any], prefix: Path) -> dict[str, Any]:
    records = verify(registration, prefix)
    sizes = {}
    for size in registration["plan"]["sizes"]:
        selected = [r for r in records if r["case"]["size"] == size]
        sizes[str(size)] = {
            "requested_rows": size,
            "completed_rows": [r["loaded"] for r in selected],
            "requested_scope_complete": all(r["load_status"] == "complete" for r in selected),
            "repetitions": [
                {
                    "repetition": r["case"]["repetition"],
                    "insert": {e: original._stats(r["insert_ns"][e]) for e in ("mini", "sqlite")},
                    "query": {
                        e: {
                            q: original._stats([x["results"][e]["ns"] for x in r["queries"] if x["name"] == q])
                            for q in ("point", "missing")
                        }
                        for e in ("mini", "sqlite")
                    },
                    "native_scans": r["scans"],
                }
                for r in selected
            ],
        }
    return {
        "control": registration["control"],
        "sizes": sizes,
        "disposition": "control_only"
        if registration["control"]
        else (
            "requested_scope_complete"
            if all(s["requested_scope_complete"] for s in sizes.values())
            else "required_scale_unresolved"
        ),
        "limits": [
            "three repetitions are descriptive, not a speed-adoption test",
            "native heap rows and SQLite VM steps are different; SQLite scan counts unavailable",
            "warm OS cache uncontrolled; power-loss equivalence unverified",
            "partial load latency never completes requested scale",
        ],
    }


def _settings(*, control: bool) -> dict[str, Any]:
    result = {**_read(original.CONFIG), **_read(PLAN)}
    if control:
        result.update(sizes=[24], repetitions=1, query_repetitions=2)
    return result


def _schedule(plan: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        {"size": n, "repetition": r, "order": ["mini", "sqlite"] if r % 2 == 0 else ["sqlite", "mini"]}
        for n in plan["sizes"]
        for r in range(plan["repetitions"])
    ]


def _run_case(case: dict[str, Any], context: tuple[Path, int, dict[str, Any], float]) -> dict[str, Any]:
    prefix, index, plan, deadline = context
    destination = Path(f"{prefix}-{index:02d}")
    record: dict[str, Any] = {
        "case": case,
        "loaded": 0,
        "insert_ns": {e: [] for e in ("mini", "sqlite")},
        "queries": [],
    }
    workers: list[_Worker] = []
    try:
        for engine in case["order"]:
            workers.append(
                _Worker(
                    engine,
                    (ROOT / plan["driver"], Path(f"{destination}.{engine}.db"), Path(f"{destination}.{engine}.stderr")),
                )
            )
        _initialize(record, workers, (plan, deadline))
        before = _scan(workers, deadline)
        _load(record, workers, (plan, deadline))
        after = _scan(workers, deadline)
        record["scans"] = {"insert": _difference(before, after)}
        _queries(record, workers, (plan, deadline))
        record["reopen"] = {}
        for worker in workers:
            response = worker.execute("@reopen", min(plan["statement_timeout_seconds"], _remaining(deadline)))
            record["reopen"][worker.engine] = response
            if response["error"]:
                raise ValueError("reopen failed")
        _pair(record, workers, "SELECT COUNT(*) FROM items", (plan, deadline))
        if original._scalar(record["inflight"]["results"]["mini"]) != record["loaded"]:
            raise ValueError("reopened row count mismatch")
        record["reopen_count"] = record.pop("inflight")["results"]
        before = _scan(workers, deadline)
        _pair(record, workers, "SELECT id FROM items WHERE name = 'user-1'", (plan, deadline))
        record["full_scan_control"] = record.pop("inflight")["results"]
        record["scans"]["full_scan_control"] = _difference(before, _scan(workers, deadline))
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        record["failure"] = repr(error)
    finally:
        record["close"] = []
        for worker in workers:
            try:
                worker.close()
                if Path(f"{destination}.{worker.engine}.stderr").stat().st_size:
                    raise ValueError("worker stderr not empty")
            except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
                record["failure"] = record.get("failure", repr(error))
            record["close"].append(
                {"engine": worker.engine, "exit": worker.process.returncode, "pending_hex": worker.pending.hex()}
            )
        _write(Path(f"{destination}.json"), record)
    return record


def _initialize(record: dict[str, Any], workers: list[_Worker], context: tuple[dict[str, Any], float]) -> None:
    plan, deadline = context
    sqlite = next(worker for worker in workers if worker.engine == "sqlite")
    record["sqlite_settings"] = {
        name: sqlite.execute(f"PRAGMA {name}", min(plan["statement_timeout_seconds"], _remaining(deadline)))
        for name in ("page_size", "journal_mode", "synchronous", "fullfsync", "cache_size", "mmap_size", "temp_store")
    }
    for sql in [
        "CREATE TABLE items (id INTEGER PRIMARY KEY, bucket INTEGER, score REAL, name TEXT, note TEXT, payload TEXT, binary BLOB)",
        "CREATE TABLE groups (id INTEGER PRIMARY KEY, label TEXT)",
        *[f"INSERT INTO groups VALUES ({i}, 'group-{i}')" for i in range(97)],
    ]:
        _pair(record, workers, sql, context)
        record.pop("inflight")


def _load(record: dict[str, Any], workers: list[_Worker], context: tuple[dict[str, Any], float]) -> None:
    plan, _ = context
    start = time.monotonic()
    for index in range(1, record["case"]["size"] + 1):
        if time.monotonic() - start >= plan["load_timeout_seconds"]:
            break
        _pair(record, workers, original._insert_sql(index, plan["seed"]), context)
        for engine, value in record["inflight"]["results"].items():
            record["insert_ns"][engine].append(value["ns"])
        record["loaded"] = index
        record["load_last"] = {e: original._compact(v) for e, v in record.pop("inflight")["results"].items()}
    record["load_elapsed_seconds"] = time.monotonic() - start
    record["load_status"] = (
        "complete"
        if record["loaded"] == record["case"]["size"] and record["load_elapsed_seconds"] < plan["load_timeout_seconds"]
        else "load_timeout"
    )
    if record["loaded"] == 0:
        raise ValueError("no rows loaded")


def _queries(record: dict[str, Any], workers: list[_Worker], context: tuple[dict[str, Any], float]) -> None:
    plan, deadline = context
    for repetition in range(-plan["warmups"], plan["query_repetitions"]):
        seed = plan["seed"] + max(repetition, 0)
        for name, sql in original._query_cases(record["loaded"], seed):
            before = _scan(workers, deadline)
            _pair(record, workers, sql, context)
            result = record.pop("inflight")["results"]
            delta = _difference(before, _scan(workers, deadline))
            if repetition >= 0:
                if name not in ("point", "missing"):
                    result = {engine: original._compact(value) for engine, value in result.items()}
                record["queries"].append(
                    {"repetition": repetition, "name": name, "sql": sql, "results": result, "scan": delta}
                )
    record["scans"]["queries"] = [q["scan"] for q in record["queries"]]


def _pair(record: dict[str, Any], workers: list[_Worker], sql: str, context: tuple[dict[str, Any], float]) -> None:
    plan, deadline = context
    record["inflight"] = {"sql": sql, "results": {}}
    for worker in workers:
        record["inflight"]["results"][worker.engine] = worker.execute(
            sql, min(plan["statement_timeout_seconds"], _remaining(deadline))
        )
    original._compare(record["inflight"]["results"], sql)


def _scan(workers: list[_Worker], deadline: float) -> list[int]:
    mini = next(w for w in workers if w.engine == "mini")
    value = mini.execute("@scan", min(30, _remaining(deadline)))
    if value["error"] or value["row_count"] != 1 or value["columns"] != 3:
        raise ValueError("invalid native scan response")
    return [int(cell.removeprefix("I")) for cell in value["rows"][0].strip(";").split(";")]


def _difference(before: list[int], after: list[int]) -> list[int]:
    result = [b - a for a, b in zip(before, after)]
    if len(result) != 3 or any(n < 0 for n in result):
        raise ValueError("invalid scan counter delta")
    return result


def _validate_record(record: dict[str, Any], case: dict[str, Any], plan: dict[str, Any]) -> None:
    n = record["loaded"]
    if record.get("failure") or record["case"] != case or not 0 < n <= case["size"]:
        raise ValueError("failed or mismatched case")
    if any(len(record["insert_ns"][e]) != n for e in ("mini", "sqlite")):
        raise ValueError("incomplete insertion observations")
    values = [v for a in record["insert_ns"].values() for v in a]
    values += [r["ns"] for q in record["queries"] for r in q["results"].values()]
    if any(not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError("invalid latency")
    expected = (
        "complete"
        if n == case["size"] and record["load_elapsed_seconds"] < plan["load_timeout_seconds"]
        else "load_timeout"
    )
    if (
        record["load_status"] != expected
        or not math.isfinite(record["load_elapsed_seconds"])
        or record["load_elapsed_seconds"] <= 0
    ):
        raise ValueError("incorrect load completion classification")
    if len(record["queries"]) != 6 * plan["query_repetitions"]:
        raise ValueError("incomplete query observations")
    queries = [(r, *q) for r in range(plan["query_repetitions"]) for q in original._query_cases(n, plan["seed"] + r)]
    for q, expected_query in zip(record["queries"], queries):
        _validate_query(q, (expected_query, n, plan["seed"]))
    if record["scans"]["insert"][0] != 0 or record["scans"]["insert"][1] <= 0:
        raise ValueError("primary-key insertion scanned rows or missed counters")
    if record["scans"]["full_scan_control"][0] != n:
        raise ValueError("full-scan positive control failed")
    for engine in ("mini", "sqlite"):
        if record["reopen"][engine]["error"] or original._scalar(record["reopen_count"][engine]) != n:
            raise ValueError("reopen oracle mismatch")
        if record["full_scan_control"][engine]["rows"] != ["I1;"]:
            raise ValueError("full-scan result mismatch")
    if [c["engine"] for c in record["close"]] != case["order"] or any(c["exit"] != 0 for c in record["close"]):
        raise ValueError("worker exit mismatch")
    settings = {
        "page_size": "I4096;",
        "journal_mode": "T64656c657465;",
        "synchronous": "I2;",
        "fullfsync": "I0;",
        "cache_size": "I-2048;",
        "mmap_size": "I0;",
        "temp_store": "I1;",
    }
    if any(record["sqlite_settings"][k]["rows"] != [v] for k, v in settings.items()):
        raise ValueError("SQLite configuration differs from fixed original contract")


def _validate_query(query: dict[str, Any], context: tuple[tuple[int, str, str], int, int]) -> None:
    expected, count, seed = context
    _, name, sql = expected
    if (query["repetition"], query["name"], query["sql"]) != expected:
        raise ValueError("query schedule mismatch")
    if name not in ("point", "missing"):
        _validate_descriptive_query(query, (count, seed))
        return
    original._compare(query["results"], sql)
    if query["scan"][0] != 0 or query["scan"][1] <= 0:
        raise ValueError("primary-key query did not use native index")
    key = int(sql.rsplit(" = ", 1)[1])
    rows = [f"I{key};T{('user-' + str(key)).encode().hex()};"] if name == "point" else []
    columns = 2 if name == "point" else 1
    for result in query["results"].values():
        if result["rows"] != rows or result["row_count"] != len(rows) or result["columns"] != columns:
            raise ValueError("independent point/missing oracle mismatch")


def _validate_descriptive_query(query: dict[str, Any], context: tuple[int, int]) -> None:
    count, seed = context
    name = query["name"]
    rows = {
        "range": min(count, 100),
        "bucket": sum((i * 17 + seed) % 97 == 3 for i in range(1, count + 1)),
        "null": count // 11,
        "full_scan": count,
    }[name]
    columns = 7 if name == "full_scan" else 2
    left, right = (query["results"][engine] for engine in ("mini", "sqlite"))
    for result in (left, right):
        if result["error"] or (result["row_count"], result["columns"]) != (rows, columns):
            raise ValueError("descriptive query count or shape mismatch")
    if left["rows_sha256"] != right["rows_sha256"]:
        raise ValueError("descriptive query result digest mismatch")


def _validate_registration(registration: dict[str, Any]) -> None:
    if registration["plan"] != _settings(control=registration["control"]) or registration["schedule"] != _schedule(
        registration["plan"]
    ):
        raise ValueError("registered plan or schedule changed")
    if set(registration["sources"]) != set(SOURCES):
        raise ValueError("registered source coverage mismatch")
    if set(registration["identities"]) != {registration["plan"][k] for k in IDENTITY_KEYS}:
        raise ValueError("registered binary coverage mismatch")
    if (
        registration["environment"]["platform"] != platform.platform()
        or registration["environment"]["python"] != platform.python_version()
        or registration["environment"]["sqlite"]
        != subprocess.check_output([str(ROOT / registration["plan"]["driver"]), "--version"], text=True).strip()
    ):
        raise ValueError("registered platform/interpreter changed")
    for group in ("sources", "runtime", "identities"):
        for path, digest in registration[group].items():
            if _digest(ROOT / path) != digest:
                raise ValueError(f"registered input changed: {path}")
    if registration["runtime"] != _runtime(registration["plan"]["runtime_commit"]):
        raise ValueError("runtime coverage mismatch")
    if not registration["control"]:
        _check_sources(registration["plan_commit"], registration["sources"])


def _runtime(commit: str) -> dict[str, str]:
    names = _git("ls-tree", "-r", "--name-only", commit, "src", "include", "python").splitlines()
    result = {}
    for p in names:
        if not p.endswith((".c", ".h", ".py")):
            continue
        digest = hashlib.sha256(subprocess.check_output(["git", "show", f"{commit}:{p}"], cwd=ROOT)).hexdigest()
        if _digest(ROOT / p) != digest:
            raise ValueError(f"runtime content mismatch: {p}")
        result[p] = digest
    return result


def _check_sources(commit: str, hashes: dict[str, str]) -> None:
    _git("merge-base", "--is-ancestor", commit, "HEAD")
    for path, digest in hashes.items():
        blob = subprocess.check_output(["git", "show", f"{commit}:{path}"], cwd=ROOT)
        if hashlib.sha256(blob).hexdigest() != digest:
            raise ValueError("plan source differs from registered ancestor")


def _git(*arguments: str) -> str:
    return subprocess.check_output(["git", *arguments], cwd=ROOT, text=True)


def _remaining(deadline: float) -> float:
    value = deadline - time.monotonic()
    if value <= 0:
        raise TimeoutError("campaign deadline exceeded")
    return value


def _files(prefix: Path) -> list[Path]:
    return sorted(p for p in prefix.parent.glob(prefix.name + "*") if p.is_file())


def _check_storage(prefix: Path, plan: dict[str, Any]) -> None:
    if sum(p.stat().st_size for p in _files(prefix)) > plan["disk_bytes"]:
        raise ValueError("campaign storage exceeded")
    if shutil.disk_usage(prefix.parent).free < plan["disk_headroom_bytes"]:
        raise ValueError("insufficient disk headroom")


def _required(prefix: Path, cases: list[dict[str, Any]]) -> set[str]:
    paths = {f"{prefix}.registration.json"}
    for i in range(len(cases)):
        paths.add(f"{prefix}-{i:02d}.json")
        for e in ("mini", "sqlite"):
            paths.update((f"{prefix}-{i:02d}.{e}.db", f"{prefix}-{i:02d}.{e}.stderr"))
    return paths


def _digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            value.update(block)
    return value.hexdigest()


def _read(path: Path) -> Any:
    return json.loads(path.read_text(), object_pairs_hook=_unique)


def _unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _write(path: Path, value: Any) -> None:
    with path.open("x") as output:
        json.dump(value, output, sort_keys=True, indent=2, allow_nan=False)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())


def _main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["build", "register", "collect", "verify", "analyze"])
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--plan-commit", default="HEAD")
    parser.add_argument("--control", action="store_true")
    args = parser.parse_args()
    if args.command == "build":
        print(json.dumps(build(), sort_keys=True))
        return 0
    if args.prefix is None:
        parser.error("--prefix is required")
    prefix = args.prefix.resolve()
    if args.command == "register":
        result = register(prefix, control=args.control, plan_commit=args.plan_commit)
        print(json.dumps({"registered": len(result["schedule"]), "control": result["control"]}))
        return 0
    registration = _read(Path(f"{prefix}.registration.json"))
    if args.command == "collect":
        collect(registration, prefix)
    elif args.command == "verify":
        print(json.dumps({"verified": len(verify(registration, prefix)), "control": registration["control"]}))
    else:
        print(json.dumps(analyze(registration, prefix), sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    try:
        raise SystemExit(_main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        logger.error("primary-key scale failed: %s", error)
        raise SystemExit(1) from error
