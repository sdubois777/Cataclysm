// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSharedBuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * "You cannot benefit from buffs applied by allies",
 * `Negative_You_cannot_benefit_from_buffs_applied_by_allies` in `game/Data/EnchantmentsNegative.csv`. Ruled 2026-10-19
 * by the coordinating session (Q3 option a, A1 to A5): every buff another character's code puts on the holder is
 * refused, by the three routes that exist, and none of the holder's own buffs is touched. The flag is
 * `ally_buffs_refused`, held by the RECEIVER and asked by `UCataclysmAbilitySystemComponent::RefusesBuffsFromAllies`.
 *
 * THE FLAG IS GIVEN BY HAND, as a worn row would give it: no row is authored. Every character's stat line is written
 * with `SetStatInputs`, a flat base per stat.
 *
 * THE CHARACTERS ARE REAL: player pawns with their ability system on a player state, as the game wires them, so each
 * is on the players' side and each is the other's ally. The one creature, which a self buff counts as burning, is on
 * the monsters' side. Nobody is refreshed, so nobody holds a class line. Every character stands at least 1 m from every
 * other: the giver at the origin, one ally 3 m along X and another 3 m along Y, 4.24 m apart, and the creature 10 m
 * the other way along X, outside every aura ring here and inside the self buff's 15 m count.
 *
 * NO WAIT ON A TICK. The shared buffs' step is called where the regeneration step would call it, and an aura's pulse
 * is called by hand; a test world is not ticked.
 */
namespace CataclysmAllyBuffsRefusedTest
{
	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float AllyBuffsRefusedMetre = 100.0f;

	/** Burning Wrath's row: 4% more damage for each burning enemy within 15 m. Granted with no element tag. */
	const TCHAR* const AllyBuffsRefusedSelfBuffRow = TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning");

	/** An aura giving the allies inside its 6 m ring 8% increased damage. Granted with no element tag. */
	const TCHAR* const AllyBuffsRefusedHelpingAuraRow = TEXT("Radius=6; Interval=1; AllyIncreasedDamage=8");

	/** An aura making its caster immune to three kinds, Living Pyre's three, in a 6 m ring. */
	const TCHAR* const AllyBuffsRefusedImmuneAuraRow = TEXT("Radius=6; Interval=1; Immune=Stun, Slow, Displacement");

	FVector AllyBuffsRefusedAt(float XMetres, float YMetres)
	{
		return FVector(XMetres * AllyBuffsRefusedMetre, YMetres * AllyBuffsRefusedMetre, 0.0f);
	}

