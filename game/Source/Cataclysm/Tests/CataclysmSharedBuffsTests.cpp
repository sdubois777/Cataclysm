// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmMinion.h"
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
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Issue #1833 group E part 4a, ruled 2026-10-02: a running self buff's More
 * damage, and the standing "Nearby allies gain 10-20% more damage", given to the
 * allies near the wearer by `UCataclysmSharedBuffs::Step` and taken back.
 *
 * THE STEP IS CALLED DIRECTLY. The regeneration step calls it every quarter
 * second in play; a test world is not ticked, so each case calls it where the
 * step would have run.
 */
namespace CataclysmSharedBuffsTest
{
	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float M = 100.0f;

	/** Burning Wrath's row: 4% more damage for each burning enemy within 15 m. */
	const TCHAR* const MoreDamageBuff = TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning");

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	ACataclysmPlayerCharacter* SpawnWearer(UWorld* World, const FVector& Where = FVector::ZeroVector)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (State && Actor)
		{
			Actor->SetPlayerState(State);
			Actor->OnRep_PlayerState();
			return Actor;
		}
		return nullptr;
	}

	/** A creature on `Team`, `Metres` along the X axis from the origin. */
	ACataclysmEnemyCharacter* Creature(UWorld* World, float Metres, ECataclysmTeam Team)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * M, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			Made->SetHealth(100000.0f);
		}
		return Made;
	}

	/** Write the wearer's stat line, as worn rows would. */
	void Wear(ACataclysmPlayerCharacter* Wearer, TArray<TPair<const TCHAR*, float>> Lines)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		for (const TPair<const TCHAR*, float>& Line : Lines)
		{
			Inputs.FindOrAdd(FName(Line.Key)).Base = Line.Value;
		}
		SystemOf(Wearer)->SetStatInputs(MoveTemp(Inputs));
	}

	/** Grant a self buff in `Slot` with `ParamText` and `TagCell`, and use it. */
	UCataclysmSelfBuffSkill* Buff(ACataclysmPlayerCharacter* Wearer, ECataclysmAbilitySlot Slot,
								  const TCHAR* ParamText, const TCHAR* TagCell)
	{
		UCataclysmAbilitySystemComponent* System = SystemOf(Wearer);
		const FGameplayAbilitySpecHandle Handle =
			System->GiveAbilityInSlot(UCataclysmSelfBuffSkill::StaticClass(), Slot, /*Level=*/100, Wearer);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		UCataclysmSelfBuffSkill* Skill = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Skill)
		{
			return nullptr;
		}
		Skill->SkillName = TEXT("Burning Wrath");
		Skill->Params = UCataclysmSkillShapes::ParseParams(ParamText);
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		return Skill;
	}

	/**
	 * Set one enemy alight 3 m from the wearer, so a Burning Wrath cast after it
	 * grants 4% more damage: "4% more fire damage for every enemy currently
	 * burning within 15 meters".
	 */
	ACataclysmEnemyCharacter* SomethingBurning(UWorld* World, ACataclysmPlayerCharacter* Wearer)
	{
		ACataclysmEnemyCharacter* Alight = Creature(World, -3.0f, ECataclysmTeam::Monsters);
		if (Alight)
		{
			UCataclysmSkillEffects::ApplyBurn(Wearer, Alight, 100.0f,
				/*bScalesWithInstigator=*/true, /*bBurnIsDesigned=*/true);
		}
		return Alight;
	}

	/** The More damage `Ally` carries from modifiers on its own ability system. */
	float MoreCarried(const AActor* Ally)
	{
		float More = 0.0f;
		if (const UCataclysmAbilitySystemComponent* Its = SystemOf(Ally))
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

	int32 ModifiersOn(const AActor* Actor)
	{
		const UCataclysmAbilitySystemComponent* Its = SystemOf(Actor);
		return Its ? Its->GetStatModifiers().Num() : -1;
	}

	void MoveTo(AActor* Actor, float Metres)
	{
		Actor->SetActorLocation(FVector(Metres * M, 0.0f, 0.0f), /*bSweep=*/false, nullptr,
								ETeleportType::TeleportPhysics);
	}

	/** What a minion's blow of 100 comes to through its own ability system. */
	float BlowOf100(const AActor* Minion)
	{
		return UCataclysmSkillEffects::ModifiedDamage(SystemOf(Minion), 100.0f, FGameplayTagContainer());
	}
}

