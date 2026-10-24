from __future__ import annotations

import argparse
import gzip
import hashlib
import itertools
import json
import os
import platform
import plistlib
import resource
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from common_cost_analysis import analyze, analyze_python, python_dimensions, validate_diagnostics, validate_native

__all__ = ["cases", "collect", "register", "verify"]

ROOT = Path(__file__).resolve().parents[1]
PLAN_PATH = ROOT / "bench/common_cost_plan.json"
EXT = "dylib" if sys.platform == "darwin" else "so"
PHASES = [
    "request",
    "parse",
    "exec_including_parse",
    "metadata",
    "publish",
    "row_index_partial",
    "handle_mutex_acquire",
    "file_lock_acquire",
    "pread",
    "pwrite",
    "fsync",
    "flock_syscall",
]


def cases(plan: dict[str, Any], phase: str) -> list[dict[str, Any]]:
    result = []
    blocks = [-1, *range(plan["formal_blocks"])]
    if phase == "cache":
        dimensions = itertools.product(plan["patterns"], plan["payload_bytes"], plan["groups"])
        for pattern, payload, group in dimensions:
            for block in blocks:
                order = _condition_order(plan["conditions"], block)
                for position, condition in enumerate(order):
                    result.append(
                        {
                            "pattern": pattern,
                            "payload": payload,
                            "group": group,
                            "block": block,
                            "condition": condition,
                            "position": position,
                            "instrumented": 0,
                        }
                    )
            for condition in plan["conditions"]:
                result.append(
                    {
                        "pattern": pattern,
                        "payload": payload,
                        "group": group,
                        "block": -2,
                        "condition": condition,
                        "position": 0,
                        "instrumented": 1,
                    }
                )
    else:
        conditions = ["eager", "lazy"] if phase == "lifecycle" else ["baseline", "candidate"]
        for (scenario, payload), block in itertools.product(python_dimensions(plan, phase), blocks):
            order = conditions if block % 2 == 0 else conditions[::-1]
            for position, condition in enumerate(order):
                result.append(
                    {
                        "scenario": scenario,
                        "payload": payload,
                        "block": block,
                        "condition": condition,
                        "position": position,
                        "instrumented": 0,
                    }
                )
        for scenario, payload in python_dimensions(plan, phase):
            for condition in conditions:
                result.append(
                    {
                        "scenario": scenario,
                        "payload": payload,
                        "block": -2,
                        "condition": condition,
                        "position": 0,
                        "instrumented": 1,
                    }
                )
    for index, case in enumerate(result):
        case["id"] = f"{index:04d}"
    return result