	UCataclysmAbilitySystemComponent* AllyBuffsRefusedSystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** A player pawn with its ability system on a player state, holding 10000 mana for every cast a case makes. */
	ACataclysmPlayerCharacter* AllyBuffsRefusedPlayerAt(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Pawn = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!State || !Pawn)
		{
			return nullptr;
		}
		Pawn->SetPlayerState(State);
		Pawn->OnRep_PlayerState();
		if (UCataclysmAbilitySystemComponent* System = AllyBuffsRefusedSystemOf(Pawn))
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 10000.0f);
		}
		return Pawn;
	}

	/**
	 * A creature of the monsters' side 10 m along -X, set alight by `Giver`, so a self buff of the giver's cast after
	 * it grants 4% more damage: "4% more fire damage for every enemy currently burning within 15 meters".
	 */
	ACataclysmEnemyCharacter* AllyBuffsRefusedBurningCreature(UWorld* World, ACataclysmPlayerCharacter* Giver)
	{
		ACataclysmEnemyCharacter* Alight =
			World->SpawnActor<ACataclysmEnemyCharacter>(AllyBuffsRefusedAt(-10.0f, 0.0f), FRotator::ZeroRotator);
		if (Alight)
		{
			Alight->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Alight->SetHealth(100000.0f);
			UCataclysmSkillEffects::ApplyBurn(Giver, Alight, 100.0f,
				/*bScalesWithInstigator=*/true, /*bBurnIsDesigned=*/true);
		}
		return Alight;
	}

	/** Write this character's stat line, as worn rows would, replacing what it held: a flat base per stat. */
	void AllyBuffsRefusedHold(const AActor* Actor, const TArray<TPair<const TCHAR*, float>>& Lines)
	{
		UCataclysmAbilitySystemComponent* System = AllyBuffsRefusedSystemOf(Actor);
		if (!System)
		{
			return;
		}
		TMap<FName, FCataclysmStatInputs> Inputs;
		for (const TPair<const TCHAR*, float>& Line : Lines)
		{
			Inputs.FindOrAdd(FName(Line.Key)).Base = Line.Value;
		}
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** Give this character the drawback alone, the flag at a flat 1. */
	void AllyBuffsRefusedRefuse(const AActor* Actor)
	{
		AllyBuffsRefusedHold(Actor, {{UCataclysmAbilitySystemComponent::AllyBuffsRefusedStat, 1.0f}});
	}

	/** Grant a self buff in the Support slot, with no element tag, and use it. */
	UCataclysmSelfBuffSkill* AllyBuffsRefusedCastSelfBuff(ACataclysmPlayerCharacter* Giver)
	{
		UCataclysmAbilitySystemComponent* System = AllyBuffsRefusedSystemOf(Giver);
		if (!System)
		{
			return nullptr;
		}
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support, /*Level=*/100, Giver);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return nullptr;
		}
		Skill->SkillName = TEXT("Burning Wrath");
		Skill->Params = UCataclysmSkillShapes::ParseParams(AllyBuffsRefusedSelfBuffRow);
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT(""));
		System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		return Skill;
	}

	/** Grant an aura in the Aura slot with `ParamText` and no element tag, and switch it on. Null if it is not running. */
	UCataclysmAuraSkill* AllyBuffsRefusedRaiseAura(ACataclysmPlayerCharacter* Giver, const TCHAR* ParamText)
	{
		UCataclysmAbilitySystemComponent* System = AllyBuffsRefusedSystemOf(Giver);
		if (!System)
		{
			return nullptr;
		}
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			UCataclysmAuraSkill::StaticClass(), ECataclysmAbilitySlot::Aura, /*Level=*/100, Giver);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		UCataclysmAuraSkill* Aura = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Aura)
		{
			return nullptr;
		}
		Aura->SkillName = TEXT("A refusal test aura");
		Aura->Params = UCataclysmSkillShapes::ParseParams(ParamText);
		Aura->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT(""));
		System->TryActivateAbility(Aura->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		return Aura->IsActive() ? Aura : nullptr;
	}

	/** The sum of one bucket's modifiers this character carries on its own ability system. */
	float AllyBuffsRefusedCarried(const AActor* Actor, ECataclysmStatBucket Bucket)
	{
		float Sum = 0.0f;
		if (const UCataclysmAbilitySystemComponent* Its = AllyBuffsRefusedSystemOf(Actor))
		{
			for (const FCataclysmStatModifier& Modifier : Its->GetStatModifiers())
			{
				Sum += Modifier.Bucket == Bucket ? Modifier.Value : 0.0f;
			}
		}
		return Sum;
	}

	int32 AllyBuffsRefusedModifiersOn(const AActor* Actor)
	{
		const UCataclysmAbilitySystemComponent* Its = AllyBuffsRefusedSystemOf(Actor);
		return Its ? Its->GetStatModifiers().Num() : -1;
	}

	int32 AllyBuffsRefusedGrantsOn(const AActor* Actor)
	{
		const UCataclysmAbilitySystemComponent* Its = AllyBuffsRefusedSystemOf(Actor);
		return Its ? Its->GrantedImmunities.Num() : -1;
	}

	/** What a blow of 100 comes to through this character's own ability system, with no skill tags. */
	float AllyBuffsRefusedBlowOf100(const AActor* Actor)
	{
		return UCataclysmSkillEffects::ModifiedDamage(AllyBuffsRefusedSystemOf(Actor), 100.0f, FGameplayTagContainer());
	}
}

#define CATACLYSM_ALLY_BUFFS_REFUSED_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString&)

