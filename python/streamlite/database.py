"""DB 단독 사용: database 열기, SQL 실행, 여러 문장 트랜잭션. 큐 모듈 없이 쓸 수 있다."""

from __future__ import annotations

import ctypes
import os
import struct
import threading
import time
import weakref
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from types import TracebackType
from typing import Protocol, Union

from streamlite._native import OpenOptions, Param, load_library
from streamlite.errors import (
    STATUS_BUSY,
    BusyError,
    InvalidArgumentError,
    SqlError,
    raise_for_status,
)

__all__ = ["Database", "NativeDatabase", "Result", "SqlValue", "Transaction"]

SqlValue = Union[None, int, float, str, bytes]  # noqa: UP007 - 실행 시점에 평가되므로 3.9에서는 `|`를 못 쓴다

DEFAULT_BUSY_TIMEOUT_S = 5.0
# commit 동기화 단계(include/streamlite.h의 STREAMLITE_DURABILITY_*)
DURABILITY_LEVELS = {"normal": 0, "full": 1, "off": 2}
_PARAM_NULL, _PARAM_INTEGER, _PARAM_REAL, _PARAM_TEXT, _PARAM_BLOB = 0, 1, 2, 3, 4
_KIND_ROWS, _KIND_COMMAND, _KIND_ERROR = 0, 1, 2
_INT64_MIN, _INT64_MAX = -(2**63), 2**63 - 1
_RESULT_E_BUSY = 7  # result.h의 오류 분류: 다른 트랜잭션이 쓰는 중이라 실행 전에 거절됨
RESULT_E_CANCELLED = 8  # result.h의 오류 분류: 취소된 문장

_lib = load_library()


class Closable(Protocol):
    """DB에 등록해 DB보다 먼저 닫을 수 있는 객체."""

    def close(self) -> None:
        """자원을 놓는다."""


_TYPE_NAMES = {1: "INTEGER", 2: "REAL", 3: "TEXT", 4: "BLOB"}


@dataclass(frozen=True)
class Column:
    """결과 열 하나. type은 INTEGER·REAL·TEXT·BLOB이며 식의 형식을 정하지 못했으면 None이다."""

    name: str
    type: str | None
    nullable: bool


@dataclass(frozen=True)
class Result:
    """문장 실행 결과. 행이 있으면 column_info·rows, 명령이면 tag·affected_rows."""

    column_info: tuple[Column, ...] = ()
    rows: tuple[tuple[SqlValue, ...], ...] = ()
    tag: str = ""
    affected_rows: int = 0

    @property
    def columns(self) -> tuple[str, ...]:
        """열 이름."""
        return tuple(column.name for column in self.column_info)


def status_name(status: int) -> str:
    """상태 값의 이름(메시지용)."""
    return str(_lib.streamlite_status_name(status).decode("ascii"))


def check(status: int) -> None:
    """C 상태가 성공이 아니면 대응하는 예외를 던진다."""
    raise_for_status(status, status_name(status))


_SELF_BLOCKED = (
    "this thread holds an open transaction on the same Database, so the call cannot get the writer; "
    "pass txn=<that transaction> or commit/rollback it first"
)