def register(args: argparse.Namespace) -> dict[str, Any]:
    plan = json.loads(PLAN_PATH.read_text())
    if args.control:
        plan.update(rows=16, operations=8, groups=[1, 4], payload_bytes=[32], formal_blocks=1, lifecycle_instances=2)
    else:
        _require_clean()
        _run(["git", "merge-base", "--is-ancestor", args.plan_commit, "HEAD"])
        for name in _criteria_sources():
            expected = _run(["git", "show", f"{args.plan_commit}:{name}"], binary=True)
            if hashlib.sha256(expected).hexdigest() != _digest(ROOT / name):
                raise ValueError(f"preregistered input changed: {name}")
    if args.phase == "conditional":
        _check_conditional_eligibility(args)
    builds = {"off": _build_identity(args.off_build, instrumented=0)}
    if args.phase in ("cache", "lifecycle", "conditional"):
        builds["on"] = _build_identity(args.on_build, instrumented=1)
        shared = [
            next(line for line in builds[name]["fingerprint"].splitlines() if line.startswith("SHARED_CFLAGS="))
            for name in ("off", "on")
        ]
        if shared[0] != shared[1].replace("-UTOTE_BENCHMARK -DTOTE_BENCHMARK=1", "-UTOTE_BENCHMARK"):
            raise ValueError("ON/OFF builds differ beyond instrumentation")
    if args.phase == "conditional":
        builds["candidate"] = _build_identity(args.candidate_build, instrumented=0)
        builds["candidate_on"] = _build_identity(args.candidate_on_build, instrumented=1)
        if builds["candidate"]["library_sha256"] == builds["off"]["library_sha256"]:
            raise ValueError("conditional candidate equals baseline")
        reference_flags = next(
            line for line in builds["off"]["fingerprint"].splitlines() if line.startswith("SHARED_CFLAGS=")
        )
        for name in ("candidate", "candidate_on"):
            flags = next(line for line in builds[name]["fingerprint"].splitlines() if line.startswith("SHARED_CFLAGS="))
            if flags.replace("-UTOTE_BENCHMARK -DTOTE_BENCHMARK=1", "-UTOTE_BENCHMARK") != reference_flags:
                raise ValueError("conditional build flags differ from baseline")
    if not args.control and args.phase == "cache":
        for name in ("off", "on"):
            for kind in ("driver_sha256", "library_sha256"):
                if builds[name][kind] != plan["validated_builds"][name][kind]:
                    raise ValueError("cache comparison binary differs from preregistered build")
    return {
        "schema": 1,
        "control": args.control,
        "phase": args.phase,
        "plan": plan,
        "plan_commit": args.plan_commit,
        "head": _run(["git", "rev-parse", "HEAD"]).strip(),
        "source_hashes": {name: _digest(ROOT / name) for name in _registration_sources()},
        "criteria_hashes": {name: _digest(ROOT / name) for name in _criteria_sources()},
        "input_sha256": hashlib.sha256(json.dumps(plan, sort_keys=True).encode()).hexdigest(),
        "eligibility": _eligibility_identity(args, builds) if args.phase == "conditional" else None,
        "design_at_plan_sha256": hashlib.sha256(
            _run(["git", "show", f"{args.plan_commit}:docs/design/streamlite.md"], binary=True)
        ).hexdigest(),
        "builds": builds,
        "cases": cases(plan, args.phase),
        "environment": _environment(),
        "phase_names": PHASES,
        "native_phase_not_applicable": ["handle_mutex_acquire"],
        "python": str(Path(sys.executable).resolve()),
        "python_sha256": _digest(Path(sys.executable).resolve()),
        "library_dependencies": _run(["otool", "-L", builds["off"]["library"]])
        if sys.platform == "darwin"
        else _run(["ldd", builds["off"]["library"]]),
        "baseline_source": _run(["git", "rev-parse", plan["baseline_commit"]]).strip(),
        "lifecycle_baseline_sha256": hashlib.sha256(
            _run(["git", "show", f"{plan['lifecycle_baseline_commit']}:python/totedb/database.py"], binary=True)
        ).hexdigest(),
    }


def _eligibility_identity(args: argparse.Namespace, builds: dict[str, Any]) -> dict[str, Any]:
    evidence = json.loads(args.eligibility.read_text())
    for name in ("off", "on", "candidate", "candidate_on"):
        if evidence["library_hashes"][name] != builds[name]["library_sha256"]:
            raise ValueError("eligibility binary identity mismatch")
        _run(["git", "cat-file", "-e", f"{evidence['build_source_commits'][name]}^{{commit}}"])
    _run(["git", "merge-base", "--is-ancestor", evidence["source_commit"], "HEAD"])
    artifacts = evidence["artifacts"]
    if not artifacts or any(_digest(Path(path)) != expected for path, expected in artifacts.items()):
        raise ValueError("eligibility artifacts missing or changed")
    return {"path": str(args.eligibility.resolve()), "sha256": _digest(args.eligibility), "evidence": evidence}


