from __future__ import annotations

__all__ = ["validate_sample", "analyze"]

import itertools
import math
from functools import lru_cache
from typing import Any

import numpy as np

from pending_query_worker import CountingSerializer, expected_digest


def validate_sample(record: dict[str, Any], plan: dict[str, Any]) -> None:
    case, sample = record["case"], record["sample"]
    for phase in ("baseline", "held", "released", "closed"):
        for field in ("rss_bytes", "rss_highwater_bytes"):
            if not math.isfinite(sample[phase][field]) or sample[phase][field] <= 0:
                raise ValueError("invalid RSS observation")
    count = min(case["backlog"], case["page"]) if case["query"] in ("metadata", "payload") else case["backlog"]
    if sample["rows"] != count or sample["digest"] != expected_digest(case):
        raise ValueError("row/result/order/serializer mismatch")
    for metric in (
        "wall_ns",
        "cpu_ns",
        "process_wall_ns",
        "subprocess_user_ns",
        "subprocess_system_ns",
    ):
        value = sample[metric]
        if not math.isfinite(value) or value < 0 or (metric in ("wall_ns", "process_wall_ns") and value == 0):
            raise ValueError(f"invalid timing: {metric}")
    if _rss(sample) > plan["rss_bytes"]:
        raise ValueError("worker RSS budget exceeded")
    if not case["diagnostic"]:
        if sample["copies"] is not None or sample["loads"] is not None or sample["python_peak_bytes"] is not None:
            raise ValueError("instrumentation in outcome sample")
        return
    if (
        not math.isfinite(sample["python_peak_bytes"])
        or not 0 <= sample["python_peak_bytes"] <= plan["python_peak_bytes"]
    ):
        raise ValueError("Python query peak budget exceeded")
    expected_loads = 0 if case["query"] == "metadata" else count
    if case["condition"] != "native":
        expected_loads = case["backlog"]
    if sample["loads"] != expected_loads:
        raise ValueError("deserialization count mismatch")
    serializer = CountingSerializer()
    expected_bytes = sum(
        len(serializer.dumps({"id": item_id, "body": ""})) + case["body"] for item_id in range(1, expected_loads + 1)
    )
    if sample["loaded_bytes"] != expected_bytes:
        raise ValueError("serialized byte count mismatch")
    copies = sample["copies"]
    if case["condition"] != "vendor":
        if copies["returned_rows"] != copies["freed_rows"] or copies["live_infos"] != 0:
            raise ValueError("native/Python result lifetime mismatch")
        if copies["payload_bytes"] != sample["loaded_bytes"] or copies["python_copy_bytes"] != sample["loaded_bytes"]:
            raise ValueError("payload/copy accounting mismatch")
        page = case["page"] if case["condition"] == "native" else 100
        all_rows = case["condition"] == "compat" or case["query"] in ("sequential", "accumulate")
        expected_calls = math.ceil(case["backlog"] / page) + 1 if all_rows else 1
        expected_rows = case["backlog"] if all_rows else count
        if (copies["calls"], copies["returned_rows"]) != (expected_calls, expected_rows):
            raise ValueError("native page/row accounting mismatch")
    if case["condition"] == "native":
        _validate_native(record["native"], case, plan)


def analyze(records: list[dict[str, Any]], registration: dict[str, Any]) -> dict[str, Any]:
    plan = registration["plan"]
    cells = []
    for backlog, body, page, query in itertools.product(
        plan["backlogs"], plan["body_bytes"], plan["pages"], plan["queries"]
    ):
        for baseline in ("vendor", "compat"):
            selected = [
                record
                for record in records
                if all(
                    record["case"][key] == value
                    for key, value in (
                        ("backlog", backlog),
                        ("body", body),
                        ("page", page),
                        ("query", query),
                    )
                )
            ]
            pairs = _pairs(selected, baseline, plan["formal_blocks"])
            wall = _interval(
                [candidate["wall_ns"] / reference["wall_ns"] for reference, candidate in pairs],
                plan,
            )
            rss = _interval(
                [_rss(candidate) / _rss(reference) for reference, candidate in pairs],
                plan,
            )
            is_primary = backlog == max(plan["backlogs"]) and query in (
                "metadata",
                "payload",
            )
            supported = _has_upper(wall, plan["latency_ratio_upper"])
            if is_primary:
                supported = supported and _has_upper(rss, plan["peak_ratio_upper"])
            cells.append(
                {
                    "backlog": backlog,
                    "body": body,
                    "page": page,
                    "query": query,
                    "baseline": baseline,
                    "wall_ratio": wall,
                    "rss_ratio": rss,
                    "pairs": len(pairs),
                    "role": "primary_guard" if is_primary else "descriptive",
                    "disposition": ("support" if supported else "hold") if is_primary else "descriptive",
                }
            )
    growth = _growth(records, plan)
    primary = [
        cell for cell in cells if cell["backlog"] == max(plan["backlogs"]) and cell["query"] in ("metadata", "payload")
    ]
    is_supported = all(cell["disposition"] == "support" for cell in primary) and all(
        row["within_envelope"] for row in growth
    )
    return {
        "schema": 1,
        "control": registration["control"],
        "formal_records": sum(row["case"]["block"] >= 0 for row in records),
        "preflight_records": sum(row["case"]["block"] == -1 for row in records),
        "diagnostic_records": sum(row["case"]["block"] == -2 for row in records),
        "cells": cells,
        "growth": growth,
        "diagnostic_overhead": _diagnostic_overhead(records),
        "disposition": "control_only"
        if registration["control"]
        else ("support_sampled_scope" if is_supported else "hold"),
        "limits": [
            "finite-grid diagnostic envelope; no asymptotic proof",
            "uncontrolled warm OS cache",
            "RSS lifetime highwater plus snapshots, not isolated query allocator peak",
            "native ledger probe excludes C API/Python metadata and payload malloc",
            "no pending filter/cross-page snapshot or consumer replacement claim",
        ],
    }


