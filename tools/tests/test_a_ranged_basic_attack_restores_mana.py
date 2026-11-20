"""A ranged basic attack restores its mana when its shot lands, once per use, to the character that fired it.

WHY THIS EXISTS. Issue #2722: the design says "The basic attack returns 6 mana each time it lands"
(`docs/Cataclysm_GDD_v2.md`, "The Basic Attack Restores Mana, and This Is Not a Generator") with no exception for a
weapon that shoots, and the game paid it only through `UCataclysmSkillTemplate::HitTargets`. The four ranged basic
attacks are shots with a speed (`game/Data/ItemBases.csv`), which land through `ACataclysmProjectile::HitOne` and never
pass through `HitTargets`, so they paid nothing. The coordinating session approved the fix on 2026-11-19: `HitOne` pays
the firing slot's mana on hit through `UCataclysmSkillTemplate::PayManaOnHitTo`, to the firer, when the shot lands
carrying damage.

ONCE PER USE. `HitTargets` pays a swing that strikes five once, which is the design's own arithmetic (6 x 1.3 attacks a
second = 7.8). So a shot pays at the first landing of its whole use: not again for a second enemy a pierce or a glance
reaches, not for each enemy in its own blast, and not for each projectile of a use a row of the user's fired several of.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the `Cataclysm.RangedManaOnHit.` automation
tests never run on a pull request.

WHAT IS CHECKED:

  1. the basic attack's slot is the only one stating a mana on hit, so the payment is inert for every other skill's
     shot;
  2. the weapons whose basic attack is a shot with a speed are the four the issue names, and each is a projectile;
  3. `HitOne` works out the first landing of the use before the use's record takes the landing in, pays only then,
     only for a shot carrying damage and not evaded, and pays the firer's own ability system;
  4. `ApplyManaOnHit`, the payment `HitTargets` makes, goes through the same `PayManaOnHitTo`, so the two cannot drift.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
ABILITIES = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"

#: The four weapons issue #2722 names, by their row in `ItemBases.csv`.
RANGED_BASICS = {"Weapon_Wand", "Weapon_Crossbow", "Weapon_Staff", "Weapon_Two_Handed_Crossbow"}


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def braced(text: str, start: int, where: str) -> str:
    """The braces-matched block that opens at the first `{` after `start`."""
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:index + 1]
    raise AssertionError(f"{where} is not closed")


def body(text: str, signature: str, where: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    return braced(text, start, f"{signature!r} in {where}")


def test_only_the_basic_attacks_slot_states_a_mana_on_hit() -> None:
    with (DATA / "SkillSlots.csv").open(encoding="utf-8", newline="") as handle:
        stating = {row["Slot"]: float(row["ManaOnHit"]) for row in csv.DictReader(handle) if float(row["ManaOnHit"])}
    assert stating == {"Basic": 6.0}, f"the slots stating a mana on hit are {stating}, not the basic attack's 6 alone"


def test_the_four_ranged_basic_attacks_are_shots_with_a_speed() -> None:
    with (DATA / "ItemBases.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    first = next(iter(rows[0]))
    shooting = {}
    for row in rows:
        speed = re.search(r"\bSpeed=([0-9.]+)", row.get("BasicShapeParams") or "")
        if speed and float(speed.group(1)) > 0.0:
            shooting[row[first]] = row["BasicShape"]
    assert set(shooting) == RANGED_BASICS, f"the weapons whose basic attack flies are {sorted(shooting)}"
    assert set(shooting.values()) == {"Projectile"}, f"their basic attacks' shapes are {shooting}"


def test_a_landed_shot_pays_once_per_use_to_its_firer() -> None:
    hit_one = body(code(ABILITIES / "CataclysmProjectile.cpp"), "void ACataclysmProjectile::HitOne(",
                   "CataclysmProjectile.cpp")
    landed = re.search(r"if \(Dealt > 0\.0f && !Resolved\.bEvaded\)", hit_one)
    assert landed, "HitOne no longer opens its landed branch on `Dealt > 0.0f && !Resolved.bEvaded`"
    branch = braced(hit_one, landed.start(), "HitOne's landed branch")
    first = re.search(r"bFirstLandingOfItsUse\s*=\s*!bPaidManaOnHit\s*&&\s*\(\s*!LandedOnInItsUse\.IsValid\(\)\s*\|\|"
                      r"\s*LandedOnInItsUse->Num\(\)\s*==\s*0\s*\)\s*;", branch)
    taken_in = branch.find("LandedOnInItsUse->Add(")
    assert first and taken_in != -1 and first.start() < taken_in, (
        "the first landing of the use is not worked out before the use's record takes this landing in")

    pays = [match.start() for match in re.finditer(r"PayManaOnHitTo\(", hit_one)]
    assert len(pays) == 1, f"HitOne pays mana on hit in {len(pays)} places, not one"
    before = hit_one[:pays[0]]
    gates = ["if (!Resolved.bEvaded)", "if (SpentMoreMultiplier > 0.0f)", "if (bFirstLandingOfItsUse)"]
    places = [before.rfind(gate) for gate in gates]
    assert all(place != -1 for place in places) and places == sorted(places), (
        "the payment is not inside the not-evaded branch, under a shot carrying damage, at the first landing of the use")
    assert re.search(r"bPaidManaOnHit\s*=\s*true\s*;\s*Firing->PayManaOnHitTo\(\s*UCataclysmTargeting::AbilitySystemOf"
                     r"\(\s*Firer\s*\)\s*\)\s*;", hit_one[before.rfind("if (bFirstLandingOfItsUse)"):]), (
        "the payment does not mark the shot paid and pay the firer's own ability system")


def test_the_swing_and_the_shot_pay_through_one_function() -> None:
    template = code(ABILITIES / "CataclysmSkillTemplate.cpp")
    swing = body(template, "void UCataclysmSkillTemplate::ApplyManaOnHit() const", "CataclysmSkillTemplate.cpp")
    assert "PayManaOnHitTo(" in swing, "ApplyManaOnHit no longer pays through PayManaOnHitTo"
    payment = body(template, "void UCataclysmSkillTemplate::PayManaOnHitTo(", "CataclysmSkillTemplate.cpp")
    assert "GetManaOnHit()" in payment and "IsStatRemoved(" in payment and "ApplyModToAttribute(" in payment, (
        "PayManaOnHitTo no longer reads the slot's figure, asks whether mana on hit is removed, and adds it")
