// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSharedBuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
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
 * "Buff effects you apply are 20%-40% more potent", ruled 2026-10-13 under the owner's delegation, where a buff
 * reaches allies: THE CASTER'S POTENCY TRAVELS WITH THE COPY, and the ally's own is not applied again. And the
 * standing "Nearby allies gain 10-20% more damage", which a row lays and no skill does, is not raised.
 *
 * THE RIG IS THE ONE `CataclysmSharedBuffsTests.cpp` USES: a real player character as the caster, creatures on
 * the players' team as its allies, and `UCataclysmSharedBuffs::Step` called directly where the regeneration step
 * would have called it. The stat lines are written by hand. Nothing waits. The tests of the stat on a self buff's
 * own figure and on an aura's ally bonus are in `CataclysmSkillTemplateTests.cpp`, in the same group.
 *
 * EACH FIGURE WITH THE ROW IS COMPARED WITH THE SAME FIGURE READ WITHOUT IT. The caster with no row stands at the
 * origin and the wearer 100 m along X, so neither reaches the other's ally.
 */
namespace CataclysmBuffPotencySharedTest
{
	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float M = 100.0f;

	/** Butcher's Heat's row: 1% more damage for each kill while it lasts. */
	const TCHAR* const KillCountingBuff = TEXT("Duration=8; MoreDamagePer=1; DurationPer=1; ScalingSource=Kill");

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** A player character on a player state, `Metres` along the X axis from the origin, with mana to spare. */
	ACataclysmPlayerCharacter* SpawnCasterAt(UWorld* World, float Metres)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor = World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector(Metres * M, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (!State || !Actor)
		{
			return nullptr;
		}
		Actor->SetPlayerState(State);
		Actor->OnRep_PlayerState();
		if (UCataclysmAbilitySystemComponent* System = SystemOf(Actor))
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 10000.0f);
		}
		return Actor;
	}

	/** A creature on the players' team, `Metres` along the X axis from the origin. */
	ACataclysmEnemyCharacter* SpawnAllyAt(UWorld* World, float Metres)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * M, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Made->SetHealth(100000.0f);
		}
		return Made;
	}

	/**
	 * Write a character's stat line, as worn rows would: a `more` row on `buff_potency` when `PotencyMore` is not
	 * nought, and a base on each of the two stats `UCataclysmSharedBuffs::Step` reads when its figure is above
	 * nought. Replaces whatever the character wore.
	 */
	void WearRowsOn(const AActor* Who, float PotencyMore, float SelfBuffSharedMetres, float NearbyAlliesMore)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		if (PotencyMore != 0.0f)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::More;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = PotencyMore;
			FCataclysmStatInputs Line;
			Line.Base = 0.0f;
			Line.Modifiers.Add(Row);
			Inputs.Add(FName(UCataclysmSelfBuffSkill::BuffPotencyStat), Line);
		}
		if (SelfBuffSharedMetres > 0.0f)
		{
			Inputs.FindOrAdd(FName(UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat)).Base =
				SelfBuffSharedMetres;
		}
		if (NearbyAlliesMore > 0.0f)
		{
			Inputs.FindOrAdd(FName(UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat)).Base =
				NearbyAlliesMore;
		}
		if (UCataclysmAbilitySystemComponent* System = SystemOf(Who))
		{
			System->SetStatInputs(MoveTemp(Inputs));
		}
	}

	/** Grant the kill-counting self buff in the Support slot with no element, use it, and tell it of five kills. */
	UCataclysmSelfBuffSkill* RunBuffOfFiveKills(ACataclysmPlayerCharacter* Caster)
	{
		UCataclysmAbilitySystemComponent* System = SystemOf(Caster);
		if (!System)
		{
			return nullptr;
		}
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Support, /*Level=*/100, Caster);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return nullptr;
		}
		Skill->SkillName = TEXT("Butcher's Heat");
		Skill->Params = UCataclysmSkillShapes::ParseParams(KillCountingBuff);
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT(""));
		System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		for (int32 Kill = 0; Kill < 5; ++Kill)
		{
			Skill->NoteKill();
		}
		return Skill;
	}

	/** The More damage a character carries from modifiers on its own ability system. */
	float MoreCarriedBy(const AActor* Who)
	{
		float More = 0.0f;
		if (const UCataclysmAbilitySystemComponent* Its = SystemOf(Who))
		{
			for (const FCataclysmStatModifier& Modifier : Its->GetStatModifiers())
			{
				if (Modifier.Bucket == ECataclysmStatBucket::More)
				{
					More += Modifier.Value;
				}
			}
		}
		return More;
	}

	/** How many modifiers a character carries on its own ability system; -1 when it has none of this project's. */
	int32 ModifiersCarriedBy(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* Its = SystemOf(Who);
		return Its ? Its->GetStatModifiers().Num() : -1;
	}
}

