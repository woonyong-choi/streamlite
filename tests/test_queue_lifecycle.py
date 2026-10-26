from __future__ import annotations

import gc
import threading
import weakref
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest

import streamlite.database as database_module
from streamlite import BusyError, Database, InvalidArgumentError, NotInstalledError, Queue
from streamlite.database import Closable


def test_database_retains_unreleased_queue_and_clock_until_close(tmp_path: Path) -> None:
    db = Database(tmp_path / "owned.streamlite")

    def clock() -> int:
        return 1000

    queue = Queue(db, "jobs", clock=clock)
    queue.put(b"kept")
    queue_ref, clock_ref = weakref.ref(queue), weakref.ref(clock)
    del queue, clock
    gc.collect()

    assert queue_ref() is not None
    assert clock_ref() is not None
    db.close()
    gc.collect()

    assert queue_ref() is None
    assert clock_ref() is None
    with Database(tmp_path / "owned.streamlite", create=False) as reopened:
        assert Queue(reopened, "jobs", install=False).stats().ready == 1


def test_queue_close_releases_database_ownership_before_database_close(tmp_path: Path) -> None:
    with Database(tmp_path / "released.streamlite") as db:
        queue = Queue(db, "jobs")
        queue_ref = weakref.ref(queue)

        queue.close()
        queue.close()
        del queue
        gc.collect()

        assert queue_ref() is None
        assert Queue(db, "jobs", install=False).stats().ready == 0


@pytest.mark.parametrize("finish", ["commit", "rollback"])
@pytest.mark.parametrize("use_queue", [False, True])
def test_database_busy_close_preserves_transaction_and_closed_queues(
    tmp_path: Path, finish: str, *, use_queue: bool
) -> None:
    path = tmp_path / "busy.streamlite"
    db = Database(path)
    db.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY, body TEXT)")
    queue = Queue(db, "jobs") if use_queue else None
    txn = db.begin()
    txn.execute("INSERT INTO notes VALUES (1, 'pending')")
    if queue is not None:
        queue.put(b"pending", txn=txn)

    with pytest.raises(BusyError):
        db.close()
    assert txn.execute("SELECT id, body FROM notes").rows == ((1, "pending"),)
    if queue is not None:
        with pytest.raises(ValueError):
            queue.stats()
    getattr(txn, finish)()
    db.close()
    db.close()

    with Database(path, create=False) as reopened:
        expected = ((1, "pending"),) if finish == "commit" else ()
        assert reopened.execute("SELECT id, body FROM notes").rows == expected
        if use_queue:
            assert Queue(reopened, "jobs", install=False).stats().ready == (finish == "commit")


def test_queue_registration_failure_releases_open_native_handle(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    path = tmp_path / "registration.streamlite"
    db = Database(path)

    def refuse_registration(queue: Closable) -> None:
        raise MemoryError("registration refused")

    with monkeypatch.context() as patch:
        patch.setattr(db, "register_queue", refuse_registration)
        with pytest.raises(MemoryError):
            Queue(db, "jobs")

    queue = Queue(db, "jobs", install=False)
    queue.put(b"after failure")
    db.close()

    with Database(path, create=False) as reopened:
        assert Queue(reopened, "jobs", install=False).stats().ready == 1


def test_queue_open_error_leaves_database_usable_and_closable(tmp_path: Path) -> None:
    path = tmp_path / "missing.streamlite"
    db = Database(path)

    with pytest.raises(NotInstalledError):
        Queue(db, "jobs", install=False)
    with pytest.raises(InvalidArgumentError):
        Queue(db, "jobs", max_payload_bytes=0)
    db.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY)")
    db.execute("INSERT INTO notes VALUES (7)")
    db.close()

    with Database(path, create=False) as reopened:
        assert reopened.execute("SELECT id FROM notes").rows == ((7,),)


@pytest.mark.parametrize("failure", ["create", "insert"])
def test_queue_registry_allocation_failure_leaves_no_orphan_handle(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, failure: str
) -> None:
    path = tmp_path / "registry-oom.streamlite"
    db = Database(path)

    class RefusingRegistry(set[Closable]):
        def add(self, queue: Closable) -> None:
            raise MemoryError("insertion refused")

    def allocate_registry() -> set[Closable]:
        if failure == "create":
            raise MemoryError("allocation refused")
        return RefusingRegistry()

    with monkeypatch.context() as patch:
        patch.setattr(database_module, "set", allocate_registry, raising=False)
        with pytest.raises(MemoryError):
            Queue(db, "jobs")
    db.close()

    with Database(path, create=False) as reopened:
        queue = Queue(reopened, "jobs", install=False)
        assert queue.put(b"after allocation failure").id == 1
        assert queue.stats().ready == 1


def test_database_queue_close_error_preserves_live_handles_for_retry(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    db = Database(tmp_path / "close-error.streamlite")
    queue = Queue(db, "jobs")
    Queue(db, "other")
    close_queue = queue.close

    def refuse_close() -> None:
        raise OSError("close refused")

    monkeypatch.setattr(queue, "close", refuse_close)
    with pytest.raises(OSError):
        db.close()
    assert queue.put(b"still open").id == 1
    assert db.execute("SELECT id FROM streamlite_queue_jobs").rows == ((1,),)

    monkeypatch.setattr(queue, "close", close_queue)
    db.close()
    db.close()
    with pytest.raises(ValueError):
        queue.stats()


def test_database_close_retains_queue_registered_during_close(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    db = Database(tmp_path / "late.streamlite")
    queue = Queue(db, "jobs")
    close_queue = queue.close
    opened: list[Queue] = []

    def close_and_open() -> None:
        close_queue()
        opened.append(Queue(db, "jobs", install=False))

    monkeypatch.setattr(queue, "close", close_and_open)
    with pytest.raises(BusyError):
        db.close()
    assert opened[0].put(b"late").id == 1

    db.close()
    with pytest.raises(ValueError):
        opened[0].stats()


def test_database_closes_concurrent_first_queue_registrations(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    path = tmp_path / "parallel.streamlite"
    with Database(path) as setup:
        Queue(setup, "jobs")
    db = Database(path, create=False)
    barrier = threading.Barrier(2)

    def allocate_registry() -> set[Closable]:
        registry: set[Closable] = set()
        barrier.wait(timeout=5)
        return registry

    with monkeypatch.context() as patch:
        patch.setattr(database_module, "set", allocate_registry, raising=False)
        with ThreadPoolExecutor(max_workers=2) as workers:
            queues = list(workers.map(lambda _: Queue(db, "jobs", install=False), range(2)))

    assert [queue.put(b"parallel").id for queue in queues] == [1, 2]
    db.close()
    for queue in queues:
        with pytest.raises(ValueError):
            queue.stats()
