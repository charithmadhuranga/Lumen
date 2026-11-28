"""Manim text track for vertical shorts (TikTok, YouTube Shorts, Reels).

Rendered by short_build.py as ONE transparent 1080x1920 track covering the
whole short; the footage underneath is composed by ffmpeg. Parameters come
from the plan short_build.py writes:

    SHORT_PLAN   path to demo/short/plan.json

Kinetic language (built to stop the scroll, not to be read like a caption):
  * words POP in one by one with an overshoot (ease-out-back), staggered fast
  * *emphasis* words sit on a solid accent block that wipes in from the left
  * the hook (first beat) pulses its emphasis word so the first second moves
  * exits are fast (≈0.16 s): text never lingers over a cut
  * all text lives in the top safe band — clear of the platform UI
    (top bar, right-hand action buttons, bottom caption/description area)
"""

from __future__ import annotations

import json
import math
import os
import sys
from pathlib import Path

import numpy as np
from manim import (DOWN, LEFT, RIGHT, UP, RoundedRectangle, Scene, Text, VGroup,
                   config)

sys.path.insert(0, str(Path(__file__).resolve().parent))
import demo_theme as T  # noqa: E402

# 9 x 16 units at 1080x1920 → 120 px per unit (close to the 135 px/unit of
# the landscape cards, so font sizes mean roughly the same thing).
config.frame_width = 9.0
config.frame_height = 16.0
FW, FH = 9.0, 16.0
PX = 120.0

# Safe band for text (pixels from the top of 1920): below the platform top
# bar, above the footage window. Right margin clears the action-button rail.
BAND_TOP_PX, BAND_BOTTOM_PX = 190, 590
LEFT_MARGIN, RIGHT_MARGIN = 0.55, 1.25          # units
TEXT_W = FW - LEFT_MARGIN - RIGHT_MARGIN


def y_of(px: float) -> float:
    return (FH / 2) - px / PX


def ease_out_back(t: float, s: float = 1.9) -> float:
    t = min(1.0, max(0.0, t)) - 1
    return t * t * ((s + 1) * t + s) + 1


def ease_out(t: float) -> float:
    t = min(1.0, max(0.0, t))
    return 1 - (1 - t) ** 3


def parse(markup: str) -> list[tuple[str, bool]]:
    """'Your home. *Glowing.*' → [('Your', F), ('home.', F), ('Glowing.', T)]."""
    out, emph = [], False
    for tok in markup.split():
        start = tok.startswith("*")
        end = tok.endswith("*") and len(tok) > 1
        if start:
            emph = True
        word = tok.strip("*")
        out.append((word, emph))
        if end:
            emph = False
    return out


def _line_w(words: list[tuple[str, bool]], size: float) -> float:
    return Text(" ".join(w for w, _ in words), font=T.FONT, font_size=size,
                weight="BOLD", disable_ligatures=True).width


def wrap_measured(words: list[tuple[str, bool]], size: float, max_w: float) -> list[list]:
    """Greedy wrap on REAL rendered widths (estimates over-wrap narrow fonts)."""
    lines, cur = [], []
    for w in words:
        if cur and _line_w(cur + [w], size) > max_w:
            lines.append(cur)
            cur = [w]
        else:
            cur.append(w)
    if cur:
        lines.append(cur)
    if len(lines) > 1 and len(lines[-1]) == 1 and len(lines[-2]) > 2 \
            and _line_w([lines[-2][-1]] + lines[-1], size) <= max_w:     # no orphans
        lines[-1].insert(0, lines[-2].pop())
    return lines