class NativeDatabase:
    """Database 객체 없이도 종료자가 정리할 수 있게 따로 둔 native 상태.

    종료자(weakref.finalize)는 Database·Queue·Transaction 객체를 참조하면 안 되므로(참조하면 수거되지 않는다) 핸들과
    표시만 여기에 두고 나눠 쓴다. handle은 Database가 쥔 것과 같은 c_void_p 객체라 닫히면 양쪽에서 함께 비워진다.
    """

    def __init__(self, handle: ctypes.c_void_p) -> None:
        self.handle = handle
        self.abandoned = False  # close 없이 Database 객체가 사라졌다
        self.txn_threads: dict[int, int] = {}  # 열린 트랜잭션(핸들 객체의 id) -> 연 스레드
        self._closing = threading.RLock()

    def holds_transaction(self) -> bool:
        """지금 스레드가 연 트랜잭션이 이 DB에 남아 있는지."""
        return threading.get_ident() in list(self.txn_threads.values())  # list()는 다른 스레드의 변경과 겹치지 않는다

    def close_if_abandoned(self) -> None:
        """Database 객체가 사라졌으면 DB를 닫는다. 큐·트랜잭션이 남아 BUSY면 그 종료자가 끝난 뒤 다시 부른다."""
        with self._closing:  # 두 종료자가 같은 핸들을 동시에 닫지 않게 한다. 같은 스레드의 재진입은 허용한다
            if self.abandoned and self.handle:
                _lib.streamlite_close(ctypes.byref(self.handle))

    def finalize(self, handle: ctypes.c_void_p | None, close: Callable[..., object] | None) -> None:
        """종료자 본문. 남은 핸들을 close로 끝내고, 버려진 DB를 닫는다. 수거·인터프리터 종료 중이라 예외를 내지 않는다."""
        try:
            if handle is not None and close is not None:
                self.txn_threads.pop(id(handle), None)
                if handle:
                    close(ctypes.byref(handle))
            else:
                self.abandoned = True
            self.close_if_abandoned()
        except Exception:  # noqa: BLE001, S110 - 종료자에서는 알릴 곳이 없다
            pass


_STATUS_CANCELLED = 16  # streamlite.h의 STREAMLITE_CANCELLED


def retry_busy(call: Callable[[], int], timeout_s: float, cancelled: Callable[[], bool] = lambda: False) -> int:
    """BUSY인 동안 잠깐씩 쉬며 다시 부른다. C 호출은 잠금을 쥔 채 기다리지 않으므로 기다림은 여기서 한다.

    쉬는 사이에 cancelled()가 참이 되면 다시 부르지 않고 취소 상태를 돌려준다.
    """
    deadline = time.monotonic() + timeout_s
    delay = 0.0005
    while True:
        status = call()
        if status != STATUS_BUSY or time.monotonic() >= deadline:
            return status
        time.sleep(delay)
        if cancelled():
            return _STATUS_CANCELLED
        delay = min(delay * 2, 0.02)


def _pack_param(value: SqlValue, keep: list[object]) -> Param:
    if value is None:
        return Param(_PARAM_NULL, 0, 0.0, None, 0)
    if isinstance(value, int):
        if not _INT64_MIN <= int(value) <= _INT64_MAX:
            raise InvalidArgumentError(f"integer out of int64 range: {value}")
        return Param(_PARAM_INTEGER, int(value), 0.0, None, 0)
    if isinstance(value, float):
        return Param(_PARAM_REAL, 0, value, None, 0)
    if isinstance(value, str):
        try:
            raw = value.encode("utf-8")
        except UnicodeEncodeError as err:
            raise InvalidArgumentError("text is not valid UTF-8") from err
        kind = _PARAM_TEXT
    else:
        raw = bytes(value)
        kind = _PARAM_BLOB
    keep.append(raw)
    address = ctypes.cast(ctypes.c_char_p(raw), ctypes.c_void_p).value if raw else None
    return Param(kind, 0, 0.0, address, len(raw))


def _pack_params(params: Sequence[SqlValue]) -> tuple[ctypes.Array[Param] | None, list[object]]:
    keep: list[object] = []
    if not params:
        return None, keep
    array = (Param * len(params))(*[_pack_param(value, keep) for value in params])
    return array, keep


class _Reader:
    """인코딩된 결과 버퍼를 앞에서부터 읽는다(little-endian)."""

    def __init__(self, data: bytes) -> None:
        self._data = data
        self._at = 0

    def unpack(self, fmt: str) -> int | float:
        (value,) = struct.unpack_from(fmt, self._data, self._at)
        self._at += struct.calcsize(fmt)
        return value  # type: ignore[no-any-return]

    def take(self, size: int) -> bytes:
        chunk = self._data[self._at : self._at + size]
        self._at += size
        return chunk

    def text(self) -> str:
        return self.take(int(self.unpack("<I"))).decode("utf-8")


def _decode_cell(reader: _Reader) -> SqlValue:
    kind = int(reader.unpack("<B"))
    if kind == _PARAM_NULL:
        return None
    if kind == _PARAM_INTEGER:
        return int(reader.unpack("<q"))
    if kind == _PARAM_REAL:
        return float(reader.unpack("<d"))
    payload = reader.take(int(reader.unpack("<I")))
    return payload.decode("utf-8") if kind == _PARAM_TEXT else payload


