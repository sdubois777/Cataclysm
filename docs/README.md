# Design documents

Snapshots of the design documents, exported from the Google Drive folder
"Cataclysm" (owner `iamstephen777@gmail.com`) on 2026-08-02.

**Drive is still the place these are edited.** These copies exist so that the
simulation and the eventual Unreal data pipeline can be diffed against a fixed
version of the design, and so a reader of this repo can see the rules the sim is
trying to model without needing Drive access. Re-export when Drive changes.

| File | Source in Drive | Format note |
|---|---|---|
| `Cataclysm_GDD_v2.md` | Doc "Cataclysm\_GDD\_v2(1)" | Body text says *Version 0.3*. Converted to Markdown so it produces readable diffs. |
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
