"""Validate registered common-cost artifact coverage before statistical analysis."""

from __future__ import annotations

__all__ = ["read_json", "validate_artifacts", "validate_archives"]

import gzip
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class _ArtifactLayout:
    files: set[str]
    archives: list[list[str]]
    fixtures: list[str | None]
    storage_limit: int


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(), object_pairs_hook=_unique_object)


def validate_artifacts(registration: dict[str, Any], prefix: Path, manifest: dict[str, Any]) -> _ArtifactLayout:
    layout = _derive_layout(registration, prefix)
    hashes = manifest.get("hashes")
    if not isinstance(hashes, dict):
        raise ValueError("artifact hashes must be an object")
    missing = layout.files - hashes.keys()
    if missing:
        raise ValueError(f"missing artifact hashes: {sorted(missing)}")
    for name, digest in hashes.items():
        if name not in layout.files or name != str(Path(name).resolve()):
            raise ValueError(f"invalid artifact path: {name}")
        if not _is_digest(digest):
            raise ValueError(f"invalid artifact hash: {name}")
        if not Path(name).is_file() or _digest(Path(name)) != digest:
            raise ValueError(f"raw artifact changed or missing: {name}")
    registration_path = Path(f"{prefix}.registration.json")
    if str(registration_path) in layout.files and read_json(registration_path) != registration:
        raise ValueError("registration file differs from supplied registration")
    return layout


def validate_archives(records: list[dict[str, Any]], layout: _ArtifactLayout, hashes: dict[str, str]) -> None:
    for record, paths, fixture in zip(records, layout.archives, layout.fixtures):
        archives = record.get("archives")
        if not isinstance(archives, list) or len(archives) != len(paths):
            raise ValueError("archive count differs from registered layout")
        storage_bytes = 0
        for archive, path in zip(archives, paths):
            storage_bytes += _validate_archive(archive, path, layout.storage_limit)
        if record["storage_bytes"] != storage_bytes:
            raise ValueError("storage bytes differ from preserved databases")
        expected_fixture = None if fixture is None else hashes[fixture]
        if record["fixture_sha256"] != expected_fixture:
            raise ValueError("fixture hash differs from preserved fixture")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, value in pairs:
        if name in result:
            raise ValueError(f"duplicate JSON key: {name}")
        result[name] = value
    return result


def _derive_layout(registration: dict[str, Any], prefix: Path) -> _ArtifactLayout:
    files = {f"{prefix}.raw.jsonl"}
    registration_path = Path(f"{prefix}.registration.json")
    is_current = "bench/common_cost_artifacts.py" in registration["criteria_hashes"]
    if is_current or not registration["control"] or registration_path.exists():
        files.add(str(registration_path))
    archives, fixtures = [], []
    for index, case in enumerate(registration["cases"]):
        if case["id"] != f"{index:04d}":
            raise ValueError("invalid registered case identity")
        destination = f"{prefix}-{case['id']}"
        files.update((f"{destination}.stdout", f"{destination}.stderr"))
        fixture = None
        if registration["phase"] == "cache":
            if type(case["payload"]) is not int or case["payload"] < 0:
                raise ValueError("invalid registered fixture payload")
            fixture_prefix = f"{prefix}-fixture-{case['payload']}"
            fixture = f"{fixture_prefix}.db"
            files.update((fixture, f"{fixture_prefix}.stdout", f"{fixture_prefix}.stderr"))
            paths = [f"{destination}.db.gz"]
        elif registration["phase"] in ("lifecycle", "conditional"):
            count = 1 if case["scenario"] == "conditional" else registration["plan"]["lifecycle_instances"]
            paths = [f"{destination}-{instance}.db.gz" for instance in range(count)]
        else:
            raise ValueError("invalid registered artifact phase")
        files.update(paths)
        archives.append(paths)
        fixtures.append(fixture)
    return _ArtifactLayout(files, archives, fixtures, registration["plan"]["campaign_storage_bytes"])


def _is_digest(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _validate_archive(archive: dict[str, Any], name: str, limit: int) -> int:
    if archive.get("path") != name:
        raise ValueError("archive path differs from registered layout")
    expected = archive.get("uncompressed_sha256")
    size = archive.get("uncompressed_bytes")
    compressed = archive.get("compressed_bytes")
    if not _is_digest(expected) or type(size) is not int or not 0 < size <= limit:
        raise ValueError("invalid archive digest or uncompressed size")
    if type(compressed) is not int or compressed != Path(name).stat().st_size:
        raise ValueError("archive compressed bytes mismatch")
    digest, actual_size = hashlib.sha256(), 0
    with gzip.open(name, "rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            actual_size += len(chunk)
            if actual_size > size:
                raise ValueError("archive exceeds recorded uncompressed bytes")
            digest.update(chunk)
    if actual_size != size or digest.hexdigest() != expected:
        raise ValueError("archive uncompressed bytes or digest mismatch")
    return actual_size