def decode_result(data: bytes) -> Result:
    """streamlite_exec가 만든 버퍼를 Result로 바꾼다.

    Raises:
        SqlError: 버퍼가 오류 결과일 때.
    """
    reader = _Reader(data)
    kind = int(reader.unpack("<B"))
    if kind == _KIND_COMMAND:
        tag = reader.text()
        return Result(tag=tag, affected_rows=int(reader.unpack("<Q")))
    if kind == _KIND_ERROR:
        error_class = int(reader.unpack("<I"))
        error_code = int(reader.unpack("<i"))
        raise SqlError(
            reader.text() or f"sql error class {error_class}", error_class=error_class, error_code=error_code
        )
    columns = []
    for _ in range(int(reader.unpack("<I"))):
        name = reader.text()
        kind = int(reader.unpack("<B"))
        columns.append(Column(name, _TYPE_NAMES.get(kind), bool(reader.unpack("<B"))))
    rows: list[tuple[SqlValue, ...]] = []
    while int(reader.unpack("<B")) == 1:
        rows.append(tuple(_decode_cell(reader) for _ in columns))
    return Result(column_info=tuple(columns), rows=tuple(rows))


def run_sql(
    db_handle: ctypes.c_void_p, txn_handle: ctypes.c_void_p | None, sql: str, params: Sequence[SqlValue]
) -> Result:
    """문장 하나를 실행한다. 오류 결과는 SqlError로 던진다."""
    array, keep = _pack_params(params)
    out = ctypes.c_void_p()
    size = ctypes.c_size_t()
    status = _lib.streamlite_exec(
        db_handle, txn_handle, sql.encode("utf-8"), array, len(params), ctypes.byref(out), ctypes.byref(size)
    )
    del keep
    check(status)
    try:
        return decode_result(ctypes.string_at(out, size.value))
    finally:
        _lib.streamlite_free(out)


