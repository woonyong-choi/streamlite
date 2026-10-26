"""libstreamlite 공유 라이브러리를 ctypes로 연결한다(선언은 include/streamlite.h와 같다)."""

from __future__ import annotations

import ctypes
import os
import sys
from collections.abc import Iterator
from pathlib import Path

__all__ = ["CLOCK_FN", "LIST_NOTE_BYTES", "Item", "ListItem", "Param", "QueueOptions", "load_library"]

CLOCK_FN = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_void_p)

LIST_NOTE_BYTES = 201


class Param(ctypes.Structure):
    """SQL 매개변수 하나(streamlite_param_t)."""

    _fields_ = [
        ("type", ctypes.c_int32),
        ("integer", ctypes.c_int64),
        ("real", ctypes.c_double),
        ("bytes", ctypes.c_void_p),
        ("size", ctypes.c_size_t),
    ]


class QueueOptions(ctypes.Structure):
    """큐 옵션(streamlite_queue_options_t). 0이면 기본값이다."""

    _fields_ = [
        ("max_attempts", ctypes.c_uint32),
        ("max_payload_bytes", ctypes.c_size_t),
        ("idem_retention_ms", ctypes.c_uint64),
        ("clock", CLOCK_FN),
        ("clock_ctx", ctypes.c_void_p),
    ]


class Item(ctypes.Structure):
    """claim한 작업(streamlite_queue_item_t)."""

    _fields_ = [
        ("id", ctypes.c_uint64),
        ("fence", ctypes.c_uint64),
        ("attempts", ctypes.c_uint32),
        ("lease_until_ms", ctypes.c_uint64),
        ("payload", ctypes.c_void_p),
        ("payload_size", ctypes.c_size_t),
    ]


class ListItem(ctypes.Structure):
    """목록의 작업 한 건(streamlite_queue_list_item_t)."""

    _fields_ = [
        ("id", ctypes.c_uint64),
        ("state", ctypes.c_int32),
        ("fence", ctypes.c_uint64),
        ("attempts", ctypes.c_uint32),
        ("ready_at_ms", ctypes.c_uint64),
        ("lease_until_ms", ctypes.c_uint64),
        ("created_at_ms", ctypes.c_uint64),
        ("note", ctypes.c_char * LIST_NOTE_BYTES),
        ("payload", ctypes.c_void_p),
        ("payload_size", ctypes.c_size_t),
    ]


def load_library() -> ctypes.CDLL:
    """공유 라이브러리를 찾아 열고 함수 시그니처를 선언한다.

    Raises:
        OSError: STREAMLITE_LIB, 설치한 패키지 폴더, 저장소 빌드 위치 어디에도 라이브러리가 없을 때.
    """
    tried: list[str] = []
    for path in _candidates():
        tried.append(str(path))
        if path.exists():
            return _declare(ctypes.CDLL(str(path)))
    raise OSError("libstreamlite not found (set STREAMLITE_LIB or run make); tried: " + ", ".join(tried))


def _candidates() -> Iterator[Path]:
    env = os.environ.get("STREAMLITE_LIB")
    if env:
        yield Path(env)
    name = "libstreamlite.dylib" if sys.platform == "darwin" else "libstreamlite.so"
    package_dir = Path(__file__).resolve().parent
    yield package_dir / name
    yield package_dir.parents[1] / "build" / "lib" / name


_INT_FUNCTIONS = (
    "streamlite_open",
    "streamlite_close",
    "streamlite_exec",
    "streamlite_begin",
    "streamlite_commit",
    "streamlite_rollback",
    "streamlite_txn_failed",
    "streamlite_queue_open",
    "streamlite_queue_put",
    "streamlite_queue_get",
    "streamlite_queue_ack",
    "streamlite_queue_extend",
    "streamlite_queue_fail",
    "streamlite_queue_nack",
    "streamlite_queue_update",
    "streamlite_queue_recover",
    "streamlite_queue_release_leases",
    "streamlite_queue_purge",
    "streamlite_queue_stats",
    "streamlite_queue_list",
)


def _declare(lib: ctypes.CDLL) -> ctypes.CDLL:
    c = ctypes
    ptr, u64, size = c.c_void_p, c.c_uint64, c.c_size_t
    pptr = c.POINTER(c.c_void_p)
    lib.streamlite_status_name.restype = c.c_char_p
    lib.streamlite_status_name.argtypes = [c.c_int]
    lib.streamlite_version.restype = c.c_char_p
    lib.streamlite_version.argtypes = []
    lib.streamlite_free.argtypes = [ptr]
    lib.streamlite_set_max_result_bytes.argtypes = [size]
    lib.streamlite_max_result_bytes.restype = size
    lib.streamlite_open.argtypes = [c.c_char_p, c.c_int, pptr]
    lib.streamlite_close.argtypes = [pptr]
    lib.streamlite_cancel.argtypes = [ptr]
    lib.streamlite_cancel.restype = None
    lib.streamlite_exec.argtypes = [ptr, ptr, c.c_char_p, c.POINTER(Param), size, pptr, c.POINTER(size)]
    lib.streamlite_begin.argtypes = [ptr, pptr]
    lib.streamlite_commit.argtypes = [pptr]
    lib.streamlite_rollback.argtypes = [pptr]
    lib.streamlite_txn_failed.argtypes = [ptr]
    lib.streamlite_queue_open.argtypes = [ptr, c.c_char_p, c.c_int, c.POINTER(QueueOptions), pptr]
    lib.streamlite_queue_close.argtypes = [pptr]
    lib.streamlite_queue_put.argtypes = [ptr, ptr, c.c_char_p, size, c.c_char_p, c.POINTER(u64), c.POINTER(c.c_int)]
    lib.streamlite_queue_get.argtypes = [ptr, ptr, u64, u64, c.POINTER(Item)]
    lib.streamlite_queue_item_free.argtypes = [c.POINTER(Item)]
    lib.streamlite_queue_ack.argtypes = [ptr, ptr, u64, u64]
    lib.streamlite_queue_extend.argtypes = [ptr, ptr, u64, u64, u64, c.POINTER(u64)]
    lib.streamlite_queue_fail.argtypes = [ptr, ptr, u64, u64, c.c_char_p]
    lib.streamlite_queue_nack.argtypes = [ptr, ptr, u64, u64, u64, c.c_char_p, c.POINTER(c.c_int)]
    lib.streamlite_queue_update.argtypes = [ptr, ptr, u64, u64, c.c_char_p, size]
    lib.streamlite_queue_recover.argtypes = [ptr, ptr, size, c.POINTER(size)]
    lib.streamlite_queue_release_leases.argtypes = [ptr, ptr, size, c.POINTER(size)]
    lib.streamlite_queue_purge.argtypes = [ptr, ptr, c.c_int, size, c.POINTER(size)]
    lib.streamlite_queue_stats.argtypes = [ptr, ptr, c.POINTER(u64)]
    lib.streamlite_queue_list.argtypes = [ptr, ptr, u64, size, c.c_int, c.POINTER(ListItem), c.POINTER(size)]
    lib.streamlite_queue_list_free.argtypes = [c.POINTER(ListItem), size]
    for name in _INT_FUNCTIONS:
        getattr(lib, name).restype = c.c_int
    return lib