/**
 * The shared buffs' route, both of its kinds, asked in `CataclysmSharedBuffsDetail::LivingAlliesWithin`. A wearer of
 * `self_buff_shared_within_metres` at 8 running a self buff of 4% More gives a copy to the ally beside the refusing
 * one (the control) and none to the refuser. Then the same wearer holding only `nearby_allies_more_damage` at 15.
 */
CATACLYSM_ALLY_BUFFS_REFUSED_TEST(FCataclysmAllyBuffsRefusedSharedCopyTest,
	"Cataclysm.AllyBuffsRefused.ARefusingAllyGetsNoSharedCopyOfEitherKindAndOneBesideItDoes")
{
	using namespace CataclysmAllyBuffsRefusedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = AllyBuffsRefusedPlayerAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Refuser = AllyBuffsRefusedPlayerAt(World, AllyBuffsRefusedAt(3.0f, 0.0f));
	ACataclysmPlayerCharacter* Beside = AllyBuffsRefusedPlayerAt(World, AllyBuffsRefusedAt(0.0f, 3.0f));
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a refusing ally"), Refuser)
		|| !TestNotNull(TEXT("set-up: an ally beside it"), Beside)
		|| !TestNotNull(TEXT("set-up: something burning"), AllyBuffsRefusedBurningCreature(World, Wearer)))
	{
		return false;
	}
	AllyBuffsRefusedRefuse(Refuser);
	TestTrue(TEXT("set-up: the refusing ally refuses buffs from allies"),
		UCataclysmAbilitySystemComponent::RefusesBuffsFromAllies(Refuser));
	TestFalse(TEXT("set-up: the ally beside it does not"),
		UCataclysmAbilitySystemComponent::RefusesBuffsFromAllies(Beside));

	AllyBuffsRefusedHold(Wearer, {{UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat, 8.0f}});
	const UCataclysmSelfBuffSkill* Running = AllyBuffsRefusedCastSelfBuff(Wearer);
	if (!TestTrue(TEXT("set-up: the self buff runs"), Running && Running->IsActive())
		|| !TestEqual(TEXT("set-up: and grants its caster 4% More"), Running->GrantedIncrease, 4.0f, 0.001f))
	{
		return false;
	}
	TestEqual(TEXT("control: before the step the ally beside carries nothing"), AllyBuffsRefusedModifiersOn(Beside), 0);
	TestEqual(TEXT("control: nor does the refusing ally"), AllyBuffsRefusedModifiersOn(Refuser), 0);

	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("control: the ally beside, 3 m off, carries the self buff's 4% More"),
		AllyBuffsRefusedCarried(Beside, ECataclysmStatBucket::More), 4.0f, 0.001f);
	TestEqual(TEXT("the refusing ally, 3 m off, carries no copy"), AllyBuffsRefusedModifiersOn(Refuser), 0);
	TestEqual(TEXT("and the wearer holds one copy, the ally beside's"),
		AllyBuffsRefusedSystemOf(Wearer)->SharedBuffCopies.Num(), 1);

	AllyBuffsRefusedHold(Wearer, {{UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat, 15.0f}});
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("control: with only the nearby row at 15, the ally beside carries 15 More"),
		AllyBuffsRefusedCarried(Beside, ECataclysmStatBucket::More), 15.0f, 0.001f);
	TestEqual(TEXT("and one modifier, the self buff's copy having been taken back with its reach"),
		AllyBuffsRefusedModifiersOn(Beside), 1);
	TestEqual(TEXT("the refusing ally carries none of the nearby row either"), AllyBuffsRefusedModifiersOn(Refuser), 0);
	return true;
}

/**
 * Ruling A2 for the shared buffs: a copy already held is taken back by the step's own pass, the step after its holder
 * gains the flag, and given again the step after it loses it.
 */
