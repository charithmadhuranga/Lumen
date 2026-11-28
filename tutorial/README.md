# Lumen videos

Built with the `youtube-video-creator-skill` (github.com/charithmadhuranga/youtube-video-creator-skill).

| Folder | What |
|---|---|
| `demo/` | Narration-free project demo: Playwright recording of the real web UI on two controllers + Home Assistant, with Manim title/section cards and animated captions |
| `narration/` | Scripts for the narrated tutorial series (record one mp3 per script, then build in tutorial mode) |

## Project demo

Hardware used: KinCony KC868-AP (`lumen-0798`) and a generic ESP32 DevKit
(`lumen-e704`, profile `generic_esp32_pwm`), both on `main` firmware, on the
same Wi-Fi; Home Assistant + Mosquitto from `test/integration/ha` (ports 8124 /
18830).

```bash
cd tutorial
# one-time: Manim venv (uv-managed Python, which ships the C headers Manim needs)
uv venv .venv --python 3.12 --python-preference only-managed
PKG_CONFIG_PATH=$(.venv/bin/python -c 'import sys;print(sys.base_prefix)')/lib/pkgconfig \
  uv pip install --python .venv/bin/python manim

# demo/.secrets.env (gitignored, mode 600):
#   LUMEN_PASSWORD=…  LUMEN_KC=http://<kc ip>  LUMEN_DEV=http://<devkit ip>
python3 demo/seed.py                 # demo content on both controllers (idempotent, resets takes)
demo/run.sh record                   # every section (first_login needs a factory-fresh board: SKIP_SETUP=1)
demo/run.sh overlays && demo/run.sh compose && demo/run.sh concat
```

Output: `demo/final/lumen-project-demo.mp4` and `demo/final/chapters.txt`
(paste into the YouTube description). `demo/run.sh grid <section|final>` makes
a contact sheet for review.

## Short (TikTok / YouTube Shorts / Reels)

`demo/short.json` picks 8 beats from the demo recordings plus an end card.
`.venv/bin/python demo/video/short_build.py all` writes
`demo/final/lumen-short.mp4` (1080×1920, 39.6 s, silent track) and
`demo/final/lumen-short-cover.jpg`. Add `--music <file>` for a licensed track.

What is real: every state change in the video is a command to the
controllers, and the floor-plan glow, Home Assistant entities and peer
controller all show device-reported state. The MAC address and Wi-Fi name are
blurred; passwords are typed only into password fields.
