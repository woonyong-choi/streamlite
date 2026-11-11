"""persist-queue 1.1.0 SQLiteAckQueue 호출을 Queue 위에서 받아 주는 호환 계층.

Markchain이 쓰는 put, get(id, block, raw), ack, nack, update, queue, clear_acked_data, shrink_disk_usage, close를
같은 이름과 반환 모양으로 제공한다. 의미가 같지 않은 부분은 의도한 차이이며 아래에 모두 적었다.

의미 차이(persist-queue -> 이 계층):
1. ack·nack·ack_failed·update는 같은 프로세스에서 get으로 받은 claim(fence)이 있어야 한다. persist-queue는 id만
   주면 무조건 바꾸지만 여기서는 claim을 모르면 UnknownClaimError다(재시작 뒤에는 get으로 다시 claim한다).
2. lease를 잃은 claim(만료 뒤 다른 worker가 가져감)의 ack·nack은 StaleError다. persist-queue는 항상 성공한다.
3. get은 기본 24시간 lease를 건다. persist-queue는 lease가 없어 처리 중(unack)이 재시작 때만 풀린다. auto_resume(기본
   True)은 열 때 lease가 만료된 작업만 대기로 돌린다(recover). 다른 프로세스가 처리 중인 작업은 건드리지 않으므로 여러
   프로세스가 같은 디렉터리를 열어도 된다. persist-queue처럼 재시작 때 처리 중이던 작업을 모두 되돌리려면(이 디렉터리를
   쓰는 프로세스가 하나뿐일 때만) resume_live_leases=True를 주거나 lease_ms를 처리 시간에 맞게 줄인다. 되돌리는 것은
   실패가 아니므로 max_attempts에 닿은 작업도 대기로 돌아가고, 만료로 되돌릴 때만 시도 횟수가 다 된 작업이 실패로 끝난다.
4. update는 끝난 작업(acked·ack_failed)을 거절한다(StaleError). persist-queue는 어느 상태든 바꾼다.
5. queue()의 timestamp는 enqueue 시각(초)이고 status는 persist-queue 문자열 코드로 옮긴다(0 새 작업, 1 재시도 대기,
   2 처리 중, 5 ack됨, 9 실패).
6. clear_acked_data는 오래된 것부터 지운다(persist-queue는 최신 쪽부터 limit만큼). keep_latest=0이면 같은 결과다.
   keep_latest는 지우는 대상(ack, clear_ack_failed면 실패 포함) 전체에서 최신 쪽을 남기는 수이며 항상 지킨다.
7. shrink_disk_usage는 아무것도 하지 않는다(VACUUM에 해당하는 파일 축소가 없다). size·active_size·qsize는 전체 스캔이다.
8. 큰 payload(큐의 max_payload_bytes, 기본 1 MiB 초과)와 직렬화 결과가 UTF-8이 아닌 값은 LimitError·InvalidArgumentError다.
9. get(id=...)은 그 작업이 이미 끝났거나(ack·실패) 지워졌으면 block이어도 기다리지 않고 Empty다. 처리 중이거나 재시도
   대기 중인 작업, 아직 만들어지지 않았을 수 있는 id는 예전처럼 기다린다.
10. ack하지 않은 claim은 이 객체가 최대 _MAX_HELD_CLAIMS개까지만 기억한다. 넘으면 lease가 만료된 것부터, 그다음 오래된
    것부터 잊으며 잊은 claim의 ack·nack은 UnknownClaimError다(작업은 lease가 끝나면 다시 전달된다).
"""
# ruff: noqa: A002, ANN401, FBT001, FBT002 - persist-queue의 시그니처(id, block, 임의의 직렬화 값)를 그대로 따른다

from __future__ import annotations

import json
import threading
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any, Protocol

from streamlite.queue import Queue, Claim, ClaimToken, ItemInfo, ItemState
from streamlite.database import Database
from streamlite.errors import StreamLiteError, NotFoundError, StaleError

__all__ = ["Empty", "SQLiteAckQueue", "UnknownClaimError"]

