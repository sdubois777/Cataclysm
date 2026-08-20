"""What Commander does in the engine must agree with what the design says it does.

Commander is the only effect in the game that makes a creature better. The
Succubus's aura Dominion grants it to every allied creature within 8 metres, and
nothing else grants it today.

WHICH IS AUTHORITATIVE. The design. `game/Data/StatusEffects.csv` carries the
effect's description, and that file is GENERATED from the Buffs sheet of
`docs/All_Things_Cataclysm.xlsx` -- so a change starts in the workbook and
`python tools/generate_datatables.py` writes the CSV. Never edit the CSV by hand.

WHY THESE READ SOURCE TEXT RATHER THAN RUNNING ANYTHING. Continuous integration
never builds the C++ and never opens the editor, so an automation test cannot run
on a pull request. `CataclysmEnemyCommanderTests.cpp` checks the arithmetic by
running it; these check that the numbers and the shape have not drifted, which is
what a pull request can see.

THE ONE THING THAT CANNOT BE CHECKED FROM TEXT is whether every creature really
takes the buff, because that depends on which virtual function each overrides.
`Cataclysm.Enemy.CommanderReachesEveryCreatureThatCanBeBuffed` does that by
spawning them. What IS checked here is that no creature overrides the `final`
one, which is the mistake that would opt a creature out.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

CHARACTER_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
ENEMY_HEADER = CHARACTER_DIR / "CataclysmEnemyCharacter.h"
ENEMY_SOURCE = CHARACTER_DIR / "CataclysmEnemyCharacter.cpp"
SUCCUBUS_HEADER = CHARACTER_DIR / "CataclysmSuccubusCharacter.h"
STATUS_EFFECTS = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
DECISIONS = REPO_ROOT / "docs" / "DECISIONS.md"

#: The two stats the project owner named on 2026-08-20. The design had said only
#: "20% increased stats", which named none.
BUFFED_STATS = ("movement speed", "attack speed")


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def without_comments(text: str) -> str:
    """C++ with every comment removed.

    A SOURCE-READING TEST MUST STRIP COMMENTS FIRST, or a sentence describing
    the thing it forbids passes for the thing itself. `test_the_final_keyword_is_
    what_makes_it_a_compile_error` below carries a negative control that proves
    this function is really doing it.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def commander_row() -> str:
    """The `Buff_Commander` row's description column."""
    for line in source(STATUS_EFFECTS).splitlines():
        if line.startswith("Buff_Commander,"):
            # Name,EffectKind,EffectName,Description,DurationSeconds,PercentOfHit
            return line.split(",")[3]
    pytest.fail(
        "game/Data/StatusEffects.csv has no Buff_Commander row. That file is "
        "generated from the Buffs sheet of docs/All_Things_Cataclysm.xlsx; if "
        "the effect was renamed, rename it there and run "
        "python tools/generate_datatables.py.")


def constant(name: str, path: pathlib.Path) -> float:
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        source(path))
    if match is None:
        pytest.fail(
            f"{path.name} has no "
            f"'static constexpr float {name} = <number>f;' line.")
    return float(match.group(1))


# --------------------------------------------------------------------------
# The number
# --------------------------------------------------------------------------

def test_the_increase_is_the_one_the_effect_table_states():
    """Twenty per cent, and the C++ must not carry a different figure."""
    description = commander_row()

    match = re.search(r"(\d+(?:\.\d+)?)\s*%", description)
    assert match is not None, (
        f"the Commander row reads {description!r} and states no percentage at "
        f"all, so nothing says what the buff is worth.")

    designed = float(match.group(1))
    written = constant("CommanderIncreasePercent", ENEMY_HEADER)

    assert written == pytest.approx(designed), (
        f"CommanderIncreasePercent is {written} and "
        f"game/Data/StatusEffects.csv says {designed}. That CSV is generated "
        f"from docs/All_Things_Cataclysm.xlsx and is authoritative.")


# --------------------------------------------------------------------------
# The two stats
# --------------------------------------------------------------------------

def test_the_effect_names_the_two_stats_rather_than_saying_stats():
    """The whole of issue #768 was that "20% increased stats" named none, so a
    description that goes back to saying "stats" has undone the decision."""
    description = commander_row().lower()

    for stat in BUFFED_STATS:
        assert stat in description, (
            f"the Commander row reads {commander_row()!r} and does not mention "
            f"{stat!r}. The project owner set movement speed and attack speed "
            f"on 2026-08-20; see docs/DECISIONS.md.")

    assert not re.search(r"increased stats\b", description), (
        f"the Commander row reads {commander_row()!r}, which says 'increased "
        f"stats' again. That is the wording issue #768 was opened about: it "
        f"names no stat, so nothing can be built from it.")


