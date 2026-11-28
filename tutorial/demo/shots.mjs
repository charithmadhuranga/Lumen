// Lumen project demo: one async function per section id in demo.json.
// Recorder API: goto, move, hover, click, type, press, drag, scroll, wait,
// caption, spotlight, redact, mark (youtube-video-creator skill,
// references/screen-demo.md). Captions are called just before the action
// they describe. Every state change shown is a real command to the
// controllers; nothing is simulated in the browser.
//
// Env (tutorial/demo/.secrets.env + test/integration/ha/.env):
//   LUMEN_KC, LUMEN_DEV, LUMEN_PASSWORD, HA_URL (default 127.0.0.1:8124),
//   HA_OWNER_PASSWORD. Secrets are typed only into password fields.

const env = (k, d) => {
  const v = process.env[k] ?? d;
  if (v === undefined) throw new Error(`${k} not set (source the env files)`);
  return v;
};
const KC = () => env("LUMEN_KC");
const DEV = () => env("LUMEN_DEV");
const HA = () => env("HA_URL", "http://127.0.0.1:8124");

const role = (demo, r, name, opts = {}) => demo.page.getByRole(r, { name, ...opts }).first();

async function signIn(page, base) {
  const r = await page.request.post(`${base}/api/v1/auth/login`, {
    data: { username: "admin", password: env("LUMEN_PASSWORD") },
  });
  if (!r.ok()) throw new Error(`login to ${base} failed: ${r.status()}`);
}

/** Real light command through the controller API (used only for resets). */
async function command(page, base, light, body) {
  const s = await page.request.get(`${base}/api/v1/auth/session`);
  const csrf = (await s.json()).data?.csrfToken;
  await page.request.post(`${base}/api/v1/lights/${light}/command`, {
    data: body, headers: csrf ? { "X-CSRF-Token": csrf } : {},
  });
}

export async function setup({ page }) {
  if (process.env.SKIP_SETUP) return; // first_login runs before the password exists
  await signIn(page, KC());
  await signIn(page, DEV());
  if (process.env.HA_OWNER_PASSWORD) {
    await page.goto(HA() + "/", { waitUntil: "networkidle" });
    if (await page.locator("input[name=username]").count()) {
      await page.locator("input[name=username]").fill("lumen-test");
      await page.locator("input[name=password]").fill(env("HA_OWNER_PASSWORD"));
      await page.getByRole("button", { name: /log in/i }).click();
      await page.waitForURL(/\/(home|lovelace)/, { timeout: 30000 });
    }
  }
}

// ------------------------------------------------------------ 01 first start
export async function first_login(demo) {
  const p = demo.page;
  await p.waitForSelector("#username");
  demo.caption("Every controller starts locked", { sub: "Sign in with the default admin account" });
  await demo.wait(1.6);
  await demo.type("#username", "admin");
  await demo.type("#password", "admin", { secret: true });
  await demo.click("button[type=submit]");
  await p.waitForSelector("#new");
  demo.caption("The default password must go", { sub: "Nothing works until you choose your own" });
  await demo.wait(1.2);
  await demo.spotlight("form", { dur: 2.6, pad: 6 });
  await demo.wait(1.4);
  await demo.type("#current", "admin", { secret: true });
  await demo.type("#new", env("LUMEN_PASSWORD"), { secret: true, hold: 600 });
  demo.caption("Strength is checked as you type", { sub: "Stored as a salted PBKDF2 hash, never in plain text" });
  await demo.spotlight("text=Strength:", { dur: 2.2, pad: 6 });
  await demo.wait(1.2);
  await demo.type("#confirm", env("LUMEN_PASSWORD"), { secret: true });
  await demo.click("button[type=submit]");
  await p.waitForURL((u) => !u.pathname.startsWith("/setup"), { timeout: 15000 });
  demo.caption("Signed in and ready", { sub: "An empty controller, waiting for lights" });
  await demo.wait(3);
}

