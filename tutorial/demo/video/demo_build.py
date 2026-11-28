"""Build orchestrator for narration-free screen demo videos.

Run from the project root (the folder that holds ``demo/``):

    .venv/bin/python demo/video/demo_build.py record [ids]     # Playwright → recordings/
    .venv/bin/python demo/video/demo_build.py overlays [ids]   # Manim cards + caption tracks
    .venv/bin/python demo/video/demo_build.py compose [ids]    # card ⨯ clip ⨯ captions → parts/
    .venv/bin/python demo/video/demo_build.py concat [--music f.mp3]  # → final/<slug>.mp4
    .venv/bin/python demo/video/demo_build.py chapters         # YouTube chapter list
    .venv/bin/python demo/video/demo_build.py grid <id|final>  # contact sheet for review
    .venv/bin/python demo/video/demo_build.py all

Every step skips outputs that are newer than their inputs; ``--force``
rebuilds. Re-recording a section makes its caption track, part and the final
video stale automatically (mtime check), so the classic "stale mux" bug of the
narrated pipeline cannot happen here.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent          # demo/video
DEMO = HERE.parent                              # demo/
ROOT = DEMO.parent                              # project root
sys.path.insert(0, str(HERE))
import demo_theme as T  # noqa: E402

SPEC_PATH = DEMO / "demo.json"
REC = DEMO / "recordings"
MEDIA = DEMO / "manim_build"
PARTS = DEMO / "parts"
FINAL = DEMO / "final"
OVERLAYS = HERE / "overlays.py"
RECORDER = DEMO / "recorder" / "record.mjs"


def venv_bin(name: str) -> Path:
    for base in (ROOT / ".venv", DEMO / ".venv", Path(sys.prefix)):
        p = base / "bin" / name
        if p.exists():
            return p
    sys.exit(f"{name} not found in .venv — create it and install manim first")


def spec() -> dict:
    return json.loads(SPEC_PATH.read_text(encoding="utf-8"))


def ids_of(args_ids: list[str]) -> list[str]:
    all_ids = [s["id"] for s in spec()["sections"]]
    if not args_ids:
        return all_ids
    bad = [i for i in args_ids if i not in all_ids]
    if bad:
        sys.exit(f"unknown section(s): {', '.join(bad)}; known: {', '.join(all_ids)}")
    return args_ids


def run(cmd: list, **kw) -> None:
    print("  $", " ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd], **kw)
    if r.returncode:
        sys.exit(f"command failed (exit {r.returncode})")


def stale(out: Path, *deps: Path, force: bool = False) -> bool:
    if force or not out.exists():
        return True
    m = out.stat().st_mtime
    return any(d.exists() and d.stat().st_mtime > m for d in deps)


def probe_duration(p: Path) -> float:
    r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                        "-of", "csv=p=0", str(p)], capture_output=True, text=True)
    return float(r.stdout.strip() or 0)


def slug() -> str:
    s = spec()
    return s.get("slug") or s.get("product", "demo").lower().replace(" ", "-") + "-demo"


# ------------------------------------------------------------------ steps
def cmd_record(a) -> None:
    ids = ids_of(a.ids)
    cmd = ["node", RECORDER, DEMO]
    if a.ids:
        cmd += ["--only", ",".join(ids)]
    if a.headed:
        cmd.append("--headed")
    run(cmd, cwd=ROOT)


def manim(scene: str, out_name: str, env: dict, transparent: bool = False) -> Path:
    ext = "mov" if transparent else "mp4"
    # --disable_caching: Manim keys its cache on the scene's starting state; the
    # caption track changes only through time-driven updaters, which it can't see.
    flags = ["--disable_caching", "--fps", str(T.FPS), "--resolution", f"{T.WIDTH},{T.HEIGHT}",
             "--media_dir", MEDIA, "-v", "WARNING", "-o", out_name]
    if transparent:
        flags += ["-t", "--format", "mov"]
    run([venv_bin("manim"), "render", *flags, OVERLAYS, scene], cwd=ROOT,
        env={**os.environ, "DEMO_DIR": str(DEMO), **env})
    hits = sorted(MEDIA.rglob(f"{out_name}.{ext}"), key=lambda p: p.stat().st_mtime)
    if not hits:
        sys.exit(f"manim output {out_name}.{ext} not found under {MEDIA}")
    return hits[-1]


def overlay_path(name: str, transparent: bool = False) -> Path:
    ext = "mov" if transparent else "mp4"
    return MEDIA / "out" / f"{name}.{ext}"


def build_overlay(scene, name, env, deps, force, transparent=False) -> Path:
    dst = overlay_path(name, transparent)
    if not stale(dst, SPEC_PATH, OVERLAYS, HERE / "demo_theme.py", *deps, force=force):
        print(f"skip {name} (current)")
        return dst
    src = manim(scene, name, env, transparent)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(src), dst)
    return dst


def cmd_overlays(a) -> None:
    ids = ids_of(a.ids)
    if not a.ids:
        build_overlay("TitleCard", "title", {}, [], a.force)
        build_overlay("OutroCard", "outro", {}, [], a.force)
    for sid in ids:
        meta = REC / f"{sid}.json"
        if not meta.exists():
            sys.exit(f"{sid}: no recording yet — run `record {sid}` first")
        build_overlay("SectionCard", f"card_{sid}", {"DEMO_ID": sid}, [], a.force)
        build_overlay("CaptionTrack", f"captions_{sid}", {"DEMO_ID": sid}, [meta],
                      a.force, transparent=True)


def cmd_compose(a) -> None:
    PARTS.mkdir(parents=True, exist_ok=True)
    s = spec()
    for n, sid in enumerate(ids_of(a.ids), 1):
        idx = [x["id"] for x in s["sections"]].index(sid) + 1
        out = PARTS / f"{idx:02d}-{sid}.mp4"
        clip, card = REC / f"{sid}.mp4", overlay_path(f"card_{sid}")
        caps = overlay_path(f"captions_{sid}", transparent=True)
        for p in (clip, card, caps):
            if not p.exists():
                sys.exit(f"{sid}: missing {p.relative_to(ROOT)} — run record/overlays")
        if not stale(out, clip, card, caps, force=a.force):
            print(f"skip {out.name} (current)")
            continue
        cd = probe_duration(card)
        off = max(0.1, cd - T.XFADE)
        norm = f"fps={T.FPS},scale={T.WIDTH}:{T.HEIGHT},setsar=1,format=yuv420p,settb=AVTB"
        fc = (f"[0:v]{norm}[card];"
              f"[1:v]{norm}[rec];"
              f"[2:v]fps={T.FPS},format=yuva420p,settb=AVTB[cap];"
              f"[rec][cap]overlay=0:0:eof_action=pass:format=auto,{norm}[clip];"
              f"[card][clip]xfade=transition=fade:duration={T.XFADE}:offset={off:.3f},"
              f"format=yuv420p[v]")
        run(["ffmpeg", "-y", "-v", "error", "-i", card, "-i", clip, "-i", caps,
             "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo",
             "-filter_complex", fc, "-map", "[v]", "-map", "3:a", "-shortest",
             "-c:v", "libx264", "-preset", "slow", "-crf", "17", "-pix_fmt", "yuv420p",
             "-r", T.FPS, "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", out])
        print(f"  -> {out.relative_to(ROOT)}  {probe_duration(out):.1f}s")


def _bookend(name: str) -> Path:
    """Title/outro card re-encoded with a silent track so concat can copy."""
    src = overlay_path(name)
    out = PARTS / f"{'00' if name == 'title' else '99'}-{name}.mp4"
    if stale(out, src):
        run(["ffmpeg", "-y", "-v", "error", "-i", src, "-f", "lavfi", "-i",
             "anullsrc=r=48000:cl=stereo", "-map", "0:v", "-map", "1:a", "-shortest",
             "-vf", f"fps={T.FPS},format=yuv420p", "-c:v", "libx264", "-preset", "slow",
             "-crf", "17", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", out])
    return out


def ordered_parts() -> list[Path]:
    s = spec()
    parts = [_bookend("title")]
    for i, sec in enumerate(s["sections"], 1):
        p = PARTS / f"{i:02d}-{sec['id']}.mp4"
        if not p.exists():
            sys.exit(f"missing part {p.name} — run compose")
        parts.append(p)
    parts.append(_bookend("outro"))
    return parts


def cmd_concat(a) -> None:
    FINAL.mkdir(parents=True, exist_ok=True)
    parts = ordered_parts()
    lst = PARTS / "concat.txt"
    lst.write_text("".join(f"file '{p.resolve()}'\n" for p in parts))
    silent = FINAL / f"{slug()}.silent.mp4"
    run(["ffmpeg", "-y", "-v", "error", "-f", "concat", "-safe", "0", "-i", lst,
         "-c", "copy", "-movflags", "+faststart", silent])
    out = FINAL / f"{slug()}.mp4"
    music = a.music or spec().get("music")
    if music:
        music = (ROOT / music) if not Path(music).is_absolute() else Path(music)
        total = probe_duration(silent)
        vol = spec().get("music_volume", 0.35)
        af = (f"[1:a]volume={vol},afade=t=in:st=0:d=2,"
              f"afade=t=out:st={max(0, total - 3):.2f}:d=3,"
              f"loudnorm=I=-20:TP=-2:LRA=11[a]")
        run(["ffmpeg", "-y", "-v", "error", "-i", silent, "-stream_loop", "-1", "-i", music,
             "-filter_complex", af, "-map", "0:v", "-map", "[a]", "-t", f"{total:.3f}",
             "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", out])
        silent.unlink()
    else:
        silent.replace(out)
    print(f"  -> {out.relative_to(ROOT)}  {probe_duration(out):.1f}s")
    cmd_chapters(a)


def cmd_chapters(_a) -> None:
    s = spec()
    parts = ordered_parts()
    t, lines = 0.0, []
    names = ["Intro"] + [sec["title"] for sec in s["sections"]] + ["Wrap-up"]
    for name, p in zip(names, parts):
        lines.append(f"{int(t // 60)}:{int(t % 60):02d} {name}")
        t += probe_duration(p)
    FINAL.mkdir(parents=True, exist_ok=True)
    out = FINAL / "chapters.txt"
    out.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"wrote {out.relative_to(ROOT)}")


def cmd_grid(a) -> None:
    target = a.ids[0] if a.ids else "final"
    if target == "final":
        src = FINAL / f"{slug()}.mp4"
    else:
        hits = sorted(PARTS.glob(f"*-{target}.mp4"))
        src = hits[0] if hits else REC / f"{target}.mp4"
    if not src.exists():
        sys.exit(f"{src} not found")
    d = probe_duration(src)
    n = 20
    out = DEMO / "review" / f"grid_{target}.jpg"
    out.parent.mkdir(exist_ok=True)
    run(["ffmpeg", "-y", "-v", "error", "-i", src, "-vf",
         f"fps={n / max(d, 1):.5f},scale=480:-1,tile=5x4:padding=6:color=black",
         "-frames:v", "1", "-q:v", "3", out])
    print(f"  -> {out.relative_to(ROOT)} ({n} frames over {d:.1f}s)")


def cmd_all(a) -> None:
    for step in (cmd_record, cmd_overlays, cmd_compose):
        step(a)
    a.ids = []
    cmd_concat(a)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("step", choices=["record", "overlays", "compose", "concat",
                                    "chapters", "grid", "all"])
    p.add_argument("ids", nargs="*")
    p.add_argument("--force", action="store_true")
    p.add_argument("--headed", action="store_true", help="record with a visible browser")
    p.add_argument("--music", help="music bed for concat (looped, faded, normalised)")
    a = p.parse_args()
    globals()[f"cmd_{a.step}"](a)


if __name__ == "__main__":
    main()
