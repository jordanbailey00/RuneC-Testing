# RuneC Data Database — Plan

Everything the engine needs to be fully data-driven. For each category: what
it is, whether we already have it, and where it comes from if we don't.

Sources are ranked by authority. We take the highest-authority source per
category and only fall back when it's missing the data point.

---

## Data Categories (Everything the Engine Needs)

### Entity data
1. **NPC definitions** — name, combat level, HP, size, att/def/str/ranged/magic stats, stand/walk/run/attack/death animations, models, recolors
2. **NPC spawn positions** — (world_x, world_y, plane, direction, wander_range, is_members)
3. **NPC drop tables** — item_id × quantity_range × rarity, per NPC, with drop groups (always/main/tertiary)
4. **NPC AI behaviour** — is_aggressive, aggro_range, retreat_distance, attack_speed, attack_style, poison/venom, max hit
5. **NPC dialogue** — dialogue trees (state, options, branches, quest hooks)
6. **Player-model kit data** — hair/beard/torso/etc. base model IDs, HSL color palettes

### Item data
7. **Item definitions** — name, examine, stackable, tradeable, members-only, weight, noted pair, quest-item flag, slot type
8. **Item equipment bonuses** — stab/slash/crush att, mage-att, range-att, 5× def bonuses, str, mage-dmg, range-str, prayer
9. **Item actions** — eat/drink/wear/wield/bury/rub/spin (right-click verbs)
10. **Item ground spawns** — respawning floor items (cake stall, knives, etc.)
11. **GE prices** — live buy/sell average, buy/sell volume
12. **High/low alch values** — amount + nature rune cost baked in

### World data
13. **Object/scenery definitions** — name, model, actions (open/close/climb), size, collision flags, sound (already extractable from cache)
14. **Object interactions** — doors (pair + direction), stairs/ladders (target plane + coord), gates, trapdoors, ladders to dungeons
15. **Region areas** — named zones (multi-combat, wilderness, members-only, safe zones, PvP, music trigger boxes)
16. **Music + ambient** — per-region BGM, jingles, sound effects
17. **Weather/env** — fog, light color per region
18. **Teleports** — teleport targets for spell+object+jewelry teleports

### Skills & combat
19. **Skill XP curves** — level_for_xp table (1–99, identical to standard OSRS)
20. **Per-action skill data** — woodcutting (tree → logs, XP, reqs, success rate), mining (rock → ore), fishing (spot → fish), cooking (raw → cooked), smithing (bar + level → product), firemaking (logs → fire), fletching, crafting, herblore (herbs + secondary → potion), farming, hunter, slayer (monsters per master, points per kill), runecrafting (altar → rune × multiplier)
21. **Combat formulas** — accuracy + max hit (melee/ranged/magic) — code-only
22. **Prayer definitions** — level req, drain rate, effect, overhead icon
23. **Spellbook** — spell req, runes, damage, XP, cast animation, splash graphic (normal + ancient + lunar + arceuus)
24. **Weapon attack styles** — 4 style × weapon type, speed in ticks, XP split

### Progression
25. **Quest definitions** — steps, vars, requirements (skills + quests + items + combat), rewards (XP + items + QP), dialogue order
26. **Achievement diary tasks** — task text, req, reward (XP lamp, item, teleport)
27. **Combat achievements** — tier, task, reward
28. **Music track unlocks** — track name, unlock coord/condition
29. **Collection log** — categories, required drops per slot

### Interaction / UI
30. **Shop stock** — shop_id → (item_id, base_quantity, restock_rate, sell_price_mult, buy_price_mult)
31. **Right-click menu verbs** — per item / per NPC / per object (comes from defs)
32. **Varbits** — the ~15,000 named varbits that drive quest state, shop stock, diary progress, interface visibility
33. **Varplayers** — similar, player-scoped variables
34. **Interface/widget definitions** — UI layouts (inventory, bank, shop, etc.), already extractable from cache
35. **Random events** — trigger, NPC spawn, reward, cooldown
36. **Minigames** — Castle Wars, Pest Control, TzHaar Fight Cave, Barrows, Wintertodt etc. (data defs per game)

### Meta
37. **NPC ID names** — human-readable constants (NPC_GUARD_397 etc.) for dev scripts
38. **Object ID names** — same
39. **Item ID names** — same
40. **Region metadata** — rx/ry → name, continent, members flag

---

## Coverage Matrix

