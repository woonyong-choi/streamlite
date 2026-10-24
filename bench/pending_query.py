"""Register, collect, verify or analyze matched synthetic pending-query workers."""

from __future__ import annotations

__all__ = ["cases", "register", "collect", "verify"]

import argparse
import hashlib
import itertools
import json
import logging
import math
import os
import random
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any

from common_cost import (
    _CampaignDeadline,
    _digest,
    _environment,
    _execute,
    _finish_manifest,
    _write_new,
)
from pending_query_analysis import analyze, validate_sample

ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "bench/pending_query_plan.json"
SOURCES = [
    "bench/pending_query.py",
    "bench/pending_query_worker.py",
    "bench/pending_query_analysis.py",
    "bench/pending_query_driver.c",
    "bench/pending_query_plan.json",
    "bench/common_cost.py",
    "bench/common_cost_artifacts.py",
    "bench/common_cost_analysis.py",
    "tests/test_pending_query.py",
    "tests/test_pending_query_native.c",
    "Makefile",
]
logger = logging.getLogger(__name__)


def cases(plan: dict[str, Any]) -> list[dict[str, Any]]:
    result = []
    dimensions = itertools.product(plan["backlogs"], plan["body_bytes"], plan["pages"], plan["queries"])
    for backlog, body, page, query in dimensions:
        for block in [-1, *range(plan["formal_blocks"]), -2]:
            conditions = list(plan["conditions"])
            random.Random(plan["seed"] + (block // 2 if block >= 0 else block)).shuffle(conditions)
            if block >= 0 and block % 2:
                conditions.reverse()
            for position, condition in enumerate(conditions):
                result.append(
                    {
                        "backlog": backlog,
                        "body": body,
                        "page": page,
                        "query": query,
                        "condition": condition,
                        "block": block,
                        "position": position,
                        "diagnostic": block == -2,
                    }
                )
    return result


def register(args: argparse.Namespace) -> dict[str, Any]:
    plan = json.loads(PLAN.read_text())
    source_hashes = {path: _digest(ROOT / path) for path in SOURCES}
    head = _git(["rev-parse", "HEAD"]).strip()
    if not args.control and not args.plan_commit:
        raise ValueError("plan commit is required")
    plan_commit = head if args.control else _git(["rev-parse", f"{args.plan_commit}^{{commit}}"]).strip()
    if not args.control:
        if _git(["status", "--porcelain", "--untracked-files=normal"]):
            raise ValueError("formal registration requires clean tracked sources")
        _check_ancestor(plan_commit)
        _check_git_sources(plan_commit, source_hashes)
    else:
        plan.update(backlogs=[9], body_bytes=[32], pages=[4], formal_blocks=1)
    runtime = _runtime_sources(plan["runtime_commit"])
    identities = _identities(plan)
    registration = {
        "schema": 1,
        "control": args.control,
        "head": head,
        "plan_commit": plan_commit,
        "plan": plan,
        "sources": source_hashes,
        "runtime_sources": runtime,
        "identities": identities,
        "environment": _environment(),
        "schedule": cases(plan),
    }
    registration["environment"]["cache_state"] = "reused immutable fixtures; fresh process; warm OS cache uncontrolled"
    registration["environment"]["clock"] = "query wall/CPU; process wall/CPU; snapshot RSS and lifetime highwater"
    _check_vendor(registration)
    prefix = args.prefix.resolve()
    if list(prefix.parent.glob(prefix.name + "*")):
        raise FileExistsError(f"campaign prefix already exists: {prefix}")
    _write_new(Path(f"{prefix}.registration.json"), registration)
    return registration


def collect(registration: dict[str, Any], prefix: Path) -> None:
    _validate_registration(registration)
    if not registration["control"] and _git(["status", "--porcelain", "--untracked-files=normal"]):
        raise ValueError("formal collection requires clean sources")
    if list(prefix.parent.glob(prefix.name + "*")) != [Path(f"{prefix}.registration.json")]:
        raise FileExistsError("collection prefix has existing artifacts")
    deadline = _CampaignDeadline(registration["plan"]["campaign_seconds"])
    manifest: dict[str, Any] = {
        "registration_sha256": _registration_digest(registration),
        "completed": 0,
        "attempted": 0,
        "failed_cases": 0,
        "unexecuted": len(registration["schedule"]),
        "failures": [],
        "artifacts": {},
        "fixtures": {},
    }
    raw_path = Path(f"{prefix}.raw.jsonl")
    try:
        with raw_path.open("x") as raw:
            _check_storage(prefix, registration["plan"])
            manifest["fixtures"] = _prepare_fixtures(registration, prefix, deadline)
            for index, case in enumerate(registration["schedule"]):
                deadline.remaining_seconds("before case")
                _check_storage(prefix, registration["plan"])
                manifest["attempted"] += 1
                manifest["unexecuted"] -= 1
                record = _collect_case(registration, case, (index, prefix, deadline, manifest["fixtures"]))
                raw.write(json.dumps(record, sort_keys=True, allow_nan=False) + "\n")
                raw.flush()
                os.fsync(raw.fileno())
                manifest["completed"] += 1
                deadline.remaining_seconds("raw persistence")
            _check_storage(prefix, registration["plan"])
            _validate_registration(registration)
            deadline.remaining_seconds("final identity check")
    except Exception as error:
        manifest["failures"].append({"case": manifest["attempted"] - 1, "error": repr(error)})
    manifest["failed_cases"] = manifest["attempted"] - manifest["completed"]
    _seal_artifacts(prefix, manifest, deadline)
    _finish_manifest(Path(f"{prefix}.manifest.json"), manifest, deadline)
    if manifest["failures"]:
        raise ValueError(f"campaign failed; retained {prefix}.manifest.json")


def verify(registration: dict[str, Any], prefix: Path) -> list[dict[str, Any]]:
    prefix = prefix.resolve()
    _validate_registration(registration)
    manifest = _read_unique_json(Path(f"{prefix}.manifest.json"))
    if manifest["registration_sha256"] != _registration_digest(registration):
        raise ValueError("registration identity mismatch")
    plan = registration["plan"]
    elapsed = manifest["elapsed_seconds"]
    if not math.isfinite(elapsed) or elapsed <= 0 or elapsed >= plan["campaign_seconds"]:
        raise ValueError("campaign deadline exceeded or invalid")
    expected = len(registration["schedule"])
    if (
        manifest["failures"]
        or manifest["failed_cases"] != 0
        or (
            manifest["completed"],
            manifest["attempted"],
            manifest["unexecuted"],
        )
        != (expected, expected, 0)
    ):
        raise ValueError("campaign is incomplete")
    _validate_artifacts(registration, prefix, manifest)
    records = [json.loads(line) for line in Path(f"{prefix}.raw.jsonl").read_text().splitlines()]
    if len(records) != expected:
        raise ValueError("raw record count mismatch")
    for case, record in zip(registration["schedule"], records):
        if record["case"] != case:
            raise ValueError("raw schedule mismatch")
        validate_sample(record, plan)
    _check_storage(prefix, plan)
    return records


def _read_unique_json(path: Path) -> Any:
    return json.loads(path.read_text(), object_pairs_hook=_unique_object)


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _artifact_layout(registration: dict[str, Any], prefix: Path) -> tuple[set[str], set[str], dict[str, Path]]:
    required = {f"{prefix}{suffix}" for suffix in (".registration.json", ".fixtures.json", ".raw.jsonl")}
    optional: set[str] = set()
    fixtures: dict[str, Path] = {}
    plan = registration["plan"]
    for backlog, body, engine in itertools.product(plan["backlogs"], plan["body_bytes"], ("vendor", "native")):
        key = f"{backlog}-{body}-{engine}"
        directory = Path(f"{prefix}-fixture-{key}")
        database = directory / ("data.db" if engine == "vendor" else "data.totedb")
        fixtures[key] = database
        required.update((str(database), f"{directory}.stdout", f"{directory}.stderr"))
        suffixes = ("-shm", "-wal") if engine == "vendor" else ("-lock",)
        optional.update(f"{database}{suffix}" for suffix in suffixes)
    for index, case in enumerate(registration["schedule"]):
        output = f"{prefix}-{index:04d}"
        required.update((f"{output}.stdout", f"{output}.stderr"))
        if case["condition"] == "native" and case["diagnostic"]:
            required.update((f"{output}-native.stdout", f"{output}-native.stderr"))
    return required, optional, fixtures


def _validate_artifacts(registration: dict[str, Any], prefix: Path, manifest: dict[str, Any]) -> None:
    required, optional, fixtures = _artifact_layout(registration, prefix)
    hashes = manifest.get("artifacts")
    if not isinstance(hashes, dict):
        raise ValueError("artifact hashes must be an object")
    present_auxiliary = {path for path in optional if Path(path).exists() or Path(path).is_symlink()}
    missing = (required | present_auxiliary) - hashes.keys()
    if missing:
        raise ValueError(f"missing artifact hashes: {sorted(missing)}")
    allowed = required | optional
    for name, digest in hashes.items():
        if name not in allowed or name != str(Path(name).resolve()):
            raise ValueError(f"invalid artifact path: {name}")
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise ValueError(f"invalid artifact hash: {name}")
        if not Path(name).is_file() or _digest(Path(name)) != digest:
            raise ValueError(f"artifact changed or missing: {name}")
    if _read_unique_json(Path(f"{prefix}.registration.json")) != registration:
        raise ValueError("registration file differs from supplied registration")
    expected_fixtures = {
        key: {"path": str(database.parent), "hashes": {str(database): hashes[str(database)]}}
        for key, database in fixtures.items()
    }
    if manifest.get("fixtures") != expected_fixtures:
        raise ValueError("manifest fixture layout or hashes mismatch")
    if _read_unique_json(Path(f"{prefix}.fixtures.json")) != expected_fixtures:
        raise ValueError("fixture registration layout or hashes mismatch")


def _git(arguments: list[str]) -> str:
    return subprocess.check_output(["git", *arguments], cwd=ROOT, text=True)


def _check_ancestor(commit: str) -> None:
    if not commit:
        raise ValueError("plan commit is required")
    subprocess.run(["git", "merge-base", "--is-ancestor", commit, "HEAD"], cwd=ROOT, check=True)


def _check_git_sources(commit: str, hashes: dict[str, str]) -> None:
    for path, expected in hashes.items():
        data = subprocess.check_output(["git", "show", f"{commit}:{path}"], cwd=ROOT)
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f"registered Git source mismatch: {path}")


def _runtime_sources(commit: str) -> dict[str, str]:
    paths = _git(["ls-files", "src", "include", "python/totedb"]).splitlines()
    hashes = {path: _digest(ROOT / path) for path in paths}
    _check_git_sources(commit, hashes)
    return hashes


def _identities(plan: dict[str, Any]) -> dict[str, str]:
    paths = [plan[key] for key in ("python", "library", "diagnostic_library", "driver")]
    paths += [str(Path(plan[key]).parents[1] / ".build-fingerprint") for key in ("library", "diagnostic_library")]
    paths += [str(path.relative_to(ROOT)) for path in sorted((ROOT / plan["vendor_path"]).rglob("*.py"))]
    identities = {path: _digest(ROOT / path) for path in paths}
    for path, expected in plan["pinned_artifacts"].items():
        actual = _digest(ROOT / path)
        if actual != expected:
            raise ValueError(f"pinned binary/build mismatch: {path}")
        identities[path] = actual
    return identities


def _validate_registration(registration: dict[str, Any]) -> None:
    plan = registration["plan"]
    for path, digest in {
        **registration["sources"],
        **registration["identities"],
        **registration["runtime_sources"],
    }.items():
        if _digest(ROOT / path) != digest:
            raise ValueError(f"registered input changed: {path}")
    if registration["schedule"] != cases(plan):
        raise ValueError("schedule mismatch")
    if registration["control"]:
        expected = json.loads(PLAN.read_text())
        expected.update(backlogs=[9], body_bytes=[32], pages=[4], formal_blocks=1)
    else:
        expected = json.loads(PLAN.read_text())
        _check_ancestor(registration["plan_commit"])
        _check_git_sources(registration["plan_commit"], registration["sources"])
    if plan != expected:
        raise ValueError("registered plan mismatch")
    _check_vendor(registration)


def _check_vendor(registration: dict[str, Any]) -> None:
    plan = registration["plan"]
    command = [
        str(ROOT / plan["python"]),
        "-c",
        "import importlib.metadata,sys,sqlite3,json; print(json.dumps([importlib.metadata.version('persist-queue'),sys.version,sqlite3.sqlite_version]))",
    ]
    values = json.loads(
        subprocess.check_output(command, cwd=ROOT, env=_worker_env(plan, is_diagnostic=False), text=True)
    )
    if values[0] != plan["vendor_version"] or values[1] != registration["environment"]["python"]:
        raise ValueError("worker dependency/interpreter version mismatch")
    if values[2] != registration["environment"]["sqlite_python"]:
        raise ValueError("worker SQLite version mismatch")


def _worker_env(plan: dict[str, Any], *, is_diagnostic: bool) -> dict[str, str]:
    return {
        **os.environ,
        "TOTEDB_LIB": str(ROOT / plan["diagnostic_library" if is_diagnostic else "library"]),
        "PYTHONPATH": os.pathsep.join([str(ROOT / "python"), str(ROOT / plan["vendor_path"])]),
        "PYTHONHASHSEED": str(plan["seed"]),
        "PYTHONWARNINGS": "error",
    }


def _worker(
    registration: dict[str, Any],
    settings: dict[str, Any],
    context: tuple[str, Path, _CampaignDeadline],
) -> dict[str, Any]:
    mode, prefix, deadline = context
    plan = registration["plan"]
    command = [
        str(ROOT / plan["python"]),
        str(ROOT / "bench/pending_query_worker.py"),
        mode,
        json.dumps(settings, sort_keys=True),
    ]
    result: dict[str, Any] = _execute(
        command,
        prefix,
        deadline.worker_timeout(plan["worker_seconds"]),
        _worker_env(plan, is_diagnostic=settings.get("diagnostic", False)),
    )
    if Path(f"{prefix}.stderr").read_text():
        raise ValueError(f"worker stderr is not empty: {prefix}.stderr")
    deadline.remaining_seconds("worker completion")
    return result


def _prepare_fixtures(registration: dict[str, Any], prefix: Path, deadline: _CampaignDeadline) -> dict[str, Any]:
    plan, fixtures = registration["plan"], {}
    for backlog, body, condition in itertools.product(plan["backlogs"], plan["body_bytes"], ("vendor", "native")):
        key = f"{backlog}-{body}-{condition}"
        path = Path(f"{prefix}-fixture-{key}")
        settings = {
            "path": str(path),
            "condition": condition,
            "backlog": backlog,
            "body": body,
            "batch": plan["fixture_batch"],
        }
        _worker(registration, settings, ("fixture", path, deadline))
        hashes = _fixture_hashes(path, deadline)
        fixtures[key] = {"path": str(path), "hashes": hashes}
        _check_storage(prefix, plan)
    _write_new(Path(f"{prefix}.fixtures.json"), fixtures)
    deadline.remaining_seconds("fixture registration")
    return fixtures


def _fixture_hashes(path: Path, deadline: _CampaignDeadline) -> dict[str, str]:
    return {
        str(file): _digest(file, deadline=deadline)
        for file in (path / "data.db", path / "data.totedb")
        if file.exists()
    }


def _collect_case(registration: dict[str, Any], case: dict[str, Any], context: tuple[Any, ...]) -> dict[str, Any]:
    index, prefix, deadline, fixtures = context
    engine = "vendor" if case["condition"] == "vendor" else "native"
    fixture = fixtures[f"{case['backlog']}-{case['body']}-{engine}"]
    settings = {**case, "path": fixture["path"]}
    output = Path(f"{prefix}-{index:04d}")
    sample = _worker(registration, settings, ("query", output, deadline))
    record = {"case": case, "sample": sample, "native": None}
    if case["condition"] == "native" and case["diagnostic"]:
        record["native"] = _native_probe(registration, settings, (output, deadline))
    validate_sample(record, registration["plan"])
    if _fixture_hashes(Path(fixture["path"]), deadline) != fixture["hashes"]:
        raise ValueError("query changed frozen fixture")
    return record


def _native_probe(
    registration: dict[str, Any],
    settings: dict[str, Any],
    context: tuple[Path, _CampaignDeadline],
) -> dict[str, Any]:
    prefix, deadline = context
    plan = registration["plan"]
    command = [
        str(ROOT / plan["driver"]),
        str(Path(settings["path"]) / "data.totedb"),
        str(settings["page"]),
        str(int(settings["query"] != "metadata")),
        str(int(settings["query"] in ("sequential", "accumulate"))),
        str(plan["native_query_bytes"]),
        "0",
    ]
    result: dict[str, Any] = _execute(
        command,
        Path(f"{prefix}-native"),
        deadline.worker_timeout(plan["worker_seconds"]),
    )
    if Path(f"{prefix}-native.stderr").read_text():
        raise ValueError("native diagnostic stderr is not empty")
    deadline.remaining_seconds("native diagnostic completion")
    return result


def _registration_digest(registration: dict[str, Any]) -> str:
    return hashlib.sha256(json.dumps(registration, sort_keys=True, allow_nan=False).encode()).hexdigest()


def _owned_files(prefix: Path) -> list[Path]:
    result = []
    for path in sorted(prefix.parent.glob(prefix.name + "*")):
        result.extend([path] if path.is_file() else sorted(file for file in path.rglob("*") if file.is_file()))
    return result


def _check_storage(prefix: Path, plan: dict[str, Any]) -> None:
    if sum(path.stat().st_size for path in _owned_files(prefix)) > plan["disk_bytes"]:
        raise ValueError("campaign storage budget exceeded")
    if shutil.disk_usage(prefix.parent).free < plan["disk_headroom_bytes"]:
        raise ValueError("insufficient disk headroom")


def _seal_artifacts(prefix: Path, manifest: dict[str, Any], deadline: _CampaignDeadline) -> None:
    try:
        for path in _owned_files(prefix):
            manifest["artifacts"][str(path)] = _digest(path, deadline=deadline)
        deadline.remaining_seconds("artifact persistence")
    except (TimeoutError, OSError) as error:
        manifest["failures"].append({"case": None, "error": repr(error)})


def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("register", "collect", "verify", "analyze"))
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--plan-commit", default="")
    parser.add_argument("--control", action="store_true")
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    try:
        if args.command == "register":
            print(
                json.dumps(
                    {
                        "registered": len(register(args)["schedule"]),
                        "control": args.control,
                    }
                )
            )
            return 0
        registration = json.loads(Path(f"{prefix}.registration.json").read_text())
        if args.command == "collect":
            collect(registration, prefix)
            return 0
        records = verify(registration, prefix)
        print(
            json.dumps(
                analyze(records, registration)
                if args.command == "analyze"
                else {"verified": len(records), "control": registration["control"]},
                sort_keys=True,
                indent=2,
                allow_nan=False,
            )
        )
        return 0
    except Exception:
        logger.exception("pending campaign failed")
        return 1


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    raise SystemExit(_main())
