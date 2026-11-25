"""The passive tree screen draws each node as a mark sized by its kind (2026-11-24, for the class-tree sigils).

`Cataclysm.PassiveMarks.` tests what the screen decides; this checks the shape of the code, which continuous
integration can run without a build:

  1. the marks: an ordinary 28 by 28, a keystone 48 by 48, a vow 72 by 40; edges 10 px; the name 8 px past the mark;
     and the empire tree's box (150 by 44) and 3 px edge unchanged, since that screen still reads them;
  2. a mark is chosen by the tree files' own words for a kind, through `UCataclysmPassiveTree`'s constants;
  3. the class tree screen sizes each node by its mark, draws its edges at the mark thickness, says only the spent
     count on a mark, and adds the name label after the nodes, hit-test invisible; the box rule is gone;
  4. no text the screen shows says "sigil": the word names the Unholy Sigils modifier and the Ritualist's Binding
     Sigils, so a player would look for a connection that is not there (the sigil README's advice).
"""
from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
INTERFACE = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Interface"
LAYOUT_H = INTERFACE / "CataclysmPassiveTreeLayout.h"
LAYOUT_CPP = INTERFACE / "CataclysmPassiveTreeLayout.cpp"
WIDGET_CPP = INTERFACE / "CataclysmPassiveTreeWidget.cpp"
EMPIRE_WIDGET_CPP = INTERFACE / "CataclysmEmpireTreeWidget.cpp"


def code(path: pathlib.Path) -> str:
    """The file with its comments removed, so a name written only in a comment does not count."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    depth, at = 0, text.index("{", start)
    for index in range(at, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[at:index + 1]
    raise AssertionError(f"{signature} is never closed")


def constant(name: str) -> str:
    found = re.search(rf"static constexpr float {name} = ([^;]+);", code(LAYOUT_H))
    assert found, f"{LAYOUT_H.name} declares no {name}"
    return found.group(1).strip()


def test_the_marks_and_the_empire_trees_box_are_the_stated_sizes() -> None:
    assert constant("OrdinaryMarkPx") == "28.0f"
    assert constant("KeystoneMarkPx") == "48.0f"
    assert (constant("VowMarkWidthPx"), constant("VowMarkHeightPx")) == ("72.0f", "40.0f")
    assert constant("MarkEdgeThicknessPx") == "10.0f"
    assert constant("CaptionGapPx") == "8.0f"
    assert (constant("NodeWidthPx"), constant("NodeHeightPx"), constant("EdgeThicknessPx")) == (
        "150.0f", "44.0f", "3.0f"), "the empire tree's box or edge moved, and that screen still reads them"
    empire = flat(code(EMPIRE_WIDGET_CPP))
    assert "UCataclysmPassiveTreeLayout::NodeWidthPx" in empire and "UCataclysmPassiveTreeLayout::EdgeThicknessPx" in empire, (
        "the empire tree no longer reads the box; if that is meant, this check and the header's note go with it")


def test_a_mark_is_chosen_by_the_tree_files_words_for_a_kind() -> None:
    mark_for = flat(body(code(LAYOUT_CPP), "ECataclysmTreeMark UCataclysmPassiveTreeLayout::MarkFor("))
    assert "Kind == UCataclysmPassiveTree::KeystoneKind" in mark_for and "return ECataclysmTreeMark::Keystone;" in mark_for
    assert "Kind == UCataclysmPassiveTree::CapstoneKind" in mark_for and "return ECataclysmTreeMark::Vow;" in mark_for
    assert mark_for.rstrip("} ").endswith("return ECataclysmTreeMark::Ordinary;"), "a kind no tree writes is not ordinary"


def test_the_screen_draws_marks_thick_edges_and_one_name() -> None:
    widget = code(WIDGET_CPP)
    assert "NodeScaleFor" not in widget and "NodeScaleFor" not in code(LAYOUT_CPP), "the box rule is still used"
    place = flat(body(widget, "void UCataclysmPassiveTreeWidget::PlaceGraph("))
    assert "Placement->SetSize(UCataclysmPassiveTreeLayout::MarkSizeForNode(Table, Node) * BuiltMarkScale * Zoom);" in place, (
        "a node is not sized by its mark")
    assert "UCataclysmPassiveTreeLayout::MarkEdgeThicknessPx * Zoom" in place, "an edge is not drawn at the mark thickness"
    assert "UCataclysmPassiveTreeLayout::EdgeThicknessPx" not in place
    assert place.rstrip("} ").endswith("PlaceNodeName();"), "the name does not move with the view"
    describe = flat(body(widget, "void UCataclysmPassiveTreeWidget::DescribeNodeButton("))
    assert ("const FString Label = UCataclysmPassiveTreeLayout::MarkCaption(Allocation.PointsIn(Node), Row->MaxPoints);"
            in describe), "a mark says more than its spent count"
    build = flat(body(widget, "void UCataclysmPassiveTreeWidget::BuildGraph("))
    nodes_added = build.find("NodeButtons.Add(Button);")
    label_added = build.find("GraphCanvas->AddChild(NodeNameLabel)")
    assert -1 < nodes_added < label_added, "the name label is not added after the nodes, so it is drawn under them"
    assert "NodeNameLabel->SetVisibility(ESlateVisibility::HitTestInvisible);" in build, "the name label takes clicks"
    hover = flat(body(widget, "void UCataclysmPassiveTreeWidget::HandleNodeHovered("))
    assert hover.find("LastTouched = Node;") < hover.find("PlaceNodeName();") < hover.find("if (!ChoosingOptionFor.IsNone())"), (
        "hovering does not move the name, or moves it only when no capstone's options are on offer")


def test_no_text_the_screen_shows_says_sigil() -> None:
    for path in (WIDGET_CPP, LAYOUT_CPP):
        shown = re.findall(r'TEXT\("((?:[^"\\]|\\.)*)"\)', code(path))
        said = [text for text in shown if "sigil" in text.lower()]
        assert not said, f"{path.name} shows {said}: on screen it is the passive tree, not a sigil"
