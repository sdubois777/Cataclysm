"""The design document's control tables must match the bindings that are built.

WHY THIS FILE EXISTS. Issue #138. The control table in `Cataclysm_GDD_v2.md`
said the left mouse button was "Player movement and basic attack", while the
Combat System section of the same document said basic attacks are handled
automatically, and the code agreed with the combat section. The same table also
put the Support ability on W and directional movement on WASD, which is one key
doing two jobs. Both were resolved in code — the left mouse button moves and only
moves, and there are two mapping contexts of which only one is ever active — and
the table was left saying the old thing for a month.

WHAT IS CHECKED. The two tables under "Controls and Key Bindings" are read out of
the document and compared, key by key, against `MOUSE_MAPPINGS` and
`KEYBOARD_MAPPINGS` in `tools/generate_input_assets.py`, which is the script that
produces `IMC_MouseMovement` and `IMC_KeyboardMovement`. A binding in one and not
the other fails the test and names which side is missing it.

HOW THE GENERATOR IS READ. With `ast`, not by importing it. The module imports
`unreal`, which only exists inside the editor, so importing it outside the editor
raises. `ast.literal_eval` on the two list assignments gets the data without
running anything.

WHAT THIS DOES NOT CHECK. That the generator was actually run and the assets on
disk match the script. `IMC_MouseMovement.uasset` is a binary the editor writes;
comparing against it needs the editor. This checks document against script, which
is where the drift in issue #138 happened.
"""

from __future__ import annotations

import ast
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DESIGN_DOC = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
GENERATOR = REPO_ROOT / "tools" / "generate_input_assets.py"

#: How a key is written in the design document, and the engine key name or names
#: `tools/generate_input_assets.py` uses for it. A document row may stand for
#: several engine keys: "WASD" is one row and four bindings.
KEY_LABEL_TO_ENGINE_KEYS = {
    "LMB": ("LeftMouseButton",),
    "RMB": ("RightMouseButton",),
    "Left Shift": ("LeftShift",),
    "Spacebar": ("SpaceBar",),
    "Mouse wheel": ("MouseWheelAxis",),
    "Left stick": ("Gamepad_Left2D",),
    "WASD": ("W", "A", "S", "D"),
    "1": ("One",),
    "Q": ("Q",),
    "W": ("W",),
    "E": ("E",),
    "R": ("R",),
    "I": ("I",),
    "C": ("C",),
    "P": ("P",),
}

#: The words the document uses for an action, and the Input Action asset the
#: generator binds. Matching is on the document cell starting with the phrase,
#: so the table can carry an explanation after it.
ACTION_PHRASE_TO_INPUT_ACTION = {
    # The left mouse button. Its cell used to begin "Move to the point clicked",
    # because moving was the only thing it did. Since issues #1187 and #1188 it
    # attacks in both schemes, picks up in both, and moves in one, so the cell
    # begins with the job it always has and the rest of the sentence says the
    # rest. One phrase covers both tables.
    "Attack the enemy under the cursor": "IA_MoveToCursor",
    "Held: stand still": "IA_StandStill",
    "Heavy ability": "IA_SlotHeavy",
    "Special ability": "IA_SlotSpecial",
    "Support ability": "IA_SlotSupport",
    "Aura ability": "IA_SlotAura",
    "Ultimate ability": "IA_SlotUltimate",
    "Movement ability": "IA_SlotMovement",
    "Camera distance": "IA_Zoom",
    "Directional movement": "IA_Move",
    "Open and close the carried inventory": "IA_ToggleInventory",
    "Open and close the character creator": "IA_ToggleCharacterCreation",
    "Open and close the passive class tree": "IA_TogglePassiveTree",
}

#: The heading above each table in the document, and the list in the generator it
#: has to agree with.
SCHEMES = (
    ("Scheme 1: mouse movement", "MOUSE_MAPPINGS"),
    ("Scheme 2: keyboard movement (default)", "KEYBOARD_MAPPINGS"),
)


def bindings_from_the_generator(list_name: str) -> set[tuple[str, str]]:
    """The (engine key name, Input Action asset) pairs in one generator list.

    Read with `ast` because `tools/generate_input_assets.py` imports `unreal`,
    which does not exist outside the editor.
    """
    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        targets = [t.id for t in node.targets if isinstance(t, ast.Name)]
        if list_name not in targets:
            continue
        rows = ast.literal_eval(node.value)
        return {(key_name, action_name) for action_name, key_name, _ in rows}
    raise AssertionError(
        f"tools/generate_input_assets.py has no list called {list_name}. If it "
        "was renamed, update SCHEMES in this test."
    )


