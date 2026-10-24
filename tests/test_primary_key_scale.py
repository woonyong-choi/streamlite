from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
from typing import Any
import uuid

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))

import primary_key_scale as scale  # noqa: E402


def _prefix() -> Path:
    return ROOT / "build" / f"task20-control-{uuid.uuid4().hex[:8]}"


@pytest.fixture(scope="module")
def collected() -> tuple[Path, dict[str, Any]]:
    prefix = _prefix()
    registration = scale.register(prefix, control=True, plan_commit="HEAD")
    scale.collect(registration, prefix)
    return prefix, registration


def test_fixed_original_inputs_and_required_schedule() -> None:
    plan = scale._settings(control=False)
    assert plan["sizes"] == [1000, 10000, 100000]
    assert plan["repetitions"] == 3
    assert plan["load_timeout_seconds"] == 120
    assert plan["statement_timeout_seconds"] == 30
    assert plan["warmups"] == 1 and plan["query_repetitions"] == 20
    assert [c["size"] for c in scale._schedule(plan)] == [1000] * 3 + [10000] * 3 + [100000] * 3
    assert (
        scale.original._insert_sql(11, 137)
        == "INSERT INTO items VALUES (11, 33, 11.5, 'user-11', NULL, 'payload-가나다', X'00ff10')"
    )
    assert "x" * 9000 in scale.original._insert_sql(1000, 137)


def test_control_preserves_results_counters_reopen_and_regeneration(collected: tuple[Path, dict[str, Any]]) -> None:
    prefix, registration = collected
    records = scale.verify(registration, prefix)
    assert len(records) == 1
    record = records[0]
    assert record["loaded"] == 24 and record["load_status"] == "complete"
    assert record["scans"]["insert"][0] == 0
    assert record["scans"]["insert"][1] > 0
    assert record["scans"]["full_scan_control"][0] == 24
    assert all(q["scan"][0] == 0 and q["scan"][1] > 0 for q in record["queries"] if q["name"] in ("point", "missing"))
    assert [q["name"] for q in record["queries"][:6]] == ["point", "missing", "range", "bucket", "null", "full_scan"]
    first = scale.analyze(registration, prefix)
    assert first == scale.analyze(registration, prefix)
    assert first["disposition"] == "control_only"
    assert first["sizes"]["24"]["requested_scope_complete"]


