"""Three Death nodes that pay with Fervour, read where the engine does it.

WHY THIS EXISTS. The Death trees' slice C3c (2026-11-25). Cinders Kept returns a share of each spend 4 seconds later:
`UCataclysmAbilitySystemComponent::NoteFervourSpent` queues it and the regeneration step returns it. Phylactery saves a
lethal blow by spending all the Fervour held: `UCataclysmVitalAttributeSet` asks it last of the saves, sets health and
then raises the spend. Borrowed Step lets a movement skill be used on cooldown for Fervour:
`UCataclysmGameplayAbility::CheckCooldown` lets the use through and `ApplyCooldown` takes the Fervour without starting
the cooldown again.

WHAT IS ASSERTED. Read from the C++ with comments removed, because continuous integration compiles no C++: each hook is
where it must be and in the order it must be. And that the node-text check's new exception, a figure stated once
beside a per-point row, passes only the node named for it and only with its per-point partner. `Cataclysm.FervourPaid.`
checks the behaviour in the engine.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))

import test_passive_effects_match_the_node_text as node_text  # noqa: E402

SOURCE = ROOT / "game" / "Source" / "Cataclysm"


def code_only(text: str) -> str:
    """The source with block and line comments blanked, so a name in prose is not read as code."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def read(relative: str) -> str:
    return code_only((SOURCE / relative).read_text(encoding="utf-8"))


def body_of(code: str, signature: str) -> str:
    start = code.index(signature)
    return code[start:code.index("\n}\n", start)]


ASC = "AbilitySystem/CataclysmAbilitySystemComponent.cpp"


def test_a_spend_queues_its_returned_share_and_the_step_returns_it():
    spend = body_of(read(ASC), "void UCataclysmAbilitySystemComponent::NoteFervourSpent(")
    assert "FName(FervourSpentReturnedPercentStat)" in spend and "FervourToReturn.Emplace(" in spend, (
        "NoteFervourSpent no longer queues Cinders Kept's share")
    step = read("Character/CataclysmCharacterBase.cpp")
    assert "->ReturnFervourDue()" in step, "the regeneration step no longer returns what is due"


def test_a_death_forgets_the_shares_and_the_save_wait():
    death = body_of(read(ASC), "FCataclysmWhatDeathEnded UCataclysmAbilitySystemComponent::ClearWhatDeathEnds(")
    assert "FervourToReturn.Reset()" in death, "a death leaves Cinders Kept's shares to arrive after the revival"
    assert "FervourLethalSaveNextAllowedSeconds = -1.0f" in death, "a death leaves Phylactery's wait running"


def test_phylactery_is_the_last_save_and_raises_its_spend_after_setting_health():
    vital = read("AbilitySystem/CataclysmVitalAttributeSet.cpp")
    save = vital.index("Guarded->MaySaveLethalBlowWithFervour()")
    assert vital.index("Guarded->SavesLethalBlowByCrossing(") < save, "Phylactery is asked before a cheaper save"
    window = vital[save - 400:save + 900]
    assert "!bSavedFromALethalBlow" in window, "Phylactery is asked of a blow another save already saved"
    assert window.index("SetHealth(") < window.index("NoteFervourSpent(Spent)"), (
        "the spend is raised before health is set, so a heal on the spend is written over")


def test_borrowed_step_is_let_through_and_paid_where_the_cooldown_is_kept():
    ability = read("AbilitySystem/CataclysmGameplayAbility.cpp")
    check = body_of(ability, "bool UCataclysmGameplayAbility::CheckCooldown(")
    assert "FervourToUseOnCooldown(AbilitySystem)" in check, "a movement skill on cooldown is never let through"
    cooldown = body_of(ability, "void UCataclysmGameplayAbility::ApplyCooldown(")
    taken = cooldown.index("FervourToUseOnCooldown(Borrowing)")
    assert taken < cooldown.index("CooldownIsSkipped("), "the borrowed use reaches the cooldown before it is paid"
    assert "NoteFervourSpent(Borrowed" in cooldown, "Borrowed Step's Fervour is not a spend"
    reader = body_of(ability, "float UCataclysmGameplayAbility::FervourToUseOnCooldown(")
    assert "GetDesignedSlot() != ECataclysmAbilitySlot::Movement" in reader, (
        "the cost is asked of skills that are not movement skills")


def stated_once(node: str, min_points: int, stat: str = "movement_on_cooldown_fervour_cost") -> dict:
    return {"Name": f"{node}#1", "Node": node, "Stat": stat, "MinPoints": str(min_points)}


BORROWED = "Shadow_basic_c_c0"
SENTENCE = {"Description": "Your movement skill can be used while it is on cooldown by spending 40 Fervour, "
                           "reduced by 2 per point."}


def test_a_figure_stated_once_passes_beside_its_per_point_partner():
    effects = [stated_once(BORROWED, 1), stated_once(BORROWED, 0)]
    node_text.test_a_row_from_n_points_is_on_a_node_whose_sentence_says_at_n_points(effects, {BORROWED: SENTENCE})


@pytest.mark.parametrize("effects, nodes", [
    # THE PARTNER MISSING: the 40 alone would be the whole cost at every point.
    ([stated_once(BORROWED, 1)], {BORROWED: SENTENCE}),
    # ANOTHER NODE: the exception is named node by node.
    ([stated_once("Shadow_basic_c_c1", 1), stated_once("Shadow_basic_c_c1", 0)], {"Shadow_basic_c_c1": SENTENCE}),
    # THE WORDS GONE from the sentence.
    ([stated_once(BORROWED, 1), stated_once(BORROWED, 0)], {BORROWED: {"Description": "Something else."}}),
])
def test_a_figure_stated_once_without_its_reasons_is_still_refused(effects, nodes):
    """THE CONTROLS for the exception: without each of its three reasons the row is refused as before."""
    with pytest.raises(AssertionError, match="applies from 1 points"):
        node_text.test_a_row_from_n_points_is_on_a_node_whose_sentence_says_at_n_points(effects, nodes)