Legend: ✅ = fully covered, 🟡 = partial (scrape to fill gaps), ❌ = must source
externally. Mark the single primary source in **bold** per row.

| # | Category | b237 cache | RuneLite parsers | Void RSPS | 2011Scape | RSMod | Best gap filler |
|---|---|---|---|---|---|---|---|
| 1 | NPC defs (names, stats, models, anims) | **✅** | ✅ (parsers) | 🟡 | ❌ | 🟡 | — |
| 2 | NPC spawn positions | ❌ | ❌ | 🟡 (2011-era) | **🟡** (authoritative 2011) | ❌ (Lum only) | **Wiki Cargo** for post-2011 |
| 3 | NPC drop tables | ❌ | ❌ | 🟡 (121 files) | ❌ | ❌ | **Wiki Cargo `DropsLine`** |
| 4 | NPC AI / aggression | ❌ | ❌ | ❌ | ✅ (aggro plugin) | 🟡 | **osrsreboxed-db** |
| 5 | NPC dialogue | ❌ | ❌ | 🟡 (code) | ❌ | 🟡 (Lum) | **Wiki transcripts** (hand-curated) |
| 6 | Player kit data | ✅ | ✅ | ✅ | ❌ | ❌ | — |
| 7 | Item defs | **✅** | ✅ | 🟡 | 🟡 (names only) | 🟡 | — |
| 8 | Item equipment bonuses | ❌ | ❌ | ❌ | ❌ | ❌ | **osrsreboxed-db** |
| 9 | Item actions | ✅ | ✅ | 🟡 | ❌ | 🟡 | — |
| 10 | Item ground spawns | ❌ | ❌ | **✅** (89 files) | 🟡 (1 file) | 🟡 (Lum) | Void |
| 11 | GE prices | ❌ | ❌ | ❌ | ❌ | ❌ | **prices.runescape.wiki API** |
| 12 | Alch values | ✅ (in item def) | ✅ | ✅ | ✅ | ✅ | — |
| 13 | Object defs | **✅** | ✅ | 🟡 | 🟡 | 🟡 | — |
| 14 | Object interactions (doors etc.) | 🟡 (flags) | 🟡 | ✅ (85 files) | **✅** (doors.json, stairs.json) | 🟡 | — |
| 15 | Region areas | 🟡 (areas file in cache) | ✅ | ✅ (104 files) | 🟡 | ❌ | — |
| 16 | Music | ✅ (tracks in cache) | ✅ | ✅ | ✅ (music_by_region.yaml) | ❌ | — |
| 17 | Weather/env | ✅ (region metadata) | ✅ | ❌ | ❌ | ❌ | — |
| 18 | Teleports | ❌ | ❌ | **✅** (123 files) | ❌ | ❌ | — |
| 19 | Skill XP curves | ❌ | ✅ (Experience.java) | ✅ | ✅ | ✅ | — |
| 20 | Per-action skill data | ❌ | ❌ | **✅** (31 skills) | 🟡 | 🟡 | — |
| 21 | Combat formulas | ❌ | ❌ | ✅ (code) | ❌ | **✅** (RSMod combat-accuracy) | — |
| 22 | Prayer defs | ❌ | 🟡 (names) | **✅** | 🟡 | ✅ | — |
| 23 | Spellbook | ❌ | 🟡 | **✅** | ❌ | 🟡 | — |
| 24 | Weapon attack styles | ❌ | ❌ | ✅ | ❌ | 🟡 | — |
| 25 | Quest definitions | ❌ | ❌ (enum only) | **✅** (489 files) | ❌ | ❌ | **Wiki Cargo `QuestDetails`** for post-2011 |
| 26 | Diary tasks | ❌ | 🟡 (req code) | ✅ (varbits) | ❌ | ❌ | **Wiki Cargo `AchievementDiaryTask`** |
| 27 | Combat achievements | ❌ | ❌ | ❌ | ❌ | ❌ | **Wiki Cargo** |
| 28 | Music track unlocks | ❌ | ❌ | 🟡 | ❌ | ❌ | **Wiki Cargo** |
| 29 | Collection log | ❌ | ❌ | ❌ | ❌ | ❌ | **Wiki Cargo** |
| 30 | Shop stock | ❌ | ❌ | **✅** (66 files) | 🟡 | ❌ | — |
| 31 | Right-click verbs | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| 32 | Varbits (semantic) | 🟡 (structural) | 🟡 (IDs) | 🟡 | ❌ | ❌ | **mejrs/data_osrs** + Wiki |
| 33 | Varplayers | 🟡 | 🟡 | 🟡 | ❌ | ❌ | **mejrs/data_osrs** |
| 34 | Interface/widget defs | ✅ | ✅ | ❌ | ❌ | ❌ | — |
| 35 | Random events | ❌ | ❌ | **✅** | ✅ (plugin) | ❌ | — |
| 36 | Minigames | ❌ | ❌ | **✅** (37) | 🟡 | ❌ | — |
| 37 | NPC ID names | ❌ | **✅** (`NpcID.java`) | — | ✅ (Npcs.kt) | 🟡 | — |
| 38 | Object ID names | ❌ | **✅** | — | ✅ | 🟡 | — |
| 39 | Item ID names | ❌ | **✅** | — | ✅ | 🟡 | — |
| 40 | Region metadata | 🟡 | ✅ | 🟡 | ✅ | ❌ | — |

