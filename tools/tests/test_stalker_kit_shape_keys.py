"""The four shape keys the Corrupted Stalker's weapon kits need. Issue #2580.

The owner ruled on 2026-11-17 that the mechanisms are built before the 36 rows
are authored, since they touch no workbook cell: [N1] `Ailment=`, [N3]
`DamageRollMin` and `DamageRollMax`, [N4] `EffectPick=Random` and [N5]
`Effect=RandomBuff` on a self buff. No row of `game/Data/WeaponSkills.csv`
states any of them yet, so every row here is written by hand.

EACH KEY IS SHOWN BOTH WAYS. The generator accepts it with a valid value, and
refuses each value or combination that would make a row run and quietly do
something else -- which is the failure `parse_shape_params` exists to prevent.

THE LAST GROUP READS THE C++, because two lists of one vocabulary are the drift
this project has had five times (#588, #590, #621, #622, and the
`test_the_cpp_reads_every_shape_parameter.py` story). The parser's list of keys
read as text, the name the self buff compares `Effect=` with, and the ailment
names `UCataclysmAilments::KindNamedInASkillRow` matches are each held to the
generator's here.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ABILITIES = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

WHERE = "Weapon Skills row 9 (Chaos Whip Heavy)"


def parse(text: str, shape: str = "Strike") -> dict[str, str]:
    return gen.parse_shape_params(text, shape, WHERE)


def refused(text: str, shape: str = "Strike") -> str:
    with pytest.raises(gen.DataError) as raised:
        parse(text, shape)
    return str(raised.value)


# --------------------------------------------------------------------------
# [N1] Ailment=


@pytest.mark.parametrize("ailment", ["Poison", "Disease", "Necrosis",
                                     "VoidSplinter"])
def test_each_of_the_four_ailments_the_owner_named_is_accepted(ailment):
    assert parse(f"Radius=2.5; Angle=60; Ailment={ailment}") == {
        "Radius": "2.5", "Angle": "60", "Ailment": ailment}


@pytest.mark.parametrize("shape, text", [
    ("Projectile", "Range=12; Radius=3; Speed=1400; Ailment=Poison"),
    ("Movement", "Mode=Charge; Range=12; Radius=2; Ailment=Poison"),
    ("Aura", "Radius=10; Interval=1; Ailment=Poison"),
    ("SelfBuff", "Duration=8; Interval=1; Radius=3; Ailment=Necrosis"),
])
def test_an_ailment_is_a_rider_every_shape_that_strikes_may_carry(shape, text):
    assert parse(text, shape)["Ailment"] in gen.SKILL_ROW_AILMENTS


def test_the_owners_four_are_exactly_the_closed_set():
    """The ruling's own list, and no more. THE CONTROL for the refusals
    below: if this set held Burn, the first of them would pass."""
    assert gen.SKILL_ROW_AILMENTS == {"Poison", "Disease", "Necrosis",
                                      "VoidSplinter"}


@pytest.mark.parametrize("value", ["Burn", "Bleed", "Void Splinter",
                                   "necrosis", "Shred", "Madness"])
def test_an_ailment_outside_the_set_is_refused(value):
    said = refused(f"Radius=2.5; Angle=60; Ailment={value}")
    assert f"Ailment={value!r}" in said


def test_two_ailments_in_one_cell_are_refused():
    assert "names more than one" in refused(
        "Radius=2.5; Angle=60; Ailment=Poison, Necrosis")


@pytest.mark.parametrize("shape, text", [
    ("Summon", "Count=1; MaxActive=3; Duration=20; Minions=Imp:1; "
               "Ailment=Poison"),
    ("Deployable", "Count=1; MaxActive=1; Duration=20; Minions=Ballista:1; "
                   "Ailment=Poison"),
])
def test_an_ailment_on_a_shape_whose_blows_are_not_its_own_is_refused(
        shape, text):
    assert "would reach nothing" in refused(text, shape)


# --------------------------------------------------------------------------
# [N3] DamageRollMin, DamageRollMax


def test_a_roll_with_both_ends_in_order_is_accepted():
    params = parse("Radius=5; Angle=45; DamageRollMin=50; DamageRollMax=450")
    assert (params["DamageRollMin"], params["DamageRollMax"]) == ("50", "450")
    # A ROLL THAT CANNOT MOVE IS STILL A ROLL, and is allowed.
    assert parse("Radius=5; DamageRollMin=250; DamageRollMax=250")


@pytest.mark.parametrize("text, said", [
    ("Radius=5; DamageRollMin=50", "without the other end"),
    ("Radius=5; DamageRollMax=450", "without the other end"),
    ("Radius=5; DamageRollMin=450; DamageRollMax=50", "not below"),
    ("Radius=5; DamageRollMin=0; DamageRollMax=0", "above nought"),
    ("Radius=5; DamageRollMin=-50; DamageRollMax=450", "below nought"),
    ("Radius=5; DamageRollMin=fifty; DamageRollMax=450", "not a number"),
])
def test_a_roll_that_could_not_be_drawn_is_refused(text, said):
    assert said in refused(text)


def test_a_rolled_row_with_a_damage_percent_is_refused():
    params = parse("Radius=5; DamageRollMin=50; DamageRollMax=450")
    with pytest.raises(gen.DataError, match="leave the Damage Percent"):
        gen.check_a_roll_states_no_damage_percent(params, "250", WHERE)
    # THE CONTROLS: the same roll with the cell blank, and a figure with no
    # roll, are both passed.
    gen.check_a_roll_states_no_damage_percent(params, "", WHERE)
    gen.check_a_roll_states_no_damage_percent(parse("Radius=5"), "250", WHERE)


# --------------------------------------------------------------------------
# [N4] EffectPick=Random


def test_a_pick_among_named_effects_is_accepted():
    params = parse("Radius=10; Interval=1; Effect=Shred, Weaken, "
                   "Withered Touch; EffectPick=Random", "Aura")
    assert params["EffectPick"] == "Random"


@pytest.mark.parametrize("text, said", [
    ("Radius=10; Interval=1; Effect=Shred; EffectPick=Random",
     "names 1 effect"),
    ("Radius=10; Interval=1; EffectPick=Random", "names 0 effect"),
    ("Radius=10; Interval=1; Effect=Shred, Stun; StunSeconds=1; "
     "EffectPick=Random", "a stun is never in the draw"),
    ("Radius=10; Interval=1; Effect=Shred, Weaken; EffectPick=First",
     "EffectPick='First'"),
    ("Radius=10; Interval=1; Effect=Shred, Weaken; EffectPick=Random, Random",
     "names more than one"),
])
def test_a_pick_that_could_not_draw_is_refused(text, said):
    assert said in refused(text, "Aura")


def test_a_pick_among_damage_over_time_is_refused_where_the_sheet_is_read():
    """Only `validate_skill_effects` holds the Status Effects sheet, so it is
    what says Poison is damage over time. THE CONTROL is the same row picking
    among two debuffs, which nothing refuses."""
    effects = [{"EffectName": "Shred", "EffectKind": "Debuff"},
               {"EffectName": "Weaken", "EffectKind": "Debuff"},
               {"EffectName": "Poison", "EffectKind": "DoT"}]

    def tables(effect_cell):
        return {"StatusEffects": effects, "WeaponSkills": [{
            "Name": "Chaos_All_Aura", "Shape": "Aura",
            "ShapeParams": f"Radius=10; Interval=1; Effect={effect_cell}; "
                           f"EffectPick=Random"}]}

    assert gen.validate_skill_effects(tables("Shred, Weaken")) == []
    problems = gen.validate_skill_effects(tables("Shred, Poison"))
    assert len(problems) == 1 and "'Poison' is damage over time" in problems[0]


# --------------------------------------------------------------------------
# [N5] Effect=RandomBuff


def test_a_self_buff_may_draw_a_random_buff():
    assert parse("Duration=8; Effect=RandomBuff", "SelfBuff") == {
        "Duration": "8", "Effect": "RandomBuff"}


def test_a_random_buff_names_no_row_and_is_passed_by_name():
    """It is not in the Buffs sheet, and the effect check passes it by name.
    THE CONTROL is a misspelling of it, which is refused as any unknown
    effect is."""
    effects = [{"EffectName": "Shred", "EffectKind": "Debuff"}]

    def tables(effect_cell):
        return {"StatusEffects": effects, "WeaponSkills": [{
            "Name": "Chaos_Whip_Support", "Shape": "SelfBuff",
            "ShapeParams": f"Duration=8; Effect={effect_cell}"}]}

    assert gen.validate_skill_effects(tables("RandomBuff")) == []
    assert len(gen.validate_skill_effects(tables("RandomBuf"))) == 1


@pytest.mark.parametrize("shape, text, said", [
    ("Strike", "Radius=4; Angle=360; Effect=RandomBuff", "Only a SelfBuff"),
    ("Aura", "Radius=10; Interval=1; Effect=RandomBuff", "Only a SelfBuff"),
    ("SelfBuff", "Duration=8; Effect=RandomBuff, Commander", "named alone"),
])
def test_a_random_buff_anywhere_else_is_refused(shape, text, said):
    assert said in refused(text, shape)


# --------------------------------------------------------------------------
# The C++ holds the same names


def cpp(name: str) -> str:
    path = ABILITIES / name
    if not path.is_file():
        pytest.skip(f"{path} is not present")
    return path.read_text(encoding="utf-8")


def test_every_text_parameter_is_read_as_text_by_the_cpp():
    """`ParseParams` reads every key it does not list in `TextKeys` as a
    number, and a word read as a number is nought, which marks the WHOLE cell
    invalid: issue #622, where every summon lost its Count. So the list must
    hold every key the generator accepts as text. Nothing compared the two
    before 2026-11-17; they agreed by care alone."""
    source = cpp("CataclysmSkillShape.cpp")
    block = re.search(r"TextKeys\[\]\s*=\s*\{(.*?)\};", source, re.DOTALL)
    assert block, "the TextKeys list was not found in CataclysmSkillShape.cpp"
    body = "\n".join(line.split("//")[0] for line in block.group(1).splitlines())
    read_as_text = set(re.findall(r'TEXT\("([A-Za-z]+)"\)', body))
    # THE CONTROL: a list that read nothing would agree with an empty set.
    assert {"Mode", "Effect", "Minions"} <= read_as_text
    assert read_as_text == set(gen.TEXT_PARAMS), (
        f"read as text by the C++ only: {sorted(read_as_text - gen.TEXT_PARAMS)}; "
        f"by the generator only: {sorted(gen.TEXT_PARAMS - read_as_text)}")


def test_the_self_buff_compares_effect_with_the_generators_name():
    source = cpp("CataclysmSkillTemplate.cpp")
    found = re.findall(
        r'UCataclysmSkillTemplate::RandomBuffEffect\s*=\s*TEXT\("([^"]+)"\)',
        source)
    assert found == [gen.RANDOM_BUFF_EFFECT]


def test_every_ailment_a_row_may_name_is_one_the_engine_matches():
    """`KindNamedInASkillRow` compares with every space taken out, so each
    name the generator allows must be an ailment of `EveryKind` with its
    spaces taken out. THE CONTROL is that the list read holds all eleven."""
    source = cpp("CataclysmAilments.cpp")
    block = re.search(r"EveryKind\[\]\s*=\s*\{(.*?)\n\t\};", source, re.DOTALL)
    assert block, "EveryKind was not found in CataclysmAilments.cpp"
    kinds = re.findall(r'\{TEXT\("([^"]+)"\),\s*TEXT\("\w+_chance"\)',
                       block.group(1))
    assert len(kinds) == 11, kinds
    spaceless = {kind.replace(" ", "") for kind in kinds}
    assert gen.SKILL_ROW_AILMENTS <= spaceless, (
        sorted(gen.SKILL_ROW_AILMENTS - spaceless))
