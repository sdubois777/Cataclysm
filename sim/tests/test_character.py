"""Tests for the character sheet and the stat pipeline."""

from __future__ import annotations

import pytest

from cataclysm_sim import character as ch


# --------------------------------------------------------------------------
# The sheet is complete and self-consistent
# --------------------------------------------------------------------------

def test_the_sheet_has_forty_five_stats():
    """33 until mana leech and energy shield leech were added for issue #214,
    then 35, then 43 when the eight increased-damage-against-a-type stats were
    added for issue #213, then 45 when damage over time damage and duration
    joined damage over time frequency for issue #205.

    The design document's Character Sheet section states the same count, and
    `test_the_design_document_agrees_on_the_stat_count` compares the two.
    """
    assert len(ch.ALL_STATS) == 45
    assert len(set(ch.ALL_STATS)) == 45, "a stat is listed in two groups"


def test_every_stat_has_a_default_and_every_default_is_a_stat():
    assert set(ch.DEFAULT_STAT_LINE) == set(ch.ALL_STATS)


def test_there_is_one_resistance_per_damage_type():
    assert len(ch.DAMAGE_TYPES) == 8
    assert len(ch.RESISTANCE_STATS) == 8
    assert set(ch.RESISTANCE_STATS) <= set(ch.ALL_STATS)


def test_the_completeness_check_actually_rejects_a_gap(monkeypatch):
    """The module runs `_check_stat_line_is_complete` at import. Prove it can
    fail, or it is decoration."""
    trimmed = dict(ch.DEFAULT_STAT_LINE)
    trimmed.pop("armor")
    monkeypatch.setattr(ch, "DEFAULT_STAT_LINE", trimmed)
    with pytest.raises(ValueError, match="does not match the character sheet"):
        ch._check_stat_line_is_complete()


def test_every_attribute_scales_at_least_one_stat_on_the_sheet():
    assert len(ch.ATTRIBUTE_EFFECTS) == 8
    for attribute, effects in ch.ATTRIBUTE_EFFECTS.items():
        assert effects, f"{attribute} scales nothing"
        for stat in effects:
            assert stat in ch.ALL_STATS, (
                f"{attribute} scales {stat}, which is not on the sheet")


# --------------------------------------------------------------------------
# Where a base value comes from
# --------------------------------------------------------------------------

def test_class_supplied_stats_take_their_base_from_the_class_and_level():
    c = ch.Character(ch.GENERIC, level=10)
    expected = ch.DEFAULT_STAT_LINE["max_health"].at(10)
    assert c.base("max_health") == expected


def test_every_stat_has_exactly_one_recorded_base_source():
    assert set(ch.BASE_SOURCE) == set(ch.ALL_STATS)
    assert set(ch.BASE_SOURCE.values()) <= {"class", "weapon", "skill"}
    groups = (ch.CLASS_BASE_STATS, ch.WEAPON_BASE_STATS, ch.SKILL_BASE_STATS)
    assert sum(len(g) for g in groups) == len(ch.ALL_STATS)
    for a, b in ((0, 1), (0, 2), (1, 2)):
        assert not (groups[a] & groups[b]), "a stat is in two source groups"


def test_the_base_source_check_rejects_a_missing_or_unknown_source(monkeypatch):
    monkeypatch.setattr(ch, "BASE_SOURCE", {"max_health": "class"})
    with pytest.raises(ValueError, match="no recorded base source"):
        ch._check_every_stat_has_a_base_source()
    monkeypatch.setattr(ch, "BASE_SOURCE",
                        dict.fromkeys(ch.ALL_STATS, "somewhere_else"))
    with pytest.raises(ValueError, match="unknown base sources"):
        ch._check_every_stat_has_a_base_source()


def test_weapon_supplied_stats_ignore_the_class_and_take_the_weapon_base():
    """Attack speed's base belongs to the weapon. A class contributes only an
    increase, so that a dagger stays faster than a two-handed axe."""
    assert ch.BASE_SOURCE["attack_speed"] == "weapon"
    unarmed = ch.Character(ch.GENERIC, level=100)
    armed = ch.Character(ch.GENERIC, level=100,
                         gear=ch.Gear(weapon_base={"attack_speed": 1.4}))
    assert unarmed.stat("attack_speed") == 0.0
    assert armed.stat("attack_speed") == pytest.approx(1.4)


# --------------------------------------------------------------------------
# Skill-supplied bases
# --------------------------------------------------------------------------

def test_critical_strike_chance_comes_from_the_skill_not_the_class():
    """Stated by the project owner: each skill carries its own base critical
    strike chance, and gear and attributes scale that."""
    assert ch.BASE_SOURCE["crit_chance"] == "skill"

    # Every skill supplies the default base unless it names its own, so a
    # character always has something for its increases to scale. Before issue
    # #120 this was zero, and every increased critical strike chance affix in
    # the game was worth nothing.
    default_skill = ch.Character(ch.GENERIC, level=100,
                                 attributes=ch.Attributes(ferocity=100))
    # 100 Ferocity is +50% increased, so the 5% default becomes 7.5%.
    assert default_skill.stat("crit_chance") == pytest.approx(
        ch.DEFAULT_SKILL_CRIT_CHANCE * 1.5)


