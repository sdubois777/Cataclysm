"""The game awards empire upgrade points for a defeated dungeon as the simulation does.

WHY THIS EXISTS. Issue #52, the first slice (2026-11-03): defeating a dungeon now
earns empire upgrade points in the game, and the simulation has awarded them since
issue #1348. The rule is the owner's, ruled on 2026-09-06 and stated in
`docs/Cataclysm_GDD_v2.md`, Empire-Wide Upgrades:

    points awarded = base points for the dungeon type x (living population /
    total maximum population)

measured at the instant each dungeon is defeated, with no floor and no cap. The
design leaves the bases open ("tuning values and are not fixed here"); the
coordinating session ruled on 2026-11-03, under the owner's delegation of
unstated numbers, that the game takes the simulation's, 1, 3, 5 and 25. Two
copies of a number are two numbers, and `CLAUDE.md` records the power model
drifting from its source twice.

WHAT IS CHECKED, READ FROM THE TWO SOURCES AS TEXT, because continuous integration
compiles no C++:

    the four bases     `UCataclysmEmpireRun`'s constants against
                       `TuningConfig.empire_points_per_dungeon`, kind by kind
    the moment         both halves award after the dungeon leaves the map and
                       before a Fallen City retaken by it gets its people back
    only a defeat      neither half awards anywhere else: a resolve earns nothing

WHAT IS NOT CHECKED HERE. That the game's arithmetic gives the right figures, that
the bank keeps fractions, that it is saved and shown: the Unreal automation tests
under `Cataclysm.EmpirePoints.`. A test of a constant cannot notice that the code
ignores it.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
EMPIRE = REPO_ROOT / "game" / "Source" / "CataclysmEmpire" / "Empire"
RUN_HEADER = EMPIRE / "CataclysmEmpireRun.h"
RUN_SOURCE = EMPIRE / "CataclysmEmpireRun.cpp"
ENGINE = REPO_ROOT / "sim" / "cataclysm_sim" / "engine.py"

#: Each dungeon kind as the model names it, and the game's constant for its base.
BASES = {
    "BASIC": "BasicDungeonEmpirePoints",
    "QUEST": "QuestDungeonEmpirePoints",
    "FALLEN_CITY": "FallenCityEmpirePoints",
    "CATACLYSM": "CataclysmDungeonEmpirePoints",
}


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT).as_posix()} does not exist; "
                    f"if it was renamed, rename it here too.")
    return path.read_text(encoding="utf-8")


def code_only(text: str) -> str:
    """C++ with its comments taken out and its whitespace flattened, so a search
    cannot pass on a comment that names what it looks for."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    return " ".join(text.split())


def python_code_only(text: str) -> str:
    """Python with its `#` comments taken out. Docstrings stay; none of the
    phrases searched for below is in one."""
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def cpp_body(source: str, signature: str) -> str:
    """One C++ function, from its signature to the first closing brace in the
    first column, as code only."""
    parts = source.split(signature, 1)
    if len(parts) != 2:
        pytest.fail(f"could not find {signature} -- has it been renamed?")
    return code_only(parts[1].split("\n}", 1)[0])


def python_body(source: str, name: str) -> str:
    """One method of the model, from its `def` to the next one, as code only."""
    match = re.search(rf"\n    def {name}\(.*?(?=\n    def |\Z)", source, flags=re.DOTALL)
    if not match:
        pytest.fail(f"could not find Simulation.{name} in engine.py -- has it been renamed?")
    return python_code_only(match.group(0))


def game_base(header: str, name: str) -> float:
    match = re.search(rf"static\s+constexpr\s+double\s+{name}\s*=\s*(-?[0-9.]+)\s*;", header)
    if not match:
        pytest.fail(f"could not find {name} in {RUN_HEADER.name}; has it been renamed?")
    return float(match.group(1))


@pytest.fixture(scope="module")
def run_header() -> str:
    return read(RUN_HEADER)


@pytest.fixture(scope="module")
def run_source() -> str:
    return read(RUN_SOURCE).replace("\r\n", "\n")


@pytest.fixture(scope="module")
def engine() -> str:
    return read(ENGINE).replace("\r\n", "\n")


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim.config import TuningConfig
    return TuningConfig()


def test_every_kind_of_dungeon_has_the_same_base_in_both(run_header, model) -> None:
    """1, 3, 5 and 25, kind by kind, and no kind left out of either."""
    from cataclysm_sim.config import DungeonType

    model_kinds = {kind.name for kind in model.empire_points_per_dungeon}
    assert model_kinds == set(BASES), (
        f"the model gives bases to {sorted(model_kinds)} and this file ports "
        f"{sorted(BASES)}. A kind added to one side needs a base in the other.")
    assert {kind.name for kind in DungeonType} == set(BASES), (
        "the model has a dungeon kind this port does not know about")

    for kind, constant in BASES.items():
        modelled = model.empire_points_per_dungeon[DungeonType[kind]]
        built = game_base(run_header, constant)
        assert built == pytest.approx(modelled), (
            f"a {kind} dungeon is worth {modelled} empire points in the model and "
            f"{built} in the game ({constant}). The coordinating session ruled on "
            f"2026-11-03 that the game takes the model's bases.")


