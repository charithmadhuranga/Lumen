// Mobile take of the Lumen demo (390x780 CSS, touch) for the vertical short.
// Same rules as shots.mjs: real commands only; captions here are timing
// markers that short.json beats can point at ("caption": i).
export { setup } from "./shots.mjs";

const DEV = () => process.env.LUMEN_DEV;
const role = (demo, r, name, opts = {}) => demo.page.getByRole(r, { name, ...opts }).first();

export async function m_floorplan(demo) {
  const p = demo.page;
  const plan = p.getByRole("group", { name: /Floor plan/ });
  await plan.waitFor();
  demo.caption("plan");
  await demo.wait(1.0);
  await demo.click(role(demo, "button", "Zoom in"), { hold: 700 });
  demo.caption("tap");
  await demo.click(plan.getByRole("switch", { name: "Pendants", exact: true }), { hold: 1300 });
  await demo.click(plan.getByRole("switch", { name: "Bedside", exact: true }), { hold: 1300 });
  await demo.click(plan.getByRole("switch", { name: "Garden", exact: true }), { hold: 1600 });
}

export async function m_colour(demo) {
  const p = demo.page;
  await role(demo, "heading", "Living Room").waitFor();
  await demo.wait(0.6);
  demo.caption("colour");
  await demo.click(role(demo, "button", "Show controls for TV backlight"), { hold: 600 });
  await demo.drag(role(demo, "slider", "Hue"), { dx: -150 });
  await demo.drag(role(demo, "slider", "Saturation and brightness"), { dx: -30, dy: 25 });
  await demo.wait(0.6);
  demo.caption("effects");
  await demo.click(role(demo, "combobox", "Effect"), { hold: 700 });
  await demo.click(p.getByRole("option", { name: /Rainbow/i }).first(), { hold: 2800 });
  await demo.click(role(demo, "combobox", "Effect"), { hold: 400 });
  await demo.click(p.getByRole("option", { name: /No effect/i }).first(), { hold: 600 });
}

export async function m_ha(demo) {
  const p = demo.page;
  await demo.click(p.getByText(/Lumen 0798/i).first());
  await p.getByRole("heading", { name: "Controls" }).waitFor();
  demo.caption("device");
  await demo.wait(1.6);
  await demo.scroll(520, { duration: 800, hold: 400 });
  demo.caption("toggle");
  await demo.click(p.getByRole("switch", { name: "Toggle Lumen 0798 Pendants" }), { hold: 1400 });
  await demo.click(p.getByRole("switch", { name: "Toggle Lumen 0798 Bedside" }), { hold: 1400 });
  await demo.click(p.getByRole("switch", { name: "Toggle Lumen 0798 Pendants" }), { hold: 1600 });
}

export async function m_multi(demo) {
  const p = demo.page;
  await demo.click(role(demo, "tab", "Controllers"), { hold: 900 });
  demo.caption("peer");
  await demo.click(p.getByRole("switch", { name: /^Desk lamp/ }).first(), { hold: 1800 });
  demo.caption("other board");
  await demo.goto(DEV() + "/rooms/studio", { wait: 1200 });
  await demo.click(role(demo, "button", /^Desk lamp: .*Toggle/), { hold: 2000 });
}

export async function m_auto(demo) {
  const p = demo.page;
  await role(demo, "heading", "New automation").waitFor();
  demo.caption("form");
  await demo.type(role(demo, "textbox", "Name"), "Porch button", { delay: 55 });
  await demo.scroll(330, { duration: 700, hold: 300 });
  demo.caption("trigger");
  await demo.click(role(demo, "combobox", "Trigger type"), { hold: 600 });
  await demo.click(p.getByRole("option", { name: /Button pressed/i }).first());
  await demo.click(role(demo, "combobox", "Input"), { hold: 500 });
  await demo.click(p.getByRole("option", { name: /^DI1$/ }).first());
  await demo.scroll(360, { duration: 700, hold: 300 });
  demo.caption("action");
  await demo.click(role(demo, "combobox", "Action type"), { hold: 600 });
  await demo.click(p.getByRole("option", { name: /Toggle light/i }).first());
  await demo.click(role(demo, "combobox", "Light"), { hold: 500 });
  await demo.click(p.getByRole("option", { name: "Porch" }).first(), { hold: 1500 });
}

export async function m_overview(demo) {
  const p = demo.page;
  await p.getByText(/lights on/).first().waitFor();
  demo.caption("home");
  await demo.wait(1.2);
  demo.caption("all off");
  await demo.click(role(demo, "button", "All off"), { hold: 1800 });
  await demo.scroll(700, { duration: 800, hold: 300 });
  demo.caption("scene");
  await demo.click(role(demo, "button", /Evening/), { hold: 3000 });
}