def test_the_default_critical_strike_chance_is_a_default_not_a_floor():
    """A skill that names its own value gets that value, not the larger of the
    two. Otherwise no skill could ever be designed to crit less than average."""
    timid = ch.Character(ch.GENERIC, level=100,
                         skill=ch.Skill(name="Timid", base={"crit_chance": 1.0}))
    assert timid.stat("crit_chance") == pytest.approx(1.0)
    assert ch.DEFAULT_SKILL_CRIT_CHANCE > 1.0


def test_a_skill_base_is_scaled_by_attributes_and_gear():
    c = ch.Character(ch.GENERIC, level=100,
                     attributes=ch.Attributes(ferocity=100),
                     skill=ch.Skill(name="Cleave", base={"crit_chance": 20.0}))
    # 100 Ferocity is +50% increased, so 20% becomes 30%.
    assert c.stat("crit_chance") == pytest.approx(30.0)


def test_a_class_cannot_give_itself_a_skill_supplied_base():
    """An override for a skill-based stat is ignored, because the class is not
    where that base lives. This is what keeps the sources from overlapping."""
    pretender = ch.ClassDefinition(name="Pretender",
                                   overrides={"crit_chance": ch.Scaling(base=90.0)})
    c = ch.Character(pretender, level=100)
    # The skill's default, not the class's 90. Asserting the number rather than
    # merely "not 90" so a change to where the base comes from is caught here.
    assert c.stat("crit_chance") == pytest.approx(ch.DEFAULT_SKILL_CRIT_CHANCE)


def test_a_skill_may_not_supply_a_base_for_a_stat_it_does_not_own():
    with pytest.raises(ValueError, match="whose base does not come from"):
        ch.Skill(name="Wrong", base={"max_health": 500.0})


def test_area_of_effect_and_dot_frequency_belong_to_the_class():
    """The character holds one area of effect percentage that applies to every
    skill tagged for it. They are not per-skill numbers."""
    assert ch.BASE_SOURCE["area_of_effect"] == "class"
    assert ch.BASE_SOURCE["dot_frequency"] == "class"
    with pytest.raises(ValueError, match="whose base does not come from"):
        ch.Skill(name="Wrong", base={"area_of_effect": 5.0})


def test_area_of_effect_baselines_at_one_hundred_percent_not_zero():
    """It is a percentage of whatever the skill does, so 100% is 'unchanged'.
    A baseline of zero would leave Efficacy with nothing to scale."""
    c = ch.Character(ch.GENERIC, level=100)
    assert c.stat("area_of_effect") == pytest.approx(100.0)
    assert c.stat("dot_frequency") == pytest.approx(100.0)
    boosted = ch.Character(ch.GENERIC, level=100,
                           attributes=ch.Attributes(efficacy=50))
    assert boosted.stat("area_of_effect") == pytest.approx(200.0)


def test_loot_quantity_baselines_at_one_hundred_percent_not_zero():
    """Issue #243. It is a percentage of whatever the dungeon would otherwise
    drop, so 100% is 'unchanged'.

    A baseline of zero left it permanently at zero, because EVERY source of loot
    quantity is an increase and not one of them is flat: the Luck attribute, the
    Increased Loot Quantity affix, its hybrid with magic find, and several
    Explorer branch nodes on the empire tree.
    """
    c = ch.Character(ch.GENERIC, level=100)
    assert c.stat("loot_quantity") == pytest.approx(100.0)


def test_every_source_of_loot_quantity_actually_moves_it():
    """The failure this guards is silent: nothing errored, the number was just
    always zero. Each source is checked on its own so a later change that
    breaks one of them is named."""
    plain = ch.Character(ch.GENERIC, level=100).stat("loot_quantity")

    from_luck = ch.Character(ch.GENERIC, level=100,
                             attributes=ch.Attributes(luck=100))
    assert from_luck.stat("loot_quantity") > plain

    from_gear = ch.Character(
        ch.GENERIC, level=100,
        gear=ch.Gear(increased={"loot_quantity": 0.32}))
    assert from_gear.stat("loot_quantity") == pytest.approx(132.0)

    together = ch.Character(
        ch.GENERIC, level=100, attributes=ch.Attributes(luck=100),
        gear=ch.Gear(increased={"loot_quantity": 0.32}))
    assert together.stat("loot_quantity") == pytest.approx(232.0)