#define CATACLYSM_BUFF_POTENCY_SHARED_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString&)

/**
 * THE COPY. Two casters each run a self buff of five kills and share it within 8 m, and each has one ally 5 m
 * away WHO WEARS A `more` ROW OF 40 ITSELF. The control is the caster with no row: its buff grants something, and
 * its ally carries exactly that, so the ally's own row is not applied to a copy. The wearer of a `more` row of 40
 * is granted 1.4 times the control's figure, and its ally carries exactly the wearer's raised figure: 1.4 times,
 * and not 1.4 times 1.4.
 *
 * STANDING: the caster with no row at the origin and its ally 5 m along X; the wearer 100 m along X and its ally
 * 5 m further.
 */
CATACLYSM_BUFF_POTENCY_SHARED_TEST(FCataclysmBuffPotencyCopyTest,
	"Cataclysm.BuffPotency.TheCopyGivenToAnAllyIsTheCastersRaisedFigureAndTheAllysOwnRowIsNotAppliedAgain")
{
	using namespace CataclysmBuffPotencySharedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Plain = SpawnCasterAt(World, 0.0f);
	ACataclysmEnemyCharacter* PlainAlly = SpawnAllyAt(World, 5.0f);
	ACataclysmPlayerCharacter* Wearer = SpawnCasterAt(World, 100.0f);
	ACataclysmEnemyCharacter* WearerAlly = SpawnAllyAt(World, 105.0f);
	if (!TestNotNull(TEXT("set-up: a caster with no row"), Plain)
		|| !TestNotNull(TEXT("set-up: its ally"), PlainAlly)
		|| !TestNotNull(TEXT("set-up: a wearer"), Wearer)
		|| !TestNotNull(TEXT("set-up: its ally"), WearerAlly))
	{
		return false;
	}
	WearRowsOn(Plain, /*PotencyMore=*/0.0f, /*SelfBuffSharedMetres=*/8.0f, /*NearbyAlliesMore=*/0.0f);
	WearRowsOn(Wearer, /*PotencyMore=*/40.0f, /*SelfBuffSharedMetres=*/8.0f, /*NearbyAlliesMore=*/0.0f);
	WearRowsOn(PlainAlly, /*PotencyMore=*/40.0f, /*SelfBuffSharedMetres=*/0.0f, /*NearbyAlliesMore=*/0.0f);
	WearRowsOn(WearerAlly, /*PotencyMore=*/40.0f, /*SelfBuffSharedMetres=*/0.0f, /*NearbyAlliesMore=*/0.0f);

	const UCataclysmSelfBuffSkill* PlainBuff = RunBuffOfFiveKills(Plain);
	const UCataclysmSelfBuffSkill* WornBuff = RunBuffOfFiveKills(Wearer);
	if (!TestTrue(TEXT("set-up: the caster with no row runs the buff"), PlainBuff && PlainBuff->IsActive())
		|| !TestTrue(TEXT("set-up: the wearer runs the buff"), WornBuff && WornBuff->IsActive()))
	{
		return false;
	}

	// THE CONTROL: THE CASTER WEARS NO ROW, AND ITS ALLY DOES.
	const float PlainGranted = PlainBuff->GrantedIncrease;
	UCataclysmSharedBuffs::Step(Plain);
	if (!TestTrue(TEXT("control: with no row the buff grants its caster More damage"), PlainGranted > 0.0f)
		|| !TestEqual(TEXT("control: the ally of the caster with no row carries one modifier"),
					  ModifiersCarriedBy(PlainAlly), 1))
	{
		return false;
	}
	TestEqual(TEXT("control: and it is exactly what the buff grants its caster, though the ally wears a row of 40"),
			  MoreCarriedBy(PlainAlly), PlainGranted, 0.001f);

	// THE ROW ON THE CASTER.
	TestEqual(TEXT("with a more row of 40 the buff grants its caster 1.4 times the More damage"),
			  WornBuff->GrantedIncrease, PlainGranted * 1.4f, 0.001f);
	UCataclysmSharedBuffs::Step(Wearer);
	if (!TestEqual(TEXT("the wearer's ally carries one modifier"), ModifiersCarriedBy(WearerAlly), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is the caster's raised figure, 1.4 times: the ally's own row of 40 is not applied again"),
			  MoreCarriedBy(WearerAlly), PlainGranted * 1.4f, 0.001f);

	// A SECOND STEP GIVES NOTHING MORE.
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("a second step leaves the ally carrying the one copy at the same figure"),
			  MoreCarriedBy(WearerAlly), PlainGranted * 1.4f, 0.001f);
	return true;
}

