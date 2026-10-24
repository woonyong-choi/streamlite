from __future__ import annotations

import argparse
import copy
import ctypes
import json
import shutil
import subprocess
import sys
import uuid
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))

import pending_query as campaign  # noqa: E402
from pending_query_analysis import analyze, validate_sample  # noqa: E402
from pending_query_worker import CountingSerializer  # noqa: E402


def _prefix(name: str) -> Path:
    return ROOT / "build" / f"task18-test-{name}-{uuid.uuid4().hex[:8]}"


@pytest.fixture(scope="module")
def collected() -> tuple[Path, dict[str, Any], list[dict[str, Any]]]:
    prefix = _prefix("positive")
    registration = campaign.register(argparse.Namespace(control=True, prefix=prefix, plan_commit=""))
    campaign.collect(registration, prefix)
    records = campaign.verify(registration, prefix)
    print(json.dumps({"positive_prefix": str(prefix)}))
    return prefix, registration, records


def test_collector_matches_all_tasks_and_regenerates(
    collected: tuple[Any, ...],
) -> None:
    _, registration, records = collected

    first = analyze(records, registration)
    second = analyze(records, registration)

    assert first == second
    assert (
        first["formal_records"],
        first["preflight_records"],
        first["diagnostic_records"],
    ) == (12, 12, 12)
    assert first["disposition"] == "control_only"
    assert len(records) == 36
    diagnostic = [record for record in records if record["case"]["diagnostic"]]
    metadata = {
        row["case"]["condition"]: row["sample"]["loads"] for row in diagnostic if row["case"]["query"] == "metadata"
    }
    assert metadata == {"native": 0, "vendor": 9, "compat": 9}
    sequential = next(
        row for row in diagnostic if row["case"]["condition"] == "native" and row["case"]["query"] == "sequential"
    )
    assert sequential["sample"]["copies"]["calls"] == 4
    assert sequential["sample"]["copies"]["info_peak"] > 4
    assert sequential["native"]["pread_bytes"] > 0


@pytest.mark.parametrize(
    "field,value",
    [("digest", "corrupt"), ("rows", 8), ("wall_ns", float("nan")), ("wall_ns", 0)],
)
def test_validation_rejects_corrupt_result(collected: tuple[Any, ...], field: str, value: Any) -> None:
    _, registration, records = collected
    record = copy.deepcopy(records[0])
    record["sample"][field] = value

    with pytest.raises(ValueError):
        validate_sample(record, registration["plan"])


@pytest.mark.parametrize("field", ["digest", "rows", "loads", "loaded_bytes", "python_peak_bytes"])
def test_validation_rejects_diagnostic_mismatch(collected: tuple[Any, ...], field: str) -> None:
    _, registration, records = collected
    record = copy.deepcopy(
        next(row for row in records if row["case"]["diagnostic"] and row["case"]["condition"] == "native")
    )
    record["sample"][field] = "bad" if field == "digest" else 10**12

    with pytest.raises(ValueError):
        validate_sample(record, registration["plan"])


