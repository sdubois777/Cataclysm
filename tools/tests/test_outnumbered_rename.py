"""The rename of the Bulwark's basic node Wrath Incarnate to Outnumbered (the War rulings' R29).

`tools/apply_outnumbered_rename_2026_11_25.py` renames it by id. These tests hold that the repository's tree file and
generated node table carry the new name, that the Final Oath's option keeps the old one, that the script reproduces
the file byte for byte from the old name and then does nothing, and that it refuses a node somebody changed since.
"""
from __future__ import annotations

import csv
import json
import pathlib
import subprocess
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import apply_outnumbered_rename_2026_11_25 as rename  # noqa: E402

sys.path.insert(0, str(REPO_ROOT / "tools" / "tests"))
import without_the_war_trees_rows  # noqa: E402

TREE_FILE = REPO_ROOT / "docs" / "Bulwark_Class_Tree_Final.json"
#: THE TREE FILE AS THE RENAME LEFT IT, with the War trees' sentences of 2026-11-27 taken out: the coordinating
#: session's reading of Outnumbered's "all nearby enemies" as 4 metres rewrote the sentence the rename checks for, after
#: it. The name tests read the repository's file; the script's own tests read this copy.
BEFORE_LATER_SENTENCES = without_the_war_trees_rows.docs() / "Bulwark_Class_Tree_Final.json"


def _node(data: dict) -> dict:
    (found,) = [node for node in data["nodes"] if node.get("id") == rename.NODE_ID]
    return found["data"]


def test_the_basic_node_is_called_outnumbered_and_the_oath_option_keeps_its_name():
    data = json.loads(TREE_FILE.read_text(encoding="utf-8"))
    assert _node(data)["name"] == rename.AFTER
    names = [node["data"].get("name") for node in data["nodes"]]
    assert names.count(rename.BEFORE) == 0, "no node is called Wrath Incarnate any more"
    (oath,) = [node for node in data["nodes"] if node["data"].get("name") == rename.OATH]
    assert rename.BEFORE in [option["name"] for option in oath["data"]["options"]]


def test_the_generated_node_table_carries_the_new_name():
    with open(REPO_ROOT / "game" / "Data" / "PassiveNodes.csv", encoding="utf-8", newline="") as handle:
        rows = {row["Name"]: row for row in csv.DictReader(handle)}
    assert rows[f"Bulwark_{rename.NODE_ID}"]["NodeName"] == rename.AFTER


def test_the_script_has_nothing_left_to_do():
    result = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "apply_outnumbered_rename_2026_11_25.py"),
                             "--check", str(BEFORE_LATER_SENTENCES.parent)], capture_output=True, text=True,
                            cwd=REPO_ROOT)
    assert result.returncode == 0, result.stdout + result.stderr


def _with_name(tmp_path, name: str) -> pathlib.Path:
    raw = BEFORE_LATER_SENTENCES.read_bytes()
    old = f'"name": "{rename.AFTER}",'.encode()
    assert raw.count(old) == 1
    copy = tmp_path / TREE_FILE.name
    copy.write_bytes(raw.replace(old, f'"name": "{name}",'.encode()))
    return copy


def test_the_script_renames_the_old_name_once_byte_for_byte(tmp_path):
    copy = _with_name(tmp_path, rename.BEFORE)
    outcome = rename.plan_file(copy)
    assert outcome.changes == [f"renamed {rename.NODE_ID} from {rename.BEFORE!r} to {rename.AFTER!r}"]
    copy.write_bytes(outcome.new_bytes)
    assert copy.read_bytes() == BEFORE_LATER_SENTENCES.read_bytes()
    assert rename.plan_file(copy).new_bytes is None


def test_the_script_refuses_a_node_somebody_renamed_since(tmp_path):
    copy = _with_name(tmp_path, "Somebody Else's Name")
    with pytest.raises(rename.order.Refused, match="neither"):
        rename.plan_file(copy)