DEFAULT_LEASE_MS = 24 * 3600 * 1000
_POLL_INTERVAL_S = 0.01
_PAGE = 100
_PURGE_BATCH = 1024  # Queue.purge 한 번의 상한(ACKQ_MAX_CLAIM_BATCH)
_MAX_HELD_CLAIMS = 10_000  # ack를 기다리며 기억하는 claim 수의 상한(payload를 함께 쥔다)
_FINISHED = (ItemState.ACKED, ItemState.FAILED)
_STATUS_CODES = {ItemState.LEASED: "2", ItemState.ACKED: "5", ItemState.FAILED: "9"}


class Empty(Exception):  # noqa: N818 - persistqueue.exceptions.Empty와 같은 이름
    """가져올 작업이 없다."""


class UnknownClaimError(StreamLiteError):
    """이 프로세스가 claim한 적 없는 id에 ack·nack·update를 했다(fence를 알 수 없다)."""


class Serializer(Protocol):
    """persist-queue 직렬화 모듈의 dumps·loads."""

    def dumps(self, value: Any) -> bytes:
        """값을 바이트로."""

    def loads(self, data: bytes) -> Any:
        """바이트를 값으로."""


class _JsonSerializer:
    def dumps(self, value: Any) -> bytes:
        return json.dumps(value).encode("utf-8")

    def loads(self, data: bytes) -> Any:
        return json.loads(data.decode("utf-8"))


