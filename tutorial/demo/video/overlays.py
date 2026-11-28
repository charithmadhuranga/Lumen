"""Manim scenes for narration-free screen demos.

Rendered by demo_build.py; parameters come from environment variables so one
scene class serves every section:

    DEMO_DIR   folder holding demo.json and recordings/
    DEMO_ID    section id (SectionCard, CaptionTrack)

Scenes
    TitleCard     opaque opening card: eyebrow, word-staggered title, rule, subtitle
    SectionCard   opaque bumper: big index, accent bar, title, subtitle
    CaptionTrack  TRANSPARENT track the length of a recording: lower-third
                  captions + spotlight outlines at the recorded marker times
    OutroCard     opaque end card: title, subtitle, link lines

Motion language (shared with the narrated tutorial mode): nothing pops.
Entrances stagger in reading order, panels grow from their anchor edge, lines
draw, exits are short fades. Holds are long; motion is brief.
"""

from __future__ import annotations

import json
import math
import os
import sys
from pathlib import Path

import numpy as np
from manim import (DOWN, LEFT, RIGHT, UP, Circle, Create, FadeIn, FadeOut,
                   GrowFromEdge, LaggedStart, Line, Rectangle, RoundedRectangle,
                   Scene, Text, VGroup, config, rate_functions)

sys.path.insert(0, str(Path(__file__).resolve().parent))
import demo_theme as T  # noqa: E402

config.background_color = T.BG
FW, FH = config.frame_width, config.frame_height  # 14.22 x 8 units

DEMO_DIR = Path(os.environ.get("DEMO_DIR", "demo")).resolve()


def spec() -> dict:
    return json.loads((DEMO_DIR / "demo.json").read_text(encoding="utf-8"))


def section(sid: str) -> tuple[int, dict]:
    for i, s in enumerate(spec()["sections"]):
        if s["id"] == sid:
            return i, s
    raise SystemExit(f"unknown section {sid!r}")


# ------------------------------------------------------------------ text
def _cw(size: float, bold: bool) -> float:
    """Conservative average glyph width in units per char (DejaVu, padded)."""
    return size * (0.0089 if bold else 0.0078)


def wrap(s: str, size: float, max_w: float, bold: bool = False) -> list[str]:
    per_line = max(8, int(max_w / _cw(size, bold)))
    lines, cur = [], ""
    for w in s.split():
        if cur and len(cur) + 1 + len(w) > per_line:
            lines.append(cur)
            cur = w
        else:
            cur = f"{cur} {w}".strip()
    if cur:
        lines.append(cur)
    # No orphans: a lone last word pulls one word down from the line above.
    if len(lines) > 1 and " " not in lines[-1] and lines[-2].count(" ") >= 2:
        prev = lines[-2].rsplit(" ", 1)
        if len(prev) == 2:
            lines[-2], lines[-1] = prev[0], f"{prev[1]} {lines[-1]}"
    return lines


def words_block(s: str, size: float, color: str, *, bold: bool = False,
                max_w: float = 11.5, align: str = "center",
                line_gap: float = 0.32) -> VGroup:
    """Text built word-by-word so every word can animate on its own.

    Returns VGroup(lines) where each line is VGroup(words). Lines are centred
    (align='center') or left-aligned (align='left') as a block.
    """
    weight = "BOLD" if bold else "NORMAL"
    rows = []
    for ln in wrap(s, size, max_w, bold):
        # One Text per line keeps the font's baseline and kerning; its glyphs
        # (spaces have none) are then regrouped into words. Building words as
        # separate Texts and aligning their boxes puts "glance" lower than
        # "at" because of descenders.
        line = Text(ln, font=T.FONT, font_size=size, color=color, weight=weight,
                    disable_ligatures=True)  # "ff"/"fi" ligatures break glyph→word split
        glyphs, k, ws = [g for g in line.submobjects if g.width > 1e-4], 0, []  # spaces are empty glyphs when ligatures are off
        for w in ln.split():
            n = len(w)
            ws.append(VGroup(*glyphs[k:k + n]))
            k += n
        row = VGroup(*ws)
        rows.append(row)
    block = VGroup(*rows).arrange(DOWN, buff=line_gap * size / 40,
                                  aligned_edge=LEFT if align == "left" else UP)
    if align == "center":
        for r in rows:
            r.set_x(0)
    return block


def all_words(block: VGroup) -> list:
    return [w for row in block for w in row]


