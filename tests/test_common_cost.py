from __future__ import annotations

import argparse
import copy
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))

from common_cost import cases, collect, register, verify  # noqa: E402
from common_cost_analysis import analyze, validate_native  # noqa: E402


@pytest.fixture
def registration() -> dict[str, Any]:
    return register(
        argparse.Namespace(
            control=True, phase="cache", plan_commit="HEAD", off_build=ROOT / "build", on_build=ROOT / "build-benchmark"
        )
    )


def test_schedule_retains_four_conditions_and_balances_each_contrast() -> None:
    plan = json.loads((ROOT / "bench/common_cost_plan.json").read_text())
    schedule = cases(plan, "cache")
    assert len(schedule) == 576
    for condition in plan["conditions"]:
        if condition == "00":
            continue
        before = []
        for block in range(10):
            entries = [
                case
                for case in schedule
                if (case["pattern"], case["payload"], case["group"], case["block"]) == ("hit", 256, 1, block)
            ]
            order = [entry["condition"] for entry in entries]
            before.append(order.index(condition) < order.index("00"))
        assert sum(before) == 5
    assert sum(case["block"] == -1 for case in schedule) == 48
    assert sum(case["instrumented"] for case in schedule) == 48


def test_native_collection_matches_sqlite_and_regenerates(tmp_path: Path, registration: dict[str, Any]) -> None:
    prefix = tmp_path / "positive"

    collect(registration, prefix)
    records = verify(registration, prefix)

    assert len(records) == 72
    first = json.dumps(analyze(records, registration["plan"]), sort_keys=True, allow_nan=False)
    second = json.dumps(analyze(records, registration["plan"]), sort_keys=True, allow_nan=False)
    assert first == second
    assert all(record["sample"]["released_bytes"] == 0 for record in records)
    assert any(record["sample"]["cache_hits"] for record in records)
    assert any(record["sample"]["buffered_writes"] for record in records)
    diagnostic = copy.deepcopy(next(record for record in records if record["case"]["instrumented"]))
    diagnostic["sample"]["diagnostics"]["dropped"] = 1
    with pytest.raises(ValueError, match="incomplete instrumentation"):
        validate_native(diagnostic["sample"], diagnostic["case"], registration["plan"])
    with pytest.raises(ValueError, match="output exists"):
        collect(registration, prefix)
    Path(f"{prefix}.raw.jsonl").write_text("{}\n")
    with pytest.raises(ValueError, match="artifact changed"):
        verify(registration, prefix)


def test_native_wrong_effect_and_missing_measurements_are_rejected(
    tmp_path: Path, registration: dict[str, Any]
) -> None:
    driver = ROOT / "build/common_cost_driver"
    database = tmp_path / "dropped.db"
    common = [str(database), "16", "8", "4", "32", "hit", "1", "1"]
    subprocess.run([str(driver), "fixture", *common, "0"], check=True, capture_output=True)
    process = subprocess.run([str(driver), "run", *common, "1"], check=True, capture_output=True, text=True)
    sample = json.loads(process.stdout)
    sample["drop_first"] = 0
    case = {"group": 4, "payload": 32, "pattern": "hit", "condition": "11", "instrumented": 0}

    with pytest.raises(ValueError, match="oracle mismatch"):
        validate_native(sample, case, registration["plan"])
    del sample["peak_rss_bytes"]
    with pytest.raises(ValueError, match="missing native metrics"):
        validate_native(sample, case, registration["plan"])


def test_failure_stops_without_replacing_partial_data(tmp_path: Path, registration: dict[str, Any]) -> None:
    registration["cases"][1]["condition"] = "invalid"
    prefix = tmp_path / "failure"

    with pytest.raises(ValueError, match="stopped"):
        collect(registration, prefix)

    manifest = json.loads(Path(f"{prefix}.manifest.json").read_text())
    assert manifest["completed"] == 1
    assert len(manifest["failures"]) == 1
    assert Path(f"{prefix}-0001.db").exists()
    assert Path(f"{prefix}-0001.stderr").exists()
    with pytest.raises(ValueError, match="incomplete campaign"):
        verify(registration, prefix)


