from __future__ import annotations

import argparse
import copy
import json
import sys
import uuid
from pathlib import Path
from typing import Any

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))

import common_cost as campaign  # noqa: E402
import common_cost_analysis as analysis  # noqa: E402


@pytest.fixture(scope="module", params=["cache", "lifecycle", "conditional"])
def collected(request: pytest.FixtureRequest) -> tuple[Path, dict[str, Any]]:
    phase = request.param
    registration = campaign.register(
        argparse.Namespace(
            control=True,
            phase="cache" if phase == "cache" else "lifecycle",
            plan_commit="HEAD",
            off_build=ROOT / "build",
            on_build=ROOT / "build-benchmark",
        )
    )
    if phase == "conditional":
        registration["phase"] = phase
        registration["builds"].update(
            candidate=registration["builds"]["off"], candidate_on=registration["builds"]["on"]
        )
        registration["cases"] = campaign.cases(registration["plan"], phase)
    prefix = ROOT / "build" / f"task18b-control-{phase}-{uuid.uuid4().hex[:8]}"
    campaign.collect(registration, prefix)
    assert len(campaign.verify(registration, prefix)) == len(registration["cases"])
    print(json.dumps({"phase": phase, "prefix": str(prefix), "control": True}))
    return prefix, registration


def _pin_sources(monkeypatch: pytest.MonkeyPatch) -> None:
    sources = {name: (ROOT / name).read_bytes() for name in campaign._criteria_sources()}

    def git_output(arguments: list[str]) -> bytes:
        if arguments[0] == "rev-parse":
            return ("a" * 40).encode()
        if arguments[0] == "merge-base":
            return b""
        if arguments[0] == "ls-tree":
            return "\n".join(sources).encode()
        return sources[arguments[1].split(":", 1)[1]]

    monkeypatch.setattr(analysis, "_git_output", git_output)