#define CATACLYSM_SHARED_BUFFS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString&)

/**
 * "Your support ability affects all allies within 15 meters". A Support buff
 * granting 4% More reaches an imp 10 m away and not one 20 m away, and the
 * near imp's blow is 4% more. The buff is granted here with no element, so the
 * copy reaches an imp's blow, which carries none; the shipped buffs are
 * Demonic, and `TheCopyIsTakenBackWhenTheAllyLeavesAndWhenTheBuffEnds` shows
 * their copy keeps that scope.
 */
CATACLYSM_SHARED_BUFFS_TEST(FCataclysmSharedSupportReachTest,
	"Cataclysm.SharedBuffs.ASupportBuffReachesAnAllyWithinFifteenMetresAndNotOneBeyond")
{
	using namespace CataclysmSharedBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = SpawnWearer(World);
	if (!TestNotNull(TEXT("a wearer"), Wearer) || !TestNotNull(TEXT("something burning"), SomethingBurning(World, Wearer)))
	{
		return false;
	}
	Wear(Wearer, {{UCataclysmAbilitySystemComponent::SupportBuffSharedWithinMetresStat, 15.0f}});
	ACataclysmMinion* Near = ACataclysmMinion::Spawn(Wearer, FVector(10 * M, 0, 0), 20.0f, false, TEXT("Imp"));
	ACataclysmMinion* Far = ACataclysmMinion::Spawn(Wearer, FVector(20 * M, 0, 0), 20.0f, false, TEXT("Imp"));
	if (!TestNotNull(TEXT("an imp at 10 m"), Near) || !TestNotNull(TEXT("an imp at 20 m"), Far))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the near imp's blow of 100 is 100 before the step"), BlowOf100(Near), 100.0f, 0.01f);

	const UCataclysmSelfBuffSkill* Running = Buff(Wearer, ECataclysmAbilitySlot::Support, MoreDamageBuff, TEXT(""));
	if (!TestTrue(TEXT("set-up: the Support buff runs and grants 4% More"),
			Running && Running->IsActive() && FMath::IsNearlyEqual(Running->GrantedIncrease, 4.0f)))
	{
		return false;
	}

	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("the imp at 10 m carries the buff's 4% More"), MoreCarried(Near), 4.0f, 0.001f);
	TestEqual(TEXT("and its blow of 100 is 104"), BlowOf100(Near), 104.0f, 0.01f);
	TestEqual(TEXT("the imp at 20 m carries nothing"), MoreCarried(Far), 0.0f, 0.001f);
	TestEqual(TEXT("and its blow of 100 is 100"), BlowOf100(Far), 100.0f, 0.01f);
	return true;
}

/**
 * "Applying a buff to yourself also applies it to all allies within 8 meters".
 * A Demonic buff's copy carries the Demonic scope; it is taken back when the
 * ally walks out, given again when it walks back, and taken back when the buff
 * ends.
 */