def test_magic_find_baselines_at_zero_because_it_has_a_flat_source():
    """The other half of the same attribute, and deliberately NOT the same
    shape. Magic find is an added percentage rather than a percentage of
    something, and the Flat Magic Find affix supplies the base that Luck then
    scales. Issue #81 is about how much Luck's share of it is worth, which is a
    separate question from whether it works at all."""
    assert ch.Character(ch.GENERIC, level=100).stat("magic_find") == \
        pytest.approx(0.0)
    geared = ch.Character(ch.GENERIC, level=100,
                          gear=ch.Gear(flat={"magic_find": 40.0}))
    assert geared.stat("magic_find") == pytest.approx(40.0)
    with_luck = ch.Character(ch.GENERIC, level=100,
                             attributes=ch.Attributes(luck=100),
                             gear=ch.Gear(flat={"magic_find": 40.0}))
    assert with_luck.stat("magic_find") > geared.stat("magic_find")


# --------------------------------------------------------------------------
# Increases are scoped by tag
# --------------------------------------------------------------------------

def test_tag_matching_is_hierarchical():
    tags = frozenset({"Type.AOE.PointBlank", "Item.Weapon.Dagger"})
    assert ch.tag_matches("Type.AOE", tags)
    assert ch.tag_matches("Type.AOE.PointBlank", tags)
    assert ch.tag_matches("Item.Weapon", tags)
    assert not ch.tag_matches("Type.AOE.Aura", tags)
    assert not ch.tag_matches("Type.Projectile", tags)
    # A partial name must not match a longer tag by accident.
    assert not ch.tag_matches("Type.A", tags)


def test_the_global_scope_tag_matches_anything():
    assert ch.GLOBAL_SCOPE_TAG == "Scope.Global"
    assert ch.tag_matches(ch.GLOBAL_SCOPE_TAG, frozenset())
    assert ch.tag_matches(ch.GLOBAL_SCOPE_TAG, frozenset({"Type.Strike"}))


def test_a_tag_scoped_increase_reaches_only_matching_skills():
    """The project owner's example: equipment granting increased area of effect
    applies to anything tagged for area, and to nothing else."""
    boots = ch.Gear(modifiers=(ch.Modifier("area_of_effect", 0.40,
                                           frozenset({"Type.AOE"})),))
    area_skill = ch.Skill(name="Smoke Bomb",
                          tags=frozenset({"Type.AOE.PointBlank"}))
    strike_skill = ch.Skill(name="Thrust", tags=frozenset({"Type.Strike"}))

    assert ch.Character(ch.GENERIC, level=100, gear=boots,
                        skill=area_skill).stat("area_of_effect") == pytest.approx(140.0)
    assert ch.Character(ch.GENERIC, level=100, gear=boots,
                        skill=strike_skill).stat("area_of_effect") == pytest.approx(100.0)


def test_an_unscoped_modifier_applies_to_every_skill():
    everywhere = ch.Gear(modifiers=(ch.Modifier("area_of_effect", 0.25),))
    for tags in (frozenset(), frozenset({"Type.Strike"}),
                 frozenset({"Type.AOE.Aura"})):
        c = ch.Character(ch.GENERIC, level=100, gear=everywhere,
                         skill=ch.Skill(tags=tags))
        assert c.stat("area_of_effect") == pytest.approx(125.0)


def test_a_modifier_requiring_several_tags_needs_all_of_them():
    picky = ch.Gear(modifiers=(ch.Modifier(
        "area_of_effect", 1.0,
        frozenset({"Type.AOE", "Item.Weapon.Dagger"})),))
    both = ch.Skill(tags=frozenset({"Type.AOE.Aura", "Item.Weapon.Dagger"}))
    one = ch.Skill(tags=frozenset({"Type.AOE.Aura", "Item.Weapon.Spear"}))
    assert ch.Character(ch.GENERIC, level=100, gear=picky,
                        skill=both).stat("area_of_effect") == pytest.approx(200.0)
    assert ch.Character(ch.GENERIC, level=100, gear=picky,
                        skill=one).stat("area_of_effect") == pytest.approx(100.0)


def test_attribute_increases_are_never_tag_scoped():
    """Attribute points apply to the character, not to a tagged subset."""
    for tags in (frozenset(), frozenset({"Type.Strike"})):
        c = ch.Character(ch.GENERIC, level=100,
                         attributes=ch.Attributes(efficacy=100),
                         skill=ch.Skill(tags=tags))
        assert c.stat("area_of_effect") == pytest.approx(300.0)


def test_a_modifier_naming_a_stat_off_the_sheet_is_rejected():
    with pytest.raises(ValueError, match="not on the character sheet"):
        ch.Modifier("thorns_aura", 0.5)


def test_the_tags_used_in_these_tests_exist_in_the_generated_tag_list():
    """Guards against inventing tags. The list is generated from the design
    workbook by tools/generate_gameplay_tags.py."""
    import pathlib
    ini = (pathlib.Path(__file__).resolve().parents[2]
           / "game" / "Config" / "Tags" / "CataclysmTags.ini").read_text(
               encoding="utf-8")
    for tag in ("Type.AOE.PointBlank", "Type.AOE.Aura", "Type.Strike",
                "Item.Weapon.Dagger", "Item.Weapon.Spear", "Scope.Global"):
        assert f'"{tag}"' in ini, f"{tag} is not a generated gameplay tag"


