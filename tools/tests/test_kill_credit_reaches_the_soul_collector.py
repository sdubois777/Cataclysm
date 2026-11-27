"""The player's kill handler reaches Death's Toll's follower kill as ruled.

WHY THIS EXISTS. The Soul Collector's Death's Toll (the Death trees' slice C4b2, 2026-11-25), granted by the starting
node since the keystone rework (issue #2951), is called from `ACataclysmPlayerCharacter::OnSomethingDied`. One Unreal
test drives it with a whole player (`Cataclysm.KillCredit.AMinionsKillPaysTheRealPlayerUnderDeathsToll`); the others
call it directly. Continuous integration compiles no C++, so this reads the source with comments and strings removed.
Reclaimed and Claimed, which this also checked, were retired by issue #2951.

WHAT IS ASSERTED. A follower's kill is credited only for a death this character did not make; both decays record what
they took, which Unquiet Souls and Grave Patience act on; and a follower's kill raises `kill` only to rows whose trigger
key begins with the Soul Collector's prefix.
"""

from __future__ import annotations

import collections
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "game" / "Source" / "Cataclysm"
HANDLER = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"
FERVOUR = SOURCE / "AbilitySystem" / "CataclysmFervour.cpp"
ASC = SOURCE / "AbilitySystem" / "CataclysmAbilitySystemComponent.cpp"

#: A function definition at the start of a line: a return type, then `Class::Name(`.
DEFINITION = re.compile(r"^[A-Za-z_][\w:<>,\*&\s]*?\b((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)\s*\(")


def code_only(text: str, keep_strings: bool = False) -> str:
    """Comments removed, keeping every line where it was; string contents too unless asked to keep them."""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text if keep_strings else re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)


def bodies(path: pathlib.Path, keep_strings: bool = False) -> dict[str, str]:
    """The code of each function in a file, keyed by Class::Name, with whitespace runs made one space."""
    found: dict[str, list[str]] = collections.defaultdict(list)
    function = ""
    for line in code_only(path.read_text(encoding="utf-8"), keep_strings).splitlines():
        match = DEFINITION.match(line)
        if match:
            function = match.group(1)
        found[function].append(line)
    return {name: re.sub(r"\s+", " ", "\n".join(lines)) for name, lines in found.items()}


def test_a_followers_kill_is_credited_only_for_a_death_this_character_did_not_make():
    body = bodies(HANDLER)["ACataclysmPlayerCharacter::OnSomethingDied"]
    guarded = re.search(
        r"if \( ?Notice\.Killer != this && Notice\.Victim ?\) "
        r"\{ UCataclysmFervour::CreditAFollowersKill\( ?Acting, this, Notice ?\); \}", body)
    assert guarded, "a follower's kill must be credited only for a death this character did not make"
    assert body.count("UCataclysmFervour::CreditAFollowersKill(") == 1


def test_both_decays_record_what_they_took():
    every = bodies(FERVOUR)
    for name in ("UCataclysmFervour::DecayStep", "UCataclysmFervour::KillDecayStep"):
        assert "NoteFervourDecayed(" in every[name], f"{name} must record what it took, for Unquiet Souls and Grave Patience"


def test_a_followers_kill_raises_kill_to_the_soul_collectors_rows_only():
    credit = bodies(FERVOUR)["UCataclysmFervour::CreditAFollowersKill"]
    assert re.search(r"ActOnEvent\(.*\*Prefix ?\)", credit) and "SoulCollectorRowKeyPrefix()" in credit, (
        "Death's Toll must raise kill with the Soul Collector's prefix")
    act = bodies(ASC)["UCataclysmAbilitySystemComponent::ActOnEvent"]
    assert re.search(
        r"if \( ?OnlyRowsWhoseKeyBegins && !Listed\.TriggerKey\.ToString\(\)\.StartsWith\( ?OnlyRowsWhoseKeyBegins ?\) ?\)"
        r" \{ continue; \}", act), "ActOnEvent must skip a row whose trigger key does not begin with the prefix asked"
    assert re.search(r"BurstTheSlain\( ?Commander, Notice\.Victim, Notice\.Location, SlainMaximum, "
                     r"Cataclysm->TakePendingSlainBurstSharePercent\(\) ?\);", credit), (
        "a follower's kill Death's Toll makes the character's must make The Ferryman's burst its rows asked for")
    prefix = bodies(FERVOUR, keep_strings=True)["UCataclysmFervour::SoulCollectorRowKeyPrefix"]
    assert 'EnchantmentNameFor(FName(TEXT("SoulCollector_")), 0)' in prefix, (
        "the prefix must come from the one place that names a passive row's enchantment")
