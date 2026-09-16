"""`compare_surge_sizes` does not report two all-tied orderings as agreement.

WHY THIS EXISTS. Issue #1435. Section 7 of the balance report ends by asking
whether the preset ordering survives a change of surge size. At difficulty
tier 8 every preset scores 0% win and 100% loss, so the ranking puts all of
them in one group at both surge sizes; one group compares equal to one group,
and the report printed "SAME at both surge sizes" and then "The ordering holds
at tier 8". There is no ordering at tier 8. Two statements that nothing can be
ordered are not evidence that an ordering holds. `preset_tables` had guarded
the same case since issue #294; this holds the surge-size twin to the same rule.

DRIVEN DIRECTLY WITH HAND-BUILT RESULTS rather than by running the sweep, in
the shape issue #1385 asks for: the all-tied tier is written in, so the test
does not depend on the model still collapsing at tier 8, and it runs in
milliseconds.

THE CONTROL. Tier 1 below has three presets clearly apart in the same order at
both sizes, and must still print "SAME at both surge sizes". Without it, a
guard that refused every tier would pass the tied case and nothing here would
say so.
"""

from __future__ import annotations

import experiments

PRESETS = ("Alpha", "Beta", "Gamma")
TRIALS = 150


def _tier(win: dict[str, float], loss: dict[str, float]) -> dict:
    return {"win": win, "loss": loss,
            "margin": {name: win[name] - loss[name] for name in win}}


def _measured(sizes=(5, 7)) -> dict:
    """Two surge sizes, two tiers: tier 1 ordered, tier 8 all tied."""
    ordered = _tier({"Alpha": 80.0, "Beta": 50.0, "Gamma": 10.0},
                    {"Alpha": 5.0, "Beta": 20.0, "Gamma": 70.0})
    tied = _tier({name: 0.0 for name in PRESETS},
                 {name: 100.0 for name in PRESETS})
    measured = {}
    for size in sizes:
        measured[size] = {
            "wins": {1: ordered["win"], 8: tied["win"]},
            "losses": {1: ordered["loss"], 8: tied["loss"]},
            "margins": {1: ordered["margin"], 8: tied["margin"]},
            "unresolved": {},
        }
    return measured


def _block(capsys) -> str:
    experiments.compare_surge_sizes(_measured(), tiers=(1, 8), trials=TRIALS)
    return capsys.readouterr().out


def test_the_ordered_tier_still_reads_as_the_same_at_both_sizes(capsys):
    """The control: the guard must not swallow a real agreement."""
    printed = _block(capsys)
    tier_1 = printed.split("TIER 1")[1].split("TIER 8")[0]
    assert "Alpha > Beta > Gamma" in tier_1
    assert "SAME at both surge sizes" in tier_1


def test_an_all_tied_tier_is_reported_as_no_ordering_not_as_agreement(capsys):
    printed = _block(capsys)
    tier_8 = printed.split("TIER 8")[1].split("\n\n")[0]
    assert "Alpha = Beta = Gamma" in tier_8, "the fixture did not tie; the test measures nothing"
    assert "SAME at both surge sizes" not in tier_8, (
        "two orderings of one group were reported as agreement. Issue #1435.")
    assert "NO ORDERING" in tier_8


def test_an_all_tied_tier_is_not_counted_as_one_where_the_ordering_holds(capsys):
    printed = _block(capsys)
    conclusion = printed.rsplit("\n\n", 1)[-1]
    assert "holds at tier 1 and not at the others" in conclusion, conclusion
    assert "holds at tier 1, 8" not in conclusion
    assert "NO ORDERING at tier 8" in printed
