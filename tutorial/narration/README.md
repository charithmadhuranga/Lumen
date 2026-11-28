# Lumen tutorial narration

Narration scripts for the narrated Lumen tutorial series (youtube-video-creator
skill, tutorial mode). One script per chapter; record one mp3 per script.

| File | Chapter | Matches demo section(s) |
|---|---|---|
| `intro.md` | What Lumen is, what the series covers | title |
| `ch1.md` | Architecture: lighting task, event bus, HAL, board profiles, storage | — |
| `ch2.md` | Flashing and the secure first start | first_login |
| `ch3.md` | Devices: light types, outputs, adding a light, inputs | devices |
| `ch4.md` | Overview, rooms, light controls, colour, effects, CCT | overview, rooms |
| `ch5.md` | Scenes | scenes |
| `ch6.md` | Floor plans: upload, control mode, editor | floorplans |
| `ch7.md` | Automations: triggers, conditions, actions, safety, testing | automations |
| `ch8.md` | Hardware: profiles, pin map, peripherals, outputs, diagnostics | hardware |
| `ch9.md` | MQTT, Home Assistant, local TCP/UDP | homeassistant |
| `ch10.md` | Multiple controllers over ESP-NOW | multi |
| `ch11.md` | Network and the security model | network |
| `ch12.md` | Settings: themes, backup/restore, updates, system | settings |
| `end.md` | Wrap-up | outro |

## Recording

- Save each recording next to its script with the same name: `intro.mp3`,
  `ch1.mp3`, … `end.mp3`.
- Read the text exactly as written, or edit the `.md` to match what you say:
  the pipeline checks every mp3 against its script word for word.
- Leave a short natural pause between paragraphs; each paragraph becomes one
  timed visual beat.
- Numbers are written as spoken where it matters ("one nine two dot one six
  eight dot four dot one").

Check before building:
`python ~/.claude/skills/youtube-video-creator-skill/templates/verify_narration.py tutorial/narration/`
