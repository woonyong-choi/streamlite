"""persist-queue 호환 계층의 정리·재개·대기·claim 보관 계약(#273). 합성 데이터만 쓴다.

여러 프로세스가 필요한 시험은 별도 인터프리터(subprocess)가 같은 디렉터리의 큐를 연다.
"""

from __future__ import annotations

import subprocess
import sys
import threading
from pathlib import Path

import pytest
import streamlite.persistqueue_compat as compat
from streamlite import InvalidArgumentError, StaleError
from streamlite.persistqueue_compat import Empty, SQLiteAckQueue, UnknownClaimError

HOLDER = """
import sys
from streamlite.persistqueue_compat import SQLiteAckQueue
queue = SQLiteAckQueue(sys.argv[1], max_attempts=int(sys.argv[2]))
queue.put({"n": 1})
item = queue.get(block=False, raw=True)
print("claimed", item["pqid"], flush=True)
sys.stdin.readline()
try:
    queue.ack(id=item["pqid"])
    print("acked", flush=True)
except Exception as err:
    print("ack failed", type(err).__name__, flush=True)
queue.close()
"""


def _finish(queue: SQLiteAckQueue, acked: int, failed: int) -> None:
    """작업을 한 트랜잭션에 넣고 끝내 시험 시간을 줄인다."""
    native, db = queue._queue, queue._db  # noqa: SLF001
    with db.begin() as txn:
        for index in range(acked + failed):
            native.put(b"%d" % index, txn=txn)
    with db.begin() as txn:
        for index in range(acked + failed):
            claim = native.get(1000, txn=txn)
            assert claim is not None
            if index < acked:
                native.ack(claim, txn=txn)
            else:
                native.fail(claim, "ack_failed", txn=txn)


def _counts(queue: SQLiteAckQueue) -> tuple[int, int]:
    stats = queue._queue.stats()  # noqa: SLF001
    return stats.acked, stats.failed


def test_clear_acked_data_keeps_latest_when_clearing_failed(tmp_path: Path) -> None:
    queue = SQLiteAckQueue(tmp_path / "q")
    _finish(queue, acked=30, failed=1)

    queue.clear_acked_data(keep_latest=1000, clear_ack_failed=True)
    assert _counts(queue) == (30, 1)

    queue.clear_acked_data(keep_latest=10, clear_ack_failed=False)
    assert _counts(queue) == (10, 1)
    assert [row["id"] for row in queue.queue()][0] == 21  # 오래된 것부터 지운다

    queue.clear_acked_data(keep_latest=4, clear_ack_failed=True)
    assert _counts(queue) == (3, 1)  # 끝난 11건 중 id가 큰 4건(ack 3건과 실패 1건)이 남는다
    queue.clear_acked_data(keep_latest=0, clear_ack_failed=True)
    assert _counts(queue) == (0, 0)
    queue.close()


def test_clear_acked_data_splits_large_max_delete(tmp_path: Path) -> None:
    queue = SQLiteAckQueue(tmp_path / "q")
    _finish(queue, acked=1100, failed=0)

    queue.clear_acked_data(max_delete=5000, keep_latest=40)
    assert _counts(queue) == (40, 0)

    _finish(queue, acked=20, failed=0)
    queue.clear_acked_data(max_delete=15, keep_latest=0)
    assert _counts(queue) == (45, 0)
    queue.clear_acked_data(max_delete=None, keep_latest=0)  # type: ignore[arg-type]
    assert _counts(queue) == (0, 0)
    with pytest.raises(InvalidArgumentError):
        queue._queue.purge(5000)  # noqa: SLF001 - native purge의 한도는 그대로다
    queue.close()