def test_a_skill_may_not_name_a_stat_that_is_not_on_the_sheet():
    with pytest.raises(ValueError, match="not on the character sheet"):
        ch.Skill(name="Wrong", base={"thorns_aura": 1.0})


def test_a_class_increase_scales_the_weapon_base_rather_than_replacing_it():
    fast = ch.ClassDefinition(name="Fast",
                              overrides={"attack_speed": ch.Scaling()})
    c = ch.Character(fast, level=100,
                     gear=ch.Gear(weapon_base={"attack_speed": 1.0},
                                  increased={"attack_speed": 0.30}))
    assert c.stat("attack_speed") == pytest.approx(1.30)


def test_gear_flat_is_added_before_scaling_for_weapon_stats_too():
    c = ch.Character(ch.GENERIC, level=1,
                     gear=ch.Gear(weapon_base={"attack_speed": 1.0},
                                  flat={"attack_speed": 0.5},
                                  increased={"attack_speed": 1.0}))
    assert c.stat("attack_speed") == pytest.approx(3.0)


# --------------------------------------------------------------------------
# The default line and overrides
# --------------------------------------------------------------------------

def test_a_class_with_no_overrides_is_exactly_the_default_line():
    for stat in ch.ALL_STATS:
        assert ch.GENERIC.scaling(stat) is ch.DEFAULT_STAT_LINE[stat]


def test_an_override_replaces_only_the_stat_it_names():
    tank = ch.ClassDefinition(name="Tank",
                              overrides={"armor": ch.Scaling(base=50, per_level=5)})
    assert tank.base_at("armor", 11) == pytest.approx(100.0)
    assert tank.scaling("max_health") is ch.DEFAULT_STAT_LINE["max_health"]


def test_a_class_may_override_any_stat_on_the_sheet():
    everything = ch.ClassDefinition(
        name="Everything",
        overrides={s: ch.Scaling(base=1.0) for s in ch.ALL_STATS})
    for stat in ch.ALL_STATS:
        assert everything.base_at(stat, 1) == 1.0


def test_overriding_a_stat_that_is_not_on_the_sheet_is_rejected():
    with pytest.raises(ValueError, match="not on the character sheet"):
        ch.ClassDefinition(name="Bad",
                           overrides={"thorns_aura": ch.Scaling(base=1)})


def test_gear_naming_a_stat_that_is_not_on_the_sheet_is_rejected():
    for kwargs in ({"flat": {"nonsense": 1.0}},
                   {"increased": {"nonsense": 1.0}},
                   {"weapon_base": {"nonsense": 1.0}}):
        with pytest.raises(ValueError, match="not on the character sheet"):
            ch.Gear(**kwargs)


def test_asking_for_a_stat_that_is_not_on_the_sheet_is_rejected():
    c = ch.Character(ch.GENERIC, level=1)
    with pytest.raises(KeyError):
        c.stat("thorns_aura")


# --------------------------------------------------------------------------
# The pipeline
# --------------------------------------------------------------------------

def test_attributes_multiply_the_base_rather_than_adding_to_it():
    base = ch.DEFAULT_STAT_LINE["max_health"].at(50)
    c = ch.Character(ch.GENERIC, level=50,
                     attributes=ch.Attributes(vitality=50))
    assert c.stat("max_health") == pytest.approx(base * 2.0)


def test_gear_flat_enters_before_the_multiplication():
    level, flat = 50, 1000.0
    base = ch.DEFAULT_STAT_LINE["max_health"].at(level)
    c = ch.Character(ch.GENERIC, level=level,
                     attributes=ch.Attributes(vitality=50),
                     gear=ch.Gear(flat={"max_health": flat}))
    assert c.stat("max_health") == pytest.approx((base + flat) * 2.0)
    assert c.stat("max_health") > base * 2.0 + flat


def test_gear_increases_and_attribute_points_share_one_bucket():
    base = ch.DEFAULT_STAT_LINE["max_health"].at(50)
    c = ch.Character(ch.GENERIC, level=50,
                     attributes=ch.Attributes(vitality=25),
                     gear=ch.Gear(increased={"max_health": 0.50}))
    assert c.stat("max_health") == pytest.approx(base * 2.0)


def test_per_level_scaling_is_linear():
    sc = ch.DEFAULT_STAT_LINE["max_health"]
    steps = [sc.at(lv) for lv in range(1, 101)]
    gaps = {round(b - a, 9) for a, b in zip(steps, steps[1:], strict=False)}
    assert gaps == {sc.per_level}


