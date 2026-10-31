"""The Quarry mark's own duration, 12 seconds, held equal to the skill that applies it.

WHY THIS FILE EXISTS. Issue #1520. `Debuff_Quarry` in `game/Data/StatusEffects.csv`
stated a duration of 0.0. The Staff's Quarry (`Demonic_Staff_Support` in
`game/Data/WeaponSkills.csv`) states `EffectDuration=12` and "for 12 seconds", and a
skill's own duration wins over the sheet's, so the cast mark always lasted twelve
seconds. The sheet's figure is read where no skill states one:

- `UCataclysmSkillEffects::CopyDebuffsTo` gives a copied curse the sheet's duration and
  copies nothing when that is zero. The Quarry's "if the quarry dies before the mark
  expires, it jumps to the nearest enemy" goes through it, as `OnDeath=SpreadDebuff`,
  so at 0.0 the mark never jumped.
- `UCataclysmSkillTemplate::AppliedEffectSeconds` falls back to it for a skill naming
  `Effect=Quarry` with no `EffectDuration`, and a zero applies nothing.

Ruled 12.0 on 2026-09-14 under the owner's delegation, matching the skill row and the
design document.

WHY THE SKILL ROW AND NOT THE EFFECT'S DESCRIPTION. The other pins on this sheet, in
`test_status_effect_numbers_match_their_prose.py`, quote a phrase of the effect's own
Description. Quarry's Description states no length ("while the mark holds"); the length
is stated by the one skill that applies the mark, so that is the second source checked
here. Change either number without the other and this fails.
"""

from __future__ import annotations

import csv
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"

QUARRY_SECONDS = 12.0


def _row(path: pathlib.Path, name: str) -> dict[str, str]:
    with path.open(encoding="utf-8", newline="") as handle:
        rows = [row for row in csv.DictReader(handle) if row["Name"] == name]
    assert len(rows) == 1, f"{path.name} has {len(rows)} rows named {name}, not one"
    return rows[0]


def test_the_quarry_mark_states_its_own_twelve_seconds() -> None:
    row = _row(STATUS_EFFECTS, "Debuff_Quarry")
    assert float(row["DurationSeconds"]) == QUARRY_SECONDS, (
        f"Debuff_Quarry states a duration of {row['DurationSeconds']}, not "
        f"{QUARRY_SECONDS}. A mark copied when its quarry dies takes this figure, and "
        "a zero copies nothing (issue #1520). It is column B of the Debuffs sheet."
    )


def test_the_skill_that_applies_the_mark_states_the_same_twelve_seconds() -> None:
    skill = _row(WEAPON_SKILLS, "Demonic_Staff_Support")
    params = [part.strip() for part in skill["ShapeParams"].split(";")]
    assert "Effect=Quarry" in params, skill["ShapeParams"]
    assert f"EffectDuration={QUARRY_SECONDS:g}" in params, (
        f"The Staff's Quarry states {skill['ShapeParams']!r}; the mark's own duration "
        f"of {QUARRY_SECONDS:g} seconds was ruled to match it."
    )
    assert f"for {QUARRY_SECONDS:g} seconds" in skill["SkillDescription"], skill["SkillDescription"]