// -------------------------------------------------------------- 02 overview
export async function overview(demo) {
  const p = demo.page;
  await p.getByText("lights on").first().waitFor();
  demo.caption("The whole home on one screen", { sub: "Live state pushed from the controller" });
  await demo.wait(1.0);
  await demo.spotlight(p.getByText(/of \d+ lights on/).first(), { label: "Live count", dur: 2.4 });
  await demo.wait(2.4);
  demo.caption("Dim a whole room", { sub: "One slider drives every light in it" });
  await demo.drag(role(demo, "slider", "Living Room brightness"), { dx: -110 });
  await demo.wait(0.8);
  demo.caption("Switch a room on");
  await demo.click(role(demo, "button", /^Bedroom: .*Toggle/));
  await demo.wait(1.6);
  demo.caption("Everything off with one tap");
  await demo.click(role(demo, "button", "All off"), { hold: 2000 });
  demo.caption("Scenes bring it all back", { sub: "Each light fades to its saved level" });
  await demo.click(role(demo, "button", /Evening/), { hold: 3200 });
}

// --------------------------------------------------------------- 03 devices
export async function devices(demo) {
  const p = demo.page;
  await p.getByText("Ceiling").first().waitFor();
  demo.caption("Every light the controller drives", { sub: "Type, output and room at a glance" });
  await demo.wait(1.2);
  await demo.spotlight(p.locator("text=pca9685_0 ch 1").first(), { label: "Hardware output", dur: 2.2 });
  await demo.wait(2.2);
  demo.caption("Add a light in seconds");
  await demo.click(role(demo, "button", "Add light"));
  await p.getByRole("dialog").waitFor();
  await demo.type(role(demo, "textbox", "Name"), "Reading lamp", { delay: 70 });
  demo.caption("Pick the light type", { sub: "Switch, dimmer, RGB, RGBW or tunable white" });
  await demo.click(role(demo, "combobox", "Light type"));
  await demo.wait(0.8);
  await demo.click(p.getByRole("option", { name: /Dimmer/ }).first());
  demo.caption("Map it to a free output", { sub: "Outputs in use are never offered twice" });
  await demo.click(role(demo, "combobox", "Hardware output"));
  await demo.wait(1.0);
  await demo.click(p.getByRole("option", { name: /14/ }).first());
  await demo.click(role(demo, "combobox", "Room"));
  await demo.wait(0.6);
  await demo.click(p.getByRole("option", { name: "Living Room" }).first());
  await demo.click(p.getByRole("dialog").getByRole("button", { name: "Add light" }));
  await p.getByText("Reading lamp").first().waitFor();
  await demo.scroll(500, { duration: 900, hold: 200 });
  await demo.spotlight(p.getByText("reading_lamp").first(), { pad: 8, dur: 2.2 });
  demo.caption("Ready to use everywhere", { sub: "Rooms, scenes, automations, MQTT and the API" });
  await demo.wait(2.2);
  demo.caption("Physical inputs, live", { sub: "Wall switches and sensors on the digital inputs" });
  await demo.scroll(-500, { duration: 700, hold: 200 });
  await demo.click(role(demo, "tab", "Inputs"), { hold: 2600 });
}

// ----------------------------------------------------------------- 04 rooms
export async function rooms(demo) {
  const p = demo.page;
  await p.getByText("Living Room").first().waitFor();
  demo.caption("Rooms group your lights");
  await demo.wait(1.2);
  await demo.click(p.getByRole("link", { name: /Living Room/ }).first());
  await role(demo, "heading", "Living Room").waitFor();
  demo.caption("Controls match each light type", { sub: "No control appears that a light cannot do" });
  await demo.wait(1.4);
  await demo.drag(role(demo, "slider", "Ceiling brightness"), { dx: 60 });
  demo.caption("Full colour for RGB lights");
  await demo.click(role(demo, "button", "Show controls for TV backlight"));
  await demo.wait(0.6);
  await demo.drag(role(demo, "slider", "Hue"), { dx: -150 });
  await demo.drag(role(demo, "slider", "Saturation and brightness"), { dx: -40, dy: 20 });
  await demo.wait(0.8);
  demo.caption("Built-in effects", { sub: "Candlelight, sunrise, rainbow and more" });
  await demo.click(role(demo, "combobox", "Effect"));
  await demo.wait(0.8);
  await demo.click(p.getByRole("option", { name: /Rainbow/i }).first(), { hold: 2600 });
  await demo.click(role(demo, "combobox", "Effect"));
  await demo.click(p.getByRole("option", { name: /No effect/i }).first());
  await demo.click(role(demo, "button", "Hide controls for TV backlight"));
  demo.caption("Warm to cool white", { sub: "Tunable white lights get a colour temperature control" });
  await demo.click(role(demo, "button", "Show controls for Floor lamp"), { hold: 1200 });
  const cct = p.getByRole("slider", { name: /temperature/i }).first();
  if (await cct.count()) await demo.drag(cct, { dx: 120 });
  await demo.wait(1.5);
}

