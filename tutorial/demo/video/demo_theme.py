"""Visual theme for narration-free screen demos.

Lumen dark theme (webui/src/styles, OKLCH tokens converted to sRGB). Rebrand this to the product being demoed: sample the colours from the app's
own design tokens so cards, captions and the recording read as one piece.
Change colours together, never one at a time.
"""

# --- palette -------------------------------------------------------------
BG = "#0b0d12"          # card background (match the app's darkest surface)
BG_GLOW = "#1a2233"     # soft radial glow behind titles
PANEL = "#13161c"       # caption panel fill
PANEL_OPACITY = 0.90    # caption panels sit over live UI: keep them solid
ACCENT = "#f4a437"      # brand accent: bars, eyebrows, spotlights
ACCENT_2 = "#ffc861"    # secondary accent (sparingly)
TEXT = "#f3f2ef"
TEXT_DIM = "#c9c8c4"
TEXT_MUTED = "#9b9fa5"
BORDER = "#2a2e34"

# --- typography ----------------------------------------------------------
# Pin fonts that exist on the render machine; Manim silently falls back
# otherwise and the series stops matching. Check with `fc-list : family`.
FONT = "Noto Sans"
FONT_MONO = "DejaVu Sans Mono"

# --- timing (seconds) ----------------------------------------------------
TITLE_SECONDS = 5.0     # opening title card
SECTION_SECONDS = 3.2   # bumper before each section clip
OUTRO_SECONDS = 5.5
XFADE = 0.5             # bumper → clip crossfade
CAPTION_IN = 0.45       # caption entrance
CAPTION_OUT = 0.35      # caption exit
WORD_LAG = 0.055        # per-word stagger in captions/titles

# --- output --------------------------------------------------------------
WIDTH, HEIGHT, FPS = 1920, 1080, 30
