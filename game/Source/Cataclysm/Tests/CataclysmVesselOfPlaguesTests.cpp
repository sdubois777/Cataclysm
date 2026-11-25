// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Vessel of Plagues, the Flagellant limb's first keystone, as the project owner
 * approved it on 2026-11-24: "Debuffs on you last 50% longer, and each debuff on
 * you counts twice for everything that counts the debuffs on you."
 *
 * WHAT IT REPLACED. The old second clause was "debuffs on you deal 50% more
 * damage to you", a drawback; the owner's rule of 2026-11-24 makes keystones
 * upgrades by default. The new clause is `debuffs_carried_count_twice`, read by
 * `UCataclysmDebuffs::CountsTwiceOn` where the pipeline's count of debuffs
 * carried is built, so every node paying per debuff on you -- Flagellant's
 * Fervour, Doctrine of Pain's damage, Penitent's regeneration and the rest --
 * pays twice, and nothing else about the debuffs changes.
 *
 * TWO NODES' SHAPES ARE USED TO READ IT: Flagellant's 5 Fervour a second per
 * debuff and an increase of 4% attack damage per debuff, each recorded the way
 * `ApplyTo` records a scaled row.
 */
namespace CataclysmVesselOfPlaguesTest
{
	using Resource = UCataclysmClassResourceAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	struct FPlagueVessel
	{
		explicit FPlagueVessel(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
		}

		~FPlagueVessel()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Flagellant's and an attack damage node's rows, and the keystone's switch if asked. */
		void Record(bool bVesselOfPlagues) const
		{
			FCataclysmStatModifier FervourPerDebuff;
			FervourPerDebuff.Bucket = ECataclysmStatBucket::Flat;
			FervourPerDebuff.Source = ECataclysmModifierSource::PassiveKeystone;
			FervourPerDebuff.Value = 5.0f;
			FervourPerDebuff.Scale = ECataclysmStatScale::PerDebuffCarried;
			FervourPerDebuff.ScaleStep = 1.0f;

			FCataclysmStatModifier DamagePerDebuff;
			DamagePerDebuff.Bucket = ECataclysmStatBucket::Increased;
			DamagePerDebuff.Source = ECataclysmModifierSource::PassiveKeystone;
			DamagePerDebuff.Value = 4.0f;
			DamagePerDebuff.Scale = ECataclysmStatScale::PerDebuffCarried;
			DamagePerDebuff.ScaleStep = 1.0f;

			TMap<FName, FCataclysmStatInputs> Inputs;
			Inputs.FindOrAdd(FName(UCataclysmFervour::PerSecondStat)).Modifiers = {FervourPerDebuff};
			FCataclysmStatInputs& Damage = Inputs.FindOrAdd(FName(TEXT("attack_damage")));
			Damage.Base = 100.0f;
			Damage.Modifiers = {DamagePerDebuff};
			if (bVesselOfPlagues)
			{
				FCataclysmStatModifier Twice;
				Twice.Bucket = ECataclysmStatBucket::Flat;
				Twice.Source = ECataclysmModifierSource::PassiveKeystone;
				Twice.Value = 1.0f;
				Inputs.FindOrAdd(FName(UCataclysmDebuffs::CountTwiceStat)).Modifiers = {Twice};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		/** Two distinct debuffs, as a skill lays them: a bleed and a Cripple. */
		bool CarryTwo() const
		{
			const bool bBleed = UCataclysmSkillEffects::ApplyTagForDuration(
				Actor, Actor, UCataclysmDebuffs::BleedTag(), 30.0f);
			const bool bCripple = UCataclysmSkillEffects::ApplyTagForDuration(
				Actor, Actor, UCataclysmDebuffs::CrippleTag(), 30.0f);
			return bBleed && bCripple;
		}

		float FervourASecond() const
		{
			return AbilitySystem->StatForSkill(FName(UCataclysmFervour::PerSecondStat), FGameplayTagContainer(), 0.0f);
		}

		float AttackDamage() const
		{
			return AbilitySystem->StatForSkill(FName(TEXT("attack_damage")), FGameplayTagContainer(), 0.0f);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVesselOfPlaguesCountsTwiceTest,
	"Cataclysm.VesselOfPlagues.EachDebuffOnItsHolderCountsTwiceForTheNodesThatCountThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two debuffs on a holder of the keystone read as four: Flagellant's 5 Fervour a
 * second per debuff gives 20 rather than 10, and 4% increased attack damage per
 * debuff gives 116 on a base of 100 rather than 108. The plain count of distinct
 * debuffs, which the spreading nodes choose from, is still two.
 */
bool FCataclysmVesselOfPlaguesCountsTwiceTest::RunTest(const FString&)
{
	using namespace CataclysmVesselOfPlaguesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FPlagueVessel Holder(World);
	Holder.Record(/*bVesselOfPlagues=*/true);
	if (!TestTrue(TEXT("set-up: a bleed and a Cripple are laid"), Holder.CarryTwo())
		|| !TestEqual(TEXT("set-up: two distinct debuffs"), UCataclysmDebuffs::CountOn(Holder.AbilitySystem), 2))
	{
		return false;
	}

	TestTrue(TEXT("the keystone's switch is read"), UCataclysmDebuffs::CountsTwiceOn(Holder.AbilitySystem));
	TestEqual(TEXT("the nodes count them twice: four"), UCataclysmDebuffs::CountCarriedOn(Holder.AbilitySystem), 4);
	TestEqual(TEXT("Flagellant's Fervour is 5 a second for each of four: 20"), Holder.FervourASecond(), 20.0f, 0.01f);
	TestEqual(TEXT("4% increased attack damage per debuff on a base of 100: 116"), Holder.AttackDamage(), 116.0f,
			  0.01f);
	TestEqual(TEXT("and the plain count of distinct debuffs is still two"),
			  UCataclysmDebuffs::CountOn(Holder.AbilitySystem), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVesselOfPlaguesControlTest,
	"Cataclysm.VesselOfPlagues.WithoutItEachDebuffCountsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL: the same two debuffs and rows without the switch: 10 Fervour a
 *  second and 108 attack damage. */
bool FCataclysmVesselOfPlaguesControlTest::RunTest(const FString&)
{
	using namespace CataclysmVesselOfPlaguesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FPlagueVessel Holder(World);
	Holder.Record(/*bVesselOfPlagues=*/false);
	if (!TestTrue(TEXT("set-up: a bleed and a Cripple are laid"), Holder.CarryTwo()))
	{
		return false;
	}

	TestFalse(TEXT("no switch is recorded"), UCataclysmDebuffs::CountsTwiceOn(Holder.AbilitySystem));
	TestEqual(TEXT("the nodes count two"), UCataclysmDebuffs::CountCarriedOn(Holder.AbilitySystem), 2);
	TestEqual(TEXT("Flagellant's Fervour is 10 a second"), Holder.FervourASecond(), 10.0f, 0.01f);
	TestEqual(TEXT("and attack damage is 108"), Holder.AttackDamage(), 108.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmVesselOfPlaguesNothingCarriedTest,
	"Cataclysm.VesselOfPlagues.TwiceNothingIsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A holder carrying no debuff counts none: the keystone doubles a count and
 *  adds no debuff of its own. */
bool FCataclysmVesselOfPlaguesNothingCarriedTest::RunTest(const FString&)
{
	using namespace CataclysmVesselOfPlaguesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FPlagueVessel Holder(World);
	Holder.Record(/*bVesselOfPlagues=*/true);
	TestEqual(TEXT("no debuff is carried"), UCataclysmDebuffs::CountOn(Holder.AbilitySystem), 0);
	TestEqual(TEXT("so the nodes count none"), UCataclysmDebuffs::CountCarriedOn(Holder.AbilitySystem), 0);
	TestEqual(TEXT("and Flagellant gives nothing"), Holder.FervourASecond(), 0.0f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