// ---------------------------------------------------------------- 05 scenes
export async function scenes(demo) {
  const p = demo.page;
  await p.getByText("Evening").first().waitFor();
  demo.caption("Save the lights as they are now", { sub: "A scene is a snapshot of real light states" });
  await demo.wait(1.0);
  await demo.click(role(demo, "button", "Save current state"));
  await p.getByRole("dialog").waitFor();
  await demo.type(role(demo, "textbox", "Scene name"), "Movie night", { delay: 70 });
  demo.caption("Choose which lights it covers");
  await demo.click(p.getByRole("checkbox", { name: /^Porch/ }).first());
  await demo.click(p.getByRole("checkbox", { name: /^Garden/ }).first());
  await demo.wait(0.6);
  await demo.click(role(demo, "button", "Save scene"));
  await p.getByText("Movie night").first().waitFor();
  await demo.wait(1.2);
  demo.caption("Recall any scene with a fade", { sub: "Every light moves smoothly to its saved level" });
  await demo.click(p.locator("div", { hasText: /^Evening/ }).getByRole("button", { name: "Activate" }).first(), { hold: 2600 });
  await demo.click(p.locator("div", { hasText: /^Movie night/ }).getByRole("button", { name: "Activate" }).first(), { hold: 2800 });
}

// ------------------------------------------------------------ 06 floor plans
export async function floorplans(demo) {
  const p = demo.page;
  demo.caption("Upload a drawing of your home", { sub: "Any SVG, checked and sanitised on upload" });
  await demo.wait(1.2);
  await demo.click(p.getByRole("link", { name: /Home/ }).last());
  await p.getByRole("group", { name: /Floor plan/ }).waitFor();
  await demo.wait(1.0);
  demo.caption("Each glow is the real output", { sub: "Colour and brightness come from the controller" });
  await demo.wait(2.4);
  demo.caption("Tap a light to switch it");
  await demo.click(p.getByRole("switch", { name: "Pendants", exact: true }), { hold: 1400 });
  await demo.click(p.getByRole("switch", { name: "Bedside", exact: true }), { hold: 1400 });
  await demo.click(p.getByRole("switch", { name: "Garden", exact: true }), { hold: 1600 });
  demo.caption("Edit mode places the lights", { sub: "Snapping, alignment, undo and room outlines" });
  await demo.click(role(demo, "radio", "Edit"));
  await p.getByRole("toolbar", { name: "Editor tools" }).waitFor({ timeout: 30000 });
  await demo.wait(1.0);
  await demo.spotlight(p.getByRole("toolbar", { name: "Editor tools" }), { dur: 2.4 });
  await demo.wait(1.6);
  const tray = p.getByText("Reading lamp").first();
  if (await tray.count()) {
    demo.caption("Drag a new light onto the plan");
    const canvas = p.locator("canvas").first();
    const b = await canvas.boundingBox();
    await demo.drag(tray, { x: b.x + b.width * 0.36, y: b.y + b.height * 0.42 }, { steps: 30 });
    await demo.wait(1.0);
    await demo.click(role(demo, "button", "Save"), { hold: 1500 });
  }
  await demo.click(role(demo, "radio", "Control"), { hold: 2500 });
}

