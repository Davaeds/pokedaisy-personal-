#!/usr/bin/env python3
"""Pictures of EXPAND's battles, every battle scene of every game WIDE knows, to look at the strips past the GBA's
240 px (players report broken side bars in battles) - headless, no device.

    scripts/wide_battle_shots.py [--only NAME]... [--all] [--margin N] [--every N] [--jobs N]

Needs build/wide/wide.json (scripts/gen_wide_tables.py <roms dirs>), the pokedaisy-capture image, $DECOMPS (the
retail references below are found in the ROMs through the decomps' builds) and Pillow.

Each game boots its save in native-capture/mgba_dump (built here with the app's pk_wide.c) at MARGIN GBA px a side
(22: a 16:9 screen), saves a state, and for every scene loads it back, patches the ROM so the battle draws that
scene, starts a scripted wild battle (verify_wide.py's) and shoots a wide frame every N frames through it: the
transition, the intro slide, the text, the action menu, FIGHT, a move and its animation, the foe's turn, RUN and
the fade back. Each shot carries how WIDE drew its strips (`wideinfo`'s battle ways, pk_wide.h's
PK_WIDE_BATTLE_*): `=` BG3 as VRAM has it, `R` mirrored because BG3 repeats one 256 px picture, `O` because
BG1 / BG2 draw scenery at the edge, `<` / `>` because BG3 doesn't go on past the left / right edge.

Scenes are forced by making the game's own scene functions return a fixed value (`rompatch`: movs r0, #N; bx lr):

  FireRed family   GetBattleTerrainOverride (all 20 of battle_bg.c's scenes: the 10 terrains, link, gym, leader,
                   the indoor ones, the Elite Four, champion) and BattleSetup_GetTerrainId (the intro's slide: the
                   terrain for 0-9, BUILDING past them);
  Emerald, R / S   BattleSetup_GetEnvironmentId (the 10 terrains) and GetCurrentMapBattleScene (the map's battle
                   scene: gym, Magma, Aqua, the Elite Four, Frontier / Battle Tower), each with the other at its
                   plain value (NORMAL, BUILDING).

Functions are found by their code in each ROM: the decomp build's bytes, else the same with BL targets and literal
loads blanked, anywhere in the ROM; CFRU's jump at FireRed rev 0's entry; and in an Emerald-engine build none of
that matches (the gcc-built expansion hacks) by what the functions do (find_by_role). A game where they still aren't
found gets only its own scene, "AS_SAVED". Hacks that grew the scene tables past vanilla's show only vanilla's here.

Writes build/wide/battles/<game>/<scene>.png (every shot of the scene in order), <game>.png (each scene's key
frames: every change of the strips' ways, plus some spread over the battle) and index.html.
"""
import argparse
import concurrent.futures
import html
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(ROOT, "build", "wide")
SHOTS = os.path.join(OUT, "battles")
IMAGE = "pokedaisy-capture"
B = 0x08000000

sys.path.insert(0, HERE)
from verify_ports import OWN_SAVES, SAVES, conf  # noqa: E402
from verify_wide import BATTLE, SCRIPT_RAM, boot_lines  # noqa: E402

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("needs Pillow: python3 -m pip install pillow")

SAVES = {**SAVES, "BPE": "emerald_vanilla"}

# The decomp builds' functions (nm on their ELFs), found in each ROM by the build's code bytes.
FR_FUNCS = ("pokefirered/pokefirered_rev1.gba", {"override": 0x0800FD20, "terrain": 0x080801FC})
EM_FUNCS = ("pokeemerald/pokeemerald.gba", {"terrain": 0x080B0BD0, "scene": 0x08085C80})
RS_FUNCS = ("pokeruby/pokeruby_rev1.gba", {"terrain": 0x08081D5C, "scene": 0x080542C8})
REF = {"FR": FR_FUNCS, "FR_EX": FR_FUNCS, "EM": EM_FUNCS, "EX": EM_FUNCS, "RS": RS_FUNCS}
# The same two in FireRed rev 0 (found in its hacks by the above): CFRU hacks (Unbound, Radical Red, ...) start each
# with a jump into their own code there (ldr r0, [pc]; bx r0; .word target) and keep the rest of rev 0's code (calls
# aside) - the patch goes over that jump.
FR0 = {"override": 0x0800FC2C, "terrain": 0x0807FC5C}