def collect(registration: dict[str, Any], prefix: Path) -> None:
    _validate_registration(registration)
    raw = Path(f"{prefix}.raw.jsonl")
    if raw.exists() or Path(f"{prefix}.manifest.json").exists():
        raise ValueError("collection output exists; use a new prefix")
    allowed = {prefix.name + ".registration.json", prefix.name + ".collect.log"}
    if any(path.name not in allowed for path in prefix.parent.glob(prefix.name + "*")):
        raise ValueError("collection prefix overlaps existing artifacts")
    start = time.monotonic()
    plan = registration["plan"]
    fixtures: dict[int, Path] = {}
    failures = []
    completed = 0
    completed_ids = set()
    with raw.open("x") as output:
        for case in registration["cases"]:
            if time.monotonic() - start > plan["campaign_timeout_seconds"]:
                failures.append({"case": case, "error": "campaign timeout"})
                break
            try:
                result = _collect_case(registration, case, prefix, fixtures)
            except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
                failures.append({"case": case, "error": str(error)})
                break
            output.write(json.dumps(result, sort_keys=True) + "\n")
            completed += 1
            completed_ids.add(case["id"])
            output.flush()
            os.fsync(output.fileno())
            print(json.dumps({"case": case["id"], "completed": result["sample"]["completed"]}), flush=True)
            if _storage(prefix) > plan["campaign_storage_bytes"]:
                failures.append({"case": case, "error": "campaign storage budget"})
                break
    files = sorted(
        path
        for path in prefix.parent.glob(prefix.name + "*")
        if path.suffix in {".stdout", ".stderr", ".db", ".gz", ".jsonl"}
        or path.name == prefix.name + ".registration.json"
    )
    manifest = {
        "schema": 1,
        "control": registration["control"],
        "planned": len(registration["cases"]),
        "completed": completed,
        "unexecuted": len(registration["cases"])
        - completed
        - sum(failure["case"]["id"] not in completed_ids for failure in failures),
        "registration_sha256": _registration_digest(registration),
        "failures": failures,
        "elapsed_seconds": time.monotonic() - start,
        "hashes": {str(path.resolve()): _digest(path) for path in files if path.is_file()},
    }
    _write_new(Path(f"{prefix}.manifest.json"), manifest)
    if failures:
        raise ValueError("collection stopped; failed stdout/stderr/database and partial raw data retained")


def verify(registration: dict[str, Any], prefix: Path) -> list[dict[str, Any]]:
    for name, expected in registration["criteria_hashes"].items():
        if _digest(ROOT / name) != expected:
            raise ValueError(f"analysis input changed since registration: {name}")
    manifest = json.loads(Path(f"{prefix}.manifest.json").read_text())
    if manifest["registration_sha256"] != _registration_digest(registration):
        raise ValueError("registration differs from collection")
    for name, expected in manifest["hashes"].items():
        if _digest(Path(name)) != expected:
            raise ValueError(f"raw artifact changed: {name}")
    records = [json.loads(line) for line in Path(f"{prefix}.raw.jsonl").read_text().splitlines()]
    if [record["case"] for record in records] != registration["cases"][: len(records)]:
        raise ValueError("case order, pairing or unique identity mismatch")
    for record in records:
        _validate_sample(record["sample"], record["case"], registration)
    if manifest["failures"] or len(records) != len(registration["cases"]):
        raise ValueError("incomplete campaign; no successful-only analysis")
    return records


def _condition_order(conditions: list[str], block: int) -> list[str]:
    rotation = max(block, 0) // 2 % len(conditions)
    rotated = conditions[rotation:] + conditions[:rotation]
    return rotated if block % 2 == 0 else rotated[::-1]


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _registration_digest(registration: dict[str, Any]) -> str:
    return hashlib.sha256(json.dumps(registration, sort_keys=True).encode()).hexdigest()


def _run(command: list[str], *, binary: bool = False) -> Any:
    return subprocess.run(command, cwd=ROOT, capture_output=True, text=not binary, check=True).stdout


def _write_new(path: Path, value: Any) -> None:
    with path.open("x") as stream:
        json.dump(value, stream, sort_keys=True, indent=2, allow_nan=False)
        stream.write("\n")


def _registration_sources() -> list[str]:
    files = ["Makefile", "docs/design/streamlite.md"]
    for folder in ("src", "include", "python/totedb", "bench"):
        files.extend(
            str(path.relative_to(ROOT))
            for path in (ROOT / folder).rglob("*")
            if path.is_file() and path.suffix in {".c", ".h", ".py", ".mk", ".json"}
        )
    return sorted(files)


def _criteria_sources() -> list[str]:
    return sorted(
        str(path.relative_to(ROOT))
        for path in (ROOT / "bench").glob("common_cost*")
        if path.suffix in {".c", ".py", ".json"}
    )


def _require_clean() -> None:
    if _run(["git", "status", "--porcelain"]):
        raise ValueError("formal collection requires a clean source tree")