def test_an_attribute_scales_every_stat_it_is_listed_against():
    """Efficacy is listed against three stats. All three must respond."""
    c = ch.Character(ch.GENERIC, level=100,
                     attributes=ch.Attributes(efficacy=100))
    for stat in ch.ATTRIBUTE_EFFECTS["efficacy"]:
        assert c.increases(stat) > 0, f"efficacy did not reach {stat}"


def test_a_stat_with_a_zero_base_gains_nothing_from_its_attribute():
    """The design working, not failing. An attribute only ever scales, so with
    no base there is nothing to scale. The default line has no evasion, so
    Agility does nothing until a class gives evasion a base."""
    c = ch.Character(ch.GENERIC, level=100,
                     attributes=ch.Attributes(agility=100))
    assert c.increases("evasion") > 0
    assert c.stat("evasion") == 0.0
    nimble = ch.ClassDefinition(name="Nimble",
                                overrides={"evasion": ch.Scaling(base=10.0)})
    assert ch.Character(nimble, level=100,
                        attributes=ch.Attributes(agility=100)
                        ).stat("evasion") == pytest.approx(15.0)


def test_an_attribute_effect_is_only_ever_an_increase():
    """There is one way an attribute acts. A second kind was proposed and
    rejected by the project owner; this locks the single kind in."""
    for attribute, effects in ch.ATTRIBUTE_EFFECTS.items():
        for stat, value in effects.items():
            assert isinstance(value, float), (
                f"{attribute} -> {stat} is not a plain increase fraction")
    ch._check_attributes_only_scale()


def test_the_attributes_only_scale_check_rejects_anything_else(monkeypatch):
    class NotAnIncrease:
        pass

    monkeypatch.setattr(ch, "ATTRIBUTE_EFFECTS",
                        {"agility": {"movement_speed": NotAnIncrease()}})
    with pytest.raises(TypeError, match="plain increase fraction"):
        ch._check_attributes_only_scale()


def test_movement_speed_is_measured_in_metres_per_second():
    """The project owner's example: a tank around 3 metres per second, scaled by
    the attribute as 3 * (1 + increases)."""
    assert 1.0 <= ch.DEFAULT_STAT_LINE["movement_speed"].base <= 10.0
    tank = ch.ClassDefinition(name="Tank",
                              overrides={"movement_speed": ch.Scaling(base=3.0)})
    c = ch.Character(tank, level=50, attributes=ch.Attributes(agility=50))
    assert c.stat("movement_speed") == pytest.approx(3.0 * 2.0)


# --------------------------------------------------------------------------
# Caps
# --------------------------------------------------------------------------

def test_a_hard_cap_is_applied():
    """Critical strike chance's base comes from the skill, so the cap is tested
    against a skill that already has a high one."""
    assert ch.HARD_CAPS["crit_chance"] == 100.0
    c = ch.Character(ch.GENERIC, level=100,
                     gear=ch.Gear(increased={"crit_chance": 5.0}),
                     skill=ch.Skill(name="Sharp", base={"crit_chance": 80.0}))
    assert c.stat("crit_chance") == 100.0


def test_soft_caps_are_recorded_but_deliberately_not_applied():
    """Resistances and evasion are exceedable by design, so the model must not
    clamp them. Clamping would silently delete over-capping."""
    assert ch.SOFT_CAPS["evasion"] == 60.0
    assert all(ch.SOFT_CAPS[s] == 70.0 for s in ch.RESISTANCE_STATS)
    assert not (set(ch.SOFT_CAPS) & set(ch.HARD_CAPS))

    resistant = ch.ClassDefinition(
        name="Resistant",
        overrides={"resistance_demonic": ch.Scaling(base=50.0)})
    c = ch.Character(resistant, level=100,
                     gear=ch.Gear(increased={"resistance_demonic": 2.0}))
    assert c.stat("resistance_demonic") == pytest.approx(150.0)


# --------------------------------------------------------------------------
# Intervals divide, frequencies do not
# --------------------------------------------------------------------------

def test_cooldown_reduction_divides_and_matches_the_worked_example():
    """The design document's example: a character shown at 25% reduction turns a
    4 second skill into a 3 second one."""
    c = ch.Character(ch.GENERIC, level=100,
                     gear=ch.Gear(increased={"cooldown_reduction": 1.0 / 3.0}))
    assert c.displayed_cooldown_reduction() == pytest.approx(25.0)
    assert c.cooldown_of(4.0) == pytest.approx(3.0)


def test_one_hundred_efficacy_halves_every_cooldown():
    c = ch.Character(ch.GENERIC, level=100,
                     attributes=ch.Attributes(efficacy=100))
    assert c.cooldown_of(4.0) == pytest.approx(2.0)
    assert c.displayed_cooldown_reduction() == pytest.approx(50.0)


def test_cooldown_can_never_reach_zero():
    for increased in (1.0, 10.0, 100.0, 10_000.0):
        c = ch.Character(ch.GENERIC, level=100,
                         gear=ch.Gear(increased={"cooldown_reduction": increased}))
        assert c.cooldown_of(4.0) > 0.0
        assert c.displayed_cooldown_reduction() < 100.0


