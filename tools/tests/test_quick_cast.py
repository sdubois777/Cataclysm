"""The quick cast: the second kind of enemy attack, a flat bolt with no marker.

Issue #2808, from the owner's playtest of 2026-11-21: "sometimes enemies should
be able to just shoot a fireball at you as long as you can see it". The Attack
Telegraphs subsection of `docs/Cataclysm_GDD_v2.md` states the rule, and
`sim/cataclysm_sim/enemy_abilities.py` checks it when it is imported.

WHAT THESE HOLD. The half second is one figure in the model and the C++; the
design document's table of quick casts is what the model computes; and the
model's import-time check refuses each kind of quick cast that could not be
stepped around, each refusal beside a control that the same check accepts.
"""

from __future__ import annotations

import dataclasses
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CHARACTER_BASE = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                  / "CataclysmCharacterBase.h")
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"


def test_the_half_second_is_one_figure_in_the_model_and_the_cpp() -> None:
    from cataclysm_sim.enemy_abilities import QUICK_CAST_SECONDS

    match = re.search(r"static\s+constexpr\s+float\s+QuickCastSeconds\s*=\s*"
                      r"(\d+(?:\.\d+)?)f\s*;",
                      CHARACTER_BASE.read_text(encoding="utf-8"))
    assert match is not None, (
        "CataclysmCharacterBase.h has no FCataclysmEnemyAbility::QuickCastSeconds.")
    assert float(match.group(1)) == pytest.approx(QUICK_CAST_SECONDS), (
        f"the C++ quick cast is {match.group(1)} s and the model's is "
        f"{QUICK_CAST_SECONDS} s.")
    assert QUICK_CAST_SECONDS == pytest.approx(0.5), (
        "the quick cast is no longer the half second the coordinating session "
        "ruled on 2026-11-21 under issue #2808.")


def test_the_design_documents_table_is_what_the_model_computes() -> None:
    """Each row of the quick cast table: its cast, radius, speed and the
    distance it can be stepped around from, as the model works them out."""
    from cataclysm_sim.enemy_abilities import (ABILITIES, QUICK_CAST_SECONDS,
                                               quick_cast_dodge_distance)

    text = GDD.read_text(encoding="utf-8")
    quick = [(enemy, a) for enemy, entries in ABILITIES.items()
             for a in entries if a.quick_cast]
    assert len(quick) == 2, quick
    for enemy, ability in quick:
        owner = "the Succubus" if enemy == "Succubus" else f"the {enemy}"
        row = (f"| {owner[0].upper() + owner[1:]}'s {ability.name} "
               f"| {QUICK_CAST_SECONDS:.1f} s "
               f"| {float(ability.params['Radius']):.1f} m "
               f"| {int(ability.params['Speed'])} cm/s "
               f"| {quick_cast_dodge_distance(ability):.2f} m |")
        assert row in text, (
            f"docs/Cataclysm_GDD_v2.md has no quick cast row {row!r}.")


def run_check_with(monkeypatch, enemy: str, ability) -> None:
    """Runs the model's quick cast check with one ability swapped in."""
    from cataclysm_sim import enemy_abilities as model

    patched = dict(model.ABILITIES)
    patched[enemy] = tuple(ability if a.name == ability.name else a
                           for a in model.ABILITIES[enemy])
    monkeypatch.setattr(model, "ABILITIES", patched)
    model._check_every_quick_cast_can_be_stepped_around_in_flight()


@pytest.fixture
def soulfire():
    from cataclysm_sim.enemy_abilities import abilities

    return next(a for a in abilities("Succubus") if a.name == "Soulfire")


def test_the_check_accepts_the_designed_bolt(monkeypatch, soulfire) -> None:
    """THE CONTROL for the three refusals below: the same call, unchanged."""
    run_check_with(monkeypatch, "Succubus", soulfire)


def test_the_check_refuses_a_lob(monkeypatch, soulfire) -> None:
    params = {k: v for k, v in soulfire.params.items() if k != "Speed"}
    lob = dataclasses.replace(soulfire, params={**params, "Arc": 0.25})
    with pytest.raises(AssertionError, match="not a flat"):
        run_check_with(monkeypatch, "Succubus", lob)


def test_the_check_refuses_a_bolt_with_the_old_radius(monkeypatch, soulfire) -> None:
    """3.15 metres, what Soulfire carried before issue #2808."""
    wide = dataclasses.replace(soulfire, params={**soulfire.params, "Radius": 3.15})
    with pytest.raises(AssertionError, match="needs a marker"):
        run_check_with(monkeypatch, "Succubus", wide)


def test_the_check_refuses_a_bolt_no_one_can_step_around(monkeypatch, soulfire) -> None:
    """Fast enough that it arrives before anyone could step clear anywhere in
    its range."""
    fast = dataclasses.replace(soulfire, params={**soulfire.params, "Speed": 100000})
    with pytest.raises(AssertionError, match="cannot be stepped around"):
        run_check_with(monkeypatch, "Succubus", fast)


def test_the_dodge_distance_grows_with_the_radius(soulfire) -> None:
    """The formula, against a figure worked by hand: 0.4 + (0.5 + 0.42) / 3.5
    = 0.6629 s needed, less the 0.5 s cast, times 12 m/s = 1.954 m."""
    from cataclysm_sim.enemy_abilities import quick_cast_dodge_distance

    assert quick_cast_dodge_distance(soulfire) == pytest.approx(
        12.0 * (0.4 + 0.92 / 3.5 - 0.5))
    wider = dataclasses.replace(soulfire, params={**soulfire.params, "Radius": 0.9})
    assert quick_cast_dodge_distance(wider) > quick_cast_dodge_distance(soulfire)
