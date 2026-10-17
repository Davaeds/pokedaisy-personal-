// The r/PokeDaisy banners (docs/reddit-banners/): the daisy logo and wordmark on the app's blue over a
// pixel meadow (and a Ko-fi page banner, 1200x400), built from the website's own sprites (website/src/lib/pixel.js) and its Pixel Operator
// font. No creatures and no game art: the two handhelds are drawn here with plain screens. Writes the
// PNGs with headless Chrome: Reddit's at 2x (it asks for at least 1072x128 desktop / 1080x128 mobile),
// Ko-fi's at exactly 1200x400 (3:1, so nothing is cropped).
//
//     node scripts/gen_reddit_banners.mjs
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { SPRITES, spriteSvg } from '../website/src/lib/pixel.js';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const outDir = join(root, 'docs/reddit-banners');
const CHROME = process.env.CHROME ?? [
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  '/usr/bin/google-chrome',
  '/usr/bin/chromium',
].find(existsSync);
if (!CHROME) throw new Error('no Chrome found - set CHROME=/path/to/chrome');

// --- Simple pixel handhelds (plain screens, no game art), drawn on a grid and registered as sprites ---------------
function grid(w, h) {
  const g = Array.from({ length: h }, () => Array(w).fill('.'));
  const api = {
    set: (x, y, c) => { if (g[y]?.[x] !== undefined) g[y][x] = c; },
    fill: (x0, y0, x1, y1, c) => { for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) api.set(x, y, c); },
    body: (x0, y0, x1, y1, r = 2) => {
      api.fill(x0, y0, x1, y1, 'K');
      api.fill(x0 + 1, y0 + 1, x1 - 1, y1 - 1, 'D');
      for (let i = 0; i < r; i++) for (let j = 0; j < r - i; j++) {
        for (const [cx, cy] of [[x0 + i, y0 + j], [x1 - i, y0 + j], [x0 + i, y1 - j], [x1 - i, y1 - j]]) api.set(cx, cy, '.');
      }
      api.fill(x0 + r + 1, y0 + 1, x1 - r - 1, y0 + 1, 'L');
    },
    disc: (cx, cy, r, c) => { for (let y = -r; y <= r; y++) for (let x = -r; x <= r; x++) if (x * x + y * y <= r * r + r / 2) api.set(cx + x, cy + y, c); },
    stick: (cx, cy, r = 4) => { api.disc(cx, cy, r, 'C'); api.disc(cx, cy, r - 1, 'K'); api.disc(cx, cy, r - 2, 'M'); api.set(cx - 1, cy - 1, 'H'); },
    dpad: (cx, cy, a = 3) => { api.fill(cx - 1, cy - a, cx + 1, cy + a, 'H'); api.fill(cx - a, cy - 1, cx + a, cy + 1, 'H'); api.set(cx, cy, 'M'); },
    abxy: (cx, cy, d = 3, sz = 2) => { for (const [dx, dy] of [[0, -d], [-d, 0], [d, 0], [0, d]]) api.fill(cx + dx - sz + 1, cy + dy - sz + 1, cx + dx, cy + dy, 'G'); },
    screen: (x0, y0, x1, y1, c = 'S', glint = true) => {
      api.fill(x0 - 1, y0 - 1, x1 + 1, y1 + 1, 'B');
      api.fill(x0, y0, x1, y1, c);
      if (glint) { api.fill(x0 + 1, y0 + 1, x0 + 4, y0 + 1, 'W'); api.fill(x0 + 1, y0 + 2, x0 + 2, y0 + 2, 'W'); }
    },
    rows: () => g.map((r) => r.join('')),
  };
  return api;
}
const DEVICE_PAL = { K: '#14141c', D: '#24242f', L: '#3c3c4c', B: '#0b0b12', S: '#58a0f0', W: '#d8f0ff', C: '#38c8ff', M: '#33333f', H: '#5a5a70', G: '#a8a8b8', T: '#d6ebe7', U: '#c4e0dc' };
{ // dual screen: two bodies and a hinge, smaller buttons, a proper 4:3 lower screen
  const g = grid(44, 52);
  g.body(0, 0, 43, 22, 2);
  g.screen(6, 4, 37, 19);
  g.fill(2, 23, 41, 26, 'K'); g.fill(3, 24, 40, 25, 'L'); g.fill(4, 24, 39, 24, 'H');
  g.body(0, 27, 43, 51, 2);
  g.screen(12, 31, 31, 45, 'T', false);
  for (let y = 32; y <= 45; y += 2) g.fill(12, y, 31, y, 'U');
  g.fill(14, 48, 18, 49, 'L'); g.fill(25, 48, 29, 49, 'L');
  g.stick(6, 34, 3); g.dpad(6, 43, 2); g.abxy(37, 34, 2, 1); g.stick(37, 43, 3);
  SPRITES.bdual = { pal: DEVICE_PAL, rows: g.rows() };
}
{ // single screen: one wide screen between the sticks
  const g = grid(64, 30);
  g.body(0, 0, 63, 29, 3);
  g.screen(18, 5, 45, 24);
  g.stick(8, 9); g.dpad(8, 21); g.abxy(55, 9); g.stick(55, 21);
  SPRITES.bsingle = { pal: DEVICE_PAL, rows: g.rows() };
}

