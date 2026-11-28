"""Build a vertical short (TikTok / YouTube Shorts / Reels) from demo recordings.

Run from the project root (the folder that holds ``demo/``), after the demo
sections are recorded (``demo_build.py record``):

    .venv/bin/python demo/video/short_build.py plan        # short.json → short/plan.json (timeline)
    .venv/bin/python demo/video/short_build.py segments    # one 1080x1920 clip per beat
    .venv/bin/python demo/video/short_build.py text        # Manim kinetic text track (transparent)
    .venv/bin/python demo/video/short_build.py compose     # → final/<slug>.mp4 + cover.jpg
    .venv/bin/python demo/video/short_build.py grid        # contact sheet
    .venv/bin/python demo/video/short_build.py all [--music bed.mp3]
    .venv/bin/python demo/video/short_build.py all --spec short.mobile.json   # 2nd short

Frame layout (pixels, 1080x1920):
    0-190      platform top bar: nothing important here
    190-590    TEXT BAND: kinetic headline + sub line
    600-1680   FOOTAGE WINDOW (default; "window" in short.json): crop of the UI
               fitted to the window's aspect, slow push-in. Mobile takes use a
               full-width window to the bottom: [0, 600, 1080, 1320]
    1680-1920  platform caption/description area: blurred footage only
    right 150  platform action buttons: text keeps clear (RIGHT_MARGIN)
Behind everything: the same footage, scaled to fill, blurred and darkened.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEMO = HERE.parent
ROOT = DEMO.parent
sys.path.insert(0, str(HERE))
import demo_theme as T  # noqa: E402

SPEC = DEMO / "short.json"
WORK = DEMO / "short"
PLAN = WORK / "plan.json"
FINAL = DEMO / "final"
SHORTS = HERE / "shorts.py"
W, H, FPS = 1080, 1920, 30
DEFAULT_WINDOW = [0, 600, 1080, 1080]   # footage window x, y, w, h (desktop takes)
MAX_SECONDS = 59.0               # under 60 s plays everywhere as a short


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd)[:240])
    r = subprocess.run([str(c) for c in cmd], **kw)
    if r.returncode:
        sys.exit(f"command failed (exit {r.returncode})")


def probe(p: Path) -> float:
    r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                        "-of", "csv=p=0", str(p)], capture_output=True, text=True)
    return float(r.stdout.strip() or 0)


def venv_bin(name):
    for base in (ROOT / ".venv", Path(sys.prefix)):
        if (base / "bin" / name).exists():
            return base / "bin" / name
    sys.exit(f"{name} not found in .venv")


# ------------------------------------------------------------------ plan
def fit_focus(x, y, w, h, aspect, src_w, src_h, min_w=560):
    """Any box → a box with the window's aspect (w/h) around its centre,
    grown to contain the box, clamped to the source frame."""
    fw = max(w, h * aspect, min_w)
    fh = fw / aspect
    if fh > src_h:
        fh, fw = src_h, src_h * aspect
    if fw > src_w:
        fw, fh = src_w, src_w / aspect
    cx, cy = x + w / 2, y + h / 2
    fx = min(max(0, cx - fw / 2), src_w - fw)
    fy = min(max(0, cy - fh / 2), src_h - fh)
    return [round(fx), round(fy), round(fw), round(fh)]


def resolve_beat(b: dict, i: int, rec: Path, window: list) -> dict:
    meta = json.loads((rec / f"{b['section']}.json").read_text())
    src_w, src_h = meta["size"]["width"], meta["size"]["height"]
    aspect = window[2] / window[3]
    if "caption" in b:
        c = meta["captions"][b["caption"]]
        start = c["t"] - b.get("lead", 0.2)
        end = start + b.get("len", c["dur"])
    else:
        start, end = b["from"], b["to"]
    start = max(0.0, start)
    end = min(meta["duration"], end)
    speed = float(b.get("speed", 1.0))
    if end - start < 0.6:
        sys.exit(f"beat {i} ({b['section']}): window {start:.2f}-{end:.2f}s is too short")
    focus = b.get("focus")
    if focus in (None, "spotlight"):
        spots = [s for s in meta.get("spotlights", []) if start - 0.5 <= s["t"] <= end]
        if spots:
            s = spots[0]
            focus = fit_focus(s["x"] - 140, s["y"] - 140, s["w"] + 280, s["h"] + 280,
                              aspect, src_w, src_h)
        else:   # whole width, top-anchored (right of a sidebar on wide takes)
            focus = fit_focus(0, 0, src_w, src_h, aspect, src_w, src_h)
    else:
        focus = fit_focus(*focus, aspect, src_w, src_h)
    return {"section": b["section"], "src": str(rec / f"{b['section']}.mp4"),
            "from": round(start, 3), "to": round(end, 3),
            "speed": speed, "dur": round((end - start) / speed, 3), "focus": focus,
            "zoom": b.get("zoom", 0.10), "text": b.get("text", ""), "sub": b.get("sub", "")}


def cmd_plan(_a=None) -> dict:
    spec = json.loads(SPEC.read_text())
    rec = DEMO / spec.get("recordings", "recordings")
    window = spec.get("window", DEFAULT_WINDOW)
    beats, t = [], 0.0
    for i, b in enumerate(spec["beats"]):
        r = resolve_beat(b, i, rec, window)
        r["at"] = round(t, 3)
        t += r["dur"]
        beats.append(r)
    end = spec.get("end")
    if end:
        beats.append({"end": True, "at": round(t, 3), "dur": float(end.get("dur", 3.5)),
                      **{k: end.get(k, "") for k in ("title", "text", "sub", "link")}})
        t += float(end.get("dur", 3.5))
    plan = {"slug": spec.get("slug", "short"), "duration": round(t, 3), "window": window,
            "beats": beats}
    WORK.mkdir(parents=True, exist_ok=True)
    PLAN.write_text(json.dumps(plan, indent=2))
    for b in beats:
        if b.get("end"):
            print(f"  {b['at']:6.2f}s  END   {b['dur']:.1f}s  {b.get('title')} — {b.get('text')}")
        else:
            print(f"  {b['at']:6.2f}s  {b['section']:<14} {b['from']:6.2f}-{b['to']:6.2f}"
                  f" x{b['speed']:.2f} → {b['dur']:.1f}s  {b['text']}")
    print(f"total {t:.1f}s")
    if t > MAX_SECONDS:
        sys.exit(f"short is {t:.1f}s; keep it under {MAX_SECONDS:.0f}s (trim beats or raise speed)")
    return plan


# -------------------------------------------------------------- segments
def seg_path(i): return WORK / "segments" / f"{i:02d}.mp4"


def cmd_segments(a) -> None:
    plan = json.loads(PLAN.read_text()) if PLAN.exists() else cmd_plan()
    (WORK / "segments").mkdir(parents=True, exist_ok=True)
    last_clip = None
    for i, b in enumerate(plan["beats"]):
        out = seg_path(i)
        if b.get("end"):
            # End card background: last frame of the previous clip, blurred.
            still = WORK / "end_still.png"
            run(["ffmpeg", "-y", "-v", "error", "-sseof", "-0.1", "-i", last_clip,
                 "-frames:v", "1", still])
            vf = (f"scale={W}:{H},boxblur=30:3,eq=brightness=-0.42:saturation=0.9,"
                  f"fps={FPS},format=yuv420p")
            run(["ffmpeg", "-y", "-v", "error", "-loop", "1", "-i", still, "-t", f"{b['dur']}",
                 "-vf", vf, "-c:v", "libx264", "-preset", "slow", "-crf", "17", out])
            continue
        src = Path(b["src"])
        fx, fy, fw, fh = b["focus"]
        wx, wy, ww, wh = plan.get("window", DEFAULT_WINDOW)
        n = max(1, round(b["dur"] * FPS))
        z = b["zoom"]
        border = (f",drawbox=x=0:y=0:w=iw:h=ih:color=white@0.18:t=3"
                  if ww < W else "")            # full-bleed windows need no frame
        fc = (
            f"[0:v]trim=start={b['from']}:end={b['to']},setpts=(PTS-STARTPTS)/{b['speed']},"
            f"fps={FPS},split[a][b];"
            f"[b]scale={W}:{H}:force_original_aspect_ratio=increase,crop={W}:{H},"
            f"boxblur=26:2,eq=brightness=-0.30:saturation=1.15[bg];"
            f"[a]crop={fw}:{fh}:{fx}:{fy},"
            f"scale={int(ww * 1.5)}:{int(wh * 1.5)}:flags=lanczos,"
            f"zoompan=z='1+{z}*on/{n}':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)'"
            f":d=1:s={ww}x{wh}:fps={FPS}{border}[fg];"
            f"[bg][fg]overlay={wx}:{wy},format=yuv420p[v]")
        run(["ffmpeg", "-y", "-v", "error", "-i", src, "-filter_complex", fc, "-map", "[v]",
             "-frames:v", str(n), "-c:v", "libx264", "-preset", "slow", "-crf", "17", out])
        last_clip = out


# ------------------------------------------------------------------ text
def text_path() -> Path: return WORK / "text.mov"


def cmd_text(a) -> None:
    if not PLAN.exists():
        cmd_plan()
    media = WORK / "manim"
    # --disable_caching: Manim keys its cache on the scene's starting state, so a
    # change that only affects later frames (time-driven updaters) is NOT seen.
    run([venv_bin("manim"), "render", "--disable_caching", "-t", "--format", "mov", "--fps", FPS,
         "--resolution", f"{W},{H}", "--media_dir", media, "-v", "WARNING", "-o", "text",
         SHORTS, "ShortTrack"], cwd=ROOT, env={**os.environ, "SHORT_PLAN": str(PLAN)})
    hits = sorted(media.rglob("text.mov"), key=lambda p: p.stat().st_mtime)
    if not hits:
        sys.exit("manim output text.mov not found")
    shutil.move(str(hits[-1]), text_path())


# --------------------------------------------------------------- compose
def cmd_compose(a) -> None:
    plan = json.loads(PLAN.read_text())
    segs = [seg_path(i) for i in range(len(plan["beats"]))]
    for s in segs + [text_path()]:
        if not s.exists():
            sys.exit(f"missing {s.relative_to(ROOT)} — run segments/text first")
    lst = WORK / "segments.txt"
    lst.write_text("".join(f"file '{s.resolve()}'\n" for s in segs))
    base = WORK / "base.mp4"
    run(["ffmpeg", "-y", "-v", "error", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", base])
    FINAL.mkdir(parents=True, exist_ok=True)
    out = FINAL / f"{plan['slug']}.mp4"
    total = probe(base)
    spec = json.loads(SPEC.read_text())
    music = a.music or spec.get("music")
    inputs = ["-i", base, "-i", text_path()]
    if music:
        mp = Path(music) if Path(music).is_absolute() else ROOT / music
        inputs += ["-stream_loop", "-1", "-i", mp]
        af = (f"[2:a]volume={spec.get('music_volume', 0.6)},afade=t=in:st=0:d=0.3,"
              f"afade=t=out:st={max(0, total - 1.5):.2f}:d=1.5,loudnorm=I=-14:TP=-1.5:LRA=11[a]")
    else:
        inputs += ["-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"]
        af = "[2:a]anull[a]"
    fc = (f"[1:v]format=yuva420p,fps={FPS}[t];[0:v][t]overlay=0:0:eof_action=pass:format=auto,"
          f"format=yuv420p[v];{af}")
    run(["ffmpeg", "-y", "-v", "error", *inputs, "-filter_complex", fc, "-map", "[v]",
         "-map", "[a]", "-t", f"{total:.3f}", "-c:v", "libx264", "-preset", "slow", "-crf", "18",
         "-pix_fmt", "yuv420p", "-r", FPS, "-c:a", "aac", "-b:a", "192k",
         "-movflags", "+faststart", out])
    cover = FINAL / f"{plan['slug']}-cover.jpg"
    run(["ffmpeg", "-y", "-v", "error", "-ss", "1.2", "-i", out, "-frames:v", "1", "-q:v", "2", cover])
    print(f"  -> {out.relative_to(ROOT)}  {probe(out):.1f}s  (cover: {cover.name})")


def cmd_grid(_a) -> None:
    plan = json.loads(PLAN.read_text())
    src = FINAL / f"{plan['slug']}.mp4"
    d = probe(src)
    out = DEMO / "review" / f"grid_{plan['slug']}.jpg"
    out.parent.mkdir(exist_ok=True)
    run(["ffmpeg", "-y", "-v", "error", "-i", src, "-vf",
         f"fps={12 / max(d, 1):.5f},scale=270:-1,tile=6x2:padding=6:color=black",
         "-frames:v", "1", "-q:v", "3", out])
    print(f"  -> {out.relative_to(ROOT)}")


def cmd_all(a) -> None:
    cmd_plan()
    cmd_segments(a)
    cmd_text(a)
    cmd_compose(a)
    cmd_grid(a)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("step", choices=["plan", "segments", "text", "compose", "grid", "all"])
    p.add_argument("--music", help="music bed (looped, faded, normalised to -14 LUFS)")
    p.add_argument("--spec", default="short.json",
                   help="short spec in demo/ (e.g. short.mobile.json); work files go to "
                        "demo/short/<spec stem>/ so several shorts can coexist")
    a = p.parse_args()
    global SPEC, WORK, PLAN
    SPEC = DEMO / a.spec
    if a.spec != "short.json":
        WORK = DEMO / "short" / Path(a.spec).stem.replace("short.", "")
        PLAN = WORK / "plan.json"
    globals()[f"cmd_{a.step}"](a)


if __name__ == "__main__":
    main()