// ----------------------------------------------------------- 07 automations
export async function automations(demo) {
  const p = demo.page;
  await p.getByText("Evening lights at sunset").first().waitFor();
  demo.caption("Automations run on the controller", { sub: "No internet, cloud or Home Assistant needed" });
  await demo.wait(1.4);
  await demo.spotlight(p.getByText("Sunrise").first(), { label: "Local sun times", dur: 2.4 });
  await demo.wait(2.0);
  demo.caption("Build one in a simple form");
  await demo.click(p.getByRole("link", { name: "New automation" }));
  await role(demo, "heading", "New automation").waitFor();
  await demo.type(role(demo, "textbox", "Name"), "Porch button", { delay: 65 });
  demo.caption("Trigger: a wall button", { sub: "Inputs, time, sunrise, sunset, MQTT and more" });
  await demo.click(role(demo, "combobox", "Trigger type"));
  await demo.click(p.getByRole("option", { name: /Button press/i }).first());
  await demo.click(role(demo, "combobox", "Input"));
  await demo.click(p.getByRole("option", { name: /DI1\b/ }).first());
  demo.caption("Action: toggle the porch light");
  await demo.click(role(demo, "combobox", "Action type"));
  await demo.click(p.getByRole("option", { name: /Toggle light/i }).first());
  await demo.click(role(demo, "combobox", "Light"));
  await demo.click(p.getByRole("option", { name: "Porch" }).first());
  await demo.wait(0.8);
  demo.caption("Or wire it up visually", { sub: "The same automation as a node graph" });
  await demo.click(role(demo, "tab", "Advanced"));
  await p.locator(".react-flow__node").first().waitFor({ timeout: 30000 });
  await demo.wait(2.8);
  await demo.click(role(demo, "button", "Save"), { hold: 1200 });
  await demo.click(p.getByRole("link", { name: /Automations/ }).first());
  await p.getByText("Porch button").first().waitFor();
  await demo.wait(1.0);
  demo.caption("Test any automation on the spot", { sub: "Runs now, with loop and rate protection" });
  await demo.click(role(demo, "button", "Test Evening lights at sunset"), { hold: 3000 });
}

// -------------------------------------------------------------- 08 hardware
export async function hardware(demo) {
  const p = demo.page;
  await p.getByText("KinCony KC868-AP").first().waitFor();
  demo.caption("Pins live in board profiles", { sub: "Versioned JSON, never hard-coded in firmware" });
  await demo.wait(1.2);
  await demo.spotlight(p.getByText("KinCony KC868-AP").first(), { label: "Active profile", dur: 2.4 });
  await demo.wait(2.2);
  demo.caption("Every GPIO, checked for conflicts", { sub: "Strapping, flash and input-only pins flagged" });
  await demo.click(role(demo, "tab", "Pin mapping"), { hold: 2600 });
  await demo.scroll(260, { duration: 900 });
  demo.caption("Drivers and buses, live health");
  await demo.click(role(demo, "tab", "Peripherals"), { hold: 1400 });
  const scan = p.getByRole("button", { name: /Scan I.?C bus/i }).first();
  if (await scan.count()) {
    demo.caption("Scan the I²C bus", { sub: "Finds the PCA9685 and both input expanders" });
    await demo.click(scan, { hold: 2600 });
  }
  demo.caption("Test any output directly", { sub: "PCA9685 duty cycles, read back from the chip" });
  await demo.click(role(demo, "tab", "Outputs"), { hold: 1800 });
  demo.caption("Diagnostics for the long run", { sub: "Heap, timing, errors and dropped commands" });
  await demo.click(role(demo, "tab", "Diagnostics"), { hold: 3200 });
}

// --------------------------------------------------------- 09 home assistant
export async function homeassistant(demo) {
  const p = demo.page;
  await p.getByText("MQTT").first().waitFor();
  demo.caption("MQTT with Home Assistant discovery", { sub: "Passwords are write-only and never shown" });
  await demo.wait(1.2);
  await demo.spotlight(p.getByText(/Connected ·/).first(), { label: "Broker connected", dur: 2.4 });
  await demo.wait(2.0);
  await demo.scroll(420, { duration: 900 });
  await demo.spotlight(p.getByText("Publish Home Assistant discovery").first(), { dur: 2.2 });
  await demo.wait(2.2);
  demo.caption("Home Assistant finds the controller", { sub: "One device with every light and diagnostics" });
  await demo.goto(HA() + "/config/devices/dashboard", { wait: 1500 });
  await demo.click(p.getByText(/Lumen 0798/i).first());
  await p.getByRole("heading", { name: "Controls" }).waitFor();
  await demo.wait(1.2);
  await demo.spotlight(p.getByText("KinCony KC868-AP").first(), { label: "Discovered over MQTT", dur: 2.6 });
  await demo.wait(2.4);
  demo.caption("Control Lumen from Home Assistant", { sub: "Commands go over MQTT to the real outputs" });
  await demo.click(p.getByRole("switch", { name: "Toggle Lumen 0798 Pendants" }), { hold: 1600 });
  await demo.click(p.getByRole("switch", { name: "Toggle Lumen 0798 Bedside" }), { hold: 1600 });
  await demo.click(p.getByRole("switch", { name: "Toggle Lumen 0798 Pendants" }), { hold: 1800 });
  demo.caption("Changes show up everywhere instantly", { sub: "The Lumen app reflects what Home Assistant did" });
  await demo.goto(KC() + "/rooms/kitchen", { wait: 1200 });
  await demo.spotlight(p.getByText("Pendants").first(), { dur: 2.4 });
  await demo.wait(2.6);
}

