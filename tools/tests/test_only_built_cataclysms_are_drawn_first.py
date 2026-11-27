"""The game and the simulation draw the built Cataclysms first, and agree which those are.

The project owner's ruling of 2026-11-26, verbatim: "yeah only draw the ones that are built. It's kinda pointless to
test stuff that isn't even in the game." The owner's Death character had drawn Famine, which nothing in the game is
built for.

WHAT IS HELD HERE. The two lists of built Cataclysms -- `UCataclysmRoster::Built` in
`game/Source/CataclysmEmpire/Empire/CataclysmRoster.cpp` and `TuningConfig.BUILT_CATACLYSMS` in
`sim/cataclysm_sim/config.py` -- name the same Cataclysms, and both name only real ones. Building another is one line in
each list, and this is what fails when only one of the two lines is written.

WHAT IS HELD ELSEWHERE. The rule itself in the model, `sim/tests/test_the_cataclysms_drawn_belong_to_the_character.py`;
the same rule on a Python copy of the game's generator, `tools/tests/test_a_character_keeps_its_cataclysm_draw.py`;
and in the game, the `Cataclysm.Roster.` automation tests, which continuous integration does not run.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ROSTER_SOURCE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire" / "CataclysmRoster.cpp"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

OWNERS_WORDS = ("yeah only draw the ones that are built. It's kinda pointless to test stuff that isn't even in the "
                "game.")


def without_comments(text: str) -> str:
    """C++ with its `//` and `/* */` comments removed, so a name only a comment mentions is not read as listed."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def built_in_the_game(source: str) -> set[str]:
    """The Cataclysms `UCataclysmRoster::Built`'s list names, read from its body with comments removed."""
    body = re.search(r"const TArray<ECataclysmType>& UCataclysmRoster::Built\(\)\s*\{(.*?)\n\}", source, re.S)
    assert body is not None, f"{ROSTER_SOURCE.name} no longer defines UCataclysmRoster::Built"
    return set(re.findall(r"ECataclysmType::(\w+)", without_comments(body.group(1))))


def edited_in_built(source: str, old: str, new: str) -> str:
    """The source with the first `old` INSIDE `UCataclysmRoster::Built`'s body made `new`. `All` lists the same names
    first, so a replace over the whole file would edit `All` and leave `Built` as it was."""
    start = source.index("const TArray<ECataclysmType>& UCataclysmRoster::Built()")
    end = source.index("\n}", start)
    return source[:start] + source[start:end].replace(old, new, 1) + source[end:]


def flattened(path: pathlib.Path) -> str:
    return " ".join(path.read_text(encoding="utf-8").split())


def test_the_game_and_the_model_build_the_same_cataclysms():
    from cataclysm_sim.config import TuningConfig

    game = built_in_the_game(ROSTER_SOURCE.read_text(encoding="utf-8"))
    model = set(TuningConfig().BUILT_CATACLYSMS)
    assert game == model, (
        f"the game draws {sorted(game)} first and the model {sorted(model)}. Building a Cataclysm is one line in "
        f"UCataclysmRoster::Built and one in TuningConfig.BUILT_CATACLYSMS; one of the two was not written.")


def test_both_name_only_real_cataclysms():
    from cataclysm_sim.config import TuningConfig

    roster = set(TuningConfig().CATACLYSM_ROSTER)
    game = built_in_the_game(ROSTER_SOURCE.read_text(encoding="utf-8"))
    assert game, "UCataclysmRoster::Built names no Cataclysm, so the read found nothing to compare"
    assert game <= roster, f"UCataclysmRoster::Built names {sorted(game - roster)}, which is not one of the eight"
    assert "None" not in game


def test_control_a_list_that_differs_by_one_is_told_apart():
    """The comparison above, on the game's source with one built Cataclysm swapped for an unbuilt one, does not agree
    with the model -- so agreement is not something the read gives whatever the file says."""
    from cataclysm_sim.config import TuningConfig

    source = ROSTER_SOURCE.read_text(encoding="utf-8")
    model = set(TuningConfig().BUILT_CATACLYSMS)
    one = sorted(model)[0]
    unbuilt = sorted(set(TuningConfig().CATACLYSM_ROSTER) - model)[0]
    broken = edited_in_built(source, f"\t\tECataclysmType::{one},", f"\t\tECataclysmType::{unbuilt},")
    assert broken != source, f"the control found no line listing {one} to swap"
    assert built_in_the_game(broken) != model


def test_control_a_name_only_a_comment_mentions_is_not_read_as_built():
    source = ROSTER_SOURCE.read_text(encoding="utf-8")
    # DEATH, SINCE WAR LEFT THE LIST (2026-11-26): the control needs a built one to comment out.
    commented = edited_in_built(source, "\t\tECataclysmType::Death,", "\t\t// ECataclysmType::Death,")
    assert commented != source
    assert "Death" not in built_in_the_game(commented)


def test_the_design_document_says_only_built_ones_are_drawn_for_now():
    assert ("For now only the Cataclysms that are built — Demonic and Death — are drawn" in flattened(GDD)), (
        "docs/Cataclysm_GDD_v2.md no longer says that only built Cataclysms are drawn. The owner ruled it on "
        "2026-11-26.")


def test_the_decisions_log_quotes_the_owner():
    assert OWNERS_WORDS in flattened(DECISIONS), "docs/DECISIONS.md no longer quotes the owner's ruling of 2026-11-26"