CATACLYSM_ALLY_BUFFS_REFUSED_TEST(FCataclysmAllyBuffsRefusedCopyTakenBackTest,
	"Cataclysm.AllyBuffsRefused.ACopyHeldIsTakenBackTheStepAfterTheFlagAndGivenAgainTheStepAfterItGoes")
{
	using namespace CataclysmAllyBuffsRefusedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = AllyBuffsRefusedPlayerAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Ally = AllyBuffsRefusedPlayerAt(World, AllyBuffsRefusedAt(3.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: an ally"), Ally))
	{
		return false;
	}
	AllyBuffsRefusedHold(Wearer, {{UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat, 10.0f}});

	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("control: without the flag, the ally 3 m off carries the nearby row's 10 More"),
		AllyBuffsRefusedCarried(Ally, ECataclysmStatBucket::More), 10.0f, 0.001f);

	AllyBuffsRefusedRefuse(Ally);
	TestEqual(TEXT("with the flag written and no step yet, it still carries the 10"),
		AllyBuffsRefusedCarried(Ally, ECataclysmStatBucket::More), 10.0f, 0.001f);
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("the next step takes the copy back"), AllyBuffsRefusedModifiersOn(Ally), 0);
	TestEqual(TEXT("and the wearer holds no copy"), AllyBuffsRefusedSystemOf(Wearer)->SharedBuffCopies.Num(), 0);

	AllyBuffsRefusedHold(Ally, {});
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("with the flag gone, the next step gives the 10 again"),
		AllyBuffsRefusedCarried(Ally, ECataclysmStatBucket::More), 10.0f, 0.001f);
	TestEqual(TEXT("as one modifier"), AllyBuffsRefusedModifiersOn(Ally), 1);
	return true;
}

/**
 * The aura's ally bonus, asked in `UCataclysmAuraSkill::HelpAlliesInside`. A refusing ally inside the ring is not
 * helped and the ally beside it is (the control); and the helped one, given the flag, loses the bonus at the next
 * pulse (ruling A2), and has it again the pulse after the flag goes.
 */
CATACLYSM_ALLY_BUFFS_REFUSED_TEST(FCataclysmAllyBuffsRefusedAuraHelpTest,
	"Cataclysm.AllyBuffsRefused.AnAuraHelpsNoRefusingAllyInsideAndOneHelpedLosesItAtTheNextPulse")
{
	using namespace CataclysmAllyBuffsRefusedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Caster = AllyBuffsRefusedPlayerAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Refuser = AllyBuffsRefusedPlayerAt(World, AllyBuffsRefusedAt(3.0f, 0.0f));
	ACataclysmPlayerCharacter* Beside = AllyBuffsRefusedPlayerAt(World, AllyBuffsRefusedAt(0.0f, 3.0f));
	if (!TestNotNull(TEXT("set-up: a caster"), Caster) || !TestNotNull(TEXT("set-up: a refusing ally"), Refuser)
		|| !TestNotNull(TEXT("set-up: an ally beside it"), Beside))
	{
		return false;
	}
	AllyBuffsRefusedRefuse(Refuser);
	UCataclysmAuraSkill* Aura = AllyBuffsRefusedRaiseAura(Caster, AllyBuffsRefusedHelpingAuraRow);
	if (!TestNotNull(TEXT("set-up: the aura runs"), Aura))
	{
		return false;
	}
	TestEqual(TEXT("control: before its first pulse the aura helps nobody"), Aura->AlliesHelped(), 0);

	Aura->Pulse();
	TestTrue(TEXT("control: the ally beside, inside the 6 m ring, is helped"), Aura->IsHelping(Beside));
	TestEqual(TEXT("control: and carries the row's 8 increased"),
		AllyBuffsRefusedCarried(Beside, ECataclysmStatBucket::Increased), 8.0f, 0.001f);
	TestFalse(TEXT("the refusing ally, inside the ring, is not helped"), Aura->IsHelping(Refuser));
	TestEqual(TEXT("and carries no modifier"), AllyBuffsRefusedModifiersOn(Refuser), 0);
	TestEqual(TEXT("so the aura helps one"), Aura->AlliesHelped(), 1);

	AllyBuffsRefusedRefuse(Beside);
	TestEqual(TEXT("with the flag written on the helped ally and no pulse yet, it still carries the 8"),
		AllyBuffsRefusedCarried(Beside, ECataclysmStatBucket::Increased), 8.0f, 0.001f);
	Aura->Pulse();
	TestFalse(TEXT("the next pulse stops helping it"), Aura->IsHelping(Beside));
	TestEqual(TEXT("and takes its bonus back"), AllyBuffsRefusedModifiersOn(Beside), 0);
	TestEqual(TEXT("so the aura helps nobody"), Aura->AlliesHelped(), 0);

	AllyBuffsRefusedHold(Beside, {});
	Aura->Pulse();
	TestEqual(TEXT("with the flag gone, the next pulse gives it the 8 again"),
		AllyBuffsRefusedCarried(Beside, ECataclysmStatBucket::Increased), 8.0f, 0.001f);
	return true;
}