def test_the_design_document_says_the_same_two():
    """A design decision is not real until it is in docs/."""
    text = source(GDD).lower()

    dominion = text.find("**dominion grants commander**")
    assert dominion != -1, (
        "docs/Cataclysm_GDD_v2.md no longer has the paragraph describing what "
        "Dominion grants.")

    paragraph = text[dominion:dominion + 1600]
    for stat in BUFFED_STATS:
        assert stat in paragraph, (
            f"the Dominion paragraph of docs/Cataclysm_GDD_v2.md does not "
            f"mention {stat!r}, so the document and the effect table disagree "
            f"about what the aura does.")


def test_the_decision_and_its_reasoning_are_recorded():
    """`docs/DECISIONS.md` carries the reasoning the design documents do not."""
    text = source(DECISIONS)

    heading = "## 2026-08-20 — Commander increases movement speed and attack speed"
    assert heading in text, (
        f"docs/DECISIONS.md has no entry beginning {heading!r}. The rule is "
        f"that the log records why a decision was made, not only what.")

    entry = text[text.index(heading):]
    entry = entry[:entry.find("\n---")] if "\n---" in entry else entry

    # THE PART MOST WORTH KEEPING is why maximum health was ruled out, because
    # it is the option somebody will propose again.
    assert "maximum health" in entry.lower(), (
        "the decision entry does not say why maximum health was ruled out. "
        "That is the reasoning most likely to be needed again: current health "
        "does not rise with the maximum and is clamped down when the buff ends, "
        "so an ally walking in and out of the field would lose health.")


# --------------------------------------------------------------------------
# The shape that stops a creature opting out
# --------------------------------------------------------------------------

def test_the_buff_divides_the_interval_rather_than_multiplying_it():
    """The stored figure is seconds BETWEEN attacks and the buff is a SPEED, so
    20% more attack speed makes the number smaller. Getting it backwards makes a
    buffed creature slower, which still looks like the aura doing something."""
    text = without_comments(source(ENEMY_HEADER))

    match = re.search(
        r"SecondsBetweenAttacks\(\)\s*const\s+override\s+final\s*\{\s*"
        r"return\s+([^;]+);",
        text)
    assert match is not None, (
        "CataclysmEnemyCharacter.h no longer has a `final` "
        "SecondsBetweenAttacks that returns something. Without it every "
        "creature can override the function the buff is applied in.")

    body = " ".join(match.group(1).split())
    assert body == "DesignedSecondsBetweenAttacks() / CommanderMultiplier()", (
        f"SecondsBetweenAttacks returns {body!r}. It must DIVIDE the designed "
        f"interval by the multiplier: more attack speed means less time between "
        f"attacks.")


def test_the_final_keyword_is_what_makes_it_a_compile_error():
    """Six creatures overrode `SecondsBetweenAttacks` before the buff existed.
    Without `final` a seventh would opt itself out of every buff in silence."""
    text = without_comments(source(ENEMY_HEADER))

    assert re.search(r"SecondsBetweenAttacks\(\)\s*const\s+override\s+final",
                     text), (
        "ACataclysmEnemyCharacter::SecondsBetweenAttacks is no longer `final`. "
        "That keyword is the only thing stopping the next creature overriding "
        "it and quietly ignoring Commander.")

    # THE NEGATIVE CONTROL. The word `final` appears in the comments above that
    # line too, so a check that did not strip comments would pass even if the
    # keyword were deleted. This proves the stripping happened.
    assert "final" not in without_comments(
        "// SecondsBetweenAttacks() const override final\n"), (
        "without_comments is not removing comments, so every check in this "
        "file that relies on it is reading prose as if it were code.")