def _environment() -> dict[str, Any]:
    import _sqlite3  # noqa: PLC0415
    import numpy  # noqa: PLC0415
    import scipy  # noqa: PLC0415
    import sqlite3  # noqa: PLC0415

    return {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "python": sys.version,
        "numpy": numpy.__version__,
        "scipy": scipy.__version__,
        "sqlite_python": sqlite3.sqlite_version,
        "sqlite_module_sha256": _digest(Path(_sqlite3.__file__)),
        "sqlite_dependencies": _run(["otool", "-L", _sqlite3.__file__])
        if sys.platform == "darwin"
        else _run(["ldd", _sqlite3.__file__]),
        "compiler": _run(["gcc", "--version"]),
        "disk": _run(["df", "-P", str(ROOT)]),
        "hardware": _run(["sysctl", "-n", "machdep.cpu.brand_string", "hw.memsize", "hw.ncpu"])
        if sys.platform == "darwin"
        else _run(["uname", "-a"]),
        "filesystem": _filesystem() if sys.platform == "darwin" else _run(["stat", "-f", "-c", "%T", str(ROOT)]),
        "cache_state": "fresh handles and copied fixtures; OS cache not flushed",
        "clock": "monotonic wall and whole-worker process CPU; RSS is process high-water bytes",
    }


def _filesystem() -> dict[str, Any]:
    device = _run(["df", "-P", str(ROOT)]).splitlines()[-1].split()[0]
    info = plistlib.loads(_run(["diskutil", "info", "-plist", device], binary=True))
    names = ("FilesystemType", "DeviceIdentifier", "VolumeName", "MediaName", "SolidState", "DiskSize")
    return {name: info.get(name, "unavailable") for name in names}


def _build_identity(path: Path, *, instrumented: int) -> dict[str, Any]:
    build = path.resolve()
    flags = (build / ".build-fingerprint").read_text()
    if f"BENCHMARK={instrumented}\n" not in flags:
        raise ValueError("build instrumentation fingerprint mismatch")
    shared = next(line for line in flags.splitlines() if line.startswith("SHARED_CFLAGS="))
    if "-O2" not in shared or "-fsanitize" in shared:
        raise ValueError("outcomes require the optimized unsanitized shared path")
    library = build / f"lib/libtotedb.{EXT}"
    driver = build / "common_cost_driver"
    return {
        "directory": str(build),
        "fingerprint": flags,
        "library": str(library),
        "driver": str(driver),
        "library_sha256": _digest(library),
        "driver_sha256": _digest(driver),
    }


def _validate_registration(registration: dict[str, Any]) -> None:
    if _registration_digest(registration["plan"]) != registration["input_sha256"]:
        raise ValueError("registered input checksum mismatch")
    if not registration["control"]:
        fixed_plan = json.loads(PLAN_PATH.read_text())
        if registration["plan"] != fixed_plan or registration["cases"] != cases(fixed_plan, registration["phase"]):
            raise ValueError("registered plan or schedule differs from fixed criteria")
    if _digest(Path(registration["python"])) != registration["python_sha256"]:
        raise ValueError("Python executable changed after registration")
    eligibility = registration["eligibility"]
    if eligibility is not None:
        if _digest(Path(eligibility["path"])) != eligibility["sha256"]:
            raise ValueError("eligibility changed after registration")
        for path, expected in eligibility["evidence"]["artifacts"].items():
            if _digest(Path(path)) != expected:
                raise ValueError("eligibility artifact changed after registration")
    if not registration["control"]:
        _require_clean()
        _run(["git", "merge-base", "--is-ancestor", registration["plan_commit"], "HEAD"])
    for name, expected in registration["source_hashes"].items():
        if _digest(ROOT / name) != expected:
            raise ValueError(f"source changed after registration: {name}")
    for build in registration["builds"].values():
        for kind in ("driver", "library"):
            if _digest(Path(build[kind])) != build[f"{kind}_sha256"]:
                raise ValueError(f"binary changed after registration: {kind}")
        if (Path(build["directory"]) / ".build-fingerprint").read_text() != build["fingerprint"]:
            raise ValueError("build configuration changed after registration")
    current = _environment()
    for name in (
        "platform",
        "machine",
        "python",
        "numpy",
        "scipy",
        "compiler",
        "hardware",
        "filesystem",
        "sqlite_python",
        "sqlite_module_sha256",
        "sqlite_dependencies",
    ):
        if current[name] != registration["environment"][name]:
            raise ValueError(f"environment changed after registration: {name}")
    if (
        current["numpy"] != registration["plan"]["numpy_version"]
        or current["scipy"] != registration["plan"]["scipy_version"]
    ):
        raise ValueError("analysis versions differ from fixed plan")