class _Words:
    """A headline built word-by-word; each word pops on its own. Consecutive
    *emphasis* words share one accent block."""

    def __init__(self, markup: str, size: float, *, top_px: float, align: str = "left",
                 color: str = T.TEXT, line_gap: float = 0.22, max_w: float = TEXT_W):
        self.size = size
        lines = wrap_measured(parse(markup), size, max_w)
        self.items = []                       # (word, base_center, block or None)
        rows = VGroup()
        for ln in lines:
            text = " ".join(w for w, _ in ln)
            line = Text(text, font=T.FONT, font_size=size, weight="BOLD", color=color,
                        disable_ligatures=True)  # 1 glyph per char
            glyphs, k, row = [g for g in line.submobjects if g.width > 1e-4], 0, VGroup()  # spaces are empty glyphs when ligatures are off
            for w, emph in ln:
                g = VGroup(*glyphs[k:k + len(w)])
                k += len(w)
                g.emph = emph
                row.add(g)
            rows.add(row)
        rows.arrange(DOWN, buff=line_gap * size / 40, aligned_edge=LEFT)
        if align == "center":
            for r in rows:
                r.set_x(0)
        else:
            rows.align_to(np.array([-FW / 2 + LEFT_MARGIN, 0, 0]), LEFT)
        pad_x, pad_y = 0.11 * size / 60, 0.07 * size / 60
        cap_h = size / 40 * 0.74                      # cap height + descender room
        rows.align_to(np.array([0, y_of(top_px) - pad_y, 0]), UP)
        self.group = VGroup()
        blocks = []
        for row in rows:
            run: list = []
            for g in list(row) + [None]:
                if g is not None and g.emph:
                    run.append(g)
                    continue
                if run:                                # close a run of emphasis words
                    left, right = run[0].get_left()[0], run[-1].get_right()[0]
                    cy = row.get_center()[1]
                    blk = RoundedRectangle(width=right - left + 2 * pad_x,
                                           height=cap_h + 2 * pad_y, corner_radius=0.07,
                                           stroke_width=0).set_fill(T.ACCENT, 1)
                    blk.move_to([(left + right) / 2, cy, 0])
                    blk.base = blk.copy()
                    blocks.append((run[0], blk))
                    for w in run:
                        w.set_color(T.BG)
                    run = []
        first_of_run = {id(w): blk for w, blk in blocks}
        for _, blk in blocks:
            self.group.add(blk)
        for row in rows:
            for g in row:
                blk = first_of_run.get(id(g))
                g.base = g.copy()
                self.items.append((g, g.get_center().copy(), blk))
                self.group.add(g)
        self.orphan = len(rows) > 1 and len(rows[-1]) == 1
        top = max(m.get_top()[1] for m in self.group)
        bottom = min(m.get_bottom()[1] for m in self.group)
        self.bottom_px = top_px + (top - bottom) * PX

    def update(self, t: float, t0: float, t1: float, *, lag: float = 0.075,
               pulse: bool = False) -> None:
        if t < t0 or t > t1:
            self.group.set_opacity(0)
            return
        out = ease_out((t - (t1 - 0.16)) / 0.16)
        lift = UP * 0.2 * out
        for i, (g, base, block) in enumerate(self.items):
            ti = t - t0 - i * lag
            k = ease_out_back(ti / 0.32)               # overshoots past 1
            a = min(1.0, max(0.0, ti / 0.12)) * (1 - out)
            s = max(0.02, 0.25 + 0.75 * k) * (1 - 0.15 * out)
            if pulse and block is not None and ti > 0.55:
                s *= 1 + 0.07 * math.sin((ti - 0.55) * 7.0) * math.exp(-(ti - 0.55) * 0.5)
            pos = base + DOWN * 0.22 * (1 - min(1.0, max(0.0, k))) + lift
            if block is not None:
                kb = ease_out((ti + 0.06) / 0.22)
                bl = block.base.copy().stretch(max(kb, 0.002), 0, about_edge=LEFT)
                if pulse and ti > 0.55:
                    bl.scale(s / max(0.25 + 0.75 * k, 1e-3), about_point=block.base.get_center())
                block.become(bl.shift(lift))
                block.set_fill(T.ACCENT, (1 - out) * min(1.0, kb * 3))
            g.become(g.base.copy().scale(s).move_to(pos))
            g.set_opacity(a)


class _Sub:
    def __init__(self, text: str, size: float, top_px: float):
        self.mob = Text(text, font=T.FONT, font_size=size, color=T.TEXT_DIM)
        if self.mob.width > TEXT_W:
            self.mob.scale_to_fit_width(TEXT_W)
        self.mob.align_to(np.array([-FW / 2 + LEFT_MARGIN, 0, 0]), LEFT)
        self.mob.align_to(np.array([0, y_of(top_px), 0]), UP)
        self.base = self.mob.get_center().copy()

    def update(self, t, t0, t1):
        if t < t0 or t > t1:
            self.mob.set_opacity(0)
            return
        k = ease_out((t - t0) / 0.3)
        out = ease_out((t - (t1 - 0.16)) / 0.16)
        self.mob.move_to(self.base + DOWN * 0.15 * (1 - k) + UP * 0.2 * out)
        self.mob.set_opacity(k * (1 - out))


