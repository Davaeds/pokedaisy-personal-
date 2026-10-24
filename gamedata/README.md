# gamedata

The per-game data every PokeDaisy platform builds from: one place to write a game's
addresses, offsets and layouts, so a new game or a new field reaches every platform built from
the same commit. Nothing reads these files at run time: `scripts/gen_gamedata.py` turns them into
each platform's code, and that code is committed.

So far (phase 1) this holds the Gen 3 `NativeConfig`s, and the only output is the app's
`app/src/main/kotlin/com/pokedaisy/app/companion/data/NativeConfigsGen.kt`. The 3DS port's C comes
next, then the nested records (Pokédex / guide tables, layouts), the games' identity (SHA-1s,
codes, titles) and the name tables.

```
gamedata/
  schema/gen3_config.toml   every NativeConfig field: Kotlin type, schema type, default
  schema/gen3_pokedex.toml  PokedexTables' fields, and gen3_guide.toml GuideTables'
  gen3/retail/*.toml        FireRed, LeafGreen, Emerald, Ruby / Sapphire
  gen3/ports/*.toml         their other-language releases (written by scripts/port_retail.py)
  gen3/hacks/*.toml         one file per hack, every release of it
```

## Commands

```
make gamedata                              # regenerate NativeConfigsGen.kt
python3 scripts/gen_gamedata.py --check    # fails if it's stale (make check and CI run this)
```

Never edit `NativeConfigsGen.kt` by hand: the next `make gamedata` overwrites it and the check fails.

## Adding or changing a game

1. Open the game's family file (or add one in `gen3/hacks/`; the file name is free).
2. Add a `[config.<name>]` table. `<name>` is lower snake case and becomes the Kotlin val
   `NATIVE_<NAME>` (`[config.unbound_with_dex]` is `NATIVE_UNBOUND_WITH_DEX`).
3. Set `base` to the config it starts from, then only the fields that differ. A config with no
   base sets every field the schema has no default for.
4. Write down where each value came from, in a `#` comment above it, as the existing configs do.
   Comments are carried into the generated code.
5. `make gamedata`, then build and test as usual.

```toml
# The battle menus' cursors are FireRed's (CFRU keeps its EWRAM).
[config.unbound]
base = 'firered_rev0'
actionCursor = 0x02023FF8
moveCursor = 0x02023FFC
```

A config with `private = true` is a `private val` in Kotlin: a common base that nothing outside
the generated file uses. `lazy = true` builds it on first use (`by lazy`), and `port = 'BPRD0'` lists
it in `RETAIL_PORTS` under that game code + revision; the language ports use both.

### Records

A few fields hold a record of their own (their schema field names one with `record`, like
`pokedex` and `guideTables`). Besides naming a Kotlin val (`pokedex = 'POKEDEX_SAPPHIRE'`), a
config can spell the record out in a sub-table right after it: `base` names the val it starts
from, the rest are that record's fields (`gamedata/schema/gen3_pokedex.toml`).

```toml
[config.port_axpd0.pokedex]
base = 'POKEDEX_SAPPHIRE'
entries = 0x083BD6D4
probeCategory = "SAMEN"
metric = true
```

Without a `base`, the sub-table must set every field the record's schema has no default for.

## Values

One `key = value` a line (TOML inline tables, arrays and strings stay on that line). Field names
are the Kotlin ones. What a value can be depends on the field's `type` in the schema:

| Type | Written as | Kotlin |
| --- | --- | --- |
| `long`, `int` | `0x02024284`, `-1`, `999_999` (hex stays hex) | `0x02024284L`, `5` |
| `bool`, `char`, `string` | `true`, `'F'`, `'BPED'` | `true`, `'F'`, `"BPED"` |
| `ref` | the Kotlin val's name: `'POKEDEX_UNBOUND'` | `POKEDEX_UNBOUND` |
| `item_descs` | `{ preset = 'vanilla', base = 0x083DB028 }` or `{ base = ..., stride = 0x2C, descOff = -8 }` | `vanillaItems(...)`, `ItemDescTable(...)` |
| `int_range` | `[34, 35]` | `34..35` |

Any nullable field takes `'none'` (Kotlin `null`). Any field also takes:

- `{ from = 'firered_rev1' }`: the same field of another config.
- `{ ref = 'RUBY_SAPPHIRE_ITEMS' }`: a named Kotlin constant.
- `{ kotlin = 'Bag3Layout(...)' }`: a Kotlin expression, for the few records that aren't data
  here yet. Avoid it for anything new: it's what a platform other than the app can't read.

`ref` and `kotlin` values point at Kotlin and go away as their records move into gamedata/.

## The schemas

`schema/gen3_config.toml` lists `NativeConfig`'s fields, `gen3_pokedex.toml` / `gen3_guide.toml`
those of `PokedexTables` / `GuideTables`. Each names its Kotlin class (`kotlin_class`,
`kotlin_file`) and the generator checks the two agree, so a new field means adding it to both
(with a default, or every config without a base has to set it). A schema for another class
starts with `python3 scripts/gen_gamedata.py --new-schema <name> <File.kt> <Class>`. `version`
goes up only when a field is renamed or removed.
