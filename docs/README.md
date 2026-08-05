# Design documents

The design documents. **These files are authoritative — edit them directly.**

They began as exports from the Google Drive folder "Cataclysm" (owner
`iamstephen777@gmail.com`) on 2026-08-02. As of that date the repository copies
became the source of truth and are **not** synced back to Drive. Treat the Drive
originals as historical.

A design decision is not real until it is in this folder.

| File | Source in Drive | Format note |
|---|---|---|
| `Cataclysm_GDD_v2.md` | Doc "Cataclysm\_GDD\_v2(1)" | Converted to Markdown so it produces readable diffs. No version number: see below. |
| `Empire_Skill_Tree_Keystones.md` | Doc "Empire Skill Tree Keystones" | The 12 keystones plus all four empire quadrants (Architect, Treasury, Explorer, Artisan). Converted to Markdown. |
| `All_Things_Cataclysm.xlsx` | Sheet "All Things Cataclysm" | 11 sheets. Exported unchanged as `.xlsx`. |
| `Empire_Development_Tree_Final.json` | Passive Trees/ | Node graph: `version`, `metadata`, `viewport`, `nodes`, `uiElements`, `edges`. |
| `Berserker_Class_Tree_Final.json` | Passive Trees/ | Same schema. |
| `Bulwark_Class_Tree_Final.json` | Passive Trees/ | Same schema. |
| `Saboteur_Class_Tree_Final.json` | Passive Trees/ | Same schema. |

## Sheets in `All_Things_Cataclysm.xlsx`

| Sheet | Rows | First columns |
|---|---|---|
| Dungeon Modifiers | 1012 | Cataclysm Type, Modifier Name, Weight, Description |
| Gems | 26 | Everyday / Quality / Superb / Master Gemstone |
| City Upgrades | 1007 | Type, Tier 1, Tier 2, Tier 3 |
| Enchantments | 961 | Positives, Type, Weight / Negatives, Type, Weight |
| **Tags** | **118** | **Tag Name, Description** |
| Enemy Modifiers | 1000 | Demonic / Death / War / Pestilence Modifiers |
| Weapon Skills | 1000 | Weapon Type, Damage Type, Slot, Skill Name, Description, Tags |
| Buffs | 18 | one description per row |
| Debuffs | 20 | one description per row |
| DoTs | 8 | one description per row |
| Crafting | 1005 | Material Name, Tier & Source, Primary Use, Functions, CR Metric |

The **Tags** sheet is the intended source for the Unreal `GameplayTag` table.

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

## Related, but not in this folder

The authoritative power model is **not** here. It lives in the separate
DungeonSimulator repository at `src/utils/calculateScores.tsx`, and
`sim/cataclysm_sim/scoring.py` is a port of it. That port checks itself against
the original on every run; see the comments in `scoring.py`.
