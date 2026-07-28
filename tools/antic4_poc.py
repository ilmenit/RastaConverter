#!/usr/bin/env python3
"""Small ANTIC mode 4 renderer check against AltirraBridge.

This is intentionally a proof of concept, not a second emulator.  It exercises
random mode 4 character data, the bit-7 PF2/PF3 attribute, four randomized
players, and a WSYNC-delimited randomized color-register program.
"""

from __future__ import annotations

import argparse
import json
import random
import subprocess
import sys
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


WIDTH = 160
HEIGHT = 240
DL_ADDR = 0x1000
SCREEN_ADDR = 0x2000
FONT_ADDR = 0x3000
PMBASE = 0x40
CODE_ADDR = 0x5000

COLOR_REGS = tuple(range(0xD012, 0xD01B))
PF_REGS = (0xD016, 0xD017, 0xD018, 0xD019)
BK_REG = 0xD01A
PM_REGS = (0xD012, 0xD013, 0xD014, 0xD015)


@dataclass(frozen=True)
class Insn:
    op: str
    value: int = 0


@dataclass
class Case:
    seed: int
    screen: bytes
    font: bytes
    players: tuple[bytes, bytes, bytes, bytes]
    hpos: tuple[int, int, int, int]
    initial_colors: dict[int, int]
    lines: list[list[Insn]]