def ease(t: float) -> float:
    t = min(1.0, max(0.0, t))
    return 1 - (1 - t) ** 3  # ease-out cubic


def glow(radius: float = 5.5, color: str = T.BG_GLOW, rings: int = 14) -> VGroup:
    """Fake radial glow: concentric discs with falling opacity."""
    g = VGroup()
    for i in range(rings):
        r = radius * (1 - i / rings)
        g.add(Circle(radius=r, stroke_width=0).set_fill(color, opacity=0.09))
    return g


def dot_grid(step: float = 0.55, opacity: float = 0.10) -> VGroup:
    g = VGroup()
    for x in np.arange(-FW / 2 + 0.3, FW / 2, step):
        for y in np.arange(-FH / 2 + 0.3, FH / 2, step):
            d = Circle(radius=0.012, stroke_width=0).set_fill(T.TEXT_MUTED, opacity)
            d.move_to([x, y, 0])
            g.add(d)
    return g


def stagger_words(words: list, lag: float = T.WORD_LAG, shift=UP * 0.22,
                  run: float = 0.55):
    return LaggedStart(*[FadeIn(w, shift=shift) for w in words],
                       lag_ratio=lag / max(run, 0.01), run_time=run + lag * len(words))


# ---------------------------------------------------------------- cards
class TitleCard(Scene):
    def construct(self):
        s = spec()
        intro = s.get("intro", {})
        dur = float(intro.get("duration", T.TITLE_SECONDS))
        bg = VGroup(dot_grid(), glow().shift(UP * 0.4))
        self.add(bg)

        eyebrow = Text(intro.get("eyebrow", "PROJECT DEMO").upper(), font=T.FONT,
                       font_size=26, color=T.ACCENT, weight="BOLD")
        title = words_block(intro.get("title", s.get("product", "")), 96, T.TEXT,
                            bold=True, max_w=12.4)
        rule = Line(LEFT * 1.6, RIGHT * 1.6, color=T.ACCENT, stroke_width=5)
        sub = words_block(intro.get("subtitle", s.get("tagline", "")), 34,
                          T.TEXT_DIM, max_w=11.0)
        stack = VGroup(eyebrow, title, rule, sub).arrange(DOWN, buff=0.42)
        stack.move_to(UP * 0.15)

        self.play(FadeIn(eyebrow, shift=UP * 0.2), run_time=0.5)
        self.play(stagger_words(all_words(title), lag=0.09, run=0.6))
        self.play(Create(rule), run_time=0.45, rate_func=rate_functions.ease_out_cubic)
        self.play(stagger_words(all_words(sub), lag=0.035, run=0.45, shift=UP * 0.12))
        spent = 0.5 + 0.6 + 0.09 * len(all_words(title)) + 0.45 + 0.45 \
            + 0.035 * len(all_words(sub))
        self.wait(max(0.3, dur - spent - 0.6))
        self.play(FadeOut(stack, shift=UP * 0.15), FadeOut(bg), run_time=0.6)


class SectionCard(Scene):
    def construct(self):
        idx, sec = section(os.environ["DEMO_ID"])
        dur = float(sec.get("card_seconds", T.SECTION_SECONDS))
        bg = VGroup(dot_grid(opacity=0.07), glow(radius=4.5).shift(LEFT * 3.2))
        self.add(bg)

        num = Text(f"{idx + 1:02d}", font=T.FONT, font_size=210, weight="BOLD",
                   color=T.ACCENT).set_opacity(0.95)
        bar = Rectangle(width=0.09, height=2.7, stroke_width=0).set_fill(T.ACCENT, 1)
        title = words_block(sec["title"], 76, T.TEXT, bold=True, max_w=8.2,
                            align="left")
        sub = words_block(sec.get("subtitle", ""), 32, T.TEXT_DIM, max_w=8.2,
                          align="left") if sec.get("subtitle") else VGroup()
        text = VGroup(title, sub).arrange(DOWN, buff=0.38, aligned_edge=LEFT)
        row = VGroup(num, bar, text).arrange(RIGHT, buff=0.55)
        bar.stretch_to_fit_height(max(text.height, num.height) * 1.05)
        row.move_to(ORIGIN_SAFE())

        self.play(FadeIn(num, scale=0.86), GrowFromEdge(bar, DOWN), run_time=0.5,
                  rate_func=rate_functions.ease_out_cubic)
        self.play(stagger_words(all_words(title), lag=0.07, run=0.5, shift=RIGHT * 0.25))
        if len(sub):
            self.play(stagger_words(all_words(sub), lag=0.03, run=0.4, shift=RIGHT * 0.15))
        # Hold to the end; the composer crossfades the card into the clip.
        used = 0.5 + 0.5 + 0.07 * len(all_words(title)) \
            + (0.4 + 0.03 * len(all_words(sub)) if len(sub) else 0)
        self.wait(max(0.4, dur - used))