@pytest.mark.parametrize("creature", [
    "CataclysmAbyssalWardenCharacter",
    "CataclysmBruteCharacter",
    "CataclysmCorruptedSentinelCharacter",
    "CataclysmHellhoundCharacter",
    "CataclysmImpCharacter",
    "CataclysmSuccubusCharacter",
])
def test_no_creature_overrides_the_function_the_buff_is_applied_in(creature):
    """Each creature overrides `DesignedSecondsBetweenAttacks`, which is its own
    figure before any buff, and not `SecondsBetweenAttacks`, which is the one
    that applies the buff."""
    header = without_comments(source(CHARACTER_DIR / f"{creature}.h"))
    body = without_comments(source(CHARACTER_DIR / f"{creature}.cpp"))

    assert re.search(r"virtual\s+float\s+DesignedSecondsBetweenAttacks\(\)"
                     r"\s*const\s+override", header), (
        f"{creature}.h does not override DesignedSecondsBetweenAttacks, so "
        f"this creature attacks on the enemy base's default interval rather "
        f"than on its own designed one.")

    assert f"{creature}::DesignedSecondsBetweenAttacks() const" in body, (
        f"{creature}.cpp declares the override and does not define it.")

    assert not re.search(r"virtual\s+float\s+SecondsBetweenAttacks\(\)"
                         r"\s*const\s+override", header), (
        f"{creature}.h overrides SecondsBetweenAttacks. That is the function "
        f"the buff is applied in, so this creature would ignore Commander. "
        f"Override DesignedSecondsBetweenAttacks instead.")


# --------------------------------------------------------------------------
# Walk speed, which needs a different shape from the interval
# --------------------------------------------------------------------------

def test_the_designed_walk_speed_is_read_off_the_movement_component():
    """One copy of the number. Every creature already sets MaxWalkSpeed in its
    constructor, so a second declaration per creature could disagree with it."""
    body = without_comments(source(ENEMY_SOURCE))

    match = re.search(r"void ACataclysmEnemyCharacter::BeginPlay\(\)"
                      r"\s*\{(.*?)\n\}", body, re.DOTALL)
    assert match is not None, (
        "CataclysmEnemyCharacter.cpp no longer defines BeginPlay.")

    assert "DesignedWalkSpeedCmPerSecond = Movement->MaxWalkSpeed" in match.group(1), (
        "BeginPlay no longer records the designed walk speed off the movement "
        "component. Without it RefreshCommanderBuff has nothing to scale and a "
        "buffed creature never speeds up.")


def test_the_walk_speed_is_refreshed_every_frame_so_it_cannot_stick():
    """An interval is computed when it is asked for. Walk speed is a stored
    number the movement component reads every frame, so something has to write
    it when the buff lapses as well as when it lands."""
    body = without_comments(source(ENEMY_SOURCE))

    match = re.search(r"void ACataclysmEnemyCharacter::Tick\([^)]*\)"
                      r"\s*\{(.*?)\n\}", body, re.DOTALL)
    assert match is not None, (
        "CataclysmEnemyCharacter.cpp no longer defines Tick.")

    assert "RefreshCommanderBuff();" in match.group(1), (
        "Tick does not call RefreshCommanderBuff, so a creature whose buff "
        "expired rather than being taken away keeps walking fast for ever.")


def test_the_multiplier_is_read_from_the_tag_rather_than_a_stored_flag():
    """The tag is the single source of truth. A flag kept in step with it is a
    second copy that can disagree, and nothing would say which was right."""
    body = without_comments(source(ENEMY_SOURCE))

    match = re.search(r"float ACataclysmEnemyCharacter::CommanderMultiplier\(\)"
                      r"\s*const\s*\{(.*?)\n\}", body, re.DOTALL)
    assert match is not None, (
        "CataclysmEnemyCharacter.cpp no longer defines CommanderMultiplier.")

    assert "HasTag" in match.group(1), (
        "CommanderMultiplier does not read a gameplay tag. It must ask the "
        "creature what it is holding rather than keep a flag of its own.")


def test_the_succubus_still_grants_the_effect_this_reads():
    """Two names have to agree: what the Succubus grants and what the enemy base
    looks for. They are spelled out in different files."""
    granted = re.search(
        r"DominionEffectName\s*=\s*\n?\s*TEXT\(\"([^\"]*)\"\)\s*;",
        source(CHARACTER_DIR / "CataclysmSuccubusCharacter.cpp"))
    assert granted is not None, (
        "CataclysmSuccubusCharacter.cpp no longer defines DominionEffectName.")

    read_back = re.search(
        r"StatusTagFor\(\s*\n?\s*TEXT\(\"([^\"]*)\"\)\)",
        without_comments(source(ENEMY_SOURCE)))
    assert read_back is not None, (
        "CataclysmEnemyCharacter.cpp no longer asks StatusTagFor for an effect "
        "name, so nothing turns the tag into a number.")

    assert granted.group(1) == read_back.group(1), (
        f"the Succubus grants {granted.group(1)!r} and the enemy base looks "
        f"for {read_back.group(1)!r}. A buffed creature would hold a tag "
        f"nothing reads.")

    assert granted.group(1) == "Commander", (
        f"the effect is called {granted.group(1)!r} and every document and "
        f"table calls it Commander.")