const FONT = pathToFileURL(join(root, 'website/public/fonts/PixelOperator.ttf')).href;

/** A sprite placed at (x, bottom) at a whole scale. */
function sprite(name, x, bottom, scale, flip = false) {
  const { w, h, svg } = spriteSvg(name);
  const shadow = name.startsWith('b') && SPRITES[name].pal === DEVICE_PAL ? 'filter:drop-shadow(3px 3px 0 rgba(5,30,100,.35));' : '';
  return `<span class="sp" style="left:${x}px;bottom:${bottom}px;width:${w * scale}px;height:${h * scale}px;${shadow}${flip ? 'transform:scaleX(-1);' : ''}">${svg}</span>`;
}

// A pixel grass strip, [unit] CSS px per pixel, a fixed pseudo-random tuft pattern.
function grass(width, unit, rows) {
  let seed = 7;
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff;
  const cols = Math.ceil(width / unit);
  let r = '';
  for (let x = 0; x < cols; x++) {
    const tuft = rnd() < 0.45 ? 1 : 0;
    if (tuft) r += `<rect x="${x}" y="0" width="1" height="1" fill="#78d068"/>`;
    for (let y = 1; y < rows; y++) {
      const c = y === 1 ? '#78d068' : y < rows - 1 ? (rnd() < 0.12 ? '#78d068' : '#58a838') : '#3c8038';
      r += `<rect x="${x}" y="${y}" width="1" height="1" fill="${c}"/>`;
    }
  }
  return `<svg class="grass" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${cols} ${rows}" width="${cols * unit}" height="${rows * unit}" shape-rendering="crispEdges">${r}</svg>`;
}

function page({ w, h, logoScale, font, scale, flank, kicker, grassRows, flowerX, sky }) {
  const unit = scale;
  const gh = grassRows * unit;
  const wordmark = 'POKEDAISY'.split('').map((c, i) => `<span class="${i < 4 ? 'red' : 'ink'}">${c}</span>`).join('');
  const sprites = flank.map(([n, x, s = scale, f = false, lift = 0]) => sprite(n, x, gh - unit * 2 + lift, s, f)).join('');
  const flowers = flowerX.map((fx, i) => sprite('daisy', fx, unit * (i % 2 ? 1 : 2), unit)).join('');
  const clouds = sky.map(([x, top]) => sprite('cloud', x, h - top - 8 * unit, unit)).join('');
  const { w: lw, h: lh, svg } = spriteSvg('logo');
  return `<!doctype html><meta charset="utf-8"><style>
@font-face { font-family: PX; src: url('${FONT}'); }
* { margin: 0; box-sizing: border-box; }
html, body { width: ${w}px; height: ${h}px; overflow: hidden; }
body { position: relative; font-family: PX; background:
  radial-gradient(circle at 50% 0%, rgba(255,255,255,.20), transparent 55%),
  radial-gradient(circle at 8% 100%, rgba(7,40,120,.25), transparent 45%),
  radial-gradient(circle at 92% 100%, rgba(7,40,120,.25), transparent 45%),
  linear-gradient(180deg, #0E65F1 0%, #2f84fa 100%); }
body::before { content: ""; position: absolute; inset: 0; background-image: radial-gradient(rgba(255,255,255,.16) 1px, transparent 1.4px); background-size: ${unit * 6}px ${unit * 6}px; }
.grass { position: absolute; left: 0; bottom: 0; }
.sp { position: absolute; line-height: 0; }
.sp svg { width: 100%; height: 100%; display: block; }
.center { position: absolute; left: 0; right: 0; top: 0; bottom: ${gh - unit * 2}px; display: flex; align-items: center; justify-content: center; gap: ${unit * 5}px; }
.logo { width: ${lw * logoScale}px; height: ${lh * logoScale}px; line-height: 0; }
.logo svg { width: 100%; height: 100%; display: block; }
.text { display: flex; flex-direction: column; align-items: flex-start; gap: ${unit * 2}px; }
.word { font-size: ${font}px; line-height: 1; letter-spacing: ${font / 16}px; white-space: nowrap;
  text-shadow: ${font / 16}px 0 0 currentColor, ${font / 16}px ${font / 16}px 0 rgba(5,30,100,.35), ${font / 16 * 2}px ${font / 16}px 0 rgba(5,30,100,.35); }
.red { color: #ffffff; } .ink { color: #f8d030; }
.kicker { font-size: ${kicker}px; line-height: 1; letter-spacing: ${kicker / 16}px; color: #ffffff; white-space: nowrap; text-shadow: ${kicker / 16}px ${kicker / 16}px 0 rgba(5,30,100,.45); }
</style><body>
${grass(w, unit, grassRows)}
${clouds}
${flowers}
${sprites}
<div class="center"><span class="logo">${svg}</span><div class="text"><div class="word">${wordmark}</div>${kicker ? '<div class="kicker">FREE &amp; OPEN SOURCE POKÉMON COMPANION</div>' : ''}</div></div>
</body>`;
}