def _check_conditional_eligibility(args: argparse.Namespace) -> None:
    if args.candidate_build is None or args.candidate_on_build is None or args.eligibility is None:
        raise ValueError(
            "conditional path unsupported until candidate build and Task 16 eligibility evidence are pinned"
        )
    evidence = json.loads(args.eligibility.read_text())
    required = ("minimum_id", "lease_fence", "duplicate_stale_failed", "cancel_oom", "reopen", "failure_readback")
    if not all(evidence.get(name) is True for name in required):
        raise ValueError("conditional correctness eligibility incomplete")


def _worker_command(
    registration: dict[str, Any], case: dict[str, Any], destination: Path
) -> tuple[list[str], dict[str, str]]:
    plan = registration["plan"]
    build_name = "candidate" if case["condition"] == "candidate" else "off"
    if case["instrumented"]:
        build_name = "candidate_on" if case["condition"] == "candidate" else "on"
    build = registration["builds"][build_name]
    command = [
        registration["python"],
        str(ROOT / "bench/common_cost_worker.py"),
        "--prefix",
        str(destination),
        "--condition",
        case["condition"],
        "--scenario",
        case["scenario"],
        "--instances",
        str(plan["lifecycle_instances"]),
        "--operations",
        str(plan["operations"]),
        "--payload",
        str(case["payload"]),
    ]
    return command, {**os.environ, "TOTEDB_LIB": build["library"], "PYTHONPATH": str(ROOT / "python")}


def _native_command(registration: dict[str, Any], case: dict[str, Any], destination: Path, mode: str) -> list[str]:
    plan = registration["plan"]
    build = registration["builds"]["on" if case["instrumented"] else "off"]
    return [
        build["driver"],
        mode,
        str(destination),
        str(plan["rows"]),
        str(plan["operations"]),
        str(case["group"]),
        str(case["payload"]),
        case["pattern"],
        *case["condition"],
        "0",
    ]


def _execute(command: list[str], prefix: Path, timeout: float, env: dict[str, str] | None = None) -> dict[str, Any]:
    with Path(f"{prefix}.stdout").open("x") as stdout, Path(f"{prefix}.stderr").open("x") as stderr:
        child_before = resource.getrusage(resource.RUSAGE_CHILDREN)
        started = time.perf_counter_ns()
        result = subprocess.run(command, cwd=ROOT, stdout=stdout, stderr=stderr, env=env, timeout=timeout, check=False)
        process_wall_ns = time.perf_counter_ns() - started
        child_after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if result.returncode:
        raise ValueError(f"worker exit {result.returncode}; retained {prefix}.stderr")
    sample = json.loads(Path(f"{prefix}.stdout").read_text())
    sample["process_wall_ns"] = process_wall_ns
    sample["subprocess_user_ns"] = round((child_after.ru_utime - child_before.ru_utime) * 1e9)
    sample["subprocess_system_ns"] = round((child_after.ru_stime - child_before.ru_stime) * 1e9)
    return sample


def _collect_case(
    registration: dict[str, Any], case: dict[str, Any], prefix: Path, fixtures: dict[int, Path]
) -> dict[str, Any]:
    destination = Path(f"{prefix}-{case['id']}")
    if shutil.disk_usage(prefix.parent).free < 1024**3:
        raise ValueError("less than 1GiB disk headroom; preserve existing data and stop")
    if registration["phase"] == "cache":
        payload = case["payload"]
        if payload not in fixtures:
            fixture = Path(f"{prefix}-fixture-{payload}.db")
            command = _native_command(registration, case, fixture, "fixture")
            _execute(command, Path(f"{prefix}-fixture-{payload}"), registration["plan"]["worker_timeout_seconds"])
            fixtures[payload] = fixture
        database = Path(f"{destination}.db")
        if database.exists():
            raise ValueError("database destination exists")
        with database.open("xb") as output, fixtures[payload].open("rb") as source:
            shutil.copyfileobj(source, output)
        command = _native_command(registration, case, database, "run")
        sample = _execute(command, destination, registration["plan"]["worker_timeout_seconds"])
    else:
        command, env = _worker_command(registration, case, destination)
        sample = _execute(command, destination, registration["plan"]["worker_timeout_seconds"], env)
    _validate_sample(sample, case, registration)
    if registration["phase"] == "cache":
        databases = [database]
    else:
        instances = 1 if case["scenario"] == "conditional" else registration["plan"]["lifecycle_instances"]
        databases = [Path(f"{destination}-{index}.db") for index in range(instances)]
    storage_bytes = sum(path.stat().st_size for path in databases)
    archives = [_archive_database(path) for path in databases]
    return {
        "case": case,
        "sample": sample,
        "command": command,
        "storage_bytes": storage_bytes,
        "archives": archives,
        "fixture_sha256": _digest(fixtures[case["payload"]]) if registration["phase"] == "cache" else None,
    }