def ORIGIN_SAFE():
    return np.array([0.0, 0.1, 0.0])


class OutroCard(Scene):
    def construct(self):
        s = spec()
        o = s.get("outro", {})
        dur = float(o.get("duration", T.OUTRO_SECONDS))
        bg = VGroup(dot_grid(), glow())
        self.add(bg)
        title = words_block(o.get("title", s.get("product", "")), 84, T.TEXT,
                            bold=True, max_w=12)
        sub = words_block(o.get("subtitle", ""), 34, T.TEXT_DIM, max_w=11)
        lines = VGroup(*[Text(l, font=T.FONT_MONO, font_size=28, color=T.ACCENT)
                         for l in o.get("lines", [])]).arrange(DOWN, buff=0.22)
        stack = VGroup(title, sub, lines).arrange(DOWN, buff=0.5)
        self.play(stagger_words(all_words(title), lag=0.08, run=0.6))
        self.play(stagger_words(all_words(sub), lag=0.03, run=0.45, shift=UP * 0.12))
        if len(lines):
            self.play(LaggedStart(*[FadeIn(l, shift=UP * 0.12) for l in lines],
                                  lag_ratio=0.25, run_time=0.8))
        self.wait(max(0.5, dur - 3.2))
        self.play(FadeOut(stack), FadeOut(bg), run_time=0.9)


# -------------------------------------------------------- caption track
def px(x: float, y: float) -> np.ndarray:
    """1920x1080 pixel coords (top-left origin) → Manim units."""
    return np.array([(x / T.WIDTH - 0.5) * FW, (0.5 - y / T.HEIGHT) * FH, 0.0])


class _Caption:
    """Lower-third: panel grows from its left edge, accent bar grows up,
    headline words stagger in from the left, detail fades up, then a short
    fade-and-drop exit."""

    def __init__(self, c: dict, end: float):
        self.t0, self.t1 = c["t"], end
        pos = c.get("pos", "bottom")
        head = words_block(c["text"], 34, T.TEXT, bold=True, max_w=11.0, align="left")
        sub = (words_block(c["sub"], 24, T.TEXT_DIM, max_w=11.0, align="left")
               if c.get("sub") else VGroup())
        body = VGroup(head, sub).arrange(DOWN, buff=0.16, aligned_edge=LEFT)
        pad_x, pad_y = 0.42, 0.26
        w, h = body.width + 2 * pad_x + 0.12, body.height + 2 * pad_y
        panel = RoundedRectangle(width=w, height=h, corner_radius=0.12,
                                 stroke_color=T.BORDER, stroke_width=1.5)
        panel.set_fill(T.PANEL, T.PANEL_OPACITY)
        bar = Rectangle(width=0.085, height=h - 0.2, stroke_width=0).set_fill(T.ACCENT, 1)
        x_left = -FW / 2 + 0.55
        y = -FH / 2 + 0.55 + h / 2 if pos == "bottom" else FH / 2 - 0.55 - h / 2
        panel.move_to([x_left + w / 2, y, 0])
        bar.move_to([x_left + 0.16, y, 0])
        body.move_to([x_left + 0.16 + 0.12 + pad_x + body.width / 2 - 0.02, y, 0])
        self.panel_base, self.bar_base = panel.copy(), bar.copy()
        self.panel, self.bar = panel, bar
        self.words = all_words(head)
        self.word_base = [wd.get_center().copy() for wd in self.words]
        self.sub = sub
        self.sub_base = sub.get_center().copy() if len(sub) else None
        self.group = VGroup(panel, bar, head, sub)

    def update(self, t: float) -> None:
        if t < self.t0 or t > self.t1:
            self.group.set_opacity(0)
            return
        k_in = ease((t - self.t0) / T.CAPTION_IN)
        k_out = ease((t - (self.t1 - T.CAPTION_OUT)) / T.CAPTION_OUT)
        a = 1 - k_out
        drop = DOWN * 0.12 * k_out
        self.panel.become(self.panel_base.copy().stretch(max(k_in, 0.002), 0,
                                                         about_edge=LEFT))
        self.panel.set_fill(T.PANEL, T.PANEL_OPACITY * a).set_stroke(opacity=a)
        self.panel.shift(drop)
        self.bar.become(self.bar_base.copy().stretch(max(ease((t - self.t0) / 0.3), 0.002),
                                                     1, about_edge=DOWN))
        self.bar.set_fill(T.ACCENT, a).shift(drop)
        for i, (wd, base) in enumerate(zip(self.words, self.word_base)):
            k = ease((t - self.t0 - 0.16 - i * T.WORD_LAG) / 0.32)
            wd.move_to(base + LEFT * 0.22 * (1 - k) + drop)
            wd.set_opacity(k * a)
        if self.sub_base is not None:
            k = ease((t - self.t0 - 0.3 - len(self.words) * T.WORD_LAG) / 0.35)
            self.sub.move_to(self.sub_base + DOWN * 0.1 * (1 - k) + drop)
            self.sub.set_opacity(k * a)