// Desktop: 1072 x 128, scenery flanking the wordmark.
const BANNERS = {
  'banner-desktop': [2, page({
  w: 1072, h: 128, logoScale: 3, font: 64, scale: 3, kicker: 16, grassRows: 7, flowerX: [340, 672], sky: [[205, 16], [756, 10], [20, 44]],
  flank: [
    ['tree', 28], ['bdual', 86, 2], ['fence', 200], ['fence', 224], ['fence', 248], ['tallgrass', 292],
    ['tree', 1072 - 28 - 48, 3, true], ['bsingle', 852, 2], ['fence', 1072 - 250 - 24], ['fence', 1072 - 274 - 24], ['fence', 1072 - 298 - 24], ['tallgrass', 1072 - 340 - 24],
  ],
})],
  // Mobile: 1080 x 128, the wordmark bigger (the app shows it about a third of that width).
  'banner-mobile': [2, page({
  w: 1080, h: 128, logoScale: 3, font: 96, scale: 3, kicker: 0, grassRows: 7, flowerX: [270, 730], sky: [[210, 16]],
  flank: [
    ['tree', 40], ['bdual', 102, 2], ['tallgrass', 224],
    ['tree', 1080 - 40 - 48, 3, true], ['bsingle', 842, 2], ['tallgrass', 770],
  ],
})],
  // Ko-fi: 1200 x 400 at 1x, so the pixel art is drawn at whole 3 / 4 / 5 px steps.
  'kofi-banner': [1, page({
    w: 1200, h: 400, logoScale: 5, font: 112, scale: 4, kicker: 32, grassRows: 9, flowerX: [300, 880, 560, 20],
    sky: [[40, 56], [560, 14], [940, 36], [232, 150]],
    flank: [
      ['bdual', 30, 4], ['coffee', 222, 4], ['coffee', 906, 4, true],
      ['bsingle', 968, 3],
    ],
  })],
};

// Each page is rendered at 2x by headless Chrome (a throwaway profile, so an open Chrome doesn't take the job).
const tmp = mkdtempSync(join(tmpdir(), 'pokedaisy-banners-'));
try {
  for (const [name, [dsf, html]] of Object.entries(BANNERS)) {
    const [, w, h] = /html, body \{ width: (\d+)px; height: (\d+)px/.exec(html);
    const file = join(tmp, `${name}.html`);
    const png = join(outDir, `${name}.png`);
    writeFileSync(file, html);
    rmSync(png, { force: true });
    const chrome = spawn(CHROME, [
      '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', `--user-data-dir=${join(tmp, 'profile')}`,
      `--force-device-scale-factor=${dsf}`, `--window-size=${w},${h}`, `--screenshot=${png}`, pathToFileURL(file).href,
    ], { stdio: 'ignore' });
    const started = Date.now();
    while (!existsSync(png)) {
      if (Date.now() - started > 60000) { chrome.kill(); throw new Error('Chrome timed out'); }
      await new Promise((r) => setTimeout(r, 250));
    }
    await new Promise((r) => setTimeout(r, 500));
    chrome.kill();
    console.log(`wrote ${png}`);
  }
} finally {
  try {
    rmSync(tmp, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
  } catch {
    // Chrome may still be closing; the OS clears its temp dir.
  }
}