def test_registration_refuses_dirty_formal_and_reused_prefix(
    collected: tuple[Any, ...],
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    prefix, _, _ = collected
    original_git = campaign._git
    with monkeypatch.context() as patch:
        patch.setattr(
            campaign, "_git", lambda args: " M bench/pending_query.py\n" if args[0] == "status" else original_git(args)
        )
        with pytest.raises(ValueError, match="clean"):
            campaign.register(argparse.Namespace(control=False, prefix=_prefix("formal"), plan_commit="HEAD"))
    with pytest.raises(FileExistsError):
        campaign.register(argparse.Namespace(control=True, prefix=prefix, plan_commit=""))


def test_schedule_balances_formal_order_and_pins_full_grid() -> None:
    plan = json.loads(campaign.PLAN.read_text())
    schedule = campaign.cases(plan)
    assert len(schedule) == 1728
    assert sum(case["block"] >= 0 for case in schedule) == 1440
    assert sum(case["block"] == -1 for case in schedule) == 144
    assert sum(case["block"] == -2 for case in schedule) == 144
    for baseline in ("vendor", "compat"):
        before = []
        for block in range(10):
            selected = [
                case["condition"]
                for case in schedule
                if case["backlog"] == 128
                and case["body"] == 256
                and case["page"] == 16
                and case["query"] == "metadata"
                and case["block"] == block
            ]
            before.append(selected.index("native") < selected.index(baseline))
        assert sum(before) == 5


@pytest.mark.parametrize("kind", ["source", "binary", "schedule", "plan", "manifest", "raw"])
def test_verify_rejects_changed_inputs(collected: tuple[Any, ...], kind: str, monkeypatch: pytest.MonkeyPatch) -> None:
    prefix, original, _ = collected
    registration = copy.deepcopy(original)
    if kind in ("source", "binary"):
        hashes = registration["sources" if kind == "source" else "identities"]
        hashes[next(iter(hashes))] = "bad"
    elif kind == "schedule":
        registration["schedule"].reverse()
    elif kind == "plan":
        registration["plan"]["rss_bytes"] += 1
    else:
        original_read = Path.read_text

        def read(path: Path, *args: Any, **kwargs: Any) -> str:
            value = original_read(path, *args, **kwargs)
            if kind == "manifest" and str(path) == f"{prefix}.manifest.json":
                manifest = json.loads(value)
                manifest["unexecuted"] = 1
                return json.dumps(manifest)
            if kind == "raw" and str(path) == f"{prefix}.raw.jsonl":
                return "corrupt"
            return value

        monkeypatch.setattr(Path, "read_text", read)
    with pytest.raises((ValueError, json.JSONDecodeError)):
        campaign.verify(registration, prefix)


@pytest.mark.parametrize("kind", ["raw", "stdout", "stderr", "database", "all", "raw_altered"])
def test_verify_rejects_incomplete_artifact_coverage(
    collected: tuple[Any, ...], kind: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration, _ = collected
    original_read = Path.read_text
    suffixes = {
        "raw": ".raw.jsonl",
        "stdout": "-0000.stdout",
        "stderr": "-0000.stderr",
        "database": "-fixture-9-32-native/data.totedb",
        "raw_altered": ".raw.jsonl",
    }

    def read(path: Path, *args: Any, **kwargs: Any) -> str:
        value = original_read(path, *args, **kwargs)
        if str(path) == f"{prefix}.manifest.json":
            manifest = json.loads(value)
            if kind == "all":
                manifest["artifacts"] = {}
            else:
                del manifest["artifacts"][f"{prefix}{suffixes[kind]}"]
            return json.dumps(manifest)
        if kind == "raw_altered" and str(path) == f"{prefix}.raw.jsonl":
            records = [json.loads(line) for line in value.splitlines()]
            records[0]["sample"]["wall_ns"] *= 2
            return "\n".join(json.dumps(record) for record in records)
        return value

    monkeypatch.setattr(Path, "read_text", read)
    with pytest.raises(ValueError, match="missing artifact hashes"):
        campaign.verify(registration, prefix)


def _patch_json_read(monkeypatch: pytest.MonkeyPatch, path: Path, transform: Callable[[dict[str, Any]], None]) -> None:
    original_read = Path.read_text

    def read(file: Path, *args: Any, **kwargs: Any) -> str:
        value = original_read(file, *args, **kwargs)
        if file == path:
            data = json.loads(value)
            transform(data)
            return json.dumps(data)
        return value

    monkeypatch.setattr(Path, "read_text", read)


@pytest.mark.parametrize(
    "suffix",
    [
        ".registration.json",
        ".fixtures.json",
        "-fixture-9-32-vendor/data.db",
        "-fixture-9-32-native.stdout",
        "-fixture-9-32-vendor.stderr",
        "-fixture-9-32-vendor/data.db-wal",
        "-fixture-9-32-vendor/data.db-shm",
        "-fixture-9-32-native/data.totedb-lock",
        "-0007-native.stdout",
        "-0007-native.stderr",
        "-0035.stdout",
    ],
)
def test_verify_requires_fixture_probe_and_auxiliary_hashes(
    collected: tuple[Any, ...], suffix: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration, _ = collected
    path = f"{prefix}{suffix}"
    assert Path(path).exists()
    _patch_json_read(monkeypatch, Path(f"{prefix}.manifest.json"), lambda data: data["artifacts"].pop(path))

    with pytest.raises(ValueError, match="missing artifact hashes"):
        campaign.verify(registration, prefix)


@pytest.mark.parametrize("kind", ["outside", "relative", "dot", "parent", "neighbor", "manifest"])
def test_verify_rejects_wrong_paths_and_duplicate_aliases(
    collected: tuple[Any, ...], kind: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration, _ = collected
    raw = Path(f"{prefix}.raw.jsonl")
    names = {
        "outside": str(ROOT / "README.md"),
        "relative": str(raw.relative_to(ROOT)),
        "dot": f"{raw.parent}/./{raw.name}",
        "parent": f"{raw.parent}/ignored/../{raw.name}",
        "neighbor": f"{prefix}-neighbor.raw.jsonl",
        "manifest": f"{prefix}.manifest.json",
    }

    def edit(data: dict[str, Any]) -> None:
        data["artifacts"][names[kind]] = data["artifacts"][str(raw)]

    _patch_json_read(monkeypatch, Path(f"{prefix}.manifest.json"), edit)
    with pytest.raises(ValueError, match="invalid artifact path"):
        campaign.verify(registration, prefix)


@pytest.mark.parametrize("digest", ["", "0" * 63, "G" * 64, "A" * 64, None, 42, "0" * 64])
def test_verify_rejects_invalid_or_changed_artifact_hashes(
    collected: tuple[Any, ...], digest: Any, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration, _ = collected

    def edit(data: dict[str, Any]) -> None:
        data["artifacts"][f"{prefix}.raw.jsonl"] = digest

    _patch_json_read(monkeypatch, Path(f"{prefix}.manifest.json"), edit)
    with pytest.raises(ValueError, match="artifact (hash|changed)"):
        campaign.verify(registration, prefix)


@pytest.mark.parametrize(
    "kind", ["manifest_fixtures", "fixture_file", "registration_file", "duplicate_json", "symlink"]
)
def test_verify_rejects_inconsistent_artifact_registration(
    collected: tuple[Any, ...], kind: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration, _ = collected
    manifest_path = Path(f"{prefix}.manifest.json")
    if kind == "manifest_fixtures":
        _patch_json_read(monkeypatch, manifest_path, lambda data: data["fixtures"].clear())
    elif kind == "fixture_file":
        _patch_json_read(monkeypatch, Path(f"{prefix}.fixtures.json"), lambda data: data.clear())
    elif kind == "registration_file":
        _patch_json_read(monkeypatch, Path(f"{prefix}.registration.json"), lambda data: data.update(schema=2))
    elif kind == "duplicate_json":
        original_read = Path.read_text

        def read(path: Path, *args: Any, **kwargs: Any) -> str:
            value = original_read(path, *args, **kwargs)
            return value[:-2] + ', "artifacts": {}}' if path == manifest_path else value

        monkeypatch.setattr(Path, "read_text", read)
    else:
        original_resolve = Path.resolve

        def resolve(path: Path, *args: Any, **kwargs: Any) -> Path:
            return ROOT / "README.md" if str(path) == f"{prefix}.raw.jsonl" else original_resolve(path, *args, **kwargs)

        monkeypatch.setattr(Path, "resolve", resolve)

    with pytest.raises(ValueError, match="fixture|registration file|duplicate JSON|invalid artifact path"):
        campaign.verify(registration, prefix)


def test_verify_ignores_later_analysis_output(collected: tuple[Any, ...]) -> None:
    prefix, registration, records = collected
    output = Path(f"{prefix}.analysis-{uuid.uuid4().hex[:8]}.json")
    output.write_text(json.dumps(analyze(records, registration)))

    assert campaign.verify(registration, prefix) == records


def test_timeout_retains_failure_evidence(monkeypatch: pytest.MonkeyPatch) -> None:
    prefix = _prefix("timeout")
    registration = campaign.register(argparse.Namespace(control=True, prefix=prefix, plan_commit=""))
    original = campaign._CampaignDeadline
    monkeypatch.setattr(campaign, "_CampaignDeadline", lambda _: original(0.001))

    with pytest.raises(ValueError, match="campaign failed"):
        campaign.collect(registration, prefix)

    manifest = json.loads(Path(f"{prefix}.manifest.json").read_text())
    assert manifest["failures"]
    assert manifest["completed"] == 0
    assert Path(f"{prefix}.raw.jsonl").exists()
    with pytest.raises(ValueError):
        campaign.verify(registration, prefix)


def test_persistence_timeout_keeps_completed_case_and_stops(monkeypatch: pytest.MonkeyPatch) -> None:
    prefix = _prefix("persist-timeout")
    registration = campaign.register(argparse.Namespace(control=True, prefix=prefix, plan_commit=""))

    class AfterPersistenceDeadline(campaign._CampaignDeadline):
        def remaining_seconds(self, stage: str) -> float:
            if stage == "raw persistence":
                raise TimeoutError("injected timeout after durable raw persistence")
            return super().remaining_seconds(stage)

    monkeypatch.setattr(campaign, "_CampaignDeadline", AfterPersistenceDeadline)
    with pytest.raises(ValueError, match="campaign failed"):
        campaign.collect(registration, prefix)

    manifest = json.loads(Path(f"{prefix}.manifest.json").read_text())
    assert (manifest["attempted"], manifest["completed"], manifest["failed_cases"], manifest["unexecuted"]) == (
        1,
        1,
        0,
        35,
    )
    assert len(Path(f"{prefix}.raw.jsonl").read_text().splitlines()) == 1
    assert "durable raw persistence" in manifest["failures"][0]["error"]
    with pytest.raises(ValueError):
        campaign.verify(registration, prefix)


@pytest.fixture
def native_queue(collected: tuple[Any, ...]) -> Any:
    from totedb import Database, Queue

    prefix, _, _ = collected
    fixture = Path(f"{prefix}-fixture-9-32-native/data.totedb")
    path = _prefix("native-copy").with_suffix(".totedb")
    shutil.copyfile(fixture, path)
    db = Database(path)
    queue = Queue(db, "ack_queue", install=False, clock=lambda: 1000)
    yield queue, db
    queue.close()
    db.close()


def test_lists_preserve_mixed_states_without_pending_filter(native_queue: Any) -> None:
    from totedb.persistqueue_compat import _status_code

    queue, _ = native_queue
    leased = queue.get(1000, item_id=2)
    acked = queue.get(1000, item_id=3)
    failed = queue.get(1000, item_id=4)
    retried = queue.get(1000, item_id=5)
    queue.ack(acked)
    queue.fail(failed, "retained failure")
    queue.nack(retried)

    rows = list(queue.iter_items(2, with_payload=True))

    assert leased is not None
    assert [row.id for row in rows] == list(range(1, 10))
    assert [_status_code(row) for row in rows[:5]] == ["0", "2", "5", "9", "1"]
    assert len([row for row in rows if row.state.value == 0]) == 6


def test_page_transition_is_live_and_early_stop_keeps_owned_payload(
    native_queue: Any,
) -> None:
    queue, db = native_queue
    serializer = CountingSerializer()
    iterator = queue.iter_items(2, with_payload=True)
    held = next(iterator)
    queue.update(2, serializer.dumps({"id": 2, "body": "changed"}))
    queue.update(3, serializer.dumps({"id": 3, "body": "changed"}))
    queue.put(serializer.dumps({"id": 10, "body": "new"}))
    second, third = next(iterator), next(iterator)

    assert serializer.loads(second.payload)["body"] == "x" * 32
    assert serializer.loads(third.payload)["body"] == "changed"
    assert [row.id for row in iterator] == list(range(4, 11))
    iterator.close()
    early = queue.iter_items(2, with_payload=True)
    early_held = next(early)
    early.close()
    queue.close()
    db.close()
    assert serializer.loads(held.payload) == {"id": 1, "body": "x" * 32}
    assert early_held.payload == held.payload


def test_list_limits_and_conversion_failure_release_native_payload(
    native_queue: Any, monkeypatch: pytest.MonkeyPatch
) -> None:
    import totedb.queue as module
    from totedb import InvalidArgumentError, LimitError
    from totedb._native import ListItem

    queue, _ = native_queue
    assert queue.list_items(after_id=9) == []
    assert len(queue.list_items(limit=1025)) == 9
    with pytest.raises(InvalidArgumentError):
        queue.list_items(limit=0)
    with pytest.raises(ValueError):
        queue.list_items(limit=-1)
    with pytest.raises(LimitError):
        queue.put(b"x" * (1048576 + 1))
    count = ctypes.c_size_t()
    items = (ListItem * 1025)()
    assert module._lib.tote_queue_list(queue.handle, None, 0, 1025, 1, items, ctypes.byref(count)) != 0
    assert count.value == 0
    original_free = module._lib.tote_queue_list_free
    freed = []

    def free(values: Any, size: int) -> Any:
        freed.append(size)
        return original_free(values, size)

    def refuse(*args: Any, **kwargs: Any) -> Any:
        raise MemoryError("injected conversion failure")

    with monkeypatch.context() as patch:
        patch.setattr(module._lib, "tote_queue_list_free", free)
        patch.setattr(module, "_to_info", refuse)
        with pytest.raises(MemoryError):
            queue.list_items(limit=4, with_payload=True)
    assert freed == [4]
    assert len(queue.list_items(limit=4, with_payload=True)) == 4


def test_native_cancel_reports_no_partial_success(collected: tuple[Any, ...]) -> None:
    prefix, registration, _ = collected
    command = [
        str(ROOT / registration["plan"]["driver"]),
        f"{prefix}-fixture-9-32-native/data.totedb",
        "4",
        "1",
        "1",
        "33554432",
        "1",
    ]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    observation = json.loads(result.stdout)

    assert observation["status"] == 16
    assert observation["rows"] == 0
    assert observation["payload_bytes"] == 0
    assert observation["baseline_bytes"] == observation["released_bytes"]
    assert result.stderr == ""


@pytest.mark.parametrize("condition", ["vendor", "compat", "native"])
def test_serializer_oom_retains_error_without_success(collected: tuple[Any, ...], condition: str) -> None:
    prefix, registration, _ = collected
    engine = "vendor" if condition == "vendor" else "native"
    settings = {
        "condition": condition,
        "path": f"{prefix}-fixture-9-32-{engine}",
        "backlog": 9,
        "body": 32,
        "page": 4,
        "query": "payload",
        "diagnostic": True,
        "fail_at": 2,
    }
    output = _prefix(f"serializer-oom-{condition}")
    deadline = campaign._CampaignDeadline(10)
    with pytest.raises(ValueError, match="worker exit"):
        campaign._worker(registration, settings, ("query", output, deadline))
    assert Path(f"{output}.stdout").read_text() == ""
    assert "MemoryError: injected serializer allocation failure" in Path(f"{output}.stderr").read_text()


def test_native_fault_sweep_discards_partial_payloads(
    collected: tuple[Any, ...],
) -> None:
    prefix, _, _ = collected
    result = subprocess.run(
        [
            str(ROOT / "build-regression/test_pending_query_native"),
            f"{prefix}-fixture-9-32-native/data.totedb",
        ],
        capture_output=True,
        text=True,
        check=True,
        env={**campaign.os.environ, "UBSAN_OPTIONS": "halt_on_error=1"},
    )
    observation = json.loads(result.stdout)
    assert observation["payload_failures"] == 4
    assert observation["reserve_failures"] > 0
    assert observation["released"] is True
    assert result.stderr == ""


def test_iterator_second_page_failure_keeps_prior_owned_results(
    native_queue: Any, monkeypatch: pytest.MonkeyPatch
) -> None:
    import totedb.queue as module

    queue, _ = native_queue
    iterator = queue.iter_items(2, with_payload=True)
    first, second = next(iterator), next(iterator)
    original = module._lib.tote_queue_list_free
    freed = []

    def free(items: Any, count: int) -> Any:
        freed.append(count)
        return original(items, count)

    def refuse(*args: Any, **kwargs: Any) -> Any:
        raise MemoryError("second page conversion failure")

    with monkeypatch.context() as patch:
        patch.setattr(module._lib, "tote_queue_list_free", free)
        patch.setattr(module, "_to_info", refuse)
        with pytest.raises(MemoryError):
            next(iterator)
    assert freed == [2]
    assert (first.id, second.id) == (1, 2)
    assert CountingSerializer().loads(first.payload) == {"id": 1, "body": "x" * 32}
    assert len(queue.list_items(limit=4, with_payload=True)) == 4