@pytest.mark.parametrize("source_commit", [None, "pinned"])
@pytest.mark.parametrize("kind", ["raw", "all", "raw_altered"])
def test_verify_rejects_incomplete_hashes_before_raw_decoding(
    collected: tuple[Path, dict[str, Any]], source_commit: str | None, kind: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration = collected
    _pin_sources(monkeypatch)
    original = Path.read_text
    raw_reads = []

    def read(path: Path, *args: Any, **kwargs: Any) -> str:
        value = original(path, *args, **kwargs)
        if path == Path(f"{prefix}.manifest.json"):
            manifest = json.loads(value)
            if kind == "all":
                manifest["hashes"] = {}
            else:
                del manifest["hashes"][f"{prefix}.raw.jsonl"]
            return json.dumps(manifest)
        if path == Path(f"{prefix}.raw.jsonl"):
            raw_reads.append(path)
            if kind == "raw_altered":
                rows = [json.loads(line) for line in value.splitlines()]
                rows[0]["sample"]["wall_ns"] *= 2
                return "\n".join(json.dumps(row) for row in rows)
        return value

    monkeypatch.setattr(Path, "read_text", read)
    with pytest.raises(ValueError, match="missing artifact hashes"):
        campaign.verify(registration, prefix, source_commit=source_commit)
    assert raw_reads == []


@pytest.mark.parametrize(
    "kind",
    ["stdout", "stderr", "archive", "fixture", "registration", "alias", "outside", "digest", "symlink", "duplicate"],
)
def test_verify_rejects_missing_or_invalid_campaign_artifacts(
    collected: tuple[Path, dict[str, Any]], kind: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration = collected
    original = Path.read_text
    original_resolve = Path.resolve
    if kind == "symlink":
        monkeypatch.setattr(
            Path,
            "resolve",
            lambda path, *a, **kw: (
                ROOT / "README.md" if str(path) == f"{prefix}.raw.jsonl" else original_resolve(path, *a, **kw)
            ),
        )

    def read(path: Path, *args: Any, **kwargs: Any) -> str:
        value = original(path, *args, **kwargs)
        if path != Path(f"{prefix}.manifest.json"):
            return value
        manifest = json.loads(value)
        hashes = manifest["hashes"]
        if kind == "duplicate":
            return value.rstrip()[:-1] + ', "hashes": {}}'
        if kind == "fixture" and registration["phase"] != "cache":
            target = next(name for name in hashes if name.endswith(".db.gz"))
        elif kind in ("archive", "fixture"):
            target = next(name for name in hashes if name.endswith(".db.gz" if kind == "archive" else ".db"))
        else:
            suffix = {"stdout": "-0000.stdout", "stderr": "-0000.stderr", "registration": ".registration.json"}
            target = f"{prefix}{suffix.get(kind, '.raw.jsonl')}"
        if kind == "alias":
            hashes[f"{prefix.parent}/./{prefix.name}.raw.jsonl"] = hashes[target]
        elif kind == "outside":
            hashes[str(ROOT / "README.md")] = hashes[target]
        elif kind == "digest":
            hashes[target] = "G" * 64
        elif kind != "symlink":
            hashes.pop(target)
        return json.dumps(manifest)

    monkeypatch.setattr(Path, "read_text", read)
    with pytest.raises(ValueError, match="artifact (hash|path)|missing artifact hashes|duplicate JSON"):
        campaign.verify(registration, prefix)


@pytest.mark.parametrize("kind", ["missing", "path", "hash", "bytes", "compressed", "storage", "fixture"])
def test_verify_rejects_archive_metadata_that_differs_from_registered_layout(
    collected: tuple[Path, dict[str, Any]], kind: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, registration = collected
    original = Path.read_text

    def read(path: Path, *args: Any, **kwargs: Any) -> str:
        value = original(path, *args, **kwargs)
        if path != Path(f"{prefix}.raw.jsonl"):
            return value
        rows = [json.loads(line) for line in value.splitlines()]
        row = rows[0]
        if kind == "missing":
            row["archives"] = []
        elif kind == "path":
            row["archives"][0]["path"] = str(ROOT / "README.md")
        elif kind == "hash":
            row["archives"][0]["uncompressed_sha256"] = "0" * 64
        elif kind == "bytes":
            row["archives"][0]["uncompressed_bytes"] += 1
        elif kind == "compressed":
            row["archives"][0]["compressed_bytes"] += 1
        elif kind == "storage":
            row["storage_bytes"] += 1
        else:
            row["fixture_sha256"] = "0" * 64
        return "\n".join(json.dumps(row) for row in rows)

    monkeypatch.setattr(Path, "read_text", read)
    with pytest.raises(ValueError, match="archive|fixture|storage"):
        campaign.verify(registration, prefix)


def test_verify_accepts_later_analysis_output(collected: tuple[Path, dict[str, Any]]) -> None:
    prefix, registration = collected
    expected = campaign.verify(registration, prefix)
    Path(f"{prefix}.analysis.json").write_text("{}\n")

    assert campaign.verify(registration, prefix) == expected


def test_verify_retains_case_and_source_guards(
    collected: tuple[Path, dict[str, Any]], monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, original = collected
    registration = copy.deepcopy(original)
    name = next(iter(registration["criteria_hashes"]))
    registration["criteria_hashes"][name] = "0" * 64
    with pytest.raises(ValueError, match="analysis input changed"):
        campaign.verify(registration, prefix)
    registration = copy.deepcopy(original)
    registration["cases"][0]["id"] = "../0000"
    manifest = json.loads(Path(f"{prefix}.manifest.json").read_text())
    manifest["registration_sha256"] = campaign._registration_digest(registration)
    read = Path.read_text
    monkeypatch.setattr(
        Path,
        "read_text",
        lambda path, *a, **kw: json.dumps(manifest) if str(path) == f"{prefix}.manifest.json" else read(path, *a, **kw),
    )
    with pytest.raises(ValueError, match="case identity"):
        campaign.verify(registration, prefix)


@pytest.mark.parametrize("is_matching", [False, True])
def test_collect_preserves_existing_registration_or_rejects_mismatch(is_matching: bool) -> None:
    registration = campaign.register(
        argparse.Namespace(
            control=True, phase="cache", plan_commit="HEAD", off_build=ROOT / "build", on_build=ROOT / "build-benchmark"
        )
    )
    registration["cases"] = registration["cases"][:1]
    prefix = ROOT / "build" / f"task18b-registration-{uuid.uuid4().hex[:8]}"
    path = Path(f"{prefix}.registration.json")
    original = json.dumps(registration if is_matching else {"preserved": True}).encode()
    path.write_bytes(original)

    if is_matching:
        campaign.collect(registration, prefix)
        assert len(campaign.verify(registration, prefix)) == 1
    else:
        with pytest.raises(ValueError, match="existing registration differs"):
            campaign.collect(registration, prefix)
        assert not Path(f"{prefix}.raw.jsonl").exists()
    assert path.read_bytes() == original


def test_artifact_component_accepts_legacy_control_without_registration_file(
    collected: tuple[Path, dict[str, Any]], monkeypatch: pytest.MonkeyPatch
) -> None:
    prefix, current = collected
    registration = copy.deepcopy(current)
    del registration["criteria_hashes"]["bench/common_cost_artifacts.py"]
    manifest = json.loads(Path(f"{prefix}.manifest.json").read_text())
    path = Path(f"{prefix}.registration.json")
    del manifest["hashes"][str(path)]
    original_exists = Path.exists
    monkeypatch.setattr(Path, "exists", lambda candidate: False if candidate == path else original_exists(candidate))

    layout = campaign.validate_artifacts(registration, prefix, manifest)
    records = [json.loads(line) for line in Path(f"{prefix}.raw.jsonl").read_text().splitlines()]
    campaign.validate_archives(records, layout, manifest["hashes"])
