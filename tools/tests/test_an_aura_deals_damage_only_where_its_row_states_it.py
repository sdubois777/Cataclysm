"""An aura deals damage only where its row states it, and Conflagration states 10%.

WHY THIS EXISTS. Two rulings by the owner on 2026-11-24.

- Issue #2875, "No damage unless stated (Recommended)": an aura deals damage only
  if its description says so. Until then a row of `game/Data/WeaponSkills.csv`
  stating no Damage Percent fell back to its slot's figure, so five Aura-slot rows
  whose sentences name no damage -- Exposing Light, Miasma, Pall of Ending,
  Unreason, Gnawing Want -- dealt the slot's 25% of weapon damage a second to
  everything within 10 metres, and Undertow the same 25% a second as 150% every
  six seconds. `UCataclysmSkillTemplate::GetDamagePercent` now answers nought for
  an aura stating no figure.
- Issue #2869, "4 m, 10% a second (Recommended)": Conflagration's ring is 4 metres
  and its pulse is 10% of weapon damage a second, stated on its row.

WHAT THIS CHECKS. Every row whose Shape is Aura is listed below by a human with
what it deals and why, so a new aura row fails here until somebody says whether
its sentence names damage. Each row states exactly that figure, or none. A row
stating none names no weapon damage and no fire damage in its sentence. The Aura
slot's band in `game/Data/SkillSlots.csv` holds Conflagration's 10. And the C++
rule itself is present, read from the source as text, because continuous
integration does not run the automation tests that exercise it
(`Cataclysm.AuraDamage.` and
`Cataclysm.SkillNumbers.AnAuraStatingNoDamageDealsNoneOnAnyKeyAndKeepsAFigureItStates`).

WHAT IT DOES NOT CHECK. That a pulse at nought still lays its burn, ailment,
curse, shove or pull; the automation tests do.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"
SKILL_SLOTS = REPO_ROOT / "game" / "Data" / "SkillSlots.csv"
SKILL_TEMPLATE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm"
                      / "AbilitySystem" / "CataclysmSkillTemplate.cpp")

#: What a row means by stating no figure of its own.
NO_FIGURE_STATED = -1.0

#: Every row with the Aura shape: its skill name, the Damage Percent it states
#: (None for none, so it deals nothing), and why, read by a human on 2026-11-24.
#:
#: ADDING A ROW HERE IS HOW A NEW AURA IS ACCEPTED, and the reason is the point:
#: the ruling turns on what the sentence says, which nothing mechanical decides.
AURAS: dict[str, tuple[str, float | None, str]] = {
    "Demonic_All_Aura": (
        "Conflagration", 10.0,
        "'Enemies within it take 10% weapon damage each second': the owner's "
        "ruling on #2869"),
    "Demonic_Fist_Ultimate": (
        "Living Pyre", 66.67,
        "'raises the pyre's fire damage by 8%' names damage and states no "
        "figure; the owner ruled on 2026-11-24, '400% spread over 6 pulses "
        "(Recommended)', so the row states 66.67 a pulse (#2875)"),
    "Celestial_All_Aura": (
        "Exposing Light", None,
        "a resistance cut and an ally bonus; no damage named"),
    "Pestilence_All_Aura": (
        "Miasma", None,
        "'poisoned continuously' and an ally bonus; the poison is the effect"),
    "Void_All_Aura": (
        "Undertow", None,
        "a pull every 6 seconds and an ally bonus; no damage named. Not one of "
        "the five the ruling names, and inside the rule it states"),
    "Death_All_Aura": (
        "Pall of Ending", None,
        "enemies deal 15% less damage, and 10% more for the caster, minions "
        "and allies (the owner, 2026-11-25); it deals none"),
    "War_All_Aura": (
        "Blood and Iron", None,
        "armour and movement taken from enemies, speed and slow immunity for "
        "the caster and allies; it deals none. Given the Aura shape on "
        "2026-11-25 (the owner's 'Go with A')"),
    "Chaos_All_Aura": (
        "Unreason", None,
        "one curse a second drawn at random, and an ally bonus"),
    "Famine_All_Aura": (
        "Gnawing Want", None,
        "Withered Touch and an ally bonus; no damage named"),
    "Death_Greatsword_Support": (
        "Grave Muster", None,
        "a Cripple each second and an ally bonus; no damage named. Approved by "
        "the owner on 2026-11-24 (#2885)"),
    "Death_Dagger_Support": (
        "Grave Shroud", None,
        "Necrosis each second and two immunities; the Support slot's nought "
        "before the ruling too"),
    "Pestilence_Fist_Support": (
        "Fevered Blood", None,
        "poison each second and an immunity; the Support slot's nought before "
        "the ruling too"),
    "Chaos_Whip_Support": (
        "Coin of Misrule", None,
        "one of two misfortunes each second; the Support slot's nought before "
        "the ruling too"),
    "Celestial_Warhammer_Support": (
        "Aegis of Dawn", None,
        "a push each second; the Support slot's nought before the ruling too"),
}

#: Words a sentence uses when the aura itself deals damage. A row stating none
#: must not use them, or its sentence promises a blow it does not deal.
NAMES_ITS_OWN_DAMAGE = re.compile(r"weapon damage|fire damage", re.IGNORECASE)


def table(path: pathlib.Path) -> list[dict[str, str]]:
    text = path.read_bytes().decode("utf-8-sig")
    return list(csv.DictReader(text.splitlines()))


def aura_rows() -> dict[str, dict[str, str]]:
    return {row["Name"]: row for row in table(WEAPON_SKILLS)
            if row["Shape"] == "Aura"}


def deals(row: dict[str, str]) -> float:
    """What a pulse of this aura deals, in per cent of weapon damage a second:
    the row's own figure, and nought when it states none. The Python reading of
    `UCataclysmSkillTemplate::GetDamagePercent` for an aura."""
    stated = float(row["DamagePercent"])
    return stated if stated >= 0.0 else 0.0


def test_every_aura_row_is_listed_and_no_listed_row_is_gone() -> None:
    found = aura_rows()
    assert len(found) >= 12, (
        f"only {len(found)} Aura rows in {WEAPON_SKILLS.name}; the table did "
        "not load or was cut short")
    unlisted = sorted(set(found) - set(AURAS))
    gone = sorted(set(AURAS) - set(found))
    assert not unlisted, (
        f"these rows have the Aura shape and nobody has said what they deal: "
        f"{unlisted}. Read each sentence; an aura deals damage only if it says "
        "so (#2875). Add it to AURAS with the figure and the reason.")
    assert not gone, f"AURAS names rows that are no longer auras: {gone}"


def test_each_aura_row_carries_its_listed_name() -> None:
    found = aura_rows()
    wrong = [f"{key}: {found[key]['SkillName']!r}, listed as {name!r}"
             for key, (name, _, _) in AURAS.items()
             if key in found and found[key]["SkillName"] != name]
    assert not wrong, "; ".join(wrong)


def test_each_aura_states_exactly_the_figure_listed_or_none() -> None:
    found = aura_rows()
    wrong = []
    for key, (name, figure, _) in AURAS.items():
        if key not in found:
            continue
        stated = float(found[key]["DamagePercent"])
        expected = NO_FIGURE_STATED if figure is None else figure
        if abs(stated - expected) > 0.001:
            wrong.append(f"{name} ({key}) states {stated}, listed {expected}")
    assert not wrong, "; ".join(wrong)


def test_an_aura_stating_no_damage_names_none_in_its_sentence() -> None:
    found = aura_rows()
    promising = [f"{AURAS[key][0]}: {found[key]['SkillDescription']!r}"
                 for key in found
                 if key in AURAS and AURAS[key][1] is None
                 and NAMES_ITS_OWN_DAMAGE.search(found[key]["SkillDescription"])]
    assert not promising, (
        "these auras state no Damage Percent and their sentences name weapon or "
        "fire damage, so they promise a blow they will not deal: "
        + "; ".join(promising))


def test_an_aura_stating_damage_names_it_in_its_sentence() -> None:
    found = aura_rows()
    silent = [AURAS[key][0] for key in found
              if key in AURAS and AURAS[key][1] is not None
              and not NAMES_ITS_OWN_DAMAGE.search(found[key]["SkillDescription"])]
    assert not silent, (
        f"these auras state a Damage Percent and their sentences name no damage "
        f"of their own: {silent}. An aura deals damage only if its description "
        "says so (#2875).")


def test_what_each_aura_deals_a_second() -> None:
    """The figures the rulings leave: Conflagration 10, Living Pyre 66.67 (the
    Ultimate's 400% over its six pulses, 400.02% in all), and nought for the
    other ten."""
    found = aura_rows()
    by_name = {row["SkillName"]: deals(row) for row in found.values()}
    assert by_name["Conflagration"] == 10.0
    assert by_name["Living Pyre"] == 66.67
    assert abs(6 * by_name["Living Pyre"] - 400.0) < 0.1
    for name in ("Exposing Light", "Miasma", "Undertow", "Pall of Ending",
                 "Unreason", "Gnawing Want"):
        assert by_name[name] == 0.0, name


def test_conflagrations_row_is_the_ruling() -> None:
    """#2869: a 4 m ring, 10% a second; its burn and its Shred of 15 unchanged.
    Its allies' 8% REPLACED on 2026-11-25 by the owner's "Go with A": a burning
    enemy dying inside passes its burn on within 4 m."""
    row = aura_rows()["Demonic_All_Aura"]
    params = dict(piece.strip().split("=", 1)
                  for piece in row["ShapeParams"].split(";") if piece.strip())
    assert params["Radius"] == "4"
    assert params["Interval"] == "1"
    assert params["Burn"] == "1"
    assert params["Effect"] == "Shred"
    assert params["EffectMagnitude"] == "15"
    assert params["BurnPassesOnDeath"] == "4"
    assert "AllyIncreasedDamage" not in params
    assert float(row["DamagePercent"]) == 10.0
    assert "4 meter radius" in row["SkillDescription"]
    assert "10% weapon damage" in row["SkillDescription"]


def test_the_aura_slots_band_holds_conflagrations_ten() -> None:
    """The owner's ruling on #2869 opens the Aura slot's 15-40% band to 10."""
    aura = next(row for row in table(SKILL_SLOTS) if row["Slot"] == "Aura")
    lowest = float(aura["DamageLowest"])
    highest = float(aura["DamageHighest"])
    assert lowest <= 10.0 <= highest, (
        f"the Aura slot's band is {lowest} to {highest}, which does not hold "
        "Conflagration's 10")
    assert lowest == 10.0, f"the band opens to 10, not to {lowest}"


def function_body(source: str, signature: str) -> str:
    """The text of one function, from its signature to the closing brace in the
    first column, with every `//` comment taken out so a comment naming the
    rule cannot stand in for it."""
    start = source.find(signature)
    assert start != -1, f"{SKILL_TEMPLATE_CPP.name} has no {signature}"
    end = source.find("\n}", start)
    assert end != -1, f"{signature} has no closing brace"
    body = source[start:end]
    return "\n".join(line.split("//", 1)[0] for line in body.splitlines())


AURA_STATES_NONE = re.compile(
    r"if\s*\(\s*Shape\(\)\s*==\s*ECataclysmSkillShape::Aura\s*\)\s*"
    r"\{\s*return\s+0\.0f\s*;\s*\}")


def test_the_game_gives_an_aura_stating_nothing_no_damage() -> None:
    """The rule in C++: after the row's own figure and before the slot's."""
    source = SKILL_TEMPLATE_CPP.read_bytes().decode("utf-8").replace("\r\n", "\n")
    body = function_body(
        source, "float UCataclysmSkillTemplate::GetDamagePercent() const")
    rule = AURA_STATES_NONE.search(body)
    assert rule, (
        "GetDamagePercent no longer answers nought for an aura stating no "
        "figure, so an aura whose sentence names no damage deals its slot's "
        "(#2875)")
    own = body.find("return DamagePercentOverride;")
    slot = body.find("UCataclysmSkillSlots::NumbersFor")
    assert own != -1 and slot != -1, "GetDamagePercent has changed shape"
    assert own < rule.start() < slot, (
        "the aura's nought has to come after the row's own figure and before "
        "the slot's")
