# Design documents

The design documents. **These files are authoritative — edit them directly.**

They began as exports from the Google Drive folder "Cataclysm" (owner
`iamstephen777@gmail.com`) on 2026-08-02. As of that date the repository copies
became the source of truth and are **not** synced back to Drive. Treat the Drive
originals as historical.

A design decision is not real until it is in this folder.

## Adding a table to one of the Markdown documents

**Write it as an ordinary Markdown table, with a heading row above the alignment
row.** Do not paste one out of Google Docs.

The Google Docs converter produces tables with an empty heading row and the real
headings one row lower with their bold markers escaped, which GitHub renders as a
blank heading band followed by a row of literal `\*\*Name\*\*` text. Every table
in this folder was converted away from that shape (issue #238), and pasting one
back in would reintroduce it for that table alone.

`tools/reformat_google_docs_artefacts.py` converts a pasted table, and
`tools/tests/test_design_documents_have_no_conversion_artefacts.py` fails if one
is left unconverted:

```bash
python tools/reformat_google_docs_artefacts.py
```

| File | Source in Drive | Format note |
|---|---|---|
| `Cataclysm_GDD_v2.md` | Doc "Cataclysm\_GDD\_v2(1)" | Converted to Markdown so it produces readable diffs. No version number: see below. |
| `Empire_Skill_Tree_Keystones.md` | Doc "Empire Skill Tree Keystones" | The 12 keystones plus all four empire quadrants (Architect, Treasury, Explorer, Artisan). Converted to Markdown. |
| `All_Things_Cataclysm.xlsx` | Sheet "All Things Cataclysm" | 19 sheets, listed below. Exported unchanged as `.xlsx`, and edited in place since. |
| `Empire_Development_Tree_Final.json` | Passive Trees/ | Node graph: `version`, `metadata`, `viewport`, `nodes`, `uiElements`, `edges`. |
| `Berserker_Class_Tree_Final.json` | Passive Trees/ | Same schema. |
| `Bulwark_Class_Tree_Final.json` | Passive Trees/ | Same schema. |
| `Saboteur_Class_Tree_Final.json` | Passive Trees/ | Same schema. |
| `Masochist_Class_Tree_Final.json` | Not from Drive | Same schema. Written in this repository for issue #63, from the prose in the Drive doc "Masochist Passive Tree". It is the only class tree here that Drive does not hold, so re-exporting the folder will not overwrite it and will not restore it either. |
| `DECISIONS.md` | Not from Drive | The log of design decisions and the reasoning behind them, newest first. Written in this repository. |
| `Save_System_Design.md` | Not from Drive | What the game writes to disk: the three persistence records, how saves are partitioned by lethality mode and by offline or online, the storage format, and schema migration. Written in this repository for issue #21. |
| `Audio_Design_Plan.md` | Not from Drive | How the audio design in section XIII gets built: MetaSounds rather than middleware, the six mixing buses, naming, and how a telegraph cue is authored by animation notify so it survives a play rate change. Written in this repository for issue #33. |
| `Animation_Plan.md` | Not from Drive | How much animation the game needs and what decides whether two skills share a motion: one set per weapon and slot shared across all eight damage types, identity from effects and behaviour, plus a signature reserve. Written in this repository for issue #18. |
| `Niagara_Conventions.md` | Not from Drive | How particle effects are named, parameterised, reused and budgeted: one system per effect shape rather than per damage type, and the culling settings that decide whether twenty enemies attacking at once is playable. Written in this repository for issue #19. |

## Sheets in `All_Things_Cataclysm.xlsx`

**Rows means rows that hold data, not the padded range.** Every count here is
measured from the file rather than typed, and
`tools/tests/test_docs_readme_sheet_table_is_true.py` fails if any of them drifts
or if a sheet is added or removed without this table changing.

| Sheet | Rows | First columns |
|---|---|---|
| Dungeon Modifiers | 117 | Cataclysm Type, Modifier Name, Weight, Description |
| Gems | 27 | Column 1, Everyday / Quality / Superb / Masterful / Legendary / Mythical Gemstone |
| City Upgrades | 24 | Type, Tier 1, Tier 2, Tier 3 |
| Enchantments | 380 | Positives, Type, Weight / Negatives, Type, Weight |
| **Tags** | **132** | **Tag Name, Description** |
| Enemy Modifiers | 11 | Demonic / Death / War / Pestilence / Famine / Celestial / Chaos Modifiers |
| Weapon Skills | 398 | Weapon Type, Damage Type, Slot, Skill Name, Skill Description, Tags, Shape, Shape Params, Crit Chance |
| Buffs | 18 | one description per row, no heading row |
| Debuffs | 26 | one description per row, no heading row |
| DoTs | 8 | one description per row, no heading row |
| Crafting | 37 | Material Name, Tier & Source, Primary Use, Functions, CR Metric |
| Item Bases | 55 | Base Name, Slot, Hands, Sub-Type, Weapon Type, Max Damage Types |
| Affixes | 85 | Affix Name, Affix Kind, Position, Stat, Value Kind, Top Value, Breadth |
| Class Stats | 33 | Class, Stat, Base, Per Level |
| Minion Types | 5 | Minion Type, Family, Base Health, Health Per Level, Base Damage, Damage Per Level, Attack Interval Seconds, Move Speed, Threat Percent, Reach Cm, Notice Radius Cm, Target Mode, Tags |
| Minion Scaling | 2 | Attribute, Requires Tag, Stat, Percent Per Point |
| Attributes | 17 | Attribute, Stat, Percent Per Point |
| Skill Slots | 7 | Slot, Damage Percent, Damage Lowest, Damage Highest, Cooldown |
| Element Visuals | 8 | Element Tag, Primary, Secondary, Emissive Multiplier, Spawn Rate Scale |

**A row is one entity on eleven of these sheets and not on the other six.** Do not
read a row count as a count of the things the sheet describes without checking
which group it is in:

- **Enchantments** holds two tables side by side. Its 380 rows carry 380 positive
  enchantments in columns A to D and 195 negative ones in columns F to I.
- **Enemy Modifiers** is a matrix, one column per Cataclysm and each cell holding
  `Name: Description`. Its 11 rows across 8 columns hold 79 modifiers.
- **Crafting** is three tables stacked: 18 materials, a heading row whose first
  cell is the word "Action", and 18 Forge operations. Rows 5 to 11 also carry an
  unrelated six-row table in columns 7 to 9.
  `tools/tests/test_crafting_section_matches_the_sheet.py` explains that shape in
  full.
- **Buffs**, **Debuffs** and **DoTs** have no heading row at all, so the first row
  is data. A count that assumes a heading is one short on each of them.

The **Tags** sheet is the intended source for the Unreal `GameplayTag` table, and
is the only sanctioned place to add a gameplay tag.

## Why these documents carry no version number

`Cataclysm_GDD_v2.md` used to say *Version 0.3* in its body while its filename
said `_v2` and the Drive document it came from was titled `Cataclysm_GDD_v2(1)`.
Three identifiers for one document, and none of them was ever advanced when the
design changed, so none of them said anything true.

**The version of a document in this folder is its git history.** Every change
arrives through a pull request and `DECISIONS.md` records the reasoning. A
hand-maintained number would be a fourth thing to keep in step with the other
three, and this project has already been bitten twice by hand-maintained
duplicates of a single fact.

The `_v2` in the filename is part of the name inherited from Drive. It is not a
counter and it does not advance. The file is not renamed because roughly twenty
test files and several C++ sources name it by path.

Issue #35. The table of contents was removed in the same change: it was 106
links back into the Google Drive document, all pointing at the same anchor.

## Decisions made outside Drive

`DECISIONS.md` is the dated log of design decisions and the reasoning behind
them. Decisions are applied directly to the documents in this folder; the log
records *why* a thing is the way it is, which the documents themselves do not.

Entries dated 2026-08-02 predate the switch to editing these files directly, and
say they still need folding into Drive. They do not — that instruction is
obsolete. Those decisions still need applying to the files in this folder.

## Not exported

- **`Empire Diagram.pdf`** — a Lucidchart export from 2023-01-12. It predates the
  layered Pillar / Sanctuary / Bulwark / Outpost graph the sim models, so it is
  probably stale. Ask before relying on it.
- **"Masochist Passive Tree"** and **"Nephilim Passive Tree"** — class design docs
  in the same Drive folder, not part of the empire layer.

## Which file is the empire passive tree

**`Empire_Development_Tree_Final.json` is the tree. `Empire_Skill_Tree_Keystones.md`
is commentary on it.** Both describe the empire passive tree and they are not the
same size, so this needs saying rather than guessing. Issue #25.

| | The node graph | The prose |
|---|---|---|
| File | `Empire_Development_Tree_Final.json` | `Empire_Skill_Tree_Keystones.md` |
| What it holds | 159 nodes, 158 edges, 173 distinct node and capstone-option names | 105 bullets across 4 keystone tiers, 4 branch capstones and 4 quadrants |
| Last edited | 2026-03-05, per its own `metadata.updatedAt` | 2026-02-10 |
| Authoritative for | The tree's structure and every node's name and effect | Nothing the graph also states |

All 105 of the prose's bullets name a node that is in the graph. The graph has 68
names the prose does not mention at all, because it is roughly three weeks newer
and was expanded after the prose was written. **Where the two disagree, the graph
is right.**

**Every prose bullet now names a node in the graph.** Three did not until
2026-08-05: Bounties, Weightless Spoils, and the Architect quadrant's four
decision nodes. Issue #260 asked whether they were cut or lost, and the project
owner answered that this file is brainstorming written before the passive tree
editor existed — so an idea in the prose that is not in the graph was never built
rather than lost from it. The three were removed from the prose and recorded in
full in `DECISIONS.md`. Dropping Weightless Spoils leaves nothing in the design
granting inventory slots, which is issue #308.

**The quadrant is called Treasury.** The prose called it three things: Treasurer
in the branch list, Tyrant in the capstone list and Treasury in its section
heading. The graph uses Treasury nine times, Treasurer and Tyrant never, and its
on-canvas label reads TREASURY. The prose now matches. Note this is a different
word from the **Treasurer** city upgrade in `game/Data/CityUpgrades.csv`, which
keeps its name.

**The node graph is not generated output.** The passive tree editor at
`C:\Projects\PassiveTreeCreator` holds no tree data of its own — it opens a JSON
file the user picks and downloads one back. So this file is the data, and editing
it here is safe. The same is true of the three class tree JSON files.

`tools/tests/test_empire_tree_documents_agree.py` holds the comparison, so the two
files cannot drift apart again without something failing.

## Related, but not in this folder

The authoritative power model is **not** here. It lives in the separate
DungeonSimulator repository at `src/utils/calculateScores.tsx`, and
`sim/cataclysm_sim/scoring.py` is a port of it. That port checks itself against
the original on every run; see the comments in `scoring.py`.

**That model, not this folder, is authoritative for the Enemy Score numbers.**
The Enemy Score Formula section of `Cataclysm_GDD_v2.md` and the Dungeon Score
Formula section are transcriptions of it: the rarity, dungeon type and subtype
weights, the floor scaling bases, the base type scores and the player tier
anchors. Editing a number in the document changes nothing about the game; it only
makes the document wrong.

Those two sections went stale for a year, describing the model's January 2025
first-commit formula after it was replaced upstream and never re-exported. Issue
#30 has the history. `tools/tests/test_enemy_score_formula.py` now compares every
one of those tables against the port, so the same drift cannot happen quietly
again.