class Database:
    """독립 DB 하나. 큐를 설치하지 않으면 테이블도 객체도 생기지 않는다."""

    def __init__(
        self,
        path: str | os.PathLike[str],
        *,
        create: bool = True,
        busy_timeout_s: float = DEFAULT_BUSY_TIMEOUT_S,
        durability: str = "normal",
    ) -> None:
        """path의 DB를 연다.

        durability는 commit 동기화 단계다. 어느 단계든 프로세스가 죽은 뒤에는 commit 전부 또는 전무로 다시 열린다.
        "normal"(기본)은 commit마다 fsync(2)라 OS 장애 뒤에도 남는다(Apple 플랫폼은 장치 캐시를 비우지 않아 전원
        차단 뒤 최근 commit을 잃을 수 있다). "full"은 Apple 플랫폼에서 F_FULLFSYNC를 쓴다. "off"는 commit에서
        동기화하지 않고 close에서 한 번 동기화하므로 OS 장애·전원 차단 뒤 최근 commit을 잃거나 파일이 손상으로 열릴 수 있다.

        Raises:
            InvalidArgumentError: durability가 "normal"·"full"·"off"가 아닐 때.
        """
        level = DURABILITY_LEVELS.get(durability)
        if level is None:
            raise InvalidArgumentError(f"durability must be one of {sorted(DURABILITY_LEVELS)}: {durability!r}")
        handle = ctypes.c_void_p()
        options = OpenOptions(level, 0)  # BUSY 대기는 이 객체가 busy_timeout_s로 한다
        check(_lib.streamlite_open_ex(os.fsencode(path), 1 if create else 0, ctypes.byref(options), ctypes.byref(handle)))
        self._handle: ctypes.c_void_p | None = handle
        self._queues: set[Closable]
        self.busy_timeout_s = busy_timeout_s
        self._cancel_epoch = 0  # cancel마다 1씩 오른다. BUSY 대기 중의 취소를 알아차리는 데 쓴다
        # close 없이 참조를 잃거나 인터프리터가 끝나면 큐·트랜잭션의 종료자와 함께 native 자원을 놓는다
        self.native = NativeDatabase(handle)
        self._finalizer = weakref.finalize(self, self.native.finalize, None, None)

    @property
    def handle(self) -> ctypes.c_void_p:
        """C 핸들. 닫힌 DB면 오류다. 종료자가 닫아 값이 NULL이 된 핸들도 닫힌 것으로 본다."""
        if not self._handle:
            raise InvalidArgumentError("database is closed")
        return self._handle

    def execute(self, sql: str, params: Sequence[SqlValue] = ()) -> Result:
        """문장 하나를 자동 commit으로 실행한다.

        다른 트랜잭션이 쓰는 중이라 변경 문장이 BUSY로 거절되면(실행 전 거절이라 반영된 것이 없다)
        busy_timeout_s까지 다시 시도한다. C 호출은 잠금을 쥔 채 기다리지 않으므로 기다림은 여기서 한다.
        """
        deadline = time.monotonic() + self.busy_timeout_s
        delay = 0.0005
        epoch = self._cancel_epoch
        while True:
            try:
                return run_sql(self.handle, None, sql, params)
            except SqlError as err:
                if err.error_class != _RESULT_E_BUSY:
                    raise
                if self.native.holds_transaction():  # 기다려도 풀리지 않는다
                    raise SqlError(
                        f"{err}; {_SELF_BLOCKED}", error_class=err.error_class, error_code=err.error_code
                    ) from err
                if time.monotonic() >= deadline:
                    raise
            time.sleep(delay)
            if self._cancel_epoch != epoch:  # 기다리는 동안 취소됐다. 실행 중 취소와 같은 결과로 알린다
                raise SqlError("statement was cancelled while waiting for another writer", error_class=RESULT_E_CANCELLED, error_code=0)
            delay = min(delay * 2, 0.02)

    def begin(self) -> Transaction:
        """쓰기 트랜잭션을 시작한다. 다른 writer가 있으면 busy_timeout_s까지 기다린다.

        Raises:
            BusyError: 기다린 시간 안에 시작하지 못했을 때. 지금 스레드가 이 DB에 연 트랜잭션이 남아 있으면 기다리지 않는다.
        """
        out = ctypes.c_void_p()
        check(self.wait_for_writer(lambda: _lib.streamlite_begin(self.handle, ctypes.byref(out))))
        return Transaction(self, out)

    def wait_for_writer(self, call: Callable[[], int]) -> int:
        """call이 BUSY인 동안 busy_timeout_s까지 다시 부르고 마지막 상태를 돌려준다.

        Raises:
            BusyError: 끝까지 BUSY일 때. 지금 스레드가 이 DB에 연 트랜잭션이 writer를 쥐고 있으면 기다려도 풀리지
                않으므로 바로 던지고 원인을 메시지에 적는다.
        """
        epoch = self._cancel_epoch
        status = call()
        if status != STATUS_BUSY:
            return status
        if self.native.holds_transaction():
            raise BusyError(_SELF_BLOCKED, status=status)
        status = retry_busy(call, self.busy_timeout_s, lambda: self._cancel_epoch != epoch)
        if status == _STATUS_CANCELLED:
            check(status)
        if status == STATUS_BUSY:
            raise BusyError("another writer holds the database", status=status)
        return status

    def cancel(self) -> None:
        """다른 스레드에서 지금 실행 중인 문장이나 큐 연산 하나를 취소한다.

        취소된 문장은 SqlError(error_class=RESULT_E_CANCELLED)이고, 큐 연산은 CancelledError이며 자기 트랜잭션을
        반영하지 않는다. 게시를 시작한 문장은 취소와 무관하게 끝난다. 끝나는 순간과 겹친 요청은 같은 세션의
        다음 문장 하나에 적용될 수 있다. 다른 writer를 기다리는 중인 호출(busy_timeout_s)도 쉬는 사이에 멈추고
        같은 방식으로 취소를 알린다.
        """
        self._cancel_epoch += 1
        _lib.streamlite_cancel(self.handle)

    def register_queue(self, queue: Closable) -> None:
        """이 DB를 쓰는 큐를 등록한다. DB를 닫을 때 먼저 닫힌다(큐는 DB보다 오래 살 수 없다)."""
        queues = getattr(self, "_queues", None)
        if queues is None:
            queues = self.__dict__.setdefault("_queues", set())
        queues.add(queue)

    def unregister_queue(self, queue: Closable) -> None:
        """큐가 스스로 닫혔다."""
        queues = getattr(self, "_queues", None)
        if queues is not None:
            queues.discard(queue)

    def close(self) -> None:
        """등록된 큐를 닫고 DB를 닫는다. 열린 트랜잭션이 있으면 BusyError이며 DB는 열려 있고 큐만 닫혀 있다."""
        if not self._handle:  # 종료자가 이미 닫았으면 NULL이다
            self._handle = None
            self._finalizer.detach()
            return
        queues = getattr(self, "_queues", None)
        if queues is not None:
            for queue in list(queues):
                queue.close()
        status = _lib.streamlite_close(ctypes.byref(self._handle))
        check(status)
        self._handle = None
        self._finalizer.detach()

    def __enter__(self) -> Database:
        return self

    def __exit__(
        self, exc_type: type[BaseException] | None, exc: BaseException | None, traceback: TracebackType | None
    ) -> None:
        self.close()