---

## Gaps Requiring External Sources

### Hard gaps (no repo has these)
- **Item equipment bonuses** (#8) — every item's stab/slash/crush att, def bonuses, str bonus. `osrsreboxed-db` has this per-item JSON.
- **Comprehensive NPC drop tables** (#3) — Void has 121 files but misses most common mobs and all post-2011 content. OSRS Wiki `DropsLine` Cargo table is authoritative.
- **GE prices** (#11) — live data, use prices.runescape.wiki API.
- **Combat achievements** (#27), **collection log** (#29), **music unlocks** (#28) — Wiki Cargo.
- **Varbit semantics** (#32) — `mejrs/data_osrs` has decoded varbits; wiki explains what each controls.

### Post-2011 content gaps (Void + 2011Scape frozen at 2011)
- NPCs added later: Xuan (GE), Haakon the Champion, Herald of Varrock, Zeah/Kourend entire continent, Fossil Island, Prifddinas, new bosses (Vorkath, Nex, Nightmare, Leagues content, etc.)
- Modern quests (Song of the Elves, Dragon Slayer II, Sins of the Father, etc.)
- Must pull these from Wiki or osrsreboxed-db.

### Nice-to-have gaps
- **Dialogue transcripts** (#5) — Wiki `Transcript:*` pages. Not strictly needed for MVP but required for quests.

---

## External Sources to Clone / Integrate

Put everything under `/home/joe/projects/runescape-rl-reference/`.

### 1. `osrsreboxed-db` — **TOP PRIORITY**
- Repo: https://github.com/0xNeffarion/osrsreboxed-db
- License: GPL-3.0
- Last push: 2025-01-07 (maintained)
- Covers gaps **#4, #8, items/monsters/prayers** metadata
- Format: `docs/items-json/{id}.json`, `docs/monsters-json/{id}.json`, plus summary dumps
- ~944MB clone but only need json dirs; can do sparse-checkout
- Clone: `git clone --depth 1 https://github.com/0xNeffarion/osrsreboxed-db.git`

### 2. `mejrs/data_osrs`
- Repo: https://github.com/mejrs/data_osrs
- License: unlicensed (treat as reference-only)
- Last push: 2025-11-23 (very active, tracks live cache)
- Covers gaps **#32, #33** (varbits + varps) and cross-validates item/NPC dumps
- Format: JSON per category
- 46MB — clone whole thing
- Clone: `git clone --depth 1 https://github.com/mejrs/data_osrs.git`

### 3. `runelite/runelite` (ID constants only)
- Repo: https://github.com/runelite/runelite
- License: BSD-2
- Files we need: `runelite-api/src/main/java/net/runelite/api/gameval/{NpcID,ItemID,ObjectID,VarbitID}.java`
- Covers gap **#37–39** with human-readable names for cross-referencing
- Already partially present in our reference dir — just pull IDs for mapping

### 4. OSRS Wiki APIs (no clone — use at data-build time)
- MediaWiki API: `https://oldschool.runescape.wiki/api.php`
- Cargo query: `https://oldschool.runescape.wiki/w/Special:CargoTables`
- Key cargo tables for our gaps:
  - `DropsLine` → drop tables (#3)
  - `MonsterStats` → NPC stats cross-check (#1, #4)
  - `ItemStats` → item bonus cross-check (#8)
  - `QuestDetails` → post-2011 quests (#25)
  - `AchievementDiaryTask` → diaries (#26)
  - `VarbitDefinition` → varbit names/values (#32)
  - `SpawnLines` → NPC spawns with x/y/plane (#2)
- Cache raw responses to disk under `tools/wiki_cache/` (re-run = no re-fetch)

### 5. Live runtime API (no clone)
- `https://prices.runescape.wiki/api/v1/osrs/latest` → GE prices (#11)
- `https://prices.runescape.wiki/api/v1/osrs/mapping` → id ↔ name

---

## Proposed Storage Layout

```
data/
  defs/
    npcs.bin           # NDEF — extended: stats + aggro + drop_table_id
    items.bin          # IDEF — full def + equipment bonuses
    objects.bin        # ODEF — already have via cache
    prayers.bin
    spells.bin
    shops.bin
    drops.bin          # drop tables indexed by id, referenced from npcs.bin
    teleports.bin
    varbits.bin        # id → name + type
    regions.bin        # area metadata
  quests/
    {quest_id}.bin     # per-quest state machine + rewards
  diaries/
    {region}.bin       # diary tasks
  spawns/
    {region}.npcs.bin  # NPC spawns per region (NSPN)
    {region}.items.bin # ground items per region
  skills/
    {skill}.bin        # per-action tables (level, reward, animation)
```

All binary — consumed by rc-core loaders. No TOML/JSON at runtime.

---

## Build Pipeline

`tools/build_database.sh` runs these in order:

1. **Clone external repos** (once) → `runescape-rl-reference/osrsreboxed-db`, `data_osrs`
2. **Extract from b237 cache**: item defs, NPC defs, object defs, anims, models, sprites, widgets, varbit IDs (structural)
3. **Extract from osrsreboxed-db**: item equipment bonuses → merge into IDEF; NPC aggression + weaknesses → merge into NDEF
4. **Extract from Void RSPS TOMLs**: shop stock, teleports, ground item spawns, per-skill actions, prayers, spellbook, minigame data, quest definitions (2011-era)
5. **Extract from 2011Scape .kts**: NPC spawn positions (authoritative 2011-era), shop scripts where Void is missing
6. **Scrape OSRS Wiki Cargo** (cached to `tools/wiki_cache/`):
   - `DropsLine` → drops.bin
   - `SpawnLines` for regions we care about → fill gaps in NPC spawn coverage (post-2011 NPCs)
   - `QuestDetails` + `AchievementDiaryTask` → post-2011 quests + diaries
   - `VarbitDefinition` → varbit semantics
7. **Cross-validate**: item bonuses (osrsreboxed-db vs wiki Cargo), NPC stats (osrsreboxed-db vs 2011Scape).
8. **Emit binaries** under `data/`.

---

## Wiki Scrape Strategy (for #3, #5, #25–28, #32)

### API approach (preferred)
- Use MediaWiki action API + Cargo tables. Structured, stable, cacheable.
- Example: `https://oldschool.runescape.wiki/api.php?action=cargoquery&tables=DropsLine&fields=DroppedItem,DroppedFrom,Rarity,Quantity&limit=500&format=json`
- Pagination via `offset` param; respect rate limit (~1 req/sec, be kind).
- Cache responses to `tools/wiki_cache/{table}_{offset}.json`. Re-run is free.

### Fallback: page scrape
- Only when Cargo doesn't have it (dialogue transcripts, niche boss mechanics).
- `requests` + `mwparserfromhell` to parse wikitext templates.

### Ethics / ToS
- CC-BY-NC-SA 3.0: attribute the wiki, non-commercial use only.
- For our project (personal RL/ML experiment), this is fine.
- Script: set User-Agent `RuneC-data-builder/0.1 (contact: jordanbaileypmp@gmail.com)`.

---

## Open Decisions

1. **Quest implementation scope**: Void has 489 quest files but only some are actually coded server-side. Do we implement every quest or just MVP subset (Cooks Assistant, Demon Slayer, Romeo & Juliet)? → **Recommend MVP subset for now, keep data for all.**
2. **Post-2011 content cutoff**: Do we target current OSRS or freeze at some revision? → **Recommend: cache is b237 (2025), so target current content; gaps acceptable where wiki silent.**
3. **Binary vs SQLite**: All flat binaries simplify C loading; SQLite helps dev queries. → **Recommend flat binaries for runtime, parallel SQLite export for tool scripts.**
4. **Drop table rarity format**: Wiki uses fractions like `1/128` or `Common`. → **Parse fractions to floats; map keywords via table (Always=1.0, Common=1/20, Uncommon=1/50, Rare=1/128, Very rare=1/512).**