def test_damage_over_time_frequency_rises_with_efficacy():
    """Frequency is ticks per second, so more Efficacy must mean MORE ticks. If
    frequency were treated as an interval and divided, this would fall.

    The character holds one percentage, baselined at 100, which applies to the
    skills it uses. It is not a per-skill number."""
    none = ch.Character(ch.GENERIC, level=100)
    lots = ch.Character(ch.GENERIC, level=100,
                        attributes=ch.Attributes(efficacy=100))
    assert lots.stat("dot_frequency") > none.stat("dot_frequency")
    assert none.stat("dot_frequency") == pytest.approx(100.0)
    assert lots.stat("dot_frequency") == pytest.approx(200.0)


def test_only_cooldown_reduction_divides():
    assert ch.RATE_STATS == frozenset({"cooldown_reduction"})


# --------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------

@pytest.mark.parametrize("level", [0, 101, -1])
def test_level_outside_one_to_one_hundred_is_rejected(level):
    with pytest.raises(ValueError):
        ch.Character(ch.GENERIC, level=level)


@pytest.mark.parametrize("level", [0, 101])
def test_scaling_rejects_levels_outside_the_range(level):
    with pytest.raises(ValueError):
        ch.Scaling(base=1.0).at(level)


def test_spending_more_attribute_points_than_levels_is_rejected():
    with pytest.raises(ValueError, match="one point per level"):
        ch.Character(ch.GENERIC, level=10,
                     attributes=ch.Attributes(vitality=50))


def test_points_spread_across_all_eight_attributes_still_counts():
    with pytest.raises(ValueError, match="one point per level"):
        ch.Character(ch.GENERIC, level=7,
                     attributes=ch.Attributes(agility=1, ferocity=1,
                                              constitution=1, vitality=1,
                                              mind=1, spirit=1, efficacy=1,
                                              luck=1))


def test_spending_exactly_the_level_in_points_is_allowed():
    c = ch.Character(ch.GENERIC, level=8,
                     attributes=ch.Attributes(agility=1, ferocity=1,
                                              constitution=1, vitality=1,
                                              mind=1, spirit=1, efficacy=1,
                                              luck=1))
    assert c.attributes.total() == 8


# --------------------------------------------------------------------------
# What this module deliberately does not contain
# --------------------------------------------------------------------------

def test_no_per_class_definitions_are_shipped_here():
    """Ravager, Ritualist and Masochist are issue #77. Their values need
    reviewing on their own terms, not arriving with the structure."""
    defined = [v for v in vars(ch).values()
               if isinstance(v, ch.ClassDefinition)]
    assert [d.name for d in defined] == ["Generic"]
    assert ch.GENERIC.overrides == {}


def test_spends_health_does_not_mean_the_class_has_no_mana():
    """The Masochist pays for abilities with health, but keeps a mana pool until
    a passive tree node converts it. The flag must not zero the pool."""
    masochist_like = ch.ClassDefinition(name="Health Spender", spends_health=True)
    c = ch.Character(masochist_like, level=100,
                     attributes=ch.Attributes(mind=100))
    assert c.stat("max_mana") > 0


def test_the_full_sheet_can_be_produced_for_any_character():
    sheet = ch.Character(ch.GENERIC, level=50).sheet()
    assert set(sheet) == set(ch.ALL_STATS)
    assert len(sheet) == 45


# --------------------------------------------------------------------------
# The more bucket, which is multiplicative
# --------------------------------------------------------------------------

def probe(**kwargs) -> ch.Character:
    """A level 100 character with a 1,000 point health base and nothing else,
    so every figure below reads directly against that."""
    line = dict(ch.DEFAULT_STAT_LINE)
    line["max_health"] = ch.Scaling(base=1000.0, per_level=0.0)
    return ch.Character(ch.ClassDefinition(name="Probe", overrides=line),
                        level=100, **kwargs)


def test_a_more_multiplier_multiplies_where_an_increase_adds():
    """The whole distinction. The same 50%, in a different bucket, gives a
    different result once anything else is present."""
    assert probe().stat("max_health") == pytest.approx(1000.0)

    # With nothing else present the two are identical, which is why the
    # difference is invisible on a bare character.
    increased = probe(gear=ch.Gear(increased={"max_health": 0.50}))
    more = probe(more=(ch.More("gem", "max_health", 0.50),))
    assert increased.stat("max_health") == pytest.approx(1500.0)
    assert more.stat("max_health") == pytest.approx(1500.0)

    # Put 200% of increases in place first and they part company.
    all_increased = probe(gear=ch.Gear(increased={"max_health": 2.50}))
    increase_then_more = probe(
        gear=ch.Gear(increased={"max_health": 2.00}),
        more=(ch.More("gem", "max_health", 0.50),))
    assert all_increased.stat("max_health") == pytest.approx(3500.0)
    assert increase_then_more.stat("max_health") == pytest.approx(4500.0)


