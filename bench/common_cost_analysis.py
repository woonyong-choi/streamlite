from __future__ import annotations

import math
import hashlib
import json
import sqlite3
import statistics
import subprocess
import warnings
from functools import lru_cache
from pathlib import Path
from types import ModuleType
from typing import Any

import numpy as np
import scipy
from scipy.stats import bootstrap

__all__ = [
    "analyze",
    "analyze_python",
    "analyze_registered",
    "expected_digest",
    "python_dimensions",
    "sample_counts",
    "verify_analysis_sources",
    "validate_diagnostics",
    "validate_native",
]

METRICS = ("throughput", "p99_ns", "cpu_ns", "peak_rss_bytes")
CONTRASTS = (("00", "10"), ("01", "11"), ("00", "01"), ("10", "11"))
ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=16)
def expected_digest(rows: int, operations: int, payload: int) -> str:
    db = sqlite3.connect(":memory:")
    try:
        db.execute("CREATE TABLE counters (id INTEGER PRIMARY KEY, counter INTEGER, payload TEXT)")
        db.executemany("INSERT INTO counters VALUES (?,0,?)", ((i + 1, "x" * payload) for i in range(rows)))
        db.executemany(
            "UPDATE counters SET counter=counter+? WHERE id=?",
            ((1, (i * 17 + 257) % rows + 1) for i in range(operations)),
        )
        digest = 14695981039346656037
        for key, count, body in db.execute("SELECT id,counter,payload FROM counters ORDER BY id"):
            encoded = key.to_bytes(8, "little") + count.to_bytes(8, "little") + body.encode()
            for value in encoded:
                digest = ((digest ^ value) * 1099511628211) & ((1 << 64) - 1)
        return f"{digest:016x}"
    finally:
        db.close()


def validate_native(sample: dict[str, Any], case: dict[str, Any], plan: dict[str, Any]) -> None:
    required = {
        "wall_ns",
        "cpu_ns",
        "active_ns",
        "open_ns",
        "close_ns",
        "reopen_verify_ns",
        "peak_rss_bytes",
        "baseline_rss_bytes",
        "ledger_peak_bytes",
        "workspace_peak_bytes",
        "buffered_writes",
        "workspace_flushed_images",
        "work_stats",
        "completion_ns",
        "completion_cpu_ns",
        "audit_ns",
        "audit_cpu_ns",
        "catalog_clones",
        "cache_hits",
        "cache_misses",
        "cache_resets",
        "cache_evictions",
        "released_bytes",
        "refusals",
        "latencies_ns",
        "diagnostics",
        "digest",
        "completed",
        "works",
        "drop_first",
        "instrumented",
        "cache",
        "workspace",
    }
    missing = required - sample.keys()
    if missing:
        raise ValueError(f"missing native metrics: {sorted(missing)}")
    if sample["schema"] != 1 or sample["drop_first"]:
        raise ValueError("unsupported schema or injected control in outcomes")
    if sample["completed"] != plan["operations"] or sample["works"] != plan["operations"] // case["group"]:
        raise ValueError("incomplete operation or commit count")
    expected = expected_digest(plan["rows"], plan["operations"], case["payload"])
    if sample["digest"] != expected:
        raise ValueError("sqlite final-state oracle mismatch")
    if (sample["cache"], sample["workspace"]) != tuple(map(int, case["condition"])):
        raise ValueError("condition mismatch")
    if sample["instrumented"] != case["instrumented"]:
        raise ValueError("instrumentation mismatch")
    if sample["released_bytes"] or sample["refusals"] or sample["cache_resets"]:
        raise ValueError("ownership, refusal or reset gate failed")
    if sample["ledger_peak_bytes"] > plan["shared_requested_bytes"]:
        raise ValueError("shared memory gate failed")
    if sample["peak_rss_bytes"] > plan["worker_peak_rss_bytes"]:
        raise ValueError("whole-worker RSS gate failed")
    latencies = sample["latencies_ns"]
    if len(latencies) != sample["works"] or any(value <= 0 for value in latencies):
        raise ValueError("incomplete or invalid latency samples")
    work_stats = sample["work_stats"]
    if len(work_stats) != sample["works"] or any(work["outcome"] != "APPLIED" for work in work_stats):
        raise ValueError("missing per-work outcomes")
    for name in ("buffered_writes", "workspace_flushed_images", "catalog_clones"):
        if sum(work[name] for work in work_stats) != sample[name]:
            raise ValueError("per-work statistics do not match aggregate")
    if min(sample[name] for name in ("active_ns", "wall_ns", "cpu_ns")) <= 0:
        raise ValueError("missing positive timing or write control")
    if min(sample["completion_ns"], sample["completion_cpu_ns"]) <= 0:
        raise ValueError("missing application completion cost")
    if sample["completion_ns"] + sample["audit_ns"] != sample["wall_ns"]:
        raise ValueError("incomplete wall-time accounting")
    if sample["completion_cpu_ns"] + sample["audit_cpu_ns"] != sample["cpu_ns"]:
        raise ValueError("incomplete CPU accounting")
    validate_diagnostics(sample)
    if sample["cache"] and case["pattern"] == "hit" and plan["operations"] > 2:
        if sample["cache_hits"] < plan["operations"] - 2:
            raise ValueError("cache hit positive control failed")
    if sample["cache"] and case["pattern"] != "hit":
        if sample["cache_hits"] != 0 or sample["cache_misses"] != plan["operations"]:
            raise ValueError("cache miss negative control failed")
    if sample["workspace"] and sample["buffered_writes"] == 0:
        raise ValueError("workspace positive control failed")