class SQLiteAckQueue:
    """persist-queue SQLiteAckQueue의 일부 호출을 받는 큐. path는 디렉터리이며 그 안에 DB와 WAL을 둔다."""

    def __init__(
        self,
        path: str | Path,
        auto_resume: bool = True,
        multithreading: bool = True,
        serializer: Serializer | None = None,
        *,
        lease_ms: int = DEFAULT_LEASE_MS,
        max_attempts: int = 0,
        clock: Callable[[], int] | None = None,
        resume_live_leases: bool = False,
    ) -> None:
        """큐를 연다.

        Args:
            auto_resume: 열 때 lease가 만료된 처리 중 작업을 대기로 돌린다. 살아 있는 lease는 건드리지 않는다.
            resume_live_leases: auto_resume과 resume_unack_tasks가 살아 있는 lease까지 모두 되돌린다(persist-queue의
                재시작 의미). 이 디렉터리를 쓰는 다른 프로세스가 없을 때만 켠다. 있으면 그 프로세스의 ack가
                StaleError가 되고 작업이 중복 전달된다.
        """
        del multithreading  # 항상 스레드 안전하다
        directory = Path(path)
        directory.mkdir(parents=True, exist_ok=True)
        self._db = Database(directory / "data.streamlite")
        self._queue = Queue(self._db, "ack_queue", max_attempts=max_attempts, clock=clock)
        self._serializer: Serializer = serializer or _JsonSerializer()
        self._lease_ms = lease_ms
        self._clock = clock
        self._resume_live = resume_live_leases
        self._lock = threading.RLock()
        self._claims: dict[int, tuple[Claim, Any]] = {}
        self._max_seen_id = 0
        if auto_resume:
            self.resume_unack_tasks()

    def put(self, item: Any) -> int:
        """작업을 넣고 id를 돌려준다."""
        item_id = self._queue.put(self._serializer.dumps(item)).id
        self._saw(item_id)
        return item_id

    def get(
        self,
        block: bool = True,
        timeout: float | None = None,
        id: int | None = None,
        next_in_order: bool = False,
        raw: bool = False,
    ) -> Any:
        """작업을 claim한다. raw면 {'pqid', 'data', 'timestamp'}를 돌려준다.

        Raises:
            Empty: 가져올 것이 없거나 id를 claim할 수 없을 때(block이면 timeout까지 기다린 뒤). id가 이미 끝났거나
                지워진 작업이면 기다리지 않는다.
        """
        if next_in_order and id is None:
            raise ValueError("'next_in_order' requires the preceding 'id' be specified.")
        if timeout is not None and timeout < 0:
            raise ValueError("'timeout' must be a non-negative number")
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            claim = self._try_claim(id, next_in_order=next_in_order)
            if claim is not None:
                return self._deliver(claim, raw=raw)
            if not block or (deadline is not None and time.monotonic() >= deadline):
                raise Empty
            time.sleep(_POLL_INTERVAL_S)

    def ack(self, item: Any = None, id: int | None = None) -> int:
        """처리를 끝낸다. 옛 claim이면 StaleError다."""
        claim = self._resolve(item, id)
        self._queue.ack(claim)
        self._forget(claim.id)
        return claim.id

    def ack_failed(self, item: Any = None, id: int | None = None) -> int:
        """다시 시도하지 않는 실패로 끝낸다."""
        claim = self._resolve(item, id)
        self._queue.fail(claim, "ack_failed")
        self._forget(claim.id)
        return claim.id

    def nack(self, item: Any = None, id: int | None = None) -> int:
        """다시 가져갈 수 있게 대기로 돌린다."""
        claim = self._resolve(item, id)
        self._queue.nack(claim)
        self._forget(claim.id)
        return claim.id

    def update(self, item: Any, id: int | None = None) -> int:
        """payload를 바꾼다. {'pqid', 'data'} 모양이나 id를 받는다."""
        job_id = id
        if isinstance(item, dict) and "pqid" in item:
            job_id = item["pqid"] if id is None else id
            item = item.get("data")
        if job_id is None:
            raise ValueError("Provide an id or raw item")
        with self._lock:
            held = self._claims.get(job_id)
        fence = held[0].fence if held is not None else 0
        self._queue.update(job_id, self._serializer.dumps(item), fence=fence)
        return job_id

    def queue(self) -> list[dict[str, Any]]:
        """모든 작업을 persist-queue 모양으로 돌려준다(payload를 모두 복사한다)."""
        return [self._row(info) for info in self._queue.iter_items(_PAGE, with_payload=True)]

    def clear_acked_data(
        self, max_delete: int | None = 1000, keep_latest: int = 1000, clear_ack_failed: bool = False
    ) -> None:
        """ack된 작업(clear_ack_failed면 실패한 작업도)을 오래된 것부터 지우고 최신 keep_latest건은 남긴다.

        max_delete는 한 번에 지울 최대 수이며 0이나 None이면 제한이 없다. purge 한도(1024건)보다 크면 나눠 지운다.
        세는 것과 지우는 것이 한 트랜잭션이 아니므로 다른 프로세스가 동시에 지우면 keep_latest보다 적게 남을 수 있다.
        """
        stats = self._queue.stats()
        finished = stats.acked + (stats.failed if clear_ack_failed else 0)
        remaining = finished - max(keep_latest, 0)
        if max_delete is not None and max_delete > 0:
            remaining = min(remaining, max_delete)
        while remaining > 0:
            purged = self._queue.purge(min(remaining, _PURGE_BATCH), include_failed=clear_ack_failed)
            if purged == 0:
                return
            remaining -= purged

    def shrink_disk_usage(self) -> None:
        """파일 축소를 지원하지 않는다(의미 차이 7). 아무것도 하지 않는다."""

    def resume_unack_tasks(self, include_live: bool | None = None) -> None:
        """처리 중이던 작업을 대기로 돌린다(재시작 복구).

        include_live가 False면 lease가 만료된 작업만 돌린다(시도 횟수가 다 된 것은 실패로 끝난다). True면 살아 있는
        lease까지 모두 돌리며, 다른 프로세스가 처리 중인 작업도 되돌려 중복 전달을 만든다. None이면 생성자의
        resume_live_leases를 따른다. 돌려놓은 작업의 옛 claim은 끝난다.
        """
        if self._resume_live if include_live is None else include_live:
            while self._queue.release_leases() > 0:
                pass
            with self._lock:
                self._claims.clear()
            return
        while self._queue.recover() > 0:
            pass

    def task_done(self) -> None:
        """persist-queue의 auto_commit=False용 호출. 이 계층은 항상 commit하므로 하는 일이 없다."""

    @property
    def size(self) -> int:
        """가져갈 수 있는 작업 수(대기)."""
        return self._queue.stats().ready

    @property
    def unack_count(self) -> int:
        """처리 중인 작업 수."""
        return self._queue.stats().leased

    @property
    def acked_count(self) -> int:
        """ack된 작업 수."""
        return self._queue.stats().acked

    @property
    def active_size(self) -> int:
        """대기와 처리 중인 작업 수."""
        stats = self._queue.stats()
        return stats.ready + stats.leased

    def qsize(self) -> int:
        """size와 같다."""
        return self.size

    def empty(self) -> bool:
        """대기 작업이 없다."""
        return self.size == 0

    def __len__(self) -> int:
        return self.size

    def close(self) -> None:
        """큐와 DB를 닫는다."""
        self._queue.close()
        self._db.close()

    def _try_claim(self, item_id: int | None, *, next_in_order: bool) -> Claim | None:
        if item_id is None:
            return self._queue.get(self._lease_ms)
        if not next_in_order:
            return self._claim_id(item_id)
        for info in self._queue.iter_items(_PAGE):
            if info.id > item_id and info.state is ItemState.READY:
                claim = self._claim_id(info.id)
                if claim is not None:
                    return claim
        return None

    def _claim_id(self, item_id: int) -> Claim | None:
        """id 하나를 claim한다. 지금은 못 가져오지만 기다리면 될 수 있으면 None이다.

        Raises:
            Empty: 그 작업이 끝났거나 지워져 기다려도 가져올 수 없을 때.
        """
        try:
            return self._queue.get(self._lease_ms, item_id=item_id)
        except StaleError:
            listed = self._queue.list_items(item_id - 1, 1)
            if listed and listed[0].id == item_id and listed[0].state in _FINISHED:
                raise Empty from None
            return None  # 처리 중이거나 재시도 대기 중이다
        except NotFoundError:
            # id는 다시 쓰이지 않는다. 더 큰 id가 있거나 이 객체가 본 적 있는 id면 지워진 것이다
            if item_id <= self._max_seen_id or self._queue.list_items(item_id, 1):
                raise Empty from None
            return None  # 아직 만들어지지 않았을 수 있다

    def _saw(self, item_id: int) -> None:
        with self._lock:
            self._max_seen_id = max(self._max_seen_id, item_id)

    def _now_ms(self) -> int:
        return int(self._clock()) if self._clock is not None else int(time.time() * 1000)

    def _hold(self, claim: Claim, data: Any) -> None:
        """ack를 기다리는 claim을 기억한다. 상한을 넘으면 만료된 것부터, 그다음 오래된 것부터 잊는다."""
        with self._lock:
            self._claims.pop(claim.id, None)
            self._claims[claim.id] = (claim, data)
            if len(self._claims) <= _MAX_HELD_CLAIMS:
                return
            now = self._now_ms()
            for key in [key for key, (held, _) in self._claims.items() if held.lease_until_ms <= now]:
                if key != claim.id:
                    del self._claims[key]
            while len(self._claims) > _MAX_HELD_CLAIMS:
                del self._claims[next(iter(self._claims))]

    def _deliver(self, claim: Claim, *, raw: bool) -> Any:
        data = self._serializer.loads(claim.payload)
        self._saw(claim.id)
        self._hold(claim, data)
        if not raw:
            return data
        created_ms = self._queue.list_items(claim.id - 1, 1)[0].created_at_ms  # 전체 스캔 한 번
        return {"pqid": claim.id, "data": data, "timestamp": created_ms / 1000}

    def _resolve(self, item: Any, item_id: int | None) -> ClaimToken:
        if item_id is not None and item is not None:
            raise ValueError("Specify an id or an item, not both.")
        if item_id is None and item is None:
            raise ValueError("Specify an id or an item.")
        with self._lock:
            if item_id is None and isinstance(item, dict) and "pqid" in item:
                item_id = int(item["pqid"])
            if item_id is None:
                item_id = next((key for key, (_, data) in self._claims.items() if data is item), None)
            held = self._claims.get(item_id) if item_id is not None else None
        if held is None:
            raise UnknownClaimError(f"no claim held for id {item_id}; get it first")
        return ClaimToken(held[0].id, held[0].fence)

    def _forget(self, item_id: int) -> None:
        with self._lock:
            self._claims.pop(item_id, None)

    def _row(self, info: ItemInfo) -> dict[str, Any]:
        data = self._serializer.loads(info.payload or b"")
        return {"id": info.id, "data": data, "timestamp": info.created_at_ms / 1000, "status": _status_code(info)}


def _status_code(info: ItemInfo) -> str:
    if info.state is ItemState.READY:
        return "1" if info.attempts > 0 else "0"
    return _STATUS_CODES[info.state]
