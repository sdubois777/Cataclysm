"""The player's kill handler reaches Reclaimed, Claimed and Death's Toll in the order ruled.

WHY THIS EXISTS. The Soul Collector's Reclaimed, Claimed and Death's Toll (the Death trees' slice C4b2, 2026-11-25) are
called from `ACataclysmPlayerCharacter::OnSomethingDied`. One Unreal test drives it with a whole player
(`Cataclysm.KillCredit.AMinionsKillPaysTheRealPlayerUnderDeathsToll`); the others call the three directly. Continuous
integration compiles no C++, so this reads the order from the source with comments and strings removed.

WHAT IS ASSERTED. An own kill gives back decay before it is paid; Claimed is asked only for a death this character did
not make and that Death's Toll did not make its own, so the two never both pay; both decays record what they took, as
ruled; and a follower's kill raises `kill` only to rows whose trigger key begins with the Soul Collector's prefix.
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


def test_an_own_kill_gives_back_decay_before_it_is_paid():
    body = bodies(HANDLER)["ACataclysmPlayerCharacter::OnSomethingDied"]
    given = body.find("UCataclysmFervour::GiveBackDecayOnKill(Acting);")
    paid = body.find("UCataclysmFervour::GainOnOwnKill(Acting, Notice.Victim);")
    assert body.count("UCataclysmFervour::GiveBackDecayOnKill(Acting);") == 1 and 0 <= given < paid, (
        "the kill handler must give back Reclaimed's decay once, before it pays the kill")


def test_claimed_is_asked_only_when_no_kill_was_credited():
    body = bodies(HANDLER)["ACataclysmPlayerCharacter::OnSomethingDied"]
    guarded = re.search(
        r"if \( ?Notice\.Killer != this && Notice\.Victim && "
        r"!UCataclysmFervour::CreditAFollowersKill\( ?Acting, this, Notice ?\) ?\) "
        r"\{ UCataclysmFervour::ClaimOnDeath\( ?Acting, Notice\.Victim ?\); \}", body)
    assert guarded, ("Claimed must be asked only for a death this character did not make and that Death's Toll did "
                     "not credit to it, so the two never both pay")
    assert body.count("UCataclysmFervour::ClaimOnDeath(") == 1
    assert body.count("UCataclysmFervour::CreditAFollowersKill(") == 1


def test_both_decays_record_what_they_took():
    every = bodies(FERVOUR)
    for name in ("UCataclysmFervour::DecayStep", "UCataclysmFervour::KillDecayStep"):
        assert "NoteFervourDecayed(" in every[name], f"{name} must record what it took, for Reclaimed"


def test_a_followers_kill_raises_kill_to_the_soul_collectors_rows_only():
    credit = bodies(FERVOUR)["UCataclysmFervour::CreditAFollowersKill"]
    assert re.search(r"ActOnEvent\(.*\*Prefix ?\)", credit) and "SoulCollectorRowKeyPrefix()" in credit, (
        "Death's Toll must raise kill with the Soul Collector's prefix")
    act = bodies(ASC)["UCataclysmAbilitySystemComponent::ActOnEvent"]
    assert re.search(
        r"if \( ?OnlyRowsWhoseKeyBegins && !Listed\.TriggerKey\.ToString\(\)\.StartsWith\( ?OnlyRowsWhoseKeyBegins ?\) ?\)"
        r" \{ continue; \}", act), "ActOnEvent must skip a row whose trigger key does not begin with the prefix asked"
    prefix = bodies(FERVOUR, keep_strings=True)["UCataclysmFervour::SoulCollectorRowKeyPrefix"]
    assert 'EnchantmentNameFor(FName(TEXT("SoulCollector_")), 0)' in prefix, (
        "the prefix must come from the one place that names a passive row's enchantment")