def sample_counts(records: list[dict[str, Any]], plan: dict[str, Any]) -> dict[str, int]:
    counts = {"preflight_records": 0, "formal_records": 0, "diagnostic_records": 0}
    for record in records:
        case = record["case"]
        block, instrumented = case["block"], case["instrumented"]
        if type(block) is not int or type(instrumented) is not int or instrumented not in (0, 1):
            raise ValueError("invalid sample classification types")
        if block == -1 and not instrumented:
            counts["preflight_records"] += 1
        elif 0 <= block < plan["formal_blocks"] and not instrumented:
            counts["formal_records"] += 1
        elif block == -2 and instrumented:
            counts["diagnostic_records"] += 1
        else:
            raise ValueError(f"invalid sample classification: block={block}, instrumented={instrumented}")
    return {**counts, "instrumented_records": counts["diagnostic_records"]}


def verify_analysis_sources(
    registration: dict[str, Any], *, source_commit: str | None = None
) -> tuple[str | None, dict[str, bytes]]:
    names = sorted(
        str(p.relative_to(ROOT)) for p in (ROOT / "bench").glob("common_cost*") if p.suffix in {".c", ".py", ".json"}
    )
    if set(registration["criteria_hashes"]) != set(names):
        raise ValueError("registered criteria source set differs")
    revision: str | None = None
    if source_commit is not None:
        revision = (
            _git_output(["rev-parse", "--verify", "--end-of-options", f"{source_commit}^{{commit}}"]).decode().strip()
        )
        _git_output(["merge-base", "--is-ancestor", revision, "HEAD"])
    sources = {}
    for name in names:
        source = (ROOT / name).read_bytes() if revision is None else _git_output(["show", f"{revision}:{name}"])
        if hashlib.sha256(source).hexdigest() != registration["criteria_hashes"][name]:
            raise ValueError(f"analysis input changed since registration: {name}")
        sources[name] = source
    return revision, sources