def bindings_from_the_design_document(heading: str) -> set[tuple[str, str]]:
    r"""The (engine key name, Input Action asset) pairs in one document table.

    The table is the first one after `heading`: a heading row, an alignment
    line, then one row per binding.

    THE HEADING ROW IS SKIPPED BY POSITION. Issue #238. The document used to
    carry the Google Docs export shape, which was an empty `|  |  |` line, an
    alignment line, and then the real heading as a body row with its bold
    markers escaped, so the heading was skipped by looking for `\*`. An ordinary
    Markdown heading is plain text, so nothing is collected until the alignment
    line has been passed.
    """
    text = DESIGN_DOC.read_text(encoding="utf-8")
    start = text.find(f"### **{heading}**")
    assert start != -1, (
        f'docs/Cataclysm_GDD_v2.md has no heading "{heading}". The control '
        "section was restructured; update SCHEMES in this test to match."
    )

    # Stop at the next heading of any level so one table cannot absorb the next.
    end = text.find("\n#", start + 1)
    section = text[start : end if end != -1 else len(text)]

    bindings: set[tuple[str, str]] = set()
    past_the_heading = False
    for line in section.splitlines():
        row = line.strip()
        if not row.startswith("|"):
            continue
        cells = [cell.strip() for cell in row.strip("|").split("|")]
        if len(cells) != 2:
            continue
        key_label, action_text = cells
        if key_label.startswith(":") or set(key_label) <= {"-", ":"}:
            past_the_heading = True  # the alignment line
            continue
        if not past_the_heading or not key_label:
            continue  # the heading row, above the alignment line

        engine_keys = KEY_LABEL_TO_ENGINE_KEYS.get(key_label)
        assert engine_keys is not None, (
            f'The table under "{heading}" has a row for {key_label!r}, which is '
            "not a key this test knows. Add it to KEY_LABEL_TO_ENGINE_KEYS, or "
            "correct the table."
        )

        matched = [
            action
            for phrase, action in ACTION_PHRASE_TO_INPUT_ACTION.items()
            if action_text.startswith(phrase)
        ]
        assert len(matched) == 1, (
            f'The action cell {action_text!r} under "{heading}" matches '
            f"{len(matched)} known actions, and it must match exactly one. The "
            "cell has to start with one of: "
            + ", ".join(sorted(ACTION_PHRASE_TO_INPUT_ACTION))
        )

        bindings.update((engine_key, matched[0]) for engine_key in engine_keys)
    assert bindings, f'No binding rows found in the table under "{heading}".'
    return bindings


def describe(bindings: set[tuple[str, str]]) -> str:
    return "\n  ".join(f"{key} -> {action}" for key, action in sorted(bindings))


@pytest.mark.parametrize(("heading", "list_name"), SCHEMES)
def test_the_document_lists_every_binding_that_is_built(heading: str, list_name: str) -> None:
    built = bindings_from_the_generator(list_name)
    documented = bindings_from_the_design_document(heading)
    missing = built - documented
    assert not missing, (
        f'These bindings are in {list_name} in tools/generate_input_assets.py but '
        f'not in the "{heading}" table in docs/Cataclysm_GDD_v2.md:\n  '
        + describe(missing)
    )


@pytest.mark.parametrize(("heading", "list_name"), SCHEMES)
def test_the_document_invents_no_binding(heading: str, list_name: str) -> None:
    built = bindings_from_the_generator(list_name)
    documented = bindings_from_the_design_document(heading)
    invented = documented - built
    assert not invented, (
        f'These bindings are in the "{heading}" table in '
        f"docs/Cataclysm_GDD_v2.md but nothing binds them in {list_name} in "
        "tools/generate_input_assets.py:\n  " + describe(invented)
    )


def test_no_section_still_calls_the_basic_attack_automatic() -> None:
    """The reverse of what this test used to guard, and why.

    IT USED TO ASSERT THAT THE LEFT MOUSE BUTTON DID NOT ATTACK. That was issue
    #138: the control table said the button fired the basic attack while the
    Combat System section said basic attacks were automatic, the two
    contradicted each other for a month, and the code followed the combat
    section.

    THE PROJECT OWNER REVERSED IT ON 2026-09-02, after playing the automatic
    attack. Issue #1187. So the contradiction this guards against is now pointed
    the other way: a section still calling the basic attack automatic is the
    stale one, and the control table is right.

    THE DOCUMENT IS A SNAPSHOT OF A GOOGLE DRIVE FOLDER. Editing `docs/` changes
    nothing about the design, so a failure here means the real document in Drive
    is out of date and the export was taken before it was fixed.
    """
    text = DESIGN_DOC.read_text(encoding="utf-8")

    # "Automatic" is an ordinary word and appears about several other systems --
    # the day clock, the empire's resolution, the save. Only a sentence tying it
    # to the basic attack is a claim about the control scheme.
    offenders = [
        line.strip()
        for line in text.splitlines()
        if re.search(r"basic attacks?\b", line, re.IGNORECASE)
        and re.search(r"\bautomatic", line, re.IGNORECASE)
    ]
    assert not offenders, (
        "A section still says the basic attack is automatic. It fires on the "
        "left mouse button since issue #1187, and the code agrees. Fix the real "
        "document in Google Drive and re-export it. Offending lines:\n  "
        + "\n  ".join(offenders)
    )


def test_the_support_ability_and_directional_movement_never_share_a_key() -> None:
    """The other half of issue #138: W was listed twice in one table.

    Checked against the generator rather than the document, because that is
    where the two schemes are defined, and then the document is checked against
    the generator by the tests above.
    """
    for _, list_name in SCHEMES:
        bindings = bindings_from_the_generator(list_name)
        for key, action in bindings:
            if action != "IA_SlotSupport":
                continue
            clash = (key, "IA_Move") in bindings
            assert not clash, (
                f"{list_name} in tools/generate_input_assets.py puts both the "
                f"Support ability and directional movement on {key}. One key "
                "cannot be both. See issue #138."
            )
