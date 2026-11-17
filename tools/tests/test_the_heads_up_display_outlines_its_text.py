"""Every piece of text the heads-up display draws has a black outline behind it.

WHY THIS IS A PYTHON TEST READING C++ SOURCE. It cannot be an automation test.
`AHUD::PostRender` checks `FApp::CanEverRender()` before calling `DrawHUD`, and
the automation suite runs with `-nullrhi`, so nothing in
`CataclysmCombatOverlayTests.cpp` ever draws anything -- every test there is a
static function over plain numbers. There is no way to ask the running suite what
a number looked like.

Reading the source with a regular expression is crude. The alternative here is no
guard at all, and `tools/tests/test_the_resistance_cap_is_one_number.py` already
does the same thing for the same reason.

WHY IT IS WORTH GUARDING. Issue #671. The project owner played two builds and
reported the numbers as hard to read both times. The second report was specific:
"Maybe need to be outlined in black or something?" -- an orange figure on a pale
stone floor. The outline is the part that stops being a matter of taste, because
the design has eight environments and a number has to read in all of them.
`ACataclysmHUD::DrawBar` already solved the identical problem for the bars by
drawing a dark backing wider than the fill, and says so in its own comment.

A colour or a size can be retuned freely. Deleting the outline is a regression,
and this is what says so.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
HUD = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
       / "CataclysmHUD.cpp")
HUD_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
              / "CataclysmHUD.h")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path} is not in this checkout")
    return path.read_text(encoding="utf-8")


def centred_text_body() -> str:
    """Just `ACataclysmHUD::DrawOutlinedText`, so a match elsewhere cannot pass.

    THE FUNCTION THIS READS HAS MOVED TWICE, and both moves were the outline
    staying put while the code around it changed:

    - It was `void ACataclysmHUD::DrawTextCentred` until issue #707 made that
      function return the rectangle it filled, so a drop's name could be
      clicked. This test failed on the signature while the outline was
      untouched, so the return type stopped being part of the anchor.
    - Issue #723 then split measuring from drawing, so that every drop name
      could be laid out before any of them was committed to the screen and two
      names would stop printing over each other. The outline moved into
      `DrawOutlinedText`, which is now the one function all text goes through,
      and `DrawTextCentred` became a measure followed by a call to it.

    THE OPEN PARENTHESIS IS PART OF THE ANCHOR, and it is the part that makes a
    rename fail here rather than pass silently. Without it the pattern matches
    any function whose name merely STARTS with `DrawOutlinedText`, so renaming
    this one to `DrawOutlinedTextSomewhere` would leave the guard reading a
    different function's body and reporting success. The plain `str.find` this
    replaced had the same hole; it was found by breaking the guard on purpose.
    """
    text = read(HUD)
    match = re.search(r"^\w[\w:<>&* ]*ACataclysmHUD::DrawOutlinedText\(",
                      text, re.MULTILINE)
    assert match is not None, (
        "ACataclysmHUD::DrawOutlinedText no longer exists. Every piece of text "
        "the heads-up display draws goes through it; if that changed, this "
        "guard has to move with it.")
    start = match.start()
    end = text.find("\n}", start)
    assert end != -1
    return text[start:end]


def test_the_text_is_drawn_in_black_before_it_is_drawn_in_colour():
    body = centred_text_body()

    assert "FLinearColor::Black" in body, (
        "the text is no longer drawn in black behind itself, so a number has "
        "only its own colour to separate it from whatever it is standing on.")

    # THE DRAW CALL, NOT THE COLOUR'S NAME. Checking that the word Black appears
    # does not work, and that is measured rather than guessed: deleting the
    # DrawText call from inside the offset loop was tried on purpose and every
    # other check in this file still passed. The colour was still declared, the
    # eight offsets were still listed, and nothing was drawn. A guard that reads
    # as coverage and is not is worth less than no guard.
    outline_draw = re.search(r"DrawText\(\s*Text\s*,\s*Outline\s*,", body)
    assert outline_draw, (
        "nothing is drawn in the outline colour. The colour may still be "
        "declared and the offsets still listed; neither puts anything on the "
        "screen.")

    coloured_at = body.rfind("DrawText(Text, Colour")
    assert coloured_at > outline_draw.start(), (
        "the coloured text is drawn before the black outline, so the outline "
        "covers the number instead of surrounding it.")


def test_the_outline_surrounds_the_text_rather_than_sitting_on_one_side():
    """Eight offsets, not a drop shadow.

    A shadow at one corner helps against a background on one side and does
    nothing on the other three. The complaint that produced this was about a
    number washing out against a floor, which is on every side of it.
    """
    body = centred_text_body()

    # Each entry of the offsets table is a brace pair holding two terms.
    offsets = re.findall(r"\{\s*-?\s*(?:Spread|0\.0f)\s*,\s*-?\s*(?:Spread|0\.0f)\s*\}",
                         body)
    assert len(offsets) == 8, (
        f"expected eight outline offsets surrounding the text, found "
        f"{len(offsets)}. Four leaves the diagonals thin and one is a drop "
        f"shadow rather than an outline.")


def test_the_outline_keeps_the_texts_own_transparency():
    """A number fades as it rises, and its outline has to fade with it.

    An outline left fully opaque would stay behind after the figure had faded
    out, which is worse than no outline.
    """
    body = centred_text_body()
    assert re.search(r"Outline\.A\s*=\s*Colour\.A", body), (
        "the outline no longer takes the text's own alpha, so it will not fade "
        "with the number it surrounds.")


def test_the_base_text_size_is_bigger_than_the_font_it_starts_from():
    """Reported as too small twice, so this is a floor rather than a preference.

    It deliberately does not pin an exact figure. The size is a placeholder the
    project owner retunes by playing; what this refuses is a silent return to
    drawing at the font's own size, which is the state both complaints were
    about.
    """
    text = read(HUD_HEADER)
    match = re.search(r"constexpr float TextScale\s*=\s*([0-9.]+)f", text)
    assert match, "ACataclysmHUD::TextScale no longer exists"

    scale = float(match.group(1))
    assert scale > 1.0, (
        f"TextScale is {scale}, which draws text at the engine font's own size "
        "or smaller. It was reported as too small at 1.0 and again at 1.6.")