def test_registration_binary_change_is_rejected_before_collection(tmp_path: Path, registration: dict[str, Any]) -> None:
    registration["builds"]["off"]["driver_sha256"] = "changed"
    prefix = tmp_path / "changed"

    with pytest.raises(ValueError, match="binary changed"):
        collect(registration, prefix)

    assert not Path(f"{prefix}.raw.jsonl").exists()


def test_formal_collection_rejects_reduced_control_plan(tmp_path: Path, registration: dict[str, Any]) -> None:
    registration["control"] = False

    with pytest.raises(ValueError, match="registered plan or schedule"):
        collect(registration, tmp_path / "changed-plan")


def test_collection_preserves_existing_artifacts_with_overlapping_prefix(
    tmp_path: Path, registration: dict[str, Any]
) -> None:
    original = tmp_path / "overlap-0000-unrelated.db"
    original.write_bytes(b"preserve existing data")

    with pytest.raises(ValueError, match="prefix overlaps"):
        collect(registration, tmp_path / "overlap")

    assert original.read_bytes() == b"preserve existing data"
    assert not Path(f"{original}.gz").exists()


def test_analysis_preserves_pairs_and_degenerate_uncertainty(registration: dict[str, Any]) -> None:
    plan = {**registration["plan"], "patterns": ["hit"], "groups": [1], "formal_blocks": 10}
    records = []
    for block in range(10):
        for condition in plan["conditions"]:
            factor = 1 + (block + int(condition, 2)) / 100
            sample = {
                "completed": 8,
                "wall_ns": 1e6 * factor,
                "latencies_ns": [1000 * factor],
                "cpu_ns": 1e5 * factor,
                "peak_rss_bytes": 1000000 + block * 100 + int(condition, 2),
            }
            records.append(
                {
                    "case": {
                        "block": block,
                        "condition": condition,
                        "instrumented": 0,
                        "pattern": "hit",
                        "payload": 32,
                        "group": 1,
                    },
                    "sample": sample,
                    "storage_bytes": 1000,
                }
            )
    result = analyze(records, plan)
    assert result == analyze(copy.deepcopy(records), plan)
    assert result["cells"]["hit-32-1"]["status"] == "complete"
    records.append(records[-1])
    assert analyze(records, plan)["cells"]["hit-32-1"]["status"] == "incomplete"


@pytest.mark.parametrize("scenario", ["sql", "queue", "conditional"])
@pytest.mark.parametrize("condition", ["eager", "lazy"])
@pytest.mark.parametrize("build_name", ["build", "build-benchmark"])
def test_python_lifecycle_paths_preserve_exact_reopen(
    tmp_path: Path, scenario: str, condition: str, build_name: str
) -> None:
    import os

    command = [
        sys.executable,
        str(ROOT / "bench/common_cost_worker.py"),
        "--prefix",
        str(tmp_path / "lifecycle"),
        "--scenario",
        scenario,
        "--condition",
        condition,
        "--instances",
        "2",
        "--operations",
        "8",
        "--payload",
        "32",
    ]
    ext = "dylib" if sys.platform == "darwin" else "so"
    env = {
        **os.environ,
        "PYTHONPATH": str(ROOT / "python"),
        "TOTEDB_LIB": str(ROOT / build_name / f"lib/libtotedb.{ext}"),
    }
    result = subprocess.run(command, capture_output=True, text=True, env=env, check=True)
    sample = json.loads(result.stdout)
    assert sample["completed"] == (8 if scenario == "conditional" else 2)
    assert sample["scenario"] == scenario
    assert (sample["diagnostics"] is None) == (build_name == "build")


def test_formal_registration_refuses_dirty_source_and_conditional_without_candidate(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    import common_cost

    original = common_cost._run
    monkeypatch.setattr(
        common_cost,
        "_run",
        lambda command, **kwargs: (
            " M src/sql/database.c" if command[:3] == ["git", "status", "--porcelain"] else original(command, **kwargs)
        ),
    )
    args = argparse.Namespace(
        control=False, phase="cache", plan_commit="HEAD", off_build=ROOT / "build", on_build=ROOT / "build-benchmark"
    )
    with pytest.raises(ValueError, match="clean source"):
        register(args)
    args.control, args.phase = True, "conditional"
    args.candidate_build, args.eligibility = None, None
    with pytest.raises(ValueError, match="conditional path unsupported"):
        register(args)
