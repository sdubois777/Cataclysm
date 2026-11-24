"""Every run the game plays is handed the empire upgrade tree's bonuses, at the right moment.

WHY THIS EXISTS. Issue #2741 slice 3a. The tree's city bonuses are not saved
(`FCataclysmEmpireTreeBonuses`), so a run has them only if something hands them
over: `UCataclysmEmpireRun::SetTreeBonuses`, through the game instance. An
automation test can check what the handing does but not that every door hands
them: a game instance a test makes has no world of its own, so
`BeginACharactersEmpireRun` reads no player there, and a load is the whole load.
A door that stopped handing them over would leave a run with no tree, silently,
until the next point bought.

WHAT IS CHECKED, in the source with comments taken out, so a call named only in
a comment does not count:
  a new run       `BeginACharactersEmpireRun` hands the tree BEFORE
                  `BeginForCharacter`, so the cities are built full (S1)
  the map         `BeginForCharacter` gives the map the tree before `Build`
  a load          the loaded run is handed its character's tree once it is the
                  game's, through the door that writes nothing into its cities
  buy and choose  each hands the changed tree over after the change, through
                  the door that keeps every city's share (issue #2859)
  respec          hands it over after the points come back and BEFORE the days
                  pass, so they pass without the tree
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
GAME_INSTANCE = SOURCE / "Cataclysm" / "Player" / "CataclysmGameInstance.cpp"
SAVE_LOAD = SOURCE / "Cataclysm" / "Save" / "CataclysmSaveLoad.cpp"
EMPIRE_RUN = SOURCE / "CataclysmEmpire" / "Empire" / "CataclysmEmpireRun.cpp"


def without_comments(text: str) -> str:
    """The C++ with `/* ... */` blocks and `//` comments removed, not lines by their first character."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def body_of(path: pathlib.Path, signature: str) -> str:
    """A function's body: from its signature to the first closing brace at the start of a line."""
    text = without_comments(path.read_text(encoding="utf-8")).replace("\r\n", "\n")
    start = text.find(signature)
    assert start >= 0, f"{path.name} has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end > start, f"{path.name}: the body of {signature!r} has no end"
    return text[start:end]


def test_a_new_run_is_handed_the_tree_before_it_begins() -> None:
    body = body_of(GAME_INSTANCE, "UCataclysmGameInstance::BeginACharactersEmpireRun(")
    handed = body.find("SetTreeBonuses(EmpireTreeBonusesFor(this))")
    begun = body.find("BeginForCharacter(")
    assert handed >= 0, "BeginACharactersEmpireRun does not hand the new run the tree"
    assert begun >= 0, "BeginACharactersEmpireRun no longer calls BeginForCharacter"
    assert handed < begun, ("BeginACharactersEmpireRun hands the tree over after BeginForCharacter, "
                            "so the cities are built before it and are not full at the raised maxima")


def test_the_map_is_given_the_tree_before_it_is_built() -> None:
    body = body_of(EMPIRE_RUN, "UCataclysmEmpireRun::BeginForCharacter(")
    given = body.find("Map->TreeBonuses = TreeBonuses;")
    built = body.find("Map->Build();")
    assert 0 <= given < built, "BeginForCharacter must give the map the tree before Map->Build()"


def test_a_loaded_run_is_handed_its_characters_tree() -> None:
    text = without_comments(SAVE_LOAD.read_text(encoding="utf-8")).replace("\r\n", "\n")
    installed = text.find("Instance->EmpireRun = RestoredEmpire;")
    assert installed >= 0, "CataclysmSaveLoad.cpp no longer installs the restored run where this expects"
    # THROUGH THE LOAD'S OWN DOOR SINCE ISSUE #2859, which writes nothing into the cities; a change in play keeps each
    # city's share instead, and doing that to a loaded run would scale its saved cities by the whole tree.
    handed = text.find("UCataclysmGameInstance::HandEmpireTreeBonusesToALoadedRun(&World);", installed)
    assert handed >= 0 and handed - installed < 400, (
        "the loaded run is not handed its character's tree straight after it becomes the game's")
    assert "RefreshEmpireTreeBonuses(" not in text, (
        "a load hands its run the tree through the door for a change in play, which keeps each city's share and so "
        "scales the cities the record saved by the whole tree")


def test_a_change_in_play_keeps_each_citys_share_and_a_load_writes_nothing() -> None:
    """Issue #2859: the owner, "It should maintain the % they were at before buying the upgrades"."""
    change = body_of(GAME_INSTANCE, "void UCataclysmGameInstance::RefreshEmpireTreeBonuses(")
    assert "Run->ChangeTreeBonuses(EmpireTreeBonusesFor(WorldContext));" in change, (
        "a tree changed in play is not handed over through ChangeTreeBonuses, so the cities do not keep their shares")
    assert "SetTreeBonuses(" not in change, "a tree changed in play is handed over without keeping the cities' shares"
    loaded = body_of(GAME_INSTANCE, "void UCataclysmGameInstance::HandEmpireTreeBonusesToALoadedRun(")
    assert "Run->SetTreeBonuses(EmpireTreeBonusesFor(WorldContext));" in loaded, (
        "a loaded run is not handed its tree through SetTreeBonuses, which writes nothing into its cities")
    assert "ChangeTreeBonuses(" not in loaded, "a loaded run's saved cities are scaled by the whole tree"
    run = body_of(EMPIRE_RUN, "void UCataclysmEmpireRun::ChangeTreeBonuses(")
    taken = run.find("const FCataclysmCityShares Before = Map->SharesNow();")
    handed = run.find("SetTreeBonuses(Bonuses);", taken)
    kept = run.find("Map->KeepShares(Before);", handed)
    assert -1 < taken < handed < kept, (
        "ChangeTreeBonuses must take every city's share before the tree changes and put every city back at it after")


def test_a_point_bought_and_an_option_chosen_hand_the_tree_over() -> None:
    for signature, change in (("UCataclysmGameInstance::BuyEmpireTreePoint(", "UCataclysmEmpireTree::Spend("),
                              ("UCataclysmGameInstance::ChooseEmpireTreeOption(",
                               "UCataclysmEmpireTree::ChooseOption("),
                              ("UCataclysmGameInstance::ChooseEmpireTreeExtraOption(",
                               "UCataclysmEmpireTree::ChooseExtraOption(")):
        body = body_of(GAME_INSTANCE, signature)
        changed = body.find(change)
        handed = body.find("RefreshEmpireTreeBonuses(WorldContext);")
        assert 0 <= changed < handed, f"{signature} does not hand the tree over after {change}"


def test_a_respec_hands_the_tree_over_before_its_days_pass() -> None:
    body = body_of(GAME_INSTANCE, "UCataclysmGameInstance::RespecEmpireTree(")
    returned = body.find("UCataclysmEmpireTree::Respec(")
    handed = body.find("RefreshEmpireTreeBonuses(WorldContext);")
    days = body.find("AdvanceDays(")
    assert 0 <= returned < handed < days, (
        "RespecEmpireTree must hand the respecced tree over after the points come back and before AdvanceDays, "
        "so the days it costs pass without the tree")