def analyze_registered(
    records: list[dict[str, Any]],
    registration: dict[str, Any],
    *,
    source_commit: str | None = None,
    correct_metadata: bool = False,
) -> dict[str, Any]:
    if correct_metadata and source_commit is None:
        raise ValueError("metadata correction requires an explicit source commit")
    revision, sources = verify_analysis_sources(registration, source_commit=source_commit)
    if revision is None:
        native, python = analyze, analyze_python
    else:
        module = ModuleType("registered_common_cost_analysis")
        module.__file__ = str(ROOT / "bench/common_cost_analysis.py")
        code = compile(sources["bench/common_cost_analysis.py"], f"{revision}:bench/common_cost_analysis.py", "exec")
        exec(code, module.__dict__)
        native, python = module.analyze, module.analyze_python
    counts = sample_counts(records, registration["plan"])
    summary = (
        native(records, registration["plan"])
        if registration["phase"] == "cache"
        else python(records, registration["plan"], registration["phase"])
    )
    summary["control"] = registration["control"]
    if correct_metadata:
        summary = {
            **summary,
            "schema": 2,
            **counts,
            "metadata_correction": {
                "source_commit": revision,
                "registered_analysis_sha256": registration["criteria_hashes"]["bench/common_cost_analysis.py"],
                "registration_sha256": hashlib.sha256(json.dumps(registration, sort_keys=True).encode()).hexdigest(),
                "processor_hashes": {
                    name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                    for name in ("bench/common_cost.py", "bench/common_cost_analysis.py")
                },
            },
        }
    return summary


def analyze(records: list[dict[str, Any]], plan: dict[str, Any]) -> dict[str, Any]:
    if np.__version__ != plan["numpy_version"] or scipy.__version__ != plan["scipy_version"]:
        raise ValueError("analysis dependency version mismatch")
    counts = sample_counts(records, plan)
    formal = [record for record in records if record["case"]["block"] >= 0 and not record["case"]["instrumented"]]
    summaries: dict[str, Any] = {}
    for pattern in plan["patterns"]:
        for payload in plan["payload_bytes"]:
            for group in plan["groups"]:
                key = f"{pattern}-{payload}-{group}"
                cells = [
                    record
                    for record in formal
                    if (record["case"]["pattern"], record["case"]["payload"], record["case"]["group"])
                    == (pattern, payload, group)
                ]
                summaries[key] = _analyze_cell(cells, plan)
    return {
        "schema": 2,
        "formal_blocks": plan["formal_blocks"],
        "workers": [{"case": record["case"], **_metrics(record)} for record in records],
        "cells": summaries,
        "decision": "incomplete" if any(s["status"] != "complete" for s in summaries.values()) else "review",
        **counts,
        "diagnostic_overhead": _diagnostic_overhead(records),
        "hypotheses": _hypotheses(summaries, plan),
    }


