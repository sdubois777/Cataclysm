"""The all-resistances family does not roll on a difficulty tier 1 drop. Issue #2853.

Ruled by the project owner on 2026-11-24, the chosen option quoted: "No all-resist at tier 1 (Recommended)": the
all-resistances affix family cannot roll on a difficulty tier 1 drop; single resistances still can.
`affixes.ALL_RESISTANCES_LOWEST_DROP_TIER` is the tier, `affixes.rolls_on_a_drop_at` the rule, and `loot.roll_item`
asks it of the candidates after building them. The game's port is checked by
`tools/tests/test_all_resistances_drop_gate_port.py`.
"""

from __future__ import annotations

import random

import pytest

from cataclysm_sim import affixes as af
from cataclysm_sim import loot


def test_the_rule_is_the_owners() -> None:
    assert af.ALL_RESISTANCES_LOWEST_DROP_TIER == 2
    assert not af.rolls_on_a_drop_at(af.ALL_RESISTANCE, 1)
    for tier in range(2, af.DIFFICULTY_TIERS + 1):
        assert af.rolls_on_a_drop_at(af.ALL_RESISTANCE, tier), tier
    for tier in range(1, af.DIFFICULTY_TIERS + 1):
        for family in (af.SINGLE_RESISTANCE, af.HYBRID_RESISTANCE):
            assert af.rolls_on_a_drop_at(family, tier), (family.name, tier)
        for affix in af.everything_for("Chest", af.SUFFIX):
            if affix is not af.ALL_RESISTANCE:
                assert af.rolls_on_a_drop_at(affix, tier), (affix, tier)


@pytest.mark.parametrize("tier", [0, 9])
def test_a_tier_outside_one_to_eight_is_refused(tier: int) -> None:
    with pytest.raises(ValueError, match="outside 1 to 8"):
        af.rolls_on_a_drop_at(af.ALL_RESISTANCE, tier)


def counts(tier: int, drops: int = 3000, seed: int = 2853) -> dict[str, int]:
    """Resistance families rolled on `drops` Chests at this tier, 300 magic find so most carry several affixes."""
    rng = random.Random(seed)
    out = {family.name: 0 for family in af.RESISTANCE_FAMILIES}
    out["affixes"] = 0
    for _ in range(drops):
        item = loot.roll_item("Chest", tier, 300.0, rng)
        assert len(item.affixes) == af.affix_slots_for(item.rarity), "a drop lost an affix slot"
        out["affixes"] += len(item.affixes)
        for rolled in item.affixes:
            if isinstance(rolled.affix, af.AffixFamily):
                out[rolled.affix.name] += 1
    return out


def test_no_tier_one_drop_rolls_the_all_resistances_family_and_single_resistances_still_roll() -> None:
    """3,000 Chests at tier 1 and at tier 2 from one seed. CONTROL: at tier 2 the family rolls, so the tier 1 count
    is the rule's and not a family the Chest never offers; every drop keeps its affix count, the place taken by
    another affix."""
    one, two = counts(1), counts(2)
    assert one[af.ALL_RESISTANCE.name] == 0, one
    assert one[af.SINGLE_RESISTANCE.name] > 0 and one[af.HYBRID_RESISTANCE.name] > 0, one
    assert two[af.ALL_RESISTANCE.name] > 0, f"CONTROL: tier 2 rolled no all-resistances affix either: {two}"


def test_a_tier_two_drop_is_rolled_exactly_as_before_the_rule() -> None:
    """At tier 2 and above nothing is taken out, and the candidates are built before the rule is asked, so a seeded
    drop is the very item it was: the stream is consumed in the same order. Compared with a roll that never asks."""
    def without_the_rule(slot: str, tier: int, magic_find: float, rng) -> loot.RolledItem:
        rarity = loot.roll_rarity(tier, magic_find, rng)
        base = rng.choice(af.bases_for(slot))
        prefixes, suffixes = loot.split_for_a_drop(af.affix_slots_for(rarity), rng)
        drawn: list = []
        for position, count in (("prefix", prefixes), ("suffix", suffixes)):
            if count:
                drawn.extend(af.draw_without_repeating_a_group(loot._candidates_for(slot, position, rng), count, rng,
                                                               group_of=loot._groups_of_candidate))
        rolled = tuple(loot.RolledAffix(affix=c.affix, tier=af.roll_affix_tier(tier, rng), roll=rng.random(),
                                        covers=c.covers) for c in drawn)
        return loot.RolledItem(base=base, gear_level=loot.gear_level_gate(rarity), affixes=rolled,
                               enchantments=af.enchantments_for(rarity), residue=loot.roll_residue(rarity, rng),
                               sockets=loot.roll_sockets(base, rng))

    for seed in range(40):
        for slot in ("Chest", "Ring", "Boots"):
            assert loot.roll_item(slot, 2, 300.0, random.Random(seed)) == without_the_rule(
                slot, 2, 300.0, random.Random(seed)), (seed, slot)