CATACLYSM_SHARED_BUFFS_TEST(FCataclysmSharedTakeBackTest,
	"Cataclysm.SharedBuffs.TheCopyIsTakenBackWhenTheAllyLeavesAndWhenTheBuffEnds")
{
	using namespace CataclysmSharedBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = SpawnWearer(World);
	ACataclysmEnemyCharacter* Ally = Creature(World, 5.0f, ECataclysmTeam::Players);
	if (!TestNotNull(TEXT("a wearer"), Wearer) || !TestNotNull(TEXT("an ally"), Ally)
		|| !TestNotNull(TEXT("something burning"), SomethingBurning(World, Wearer)))
	{
		return false;
	}
	Wear(Wearer, {{UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat, 8.0f}});
	UCataclysmSelfBuffSkill* Running =
		Buff(Wearer, ECataclysmAbilitySlot::Support, MoreDamageBuff, TEXT("Element.Demonic"));
	if (!TestTrue(TEXT("set-up: the Demonic buff runs and grants 4% More"),
			Running && Running->IsActive() && FMath::IsNearlyEqual(Running->GrantedIncrease, 4.0f)))
	{
		return false;
	}

	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("the ally at 5 m carries 4% More"), MoreCarried(Ally), 4.0f, 0.001f);
	const FGameplayTag Demonic = UCataclysmDamageCalculation::ElementTagFor(FName(TEXT("Demonic")));
	const UCataclysmAbilitySystemComponent* Its = SystemOf(Ally);
	TestTrue(TEXT("scoped to Demonic damage, as the wearer's own is"),
		Its && Its->GetStatModifiers().Num() == 1 && Demonic.IsValid()
			&& Its->GetStatModifiers()[0].RequiredTags.HasTagExact(Demonic));

	MoveTo(Ally, 12.0f);
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("walked out to 12 m, it carries nothing"), ModifiersOn(Ally), 0);

	MoveTo(Ally, 5.0f);
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("walked back to 5 m, it carries the 4% again"), MoreCarried(Ally), 4.0f, 0.001f);

	SystemOf(Wearer)->CancelAbilityHandle(Running->GetCurrentAbilitySpecHandle());
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("the buff ended, so the ally carries nothing"), ModifiersOn(Ally), 0);
	TestEqual(TEXT("and the wearer holds no copy"), SystemOf(Wearer)->SharedBuffCopies.Num(), 0);
	return true;
}

/**
 * The self-buff reach applies to any buff, the support reach only to a Support
 * skill, and a buff takes the larger reach that applies to it.
 */
CATACLYSM_SHARED_BUFFS_TEST(FCataclysmSharedReachesTest,
	"Cataclysm.SharedBuffs.ASelfBuffAloneReachesEightMetresNotFifteen")
{
	using namespace CataclysmSharedBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = SpawnWearer(World);
	ACataclysmEnemyCharacter* AtSix = Creature(World, 6.0f, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* AtTwelve = Creature(World, 12.0f, ECataclysmTeam::Players);
	if (!TestNotNull(TEXT("a wearer"), Wearer) || !TestNotNull(TEXT("an ally at 6 m"), AtSix)
		|| !TestNotNull(TEXT("an ally at 12 m"), AtTwelve)
		|| !TestNotNull(TEXT("something burning"), SomethingBurning(World, Wearer)))
	{
		return false;
	}
	// MANA FOR BOTH CASTS. A Support skill costs 25 and a Special 40 in
	// SkillSlots.csv; a fresh player cannot pay for the second after the first.
	SystemOf(Wearer)->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10000.0f);
	SystemOf(Wearer)->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 10000.0f);
	UCataclysmSelfBuffSkill* Support = Buff(Wearer, ECataclysmAbilitySlot::Support, MoreDamageBuff, TEXT(""));
	if (!TestTrue(TEXT("set-up: the Support buff runs"), Support && Support->IsActive()))
	{
		return false;
	}

	Wear(Wearer, {{UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat, 8.0f}});
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("with 8 m alone, the ally at 6 m carries 4%"), MoreCarried(AtSix), 4.0f, 0.001f);
	TestEqual(TEXT("and the ally at 12 m nothing"), MoreCarried(AtTwelve), 0.0f, 0.001f);

	Wear(Wearer, {{UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat, 8.0f},
				  {UCataclysmAbilitySystemComponent::SupportBuffSharedWithinMetresStat, 15.0f}});
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("with 15 m for a Support skill as well, the ally at 12 m carries 4%"),
		MoreCarried(AtTwelve), 4.0f, 0.001f);

	// A SPECIAL-SLOT BUFF IS NOT A SUPPORT ABILITY.
	SystemOf(Wearer)->CancelAbilityHandle(Support->GetCurrentAbilitySpecHandle());
	UCataclysmSelfBuffSkill* Special = Buff(Wearer, ECataclysmAbilitySlot::Special, MoreDamageBuff, TEXT(""));
	if (!TestTrue(TEXT("set-up: the Special buff runs"), Special && Special->IsActive())
		|| !TestEqual(TEXT("set-up: and grants 4% More"), Special->GrantedIncrease, 4.0f, 0.001f))
	{
		return false;
	}
	Wear(Wearer, {{UCataclysmAbilitySystemComponent::SupportBuffSharedWithinMetresStat, 15.0f}});
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("with only the support reach, a Special buff reaches nobody at 6 m"),
		MoreCarried(AtSix), 0.0f, 0.001f);
	return true;
}