/**
 * A BUFF A ROW LAYS IS NOT A BUFF A SKILL LAYS. "Nearby allies gain 10-20% more damage" is a row's own standing
 * grant, copied to allies within 5 m by the same step. The control is a caster wearing it at 15 and no other row:
 * its ally carries More damage. A caster wearing it at 15 and a `more` row of 40 on `buff_potency` gives its ally
 * the same figure.
 *
 * STANDING: the caster with no potency row at the origin and its ally 4 m along X; the wearer 100 m along X and
 * its ally 4 m further.
 */
CATACLYSM_BUFF_POTENCY_SHARED_TEST(FCataclysmBuffPotencyNearbyRowTest,
	"Cataclysm.BuffPotency.TheMoreDamageARowGivesNearbyAlliesIsNotRaised")
{
	using namespace CataclysmBuffPotencySharedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Plain = SpawnCasterAt(World, 0.0f);
	ACataclysmEnemyCharacter* PlainAlly = SpawnAllyAt(World, 4.0f);
	ACataclysmPlayerCharacter* Wearer = SpawnCasterAt(World, 100.0f);
	ACataclysmEnemyCharacter* WearerAlly = SpawnAllyAt(World, 104.0f);
	if (!TestNotNull(TEXT("set-up: a caster with no potency row"), Plain)
		|| !TestNotNull(TEXT("set-up: its ally"), PlainAlly)
		|| !TestNotNull(TEXT("set-up: a wearer"), Wearer)
		|| !TestNotNull(TEXT("set-up: its ally"), WearerAlly))
	{
		return false;
	}
	WearRowsOn(Plain, /*PotencyMore=*/0.0f, /*SelfBuffSharedMetres=*/0.0f, /*NearbyAlliesMore=*/15.0f);
	WearRowsOn(Wearer, /*PotencyMore=*/40.0f, /*SelfBuffSharedMetres=*/0.0f, /*NearbyAlliesMore=*/15.0f);

	UCataclysmSharedBuffs::Step(Plain);
	UCataclysmSharedBuffs::Step(Wearer);
	const float PlainCarried = MoreCarriedBy(PlainAlly);
	if (!TestTrue(TEXT("control: the ally of a caster wearing the nearby row and no potency row carries More damage"),
				  PlainCarried > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the ally of a caster also wearing a more row of 40 on buff_potency carries the same figure"),
			  MoreCarriedBy(WearerAlly), PlainCarried, 0.001f);
	return true;
}

#undef CATACLYSM_BUFF_POTENCY_SHARED_TEST

#endif // WITH_AUTOMATION_TESTS
