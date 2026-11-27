"""The empire tree's drawn rings, lines and labels must reach the game as the tree file places them.

WHY THIS EXISTS. The owner, 2026-11-27, of the empire tree screen that drew one branch at a time: "Right now it looks
like this weird mess, not the beautiful circle json with 4 quadrants we built with the passivetreebuilder". The circle
and its four quadrants are the builder's drawn elements (`uiElements` in `docs/Empire_Development_Tree_Final.json`),
not its nodes, so `tools/generate_datatables.py` carries them as `game/Data/EmpireTreeDecorations.csv`. That is a copy,
and this holds it to the file.

WHAT THE TABLE CHANGES ON THE WAY: the builder places an element by the top-left corner of its box, and the table says
the point the screen draws around. A ring of radius 160 written at (-160, -160) is a ring centred on the origin; a label
is centred in its measured box; a line keeps its start. Each check below states one of those, and a control shows the
corner and the centre are told apart.
"""

from __future__ import annotations

import csv
import json
import math
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TREE_FILE = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"
DECORATIONS_CSV = REPO_ROOT / "game" / "Data" / "EmpireTreeDecorations.csv"
ROW_STRUCTS = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Data" / "CataclysmDataRows.h"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402


def elements() -> list[dict]:
    return json.loads(TREE_FILE.read_text(encoding="utf-8"))["uiElements"]


def rows() -> dict[str, dict]:
    with DECORATIONS_CSV.open(encoding="utf-8", newline="") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def test_every_drawn_element_of_the_file_is_one_row_and_nothing_else_is() -> None:
    assert set(rows()) == {element["id"] for element in elements()}
    kinds = sorted(row["Kind"] for row in rows().values())
    # SIX RINGS, FOUR LINES AND TWELVE LABELS, counted in the file on 2026-11-27.
    assert (kinds.count("circle"), kinds.count("line"), kinds.count("label")) == (6, 4, 12)


def test_the_rings_are_centred_on_the_middle_of_the_tree_and_keep_their_radii() -> None:
    radii = []
    for element in elements():
        if element["type"] != "ui_circle":
            continue
        row = rows()[element["id"]]
        radius = element["data"]["radius"]
        # THE CONTROL: the file writes the ring's corner, which is not the origin, and the row says the centre, which is.
        assert (element["position"]["x"], element["position"]["y"]) == (-radius, -radius)
        assert (float(row["CentreX"]), float(row["CentreY"])) == (0.0, 0.0)
        assert float(row["Radius"]) == radius
        radii.append(radius)
    assert sorted(radii) == [60, 160, 280, 410, 540, 740]


def test_the_four_lines_start_at_the_middle_and_point_along_the_axes() -> None:
    angles = []
    for element in elements():
        if element["type"] != "ui_line":
            continue
        row = rows()[element["id"]]
        assert (float(row["CentreX"]), float(row["CentreY"])) == (0.0, 0.0)
        assert float(row["Length"]) == element["data"]["length"] == 800
        assert float(row["Thickness"]) == element["data"]["width"]
        angles.append(float(row["AngleDegrees"]))
    assert sorted(angles) == [-90.0, 0.0, 90.0, 180.0]


def test_a_label_is_centred_in_its_measured_box_and_says_the_files_words() -> None:
    labels = {}
    for element in elements():
        if element["type"] != "ui_textLabel":
            continue
        row = rows()[element["id"]]
        measured = element["measured"]
        assert math.isclose(float(row["CentreX"]), element["position"]["x"] + measured["width"] / 2)
        assert math.isclose(float(row["CentreY"]), element["position"]["y"] + measured["height"] / 2)
        assert float(row["FontSize"]) == element["data"]["fontSize"]
        labels[row["Text"]] = row
    for branch in gen.EMPIRE_BRANCHES:
        assert branch.upper() in labels, f"no label names the {branch} quadrant"
    # EACH QUADRANT'S NAME SITS IN ITS QUADRANT: north is a negative y, as `_empire_branch` reads the nodes.
    quadrant = {"ARCHITECT": (1, -1), "EXPLORER": (1, 1), "TREASURY": (-1, 1), "ARTISAN": (-1, -1)}
    for text, (sign_x, sign_y) in quadrant.items():
        assert math.copysign(1, float(labels[text]["CentreX"])) == sign_x
        assert math.copysign(1, float(labels[text]["CentreY"])) == sign_y


def test_every_colour_is_the_files_and_every_opacity_too() -> None:
    for element in elements():
        row = rows()[element["id"]]
        assert row["Colour"] == element["data"]["color"].upper()
        assert float(row["Opacity"]) == element["data"]["opacity"]


def test_the_rings_and_lines_are_below_the_nodes_and_the_labels_over_them() -> None:
    # THE BUILDER'S OWN ORDER, its `zIndex`: a quadrant's name is drawn over the capstone beside it, not under it.
    for element in elements():
        row = rows()[element["id"]]
        assert int(row["Layer"]) == element["zIndex"]
        assert (int(row["Layer"]) < 0) == (row["Kind"] != "label"), row["Name"]


def test_a_kind_the_builder_adds_later_stops_generation(monkeypatch: pytest.MonkeyPatch) -> None:
    data = json.loads(TREE_FILE.read_text(encoding="utf-8"))
    data["uiElements"].append({"id": "ui_arrow_1", "type": "ui_arrow", "position": {"x": 0, "y": 0}, "data": {}})
    monkeypatch.setattr(gen, "_load_empire_tree", lambda: data)
    with pytest.raises(gen.DataError, match="ui_arrow"):
        gen.empire_tree_decorations()


def test_the_columns_are_the_row_structs_fields() -> None:
    header = DECORATIONS_CSV.read_text(encoding="utf-8").splitlines()[0].split(",")
    text = ROW_STRUCTS.read_text(encoding="utf-8")
    start = text.index("struct FCataclysmEmpireTreeDecorationRow")
    block = text[start:text.index("};", start)]
    # THE NAME IS THE WORD AFTER THE TYPE: `float CentreX = 0.0f;` and `FString Kind;`.
    fields = [line.split()[1].rstrip(";")
              for line in block.splitlines()
              if line.strip().startswith(("FString ", "float ", "int32 "))]
    assert header == ["Name"] + fields