// --------------------------------------------------- 10 multiple controllers
export async function multi(demo) {
  const p = demo.page;
  await demo.scroll(1400, { duration: 1000 });
  demo.caption("Controllers link over ESP-NOW", { sub: "Signed radio frames, no router or broker" });
  await demo.spotlight(p.getByText("Multi-controller (ESP-NOW)").first(), { label: "1 peer online", dur: 2.6 });
  await demo.wait(2.6);
  demo.caption("A second board, a generic ESP32", { sub: "Its lights appear here automatically" });
  await demo.goto("/devices", { wait: 800 });
  await demo.click(role(demo, "tab", "Controllers"));
  await demo.wait(1.0);
  await demo.spotlight(p.getByRole("region", { name: /Controller lumen-/ }).first(), { dur: 2.4 });
  await demo.wait(1.6);
  demo.caption("Switch its lights from here");
  await demo.click(p.getByRole("switch", { name: /^Desk lamp/ }).first(), { hold: 1800 });
  demo.caption("The other controller sees it at once", { sub: "This is lumen-e704's own web app" });
  await demo.goto(DEV() + "/rooms/studio", { wait: 1200 });
  await demo.wait(2.2);
  await demo.click(role(demo, "button", /^Desk lamp: .*Toggle/), { hold: 2000 });
}

// --------------------------------------------------------------- 11 network
export async function network(demo) {
  const p = demo.page;
  await p.getByText("Wi-Fi").first().waitFor();
  demo.caption("Wi-Fi with a safe fallback", { sub: "A WPA2 hotspot only when Wi-Fi is unreachable" });
  await demo.wait(1.2);
  await demo.spotlight(p.getByText(/Connected ·/).first(), { dur: 2.2 });
  await demo.wait(2.2);
  await demo.scroll(380, { duration: 1000 });
  demo.caption("Never an open management hotspot");
  await demo.spotlight(p.getByText("is always WPA2-protected").first(), { dur: 2.4 });
  await demo.wait(2.2);
  demo.caption("Found by name on your network", { sub: "mDNS hostname, time zone and wired Ethernet" });
  await demo.scroll(480, { duration: 1000 });
  await demo.wait(2.2);
}

// -------------------------------------------------------------- 12 settings
export async function settings(demo) {
  const p = demo.page;
  await p.getByText("Appearance").first().waitFor();
  demo.caption("Light and dark themes", { sub: "Or follow the system setting" });
  await demo.scroll(300, { duration: 800 });
  await demo.click(p.getByRole("radio", { name: "Light", exact: true }), { hold: 1800 });
  await demo.click(p.getByRole("radio", { name: "Dark", exact: true }), { hold: 1000 });
  demo.caption("Backups without secrets", { sub: "Transactional restore that keeps the last good config" });
  await demo.spotlight(p.getByText("Backup & restore").first(), { dur: 2.6 });
  await demo.wait(2.4);
  await demo.scroll(420, { duration: 900 });
  demo.caption("Safe firmware updates", { sub: "Verified before boot, rolled back if unhealthy" });
  await demo.spotlight(p.getByText(/slot ota_/).first(), { label: "A/B slots", dur: 2.6 });
  await demo.wait(2.6);
  await demo.scroll(600, { duration: 1000 });
  demo.caption("Everything about the controller", { sub: "Chip, profile, storage, memory and uptime" });
  await demo.wait(3);
}