@pytest.mark.parametrize("max_attempts", [0, 1])
def test_second_process_open_keeps_live_lease_of_first(tmp_path: Path, max_attempts: int) -> None:
    """두 번째 프로세스가 기본값으로 열어도 첫 프로세스의 살아 있는 lease를 풀지 않는다."""
    directory = tmp_path / "q"
    holder = subprocess.Popen(
        [sys.executable, "-c", HOLDER, str(directory), str(max_attempts)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
    )
    try:
        assert holder.stdout is not None and holder.stdin is not None
        assert holder.stdout.readline().split()[0] == "claimed"

        second = SQLiteAckQueue(directory, max_attempts=max_attempts)
        assert (second.unack_count, second.size, _counts(second)) == (1, 0, (0, 0))
        with pytest.raises(Empty):
            second.get(block=False)  # 중복 전달이 없다

        holder.stdin.write("\n")
        holder.stdin.flush()
        assert holder.stdout.readline().strip() == "acked"
        assert holder.wait(timeout=30) == 0
        assert (second.unack_count, second.acked_count) == (0, 1)
        second.close()
    finally:
        holder.kill()
        holder.wait()


def test_open_resumes_only_expired_leases_unless_asked(tmp_path: Path) -> None:
    now = {"ms": 1000}
    directory = tmp_path / "q"
    first = SQLiteAckQueue(directory, lease_ms=100, max_attempts=1, clock=lambda: now["ms"])
    live, expired = first.put("live"), first.put("expired")
    first.get(id=expired, block=False)
    now["ms"] += 100
    first.get(id=live, block=False)
    first.close()

    second = SQLiteAckQueue(directory, lease_ms=100, max_attempts=1, clock=lambda: now["ms"])
    assert (second.unack_count, second.size, _counts(second)) == (1, 0, (0, 1))  # 만료 + 시도 한도는 실패로 끝난다
    second.close()

    untouched = SQLiteAckQueue(directory, auto_resume=False, clock=lambda: now["ms"])
    assert untouched.unack_count == 1
    untouched.close()

    # 단일 프로세스 재시작: 살아 있는 lease까지 되돌린다. 해제는 실패가 아니라 시도 한도에 닿았어도 대기로 간다
    third = SQLiteAckQueue(directory, lease_ms=100, max_attempts=1, clock=lambda: now["ms"], resume_live_leases=True)
    assert (third.unack_count, third.size, _counts(third)) == (0, 1, (0, 1))
    assert third.get(block=False) == "live"
    third.close()


def test_resume_unack_tasks_revokes_claims_it_returned(tmp_path: Path) -> None:
    now = {"ms": 1000}
    queue = SQLiteAckQueue(tmp_path / "q", lease_ms=100, clock=lambda: now["ms"])
    first, second = queue.put("a"), queue.put("b")
    queue.get(id=first, block=False)
    now["ms"] += 100
    queue.get(id=second, block=False)

    queue.resume_unack_tasks()  # 기본은 만료된 lease만
    assert (queue.size, queue.unack_count) == (1, 1)
    with pytest.raises(StaleError):
        queue.ack(id=first)
    queue.resume_unack_tasks(include_live=True)
    assert (queue.size, queue.unack_count) == (2, 0)
    with pytest.raises(UnknownClaimError):
        queue.ack(id=second)
    queue.close()


def test_blocking_get_of_finished_or_purged_id_returns_at_once(tmp_path: Path) -> None:
    queue = SQLiteAckQueue(tmp_path / "q")
    purged, acked, failed = queue.put("a"), queue.put("b"), queue.put("c")
    for item_id in (purged, acked):
        queue.get(id=item_id, block=False)
        queue.ack(id=item_id)
    queue.get(id=failed, block=False)
    queue.ack_failed(id=failed)
    assert queue._queue.purge(1) == 1  # noqa: SLF001 - 가장 작은 id(purged)만 지운다
    outcomes: dict[int, str] = {}

    def wait(item_id: int) -> None:
        try:
            queue.get(id=item_id)  # block=True, timeout 없음
            outcomes[item_id] = "claimed"
        except Empty:
            outcomes[item_id] = "empty"

    workers = [threading.Thread(target=wait, args=(item_id,), daemon=True) for item_id in (acked, failed, purged)]
    for worker in workers:
        worker.start()
    for worker in workers:
        worker.join(timeout=3)

    assert outcomes == {acked: "empty", failed: "empty", purged: "empty"}
    with pytest.raises(Empty):
        queue.get(id=failed + 100, timeout=0.05)  # 아직 만들어지지 않았을 수 있는 id는 기다린다
    queue.close()


def test_blocking_get_still_waits_for_leased_id(tmp_path: Path) -> None:
    queue = SQLiteAckQueue(tmp_path / "q")
    item_id = queue.put("a")
    queue.get(id=item_id, block=False)
    got: list[object] = []
    waiter = threading.Thread(target=lambda: got.append(queue.get(id=item_id, timeout=10)), daemon=True)
    waiter.start()
    waiter.join(timeout=0.2)
    assert waiter.is_alive()  # 처리 중인 작업은 돌아올 수 있으므로 기다린다

    queue.nack(id=item_id)
    waiter.join(timeout=10)
    assert got == ["a"]
    queue.ack(id=item_id)
    queue.close()


def test_held_claims_are_bounded(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(compat, "_MAX_HELD_CLAIMS", 3)
    now = {"ms": 1000}
    queue = SQLiteAckQueue(tmp_path / "q", lease_ms=100, clock=lambda: now["ms"])
    ids = [queue.put(index) for index in range(6)]
    queue.get(id=ids[0], block=False)
    now["ms"] += 100  # 첫 claim의 lease가 만료됐다
    for item_id in ids[1:5]:
        queue.get(id=item_id, block=False)

    held = queue._claims  # noqa: SLF001
    assert list(held) == ids[2:5]  # 만료된 것부터, 그다음 오래된 것부터 잊는다
    with pytest.raises(UnknownClaimError):
        queue.ack(id=ids[1])
    assert queue.ack(id=ids[4]) == ids[4]
    queue.get(id=ids[5], block=False)
    assert len(held) == 3
    queue.close()