class _Spotlight:
    """Accent outline that draws itself around a UI region, breathes once,
    then fades. Optional label chip sits above the box."""

    def __init__(self, s: dict):
        self.t0, self.t1 = s["t"], s["t"] + s.get("dur", 2.4)
        tl, br = px(s["x"], s["y"]), px(s["x"] + s["w"], s["y"] + s["h"])
        w, h = br[0] - tl[0], tl[1] - br[1]
        c = (tl + br) / 2
        self.base = RoundedRectangle(width=w, height=h, corner_radius=0.1,
                                     stroke_color=T.ACCENT, stroke_width=5).move_to(c)
        self.halo = RoundedRectangle(width=w + 0.08, height=h + 0.08, corner_radius=0.13,
                                     stroke_color=T.ACCENT, stroke_width=16).move_to(c)
        self.box = self.base.copy()
        self.label = VGroup()
        if s.get("label"):
            txt = Text(s["label"], font=T.FONT, font_size=24, weight="BOLD", color=T.BG)
            chip = RoundedRectangle(width=txt.width + 0.4, height=txt.height + 0.24,
                                    corner_radius=0.1, stroke_width=0).set_fill(T.ACCENT, 1)
            self.label = VGroup(chip, txt.move_to(chip))
            self.label.next_to(self.base, UP, buff=0.12, aligned_edge=LEFT)
            if self.label.get_top()[1] > FH / 2 - 0.1:
                self.label.next_to(self.base, DOWN, buff=0.12, aligned_edge=LEFT)
        self.group = VGroup(self.halo, self.box, self.label)

    def update(self, t: float) -> None:
        if t < self.t0 or t > self.t1:
            self.group.set_opacity(0)
            return
        k = ease((t - self.t0) / 0.55)
        a = 1 - ease((t - (self.t1 - 0.4)) / 0.4)
        self.box.pointwise_become_partial(self.base, 0, max(k, 0.001))
        self.box.set_stroke(T.ACCENT, width=5, opacity=a)
        breathe = 0.5 + 0.5 * math.sin(max(0.0, t - self.t0 - 0.55) * 3.2)
        self.halo.set_stroke(T.ACCENT, width=16, opacity=0.18 * k * a * (0.6 + 0.4 * breathe))
        kl = ease((t - self.t0 - 0.35) / 0.3) * a
        self.label.set_opacity(kl)


class CaptionTrack(Scene):
    """Transparent overlay the exact length of a recording (render with -t)."""

    def construct(self):
        sid = os.environ["DEMO_ID"]
        meta = json.loads((DEMO_DIR / "recordings" / f"{sid}.json").read_text())
        dur = float(meta["duration"])
        caps = sorted(meta.get("captions", []), key=lambda c: c["t"])
        items = []
        for i, c in enumerate(caps):
            end = c["t"] + c["dur"]
            if i + 1 < len(caps):          # never stack two lower-thirds
                end = min(end, caps[i + 1]["t"] - 0.05)
            items.append(_Caption(c, min(end, dur - 0.05)))
        items += [_Spotlight(s) for s in meta.get("spotlights", [])]
        layer = VGroup(*[it.group for it in items])
        clock = {"t": 0.0}

        def tick(_m, dt):
            clock["t"] += dt
            for it in items:
                it.update(clock["t"])

        for it in items:
            it.update(0.0)
        layer.add_updater(tick)
        self.add(layer)
        self.wait(dur)
