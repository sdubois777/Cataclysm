// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The hit, death and skill-used notices. Issue #41, slice 4.
 *
 * EVERY TEST DRIVES THE REAL SENDER. A blow goes through
 * `UCataclysmSkillEffects::ApplyHit` or a real skill and resolves in the target's
 * own attribute set, a death is the target's own death, and a skill is
 * activated. None of them calls a `UCataclysmCombatEvents::Note...` function by
 * hand: a test that did would prove a notice can be built and nothing about
 * whether the game sends one.
 *
 * WHICH SKILL A BLOW CAME FROM IS READ FROM THE INSTANCE. The effect context
 * keeps the skill's instance and its class default object, and every player
 * skill is an instance of one of eight template classes, so only the instance
 * knows its name. `EachSkillNamesItselfOnItsBlowsItsBurnsAndItsKills` uses two
 * skills of one class on purpose, because that is the case that tells the two
 * apart.
 *
 * WHAT NO TEST HERE REACHES, AND WHY.
 *
 * - THE CREATURE BRAIN'S TWO CALLS, in `ACataclysmEnemyController::ContinueWindUp`
 *   and `UseAbilitiesOn`, which need a whole fight's scaffolding to drive.
 *   `tools/tests/test_hooks_no_headless_test_can_drive_still_call_their_jobs.py`
 *   reads both from the source instead.
 * - A REFUSED PRESS. The cost and the cooldown are checked by
 *   `UGameplayAbility::CanActivateAbility`, before any of a skill's own code
 *   runs, so a refused press never reaches `CommitAndBegin` and no change inside
 *   it could make one announce. A test asserting the refusal would pass whatever
 *   `CommitAndBegin` did -- the kind of test that looks perfect and checks
 *   nothing.
 */

namespace CataclysmCombatEventsTest
{
	constexpr float M = 100.0f;

	/** A bare actor holding an ability system, the attributes a blow reads and a weapon. */
	struct FArmedActor
	{
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * `CataclysmBasicAttackTests.cpp`'s armed actor, under this file's own names
	 * so a unity build does not see two of them.
	 *
	 * IT STANDS AT THE ORIGIN, FACING +X, AND CANNOT BE MOVED. A bare actor has
	 * no root component, so its location is the origin whatever it is told, and
	 * every distance below is measured from there.
	 *
	 * IT HAS A WEAPON'S WORTH OF DAMAGE, because an actor built this way starts
	 * at zero and every blow it landed would deal nothing.
	 */
	static FArmedActor MakeArmed(UWorld* World)
	{
		FArmedActor Made;
		Made.Actor = World->SpawnActor<AActor>();
		if (!Made.Actor)
		{
			return Made;
		}

		Made.AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Made.Actor);
		Made.AbilitySystem->RegisterComponent();
		Made.AbilitySystem->AddAttributeSetSubobject(
			NewObject<UCataclysmCombatAttributeSet>(Made.Actor));
		Made.AbilitySystem->AddAttributeSetSubobject(
			NewObject<UCataclysmVitalAttributeSet>(Made.Actor));
		Made.AbilitySystem->InitAbilityActorInfo(Made.Actor, Made.Actor);

		UCataclysmWeaponSlotsComponent* Slots =
			NewObject<UCataclysmWeaponSlotsComponent>(Made.Actor);
		Slots->RegisterComponent();
		Slots->SetDamageType(TEXT("Demonic"));

		Made.AbilitySystem->SetNumericAttributeBase(
			UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		return Made;
	}

