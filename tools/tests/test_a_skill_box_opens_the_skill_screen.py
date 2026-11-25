"""A press on a skill box opens the skill screen, the bar names K, and a new weapon's skills are told (issue #837).

WHY THIS EXISTS. Slice 2 of the skill selection plan, approved by the project owner on 2026-11-24 ("Approve all three
(Recommended)", Q2, Q3 and Q4). The plan found that nothing on screen named the skill screen's key: the owner's logs held
62 passive and attribute point lines and no skill chosen in five days.

WHAT IS CHECKED, by reading the C++ with comments removed, because a test world has no viewport to press in and no
screen to draw on:

  1. the left press asks for a skill box FIRST, before the interface test, and when it finds one opens the screen and
     returns, so the press is neither a move nor a swing;
  2. opening from the bar never closes the screen, and selects the key;
  3. K is the key `tools/generate_input_assets.py` binds to `IA_ToggleSkillAssignment` in both control schemes, and
     the bar's line says K;
  4. the heads-up display draws the bar's line and the skill pool's notice;
  5. the pawn says what a change of gear added only for a change of gear, never for a load, and a pool skill that is
     not built yet is marked on the screen.

WHAT IS NOT. What the notice says and what the key lookup answers: the `Cataclysm.SkillBar.` and
`Cataclysm.SkillDefaults.` automation tests check those.
"""

from __future__ import annotations

import ast
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tests"))

from test_the_play_loop_is_logged_at_each_step import function_body, read_code  # noqa: E402

SOURCE = ROOT / "game" / "Source" / "Cataclysm"
CONTROLLER = SOURCE / "Player" / "CataclysmPlayerController.cpp"
HUD = SOURCE / "Interface" / "CataclysmHUD.cpp"
BAR = SOURCE / "Interface" / "CataclysmSkillBar.cpp"
PAWN = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
SCREEN = SOURCE / "Interface" / "CataclysmSkillAssignmentWidget.cpp"
INPUTS = ROOT / "tools" / "generate_input_assets.py"


def test_a_press_on_a_skill_box_is_asked_first_and_goes_no_further() -> None:
    press = function_body(read_code(CONTROLLER), "void ACataclysmPlayerController::Input_MoveToCursorStarted()")
    asked = press.find("SkillBoxKeyAt(")
    interface = press.find("bPressBeganOnInterface = CursorIsOverInterface();")
    assert asked != -1, "the left press no longer asks whether it landed on a skill box"
    assert interface != -1 and asked < interface, (
        "the left press asks the interface test before the skill bar, which the heads-up display draws and the "
        "interface test does not see")
    opened = press.find("OpenSkillAssignmentOn(Box); return;", asked)
    assert opened != -1 and opened < interface, (
        "a press on a skill box no longer opens the screen and returns, so it would also move or swing")


def test_opening_from_the_bar_never_closes_the_screen_and_selects_the_key() -> None:
    body = function_body(read_code(CONTROLLER), "bool ACataclysmPlayerController::OpenSkillAssignmentOn(")
    assert "if (!SkillAssignmentScreen || !SkillAssignmentScreen->IsInViewport()) { ToggleSkillAssignment(); }" in body, (
        "OpenSkillAssignmentOn toggles the screen without asking whether it is open, so a second click would close it")
    assert "SkillAssignmentScreen->SelectSlot(Key);" in body, "OpenSkillAssignmentOn no longer selects the key"


def _mappings() -> dict[str, list]:
    found = {}
    for node in ast.parse(INPUTS.read_text(encoding="utf-8")).body:
        if (isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id in ("MOUSE_MAPPINGS", "KEYBOARD_MAPPINGS")):
            found[node.targets[0].id] = ast.literal_eval(node.value)
    return found


def test_k_opens_the_skill_screen_and_the_bar_says_so() -> None:
    mappings = _mappings()
    assert set(mappings) == {"MOUSE_MAPPINGS", "KEYBOARD_MAPPINGS"}, "the input generator's two schemes were not found"
    for scheme, rows in mappings.items():
        keys = sorted({key for action, key, _ in rows if action == "IA_ToggleSkillAssignment"})
        assert keys == ["K"], f"{scheme} binds IA_ToggleSkillAssignment to {keys}, and the bar says K"
    assert 'UCataclysmSkillBar::OpenScreenHint = TEXT("K: skills");' in read_code(BAR), (
        "the bar's line no longer says K")


def test_the_display_draws_the_bars_line_and_the_notice() -> None:
    hud = read_code(HUD)
    assert "UCataclysmSkillBar::OpenScreenHint" in function_body(hud, "void ACataclysmHUD::DrawSkillBar()"), (
        "the skill bar no longer draws the line naming K")
    assert "DrawSkillPoolNotice();" in hud, "the heads-up display no longer draws the skill pool's notice"


def test_a_change_of_gear_tells_and_a_load_does_not() -> None:
    changed = function_body(read_code(PAWN), "void ACataclysmPlayerCharacter::OnEquipmentChanged()")
    assert "NoteSkillsAddedToThePool(PoolBefore, TypesBefore);" in changed, (
        "a change of gear no longer says what it added to the skill pool")
    assert "!(Equipment && Equipment->IsAnnouncingARestore())" in changed, (
        "the notice is no longer held back for a load, whose one announcement comes through the same handler")
    label = function_body(read_code(SCREEN), "void UCataclysmSkillAssignmentWidget::DescribePoolButton(")
    assert "if (!UCataclysmWeaponSkills::IsBuilt(Skill)) { Label += NotBuiltYetMark; }" in label, (
        "a pool skill that is not built yet is no longer marked on the screen")