@pytest.mark.parametrize("kind", ["registration", "record", "database", "stderr", "lock", "all"])
def test_verifier_rejects_missing_required_hashes(
    collected: tuple[Path, dict[str, Any]], kind: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration = collected
    manifest_path = Path(f"{prefix}.manifest.json")
    original = Path.read_text
    record_reads = []

    def read(path: Path, *args: Any, **kwargs: Any) -> str:
        value = original(path, *args, **kwargs)
        if path == Path(f"{prefix}-00.json"):
            record_reads.append(path)
        if path == manifest_path:
            data = json.loads(value)
            suffix = {
                "registration": ".registration.json",
                "record": "-00.json",
                "database": "-00.mini.db",
                "stderr": "-00.sqlite.stderr",
                "lock": "-00.mini.db-lock",
            }
            if kind == "all":
                data["artifacts"] = {}
            else:
                del data["artifacts"][f"{prefix}{suffix[kind]}"]
            return json.dumps(data)
        return value

    monkeypatch.setattr(Path, "read_text", read)
    with pytest.raises(ValueError, match="missing required artifact hashes"):
        scale.verify(registration, prefix)
    assert record_reads == []


@pytest.mark.parametrize("kind", ["source", "runtime", "identities", "schedule", "plan"])
def test_verifier_rejects_changed_registration(collected: tuple[Path, dict[str, Any]], kind: str) -> None:
    prefix, registration = collected
    changed = copy.deepcopy(registration)
    if kind == "source":
        changed["sources"].pop("bench/sqlite_driver.c")
    elif kind in ("runtime", "identities"):
        changed[kind] = {}
    elif kind == "schedule":
        changed[kind][0]["order"].reverse()
    else:
        changed[kind]["load_timeout_seconds"] = 180
    with pytest.raises(ValueError):
        scale.verify(changed, prefix)


@pytest.mark.parametrize(
    "kind",
    [
        "same_wrong_rows",
        "query_schedule",
        "false_complete",
        "missing_queries",
        "disabled_counter",
        "missing_close",
        "sync",
    ],
)
def test_verifier_rejects_wrong_semantics_even_when_engines_agree(
    collected: tuple[Path, dict[str, Any]], kind: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration = collected
    path = Path(f"{prefix}-00.json")
    original = Path.read_text

    def read(target: Path, *args: Any, **kwargs: Any) -> str:
        value = original(target, *args, **kwargs)
        if target != path:
            return value
        record = json.loads(value)
        if kind == "same_wrong_rows":
            for result in record["queries"][0]["results"].values():
                result["rows"] = ["I999;T77726f6e67;"]
        elif kind == "query_schedule":
            record["queries"][0]["sql"] = "SELECT id, name FROM items WHERE id = 999"
        elif kind == "false_complete":
            record["load_elapsed_seconds"] = 121
        elif kind == "missing_queries":
            record["queries"].pop()
        elif kind == "missing_close":
            record["close"] = []
        elif kind == "sync":
            record["sqlite_settings"]["synchronous"]["rows"] = ["I0;"]
        else:
            record["scans"]["full_scan_control"][0] = 0
        return json.dumps(record)

    monkeypatch.setattr(Path, "read_text", read)
    with pytest.raises(ValueError):
        scale.verify(registration, prefix)


def test_collection_failure_preserves_partial_rows_and_inflight_request(monkeypatch: pytest.MonkeyPatch) -> None:
    prefix = _prefix()
    registration = scale.register(prefix, control=True, plan_commit="HEAD")
    original = scale._Worker.execute

    def execute(worker: scale._Worker, sql: str, timeout_seconds: float) -> dict[str, Any]:
        if worker.engine == "sqlite" and sql.startswith("INSERT INTO items VALUES (4,"):
            raise TimeoutError("injected second-engine response timeout")
        return original(worker, sql, timeout_seconds)

    monkeypatch.setattr(scale._Worker, "execute", execute)
    with pytest.raises(ValueError, match="campaign failed"):
        scale.collect(registration, prefix)
    record = json.loads(Path(f"{prefix}-00.json").read_text())
    manifest = json.loads(Path(f"{prefix}.manifest.json").read_text())
    assert record["loaded"] == 3
    assert record["inflight"]["results"]["mini"]["affected"] == 1
    assert "(4," in record["inflight"]["sql"]
    assert len(record["insert_ns"]["mini"]) == 3
    assert manifest["completed"] == 0 and manifest["attempted"] == 1
    assert manifest["failures"]
    assert all(c["exit"] == 0 for c in record["close"])
    assert Path(f"{prefix}-00.mini.db").exists()
    with pytest.raises(ValueError, match="incomplete campaign"):
        scale.verify(registration, prefix)


def test_registration_and_collection_refuse_prefix_reuse(collected: tuple[Path, dict[str, Any]]) -> None:
    prefix, registration = collected
    with pytest.raises(FileExistsError):
        scale.register(prefix, control=True, plan_commit="HEAD")
    with pytest.raises(FileExistsError):
        scale.collect(registration, prefix)


def test_worker_stderr_failure_retains_case_and_manifest(monkeypatch: pytest.MonkeyPatch) -> None:
    prefix = _prefix()
    registration = scale.register(prefix, control=True, plan_commit="HEAD")
    original = scale._Worker.close

    def close(worker: scale._Worker) -> None:
        original(worker)
        Path(str(worker.path).removesuffix(".db") + ".stderr").write_text("injected worker warning\n")

    monkeypatch.setattr(scale._Worker, "close", close)
    with pytest.raises(ValueError, match="campaign failed"):
        scale.collect(registration, prefix)
    record = json.loads(Path(f"{prefix}-00.json").read_text())
    assert "stderr not empty" in record["failure"]
    assert len(record["close"]) == 2
    assert json.loads(Path(f"{prefix}.manifest.json").read_text())["failures"]


def test_load_timeout_retains_smaller_fixture_without_completing_requested_scale(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    prefix = _prefix()
    registration = scale.register(prefix, control=True, plan_commit="HEAD")
    clock = [0.0]
    original = scale._pair

    def pair(
        record: dict[str, Any], workers: list[scale._Worker], sql: str, context: tuple[dict[str, Any], float]
    ) -> None:
        original(record, workers, sql, context)
        if sql.startswith("INSERT INTO items VALUES"):
            clock[0] += 40

    monkeypatch.setattr(scale.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(scale, "_pair", pair)
    scale.collect(registration, prefix)
    record = scale.verify(registration, prefix)[0]
    assert record["loaded"] == 3 and record["case"]["size"] == 24
    assert record["load_status"] == "load_timeout"
    assert record["load_elapsed_seconds"] == 120
    assert not scale.analyze(registration, prefix)["sizes"]["24"]["requested_scope_complete"]
