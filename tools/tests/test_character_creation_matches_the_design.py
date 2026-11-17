r"""The character creator's choices, checked against the design document.

WHY THIS EXISTS. Issue #50. `docs/Cataclysm_GDD_v2.md` section IV says a player
choosing a character "choose a starting weapon type and damage type, which
determines their initial skill set and first available passive class tree", and
the "Classes by Damage Type" section names the three classes each damage type
unlocks. Two of the three facts the creator needs are in generated data tables
and are therefore already checked:

    which weapon types exist            `game/Data/ItemBases.csv`
    which damage types each carries     `game/Data/WeaponSkills.csv`, and
                                        `test_damage_type_availability_matches_the_design.py`
                                        compares that against the document

The third is not in any table. Which three classes a damage type unlocks appears
only in the design document's prose tables, so
`UCataclysmCharacterCreation::ClassesByDamageType` is a copy of it written out in
C++. **A copy nothing checks is how a renamed class survives in the game for a
year**, which is the same reasoning that produced the weapon table's test.

WHAT IS ASSERTED HERE.

    the eight damage types the C++ names are the eight the document names
    each one's three classes are the document's three, spelled the same and in
      the document's order
    twenty-four distinct classes, three to a damage type, with no class shared
    the Shield is excluded from the weapon types the creator offers, and the
      design document states the reason the exclusion rests on
    every other weapon type in the item bases table IS offered
    the pair a character has when nobody has chosen is itself a legal choice
    the control tables carry the key that opens the creator

HOW THE C++ IS READ. As text, with both kinds of comment stripped first. That is
not tidiness: the reasoning in this project lives in `/** ... */` blocks and
those blocks quote the very names being searched for. See `without_comments`.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
CREATION_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmCharacterCreation.cpp")
ITEM_BASES = REPO_ROOT / "game" / "Data" / "ItemBases.csv"
WEAPON_SKILLS = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"

#: The heading of the design document section holding the eight class tables.
CLASSES_HEADING = "## **Classes by Damage Type**"

#: The heading that ends it.
NEXT_HEADING = "## **Character Stats and Attributes**"

#: The one weapon type the creator does not offer, and why.
SHIELD = "Shield"

#: The WeaponType the weapon skill matrix uses for skills that do not care what
#: is held -- the auras. Counting it would give every weapon every damage type.
WEAPON_INDEPENDENT = "All"


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return path.read_text(encoding="utf-8")


def without_comments(text: str) -> str:
    """The C++ with both kinds of comment removed.

    THE SAME HELPER `test_enemy_score_port.py` NEEDED, and for the same reason.
    `ClassesByDamageType`'s own comment names the design document section this
    file reads, and a block comment elsewhere in the file spells out
    `Shield` in a sentence about why it is excluded. Searching the raw text
    would find both and prove nothing.

    Line breaks inside a block comment are kept, so line numbers still line up.
    """
    kept: list[str] = []
    rest = text
    while True:
        start = rest.find("/*")
        if start == -1:
            kept.append(rest)
            break
        kept.append(rest[:start])
        end = rest.find("*/", start + 2)
        if end == -1:
            break
        kept.append("\n" * rest.count("\n", start, end + 2))
        rest = rest[end + 2:]

    joined = "".join(kept)
    return "\n".join(line.split("//")[0] for line in joined.splitlines())


def rows_of(path: pathlib.Path) -> list[dict]:
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def in_the_document() -> dict[str, list[str]]:
    """Damage type to its three class names, in the order the document lists.

    The section is eight `### **Name**` subsections, each followed by a table
    whose first column is a class name.
    """
    text = read(GDD)
    start = text.find(CLASSES_HEADING)
    assert start != -1, (
        f"{GDD.name} has no {CLASSES_HEADING!r} section. If it was renamed, "
        "update CLASSES_HEADING in this test."
    )
    end = text.find(NEXT_HEADING, start)
    assert end != -1, (
        f"{GDD.name} has no {NEXT_HEADING!r} after the classes section, so "
        "this test cannot tell where the section stops."
    )

    section = text[start:end]
    found: dict[str, list[str]] = {}
    damage_type: str | None = None

    for line in section.splitlines():
        stripped = line.strip()

        heading = re.fullmatch(r"### \*\*(.+?)\*\*", stripped)
        if heading:
            damage_type = heading.group(1).strip()
            found[damage_type] = []
            continue

        if damage_type is None or not stripped.startswith("|"):
            continue

        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if len(cells) < 2:
            continue

        # SKIP THE HEADING ROW AND THE ALIGNMENT ROW. The heading row's first
        # cell is the word "Class"; the alignment row is colons and dashes.
        first = cells[0]
        if not first or first == "Class" or set(first) <= set(":- "):
            continue

        found[damage_type].append(first)

    return found


@pytest.fixture(scope="module")
def in_the_cpp() -> dict[str, list[str]]:
    """Damage type to its three class names, as the C++ states them."""
    text = without_comments(read(CREATION_CPP))

    found: dict[str, list[str]] = {}
    pattern = re.compile(
        r'Built\.Add\(TEXT\("([^"]+)"\),\s*\{(.*?)\}\s*\);', re.DOTALL)
    for match in pattern.finditer(text):
        classes = re.findall(r'TEXT\("([^"]+)"\)', match.group(2))
        found[match.group(1)] = classes

    assert found, (
        "No Built.Add(TEXT(\"...\"), {...}) entries were found in "
        f"{CREATION_CPP.name}. If ClassesByDamageType was rewritten in another "
        "shape, update the pattern in this test rather than deleting it."
    )
    return found


@pytest.fixture(scope="module")
def offered_weapon_types() -> set[str]:
    """The weapon types the creator offers, worked out the way the C++ does."""
    return {row["WeaponType"].strip() for row in rows_of(ITEM_BASES)
            if row["WeaponType"].strip() and row["WeaponType"].strip() != SHIELD}


def test_the_cpp_names_the_same_eight_damage_types_as_the_document(
        in_the_document, in_the_cpp):
    assert set(in_the_cpp) == set(in_the_document), (
        "The damage types the character creator knows about and the ones the "
        "design document's Classes by Damage Type section lists disagree.\n"
        f"  only in {CREATION_CPP.name}: "
        f"{sorted(set(in_the_cpp) - set(in_the_document))}\n"
        f"  only in {GDD.name}: "
        f"{sorted(set(in_the_document) - set(in_the_cpp))}"
    )


def test_every_damage_type_unlocks_the_documents_three_classes(
        in_the_document, in_the_cpp):
    for damage_type, documented in sorted(in_the_document.items()):
        assert in_the_cpp.get(damage_type) == documented, (
            f"{damage_type} unlocks different classes in the two places.\n"
            f"  {CREATION_CPP.name}: {in_the_cpp.get(damage_type)}\n"
            f"  {GDD.name}:          {documented}\n"
            "The order matters as well as the names: the creator shows them in "
            "the order the document prints them."
        )


def test_there_are_twenty_four_classes_and_none_is_shared(in_the_cpp):
    every = [name for classes in in_the_cpp.values() for name in classes]

    assert len(every) == 24, (
        f"The design gives 8 damage types 3 classes each, which is 24. "
        f"{CREATION_CPP.name} names {len(every)}."
    )
    assert len(set(every)) == 24, (
        "Two damage types unlock a class of the same name. A class belongs to "
        "exactly one damage type, so this is a copying mistake rather than a "
        f"design: {sorted(name for name in set(every) if every.count(name) > 1)}"
    )


def test_the_shield_is_not_offered_as_a_starting_weapon(offered_weapon_types):
    assert SHIELD not in offered_weapon_types

    # THE EXCLUSION RESTS ON A SENTENCE IN THE DESIGN, so the sentence is
    # checked. If the Shield is ever given attack damage, this fails and the
    # exclusion should be reconsidered rather than quietly kept.
    text = read(GDD)
    assert "grants no attack damage" in text, (
        "The character creator leaves the Shield out because the design says it "
        "grants no attack damage, and every skill's damage is a percentage of "
        "weapon damage. That sentence is no longer in "
        f"{GDD.name}, so the exclusion has lost its reason."
    )


def test_every_other_weapon_type_is_offered(offered_weapon_types):
    in_the_table = {row["WeaponType"].strip() for row in rows_of(ITEM_BASES)
                    if row["WeaponType"].strip()}

    assert offered_weapon_types == in_the_table - {SHIELD}
    assert len(offered_weapon_types) == 13, (
        "The design document's Weapon Types section lists 14, of which the "
        f"Shield is not offered. {ITEM_BASES.name} produces "
        f"{len(offered_weapon_types)} offered: {sorted(offered_weapon_types)}"
    )


def test_the_pair_a_character_has_by_default_is_itself_a_legal_choice(
        offered_weapon_types):
    """The two stand-ins the creator replaces must be choosable.

    WHY IT MATTERS. `ACataclysmPlayerState::GetChosenWeaponType` answers
    `DefaultWeaponType` for a character nobody has chosen for, and every
    automation test in the project stands up such a character. If that pair
    were not one the creator would allow, then the character every other test
    exercises would be a character the game says cannot exist.
    """
    text = without_comments(read(CREATION_CPP))

    def constant(name: str) -> str:
        match = re.search(
            rf'UCataclysmCharacterCreation::{name}\s*=\s*TEXT\("([^"]+)"\)', text)
        assert match, f"{CREATION_CPP.name} no longer defines {name}."
        return match.group(1)

    weapon = constant("DefaultWeaponType")
    damage = constant("DefaultDamageType")

    assert weapon in offered_weapon_types, (
        f"The default weapon type is {weapon}, which the creator does not "
        "offer."
    )

    carried = {row["DamageType"].strip() for row in rows_of(WEAPON_SKILLS)
               if row["WeaponType"].strip() == weapon}
    assert damage in carried, (
        f"The default pair is {weapon} and {damage}, but "
        f"{WEAPON_SKILLS.name} says a {weapon} carries {sorted(carried)}."
    )


def test_every_native_input_action_is_declared_in_the_cpp():
    r"""Four things have to agree about a key and a test already checks three.

    `test_controls_table_matches_the_input_assets.py` compares the design
    document's two control tables against the generator's two mapping lists, so
    it covers the key itself and which action the key triggers. What it cannot
    see is whether the C++ knows that action's NAME. A native action is bound by
    name -- `UCataclysmInputConfig` matches the string on the asset against
    `CataclysmInputActionNames` -- so a generated Input Action whose name the
    header does not carry is an asset the controller never binds, and the key
    does nothing with nothing reported.

    IT CHECKS ALL OF THEM RATHER THAN THE NEW ONE. The first version of this
    asserted that the string `ToggleCharacterCreation` appeared somewhere in the
    header, and `tools/prove_guard.py` showed that renaming the declaration to
    `ToggleCharacterCreationX` still passed, because the old name is a substring
    of the new one. Reading the generator's list and requiring an exact
    declaration for each cannot pass that way, and covers the other five as
    well.
    """
    import ast

    generator = REPO_ROOT / "tools" / "generate_input_assets.py"
    config = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Input"
              / "CataclysmInputConfig.h")

    tree = ast.parse(read(generator))
    native = None
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
                isinstance(t, ast.Name) and t.id == "NATIVE_ACTIONS"
                for t in node.targets):
            # NOT `ast.literal_eval`, WHICH RAISES ON THIS LIST. Each row's
            # third element is `BOOLEAN`, `AXIS1D` or `AXIS2D` -- names bound
            # further up the module to `unreal.InputActionValueType` members --
            # and a name is not a literal. `test_controls_table_matches_the_
            # input_assets.py` can use literal_eval because the two mapping
            # lists it reads are strings and empty lists throughout.
            #
            # THE SECOND ELEMENT IS THE ACTION NAME and it is a plain string, so
            # each row is read element by element and the rows are taken only
            # when that element really is one.
            native = []
            for element in node.value.elts:
                if not isinstance(element, ast.Tuple) or len(element.elts) < 2:
                    continue
                action = element.elts[1]
                if isinstance(action, ast.Constant) and isinstance(action.value, str):
                    native.append(action.value)
            break

    assert native, (
        f"{generator.name} has no NATIVE_ACTIONS list of (asset, name, type, "
        "display) rows. If it was renamed or reshaped, update this test rather "
        "than deleting it."
    )

    text = without_comments(read(config))
    for action_name in native:
        # THE C++ IDENTIFIER AND THE STRING IT HOLDS, BOTH, and allowing a line
        # break between them because a long name is wrapped in the header.
        declared = re.search(
            rf'inline\s+const\s+FName\s+{re.escape(action_name)}\s*=\s*'
            rf'\s*FName\(TEXT\("{re.escape(action_name)}"\)\);',
            text)
        assert declared, (
            f"{generator.name} generates an Input Action for {action_name!r}, "
            f"and {config.name} has no matching\n"
            f'    inline const FName {action_name} = '
            f'FName(TEXT("{action_name}"));\n'
            "so ACataclysmPlayerController cannot bind it and the key does "
            "nothing."
        )