TERRAINS = ["GRASS", "LONG_GRASS", "SAND", "UNDERWATER", "WATER", "POND", "MOUNTAIN", "CAVE", "BUILDING", "PLAIN"]
FR_SCENES = TERRAINS + ["LINK", "GYM", "LEADER", "INDOOR_2", "INDOOR_1", "LORELEI", "BRUNO", "AGATHA", "LANCE",
                        "CHAMPION"]
EM_SCENES = ["GYM", "MAGMA", "AQUA", "SIDNEY", "PHOEBE", "GLACIA", "DRAKE", "FRONTIER"]
RS_SCENES = EM_SCENES[:-1] + ["BATTLE_TOWER"]
BUILDING = 8

# The default run: one build of each game (the languages share the English one's art; a hack's builds share theirs).
DEFAULT = {
    "84ee4776", "daffecec", "1f1c08fb", "61641576", "bafedae5",   # FireRed / LeafGreen rev 1, Emerald, Ruby / Sapphire rev 1
    "a969986d", "10ef71a7", "458c9fad", "1d230210", "9c31cc4b", "66c1cce7", "9dd7c03b", "d14f12a3", "14465a33",
    "f2d16ebd", "3da4e335", "b9465c5a", "737a7a12", "e4ceffe2", "01713508", "558ae42f", "6c5ee1b2", "de228cd3",
    "78f1693e", "fba55dd8", "def0f69e", "283238a6", "d854eb09", "d25fbccc", "ee742896", "4b3d4957", "6b1d4a0f",
    "cc412549",
}

WAYS = [(2, "R"), (4, "O"), (8, "<"), (16, ">")]


def masked(d, off, n):
    """Thumb code with BL targets and literal loads' offsets blanked (port_retail.masked)."""
    out = bytearray(d[off:off + n])
    i = 0
    while i + 3 < n:
        if (out[i + 1] & 0xF8) == 0xF0 and (out[i + 3] & 0xF8) == 0xF8:
            out[i:i + 4] = b"\0\0\0\0"
            i += 4
            continue
        if (out[i + 1] & 0xF8) == 0x48:
            out[i] = 0
        i += 2
    return bytes(out)


def find_function(ref, rom, a, n=40):
    """Where the build's function at a is in rom: its exact bytes (unique), else its masked bytes - the best match
    anywhere, at most 2 bytes off and no tie. None if neither."""
    off = (a & ~1) - B
    for k in (32, 24, 16):
        w = ref[off:off + k]
        i = rom.find(w)
        if i >= 0 and rom.find(w, i + 1) < 0:
            return B + i
    want = masked(ref, off, n)
    # The longest run the masking left alone anchors the search.
    runs, start = [], 0
    for i in range(0, n + 2, 2):
        if i >= n or want[i] == 0:
            if i - start >= 4:
                runs.append((i - start, start))
            start = i + 2
    if not runs:
        return None
    ln, at = max(runs)
    anchor = want[at:at + ln]
    scores = {}
    i = rom.find(anchor)
    while i >= 0:
        k = i - at
        if k >= 0 and k % 2 == 0:
            scores[k] = sum(1 for x, y in zip(want, masked(rom, k, n)) if x == y)
        i = rom.find(anchor, i + 1)
    if not scores:
        return None
    best = max(scores.values())
    hits = [k for k, s in scores.items() if s == best]
    return B + hits[0] if best >= n - 2 and len(hits) == 1 else None


def u16(rom, a):
    return rom[a - B] | rom[a - B + 1] << 8


def u32(rom, a):
    return int.from_bytes(rom[a - B:a - B + 4], "little")


def bl_targets(rom):
    """Every BL in the ROM: target -> the BLs' addresses."""
    out = {}
    for i in range(0, len(rom) - 4, 2):
        if (rom[i + 1] & 0xF8) == 0xF0 and (rom[i + 3] & 0xF8) == 0xF8:
            off = ((rom[i] | (rom[i + 1] & 7) << 8) << 12) | ((rom[i + 2] | (rom[i + 3] & 7) << 8) << 1)
            if off & 0x400000:
                off -= 0x800000
            out.setdefault(B + i + 4 + off, []).append(B + i)
    return out