def make_case(seed: int) -> Case:
    rng = random.Random(seed)
    hpos = (56, 96, 136, 176)

    # Keep the playfield at background under each player.  This leaves player
    # priority out of the first PoC while still checking all four DMA streams,
    # shapes, positions, and mid-line player color changes.
    protected_cells: set[int] = set()
    for pos in hpos:
        x0 = pos - 48
        protected_cells.update(range(x0 // 4, (x0 + 7) // 4 + 1))

    font = bytearray(1024)
    for i in range(8, len(font)):
        font[i] = rng.randrange(256)

    screen = bytearray()
    for _row in range(HEIGHT // 8):
        for cell in range(40):
            if cell in protected_cells:
                screen.append(0)
            else:
                screen.append(rng.randrange(1, 128) | (rng.randrange(2) << 7))

    players = tuple(
        bytes(rng.randrange(256) for _ in range(HEIGHT))
        for _ in range(4)
    )
    initial_colors = {reg: rng.randrange(128) * 2 for reg in COLOR_REGS}

    lines: list[list[Insn]] = [[] for _ in range(HEIGHT)]
    for y in range(1, HEIGHT):
        insns: list[Insn] = [Insn("nop") for _ in range(rng.randrange(3))]
        for pair in range(1 + rng.randrange(2)):
            if pair:
                insns.extend(Insn("nop") for _ in range(rng.randrange(3)))
            insns.append(Insn("lda", rng.randrange(128) * 2))
            insns.append(Insn("sta", rng.choice(COLOR_REGS)))
        lines[y] = insns

    return Case(seed, bytes(screen), bytes(font), players, hpos,
                initial_colors, lines)


def build_display_list() -> bytes:
    # One LMS is sufficient: 30*40 bytes do not cross ANTIC's 4K scan-counter
    # wrap.  The JVB points back to the same list.
    return bytes((0x44, SCREEN_ADDR & 0xFF, SCREEN_ADDR >> 8)
                 + (0x04,) * 29
                 + (0x41, DL_ADDR & 0xFF, DL_ADDR >> 8))


def build_kernel(case: Case) -> bytes:
    code = bytearray((
        0xAD, 0x0B, 0xD4,       # sync: lda VCOUNT
        0xC9, 0x04,             #       cmp #4 (ANTIC scanlines 8/9)
        0xD0, 0xF9,             #       bne sync
        0x8D, 0x0A, 0xD4,       #       sta WSYNC; line 0 remains unchanged
    ))

    for y in range(1, HEIGHT):
        for insn in case.lines[y]:
            if insn.op == "nop":
                code.append(0xEA)
            elif insn.op == "lda":
                code.extend((0xA9, insn.value))
            elif insn.op == "sta":
                code.extend((0x8D, insn.value & 0xFF, insn.value >> 8))
            else:
                raise AssertionError(insn.op)
        code.extend((0x8D, 0x0A, 0xD4))  # sta WSYNC

    code.extend((0x4C, CODE_ADDR & 0xFF, CODE_ADDR >> 8))
    return bytes(code)


def dma_stolen(output_y: int, x: int) -> bool:
    """Normal-width mode 4 + single-line P/M DMA, no horizontal scroll."""
    stolen = {0, 2, 3, 4, 5}  # missile and four players
    badline = output_y % 8 == 0
    if badline:
        stolen.add(1)  # display-list opcode
        if output_y == 0:
            stolen.update((6, 7))  # the only LMS
        stolen.update(range(18, 98, 2))   # 40 character names
    stolen.update(range(21, 101, 2))      # 40 character row bytes

    # ANTIC refresh: nine requests, delayed to the first free cycle.  A
    # request is dropped when the prior delayed refresh reaches its due time.
    refresh = set()
    last = 24
    for due in range(25, 61, 4):
        if last >= due:
            continue
        last = due
        while last < 107:
            candidate = last
            last += 1
            if candidate not in stolen:
                refresh.add(candidate)
                break
    return x in stolen or x in refresh


def consume_cpu_cycles(output_y: int, start: int, count: int) -> tuple[int, int]:
    """Consume CPU cycles from an absolute position around one visible line.

    ``start`` is relative to the current visible scanline, so -8 means ANTIC
    cycle 106 of the preceding scanline.  Returns (next position, last slot).
    """
    pos = start
    last = start
    consumed = 0
    while consumed < count:
        line_delta, x = divmod(pos, 114)
        line = output_y + line_delta
        if not (0 <= line < HEIGHT) or not dma_stolen(line, x):
            last = pos
            consumed += 1
        pos += 1
    return pos, last


def raster_events(case: Case) -> tuple[list[dict[int, int]], list[list[tuple[int, int, int]]]]:
    # At steady state, scanline 0 begins with the colors left by scanline 239
    # of the preceding frame.
    steady = dict(case.initial_colors)
    for line in case.lines:
        accumulator = 0
        for insn in line:
            if insn.op == "lda":
                accumulator = insn.value
            elif insn.op == "sta":
                steady[insn.value] = accumulator & 0xFE

    state = dict(steady)
    initial_states = [dict(steady) for _ in range(HEIGHT)]
    events: list[list[tuple[int, int, int]]] = [[] for _ in range(HEIGHT)]
    for y in range(1, HEIGHT):
        pos = -8  # cycle 106 of the preceding scanline after WSYNC release
        accumulator = 0
        after_visible: list[tuple[int, int]] = []
        for insn in case.lines[y]:
            cycles = 2 if insn.op in ("nop", "lda") else 4
            pos, write_pos = consume_cpu_cycles(y, pos, cycles)
            if insn.op == "lda":
                accumulator = insn.value
            elif insn.op == "sta":
                line_delta, antic_x = divmod(write_pos, 114)
                effect_x = antic_x * 2 + 1 - 48
                if line_delta < 0 or effect_x < 0:
                    state[insn.value] = accumulator & 0xFE
                elif line_delta == 0 and effect_x < WIDTH:
                    events[y].append((effect_x, insn.value, accumulator & 0xFE))
                else:
                    after_visible.append((insn.value, accumulator & 0xFE))

        initial_states[y] = dict(state)
        for _x, reg, value in events[y]:
            state[reg] = value
        for reg, value in after_visible:
            state[reg] = value

    return initial_states, events


def render_local(case: Case) -> list[list[int]]:
    line_initials, line_events = raster_events(case)
    frame = [[0] * WIDTH for _ in range(HEIGHT)]

    for y in range(HEIGHT):
        colors = dict(line_initials[y])
        changes = line_events[y]
        change_index = 0

        row = y // 8
        glyph_y = y & 7
        for x in range(WIDTH):
            while change_index < len(changes) and changes[change_index][0] <= x:
                _, reg, value = changes[change_index]
                colors[reg] = value
                change_index += 1

            player = -1
            for i, pos in enumerate(case.hpos):
                bit = x - (pos - 48)
                if 0 <= bit < 8 and case.players[i][y] & (0x80 >> bit):
                    player = i
                    break

            if player >= 0:
                frame[y][x] = colors[PM_REGS[player]]
                continue

            code = case.screen[row * 40 + x // 4]
            glyph = case.font[(code & 0x7F) * 8 + glyph_y]
            value = (glyph >> (6 - 2 * (x & 3))) & 3
            if value == 0:
                reg = BK_REG
            elif value == 1:
                reg = PF_REGS[0]
            elif value == 2:
                reg = PF_REGS[1]
            else:
                reg = PF_REGS[3] if code & 0x80 else PF_REGS[2]
            frame[y][x] = colors[reg]
    return frame


def start_bridge(bridge_dir: Path):
    sys.path.insert(0, str(bridge_dir / "sdk/python"))
    from altirra_bridge import AltirraBridge  # type: ignore

    server = subprocess.Popen(
        [str(bridge_dir / "AltirraBridgeServer"), "--bridge=tcp:127.0.0.1:0"],
        cwd=bridge_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    assert server.stderr is not None
    token_path: Path | None = None
    for line in server.stderr:
        if "[Bridge] token-file:" in line:
            token_path = Path(line.split(":", 1)[1].strip())
            break
        if server.poll() is not None:
            raise RuntimeError("AltirraBridgeServer exited during startup")
    if token_path is None:
        server.terminate()
        raise RuntimeError("AltirraBridgeServer did not report a token file")

    def drain() -> None:
        assert server.stderr is not None
        for _line in server.stderr:
            pass

    threading.Thread(target=drain, daemon=True).start()
    return server, AltirraBridge.from_token_file(str(token_path))


def render_bridge(case: Case, bridge_dir: Path):
    server, bridge = start_bridge(bridge_dir)
    try:
        with bridge as a:
            a.boot_bare()
            a.config("artifact", "none")
            a.memload(DL_ADDR, build_display_list())
            a.memload(SCREEN_ADDR, case.screen)
            a.memload(FONT_ADDR, case.font)
            a.memload(0x4300, bytes(256))
            for i, player in enumerate(case.players):
                # Single-line P/M DMA indexes the 256-byte player page by
                # ANTIC scanline; visible line zero is ANTIC line 8.
                a.memload(0x4408 + i * 0x100, player)
            a.memload(CODE_ADDR, build_kernel(case))

            writes = [
                (0xD402, DL_ADDR & 0xFF),
                (0xD403, DL_ADDR >> 8),
                (0xD409, FONT_ADDR >> 8),
                (0xD401, 0),
                (0xD407, PMBASE),
                (0xD01B, 0),
                (0xD01C, 0),
                (0xD01D, 2),
            ]
            writes += [(0xD000 + i, pos) for i, pos in enumerate(case.hpos)]
            writes += [(0xD008 + i, 0) for i in range(4)]
            writes += sorted(case.initial_colors.items())
            for addr, value in writes:
                a.hwpoke(addr, value)

            a.hwpoke(0xD400, 0x3E)
            a.memload(0x060F, bytes((0x4C, CODE_ADDR & 0xFF, CODE_ADDR >> 8)))
            a.frame(3)
            raw = a.rawscreen()
            palette = a.palette()
            a.quit()
            return raw, palette
    finally:
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.terminate()
            server.wait(timeout=5)


def expected_bgr(frame: list[list[int]], palette: bytes) -> bytes:
    out = bytearray()
    for row in frame:
        for color in row:
            rgb = palette[color * 3:color * 3 + 3]
            out.extend((rgb[2], rgb[1], rgb[0], 0))
            out.extend((rgb[2], rgb[1], rgb[0], 0))
    return bytes(out)


def crop_bridge(raw) -> bytes:
    if (raw.width, raw.height) != (336, 240):
        raise RuntimeError(f"unexpected RAWSCREEN size {raw.width}x{raw.height}")
    out = bytearray()
    pitch = raw.width * 4
    for y in range(HEIGHT):
        start = y * pitch + 8 * 4
        out.extend(raw.pixels[start:start + WIDTH * 2 * 4])
    return bytes(out)


def write_ppm(path: Path, bgra: bytes) -> None:
    with path.open("wb") as f:
        f.write(f"P6\n{WIDTH * 2} {HEIGHT}\n255\n".encode())
        for i in range(0, len(bgra), 4):
            f.write(bytes((bgra[i + 2], bgra[i + 1], bgra[i])))


def save_failure(root: Path, case: Case, expected: bytes, actual: bytes) -> Path:
    out = root / f"antic4-poc-failure-{case.seed}"
    out.mkdir(parents=True, exist_ok=True)
    (out / "screen.bin").write_bytes(case.screen)
    (out / "font.bin").write_bytes(case.font)
    for i, player in enumerate(case.players):
        (out / f"player{i}.bin").write_bytes(player)
    (out / "kernel.bin").write_bytes(build_kernel(case))
    (out / "case.json").write_text(json.dumps({
        "seed": case.seed,
        "hpos": case.hpos,
        "initial_colors": {hex(k): hex(v) for k, v in case.initial_colors.items()},
        "lines": [[(i.op, hex(i.value)) for i in line] for line in case.lines],
    }, indent=2))
    write_ppm(out / "local.ppm", expected)
    write_ppm(out / "altirra.ppm", actual)
    return out


def first_mismatch(expected: bytes, actual: bytes):
    pixels = min(len(expected), len(actual)) // 4
    for p in range(pixels):
        i = p * 4
        if expected[i:i + 3] != actual[i:i + 3]:
            return p % (WIDTH * 2), p // (WIDTH * 2), expected[i:i + 3], actual[i:i + 3]
    return None


def main(argv: Iterable[str] | None = None) -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=lambda s: int(s, 0), default=0xA4)
    parser.add_argument("--bridge-dir", type=Path,
                        default=repo / "AltirraBridge-nightly-linux-x86_64")
    parser.add_argument("--failure-dir", type=Path, default=repo / "build")
    args = parser.parse_args(argv)

    case = make_case(args.seed)
    local = render_local(case)
    raw, palette = render_bridge(case, args.bridge_dir.resolve())
    expected = expected_bgr(local, palette)
    actual = crop_bridge(raw)
    mismatch = first_mismatch(expected, actual)
    if mismatch is None and len(expected) == len(actual):
        print(f"PASS seed={case.seed}: {WIDTH * 2}x{HEIGHT} pixels match Altirra")
        return 0

    failure = save_failure(args.failure_dir, case, expected, actual)
    print(f"FAIL seed={case.seed}: first mismatch {mismatch}", file=sys.stderr)
    print(f"reproduction saved to {failure}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