def test_every_base_is_reached_by_the_game(run_source) -> None:
    """Each constant is what its kind answers, so none is declared and ignored."""
    body = cpp_body(run_source, "double UCataclysmEmpireRun::EmpirePointsBaseFor(")
    for kind, constant in (("Basic", "BasicDungeonEmpirePoints"),
                           ("Quest", "QuestDungeonEmpirePoints"),
                           ("FallenCity", "FallenCityEmpirePoints"),
                           ("Cataclysm", "CataclysmDungeonEmpirePoints")):
        assert f"case ECataclysmDungeonType::{kind}: return {constant};" in body, (
            f"EmpirePointsBaseFor no longer answers {constant} for the {kind} kind.")


def test_both_halves_measure_the_living_share_of_every_city(run_source, engine) -> None:
    """Living people over what every city, fallen or not, would hold intact."""
    award = cpp_body(run_source, "double UCataclysmEmpireRun::EmpirePointsFor(")
    assert "EmpirePointsBaseFor(Type) * (LivingPopulation / FMath::Max(1.0, MaxPopulation))" in award, (
        "UCataclysmEmpireRun::EmpirePointsFor is no longer base x living / "
        "max(1, maximum), which is the owner's formula and the model's "
        "`base * self.empire.population_frac()`.")

    clear = cpp_body(run_source, "bool UCataclysmEmpireRun::ClearDungeon(")
    assert "Map->TotalPopulation()" in clear and "Map->TotalMaxPopulation()" in clear, (
        "ClearDungeon no longer measures the award with the map's living and "
        "maximum populations.")

    finish = python_body(engine, "_finish_current")
    assert "base * self.empire.population_frac()" in finish, (
        "the model's award is no longer `base * self.empire.population_frac()`; "
        "the game's port would then describe a rule the model no longer has.")


def test_both_halves_award_at_the_defeat_and_before_the_retake(run_source, engine) -> None:
    """After the dungeon leaves the map, before a retaken city gets people back.

    THE DESIGN: "The multiplier is measured at the instant each dungeon is
    defeated". Retaking a Fallen City gives it half its people back, so an award
    measured after the retake would pay for people the defeat restored.
    """
    clear = cpp_body(run_source, "bool UCataclysmEmpireRun::ClearDungeon(")
    removed = clear.find("RemoveDungeon(DungeonId)")
    awarded = clear.find("LastEmpirePointsAwarded = EmpirePointsFor(")
    retaken = clear.find("Map->Retake(CityId)")
    assert -1 not in (removed, awarded, retaken), (
        f"ClearDungeon no longer has all three of its removal ({removed}), its "
        f"award ({awarded}) and its retake ({retaken}).")
    assert removed < awarded < retaken, (
        "ClearDungeon awards empire points outside the order removal, award, "
        "retake. Measured after the retake, a Fallen City's defeat would pay for "
        "the people the retake gave back; measured before the removal, a "
        "refused clear could award.")

    finish = python_body(engine, "_finish_current")
    model_award = finish.find("self.empire_points += ")
    model_retake = finish.find("self._retake(")
    assert -1 not in (model_award, model_retake), (
        "Simulation._finish_current no longer both awards empire points and "
        "retakes a Fallen City's city.")
    assert model_award < model_retake, (
        "the model now awards after the retake; the game awards before it, so the "
        "two disagree on a Fallen City's defeat.")


def test_neither_half_awards_anywhere_but_a_defeat(run_source, engine) -> None:
    """A dungeon that resolves -- hits its city -- earns nothing in either."""
    code = code_only(run_source)
    assignments = [m.start() for m in re.finditer(r"LastEmpirePointsAwarded\s*=", code)]
    clear = cpp_body(run_source, "bool UCataclysmEmpireRun::ClearDungeon(")
    begin = cpp_body(run_source, "void UCataclysmEmpireRun::BeginForCharacter(")
    in_clear = len(re.findall(r"LastEmpirePointsAwarded\s*=", clear))
    in_begin = len(re.findall(r"LastEmpirePointsAwarded\s*=\s*0\.0\s*;", begin))
    assert in_clear == 2 and in_begin == 1 and len(assignments) == 3, (
        f"LastEmpirePointsAwarded is set {len(assignments)} times in "
        f"CataclysmEmpireRun.cpp: {in_clear} in ClearDungeon (its reset and its "
        f"award) and {in_begin} in BeginForCharacter are the only ones expected. "
        f"A dungeon that resolves must not award anything.")

    model_code = python_code_only(engine)
    assert model_code.count("self.empire_points += ") == 1, (
        "the model adds to empire_points in more than one place; only a defeat, "
        "in _finish_current, awards them.")