/** A running buff that grants no More damage, as Slipstream does, gives allies nothing. */
CATACLYSM_SHARED_BUFFS_TEST(FCataclysmSharedNoMoreTest,
	"Cataclysm.SharedBuffs.ABuffGrantingNoMoreGivesAlliesNothing")
{
	using namespace CataclysmSharedBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = SpawnWearer(World);
	ACataclysmEnemyCharacter* Ally = Creature(World, 5.0f, ECataclysmTeam::Players);
	if (!TestNotNull(TEXT("a wearer"), Wearer) || !TestNotNull(TEXT("an ally"), Ally))
	{
		return false;
	}
	Wear(Wearer, {{UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat, 8.0f},
				  {UCataclysmAbilitySystemComponent::SupportBuffSharedWithinMetresStat, 15.0f}});
	const UCataclysmSelfBuffSkill* Running =
		Buff(Wearer, ECataclysmAbilitySlot::Support, TEXT("Duration=8"), TEXT(""));
	if (!TestTrue(TEXT("set-up: the buff runs"), Running && Running->IsActive()))
	{
		return false;
	}

	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("the ally at 5 m carries nothing"), ModifiersOn(Ally), 0);
	TestEqual(TEXT("and the wearer holds no copy"), SystemOf(Wearer)->SharedBuffCopies.Num(), 0);
	return true;
}

/**
 * "Nearby allies gain 10-20% more damage". Every ally within 5 m carries the
 * More unscoped, so an imp's blow rises with it; one at 7 m does not, and the
 * wearer itself is not given it.
 */
CATACLYSM_SHARED_BUFFS_TEST(FCataclysmSharedNearbyTest,
	"Cataclysm.SharedBuffs.NearbyAlliesGainUnscopedMoreWithinFiveMetresButNotTheWearer")
{
	using namespace CataclysmSharedBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = SpawnWearer(World);
	ACataclysmEnemyCharacter* AtFour = Creature(World, 4.0f, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* AtSeven = Creature(World, 7.0f, ECataclysmTeam::Players);
	if (!TestNotNull(TEXT("a wearer"), Wearer) || !TestNotNull(TEXT("an ally at 4 m"), AtFour)
		|| !TestNotNull(TEXT("an ally at 7 m"), AtSeven))
	{
		return false;
	}
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Wearer, FVector(0, 3 * M, 0), 20.0f, false, TEXT("Imp"));
	if (!TestNotNull(TEXT("an imp at 3 m"), Imp))
	{
		return false;
	}
	Wear(Wearer, {{UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat, 15.0f}});

	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("the ally at 4 m carries 15% More"), MoreCarried(AtFour), 15.0f, 0.001f);
	const UCataclysmAbilitySystemComponent* Its = SystemOf(AtFour);
	TestTrue(TEXT("unscoped"),
		Its && Its->GetStatModifiers().Num() == 1 && Its->GetStatModifiers()[0].RequiredTags.IsEmpty());
	TestEqual(TEXT("the ally at 7 m carries nothing"), ModifiersOn(AtSeven), 0);
	TestEqual(TEXT("the imp's blow of 100 is 115"), BlowOf100(Imp), 115.0f, 0.01f);
	TestEqual(TEXT("and the wearer carries nothing of it"), ModifiersOn(Wearer), 0);
	return true;
}

/** Two steps give one copy, and a moved value changes that copy rather than adding one. */
CATACLYSM_SHARED_BUFFS_TEST(FCataclysmSharedHeldOnceTest,
	"Cataclysm.SharedBuffs.ACopyIsHeldOnceAndFollowsAChangedValue")
{
	using namespace CataclysmSharedBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = SpawnWearer(World);
	ACataclysmEnemyCharacter* Ally = Creature(World, 4.0f, ECataclysmTeam::Players);
	if (!TestNotNull(TEXT("a wearer"), Wearer) || !TestNotNull(TEXT("an ally"), Ally))
	{
		return false;
	}
	Wear(Wearer, {{UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat, 10.0f}});

	UCataclysmSharedBuffs::Step(Wearer);
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("two steps leave one modifier on the ally"), ModifiersOn(Ally), 1);
	TestEqual(TEXT("worth 10"), MoreCarried(Ally), 10.0f, 0.001f);

	Wear(Wearer, {{UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat, 20.0f}});
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("the value moved to 20, still one modifier"), ModifiersOn(Ally), 1);
	TestEqual(TEXT("now worth 20"), MoreCarried(Ally), 20.0f, 0.001f);
	TestEqual(TEXT("and the wearer holds one copy"), SystemOf(Wearer)->SharedBuffCopies.Num(), 1);
	return true;
}