def _archive_database(path: Path) -> dict[str, Any]:
    expected = _digest(path)
    destination = Path(f"{path}.gz")
    with destination.open("xb") as output, path.open("rb") as source:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as archive:
            shutil.copyfileobj(source, archive)
    digest = hashlib.sha256()
    with gzip.open(destination, "rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
    if digest.hexdigest() != expected:
        raise ValueError("lossless artifact archive verification failed; original retained")
    size = path.stat().st_size
    path.unlink()
    return {
        "path": str(destination.resolve()),
        "uncompressed_sha256": expected,
        "uncompressed_bytes": size,
        "compressed_bytes": destination.stat().st_size,
    }


def _validate_sample(sample: dict[str, Any], case: dict[str, Any], registration: dict[str, Any]) -> None:
    if registration["phase"] == "cache":
        validate_native(sample, case, registration["plan"])
        return
    expected = (
        registration["plan"]["operations"]
        if case["scenario"] == "conditional"
        else registration["plan"]["lifecycle_instances"]
    )
    if sample["completed"] != expected or sample["condition"] != case["condition"]:
        raise ValueError("Python workload incomplete or condition mismatch")
    if sample["scenario"] != case["scenario"] or sample["instrumented"] != case["instrumented"]:
        raise ValueError("Python scenario or instrumentation mismatch")
    validate_diagnostics(sample)
    if sample["peak_rss_bytes"] > registration["plan"]["worker_peak_rss_bytes"]:
        raise ValueError("Python worker RSS gate failed")
    if min(sample[name] for name in ("wall_ns", "cpu_ns", "completed")) <= 0:
        raise ValueError("invalid Python metric")
    if len(sample["latencies_ns"]) != expected or any(value <= 0 for value in sample["latencies_ns"]):
        raise ValueError("incomplete Python latency samples")


def _storage(prefix: Path) -> int:
    return sum(path.stat().st_size for path in prefix.parent.glob(prefix.name + "*") if path.is_file())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("register", "collect", "verify", "analyze"))
    parser.add_argument("--registration", type=Path, required=True)
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--phase", choices=("cache", "lifecycle", "conditional"), default="cache")
    parser.add_argument("--off-build", type=Path, default=ROOT / "build")
    parser.add_argument("--on-build", type=Path, default=ROOT / "build-benchmark")
    parser.add_argument("--candidate-build", type=Path)
    parser.add_argument("--candidate-on-build", type=Path)
    parser.add_argument("--eligibility", type=Path)
    parser.add_argument("--plan-commit", default="HEAD")
    parser.add_argument("--control", action="store_true")
    args = parser.parse_args()
    if args.command == "register":
        _write_new(args.registration, register(args))
        return 0
    if args.prefix is None:
        parser.error("--prefix is required")
    registration = json.loads(args.registration.read_text())
    if args.command == "collect":
        collect(registration, args.prefix)
        return 0
    records = verify(registration, args.prefix)
    if args.command == "analyze":
        summary = (
            analyze(records, registration["plan"])
            if registration["phase"] == "cache"
            else analyze_python(records, registration["plan"], registration["phase"])
        )
        summary["control"] = registration["control"]
        print(json.dumps(summary, indent=2, sort_keys=True, allow_nan=False))
    else:
        print(json.dumps({"verified": len(records), "control": registration["control"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