def test_two_more_multipliers_multiply_with_each_other():
    """Not summed. Two independent 50% sources give 2.25x rather than 2.0x, and
    that compounding is why a player chases them."""
    c = probe(more=(ch.More("gem", "max_health", 0.50),
                    ch.More("keystone", "max_health", 0.50)))
    assert c.more_multiplier("max_health") == pytest.approx(2.25)
    assert c.stat("max_health") == pytest.approx(2250.0)


def test_increases_have_diminishing_returns_and_more_multipliers_do_not():
    """The reason gearing is a puzzle rather than a sum, measured rather than
    asserted. Each further point of increase is worth less than the last; each
    further more multiplier is worth the same proportion as the first."""
    def gain_from_another_increase(already_held: float) -> float:
        before = probe(gear=ch.Gear(increased={"max_health": already_held}))
        after = probe(gear=ch.Gear(increased={"max_health": already_held + 0.60}))
        return after.stat("max_health") / before.stat("max_health")

    def gain_from_another_more(already_held: int) -> float:
        pool = tuple(ch.More("gem", "max_health", 0.60)
                     for _ in range(already_held))
        gear = ch.Gear(increased={"max_health": 8.0})
        before = probe(gear=gear, more=pool)
        after = probe(gear=gear,
                      more=pool + (ch.More("gem", "max_health", 0.60),))
        return after.stat("max_health") / before.stat("max_health")

    # 60% more increase, on a character who has none, is worth 60%.
    assert gain_from_another_increase(0.0) == pytest.approx(1.60)
    # The same 60% on a character already at +800% is worth 6.7%.
    assert gain_from_another_increase(8.0) == pytest.approx(1.0667, abs=0.001)

    # A more multiplier is worth its full 60% at every point.
    for already_held in range(0, 5):
        assert gain_from_another_more(already_held) == pytest.approx(1.60)


def test_a_more_multiplier_is_scoped_by_tag_the_same_way_an_increase_is():
    """A gem granting more area damage should not help a single-target skill."""
    boost = (ch.More("gem", "crit_chance", 1.00,
                     requires=frozenset({"Type.AOE"})),)
    area = ch.Skill(name="Blast", base={"crit_chance": 10.0},
                    tags=frozenset({"Type.AOE.PointBlank"}))
    single = ch.Skill(name="Stab", base={"crit_chance": 10.0},
                      tags=frozenset({"Type.Melee"}))
    assert probe(more=boost, skill=area).stat("crit_chance") == pytest.approx(20.0)
    assert probe(more=boost, skill=single).stat("crit_chance") == pytest.approx(10.0)


def test_an_unscoped_more_multiplier_reaches_every_skill():
    boost = (ch.More("enchantment", "crit_chance", 0.50),)
    for tags in (frozenset(), frozenset({"Type.Melee"}), frozenset({"Type.AOE"})):
        c = probe(more=boost,
                  skill=ch.Skill(name="Any", base={"crit_chance": 10.0},
                                 tags=tags))
        assert c.stat("crit_chance") == pytest.approx(15.0)


def test_a_more_multiplier_on_a_stat_with_no_base_still_creates_nothing():
    """The rule that a modifier only ever scales applies to this bucket too. A
    class with no energy shield gains none from a gem that multiplies it."""
    c = probe(more=(ch.More("gem", "max_energy_shield", 5.00),))
    assert c.base("max_energy_shield") == 0.0
    assert c.stat("max_energy_shield") == 0.0


def test_ordinary_gear_affixes_cannot_grant_a_more_multiplier():
    """The project owner put the multiplicative sources on gems, keystones and
    enchantments, so that a rare drop stays readable."""
    assert ch.MORE_SOURCES == {"gem", "keystone", "enchantment"}
    with pytest.raises(ValueError, match="expected one of"):
        ch.More("affix", "max_health", 0.5)


def test_a_more_multiplier_naming_a_stat_that_does_not_exist_is_rejected():
    with pytest.raises(ValueError, match="not on the character sheet"):
        ch.More("gem", "max_stamina", 0.5)


def test_a_less_multiplier_is_legal_but_cannot_reach_minus_one_hundred_percent():
    """Otherwise a single source could zero a stat outright, or invert it."""
    assert probe(more=(ch.More("keystone", "max_health", -0.30),)
                 ).stat("max_health") == pytest.approx(700.0)
    for impossible in (-1.0, -2.0):
        with pytest.raises(ValueError, match="zero or invert"):
            ch.More("keystone", "max_health", impossible)


def test_a_more_multiplier_shortens_a_cooldown_rather_than_lengthening_it():
    """Cooldown reduction is a rate stat: increases divide rather than multiply,
    so a more multiplier has to divide for the same reason. Multiplying would
    make a cooldown reduction gem lengthen the cooldown."""
    plain = probe()
    reduced = probe(more=(ch.More("gem", "cooldown_reduction", 1.00),))
    assert reduced.cooldown_of(8.0) < plain.cooldown_of(8.0)
    assert reduced.cooldown_of(8.0) == pytest.approx(4.0)