def _diagnostic_overhead(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    indexed = {
        (r["case"]["pattern"], r["case"]["payload"], r["case"]["group"], r["case"]["condition"]): r
        for r in records
        if r["case"]["block"] == -1
    }
    comparisons = []
    for record in records:
        if not record["case"]["instrumented"]:
            continue
        case = record["case"]
        key = case["pattern"], case["payload"], case["group"], case["condition"]
        if key in indexed:
            baseline = indexed[key]["sample"]
            comparisons.append(
                {
                    "cell": key,
                    "wall_ratio": record["sample"]["wall_ns"] / baseline["wall_ns"],
                    "cpu_ratio": record["sample"]["cpu_ns"] / baseline["cpu_ns"],
                    "inference": "one diagnostic/preflight pair; descriptive only",
                }
            )
    return comparisons


def _hypotheses(cells: dict[str, Any], plan: dict[str, Any]) -> dict[str, str]:
    if any(cell["status"] != "complete" for cell in cells.values()):
        return {"cache": "incomplete", "workspace": "incomplete"}
    results = {}
    for name, contrasts, primary in (
        ("cache", ("00->10", "01->11"), "hit-256-1"),
        ("workspace", ("00->01", "10->11"), "hit-256-32"),
    ):
        if primary not in cells:
            results[name] = "control-only"
            continue
        eligible = True
        for key, cell in cells.items():
            for contrast in contrasts:
                comparison = cell["contrasts"][contrast]
                intervals = comparison["metrics"]
                complete = all(value["status"] == "complete" for value in intervals.values())
                if not complete:
                    eligible = False
                    continue
                eligible &= intervals["throughput"]["low"] >= (
                    plan["minimum_throughput_ratio"] if key == primary else 0.95
                )
                eligible &= intervals["p99_ns"]["high"] <= plan["maximum_tail_ratio"]
                eligible &= intervals["cpu_ns"]["high"] <= plan["maximum_cpu_ratio"]
                eligible &= max(comparison["rss_deltas"]) <= plan["maximum_rss_extra_bytes"]
                eligible &= max(comparison["storage_ratios"]) <= plan["maximum_storage_ratio"]
        results[name] = "eligible-for-review" if eligible else "hold"
    return results


def analyze_python(records: list[dict[str, Any]], plan: dict[str, Any], phase: str) -> dict[str, Any]:
    if np.__version__ != plan["numpy_version"] or scipy.__version__ != plan["scipy_version"]:
        raise ValueError("analysis dependency version mismatch")
    counts = sample_counts(records, plan)
    conditions = ["eager", "lazy"] if phase == "lifecycle" else ["baseline", "candidate"]
    dimensions = python_dimensions(plan, phase)
    adjusted = {**plan, "conditions": conditions, "family_intervals": len(dimensions) * 4}
    summaries = {}
    for scenario, payload in dimensions:
        matching = [
            record
            for record in records
            if record["case"]["block"] >= 0
            and (record["case"]["scenario"], record["case"]["payload"]) == (scenario, payload)
        ]
        summaries[f"{scenario}-{payload}"] = _analyze_cell(matching, adjusted, ((conditions[0], conditions[1]),))
    return {
        "schema": 2,
        **counts,
        "phase": phase,
        "workers": [{"case": record["case"], **_metrics(record)} for record in records],
        "cells": summaries,
        "hypothesis": _python_hypothesis(summaries, plan, phase),
        "decision": "incomplete" if any(cell["status"] != "complete" for cell in summaries.values()) else "review",
    }


def _python_hypothesis(cells: dict[str, Any], plan: dict[str, Any], phase: str) -> str:
    primary = "sql-0" if phase == "lifecycle" else "conditional-256"
    eligible = True
    for key, cell in cells.items():
        if cell["status"] != "complete":
            return "incomplete"
        comparison = next(iter(cell["contrasts"].values()))
        metrics = comparison["metrics"]
        if any(value["status"] != "complete" for value in metrics.values()):
            eligible = False
            continue
        eligible &= metrics["throughput"]["low"] >= (plan["minimum_throughput_ratio"] if key == primary else 0.95)
        eligible &= metrics["p99_ns"]["high"] <= plan["maximum_tail_ratio"]
        eligible &= metrics["cpu_ns"]["high"] <= plan["maximum_cpu_ratio"]
        eligible &= max(comparison["rss_deltas"]) <= plan["maximum_rss_extra_bytes"]
        eligible &= max(comparison["storage_ratios"]) <= plan["maximum_storage_ratio"]
    return "eligible-for-review" if eligible else "hold"


def python_dimensions(plan: dict[str, Any], phase: str) -> list[tuple[str, int]]:
    if phase == "lifecycle":
        return [("sql", 0), *(("queue", payload) for payload in plan["payload_bytes"])]
    return [("conditional", payload) for payload in plan["payload_bytes"]]


def _git_output(arguments: list[str]) -> bytes:
    return subprocess.run(["git", *arguments], cwd=ROOT, capture_output=True, check=True).stdout


def validate_diagnostics(sample: dict[str, Any]) -> None:
    diagnostics = sample["diagnostics"]
    if not sample["instrumented"]:
        if diagnostics is not None:
            raise ValueError("OFF sample contains ON diagnostics")
        return
    if not isinstance(diagnostics, dict) or diagnostics["records"] <= 0:
        raise ValueError("diagnostic positive control missing")
    if any(diagnostics[name] for name in ("incomplete", "overflow", "dropped")):
        raise ValueError("incomplete instrumentation")
    phases = diagnostics["phases"]
    if len(phases) != 12 or phases[9]["attempts"] <= 0 or phases[10]["attempts"] <= 0:
        raise ValueError("I/O positive control missing")
    if any(phase["errors"] or phase["short_io"] for phase in phases):
        raise ValueError("I/O failure in diagnostic sample")


def _metrics(record: dict[str, Any]) -> dict[str, float]:
    sample = record["sample"]
    values = sorted(sample["latencies_ns"])
    return {
        "throughput": sample["completed"] * 1e9 / sample.get("completion_ns", sample["wall_ns"]),
        "p99_ns": values[math.ceil(len(values) * 0.99) - 1],
        "p50_ns": values[math.ceil(len(values) * 0.50) - 1],
        "p95_ns": values[math.ceil(len(values) * 0.95) - 1],
        "maximum_ns": values[-1],
        "cpu_ns": sample.get("completion_cpu_ns", sample["cpu_ns"]),
        "peak_rss_bytes": sample["peak_rss_bytes"],
    }


def _interval(values: list[float], plan: dict[str, Any]) -> dict[str, Any]:
    data = np.asarray(values)
    if len(set(values)) == 1:
        return {"median": values[0], "low": None, "high": None, "status": "degenerate"}
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        result = bootstrap(
            (data,),
            np.median,
            method="BCa",
            paired=True,
            n_resamples=plan["bootstrap_resamples"],
            random_state=plan["seed"],
            confidence_level=1 - plan["alpha"] / plan["family_intervals"],
        )
        nominal = bootstrap(
            (data,),
            np.median,
            method="BCa",
            paired=True,
            n_resamples=0,
            random_state=plan["seed"],
            confidence_level=0.95,
            bootstrap_result=result,
        )
    low, high = float(result.confidence_interval.low), float(result.confidence_interval.high)
    valid = not caught and math.isfinite(low) and math.isfinite(high)
    return {
        "median": statistics.median(values),
        "low": low if valid else None,
        "high": high if valid else None,
        "status": "complete" if valid else "degenerate",
        "nominal95": [float(nominal.confidence_interval.low), float(nominal.confidence_interval.high)]
        if valid
        else None,
    }


def _analyze_cell(
    records: list[dict[str, Any]], plan: dict[str, Any], contrasts_to_use: tuple[tuple[str, str], ...] = CONTRASTS
) -> dict[str, Any]:
    indexed = {(r["case"]["block"], r["case"]["condition"]): r for r in records}
    expected = {(block, condition) for block in range(plan["formal_blocks"]) for condition in plan["conditions"]}
    if set(indexed) != expected or len(records) != len(expected):
        return {"status": "incomplete", "actual": len(records), "expected": len(expected)}
    contrasts = {}
    for base, candidate in contrasts_to_use:
        metrics: dict[str, list[float]] = {name: [] for name in METRICS}
        rss_deltas, storage_ratios = [], []
        for block in range(plan["formal_blocks"]):
            reference, comparison = indexed[block, base], indexed[block, candidate]
            a, b = _metrics(reference), _metrics(comparison)
            for metric in METRICS:
                metrics[metric].append(b[metric] / a[metric])
            rss_deltas.append(b["peak_rss_bytes"] - a["peak_rss_bytes"])
            storage_ratios.append(comparison["storage_bytes"] / reference["storage_bytes"])
        intervals = {metric: {"ratios": values, **_interval(values, plan)} for metric, values in metrics.items()}
        resource_ok = max(rss_deltas) <= plan["maximum_rss_extra_bytes"]
        resource_ok &= max(storage_ratios) <= plan["maximum_storage_ratio"]
        complete = all(interval["status"] == "complete" for interval in intervals.values())
        admitted = complete and resource_ok and intervals["throughput"]["low"] >= plan["minimum_throughput_ratio"]
        admitted &= complete and intervals["p99_ns"]["high"] <= plan["maximum_tail_ratio"]
        admitted &= complete and intervals["cpu_ns"]["high"] <= plan["maximum_cpu_ratio"]
        contrasts[f"{base}->{candidate}"] = {
            "metrics": intervals,
            "rss_deltas": rss_deltas,
            "storage_ratios": storage_ratios,
            "decision": "eligible" if admitted else "hold",
        }
    return {"status": "complete", "contrasts": contrasts}