class _Beat:
    def __init__(self, b: dict, hook: bool):
        self.t0, self.t1 = b["at"], b["at"] + b["dur"]
        size = 78 if hook else 66
        sub_h = 34 / 40 * 0.9 * PX + 24 if b.get("sub") else 0
        self.head = None
        if b.get("text"):
            # Largest size that fits the band; within 15 % of the start size,
            # prefer one whose last line is not a lone word.
            fits = []
            while size >= 40:
                w = _Words(b["text"], size, top_px=BAND_TOP_PX)
                if w.bottom_px + sub_h <= BAND_BOTTOM_PX:
                    fits.append((size, w))
                size *= 0.95
            base = fits[0][0] if fits else 40
            clean = [w for s_, w in fits if s_ >= base * 0.85 and not w.orphan]
            self.head = clean[0] if clean else (fits[0][1] if fits else
                                                _Words(b["text"], 40, top_px=BAND_TOP_PX))
        sub_top = (self.head.bottom_px + 20) if self.head else BAND_TOP_PX
        self.sub = _Sub(b["sub"], 34, sub_top) if b.get("sub") else None
        self.hook = hook
        delay = 0.05
        n = len(self.head.items) if self.head else 0
        self.sub_t0 = self.t0 + delay + n * 0.075 + 0.15
        self.mobs = VGroup(*([self.head.group] if self.head else []),
                           *([self.sub.mob] if self.sub else []))

    def update(self, t):
        if self.head:
            self.head.update(t, self.t0 + 0.05, self.t1, pulse=self.hook)
        if self.sub:
            self.sub.update(t, self.sub_t0, self.t1)


class _End:
    """End card text: product name slams in, line, CTA, link."""

    def __init__(self, b: dict):
        self.t0, self.t1 = b["at"], b["at"] + b["dur"] + 1.0
        self.title = _Words(b.get("title", ""), 120, top_px=640, align="center", max_w=FW - 1.0)
        self.lines = []
        top = self.title.bottom_px + 40
        for i, (txt, size, col) in enumerate([(b.get("text", ""), 54, T.TEXT),
                                              (b.get("sub", ""), 36, T.ACCENT),
                                              (b.get("link", ""), 30, T.TEXT_DIM)]):
            if not txt:
                continue
            if i == 0:
                w = _Words(txt, size, top_px=top, align="center", color=col, max_w=FW - 1.0)
                self.lines.append(("w", w, i))
                top = w.bottom_px + 30
            else:
                m = Text(txt, font=T.FONT if i == 1 else T.FONT_MONO, font_size=size,
                         color=col, weight="BOLD" if i == 1 else "NORMAL")
                if m.width > FW - 1.2:
                    m.scale_to_fit_width(FW - 1.2)
                m.move_to([0, y_of(top) - m.height / 2, 0])
                m.base = m.get_center().copy()
                self.lines.append(("t", m, i))
                top += m.height * PX + 26
        self.mobs = VGroup(self.title.group,
                           *[x[1].group if x[0] == "w" else x[1] for x in self.lines])

    def update(self, t):
        self.title.update(t, self.t0 + 0.1, self.t1, lag=0.06, pulse=True)
        for kind, m, i in self.lines:
            d = 0.45 + 0.25 * i
            if kind == "w":
                m.update(t, self.t0 + d, self.t1, lag=0.06)
            else:
                k = ease_out((t - self.t0 - d) / 0.35) if t >= self.t0 + d else 0
                m.move_to(m.base + UP * 0.2 * (1 - k))
                m.set_opacity(k)


class ShortTrack(Scene):
    def construct(self):
        plan = json.loads(Path(os.environ["SHORT_PLAN"]).read_text())
        items = []
        for i, b in enumerate(plan["beats"]):
            items.append(_End(b) if b.get("end") else _Beat(b, hook=(i == 0)))
        layer = VGroup(*[it.mobs for it in items])
        clock = {"t": 0.0}

        def tick(_m, dt):
            clock["t"] += dt
            for it in items:
                it.update(clock["t"])

        for it in items:
            it.update(0.0)
        layer.add_updater(tick)
        self.add(layer)
        self.wait(plan["duration"])