/**
 * The aura's shared immunities, asked in `UCataclysmAuraSkill::ShareImmunitiesWithAlliesInside`. A caster wearing
 * `aura_shares_immunities_with_allies` raises an aura naming three kinds: the ally beside the refusing one is granted
 * all three (the control) and the refuser none; and the granted one, given the flag, loses them at the next pulse
 * (ruling A2).
 */
CATACLYSM_ALLY_BUFFS_REFUSED_TEST(FCataclysmAllyBuffsRefusedAuraImmunityTest,
	"Cataclysm.AllyBuffsRefused.AnAuraSharesNoImmunityWithARefusingAllyAndOneHoldingThemLosesThemAtTheNextPulse")
{
	using namespace CataclysmAllyBuffsRefusedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Caster = AllyBuffsRefusedPlayerAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Refuser = AllyBuffsRefusedPlayerAt(World, AllyBuffsRefusedAt(3.0f, 0.0f));
	ACataclysmPlayerCharacter* Beside = AllyBuffsRefusedPlayerAt(World, AllyBuffsRefusedAt(0.0f, 3.0f));
	if (!TestNotNull(TEXT("set-up: a caster"), Caster) || !TestNotNull(TEXT("set-up: a refusing ally"), Refuser)
		|| !TestNotNull(TEXT("set-up: an ally beside it"), Beside))
	{
		return false;
	}
	AllyBuffsRefusedHold(Caster, {{UCataclysmAbilitySystemComponent::AuraSharesImmunitiesStat, 1.0f}});
	AllyBuffsRefusedRefuse(Refuser);
	UCataclysmAuraSkill* Aura = AllyBuffsRefusedRaiseAura(Caster, AllyBuffsRefusedImmuneAuraRow);
	if (!TestNotNull(TEXT("set-up: the aura runs"), Aura))
	{
		return false;
	}
	TestEqual(TEXT("control: before the first pulse the ally beside holds no granted immunity"),
		AllyBuffsRefusedGrantsOn(Beside), 0);

	Aura->Pulse();
	TestEqual(TEXT("control: the ally beside, inside the 6 m ring, is granted the row's three kinds"),
		AllyBuffsRefusedGrantsOn(Beside), 3);
	TestTrue(TEXT("control: so it is immune to a stun"), UCataclysmSkillTemplate::IsImmuneTo(Beside, TEXT("Stun")));
	TestEqual(TEXT("the refusing ally, inside the ring, is granted none"), AllyBuffsRefusedGrantsOn(Refuser), 0);
	TestFalse(TEXT("and is not immune to a stun"), UCataclysmSkillTemplate::IsImmuneTo(Refuser, TEXT("Stun")));
	TestFalse(TEXT("nor to a shove"), UCataclysmSkillTemplate::IsImmuneTo(Refuser, TEXT("Displacement")));

	AllyBuffsRefusedRefuse(Beside);
	TestEqual(TEXT("with the flag written on the granted ally and no pulse yet, it still holds the three"),
		AllyBuffsRefusedGrantsOn(Beside), 3);
	Aura->Pulse();
	TestEqual(TEXT("the next pulse takes all three back"), AllyBuffsRefusedGrantsOn(Beside), 0);
	TestFalse(TEXT("so it is no longer immune to a stun"), UCataclysmSkillTemplate::IsImmuneTo(Beside, TEXT("Stun")));
	TestEqual(TEXT("and the refusing ally still holds none"), AllyBuffsRefusedGrantsOn(Refuser), 0);
	return true;
}

