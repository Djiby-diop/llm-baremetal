#!/usr/bin/env python3
"""Inspect llm-baremetal OO save files (OO1..OO4).

This is a host-side helper for debugging /oo_save outputs without booting.
It validates the optional CRC32 integrity line (OO3+) and prints entities and
agenda items (OO4 includes state+priority).

Usage:
  python tools/oo_inspect.py path/to/oo_save.txt
  python tools/oo_inspect.py --full path/to/oo_save.txt
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import sys
import zlib


@dataclasses.dataclass
class AgendaItem:
    state: int = 0
    prio: int = 0
    text: str = ""


@dataclasses.dataclass
class Entity:
    id: int
    energy: int
    ticks: int
    status: int
    goal: str
    digest: str
    notes: str
    agenda: list[AgendaItem]


def _read_line(buf: bytes, i: int) -> tuple[bytes, int]:
    if i >= len(buf):
        return b"", i
    j = buf.find(b"\n", i)
    if j < 0:
        return buf[i:], len(buf)
    return buf[i:j], j + 1


def _parse_u32(line: bytes, key: bytes) -> int:
    if not line.startswith(key + b"="):
        raise ValueError(f"expected {key.decode()}=..., got: {line!r}")
    v = line[len(key) + 1 :].strip()
    if not v or any(c < 48 or c > 57 for c in v):
        raise ValueError(f"invalid {key.decode()} value: {v!r}")
    return int(v)


def _parse_i32(line: bytes, key: bytes) -> int:
    if not line.startswith(key + b"="):
        raise ValueError(f"expected {key.decode()}=..., got: {line!r}")
    v = line[len(key) + 1 :].strip()
    if not v:
        raise ValueError(f"invalid {key.decode()} value: {v!r}")
    s = v.decode("ascii", errors="strict")
    # match llmk_oo_parse_kv_i32: optional leading '-', digits only
    if s[0] == "-":
        if len(s) == 1 or not s[1:].isdigit():
            raise ValueError(f"invalid {key.decode()} value: {s!r}")
    else:
        if not s.isdigit():
            raise ValueError(f"invalid {key.decode()} value: {s!r}")
    return int(s)


def _read_bytes_blob(buf: bytes, i: int, length: int) -> tuple[bytes, int]:
    end = i + length
    if end > len(buf):
        raise ValueError("truncated blob")
    blob = buf[i:end]
    i = end
    if i < len(buf) and buf[i : i + 1] == b"\n":
        i += 1
    return blob, i


def _status_name(v: int) -> str:
    # Matches llmk_oo_status_name intent (we keep it generic).
    return {0: "IDLE", 1: "RUN", 2: "SLEEP", 3: "DEAD"}.get(v, str(v))


def _action_state_name(v: int) -> str:
    return {0: "TODO", 1: "DOING", 2: "DONE"}.get(v, str(v))


def _find_crc_region(buf: bytes) -> tuple[int, int, int] | None:
    """Return (payload_start, crc_line_start, expected_crc) if crc32= exists."""
    header_end = buf.find(b"\n")
    if header_end < 0:
        return None
    payload_start = header_end + 1

    # crc32 line is at line start.
    crc_line_start = -1
    if buf[payload_start : payload_start + 6] == b"crc32=":
        crc_line_start = payload_start
    else:
        j = buf.find(b"\ncrc32=", payload_start)
        if j >= 0:
            crc_line_start = j + 1

    if crc_line_start < 0:
        return None

    line, _ = _read_line(buf, crc_line_start)
    if not line.startswith(b"crc32=") or len(line) < 6 + 8:
        raise ValueError("invalid crc32 line")
    hex8 = line[6:14].decode("ascii", errors="strict")
    try:
        expected = int(hex8, 16)
    except ValueError as e:
        raise ValueError("invalid crc32 hex") from e

    return payload_start, crc_line_start, expected


def parse_oo(buf: bytes) -> tuple[int, list[Entity]]:
    i = 0
    header, i = _read_line(buf, i)
    if header not in (b"OO1", b"OO2", b"OO3", b"OO4"):
        raise ValueError(f"unknown header: {header!r}")
    version = int(header[-1:])

    crc = _find_crc_region(buf)
    if version >= 3 and crc is not None:
        payload_start, crc_line_start, expected = crc
        got = zlib.crc32(buf[payload_start:crc_line_start]) & 0xFFFFFFFF
        if got != expected:
            raise ValueError(f"CRC mismatch: expected {expected:08x}, got {got:08x}")

    entities: list[Entity] = []

    while i < len(buf):
        line, i2 = _read_line(buf, i)
        i = i2
        if line == b"" and i >= len(buf):
            break
        if line == b"DONE":
            break
        if line.startswith(b"crc32="):
            # CRC line appears near the end; ignore here.
            continue
        if line != b"BEGIN":
            # Forward compatibility: skip unknown lines.
            continue

        ent_id = _parse_u32(_read_line(buf, i)[0], b"id")
        _, i = _read_line(buf, i)
        energy = _parse_u32(_read_line(buf, i)[0], b"energy")
        _, i = _read_line(buf, i)
        ticks = _parse_u32(_read_line(buf, i)[0], b"ticks")
        _, i = _read_line(buf, i)
        status = _parse_u32(_read_line(buf, i)[0], b"status")
        _, i = _read_line(buf, i)

        goal_len = _parse_u32(_read_line(buf, i)[0], b"goal_len")
        _, i = _read_line(buf, i)
        goal_b, i = _read_bytes_blob(buf, i, goal_len)

        digest_len = _parse_u32(_read_line(buf, i)[0], b"digest_len")
        _, i = _read_line(buf, i)
        digest_b, i = _read_bytes_blob(buf, i, digest_len)

        notes_len = _parse_u32(_read_line(buf, i)[0], b"notes_len")
        _, i = _read_line(buf, i)
        notes_b, i = _read_bytes_blob(buf, i, notes_len)

        agenda: list[AgendaItem] = []

        # Optional agenda (OO2+). Peek next line.
        line, peek_i = _read_line(buf, i)
        if version >= 2 and line.startswith(b"agenda_count="):
            agenda_count = _parse_u32(line, b"agenda_count")
            i = peek_i
            for _ in range(agenda_count):
                state = 0
                prio = 0
                if version >= 4:
                    state_line, i = _read_line(buf, i)
                    state = _parse_u32(state_line, b"agenda_state")
                    prio_line, i = _read_line(buf, i)
                    prio = _parse_i32(prio_line, b"agenda_prio")

                alen_line, i = _read_line(buf, i)
                alen = _parse_u32(alen_line, b"agenda_len")
                text_b, i = _read_bytes_blob(buf, i, alen)
                agenda.append(
                    AgendaItem(state=int(state), prio=int(prio), text=text_b.decode("utf-8", errors="replace"))
                )

        end_line, i = _read_line(buf, i)
        if end_line != b"END":
            raise ValueError(f"expected END, got: {end_line!r}")

        entities.append(
            Entity(
                id=int(ent_id),
                energy=int(energy),
                ticks=int(ticks),
                status=int(status),
                goal=goal_b.decode("utf-8", errors="replace"),
                digest=digest_b.decode("utf-8", errors="replace"),
                notes=notes_b.decode("utf-8", errors="replace"),
                agenda=agenda,
            )
        )

    return version, entities


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("path", type=pathlib.Path)
    p.add_argument("--full", action="store_true", help="print full digest + notes")
    args = p.parse_args(argv)

    buf = args.path.read_bytes()
    version, entities = parse_oo(buf)

    print(f"OO version: {version}")
    print(f"Entities: {len(entities)}")

    for e in entities:
        print("-")
        print(f"  id={e.id} status={_status_name(e.status)} energy={e.energy} ticks={e.ticks}")
        print(f"  goal: {e.goal}")
        print(f"  agenda: {len(e.agenda)} item(s)")
        for k, a in enumerate(e.agenda, start=1):
            print(f"    {k:02d} [{_action_state_name(a.state)}] p={a.prio:+d}  {a.text}")

        if args.full:
            print("  digest:")
            for line in e.digest.splitlines() or [""]:
                print(f"    {line}")
            print("  notes:")
            for line in e.notes.splitlines() or [""]:
                print(f"    {line}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