def test_a_more_multiplier_shortens_a_rate_stat_read_off_the_sheet_too():
    """`Character.stat` has its own branch for rate stats, separate from
    `cooldown_of`, and a more multiplier has to divide in both.

    Reached only by a class that gives cooldown reduction a nonzero base. The
    default line gives it zero, so `stat` short-circuits and the branch never
    runs -- which is why breaking it caught nothing until this test existed.
    """
    line = dict(ch.DEFAULT_STAT_LINE)
    line["cooldown_reduction"] = ch.Scaling(base=8.0, per_level=0.0)
    hasty = ch.ClassDefinition(name="Hasty", overrides=line)

    plain = ch.Character(hasty, level=100)
    with_more = ch.Character(hasty, level=100,
                             more=(ch.More("gem", "cooldown_reduction", 1.00),))
    assert plain.stat("cooldown_reduction") == pytest.approx(8.0)
    assert with_more.stat("cooldown_reduction") == pytest.approx(4.0)
    assert with_more.stat("cooldown_reduction") < plain.stat("cooldown_reduction")


def test_a_rate_stat_divides_by_both_buckets_together():
    """Not by one and then multiplied by the other, which would make a more
    multiplier lengthen the interval instead of shortening it."""
    line = dict(ch.DEFAULT_STAT_LINE)
    line["cooldown_reduction"] = ch.Scaling(base=12.0, per_level=0.0)
    hasty = ch.ClassDefinition(name="Hasty", overrides=line)
    c = ch.Character(hasty, level=100,
                     gear=ch.Gear(increased={"cooldown_reduction": 0.50}),
                     more=(ch.More("gem", "cooldown_reduction", 0.60),))
    assert c.stat("cooldown_reduction") == pytest.approx(12.0 / (1.5 * 1.6))


def test_no_number_of_more_multipliers_brings_a_cooldown_to_zero():
    """The design says the cooldown formula cannot reach zero, which is why the
    stat needs no cap. A second bucket must not break that."""
    heavy = probe(more=tuple(ch.More("gem", "cooldown_reduction", 4.00)
                             for _ in range(20)))
    assert heavy.cooldown_of(8.0) > 0.0
    assert heavy.displayed_cooldown_reduction() < 100.0


def test_hard_caps_still_apply_after_the_more_bucket():
    """Critical strike chance caps at 100%. A more multiplier must not slip past
    it by being applied after the cap."""
    c = probe(more=(ch.More("gem", "crit_chance", 20.0),),
              skill=ch.Skill(name="Probe", base={"crit_chance": 30.0}))
    assert c.stat("crit_chance") == pytest.approx(100.0)


def test_the_individual_sources_reaching_a_stat_can_be_listed():
    """So a report can show which multipliers a build has rather than only their
    product, which is the number a player wants to see."""
    c = probe(more=(ch.More("gem", "max_health", 0.20),
                    ch.More("keystone", "max_health", 0.30),
                    ch.More("gem", "crit_chance", 0.50)))
    reaching = c.more_sources_for("max_health")
    assert len(reaching) == 2
    assert {m.source for m in reaching} == {"gem", "keystone"}


def test_the_base_source_comment_agrees_with_the_table_it_introduces():
    """Issue #265. The comment above `_NON_CLASS_BASE` used to say that area of
    effect and damage over time frequency were "placed with the skill". They come
    from the class, which the table it introduces, the design document's "Where
    Each Stat's Base Comes From" section, and
    `test_area_of_effect_and_dot_frequency_belong_to_the_class` in this file all
    agree on. The comment was the only copy that disagreed, and it disagreed with
    the four lines directly beneath it.

    A comment cannot be checked by running it, so this reads the source text.
    """
    import pathlib

    source = pathlib.Path(ch.__file__).read_text(encoding="utf-8")
    start = source.index("#: Where each stat's base value comes from")
    end = source.index("_NON_CLASS_BASE: dict[str, str] = {")
    comment = " ".join(source[start:end].split())

    off_class = {s for s, src in ch.BASE_SOURCE.items() if src != "class"}
    assert off_class == {"attack_speed", "crit_chance"}, (
        f"the stats that do not come from the class are now {sorted(off_class)}. "
        "The comment above _NON_CLASS_BASE describes that set by name and needs "
        "rewriting to match.")

    assert "placed with the skill" not in comment, (
        "the comment says area of effect and damage over time are placed with "
        "the skill. They come from the class. Issue #265.")
    assert "COME FROM THE CLASS" in comment, (
        "the comment no longer states which side area of effect and the damage "
        "over time stats fall on, which is the ambiguity issue #265 was filed "
        "about.")
