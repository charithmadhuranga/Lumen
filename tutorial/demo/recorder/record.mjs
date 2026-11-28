#!/usr/bin/env node
// Screen-demo recorder: drives a web app with Playwright and records each
// section as a crisp 1080p30 H.264 clip plus a JSON marker file.
//
//   node record.mjs <demo-dir> [--only id1,id2] [--headed]
//                   [--spec demo.json] [--shots shots.mjs] [--out recordings]
//
// <demo-dir> holds demo.json (sections, viewport, baseURL) and shots.mjs
// (one async function per section id). For each section this writes
//   <demo-dir>/recordings/<id>.mp4    the clean screen recording
//   <demo-dir>/recordings/<id>.json   duration + caption/spotlight markers
//
// Frames come from the Chrome DevTools screencast (JPEG q=95, device pixels)
// rather than Playwright's recordVideo (low-bitrate VP8), then are re-timed
// to a constant 30 fps and encoded with x264 CRF 16. A visible cursor, click
// ripples and key-press pacing are injected into the page because headless
// Chromium draws no cursor.

import { createRequire } from "node:module";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { pathToFileURL, fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------- playwright
function loadPlaywright(demoDir) {
  const roots = [
    process.env.DEMO_NODE_MODULES,
    demoDir,
    process.cwd(),
    HERE,
  ].filter(Boolean);
  for (const root of roots) {
    const req = createRequire(path.join(path.resolve(root), "noop.js"));
    for (const name of ["playwright", "@playwright/test", "playwright-core"]) {
      try {
        const mod = req(name);
        if (mod.chromium) return mod;
      } catch { /* try next */ }
    }
  }
  console.error(
    "playwright not found. Install it next to the demo (npm i -D playwright &&" +
      " npx playwright install chromium) or set DEMO_NODE_MODULES to a" +
      " node_modules folder that has it.",
  );
  process.exit(2);
}

// ------------------------------------------------------------------- cursor
// Injected into every page. Draws an arrow cursor and click ripples on top of
// everything; pointer-events: none so it never intercepts the real clicks.
const CURSOR_SCRIPT = String.raw`
(() => {
  if (window.__demo) return;
  const TOUCH = !!window.__demoTouch;
  const state = { x: innerWidth * 0.5, y: innerHeight * 0.6 };
  try {
    const saved = JSON.parse(sessionStorage.getItem("__demo_cursor") || "null");
    if (saved) Object.assign(state, saved);
  } catch {}
  let root;
  function ensure() {
    if (root && root.isConnected) return root;
    root = document.createElement("div");
    root.id = "__demo_cursor";
    root.style.cssText = "position:fixed;left:0;top:0;width:0;height:0;" +
      "z-index:2147483647;pointer-events:none;";
    const shadow = root.attachShadow({ mode: "open" });
    shadow.innerHTML = '<style>' +
      '.c{position:fixed;left:0;top:0;width:26px;height:26px;will-change:transform;' +
      'filter:drop-shadow(0 2px 3px rgba(0,0,0,.45));transition:scale .12s ease;}' +
      '.c.down{scale:.86}' +
      '.r{position:fixed;width:44px;height:44px;margin:-22px 0 0 -22px;border-radius:50%;' +
      'border:3px solid rgba(255,255,255,.95);background:rgba(255,255,255,.18);' +
      'box-shadow:0 0 0 2px rgba(0,0,0,.25);animation:rip .55s ease-out forwards}' +
      '@keyframes rip{from{transform:scale(.3);opacity:1}to{transform:scale(1.25);opacity:0}}' +
      // Touch mode: a finger dot that only shows while pressed (phones have no hover).
      '.c.t{width:40px;height:40px;border-radius:50%;background:rgba(255,255,255,.55);' +
      'border:2px solid rgba(255,255,255,.95);box-shadow:0 0 0 2px rgba(0,0,0,.25);' +
      'opacity:0;transition:opacity .12s ease,scale .12s ease;scale:1}' +
      '.c.t.down{opacity:.9;scale:.9}' +
      '</style>' +
      (TOUCH ? '<div class="c t"></div>' :
      '<svg class="c" viewBox="0 0 26 26"><path d="M3 2 L3 21 L8.2 16.4 L11.6 24 L15 22.5 ' +
      'L11.7 15 L18.5 15 Z" fill="#fff" stroke="#111" stroke-width="1.6" ' +
      'stroke-linejoin="round"/></svg>');
    (document.body || document.documentElement).appendChild(root);
    place();
    return root;
  }
  function place() {
    const c = root && root.shadowRoot.querySelector(".c");
    if (c) c.style.transform = TOUCH
      ? "translate(" + (state.x - 20) + "px," + (state.y - 20) + "px)"
      : "translate(" + (state.x - 3) + "px," + (state.y - 2) + "px)";
    try { sessionStorage.setItem("__demo_cursor", JSON.stringify(state)); } catch {}
  }
  const ease = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
  window.__demo = {
    pos: () => ({ x: state.x, y: state.y }),
    show() { ensure(); },
    moveTo(x, y, ms) {
      ensure();
      const sx = state.x, sy = state.y;
      // A gentle arc reads as a human hand rather than a robot line.
      const bend = Math.min(80, Math.hypot(x - sx, y - sy) * 0.12);
      return new Promise((done) => {
        const t0 = performance.now();
        function step(now) {
          const t = Math.min(1, (now - t0) / ms), k = ease(t);
          const arc = Math.sin(Math.PI * k) * bend;
          state.x = sx + (x - sx) * k;
          state.y = sy + (y - sy) * k - arc;
          place();
          if (t < 1) requestAnimationFrame(step); else done();
        }
        requestAnimationFrame(step);
      });
    },
    press(down) {
      ensure();
      root.shadowRoot.querySelector(".c").classList.toggle("down", down);
    },
    ripple() {
      ensure();
      const r = document.createElement("div");
      r.className = "r";
      r.style.left = state.x + "px";
      r.style.top = state.y + "px";
      root.shadowRoot.appendChild(r);
      setTimeout(() => r.remove(), 700);
    },
  };
  const boot = () => ensure();
  if (document.readyState === "loading") addEventListener("DOMContentLoaded", boot);
  else boot();
  // SPA re-renders can wipe <body> children; re-attach when that happens.
  new MutationObserver(() => { if (root && !root.isConnected) ensure(); })
    .observe(document.documentElement, { childList: true, subtree: true });
})();
`;

// --------------------------------------------------------------- recorder
class Recorder {
  constructor(page, cdp, frameDir) {
    this.page = page;
    this.cdp = cdp;
    this.frameDir = frameDir;
    this.frames = []; // {file, ts}
    this.t0 = null;
    this.captions = [];
    this.spotlights = [];
    this.marks = [];
    this.pending = [];
    this.paused = false; // true while goto() loads: blank/white load frames are dropped
    this.cut = 0;        // seconds of page-load time removed from the timeline
    this.pauseAt = null;
  }

  async start(size) {
    this.cdp.on("Page.screencastFrame", (f) => {
      if (this.paused && this.frames.length) {
        // Hold the last good frame instead of recording the white page a
        // cross-origin navigation shows before the new app paints.
        this.cdp.send("Page.screencastFrameAck", { sessionId: f.sessionId }).catch(() => {});
        return;
      }
      const last = this.frames.length ? this.frames[this.frames.length - 1].ts : -Infinity;
      const ts = Math.max(f.metadata.timestamp - this.cut, last + 0.001); // timeline seconds
      const file = path.join(this.frameDir, `f${String(this.frames.length).padStart(6, "0")}.jpg`);
      this.pending.push(fs.promises.writeFile(file, Buffer.from(f.data, "base64")));
      this.frames.push({ file, ts });
      this.cdp.send("Page.screencastFrameAck", { sessionId: f.sessionId }).catch(() => {});
    });
    await this.cdp.send("Page.startScreencast", {
      format: "jpeg", quality: 95, maxWidth: size.width, maxHeight: size.height, everyNthFrame: 1,
    });
    // Force a first frame so t0 is anchored even on a static page.
    await this.page.evaluate(() => window.__demo && window.__demo.show());
    const wait = Date.now();
    while (!this.frames.length && Date.now() - wait < 3000) await sleep(20);
    this.t0 = this.frames.length ? this.frames[0].ts : Date.now() / 1000;
  }

  now() {
    return Date.now() / 1000 - this.t0 - this.cut;
  }

  /** Navigation is a cut: the load time between pause() and resume() is
   *  removed from the timeline (frames, captions and spotlights all shift),
   *  so a slow server never shows up as a white flash or a frozen frame. */
  pause() {
    this.paused = true;
    this.pauseAt = Date.now() / 1000;
  }

  resume(keep = 0.15) {
    if (this.pauseAt !== null) this.cut += Math.max(0, Date.now() / 1000 - this.pauseAt - keep);
    this.pauseAt = null;
    this.paused = false;
  }

  async stop() {
    const end = Date.now() / 1000;
    await this.cdp.send("Page.stopScreencast").catch(() => {});
    await Promise.all(this.pending);
    return end - this.t0 - this.cut;
  }
}

// ------------------------------------------------------------- demo API
function makeDemo(page, rec, cfg) {
  const pace = cfg.pace ?? 1; // global speed multiplier (>1 = slower)
  const ms = (v) => Math.round(v * pace);

  async function box(target) {
    const loc = typeof target === "string" ? page.locator(target).first() : target;
    await loc.scrollIntoViewIfNeeded().catch(() => {});
    await loc.waitFor({ state: "visible", timeout: 15000 });
    const b = await loc.boundingBox();
    if (!b) throw new Error(`no bounding box for ${target}`);
    return { loc, b };
  }

  const demo = {
    page,
    config: cfg,
    /** Seconds since this section's recording started. */
    time: () => rec.now(),

    async goto(url, { wait = 900 } = {}) {
      rec.pause(); // cut straight from the old page to the painted new one
      try {
        await page.goto(new URL(expand(url), cfg.baseURL).href, { waitUntil: "networkidle" })
          .catch(() => page.waitForLoadState("load"));
        await page.evaluate(() => window.__demo && window.__demo.show());
        await sleep(350);
      } finally {
        rec.resume();
      }
      await sleep(ms(wait));
    },

    async wait(seconds) {
      await sleep(ms(seconds * 1000));
    },

    async move(target, { duration = 650, offset } = {}) {
      let x, y;
      if (target && typeof target === "object" && "x" in target && "y" in target && !target.locator) {
        ({ x, y } = target);
      } else {
        const { b } = await box(target);
        x = b.x + (offset?.x ?? b.width / 2);
        y = b.y + (offset?.y ?? b.height / 2);
      }
      const from = await page.evaluate(() => window.__demo.pos());
      const dist = Math.hypot(x - from.x, y - from.y);
      const dur = ms(Math.max(260, Math.min(duration * 1.6, duration * (0.5 + dist / 900))));
      await page.evaluate(([x, y, d]) => window.__demo.moveTo(x, y, d), [x, y, dur]);
      await page.mouse.move(x, y);
      return { x, y };
    },

    async hover(target, opts = {}) {
      await demo.move(target, opts);
      await sleep(ms(opts.hold ?? 450));
    },

    async click(target, { hold = 550, offset, before = 140 } = {}) {
      await demo.move(target, { offset });
      await sleep(ms(before));
      await page.evaluate(() => { window.__demo.press(true); window.__demo.ripple(); });
      await page.mouse.down();
      await sleep(70);
      await page.mouse.up();
      await page.evaluate(() => window.__demo.press(false));
      await sleep(ms(hold));
    },

    /** Type like a person. secret: true types fast and never logs the text. */
    async type(target, text, { delay = 55, secret = false, clear = true, hold = 350 } = {}) {
      if (target) await demo.click(target, { hold: 200 });
      if (clear) {
        await page.keyboard.press("ControlOrMeta+A");
        await page.keyboard.press("Backspace");
      }
      await page.keyboard.type(text, { delay: secret ? 25 : ms(delay) });
      await sleep(ms(hold));
    },

    async press(key, { hold = 400 } = {}) {
      await page.keyboard.press(key);
      await sleep(ms(hold));
    },

    /** Drag (sliders, canvas editors). Cursor and real mouse move together. */
    async drag(from, to, { steps = 24, hold = 500 } = {}) {
      const a = await demo.move(from);
      await page.evaluate(() => window.__demo.press(true));
      await page.mouse.down();
      let bx, by;
      if (to && typeof to === "object" && "dx" in to) {
        bx = a.x + to.dx; by = a.y + (to.dy ?? 0);
      } else if (to && typeof to === "object" && "x" in to && !to.locator) {
        bx = to.x; by = to.y;
      } else {
        const { b } = await box(to);
        bx = b.x + b.width / 2; by = b.y + b.height / 2;
      }
      for (let i = 1; i <= steps; i++) {
        const k = i / steps, e = k < 0.5 ? 2 * k * k : 1 - Math.pow(-2 * k + 2, 2) / 2;
        const x = a.x + (bx - a.x) * e, y = a.y + (by - a.y) * e;
        await page.mouse.move(x, y);
        await page.evaluate(([x, y]) => window.__demo.moveTo(x, y, 16), [x, y]);
        await sleep(ms(22));
      }
      await page.mouse.up();
      await page.evaluate(() => window.__demo.press(false));
      await sleep(ms(hold));
    },

    /** Smooth scroll an element (or the page) by dy pixels. */
    async scroll(dy, { target = null, duration = 900, hold = 400 } = {}) {
      if (target) await demo.move(target);
      const steps = Math.max(8, Math.round(duration / 30));
      for (let i = 0; i < steps; i++) {
        await page.mouse.wheel(0, dy / steps);
        await sleep(ms(duration / steps));
      }
      await sleep(ms(hold));
    },

    /**
     * Lower-third caption, timed to *now*. Non-blocking: the action keeps
     * running while the caption animates in the overlay pass.
     *   text  ≤ 8 words headline     sub  optional ≤ 12 words detail
     *   dur   seconds on screen (default: reading time)   pos 'bottom'|'top'
     */
    caption(text, { sub = "", dur, pos = "bottom" } = {}) {
      const words = (text + " " + sub).trim().split(/\s+/).length;
      const d = dur ?? Math.max(2.6, Math.min(6.5, 1.2 + words * 0.32));
      rec.captions.push({ t: +rec.now().toFixed(3), text, sub, dur: d, pos });
    },

    /** Animated outline around an element (viewBox = the 1920x1080 frame). */
    async spotlight(target, { dur = 2.4, pad = 10, label = "" } = {}) {
      const { b } = await box(target);
      const s = cfg.deviceScaleFactor ?? 1;
      rec.spotlights.push({
        t: +rec.now().toFixed(3), dur, label,
        x: (b.x - pad) * s, y: (b.y - pad) * s, w: (b.width + 2 * pad) * s, h: (b.height + 2 * pad) * s,
      });
    },

    /**
     * Blur sensitive UI (MAC addresses, SSIDs, serials, e-mails) for the rest
     * of the page's life: by CSS selector and/or any element whose text or
     * input value matches a regex. Re-applied automatically as the SPA
     * re-renders. Redaction is presentation only; it never changes state.
     */
    async redact({ selector = null, text = null, flags = "i", blur = 7 } = {}) {
      await page.evaluate(([sel, src, fl, px]) => {
        let css = document.getElementById("__demo_redact");
        if (!css) {
          css = document.createElement("style");
          css.id = "__demo_redact";
          document.head.appendChild(css);
        }
        if (sel) css.textContent += `${sel}{filter:blur(${px}px)!important;}`;
        if (!src) return;
        const re = new RegExp(src, fl);
        const apply = () => {
          const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
          for (let n = walker.nextNode(); n; n = walker.nextNode()) {
            if (re.test(n.nodeValue || "") && n.parentElement && n.parentElement.id !== "__demo_cursor") {
              n.parentElement.style.filter = `blur(${px}px)`;
            }
          }
          for (const el of document.querySelectorAll("input, textarea")) {
            if (re.test(el.value || "")) el.style.filter = `blur(${px}px)`;
          }
        };
        apply();
        (window.__demoRedactors ||= []).push(apply);
        if (!window.__demoRedactObs) {
          window.__demoRedactObs = new MutationObserver(() => window.__demoRedactors.forEach((f) => f()));
          window.__demoRedactObs.observe(document.body, { subtree: true, childList: true, characterData: true });
        }
      }, [selector, text, flags, blur]);
    },

    mark(name) {
      rec.marks.push({ t: +rec.now().toFixed(3), name });
    },
  };
  return demo;
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** Expand ${VAR} from the environment (device IPs change between sessions). */
const expand = (s) =>
  s.replace(/\$\{(\w+)\}/g, (_, k) => {
    if (process.env[k] === undefined) throw new Error(`env var ${k} is not set`);
    return process.env[k];
  });

// ------------------------------------------------------------- encoding
function encode(frames, duration, out, size, fps = 30) {
  if (!frames.length) throw new Error("no frames captured");
  const t0 = frames[0].ts;
  const lines = ["ffconcat version 1.0"];
  for (let i = 0; i < frames.length; i++) {
    const next = i + 1 < frames.length ? frames[i + 1].ts - t0 : duration;
    const d = Math.max(0.001, next - (frames[i].ts - t0));
    lines.push(`file '${frames[i].file}'`, `duration ${d.toFixed(4)}`);
  }
  lines.push(`file '${frames[frames.length - 1].file}'`); // concat demuxer quirk
  const list = path.join(path.dirname(frames[0].file), "frames.ffconcat");
  fs.writeFileSync(list, lines.join("\n"));
  const r = spawnSync("ffmpeg", [
    "-y", "-v", "error", "-f", "concat", "-safe", "0", "-i", list,
    "-vf", `fps=${fps},scale=${size.width}:${size.height}:flags=lanczos,format=yuv420p`,
    "-t", duration.toFixed(3),
    "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-tune", "stillimage",
    "-movflags", "+faststart", out,
  ], { stdio: "inherit" });
  if (r.status !== 0) throw new Error(`ffmpeg failed for ${out}`);
}

// ------------------------------------------------------------------ main
async function main() {
  const args = process.argv.slice(2);
  const valued = new Set(["--only", "--spec", "--shots", "--out"]);
  const demoDir = path.resolve(
    args.find((a, i) => !a.startsWith("--") && !valued.has(args[i - 1])) ?? ".");
  const onlyArg = args.indexOf("--only");
  const only = onlyArg >= 0 ? new Set(args[onlyArg + 1].split(",")) : null;
  const headed = args.includes("--headed");
  // --spec / --shots / --out let one project keep several takes side by side,
  // e.g. a desktop demo and a mobile take for shorts.
  const opt = (name, dflt) => {
    const i = args.indexOf(name);
    return i >= 0 ? args[i + 1] : dflt;
  };

  const spec = JSON.parse(fs.readFileSync(path.join(demoDir, opt("--spec", "demo.json")), "utf8"));
  const shots = await import(pathToFileURL(path.join(demoDir, opt("--shots", "shots.mjs"))).href);
  const rec = spec.recording ?? {};
  const out = { width: rec.width ?? 1920, height: rec.height ?? 1080 };
  const dsf = rec.deviceScaleFactor ?? 1.25;
  const cfg = {
    ...rec,
    deviceScaleFactor: dsf,
    baseURL: expand(process.env.DEMO_BASE_URL ?? rec.baseURL),
    env: process.env,
  };
  const outDir = path.join(demoDir, opt("--out", "recordings"));
  fs.mkdirSync(outDir, { recursive: true });

  const { chromium } = loadPlaywright(demoDir);
  const browser = await chromium.launch({ headless: !headed, args: ["--hide-scrollbars"] });
  const context = await browser.newContext({
    viewport: { width: Math.round(out.width / dsf), height: Math.round(out.height / dsf) },
    deviceScaleFactor: dsf,
    colorScheme: rec.colorScheme ?? "dark",
    reducedMotion: "no-preference",
    ignoreHTTPSErrors: true,
    locale: rec.locale ?? "en-US",
    // "mobile": true emulates a phone: mobile layout, touch events, no hover.
    isMobile: !!rec.mobile,
    hasTouch: !!rec.mobile,
  });
  if (rec.mobile) await context.addInitScript("window.__demoTouch = true;");
  await context.addInitScript(CURSOR_SCRIPT);

  // Optional one-off setup (log in, seed data) — not recorded.
  if (shots.setup) {
    const page = await context.newPage();
    await shots.setup({ page, context, config: cfg });
    await page.close();
  }

  let failed = 0;
  for (const section of spec.sections) {
    if (only && !only.has(section.id)) continue;
    const fn = shots[section.id] ?? shots.default?.[section.id];
    if (!fn) {
      console.error(`! no shot function exported for section '${section.id}'`);
      failed++;
      continue;
    }
    const page = await context.newPage();
    const cdp = await context.newCDPSession(page);
    if (section.start) {
      // Land on the section's start page before recording begins.
      await page.goto(new URL(expand(section.start), cfg.baseURL).href, { waitUntil: "networkidle" })
        .catch(() => page.waitForLoadState("load"));
      await sleep(600);
    } else {
      await page.goto("about:blank");
    }
    const frameDir = fs.mkdtempSync(path.join(os.tmpdir(), `demo-${section.id}-`));
    const recorder = new Recorder(page, cdp, frameDir);
    const demo = makeDemo(page, recorder, cfg);
    // Redactions listed in demo.json apply BEFORE the first frame; a
    // redact() call inside the shot would leave the opening frames clear.
    for (const r of section.redact ?? []) await demo.redact(r);
    if (section.redact?.length) await sleep(150);
    console.log(`● recording ${section.id} — ${section.title}`);
    await recorder.start(out);
    let error = null;
    try {
      await sleep(400);
      await fn(demo);
      await sleep(Math.round((section.tail ?? 0.8) * 1000));
    } catch (e) {
      error = e;
    }
    const duration = await recorder.stop();
    await page.close();
    if (error) {
      // Playwright's call log (2nd+ lines) names the locator that failed.
      const lines = String(error.message).split("\n").filter((l) => l.trim());
      console.error(`✗ ${section.id}: ${lines.slice(0, 4).join("\n    ")}`);
      fs.rmSync(frameDir, { recursive: true, force: true });
      failed++;
      continue;
    }
    const mp4 = path.join(outDir, `${section.id}.mp4`);
    encode(recorder.frames, duration, mp4, out);
    fs.writeFileSync(path.join(outDir, `${section.id}.json`), JSON.stringify({
      id: section.id,
      duration: +duration.toFixed(3),
      frames: recorder.frames.length,
      size: out,
      captions: recorder.captions,
      spotlights: recorder.spotlights,
      marks: recorder.marks,
    }, null, 2));
    fs.rmSync(frameDir, { recursive: true, force: true });
    console.log(`  ✓ ${path.relative(process.cwd(), mp4)}  ${duration.toFixed(1)}s, ` +
      `${recorder.frames.length} frames, ${recorder.captions.length} captions`);
  }
  await browser.close();
  if (failed) process.exit(1);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