	/** A creature on the monsters' side with this much health. */
	static ACataclysmEnemyCharacter* SpawnCreatureAt(UWorld* World, const FVector& Where,
													 float Health)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(Health);
		}
		return Spawned;
	}

	static UCataclysmAbilitySystemComponent* SystemOf(ACataclysmEnemyCharacter* Creature)
	{
		return Creature
			? Cast<UCataclysmAbilitySystemComponent>(Creature->GetAbilitySystemComponent())
			: nullptr;
	}

	static float HealthOf(ACataclysmEnemyCharacter* Creature)
	{
		const UCataclysmAbilitySystemComponent* System = SystemOf(Creature);
		return System
			? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
			: -1.0f;
	}

	static FGameplayTag TagNamed(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), false);
	}

	static FGameplayTagContainer TagsNamed(std::initializer_list<const TCHAR*> Names)
	{
		FGameplayTagContainer Tags;
		for (const TCHAR* Name : Names)
		{
			Tags.AddTag(TagNamed(Name));
		}
		return Tags;
	}

	/** Tags written the way a row writes them, separated by commas. */
	static FGameplayTagContainer TagsListed(const TCHAR* Listed)
	{
		TArray<FString> Names;
		FString(Listed).ParseIntoArray(Names, TEXT(","), /*InCullEmpty=*/true);
		FGameplayTagContainer Tags;
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			Tags.AddTag(TagNamed(*Name));
		}
		return Tags;
	}

	/**
	 * Grants a skill into a slot and names and tags it the way its row would,
	 * which is how `CataclysmBasicAttackTests.cpp` grants the basic attack.
	 *
	 * RETURNS THE GRANTED INSTANCE, and the name, the tags and the shape
	 * parameters are set on it. That is where a real skill keeps them: the class
	 * default object has none of the three.
	 */
	template <typename TSkill>
	static TSkill* GrantNamedSkill(const FArmedActor& User, ECataclysmAbilitySlot Slot,
								   const TCHAR* Name, const TCHAR* Tags,
								   const TCHAR* Params, FGameplayAbilitySpecHandle& OutHandle)
	{
		OutHandle = User.AbilitySystem->GiveAbilityInSlot(
			TSkill::StaticClass(), Slot, /*Level=*/100, User.Actor);
		FGameplayAbilitySpec* Spec = User.AbilitySystem->FindAbilitySpecFromHandle(OutHandle);
		TSkill* Skill = Spec ? Cast<TSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = Name;
			Skill->SkillTags = TagsListed(Tags);
			Skill->Params = UCataclysmSkillShapes::ParseParams(Params);
		}
		return Skill;
	}

	/**
	 * What one world sent, kept for a test to read.
	 *
	 * THE TAG CONTAINERS ARE COPIED WHILE THE NOTICE IS BEING SENT, AND THE
	 * KEPT NOTICES' POINTERS ARE CLEARED. A notice's tag pointers are valid only
	 * while it is being handed out, which is the rule the notices state. A test
	 * reading them afterwards would read memory that may be gone and could pass
	 * by luck, so nothing is left to read.
	 */
	struct FHeard
	{
		TArray<FCataclysmHitNotice> Hits;
		TArray<FGameplayTagContainer> HitTags;
		TArray<bool> HitHadGrantedTags;
		TArray<FGameplayTagContainer> HitSkillTags;
		TArray<bool> HitHadSkillTags;

		TArray<FCataclysmDeathNotice> Deaths;
		TArray<FGameplayTagContainer> KillingTags;
		TArray<FGameplayTagContainer> DeathSkillTags;
		TArray<bool> DeathHadSkillTags;

		TArray<FCataclysmSkillUsedNotice> SkillsUsed;
		TArray<bool> SkillHadTags;

		FDelegateHandle HitHandle;
		FDelegateHandle DeathHandle;
		FDelegateHandle SkillHandle;

		/** The first hit notice from this attacker at or after `From`, or none. */
		int32 HitFrom(const AActor* Attacker, int32 From, bool bDamageOverTime) const
		{
			for (int32 Index = From; Index < Hits.Num(); ++Index)
			{
				if (Hits[Index].Attacker == Attacker
					&& Hits[Index].bDamageOverTime == bDamageOverTime)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}
	};

	static void ListenTo(UCataclysmCombatEvents* Events, FHeard& Heard)
	{
		Heard.HitHandle = Events->OnHit.AddLambda(
			[&Heard](const FCataclysmHitNotice& Notice)
			{
				FGameplayTagContainer Both;
				if (Notice.EffectTags)
				{
					Both.AppendTags(*Notice.EffectTags);
				}
				if (Notice.GrantedTags)
				{
					Both.AppendTags(*Notice.GrantedTags);
				}
				Heard.HitTags.Add(Both);
				Heard.HitHadGrantedTags.Add(Notice.GrantedTags != nullptr);
				Heard.HitSkillTags.Add(Notice.SkillTags
					? *Notice.SkillTags : FGameplayTagContainer());
				Heard.HitHadSkillTags.Add(Notice.SkillTags != nullptr);

				FCataclysmHitNotice Kept = Notice;
				Kept.EffectTags = nullptr;
				Kept.GrantedTags = nullptr;
				Kept.SkillTags = nullptr;
				Heard.Hits.Add(Kept);
			});
		Heard.DeathHandle = Events->OnDeath.AddLambda(
			[&Heard](const FCataclysmDeathNotice& Notice)
			{
				Heard.KillingTags.Add(Notice.KillingTags
					? *Notice.KillingTags : FGameplayTagContainer());
				Heard.DeathSkillTags.Add(Notice.KillingSkillTags
					? *Notice.KillingSkillTags : FGameplayTagContainer());
				Heard.DeathHadSkillTags.Add(Notice.KillingSkillTags != nullptr);

				FCataclysmDeathNotice Kept = Notice;
				Kept.KillingTags = nullptr;
				Kept.KillingSkillTags = nullptr;
				Heard.Deaths.Add(Kept);
			});
		Heard.SkillHandle = Events->OnSkillUsed.AddLambda(
			[&Heard](const FCataclysmSkillUsedNotice& Notice)
			{
				Heard.SkillHadTags.Add(Notice.SkillTags != nullptr);

				FCataclysmSkillUsedNotice Kept = Notice;
				Kept.SkillTags = nullptr;
				Heard.SkillsUsed.Add(Kept);
			});
	}

	static void StopListening(UCataclysmCombatEvents* Events, FHeard& Heard)
	{
		Events->OnHit.Remove(Heard.HitHandle);
		Events->OnDeath.Remove(Heard.DeathHandle);
		Events->OnSkillUsed.Remove(Heard.SkillHandle);
	}
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsHitNotice,
	"Cataclysm.CombatEvents.AHitIsAnnouncedOnceNamingBothSides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsHitNotice::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FArmedActor Attacker = MakeArmed(World);
	ACataclysmEnemyCharacter* Target =
		SpawnCreatureAt(World, FVector(2.0f * M, 0.0f, 0.0f), 1000.0f);
	if (!TestNotNull(TEXT("the world has the notices subsystem"), Events)
		|| !TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target"), Target))
	{
		return false;
	}

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	const float Before = HealthOf(Target);
	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
									 TagsNamed({TEXT("Type.Melee")}));

	if (!TestEqual(TEXT("one blow sends one hit notice"), Heard.Hits.Num(), 1))
	{
		return false;
	}
	const FCataclysmHitNotice& Hit = Heard.Hits[0];

	TestTrue(TEXT("the blow hurt the target"), HealthOf(Target) < Before);
	TestTrue(TEXT("the notice names the attacker"), Hit.Attacker == Attacker.Actor);
	TestTrue(TEXT("and names it as what dealt the blow, since it struck for itself"),
			 Hit.DealtBy == Attacker.Actor);
	TestTrue(TEXT("and names the target"), Hit.Target == Target);
	TestTrue(TEXT("and that something landed"), Hit.Landed > 0.0f);
	TestTrue(TEXT("and reached health"), Hit.DealtToHealth > 0.0f);
	TestFalse(TEXT("and was not evaded"), Hit.bEvaded);
	TestFalse(TEXT("and is not damage over time"), Hit.bDamageOverTime);
	TestEqual(TEXT("and says how far apart the two stood, in metres"),
			  Hit.DistanceMetres, 2.0f, 0.05f);
	TestTrue(TEXT("and the tags it carried say melee"),
			 Heard.HitTags[0].HasTag(UCataclysmDamageCalculation::MeleeTag()));
	TestFalse(TEXT("and a direct blow grants nothing, so it points at no granted tags"),
			  Heard.HitHadGrantedTags[0]);
	TestTrue(TEXT("and a blow no skill dealt names no skill"), Hit.SkillName.IsNone());
	TestFalse(TEXT("and points at no skill's tags"), Heard.HitHadSkillTags[0]);
	TestEqual(TEXT("and the world counted one"), static_cast<int32>(Events->HitsSent()), 1);

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsHitFacts,
	"Cataclysm.CombatEvents.AHitSaysWhetherItWasMeleeRangedOrASpellAndWhetherABossDealtIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsHitFacts::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	// THE FOUR FACTS ARE ISSUE #666'S AND THE NOTICES COPY THEM. A spell that
	// fires a projectile is both a spell and ranged, which is why they are three
	// facts and not one kind.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FArmedActor Attacker = MakeArmed(World);
	ACataclysmEnemyCharacter* Target =
		SpawnCreatureAt(World, FVector(2.0f * M, 0.0f, 0.0f), 1000000.0f);
	if (!TestNotNull(TEXT("the notices subsystem"), Events)
		|| !TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target"), Target))
	{
		return false;
	}

	// AND SPELL DAMAGE, because a spell is scaled by it, and a blow that deals
	// nothing is never applied and so never announced.
	Attacker.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetSpellDamageAttribute(), 100.0f);

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	struct FCase
	{
		const TCHAR* What;
		const TCHAR* Tags;
		bool bMelee;
		bool bRanged;
		bool bSpell;
	};
	const FCase Cases[] = {
		{TEXT("a melee blow"), TEXT("Type.Melee"), true, false, false},
		{TEXT("a projectile"), TEXT("Type.Projectile"), false, true, false},
		{TEXT("a ranged attack"), TEXT("Type.Ranged"), false, true, false},
		{TEXT("a spell"), TEXT("Type.Spell"), false, false, true},
		{TEXT("a spell that flies"), TEXT("Type.Spell, Type.Projectile"), false, true, true},
		{TEXT("a blow of no kind"), TEXT(""), false, false, false},
	};
	for (const FCase& Case : Cases)
	{
		const int32 Was = Heard.Hits.Num();
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f, TagsListed(Case.Tags));
		if (!TestEqual(FString::Printf(TEXT("%s sends one notice"), Case.What),
					   Heard.Hits.Num(), Was + 1))
		{
			continue;
		}
		const FCataclysmHitNotice& Hit = Heard.Hits.Last();
		TestEqual(FString::Printf(TEXT("%s: melee"), Case.What), Hit.bIsMelee, Case.bMelee);
		TestEqual(FString::Printf(TEXT("%s: ranged"), Case.What), Hit.bIsRanged, Case.bRanged);
		TestEqual(FString::Printf(TEXT("%s: spell"), Case.What), Hit.bIsSpell, Case.bSpell);
		TestFalse(FString::Printf(TEXT("%s: no boss dealt it"), Case.What), Hit.bFromBoss);
	}

	// A BOSS'S BLOW SAYS SO, AND A HERALD'S, ONE RUNG BELOW, DOES NOT. A boss is
	// what `ACataclysmEnemyCharacter::IsBoss` says, set the way #666's own test
	// sets it. Their target is on the players' side.
	ACataclysmEnemyCharacter* Guarded =
		SpawnCreatureAt(World, FVector(0.0f, 4.0f * M, 0.0f), 1000000.0f);
	ACataclysmEnemyCharacter* Boss =
		SpawnCreatureAt(World, FVector(2.0f * M, 4.0f * M, 0.0f), 1000000.0f);
	ACataclysmEnemyCharacter* Herald =
		SpawnCreatureAt(World, FVector(-2.0f * M, 4.0f * M, 0.0f), 1000000.0f);
	if (!TestNotNull(TEXT("a creature on the players' side"), Guarded)
		|| !TestNotNull(TEXT("a boss"), Boss) || !TestNotNull(TEXT("a Herald"), Herald))
	{
		return false;
	}
	Guarded->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	Boss->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep);
	Herald->SetRarityStep(ACataclysmEnemyCharacter::FirstBossRarityStep - 1);
	Boss->SetAttackDamage(100.0f);
	Herald->SetAttackDamage(100.0f);
	TestTrue(TEXT("the first boss rung is a boss"), Boss->IsBoss());
	TestFalse(TEXT("and the rung below it is not"), Herald->IsBoss());

	const int32 BeforeBoss = Heard.Hits.Num();
	UCataclysmSkillEffects::ApplyHit(Boss, Guarded, 100.0f, TagsNamed({TEXT("Type.Melee")}));
	UCataclysmSkillEffects::ApplyHit(Herald, Guarded, 100.0f, TagsNamed({TEXT("Type.Melee")}));
	const int32 FromBoss = Heard.HitFrom(Boss, BeforeBoss, /*bDamageOverTime=*/false);
	const int32 FromHerald = Heard.HitFrom(Herald, BeforeBoss, /*bDamageOverTime=*/false);
	if (TestTrue(TEXT("the boss's blow was announced"), FromBoss != INDEX_NONE))
	{
		TestTrue(TEXT("and says a boss dealt it"), Heard.Hits[FromBoss].bFromBoss);
	}
	if (TestTrue(TEXT("the Herald's blow was announced"), FromHerald != INDEX_NONE))
	{
		TestFalse(TEXT("and does not say a boss dealt it"), Heard.Hits[FromHerald].bFromBoss);
	}

	// AND A DEATH CARRIES THE KILLING BLOW'S FACTS: a spell, here.
	if (UCataclysmAbilitySystemComponent* TargetSystem = SystemOf(Target))
	{
		TargetSystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	}
	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
									 TagsNamed({TEXT("Type.Spell")}));
	if (TestEqual(TEXT("the spell killed the target"), Heard.Deaths.Num(), 1))
	{
		TestTrue(TEXT("and the death says the killing blow was a spell"),
				 Heard.Deaths[0].bIsSpell);
		TestFalse(TEXT("and not melee"), Heard.Deaths[0].bIsMelee);
	}

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsNoListener,
	"Cataclysm.CombatEvents.NothingIsSentWhenNothingListens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsNoListener::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	// THE COST RULE, CHECKED RATHER THAN CLAIMED. In a world nobody listens to,
	// a blow still lands and nothing is sent.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FArmedActor Attacker = MakeArmed(World);
	ACataclysmEnemyCharacter* Target =
		SpawnCreatureAt(World, FVector(2.0f * M, 0.0f, 0.0f), 1000000.0f);
	if (!TestNotNull(TEXT("the notices subsystem"), Events)
		|| !TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a target"), Target))
	{
		return false;
	}

	const float Before = HealthOf(Target);
	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
									 TagsNamed({TEXT("Type.Melee")}));
	TestTrue(TEXT("a blow with nobody listening still hurts"), HealthOf(Target) < Before);
	TestEqual(TEXT("and sends nothing"), static_cast<int32>(Events->HitsSent()), 0);

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Target, 100.0f,
									 TagsNamed({TEXT("Type.Melee")}));
	TestEqual(TEXT("the same blow with a listener sends one"),
			  static_cast<int32>(Events->HitsSent()), 1);
	TestEqual(TEXT("and the listener heard it"), Heard.Hits.Num(), 1);

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsDeathNotice,
	"Cataclysm.CombatEvents.AKillingBlowIsAnnouncedOnceAndNamesTheKiller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsDeathNotice::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FArmedActor Attacker = MakeArmed(World);
	ACataclysmEnemyCharacter* Victim =
		SpawnCreatureAt(World, FVector(2.0f * M, 0.0f, 0.0f), 10.0f);
	if (!TestNotNull(TEXT("the notices subsystem"), Events)
		|| !TestNotNull(TEXT("an attacker"), Attacker.Actor)
		|| !TestNotNull(TEXT("a victim"), Victim))
	{
		return false;
	}

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Victim, 100.0f,
									 TagsNamed({TEXT("Type.Melee")}));

	TestTrue(TEXT("the blow killed it"), UCataclysmSkillEffects::IsDead(Victim));
	if (!TestEqual(TEXT("one death sends one death notice"), Heard.Deaths.Num(), 1))
	{
		return false;
	}
	const FCataclysmDeathNotice& Death = Heard.Deaths[0];

	TestTrue(TEXT("it names the victim"), Death.Victim == Victim);
	TestTrue(TEXT("and the character the blow is credited to"),
			 Death.Killer == Attacker.Actor);
	TestTrue(TEXT("and what dealt it, which is the same actor"),
			 Death.KillingCauser == Attacker.Actor);
	TestFalse(TEXT("and says it was not damage over time"), Death.bByDamageOverTime);
	TestTrue(TEXT("and that a blow is on record"), Death.SecondsSinceLastBlow >= 0.0f);
	TestTrue(TEXT("and the killing blow's tags say melee"),
			 Heard.KillingTags[0].HasTag(UCataclysmDamageCalculation::MeleeTag()));
	TestTrue(TEXT("and a kill no skill made names no skill"),
			 Death.KillingSkillName.IsNone());
	TestFalse(TEXT("and points at no skill's tags"), Heard.DeathHadSkillTags[0]);

	// A BLOW ON A CORPSE ANNOUNCES NO SECOND DEATH. `MarkDead` refuses a
	// character already dead, and the announcement is inside it.
	UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Victim, 100.0f,
									 TagsNamed({TEXT("Type.Melee")}));
	TestEqual(TEXT("and a blow on the corpse announces no second death"),
			  Heard.Deaths.Num(), 1);

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsMinionKill,
	"Cataclysm.CombatEvents.AMinionsKillCreditsItsSummonerAndNamesTheMinion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsMinionKill::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	// THE KILL IS THE SUMMONER'S AND THE MINION IS NAMED, under today's
	// placeholder minion model, issue #340. The minion is recorded as the
	// effect's source object and the causer stays the summoner, so nothing that
	// works out damage sees anything different.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FArmedActor Summoner = MakeArmed(World);
	ACataclysmEnemyCharacter* Victim =
		SpawnCreatureAt(World, FVector(4.0f * M, 0.0f, 0.0f), 1.0f);
	if (!TestNotNull(TEXT("the notices subsystem"), Events)
		|| !TestNotNull(TEXT("a summoner"), Summoner.Actor)
		|| !TestNotNull(TEXT("a victim"), Victim))
	{
		return false;
	}

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(3.0f * M, 0.0f, 0.0f), /*Lifetime=*/20.0f,
		/*bBurns=*/false);
	if (!TestNotNull(TEXT("a minion"), Imp))
	{
		return false;
	}

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	Imp->AttackTarget(Victim);

	if (!TestEqual(TEXT("the minion's blow sends one hit notice"), Heard.Hits.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("credited to the summoner"), Heard.Hits[0].Attacker == Summoner.Actor);
	TestTrue(TEXT("and dealt by the minion"), Heard.Hits[0].DealtBy == Imp);
	TestEqual(TEXT("and the distance is the minion's, not the summoner's"),
			  Heard.Hits[0].DistanceMetres, 1.0f, 0.05f);
	TestTrue(TEXT("and a minion's blow names no skill"), Heard.Hits[0].SkillName.IsNone());

	TestTrue(TEXT("the blow killed the victim"), UCataclysmSkillEffects::IsDead(Victim));
	if (!TestEqual(TEXT("and sent one death notice"), Heard.Deaths.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("the kill is credited to the summoner"),
			 Heard.Deaths[0].Killer == Summoner.Actor);
	TestTrue(TEXT("and names the minion as what dealt it"),
			 Heard.Deaths[0].KillingCauser == Imp);

	// AND A MINION'S BURN NAMES THE MINION TOO, TICK BY TICK. A burn reaches the
	// target through `ApplyDamageOverTime` rather than as a blow, so the minion
	// is recorded on it by a different line from the one above, and a burn kill
	// by a minion has to name the minion as surely as its swing does.
	ACataclysmMinion* Burner = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(3.0f * M, 3.0f * M, 0.0f), /*Lifetime=*/20.0f,
		/*bBurns=*/true);
	ACataclysmEnemyCharacter* Kindling =
		SpawnCreatureAt(World, FVector(4.0f * M, 3.0f * M, 0.0f), 1000000.0f);
	UCataclysmAbilitySystemComponent* KindlingSystem = SystemOf(Kindling);
	if (!TestNotNull(TEXT("a minion that burns what it hits"), Burner)
		|| !TestNotNull(TEXT("something to set alight"), KindlingSystem))
	{
		return false;
	}

	Burner->AttackTarget(Kindling);
	const int32 HitsBeforeTick = Heard.Hits.Num();
	if (!TestEqual(TEXT("the burning minion's blow set one burn running"),
				   KindlingSystem->ExecutePeriodicEffectsGrantingForTests(
					   TagNamed(TEXT("Keyword.DoT.Burn"))), 1)
		|| !TestEqual(TEXT("and one tick of it sent one hit notice"),
					  Heard.Hits.Num(), HitsBeforeTick + 1))
	{
		return false;
	}
	TestTrue(TEXT("the tick is damage over time"), Heard.Hits.Last().bDamageOverTime);
	TestTrue(TEXT("credited to the summoner"),
			 Heard.Hits.Last().Attacker == Summoner.Actor);
	TestTrue(TEXT("and dealt by the minion that set the fire"),
			 Heard.Hits.Last().DealtBy == Burner);

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsTickDeath,
	"Cataclysm.CombatEvents.ADeathFromATickNamesTheAilment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsTickDeath::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	// A TICK IS A BLOW FOR THIS PURPOSE, AND IT HAS TO SAY WHICH AILMENT IT WAS.
	// `ApplyDamageOverTime` does not carry the ailment among the effect's own
	// tags -- only the bare `Keyword.DoT` travels that way -- it GRANTS the
	// ailment's tag to the target. A notice that read only what the effect
	// carried would say "damage over time" for every case here and never
	// "bleed", and the Demonic trees' kill payouts need the ailment by name.
	//
	// EVERY `Keyword.DoT.*` TAG IN THE VOCABULARY, through the one function every
	// ailment is applied with.
	const TArray<const TCHAR*> Ailments = {
		TEXT("Keyword.DoT.Bleed"), TEXT("Keyword.DoT.Burn"), TEXT("Keyword.DoT.Disease"),
		TEXT("Keyword.DoT.Generic"), TEXT("Keyword.DoT.Necrosis"),
		TEXT("Keyword.DoT.Poison"), TEXT("Keyword.DoT.VoidSplinter"),
	};

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FArmedActor Attacker = MakeArmed(World);
	if (!TestNotNull(TEXT("the notices subsystem"), Events)
		|| !TestNotNull(TEXT("an attacker"), Attacker.Actor))
	{
		return false;
	}

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	const FGameplayTag Bare = UCataclysmDamageCalculation::DamageOverTimeTag();
	TestTrue(TEXT("the bare damage-over-time tag is in the vocabulary"), Bare.IsValid());

	float Along = 2.0f * M;
	for (const TCHAR* Name : Ailments)
	{
		const FGameplayTag Ailment = TagNamed(Name);
		if (!TestTrue(FString::Printf(TEXT("%s is in the vocabulary"), Name),
					  Ailment.IsValid()))
		{
			continue;
		}

		ACataclysmEnemyCharacter* Victim =
			SpawnCreatureAt(World, FVector(Along, 0.0f, 0.0f), 10.0f);
		Along += 2.0f * M;
		UCataclysmAbilitySystemComponent* VictimSystem = SystemOf(Victim);
		if (!TestNotNull(TEXT("a victim with an ability system"), VictimSystem))
		{
			continue;
		}

		const bool bApplied = UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Victim, /*DamagePerTick=*/1000.0f,
			/*DurationSeconds=*/4.0f, Ailment, /*bScalesWithInstigator=*/false);
		if (!TestTrue(FString::Printf(TEXT("%s was applied"), Name), bApplied))
		{
			continue;
		}

		const int32 HitsBefore = Heard.Hits.Num();
		const int32 DeathsBefore = Heard.Deaths.Num();
		TestEqual(FString::Printf(TEXT("one %s ran a tick"), Name),
				  VictimSystem->ExecutePeriodicEffectsGrantingForTests(Ailment), 1);

		if (!TestEqual(FString::Printf(TEXT("the %s tick sends one hit notice"), Name),
					   Heard.Hits.Num(), HitsBefore + 1))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("the %s tick's notice says damage over time"), Name),
				 Heard.Hits.Last().bDamageOverTime);
		TestTrue(FString::Printf(TEXT("and points at what the %s grants"), Name),
				 Heard.HitHadGrantedTags.Last());
		TestTrue(FString::Printf(TEXT("and between the two it names %s"), Name),
				 Heard.HitTags.Last().HasTagExact(Ailment));
		TestTrue(FString::Printf(TEXT("and a %s no skill applied names no skill"), Name),
				 Heard.Hits.Last().SkillName.IsNone());

		TestTrue(FString::Printf(TEXT("the %s tick killed its victim"), Name),
				 UCataclysmSkillEffects::IsDead(Victim));
		if (!TestEqual(FString::Printf(TEXT("and sent one death notice for %s"), Name),
					   Heard.Deaths.Num(), DeathsBefore + 1))
		{
			continue;
		}
		const FCataclysmDeathNotice& Death = Heard.Deaths.Last();
		const FGameplayTagContainer& Killing = Heard.KillingTags.Last();
		TestTrue(FString::Printf(TEXT("a %s death is credited to whoever applied it"), Name),
				 Death.Killer == Attacker.Actor);
		TestTrue(FString::Printf(TEXT("a %s death says it was damage over time"), Name),
				 Death.bByDamageOverTime);
		TestTrue(FString::Printf(TEXT("a %s death's killing tags name %s"), Name, Name),
				 Killing.HasTagExact(Ailment));
		TestTrue(FString::Printf(TEXT("and carry the bare damage-over-time tag, for %s"),
								 Name),
				 Killing.HasTagExact(Bare));
	}

	// AND AN AILMENT A SKILL APPLIED NAMES THE SKILL, ON EVERY TICK. The skill
	// rides on the effect's context from the moment it is applied. The Strike
	// here is only a named skill to hand over; nothing activates it.
	FGameplayAbilitySpecHandle Unused;
	UCataclysmStrikeSkill* Rend = GrantNamedSkill<UCataclysmStrikeSkill>(
		Attacker, ECataclysmAbilitySlot::BasicAttack, TEXT("Rending Cut"),
		TEXT("Item.Weapon.Sword, Type.Strike"), TEXT("Radius=2.4"), Unused);
	ACataclysmEnemyCharacter* Rent =
		SpawnCreatureAt(World, FVector(Along, 0.0f, 0.0f), 10.0f);
	UCataclysmAbilitySystemComponent* RentSystem = SystemOf(Rent);
	const FGameplayTag Bleed = TagNamed(TEXT("Keyword.DoT.Bleed"));
	if (!TestNotNull(TEXT("a named skill"), Rend)
		|| !TestNotNull(TEXT("a victim for it"), RentSystem)
		|| !TestTrue(TEXT("the skill's bleed was applied"),
					 UCataclysmSkillEffects::ApplyDamageOverTime(
						 Attacker.Actor, Rent, /*DamagePerTick=*/1000.0f,
						 /*DurationSeconds=*/4.0f, Bleed, /*bScalesWithInstigator=*/false,
						 /*DealtBy=*/nullptr, /*Skill=*/Rend)))
	{
		return false;
	}

	const int32 HitsBeforeRend = Heard.Hits.Num();
	const int32 DeathsBeforeRend = Heard.Deaths.Num();
	RentSystem->ExecutePeriodicEffectsGrantingForTests(Bleed);
	if (TestEqual(TEXT("the skill's bleed ticked once"), Heard.Hits.Num(), HitsBeforeRend + 1))
	{
		TestEqual(TEXT("and the tick names the skill that applied the bleed"),
				  Heard.Hits.Last().SkillName.ToString(), FString(TEXT("Rending Cut")));
		TestTrue(TEXT("and points at its tags"),
				 Heard.HitSkillTags.Last().HasTagExact(TagNamed(TEXT("Item.Weapon.Sword"))));
	}
	if (TestEqual(TEXT("the tick killed the victim"), Heard.Deaths.Num(), DeathsBeforeRend + 1))
	{
		TestEqual(TEXT("and the death names the skill that applied the bleed"),
				  Heard.Deaths.Last().KillingSkillName.ToString(),
				  FString(TEXT("Rending Cut")));
		TestTrue(TEXT("and points at its tags"), Heard.DeathHadSkillTags.Last());
	}

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsSkillNamed,
	"Cataclysm.CombatEvents.EachSkillNamesItselfOnItsBlowsItsBurnsAndItsKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsSkillNamed::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	// TWO SKILLS OF ONE CLASS, BECAUSE THAT IS THE CASE THAT CAN BE GOT WRONG.
	// Every player skill is an instance of one of the eight template classes,
	// with its name and tags set on the instance from its row. The effect
	// context keeps both the instance and the class default object, and the
	// class default names no skill, so a notice that read the class would name
	// nothing. Two skills of one class, each on its own caster, tell the two
	// apart, which is the check the coordinating session asked for.
	TestTrue(TEXT("the Strike class default names no skill"),
			 GetDefault<UCataclysmStrikeSkill>()->SkillName.IsEmpty());
	TestTrue(TEXT("and carries no tags"),
			 GetDefault<UCataclysmStrikeSkill>()->SkillTags.IsEmpty());

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);

	// ONE METRE IN FRONT, where a bare actor's strike reaches, with health enough
	// to take every blow until the last one.
	ACataclysmEnemyCharacter* Target =
		SpawnCreatureAt(World, FVector(1.0f * M, 0.0f, 0.0f), 1000000.0f);
	UCataclysmAbilitySystemComponent* TargetSystem = SystemOf(Target);
	if (!TestNotNull(TEXT("the notices subsystem"), Events)
		|| !TestNotNull(TEXT("a target"), TargetSystem))
	{
		return false;
	}

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Tags;
		const TCHAR* OnlyItsOwn;
		const TCHAR* Params;
	};
	// THE FIRST ONE BURNS, so a tick it started can be checked too: a burn is
	// applied through `ApplyBurn`, which the skill hands itself to.
	const FCase Cases[] = {
		{TEXT("Cleaving Blow"), TEXT("Item.Weapon.Greataxe, Element.Demonic, Type.Strike"),
			TEXT("Item.Weapon.Greataxe"), TEXT("Radius=2.4; Angle=120; MaxTargets=1; Burn=1")},
		{TEXT("Riposte"), TEXT("Item.Weapon.Sword, Element.War, Type.Strike"),
			TEXT("Item.Weapon.Sword"), TEXT("Radius=2.4; Angle=120; MaxTargets=1")},
	};

	for (const FCase& Case : Cases)
	{
		FArmedActor Caster = MakeArmed(World);
		FGameplayAbilitySpecHandle Handle;
		UCataclysmStrikeSkill* Skill = GrantNamedSkill<UCataclysmStrikeSkill>(
			Caster, ECataclysmAbilitySlot::BasicAttack, Case.Name, Case.Tags, Case.Params,
			Handle);
		if (!TestNotNull(FString::Printf(TEXT("%s was granted"), Case.Name), Skill))
		{
			continue;
		}

		const int32 Was = Heard.Hits.Num();
		if (!TestTrue(FString::Printf(TEXT("%s started"), Case.Name),
					  Caster.AbilitySystem->TryActivateAbility(
						  Handle, /*bAllowRemoteActivation=*/false)))
		{
			continue;
		}

		const int32 Blow = Heard.HitFrom(Caster.Actor, Was, /*bDamageOverTime=*/false);
		if (!TestTrue(FString::Printf(TEXT("%s's blow was announced"), Case.Name),
					  Blow != INDEX_NONE))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s's blow names %s"), Case.Name, Case.Name),
				  Heard.Hits[Blow].SkillName.ToString(), FString(Case.Name));
		TestTrue(FString::Printf(TEXT("and points at %s's tags"), Case.Name),
				 Heard.HitHadSkillTags[Blow]);
		TestTrue(FString::Printf(TEXT("which are its own, holding %s"), Case.OnlyItsOwn),
				 Heard.HitSkillTags[Blow].HasTagExact(TagNamed(Case.OnlyItsOwn)));
	}

	// THE BURN CLEAVING BLOW LEFT, ONE TICK OF IT. Riposte struck the same
	// target afterwards, so a tick that named the last skill to strike rather
	// than the one that set the fire would say Riposte.
	const int32 BeforeTick = Heard.Hits.Num();
	if (TestEqual(TEXT("Cleaving Blow left one burn running"),
				  TargetSystem->ExecutePeriodicEffectsGrantingForTests(
					  TagNamed(TEXT("Keyword.DoT.Burn"))), 1)
		&& TestEqual(TEXT("and one tick of it was announced"),
					 Heard.Hits.Num(), BeforeTick + 1))
	{
		TestTrue(TEXT("the tick is damage over time"), Heard.Hits.Last().bDamageOverTime);
		TestEqual(TEXT("and names the skill that set the fire"),
				  Heard.Hits.Last().SkillName.ToString(), FString(TEXT("Cleaving Blow")));
	}

	// AND A KILL NAMES THE SKILL THAT MADE IT: a third skill of the same class,
	// with the target left one point of health.
	TargetSystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
	FArmedActor Executioner = MakeArmed(World);
	FGameplayAbilitySpecHandle FinisherHandle;
	UCataclysmStrikeSkill* Finisher = GrantNamedSkill<UCataclysmStrikeSkill>(
		Executioner, ECataclysmAbilitySlot::BasicAttack, TEXT("Executioner's Swing"),
		TEXT("Item.Weapon.Greataxe, Type.Strike"), TEXT("Radius=2.4; Angle=120; MaxTargets=1"),
		FinisherHandle);
	if (!TestNotNull(TEXT("a third skill"), Finisher)
		|| !TestTrue(TEXT("and it started"),
					 Executioner.AbilitySystem->TryActivateAbility(
						 FinisherHandle, /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}

	TestTrue(TEXT("the third blow killed the target"), UCataclysmSkillEffects::IsDead(Target));
	if (TestEqual(TEXT("and one death was announced"), Heard.Deaths.Num(), 1))
	{
		TestTrue(TEXT("credited to the third caster"),
				 Heard.Deaths[0].Killer == Executioner.Actor);
		TestEqual(TEXT("and naming the skill that made the kill"),
				  Heard.Deaths[0].KillingSkillName.ToString(),
				  FString(TEXT("Executioner's Swing")));
		TestTrue(TEXT("and pointing at its tags"), Heard.DeathHadSkillTags[0]);
		TestTrue(TEXT("which are its own"),
				 Heard.DeathSkillTags[0].HasTagExact(TagNamed(TEXT("Type.Strike"))));
	}

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsProjectileSkill,
	"Cataclysm.CombatEvents.AProjectileNamesTheSkillThatFiredIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsProjectileSkill::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	// A PROJECTILE LANDS AFTER THE SKILL THAT FIRED IT HAS FINISHED, so the skill
	// travels with it from the moment it is fired, the way the skill's critical
	// strike chance and health cost already do.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FArmedActor Thrower = MakeArmed(World);
	if (!TestNotNull(TEXT("the notices subsystem"), Events)
		|| !TestNotNull(TEXT("a thrower"), Thrower.Actor))
	{
		return false;
	}

	// ENOUGH MANA FOR THE SLOT'S COST, so nothing but the test's subject could
	// stop the throw.
	Thrower.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
	Thrower.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);

	FGameplayAbilitySpecHandle Handle;
	UCataclysmProjectileSkill* Hurl = GrantNamedSkill<UCataclysmProjectileSkill>(
		Thrower, ECataclysmAbilitySlot::Special, TEXT("Hurled Ember"),
		TEXT("Element.Demonic, Type.Projectile"), TEXT("Range=6; Radius=1; Speed=1800"),
		Handle);

	// THREE METRES ALONG THE THROWER'S FACING, which is where a projectile goes
	// when nobody aims it.
	ACataclysmEnemyCharacter* Target =
		SpawnCreatureAt(World, FVector(3.0f * M, 0.0f, 0.0f), 1000000.0f);
	if (!TestNotNull(TEXT("a projectile skill"), Hurl)
		|| !TestNotNull(TEXT("a target"), Target))
	{
		return false;
	}

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	if (!TestTrue(TEXT("the throw started"),
				  Thrower.AbilitySystem->TryActivateAbility(
					  Handle, /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}
	ACataclysmProjectile* Projectile = Hurl->InFlight;
	if (!TestNotNull(TEXT("a projectile in flight"), Projectile))
	{
		return false;
	}
	TestEqual(TEXT("and nothing has landed yet"), Heard.Hits.Num(), 0);

	// THE WORLD NEVER TICKS, so the flight is stepped by hand, a sixtieth of a
	// second at a time, the way the projectile tests step it.
	for (int32 Frame = 0; Frame < 600 && !Projectile->bFinished; ++Frame)
	{
		Projectile->Step(1.0f / 60.0f);
	}
	TestTrue(TEXT("the projectile finished its flight"), Projectile->bFinished);

	const int32 Blow = Heard.HitFrom(Thrower.Actor, 0, /*bDamageOverTime=*/false);
	if (!TestTrue(TEXT("its blow was announced"), Blow != INDEX_NONE))
	{
		return false;
	}
	TestTrue(TEXT("on the target"), Heard.Hits[Blow].Target == Target);
	TestEqual(TEXT("naming the skill that fired it"),
			  Heard.Hits[Blow].SkillName.ToString(), FString(TEXT("Hurled Ember")));
	TestTrue(TEXT("and pointing at that skill's own tags"),
			 Heard.HitSkillTags[Blow].HasTagExact(TagNamed(TEXT("Type.Projectile"))));

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsSkillUsed,
	"Cataclysm.CombatEvents.ASkillIsAnnouncedOnceWhenItIsUsed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsSkillUsed::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	// THE BASIC ATTACK AND A SKILL ON A KEY, EACH ON ITS OWN CASTER. The basic
	// attack is the one most likely to be missed -- it fires by itself and has
	// no row in the Weapon Skills sheet -- and a skill in any other slot is what
	// a player presses. Both reach `CommitAndBegin`, which is where the notice is
	// sent. Two casters rather than one, because a world that never ticks never
	// clears a cooldown.
	//
	// The file's comment says why there is no refused press here.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("the notices subsystem"), Events))
	{
		return false;
	}

	FHeard Heard;
	ListenTo(Events, Heard);
	ON_SCOPE_EXIT { StopListening(Events, Heard); };

	struct FCase
	{
		const TCHAR* What;
		ECataclysmAbilitySlot Slot;
		const TCHAR* Name;
	};
	const FCase Cases[] = {
		{TEXT("the basic attack"), ECataclysmAbilitySlot::BasicAttack, TEXT("Basic Attack")},
		{TEXT("a heavy attack"), ECataclysmAbilitySlot::Heavy, TEXT("Heavy Strike")},
	};

	for (const FCase& Case : Cases)
	{
		FArmedActor User = MakeArmed(World);
		if (!TestNotNull(FString::Printf(TEXT("a user for %s"), Case.What), User.Actor))
		{
			continue;
		}

		// ENOUGH MANA FOR ANY SLOT'S COST, so the only thing that could refuse
		// the press is something this test is not about.
		User.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
		User.AbilitySystem->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);

		FGameplayAbilitySpecHandle Handle;
		UCataclysmStrikeSkill* Skill = GrantNamedSkill<UCataclysmStrikeSkill>(
			User, Case.Slot, Case.Name, TEXT("Type.Strike"),
			TEXT("Radius=2.4; Angle=120; MaxTargets=1"), Handle);
		if (!TestNotNull(FString::Printf(TEXT("%s was granted"), Case.What), Skill))
		{
			continue;
		}

		const int32 Was = Heard.SkillsUsed.Num();
		const bool bStarted =
			User.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false);
		if (!TestTrue(FString::Printf(TEXT("%s started"), Case.What), bStarted)
			|| !TestEqual(FString::Printf(TEXT("%s was announced once"), Case.What),
						  Heard.SkillsUsed.Num(), Was + 1))
		{
			continue;
		}

		const FCataclysmSkillUsedNotice& Used = Heard.SkillsUsed.Last();
		TestTrue(FString::Printf(TEXT("%s names who used it"), Case.What),
				 Used.User == User.Actor);
		TestEqual(FString::Printf(TEXT("%s names the skill"), Case.What),
				  Used.SkillName.ToString(), FString(Case.Name));
		TestEqual(FString::Printf(TEXT("%s names its slot"), Case.What),
				  static_cast<int32>(Used.Slot), static_cast<int32>(Case.Slot));
		TestTrue(FString::Printf(TEXT("%s points at its tags"), Case.What),
				 Heard.SkillHadTags.Last());
	}

	TestEqual(TEXT("and the world counted two"),
			  static_cast<int32>(Events->SkillUsesSent()), 2);

	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCombatEventsCost,
	"Cataclysm.CombatEvents.TheCostOfNoticesAcrossAHordeSizedCrowd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCombatEventsCost::RunTest(const FString&)
{
	using namespace CataclysmCombatEventsTest;

	// THE COORDINATING SESSION ASKED FOR THIS NUMBER, NOT FOR A LIMIT, so the
	// test reports what the notices cost and asserts only that every notice
	// arrived. A time limit would fail on a slow or busy machine and say nothing
	// about the code.
	//
	// 350 CREATURES, the top of what `CataclysmFloorPopulation.h` states for an
	// arena ("73 to 350 creatures in 16 to 102 groups"). Four blows each, timed
	// twice each way, interleaved: from no skill with nobody listening, from a
	// named skill with nobody listening, and from the skill with three
	// listeners, after one untimed round so the first timing does not also pay
	// for warming up. Then a burn on each and one tick of it: a wave on fire.
	//
	// THE FIRST DIFFERENCE IS WHAT CARRYING THE SKILL COSTS, which every blow a
	// skill deals pays whether or not anything listens. The second is what the
	// notices cost. Timed in one run, so a busy machine slows all three alike.
	constexpr int32 Crowd = 350;
	constexpr int32 BlowsEach = 4;
	constexpr int32 Listeners = 3;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	FArmedActor Attacker = MakeArmed(World);
	if (!TestNotNull(TEXT("the notices subsystem"), Events)
		|| !TestNotNull(TEXT("an attacker"), Attacker.Actor))
	{
		return false;
	}

	// THE BLOWS AND THE BURNS COME FROM A NAMED SKILL, so what a notice costs
	// includes reading the skill and its name, which is the most a notice does.
	FGameplayAbilitySpecHandle Unused;
	const UCataclysmStrikeSkill* Swing = GrantNamedSkill<UCataclysmStrikeSkill>(
		Attacker, ECataclysmAbilitySlot::BasicAttack, TEXT("Sweeping Blow"),
		TEXT("Item.Weapon.Greataxe, Type.Strike, Type.Melee"), TEXT("Radius=2.4"),
		Unused);
	if (!TestNotNull(TEXT("a named skill"), Swing))
	{
		return false;
	}
	FCataclysmHitDelivery FromTheSkill;
	FromTheSkill.Skill = Swing;

	TArray<ACataclysmEnemyCharacter*> Creatures;
	const int32 Side = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Crowd)));
	for (int32 Index = 0; Index < Crowd; ++Index)
	{
		const FVector Where((Index % Side + 1) * 1.5f * M, (Index / Side) * 1.5f * M, 0.0f);
		if (ACataclysmEnemyCharacter* Creature = SpawnCreatureAt(World, Where, 1.0e9f))
		{
			Creatures.Add(Creature);
		}
	}
	if (!TestEqual(TEXT("the whole crowd spawned"), Creatures.Num(), Crowd))
	{
		return false;
	}

	const FGameplayTagContainer Melee = TagsNamed({TEXT("Type.Melee")});
	const FCataclysmHitDelivery FromNoSkill;
	const auto StrikeEveryone = [&](const FCataclysmHitDelivery& Delivery)
	{
		const double Started = FPlatformTime::Seconds();
		for (int32 Round = 0; Round < BlowsEach; ++Round)
		{
			for (ACataclysmEnemyCharacter* Creature : Creatures)
			{
				UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Creature, 100.0f, Melee,
												 Delivery);
			}
		}
		return (FPlatformTime::Seconds() - Started) * 1000.0;
	};

	int32 Heard[Listeners] = {};
	TArray<FDelegateHandle> Handles;
	const auto Listen = [&]()
	{
		for (int32 Which = 0; Which < Listeners; ++Which)
		{
			Handles.Add(Events->OnHit.AddLambda(
				[&Heard, Which](const FCataclysmHitNotice& Notice)
				{
					// READS WHAT A REAL LISTENER WOULD: who, which skill, and one tag.
					if (Notice.Attacker && !Notice.SkillName.IsNone()
						&& Notice.HasTag(UCataclysmDamageCalculation::MeleeTag()))
					{
						++Heard[Which];
					}
				}));
		}
	};
	const auto StopListeningAll = [&]()
	{
		for (const FDelegateHandle& Handle : Handles)
		{
			Events->OnHit.Remove(Handle);
		}
		Handles.Reset();
	};
	ON_SCOPE_EXIT { StopListeningAll(); };

	const int32 Blows = Crowd * BlowsEach;
	StrikeEveryone(FromTheSkill);
	const double PlainFirst = StrikeEveryone(FromNoSkill);
	const double NobodyFirst = StrikeEveryone(FromTheSkill);
	Listen();
	const double ListenedFirst = StrikeEveryone(FromTheSkill);
	StopListeningAll();
	const double PlainSecond = StrikeEveryone(FromNoSkill);
	const double NobodySecond = StrikeEveryone(FromTheSkill);
	Listen();
	const double ListenedSecond = StrikeEveryone(FromTheSkill);

	TestEqual(TEXT("only the blows somebody listened to were sent"),
			  static_cast<int32>(Events->HitsSent()), 2 * Blows);
	for (int32 Which = 0; Which < Listeners; ++Which)
	{
		TestEqual(FString::Printf(TEXT("listener %d heard every blow it listened to"),
								  Which + 1),
				  Heard[Which], 2 * Blows);
	}

	// A WAVE ON FIRE: one burn each, and one tick of it each, with the three
	// listeners still bound.
	const FGameplayTag Burn = TagNamed(TEXT("Keyword.DoT.Burn"));
	int32 Burning = 0;
	for (ACataclysmEnemyCharacter* Creature : Creatures)
	{
		Burning += UCataclysmSkillEffects::ApplyDamageOverTime(
			Attacker.Actor, Creature, /*DamagePerTick=*/10.0f, /*DurationSeconds=*/4.0f,
			Burn, /*bScalesWithInstigator=*/false, /*DealtBy=*/nullptr,
			/*Skill=*/Swing) ? 1 : 0;
	}
	TestEqual(TEXT("every creature caught fire"), Burning, Crowd);

	const uint32 HitsBeforeTicks = Events->HitsSent();
	const double TicksStarted = FPlatformTime::Seconds();
	int32 Ticked = 0;
	for (ACataclysmEnemyCharacter* Creature : Creatures)
	{
		if (UCataclysmAbilitySystemComponent* System = SystemOf(Creature))
		{
			Ticked += System->ExecutePeriodicEffectsGrantingForTests(Burn);
		}
	}
	const double TickMs = (FPlatformTime::Seconds() - TicksStarted) * 1000.0;
	TestEqual(TEXT("every creature's burn ticked once"), Ticked, Crowd);
	TestEqual(TEXT("and every tick was announced"),
			  static_cast<int32>(Events->HitsSent() - HitsBeforeTicks), Crowd);

	const double Plain = FMath::Min(PlainFirst, PlainSecond);
	const double Nobody = FMath::Min(NobodyFirst, NobodySecond);
	const double Listened = FMath::Min(ListenedFirst, ListenedSecond);
	UE_LOG(LogTemp, Display,
		TEXT("CataclysmCombatEventsMeasure: %d blows on %d creatures, the faster of two "
			 "runs each: %.1f ms from no skill with nobody listening (%.2f us a blow), "
			 "%.1f ms from a named skill with nobody listening (%.2f us a blow), %.1f ms "
			 "from the skill with %d listeners (%.2f us a blow). Carrying the skill cost "
			 "%.2f us a blow and the notices %.2f us a blow. Runs: no skill %.1f and "
			 "%.1f ms, skill %.1f and %.1f ms, listened %.1f and %.1f ms. %d burn ticks "
			 "from the skill with %d listeners took %.1f ms (%.2f us a tick)."),
		Blows, Crowd, Plain, Plain * 1000.0 / Blows, Nobody, Nobody * 1000.0 / Blows,
		Listened, Listeners, Listened * 1000.0 / Blows,
		(Nobody - Plain) * 1000.0 / Blows, (Listened - Nobody) * 1000.0 / Blows,
		PlainFirst, PlainSecond, NobodyFirst, NobodySecond, ListenedFirst,
		ListenedSecond, Crowd, Listeners, TickMs, TickMs * 1000.0 / Crowd);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