def _rss(sample: dict[str, Any]) -> int:
    return max(
        sample[phase][field]
        for phase in ("baseline", "held", "released", "closed")
        for field in ("rss_bytes", "rss_highwater_bytes")
    )


def _validate_native(sample: dict[str, Any], case: dict[str, Any], plan: dict[str, Any]) -> None:
    count = min(case["backlog"], case["page"]) if case["query"] in ("metadata", "payload") else case["backlog"]
    payload = case["query"] != "metadata"
    expected_hash, expected_bytes = _payload_identity(count, case["body"]) if payload else ("cbf29ce484222325", 0)
    if (
        sample["status"],
        sample["rows"],
        sample["payload_hash"],
        sample["payload_bytes"],
    ) != (0, count, expected_hash, expected_bytes):
        raise ValueError("native diagnostic result mismatch")
    if (
        sample["query_peak_bytes"] > plan["native_query_bytes"]
        or sample["shared_peak_bytes"] > plan["native_shared_bytes"]
    ):
        raise ValueError("native requested-byte budget exceeded")
    if sample["released_bytes"] != sample["baseline_bytes"] or sample["refused"] != 0:
        raise ValueError("native diagnostic lifetime/refusal mismatch")


@lru_cache(maxsize=64)
def _payload_identity(count: int, body: int) -> tuple[str, int]:
    digest, total = 14695981039346656037, 0
    serializer = CountingSerializer()
    for item_id in range(1, count + 1):
        data = serializer.dumps({"id": item_id, "body": "x" * body})
        total += len(data)
        for byte in data:
            digest = ((digest ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{digest:016x}", total


def _pairs(records: list[dict[str, Any]], baseline: str, blocks: int) -> list[tuple[Any, Any]]:
    pairs = []
    for block in range(blocks):
        selected = {row["case"]["condition"]: row["sample"] for row in records if row["case"]["block"] == block}
        pairs.append((selected[baseline], selected["native"]))
    return pairs


def _interval(values: list[float], plan: dict[str, Any]) -> dict[str, Any]:
    if not values or any(not math.isfinite(value) or value <= 0 for value in values):
        raise ValueError("invalid paired ratio")
    median = float(np.median(values))
    if len(set(values)) == 1:
        return {"median": median, "low": None, "high": None, "status": "degenerate"}
    data = np.asarray(values)
    random = np.random.RandomState(plan["seed"])
    medians = np.median(
        random.choice(data, size=(plan["bootstrap_resamples"], len(data)), replace=True),
        axis=1,
    )
    tail = (1 - plan["confidence"]) / (2 * plan["interval_family"])
    low, high = np.quantile(medians, [tail, 1 - tail])
    if not low < high:
        return {"median": median, "low": None, "high": None, "status": "degenerate"}
    return {
        "median": median,
        "low": float(low),
        "high": float(high),
        "status": "complete",
    }


def _has_upper(interval: dict[str, Any], limit: float) -> bool:
    return interval["status"] == "complete" and interval["high"] <= limit


def _growth(records: list[dict[str, Any]], plan: dict[str, Any]) -> list[dict[str, Any]]:
    result = []
    for body, page, query in itertools.product(
        plan["body_bytes"], plan["pages"], ("metadata", "payload", "sequential")
    ):
        selected = [
            row
            for row in records
            if row["case"]["diagnostic"]
            and row["case"]["condition"] == "native"
            and (row["case"]["body"], row["case"]["page"], row["case"]["query"]) == (body, page, query)
        ]
        selected.sort(key=lambda row: row["case"]["backlog"])
        small = selected[0]
        python_growth = max(
            row["sample"]["python_peak_bytes"] - small["sample"]["python_peak_bytes"] for row in selected
        )
        native_growth = max(row["native"]["query_peak_bytes"] - small["native"]["query_peak_bytes"] for row in selected)
        rss_growth = max(_rss(row["sample"]) - _rss(small["sample"]) for row in selected)
        result.append(
            {
                "body": body,
                "page": page,
                "query": query,
                "python_growth_bytes": python_growth,
                "native_query_growth_bytes": native_growth,
                "rss_growth_bytes": rss_growth,
                "within_envelope": python_growth <= plan["python_growth_bytes"]
                and native_growth <= plan["native_growth_bytes"]
                and rss_growth <= plan["rss_growth_bytes"],
            }
        )
    return result


def _diagnostic_overhead(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    preflight = {
        tuple(row["case"][key] for key in ("backlog", "body", "page", "query", "condition")): row["sample"]
        for row in records
        if row["case"]["block"] == -1
    }
    result = []
    for row in records:
        if not row["case"]["diagnostic"]:
            continue
        before = preflight[tuple(row["case"][key] for key in ("backlog", "body", "page", "query", "condition"))]
        result.append(
            {
                "case": row["case"],
                "wall_ratio": row["sample"]["wall_ns"] / before["wall_ns"],
                "rss_delta_bytes": _rss(row["sample"]) - _rss(before),
                "role": "descriptive_single_pair",
            }
        )
    return result