class Transaction:
    """여러 문장을 하나로 확정하거나 모두 버린다. with 블록은 예외가 없으면 commit, 있으면 rollback한다.

    commit·rollback 없이 참조를 잃으면 수거될 때 rollback된다. 수거 시점은 정해져 있지 않으므로 writer를 제때 놓으려면
    with 블록이나 명시적 commit·rollback을 쓴다. 인터프리터 종료 때는 rollback하지 않는다. 그때 다른 스레드가 아직 쓰고
    있을 수 있고, commit하지 않은 변경은 프로세스가 끝나면 게시되지 않는다.
    """

    def __init__(self, db: Database, handle: ctypes.c_void_p) -> None:
        self.db = db
        self._handle: ctypes.c_void_p | None = handle
        self._native = db.native
        self._native.txn_threads[id(handle)] = threading.get_ident()
        self._finalizer = weakref.finalize(self, self._native.finalize, handle, _lib.streamlite_rollback)
        self._finalizer.atexit = False

    @property
    def handle(self) -> ctypes.c_void_p:
        """C 핸들. commit·rollback한 뒤와 종료자가 rollback해 값이 NULL이 된 뒤에는 오류다.

        NULL 핸들을 C에 넘기면 트랜잭션 없는 자동 commit으로 실행되므로 넘기지 않는다.
        """
        if not self._handle:
            raise InvalidArgumentError("transaction is finished")
        return self._handle

    @property
    def is_failed(self) -> bool:
        """문장 오류로 실패 상태이면 commit할 수 없고 rollback만 된다."""
        return bool(self._handle) and bool(_lib.streamlite_txn_failed(self._handle))

    def execute(self, sql: str, params: Sequence[SqlValue] = ()) -> Result:
        """이 트랜잭션 안에서 문장 하나를 실행한다."""
        return run_sql(self.db.handle, self.handle, sql, params)

    def commit(self) -> None:
        """확정한다. 결과를 알 수 없으면 UncertainError이며 성공으로 취급하면 안 된다."""
        handle = self._release()
        check(_lib.streamlite_commit(ctypes.byref(handle)))

    def rollback(self) -> None:
        """모두 버린다."""
        handle = self._release()
        check(_lib.streamlite_rollback(ctypes.byref(handle)))

    def _release(self) -> ctypes.c_void_p:
        """핸들을 꺼내고 이 객체를 끝난 상태로 만든다. 그 뒤의 commit·rollback 한 번이 핸들을 소비한다."""
        handle, self._handle = self.handle, None
        self._finalizer.detach()
        self._native.txn_threads.pop(id(handle), None)
        return handle

    def __enter__(self) -> Transaction:
        return self

    def __exit__(
        self, exc_type: type[BaseException] | None, exc: BaseException | None, traceback: TracebackType | None
    ) -> None:
        if self._handle is None or (not self._handle and exc_type is not None):
            return  # 종료자가 이미 rollback했으면 원래 예외를 가리지 않는다
        if exc_type is None and not self.is_failed:
            self.commit()
        else:
            self.rollback()