def literal_loads(rom, value):
    """`ldr rd, [pc, #imm]` loading the word value: (address, rd)."""
    out, w = [], value.to_bytes(4, "little")
    i = rom.find(w)
    while i >= 0:
        if i % 4 == 0:
            for k in range(2, 1024, 2):
                a = B + i - k
                h = u16(rom, a) if a >= B else 0
                if (h & 0xF800) == 0x4800 and ((a + 4) & ~3) + (h & 0xFF) * 4 == B + i:
                    out.append((a, (h >> 8) & 7))
        i = rom.find(w, i + 1)
    return out


def find_by_role(e, rom):
    """Emerald-engine builds the decomps' code doesn't match (gcc-built expansion hacks): the scene and terrain
    functions found by what they do. GetCurrentMapBattleScene loads gSaveBlock1Ptr, finds the save's map header
    (through gMapGroups, often inlined) and returns its battleType, `ldrb r0, [rX, #0x1B]` then a return - a BL
    target a few instructions before that load. BattleSetup_GetEnvironmentId is the one call whose result goes
    straight into gBattleEnvironment, a variable the scene function's callers (the background loaders) read."""
    sb1 = int(e["addrs"]["SAVE_BLOCK1_PTR"], 16)
    targets = bl_targets(rom)
    scene = set()
    for load, _ in literal_loads(rom, sb1):
        for a in range(load + 2, load + 40, 2):
            h = u16(rom, a)
            if (h & 0xFFC7) == 0x7EC0 and (u16(rom, a + 2) == 0x4770 or (u16(rom, a + 2) >> 8) == 0xBC):
                scene |= {s for s in range(load - 16, load + 2, 2) if s in targets}
    if len(scene) != 1:
        return None
    scene = scene.pop()
    # The RAM the callers (the background loaders) read near their calls: gBattleEnvironment is among it. First what
    # every caller reads close by, then what most read a little further (one of Seaglass's loaders reads it 0x224 on).
    callers = targets[scene]
    for after, need in ((0x200, len(callers)), (0x300, len(callers) // 2 + 1)):
        seen = {}
        for c in callers:
            near = set()
            for a in range(c - 0x100, c + after, 2):
                h = u16(rom, a)
                if (h & 0xF800) == 0x4800:
                    v = u32(rom, ((a + 4) & ~3) + (h & 0xFF) * 4)
                    if 0x02000000 <= v < 0x02040000:
                        near.add(v)
            for v in near:
                seen[v] = seen.get(v, 0) + 1
        terrain = unique_store(rom, {v for v, n in seen.items() if n >= need})
        if terrain:
            return {"terrain": terrain, "scene": scene}
    return None


def unique_store(rom, ram):
    """The one function whose result is stored straight into one of the variables ram (`bl f; ldr rX, =var;
    strb r0, [rX]`, the only call stored there) - None unless exactly one such."""
    stores = set()
    for v in ram:
        for load, rd in literal_loads(rom, v):
            for a in range(load + 2, load + 18, 2):
                h = u16(rom, a)
                if (h & 0xFFC0) == 0x7000 and ((h >> 3) & 7) == rd and (h & 7) == 0:   # strb r0, [rd]
                    for b in range(a - 20, a, 2):
                        h1, h2 = u16(rom, b), u16(rom, b + 2)
                        if (h1 & 0xF800) == 0xF000 and (h2 & 0xF800) == 0xF800:
                            off = ((h1 & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
                            stores.add((v, b + 4 + (off - 0x800000 if off & 0x400000 else off)))
    # The variable written by exactly one call's result.
    by_var = {}
    for v, t in stores:
        by_var.setdefault(v, set()).add(t)
    terrain = {next(iter(t)) for v, t in by_var.items() if len(t) == 1}
    return terrain.pop() if len(terrain) == 1 else None


def patch(rom, addr, value):
    """`rompatch` making the function at addr return value: movs r0, #value; bx lr over its first 4 bytes."""
    return f"rompatch {addr:x} {rom[addr - B:addr - B + 4].hex()} {addr:x} {value:02x}207047"


def scenes_for(e, rom):
    """[(name, [rompatch lines])] for the game, the save's own scene first; and a note on what was found."""
    out = [("AS_SAVED", [])]
    fam = e["family"]
    path, funcs = REF[fam]
    ref_path = os.path.join(os.environ.get("DECOMPS", ""), path)
    if not os.path.isfile(ref_path):
        return out, f"no {path} under $DECOMPS"
    ref = open(ref_path, "rb").read()
    found = {k: find_function(ref, rom, a) for k, a in funcs.items()}
    if fam == "FR":
        for k, a in FR0.items():
            if found[k] is None and rom[a - B:a - B + 4] == bytes.fromhex("00480047") \
                    and masked(rom, a - B + 8, 16) == masked(ref, FR_FUNCS[1][k] - B + 8, 16):
                found[k] = a
    missing = [k for k, v in found.items() if v is None]
    if missing and fam in ("EM", "EX"):
        found = find_by_role(e, rom) or found
        missing = [k for k, v in found.items() if v is None]
    if missing:
        return out, "scene functions not found (" + ", ".join(missing) + "): its own scene only"
    if fam in ("FR", "FR_EX"):
        for i, name in enumerate(FR_SCENES):
            out.append((name, [patch(rom, found["override"], i),
                               patch(rom, found["terrain"], i if i < len(TERRAINS) else BUILDING)]))
    else:
        for i, name in enumerate(TERRAINS):
            out.append((name, [patch(rom, found["terrain"], i), patch(rom, found["scene"], 0)]))
        for i, name in enumerate(RS_SCENES if fam == "RS" else EM_SCENES):
            out.append((name, [patch(rom, found["terrain"], BUILDING), patch(rom, found["scene"], i + 1)]))
    return out, " ".join(f"{k} {v:#x}" for k, v in found.items())


def battle(e, every):
    """After the scripted call: waits and presses, a wideshot (+ its wideinfo) every N frames throughout; and the
    shots taken just before each press (the moments the sheets show). The keys are verify_wide.py's RUN, with FIGHT
    and the first move before it."""
    slow = e["code"][:3] in ("BPE", "AXV", "AXP")   # Emerald and Ruby / Sapphire print their battle text slower
    steps = [(520 if slow else 420, "A"),            # the foe's "appeared" text: on to the action menu
             (480 if slow else 330, "A"),            # FIGHT
             (60, "A"),                              # the first move
             (900, "B"), (30, "B"),                  # its animation, the foe's turn; back out of any menu
             (90, "RIGHT"), (12, "DOWN"), (12, "A"),  # RUN
             (90, "A"), (300, None)]                 # "Got away safely!" and the fade back
    lines, shot, moments = [], 0, []
    for wait, key in steps:
        while wait > 0:
            n = min(every, wait)
            lines += [f"wait {n}", f"wideshot {{scene}}_{shot:04d}.ppm", "wideinfo"]
            shot += 1
            wait -= n
        moments.append(shot - 1)
        if key:
            lines.append(f"press {key}")
    return lines, moments


def commands(e, scenes, margin, every):
    out = [f"wideon {margin}"] + boot_lines(e["code"], e.get("boot", []), e.get("boot_file")) + ["wait 60", "savestate base.ss"]
    script_ram = int(e.get("script_ram") or SCRIPT_RAM)
    for name, patches in scenes:
        out += ["loadstate base.ss", "romunpatch"] + patches
        out += [f"poke8 {script_ram + i:#x} {b:#x}" for i, b in enumerate(BATTLE)]
        out.append(f"call {e['script']} {script_ram:#x}")
        out += [l.replace("{scene}", name) for l in battle(e, every)[0]]
    return out


def ways_label(w):
    if not w & 1:
        return ""
    return "".join(c for bit, c in WAYS if w & bit) or "="


def mark(im, margin, label, scale):
    """Ticks at the GBA's own 240 px edges (magenta), and the strips' ways."""
    d = ImageDraw.Draw(im)
    for x in (margin, margin + 240):
        d.line([(x * scale, 0), (x * scale, 4 * scale)], fill=(255, 0, 255), width=scale)
        d.line([(x * scale, im.height - 4 * scale), (x * scale, im.height)], fill=(255, 0, 255), width=scale)
    if label:
        d.rectangle([0, 0, 6 + 6 * len(label), 11], fill=(0, 0, 0))
        d.text((3, 0), label, fill=(255, 255, 0))
    return im


def film(frames, path, every, margin, cols=6):
    """Every shot of a scene in order, 1x, with its frame (after the battle's start) and ways."""
    w, h = frames[0][0].size
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + 2), rows * (h + 2)), (40, 40, 40))
    for i, (im, ways) in enumerate(frames):
        im = mark(im.copy(), margin, f"{(i + 1) * every} {ways_label(ways)}".strip(), 1)
        sheet.paste(im, ((i % cols) * (w + 2), (i // cols) * (h + 2)))
    sheet.save(path)


def key_frames(frames, moments):
    """The intro's slide (the first shots with strips), the shot before each press (the text, the action menu,
    the move menu, the move's animation, ...) and any other change of the strips' ways - in order."""
    drawn = [i for i, (_, ways) in enumerate(frames) if ways & 1]
    keep = set(drawn[:12:3]) | {m for m in moments if m < len(frames)}
    last = None
    for i in drawn:
        if frames[i][1] != last:
            keep.add(i)
            last = frames[i][1]
    return [frames[i] for i in sorted(keep)][:12]


def game_sheet(name, rows, path, margin, scale=2):
    w, h = rows[0][1][0][0].size
    cols = max(len(r) for _, r in rows)
    lw = 110
    sheet = Image.new("RGB", (lw + cols * (w * scale + 4), len(rows) * (h * scale + 4) + 20), (30, 30, 30))
    d = ImageDraw.Draw(sheet)
    d.text((4, 4), name, fill=(255, 255, 255))
    for r, (scene, frames) in enumerate(rows):
        y = 20 + r * (h * scale + 4)
        d.text((4, y + 4), scene, fill=(255, 255, 255))
        for c, (im, ways) in enumerate(frames):
            im = mark(im.resize((w * scale, h * scale), Image.NEAREST), margin, ways_label(ways), scale)
            sheet.paste(im, (lw + c * (w * scale + 4), y))
    sheet.save(path)


def run_game(e, tool, margin, every):
    name = e.get("name") or os.path.splitext(e["file"])[0]
    slug = re.sub(r"[^A-Za-z0-9.]+", "_", name).strip("_")
    save = conf(e.get("save") or OWN_SAVES.get(e["code"], SAVES.get(e["code"][:3], "")))
    if not save or not os.path.exists(save):
        return name, slug, [], "no save"
    rom = open(e["path"], "rb").read()
    scenes, note = scenes_for(e, rom)
    work = os.path.join(OUT, "battle-runs", e["crc"])
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shutil.copy(e["path"], os.path.join(work, "rom.gba"))
    shutil.copy(save, os.path.join(work, "save.sav"))
    shutil.copy(tool, os.path.join(work, "mgba_dump"))
    open(os.path.join(work, "cmds.txt"), "w").write("\n".join(commands(e, scenes, margin, every)) + "\n")
    r = subprocess.run(["docker", "run", "--rm", "-v", f"{work}:/work", "-w", "/work", IMAGE, "-c",
                        "./mgba_dump rom.gba save.sav < cmds.txt"], capture_output=True, text=True)
    open(os.path.join(work, "out.txt"), "w").write(r.stdout + r.stderr)
    if re.search(r"^rompatch \w+: code didn't match", r.stdout, re.M):
        note += " (a patch didn't match)"
    if re.search(r"^loadstate: .* FAILED", r.stdout, re.M):
        note += " (the state didn't load)"
    # Every wideshot is followed by its wideinfo, in the same order as the shots' files.
    infos = iter(int(m.group(1)) for m in re.finditer(r"^wideinfo: .* battle (\d+)$", r.stdout, re.M))
    out_dir = os.path.join(SHOTS, slug)
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir)
    rows, seen, moments = [], [], battle(e, every)[1]
    for scene, _ in scenes:
        files = sorted(f for f in os.listdir(work) if f.startswith(scene + "_") and f.endswith(".ppm"))
        frames = [(Image.open(os.path.join(work, f)).convert("RGB"), next(infos, 0)) for f in files]
        if frames:
            film(frames, os.path.join(out_dir, f"{scene}.png"), every, margin)
            rows.append((scene, key_frames(frames, moments)))
            seen.append((scene, "".join(sorted({c for _, w in frames if w & 1 for c in ways_label(w)}))))
    if rows:
        game_sheet(name, rows, os.path.join(SHOTS, f"{slug}.png"), margin)
        shutil.rmtree(work, ignore_errors=True)   # kept (with out.txt) when nothing came out
    return name, slug, seen, note


def index(results):
    """index.html over this run's games and the earlier runs' still on disk (results.json keeps them)."""
    path = os.path.join(SHOTS, "results.json")
    known = {r[1]: r for r in json.load(open(path))} if os.path.exists(path) else {}
    known.update({r[1]: list(r) for r in results})
    json.dump(sorted(known.values()), open(path, "w"), indent=1)
    rows = []
    for name, slug, scenes, note in sorted(known.values()):
        links = " ".join(f'<a href="{slug}/{html.escape(s)}.png">{html.escape(s)}</a> <code>{html.escape(w)}</code>'
                         for s, w in scenes)
        sheet = f'<a href="{slug}.png"><img src="{slug}.png" loading="lazy"></a>' if scenes else ""
        rows.append(f"<section><h2>{html.escape(name)}</h2><p>{html.escape(note)}</p><p>{links}</p>{sheet}</section>")
    open(os.path.join(SHOTS, "index.html"), "w").write(
        "<!doctype html><meta charset=utf-8><title>WIDE battles</title><style>body{background:#222;color:#ddd;"
        "font:14px sans-serif}img{max-width:100%;image-rendering:pixelated}a{color:#9cf}code{color:#fd6}</style>"
        "<p>Ways: = BG3 as VRAM has it, R a repeated picture mirrored in, O BG1 / BG2 at the edge mirrored in, "
        "&lt; &gt; BG3 broken past that edge, mirrored in (beside each scene: every way its battle used). Magenta "
        "ticks: the GBA's own 240 px.</p>" + "".join(rows))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--only", action="append", help="a name or CRC (part of a name is enough); repeat for more")
    ap.add_argument("--all", action="store_true", help="every ROM in wide.json, not one build per game")
    ap.add_argument("--margin", type=int, default=22, help="GBA px a side (22: 16:9; 58: 20:9)")
    ap.add_argument("--every", type=int, default=10, help="frames between shots")
    ap.add_argument("--jobs", type=int, default=3, help="games at once")
    a = ap.parse_args()
    picked = []
    for e in json.load(open(os.path.join(OUT, "wide.json"))):
        name = (e.get("name") or e["file"]).lower()
        if e["missing"] or not e.get("script"):
            continue
        if a.only:
            if not any(o.lower() in name or o.lower() == e["crc"] for o in a.only):
                continue
        elif not a.all and e["crc"] not in DEFAULT:
            continue
        picked.append(e)
    subprocess.run(["docker", "image", "inspect", IMAGE], capture_output=True, check=True)
    tool = os.path.join(OUT, "mgba_dump")
    subprocess.run(["docker", "run", "--rm", "-v", f"{ROOT}:/repo", "-v", f"{OUT}:/out", "-w", "/repo/native-capture", IMAGE,
                    "-c", "gcc -O2 -Wall -DPK_WIDE_PENDING -o /out/mgba_dump mgba_dump.c -I/usr/include "
                          "-L/usr/lib/aarch64-linux-gnu -lmgba -lm"], check=True)
    os.makedirs(SHOTS, exist_ok=True)
    results = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
        for res in pool.map(lambda e: run_game(e, tool, a.margin, a.every), picked):
            ways = sorted({c for _, w in res[2] for c in w})
            print(f"{res[0]}: {len(res[2])} scenes, ways {''.join(ways) or '-'}. {res[3]}", flush=True)
            results.append(res)
    index(results)
    print(f"-> {os.path.relpath(os.path.join(SHOTS, 'index.html'), ROOT)}")


if __name__ == "__main__":
    main()