/**
 * Ruling A3, and "none of the wearer's own": a giver holding the flag keeps its own self buff, which raises its own
 * blow, keeps its own aura's immunity, and still gives all three routes' buffs to the ally beside it.
 */
CATACLYSM_ALLY_BUFFS_REFUSED_TEST(FCataclysmAllyBuffsRefusedStillGivesTest,
	"Cataclysm.AllyBuffsRefused.ARefusersOwnBuffsAreUntouchedAndItStillGivesItsAlliesAllThree")
{
	using namespace CataclysmAllyBuffsRefusedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Giver = AllyBuffsRefusedPlayerAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Ally = AllyBuffsRefusedPlayerAt(World, AllyBuffsRefusedAt(3.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a refusing giver"), Giver) || !TestNotNull(TEXT("set-up: an ally"), Ally)
		|| !TestNotNull(TEXT("set-up: something burning"), AllyBuffsRefusedBurningCreature(World, Giver)))
	{
		return false;
	}
	AllyBuffsRefusedHold(Giver, {{UCataclysmAbilitySystemComponent::AllyBuffsRefusedStat, 1.0f},
								 {UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat, 15.0f},
								 {UCataclysmAbilitySystemComponent::AuraSharesImmunitiesStat, 1.0f}});
	TestTrue(TEXT("set-up: the giver refuses buffs from allies"),
		UCataclysmAbilitySystemComponent::RefusesBuffsFromAllies(Giver));

	const float BlowBefore = AllyBuffsRefusedBlowOf100(Giver);
	TestTrue(TEXT("control: before its self buff the giver's blow of 100 comes to something"), BlowBefore > 0.0f);
	const UCataclysmSelfBuffSkill* Running = AllyBuffsRefusedCastSelfBuff(Giver);
	if (!TestTrue(TEXT("set-up: the self buff runs"), Running && Running->IsActive())
		|| !TestEqual(TEXT("set-up: and grants its caster 4% More"), Running->GrantedIncrease, 4.0f, 0.001f))
	{
		return false;
	}
	TestEqual(TEXT("its own self buff makes its own blow 4% more, though it refuses buffs from allies"),
		AllyBuffsRefusedBlowOf100(Giver) / BlowBefore, 1.04f, 0.0001f);

	UCataclysmAuraSkill* Aura = AllyBuffsRefusedRaiseAura(
		Giver, TEXT("Radius=6; Interval=1; Immune=Stun; AllyIncreasedDamage=8"));
	if (!TestNotNull(TEXT("set-up: its aura runs"), Aura))
	{
		return false;
	}
	TestEqual(TEXT("control: before the pulse the ally carries nothing"), AllyBuffsRefusedModifiersOn(Ally), 0);
	TestEqual(TEXT("control: and holds no granted immunity"), AllyBuffsRefusedGrantsOn(Ally), 0);
	Aura->Pulse();
	TestTrue(TEXT("its own aura makes the giver itself immune to a stun"),
		UCataclysmSkillTemplate::IsImmuneTo(Giver, TEXT("Stun")));
	TestTrue(TEXT("and helps the ally 3 m off"), Aura->IsHelping(Ally));
	TestEqual(TEXT("which carries the aura's 8 increased"),
		AllyBuffsRefusedCarried(Ally, ECataclysmStatBucket::Increased), 8.0f, 0.001f);
	TestEqual(TEXT("and is granted the aura's one kind"), AllyBuffsRefusedGrantsOn(Ally), 1);

	UCataclysmSharedBuffs::Step(Giver);
	TestEqual(TEXT("the step gives the ally the nearby row's 15 More"),
		AllyBuffsRefusedCarried(Ally, ECataclysmStatBucket::More), 15.0f, 0.001f);
	TestEqual(TEXT("and the giver itself carries only its own self buff"), AllyBuffsRefusedModifiersOn(Giver), 1);
	return true;
}

#undef CATACLYSM_ALLY_BUFFS_REFUSED_TEST

#endif // WITH_AUTOMATION_TESTS