/**
 * Nothing a wearer gave survives the wearer: a dead wearer's step takes every
 * copy back, and a wearer leaving play does too. And an ally that dies or is
 * gone has its entry dropped on the next step.
 */
CATACLYSM_SHARED_BUFFS_TEST(FCataclysmSharedDeathTest,
	"Cataclysm.SharedBuffs.ADeadWearerTakesEveryCopyBack")
{
	using namespace CataclysmSharedBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = SpawnWearer(World);
	ACataclysmEnemyCharacter* Stays = Creature(World, 3.0f, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Dies = Creature(World, 4.0f, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Goes = Creature(World, 2.0f, ECataclysmTeam::Players);
	ACataclysmEnemyCharacter* Killer = Creature(World, -40.0f, ECataclysmTeam::Monsters);
	if (!TestNotNull(TEXT("a wearer"), Wearer) || !TestNotNull(TEXT("an ally that stays"), Stays)
		|| !TestNotNull(TEXT("an ally that dies"), Dies) || !TestNotNull(TEXT("an ally that goes"), Goes)
		|| !TestNotNull(TEXT("a killer"), Killer))
	{
		return false;
	}
	Wear(Wearer, {{UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat, 10.0f}});
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("set-up: the wearer holds three copies"), SystemOf(Wearer)->SharedBuffCopies.Num(), 3);

	UCataclysmSkillEffects::ApplyDirectDamage(Killer, Dies, 1000000.0f);
	TestTrue(TEXT("set-up: one ally died"), UCataclysmSkillEffects::IsDead(Dies));
	Goes->Destroy();
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("the dead ally's and the gone ally's entries are dropped"),
		SystemOf(Wearer)->SharedBuffCopies.Num(), 1);
	TestEqual(TEXT("and the dead ally carries nothing"), ModifiersOn(Dies), 0);

	UCataclysmSkillEffects::ApplyDirectDamage(Killer, Wearer, 100000000.0f);
	TestTrue(TEXT("set-up: the wearer died"), UCataclysmSkillEffects::IsDead(Wearer));
	UCataclysmSharedBuffs::Step(Wearer);
	TestEqual(TEXT("a dead wearer holds no copy"), SystemOf(Wearer)->SharedBuffCopies.Num(), 0);
	TestEqual(TEXT("and the ally that stayed carries nothing"), ModifiersOn(Stays), 0);

	// AND A WEARER LEAVING PLAY, through its character's EndPlay. A player's
	// ability system belongs to its player state, so it is the character's
	// ending that must take the copies back.
	// TWENTY METRES OFF, so nothing of the first wearer's arrangement is nearby.
	ACataclysmPlayerCharacter* Second = SpawnWearer(World, FVector(20 * M, 0, 0));
	ACataclysmEnemyCharacter* Beside = Creature(World, 23.0f, ECataclysmTeam::Players);
	if (!TestNotNull(TEXT("a second wearer"), Second) || !TestNotNull(TEXT("an ally beside it"), Beside))
	{
		return false;
	}
	Wear(Second, {{UCataclysmAbilitySystemComponent::NearbyAlliesMoreDamageStat, 10.0f}});
	UCataclysmSharedBuffs::Step(Second);
	TestEqual(TEXT("set-up: the ally beside the second wearer carries 10"), MoreCarried(Beside), 10.0f, 0.001f);
	Second->Destroy();
	TestEqual(TEXT("the second wearer left play, so the ally carries nothing"), ModifiersOn(Beside), 0);
	return true;
}

#undef CATACLYSM_SHARED_BUFFS_TEST

#endif // WITH_AUTOMATION_TESTS
