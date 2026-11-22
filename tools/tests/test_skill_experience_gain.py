"""The stat that raises the experience every skill earns: `skill_experience_gain`.

WHY THIS EXISTS. Issue #2803. The project owner, 2026-11-21: "we need to create a new gem that increases the exp gain
for skills". The coordinating session ruled the gem Of The Scholar, "Increases skill experience gain by 10%", and a
new stat it writes, read with no skill tags so that every skill's share of a kill rises alike, while the character's
own experience does not read it. THE GEM DOES NOT ACT UNTIL GEMS CAN BE SOCKETED (#46); the stat and its reader are
built now.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so `Cataclysm.SkillLevels.` and the exemption
probe never run on a pull request, and where the kill hands its experience over is where no automation test reaches.

WHAT IS CHECKED:

  1. `SkillExperienceGainFactor` asks `skill_experience_gain` through `StatAppliedTo` over a base of 100 with no skill
     tags, and `GrantSkillExperience` multiplies the kill by it before sharing it;
  2. the stat is on the list of stats with no attribute, and the exemption test has a probe for it;
  3. the character's own grant does not read it, and the kill still hands both grants the experience after the
     character's own gain;
  4. the design document says what the stat does and that the gem waits on #46.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
STATE = SOURCE / "Player" / "CataclysmPlayerState.cpp"
ENEMY = SOURCE / "Character" / "CataclysmEnemyCharacter.cpp"
EXEMPTION_TESTS = SOURCE / "Tests" / "CataclysmStatExemptionTests.cpp"
DESIGN = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

STAT = "skill_experience_gain"


def code(path: pathlib.Path) -> str:
    """A source file with its comments removed."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def body(text: str, signature: str, where: str) -> str:
    """The braces-matched body of the function whose signature opens at `signature`."""
    start = text.find(signature)
    assert start != -1, f"{where} no longer defines {signature!r}"
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} in {where} is not closed")


def test_the_factor_reads_the_stat_with_no_tags_and_the_kill_is_raised_before_it_is_shared() -> None:
    state = code(STATE)
    assert f'ACataclysmPlayerState::SkillExperienceGainStat = TEXT("{STAT}");' in state, (
        f"the stat's name is no longer {STAT!r}")
    factor = body(state, "double ACataclysmPlayerState::SkillExperienceGainFactor()", "CataclysmPlayerState.cpp")
    asked = re.search(r"StatAppliedTo\(\s*FName\(SkillExperienceGainStat\),\s*FGameplayTagContainer\(\),\s*100\.0f\)",
                      factor)
    assert asked, "the factor no longer asks skill_experience_gain over a base of 100 with no skill tags"
    assert "/ 100.0" in factor, "the factor is no longer the percentage over 100"

    grant = body(state, "void ACataclysmPlayerState::GrantSkillExperience(", "CataclysmPlayerState.cpp")
    raised = grant.find("* SkillExperienceGainFactor()")
    shared = grant.find("SkillExperience.FindOrAdd(Each.Key) +=")
    assert -1 < raised < shared, "the kill is no longer multiplied by the factor before it is shared"
    assert re.search(r"SkillExperience\.FindOrAdd\(Each\.Key\) \+= Raised \* Each\.Value / Whole;", grant), (
        "a skill's share is no longer taken from the raised kill")


def test_the_stat_has_no_attribute_and_a_probe_that_reads_it() -> None:
    assert STAT in gen.stats_with_no_attribute(), f"{STAT} is not on UCataclysmPlayerClassStats::StatsWithNoAttribute()"
    probes = code(EXEMPTION_TESTS)
    assert f'{{TEXT("{STAT}"), &ProbeSkillExperienceGain}}' in probes, f"no probe for {STAT} in the exemption test"


def test_the_characters_own_grant_does_not_read_it_and_the_kill_hands_both_the_gained_experience() -> None:
    state = code(STATE)
    for signature in ("int64 ACataclysmPlayerState::ExperienceAfterGain(",
                      "int32 ACataclysmPlayerState::GrantExperience("):
        assert "SkillExperienceGain" not in body(state, signature, "CataclysmPlayerState.cpp"), (
            f"{signature} reads the skills' stat; the character's own experience must not")

    death = body(code(ENEMY), "void ACataclysmEnemyCharacter::HandleDeath()", "CataclysmEnemyCharacter.cpp")
    assert re.search(r"State->GrantSkillExperience\(State->ExperienceAfterGain\(", death), (
        "the kill no longer hands the skills the experience after the character's own gain")


def test_the_design_says_what_the_stat_does_and_that_the_gem_waits_on_sockets() -> None:
    text = DESIGN.read_text(encoding="utf-8")
    start = text.index("## **What a Skill Levels**")
    section = text[start:text.index("\n## ", start + 1)]
    assert f"`{STAT}`" in section, f"'What a Skill Levels' does not name {STAT}"
    assert "Of The Scholar" in section and "#46" in section, (
        "'What a Skill Levels' does not say the gem Of The Scholar waits on #46 to act")
