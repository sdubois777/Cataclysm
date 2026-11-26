// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmDetonation.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmHordeAscendant.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Necromancer's detonations: the Charnel limb and two vows. The finishing of the Death Cataclysm, slice C
 * (2026-11-26); `docs/DECISIONS.md` carries the judgements.
 *
 * Lit Fuse makes every weapon skill (not the basic attack) detonate the minions in its area, and a skill with neither
 * the row's key nor Lit Fuse leaves them standing. Made Room detonates a minion released for a thrall, as the skill that
 * raised it. A detonation's figure takes Made Room's increases and its area, Close Packed's fellows, Swollen's age and
 * Bursting Ranks' recent detonations, each up to five. Lingering Rot leaves ground dealing its share each second. Ripened
 * explodes a minion whose time ends. Mass Grave detonates the minions near the first at 40%, one ring only. Last Breath
 * pays twice the reserve. Living Bombs go off at contact and may raise a husk. Teeming Grave raises one more. The Horde
 * Ascendant has every follower within 10 metres cast the skill at its share. And the real rows.
 *
 * EVERY ROW BY HAND but the last test's, so each mechanism is measured on its own; the row test reads
 * `DT_PassiveEffects`, which the asset step rebuilds after the CSVs change. A test that raises a husk reads
 * `DT_WeaponSkills` for the row that raises one.
 *
 * WRITTEN WITHOUT A BUILD. Every figure is reasoned from the code; the window that proves them is the coordinating
 * session's.
 */
namespace CataclysmNecromancerDetonationTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** Centimetres in a metre. */
	constexpr float Metre = 100.0f;

	/** What a bare fighter's weapon deals. */
	constexpr float WeaponDamage = 100.0f;

	/**
	 * A bare actor a sphere search finds, holding what a skill reads and pays from, critical strikes off: the shape of
	 * the Death skills' `FDeathSkillFighter`, under its own name so the files can share a unity build. Teamless, so it
	 * is every other body's enemy until it is sided with the players.
	 */
	struct FDetonationFighter
	{
		FDetonationFighter(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), WeaponDamage);
			Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
		}

		~FDetonationFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Health() const
		{
			return Get(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		/** One line per stat, each holding the rows given, replacing every line held before. */
		void Lines(const TArray<TPair<const TCHAR*, FCataclysmStatModifier>>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			for (const TPair<const TCHAR*, FCataclysmStatModifier>& Row : Rows)
			{
				Inputs.FindOrAdd(FName(Row.Key)).Modifiers.Add(Row.Value);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A creature on the players' side, to whom a fighter is attached so the minions it raises are the players'. */
	struct FDetonationPlayersSide
	{
		FDetonationPlayersSide(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		}

		~FDetonationPlayersSide()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};

	/** Put a fighter on the players' side, as the Death skills' tests do: its owner's team is its team. */
	void SideWithThePlayers(const FDetonationFighter& Fighter, const FDetonationPlayersSide& Side)
	{
		Fighter.Actor->SetOwner(Side.Actor);
	}

	/** One row of a stat line, a passive keystone's. */
	FCataclysmStatModifier Row(ECataclysmStatBucket Bucket, float Value)
	{
		FCataclysmStatModifier Made;
		Made.Bucket = Bucket;
		Made.Source = ECataclysmModifierSource::PassiveKeystone;
		Made.Value = Value;
		return Made;
	}

	FCataclysmStatModifier Flat(float Value)
	{
		return Row(ECataclysmStatBucket::Flat, Value);
	}

	FCataclysmStatModifier Increased(float Value)
	{
		return Row(ECataclysmStatBucket::Increased, Value);
	}

	/** An increase of `Value` a step of one, by `Scale`, at most `Cap` steps. */
	FCataclysmStatModifier IncreasedPer(float Value, ECataclysmStatScale Scale, int32 Cap, float ReachMetres = -1.0f)
	{
		FCataclysmStatModifier Made = Increased(Value);
		Made.Scale = Scale;
		Made.ScaleStep = 1.0f;
		Made.ScaleMaxSteps = Cap;
		Made.ReachMetres = ReachMetres;
		return Made;
	}

	/** An Imp of `Summoner`'s at `Where`, told Summon Imp's 3 m explosion radius as a summon would tell it. */
	ACataclysmMinion* ImpOf(AActor* Summoner, const FVector& Where, float Lifetime = 20.0f)
	{
		ACataclysmMinion* Minion =
			ACataclysmMinion::Spawn(Summoner, Where, Lifetime, /*bBurns=*/false, FString(TEXT("Imp")));
		if (Minion)
		{
			Minion->RecordExplosionRadius(3.0f * Metre);
		}
		return Minion;
	}

	/** Grant a template into a slot at level 100, its cell parsed as the game parses a row's. */
	template <typename T>
	T* Grant(const FDetonationFighter& Fighter, ECataclysmAbilitySlot Slot, const FString& ParamText, const FString& Name)
	{
		const FGameplayAbilitySpecHandle Handle =
			Fighter.AbilitySystem->GiveAbilityInSlot(T::StaticClass(), Slot, /*Level=*/100, Fighter.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? Fighter.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = Name;
			Instance->Params = UCataclysmSkillShapes::ParseParams(ParamText);
		}
		return Instance;
	}

	/** Destroy every minion still standing at the end of a case. */
	void Clear(const TArray<ACataclysmMinion*>& Minions)
	{
		for (ACataclysmMinion* Minion : Minions)
		{
			if (IsValid(Minion))
			{
				Minion->Destroy();
			}
		}
	}

	/** What `Caster` sends at `Twin` as `Skill`'s area hit at `Percent` of its weapon: the figure a detonation sends. */
	float TwinStruck(const FDetonationFighter& Caster, const FDetonationFighter& Twin, UCataclysmSkillTemplate* Skill,
					 float Percent)
	{
		FCataclysmHitDelivery AsTheSkill;
		AsTheSkill.bIsArea = true;
		AsTheSkill.Skill = Skill;
		return UCataclysmSkillEffects::ApplyHit(Caster.Actor, Twin.Actor, Percent, Skill->SkillTagsForStats(), AsTheSkill);
	}

	/** The ground zones `Owner` has left, standing now. */
	TArray<ACataclysmGroundZone*> ZonesOf(UWorld* World, const AActor* Owner)
	{
		TArray<ACataclysmGroundZone*> Found;
		for (TActorIterator<ACataclysmGroundZone> Zone(World); Zone; ++Zone)
		{
			if (IsValid(*Zone) && Zone->GetOwner() == Owner)
			{
				Found.Add(*Zone);
			}
		}
		return Found;
	}
}

// ==========================================================================
// Lit Fuse, and the key asked around the minions
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationLitFuseTest,
	"Cataclysm.NecromancerDetonation.LitFuseMakesAWeaponSkillDetonateItsMinionsAndASkillWithNeitherLeavesThemStanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Lit Fuse, `Necromancer_basic_ch_stem0` ("Your weapon skills that land on one of your own minions detonate it"): a
 * 4 m, 90 degree swing in the Heavy slot, stating no `Detonates`, explodes the caster's Imp 2 m ahead when the caster
 * holds `weapon_skills_detonate_own_minions`. THE CONTROLS: the same swing without the stat leaves the Imp standing --
 * the coordinating session's requirement of 2026-11-26, now that every landing reaches
 * `UCataclysmSkillTemplate::DetonateOwnMinionsWhere` and the key is asked there -- and the same swing in the basic
 * attack's slot, holding the stat, leaves it standing too (the judgement that the basic attack is no weapon skill).
 */
bool FCataclysmNecromancerDetonationLitFuseTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDetonationPlayersSide Side(World, FVector(0.0f, -500.0f * Metre, 0.0f));
	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	struct FSwung
	{
		int32 Detonated = -1;
		bool bImpStands = false;
		bool bSetUp = false;
	};
	const auto Swing = [&](bool bLitFuse, ECataclysmAbilitySlot Slot, float AcrossMetres) -> FSwung
	{
		FSwung Out;
		const float Y = AcrossMetres * Metre;
		FDetonationFighter Caster(World, FVector(0.0f, Y, 0.0f));
		SideWithThePlayers(Caster, Side);
		if (bLitFuse)
		{
			Caster.Lines({{UCataclysmDetonation::WeaponSkillsDetonateOwnMinionsStat, Flat(1.0f)}});
		}
		ACataclysmMinion* Imp = ImpOf(Caster.Actor, FVector(2.0f * Metre, Y, 0.0f));
		Made.Add(Imp);
		UCataclysmStrikeSkill* Strike =
			Grant<UCataclysmStrikeSkill>(Caster, Slot, TEXT("Radius=4; Angle=90"), TEXT("A plain swing"));
		if (!Imp || !Strike)
		{
			return Out;
		}
		Out.bSetUp = true;
		Strike->SwingOnce();
		Out.Detonated = Strike->MinionsDetonated;
		Out.bImpStands = IsValid(Imp);
		return Out;
	};

	const FSwung WithLitFuse = Swing(true, ECataclysmAbilitySlot::Heavy, 0.0f);
	const FSwung Without = Swing(false, ECataclysmAbilitySlot::Heavy, 100.0f);
	const FSwung BasicAttack = Swing(true, ECataclysmAbilitySlot::BasicAttack, 200.0f);
	if (!TestTrue(TEXT("set-up: each scene has its Imp and its swing"),
				  WithLitFuse.bSetUp && Without.bSetUp && BasicAttack.bSetUp))
	{
		return false;
	}
	TestEqual(TEXT("LIT FUSE: a swing stating no Detonates explodes the Imp inside its arc"), WithLitFuse.Detonated, 1);
	TestFalse(TEXT("and the Imp is gone"), WithLitFuse.bImpStands);
	TestEqual(TEXT("CONTROL: the same swing with neither the key nor Lit Fuse explodes nothing"), Without.Detonated, 0);
	TestTrue(TEXT("CONTROL: and its Imp stands"), Without.bImpStands);
	TestEqual(TEXT("CONTROL: the basic attack, with Lit Fuse held, explodes nothing"), BasicAttack.Detonated, 0);
	TestTrue(TEXT("CONTROL: and its Imp stands"), BasicAttack.bImpStands);
	return true;
}

// ==========================================================================
// The detonation's figure: Made Room's increases, Close Packed, Swollen and Bursting Ranks
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationFigureTest,
	"Cataclysm.NecromancerDetonation.ADetonationTakesItsIncreasesAndItsAreaAndEachCountedIncreaseStopsAtFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmDetonation::DamageMultiplierFor` and `AreaMultiplierFor`, read off the summoner with the minion named:
 *   - Made Room's two rows, 20% increased each (ten points): 1.2 and 1.2.
 *   - Close Packed's row, 1% a minion within 3 m of the one asking, at most 5: two fellows at 2 m give 1.02; seven give
 *     1.05; a fellow at 4 m and a machine at 1 m count nothing (THE CONTROLS).
 *   - Swollen's row, 1% a whole second the minion has stood, at most 5: 3.5 s gives 1.03; 10 s gives 1.05.
 *   - Bursting Ranks' row, 1% an own stack the event `minion_detonated` grants, 2 s, at most 5: three detonations noted
 *     give 1.03; seven give 1.05; 2.5 s later, nothing (THE CONTROL).
 * With nothing held, both multipliers are 1 (THE CONTROL).
 */
bool FCataclysmNecromancerDetonationFigureTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	FDetonationFighter Summoner(World, FVector::ZeroVector);
	ACataclysmMinion* Asking = ImpOf(Summoner.Actor, FVector(10.0f * Metre, 0.0f, 0.0f));
	Made.Add(Asking);
	if (!TestNotNull(TEXT("set-up: the Imp asking"), Asking))
	{
		return false;
	}

	TestEqual(TEXT("CONTROL: with nothing held, the damage multiplier is 1"),
			  UCataclysmDetonation::DamageMultiplierFor(Asking), 1.0f, 0.0001f);
	TestEqual(TEXT("CONTROL: and the area multiplier is 1"), UCataclysmDetonation::AreaMultiplierFor(Asking), 1.0f,
			  0.0001f);

	// MADE ROOM, TEN POINTS.
	Summoner.Lines({{UCataclysmDetonation::DetonationDamageStat, Increased(20.0f)},
					{UCataclysmDetonation::DetonationAreaStat, Increased(20.0f)}});
	TestEqual(TEXT("MADE ROOM: 20% increased detonation damage"), UCataclysmDetonation::DamageMultiplierFor(Asking), 1.2f,
			  0.0001f);
	TestEqual(TEXT("MADE ROOM: and 20% increased area"), UCataclysmDetonation::AreaMultiplierFor(Asking), 1.2f, 0.0001f);

	// CLOSE PACKED: two fellows within 3 m, one at 4 m, a machine at 1 m.
	Summoner.Lines({{UCataclysmDetonation::DetonationDamageStat,
					 IncreasedPer(1.0f, ECataclysmStatScale::PerOtherMinionNearTheMinion, 5, /*ReachMetres=*/3.0f)}});
	Made.Add(ImpOf(Summoner.Actor, FVector(12.0f * Metre, 0.0f, 0.0f)));
	Made.Add(ImpOf(Summoner.Actor, FVector(8.0f * Metre, 0.0f, 0.0f)));
	Made.Add(ImpOf(Summoner.Actor, FVector(10.0f * Metre, 4.0f * Metre, 0.0f)));
	ACataclysmMinion* Machine = ACataclysmMinion::Spawn(Summoner.Actor, FVector(10.0f * Metre, -1.0f * Metre, 0.0f),
														20.0f, false, FString(TEXT("BoltTurret")));
	Made.Add(Machine);
	if (!TestTrue(TEXT("set-up: the turret is a machine"), Machine && Machine->bIsMachine))
	{
		return false;
	}
	TestEqual(TEXT("CLOSE PACKED: two fellows within 3 m, 2%; the one at 4 m and the machine count nothing"),
			  UCataclysmDetonation::DamageMultiplierFor(Asking), 1.02f, 0.0001f);
	for (int32 More = 0; More < 5; ++More)
	{
		Made.Add(ImpOf(Summoner.Actor, FVector(10.0f * Metre, (1.0f + 0.3f * More) * Metre, 0.0f)));
	}
	TestEqual(TEXT("CLOSE PACKED: seven fellows within 3 m stop at five, 5%"),
			  UCataclysmDetonation::DamageMultiplierFor(Asking), 1.05f, 0.0001f);

	// SWOLLEN: the world's seconds moved by hand, as the Death skills' tests move them.
	Summoner.Lines({{UCataclysmDetonation::DetonationDamageStat,
					 IncreasedPer(1.0f, ECataclysmStatScale::PerSecondTheMinionHasBeenActive, 5)}});
	const float Born = World->TimeSeconds;
	World->TimeSeconds = Born + 3.5f;
	TestEqual(TEXT("SWOLLEN: 3.5 s stood, three whole seconds, 3%"), UCataclysmDetonation::DamageMultiplierFor(Asking),
			  1.03f, 0.0001f);
	World->TimeSeconds = Born + 10.0f;
	TestEqual(TEXT("SWOLLEN: 10 s stood stop at five, 5%"), UCataclysmDetonation::DamageMultiplierFor(Asking), 1.05f,
			  0.0001f);

	// BURSTING RANKS: own stacks the detonation event grants.
	const FName Key(TEXT("Passive_Necromancer_basic_ch_a2:minion_detonation_damage"));
	FCataclysmPoolAction Grant;
	Grant.Event = FName(UCataclysmDetonation::DetonatedEvent);
	Grant.StackKey = Key;
	Grant.StackSeconds = 2.0f;
	Grant.StackCap = 5;
	Grant.TriggerKey = FName(TEXT("Passive_Necromancer_basic_ch_a2:minion_detonated"));
	Summoner.AbilitySystem->SetPoolActions({Grant});
	FCataclysmStatModifier PerStack = IncreasedPer(1.0f, ECataclysmStatScale::PerOwnStack, 5);
	PerStack.StackKey = Key;
	Summoner.Lines({{UCataclysmDetonation::DetonationDamageStat, PerStack}});
	TestEqual(TEXT("CONTROL: no detonation yet, no stack"), UCataclysmDetonation::DamageMultiplierFor(Asking), 1.0f,
			  0.0001f);
	for (int32 Each = 0; Each < 3; ++Each)
	{
		UCataclysmDetonation::NoteDetonation(Summoner.Actor);
	}
	TestEqual(TEXT("BURSTING RANKS: three detonations, 3%"), UCataclysmDetonation::DamageMultiplierFor(Asking), 1.03f,
			  0.0001f);
	for (int32 Each = 0; Each < 4; ++Each)
	{
		UCataclysmDetonation::NoteDetonation(Summoner.Actor);
	}
	TestEqual(TEXT("BURSTING RANKS: seven stop at five, 5%"), UCataclysmDetonation::DamageMultiplierFor(Asking), 1.05f,
			  0.0001f);
	World->TimeSeconds += 2.5f;
	TestEqual(TEXT("CONTROL: 2.5 s after the last, nothing"), UCataclysmDetonation::DamageMultiplierFor(Asking), 1.0f,
			  0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationBlastTest,
	"Cataclysm.NecromancerDetonation.ADetonationsIncreaseRaisesItsBlastAndItsAreaWidensItsReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Through a real swing stating `Detonates=1` (`ACataclysmMinion::ExplodeSetOffBy`): with 50% increased
 * `minion_detonation_damage` the enemy beside the Imp loses what a twin struck at 300% of the caster's weapon (200%
 * times 1.5) as the swing loses. With 100% increased `minion_detonation_area` an enemy 5 m from the Imp -- outside its
 * 3 m, inside 6 m -- is struck. THE CONTROL: without the area row the enemy 5 m away is not.
 */
bool FCataclysmNecromancerDetonationBlastTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDetonationPlayersSide Side(World, FVector(0.0f, -500.0f * Metre, 0.0f));
	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	struct FBlast
	{
		float NearLost = -1.0f;
		float FarLost = -1.0f;
		float TwinLost = -1.0f;
		bool bSetUp = false;
	};
	const auto Detonate = [&](float DamageIncrease, float AreaIncrease, float AcrossMetres) -> FBlast
	{
		FBlast Out;
		const float Y = AcrossMetres * Metre;
		FDetonationFighter Caster(World, FVector(0.0f, Y, 0.0f));
		SideWithThePlayers(Caster, Side);
		TArray<TPair<const TCHAR*, FCataclysmStatModifier>> Rows;
		if (DamageIncrease > 0.0f)
		{
			Rows.Add({UCataclysmDetonation::DetonationDamageStat, Increased(DamageIncrease)});
		}
		if (AreaIncrease > 0.0f)
		{
			Rows.Add({UCataclysmDetonation::DetonationAreaStat, Increased(AreaIncrease)});
		}
		Caster.Lines(Rows);
		// THE IMP 2 m AHEAD; AN ENEMY 2.7 m FROM IT, 1 m AHEAD AND 2.5 m ASIDE, outside the 90 degree arc; AND ONE 5 m
		// FROM IT ON THE OTHER SIDE, outside the arc and the swing's 4 m.
		ACataclysmMinion* Imp = ImpOf(Caster.Actor, FVector(2.0f * Metre, Y, 0.0f));
		Made.Add(Imp);
		FDetonationFighter Near(World, FVector(1.0f * Metre, Y + 2.5f * Metre, 0.0f));
		FDetonationFighter Far(World, FVector(2.0f * Metre, Y - 5.0f * Metre, 0.0f));
		FDetonationFighter Twin(World, FVector(0.0f, Y + 40.0f * Metre, 0.0f));
		UCataclysmStrikeSkill* Strike = Grant<UCataclysmStrikeSkill>(
			Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=90; Detonates=1"), TEXT("A detonating swing"));
		if (!Imp || !Strike)
		{
			return Out;
		}
		Out.bSetUp = true;
		const float TwinBefore = Twin.Health();
		TwinStruck(Caster, Twin, Strike,
				   ACataclysmMinion::DetonatedPercentOfWeaponDamage * (1.0f + DamageIncrease / 100.0f));
		Out.TwinLost = TwinBefore - Twin.Health();
		const float NearBefore = Near.Health();
		const float FarBefore = Far.Health();
		Strike->SwingOnce();
		Out.NearLost = NearBefore - Near.Health();
		Out.FarLost = FarBefore - Far.Health();
		return Out;
	};

	const FBlast Raised = Detonate(50.0f, 0.0f, 0.0f);
	const FBlast Widened = Detonate(0.0f, 100.0f, 100.0f);
	if (!TestTrue(TEXT("set-up: each scene"), Raised.bSetUp && Widened.bSetUp)
		|| !TestTrue(TEXT("set-up: the twin was struck"), Raised.TwinLost > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("50% INCREASED DETONATION DAMAGE: the enemy beside the Imp loses what its twin lost at 300%"),
			  Raised.NearLost, Raised.TwinLost, Raised.TwinLost * 0.01f);
	TestEqual(TEXT("CONTROL: the enemy 5 m from the Imp, outside its 3 m, is not struck"), Raised.FarLost, 0.0f, 0.001f);
	TestTrue(TEXT("100% INCREASED DETONATION AREA: the enemy 5 m from the Imp is struck"), Widened.FarLost > 0.0f);
	return true;
}

// ==========================================================================
// Made Room
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationMadeRoomTest,
	"Cataclysm.NecromancerDetonation.MadeRoomDetonatesAMinionReleasedForAThrallAsTheSkillThatRaisedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Made Room, `Necromancer_basic_ch_stem1`: an Imp holding 140 of its commander's 150 Fervour is released to make room for
 * a thrall reserving 30 (`UCataclysmCommand::ReleaseOldestForAThrall`). With `minion_detonates_when_released` it
 * detonates as the summon skill that raised it (its `SummonedBy`): the enemy beside it loses what a twin struck at 200%
 * of the commander's weapon as that skill loses. THE CONTROL: without the stat the Imp is released all the same and the
 * enemy loses nothing.
 */
bool FCataclysmNecromancerDetonationMadeRoomTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDetonationPlayersSide Side(World, FVector(0.0f, -500.0f * Metre, 0.0f));
	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	struct FReleased
	{
		int32 Released = -1;
		bool bImpStands = true;
		float Lost = -1.0f;
		float TwinLost = -1.0f;
		bool bSetUp = false;
	};
	const auto Release = [&](bool bMadeRoom, float AcrossMetres) -> FReleased
	{
		FReleased Out;
		const float Y = AcrossMetres * Metre;
		FDetonationFighter Commander(World, FVector(0.0f, Y, 0.0f));
		SideWithThePlayers(Commander, Side);
		if (bMadeRoom)
		{
			Commander.Lines({{UCataclysmDetonation::DetonatesWhenReleasedStat, Flat(1.0f)}});
		}
		UCataclysmSummonSkill* Raiser = Grant<UCataclysmSummonSkill>(
			Commander, ECataclysmAbilitySlot::Special, TEXT("Minions=Imp:1; Radius=3"), TEXT("The skill that raised it"));
		ACataclysmMinion* Imp = ImpOf(Commander.Actor, FVector(3.0f * Metre, Y, 0.0f));
		Made.Add(Imp);
		FDetonationFighter Near(World, FVector(4.5f * Metre, Y, 0.0f));
		FDetonationFighter Twin(World, FVector(0.0f, Y + 40.0f * Metre, 0.0f));
		if (!Raiser || !Imp)
		{
			return Out;
		}
		Imp->SummonedBy = Raiser;
		UCataclysmCommand::HoldReserve(Commander.Actor, Imp, 140.0f);
		Out.bSetUp = !UCataclysmCommand::HasRoomForAnotherThrall(Commander.Actor, 30.0f);

		const float TwinBefore = Twin.Health();
		TwinStruck(Commander, Twin, Raiser, ACataclysmMinion::DetonatedPercentOfWeaponDamage);
		Out.TwinLost = TwinBefore - Twin.Health();
		const float Before = Near.Health();
		Out.Released = UCataclysmCommand::ReleaseOldestForAThrall(Commander.Actor, 30.0f);
		Out.bImpStands = IsValid(Imp) && !Imp->IsActorBeingDestroyed();
		Out.Lost = Before - Near.Health();
		return Out;
	};

	const FReleased WithMadeRoom = Release(true, 0.0f);
	const FReleased Without = Release(false, 100.0f);
	if (!TestTrue(TEXT("set-up: each commander's pool has no room for a thrall until the Imp goes"),
				  WithMadeRoom.bSetUp && Without.bSetUp)
		|| !TestTrue(TEXT("set-up: the twin was struck"), WithMadeRoom.TwinLost > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("MADE ROOM: one released"), WithMadeRoom.Released, 1);
	TestFalse(TEXT("and the Imp is gone"), WithMadeRoom.bImpStands);
	TestEqual(TEXT("MADE ROOM: the enemy beside it loses what the twin lost to the raising skill at 200%"),
			  WithMadeRoom.Lost, WithMadeRoom.TwinLost, WithMadeRoom.TwinLost * 0.01f);
	TestEqual(TEXT("CONTROL: without the node the Imp is released"), Without.Released, 1);
	TestFalse(TEXT("CONTROL: and gone"), Without.bImpStands);
	TestEqual(TEXT("CONTROL: and the enemy beside it loses nothing"), Without.Lost, 0.0f, 0.001f);
	return true;
}

// ==========================================================================
// Lingering Rot, Mass Grave and Last Breath, after a real detonation
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationAfterTest,
	"Cataclysm.NecromancerDetonation.LingeringRotMassGraveAndLastBreathFollowADetonationAndNothingFollowsWithoutThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A 4 m, 90 degree swing stating `Detonates=1` explodes the caster's Imp A, 2 m ahead, which holds 20 Fervour back. Its
 * caster holds, by hand:
 *   - LINGERING ROT, 4 seconds and 10%: one zone of the caster's is left, dealing a tenth of what the blast sent to the
 *     twin each sweep, for 4 seconds.
 *   - MASS GRAVE, 40% within 3 m: Imp B, 2.5 m aside from A and outside the arc, detonates, and the enemy beside B (5 m
 *     from A) loses what a twin struck at 80% (40% of 200%) loses. Imp C, 2.5 m beyond B and 5 m from A, stands: the ring
 *     is one deep.
 *   - LAST BREATH, twice the reserve: with the pool emptied before the swing, the caster holds 40 after it, A's 20 twice
 *     and B's none (B held nothing back).
 * THE CONTROL: a second caster holding none of them: no zone, B stands, and its pool stays empty.
 */
bool FCataclysmNecromancerDetonationAfterTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDetonationPlayersSide Side(World, FVector(0.0f, -500.0f * Metre, 0.0f));
	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	struct FAfter
	{
		int32 Zones = -1;
		TArray<float> ZoneTicks;
		TArray<float> ZoneLives;
		float ChainSent = -1.0f;
		bool bBStands = true;
		bool bCStands = false;
		float NearBLost = -1.0f;
		float ChainTwinLost = -1.0f;
		float Fervour = -1.0f;
		bool bSetUp = false;
	};
	const auto Detonate = [&](bool bHeld, float AcrossMetres) -> FAfter
	{
		FAfter Out;
		const float Y = AcrossMetres * Metre;
		FDetonationFighter Caster(World, FVector(0.0f, Y, 0.0f));
		SideWithThePlayers(Caster, Side);
		if (bHeld)
		{
			Caster.Lines({{UCataclysmDetonation::RotSecondsStat, Flat(4.0f)},
						  {UCataclysmDetonation::RotPercentStat, Flat(10.0f)},
						  {UCataclysmDetonation::SpreadPercentStat, Flat(40.0f)},
						  {UCataclysmDetonation::SpreadRadiusMetresStat, Flat(3.0f)},
						  {UCataclysmDetonation::FervourPerReserveMultipleStat, Flat(2.0f)}});
		}
		ACataclysmMinion* A = ImpOf(Caster.Actor, FVector(2.0f * Metre, Y, 0.0f));
		ACataclysmMinion* B = ImpOf(Caster.Actor, FVector(2.0f * Metre, Y + 2.5f * Metre, 0.0f));
		ACataclysmMinion* C = ImpOf(Caster.Actor, FVector(2.0f * Metre, Y + 5.0f * Metre, 0.0f));
		Made.Append({A, B, C});
		// THE ENEMY BESIDE B, 2 m from it and 4.5 m from A: inside B's 3 m, outside A's, and outside the swing's arc.
		FDetonationFighter NearB(World, FVector(2.0f * Metre, Y + 4.5f * Metre, 0.0f));
		FDetonationFighter ChainTwin(World, FVector(0.0f, Y + 60.0f * Metre, 0.0f));
		UCataclysmStrikeSkill* Strike = Grant<UCataclysmStrikeSkill>(
			Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=90; Detonates=1"), TEXT("A detonating swing"));
		if (!A || !B || !C || !Strike)
		{
			return Out;
		}
		UCataclysmCommand::HoldReserve(Caster.Actor, A, 20.0f);
		Caster.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
		Out.bSetUp = true;

		const float ChainBefore = ChainTwin.Health();
		Out.ChainSent = TwinStruck(Caster, ChainTwin, Strike, ACataclysmMinion::DetonatedPercentOfWeaponDamage * 0.4f);
		Out.ChainTwinLost = ChainBefore - ChainTwin.Health();

		const float NearBBefore = NearB.Health();
		Strike->SwingOnce();
		Out.NearBLost = NearBBefore - NearB.Health();
		Out.bBStands = IsValid(B) && !B->IsActorBeingDestroyed();
		Out.bCStands = IsValid(C) && !C->IsActorBeingDestroyed();
		Out.Fervour = Caster.Get(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		const TArray<ACataclysmGroundZone*> Zones = ZonesOf(World, Caster.Actor);
		Out.Zones = Zones.Num();
		for (const ACataclysmGroundZone* Zone : Zones)
		{
			Out.ZoneTicks.Add(Zone->DamagePerTick);
			Out.ZoneLives.Add(Zone->GetLifeSpan());
		}
		return Out;
	};

	const FAfter Held = Detonate(true, 0.0f);
	const FAfter None = Detonate(false, 100.0f);
	AddInfo(FString::Printf(TEXT("AFTER A DETONATION: %d zones, the chain's twin sent %.2f, the enemy beside B lost %.2f, "
								 "Fervour %.1f"), Held.Zones, Held.ChainSent, Held.NearBLost, Held.Fervour));
	if (!TestTrue(TEXT("set-up: each scene"), Held.bSetUp && None.bSetUp)
		|| !TestTrue(TEXT("set-up: the chain's twin was struck"), Held.ChainSent > 0.0f && Held.ChainTwinLost > 0.0f))
	{
		return false;
	}
	// LINGERING ROT. A zone for each detonation, A's and B's. B's blast struck the enemy beside it, so its ground deals a
	// tenth of what that blast sent: what the chain's twin was sent at 80%.
	TestEqual(TEXT("LINGERING ROT: a zone for each detonation, A's and B's"), Held.Zones, 2);
	TestTrue(TEXT("LINGERING ROT: B's ground deals a tenth of what its blast sent, each second"),
			 Held.ZoneTicks.ContainsByPredicate([&Held](float Tick)
			 {
				 return FMath::IsNearlyEqual(Tick, Held.ChainSent * 0.1f, Held.ChainSent * 0.001f);
			 }));
	for (const float Life : Held.ZoneLives)
	{
		TestEqual(TEXT("LINGERING ROT: each for 4 seconds"), Life, 4.0f, 0.05f);
	}
	// MASS GRAVE.
	TestFalse(TEXT("MASS GRAVE: B, within 3 m of A and outside the arc, detonates"), Held.bBStands);
	TestEqual(TEXT("MASS GRAVE: the enemy beside B loses what a twin lost at 40% of the detonation"), Held.NearBLost,
			  Held.ChainTwinLost, Held.ChainTwinLost * 0.01f);
	TestTrue(TEXT("MASS GRAVE: C, 2.5 m from B and 5 m from A, stands: the ring is one deep"), Held.bCStands);
	// LAST BREATH.
	TestEqual(TEXT("LAST BREATH: twice A's 20 held back"), Held.Fervour, 40.0f, 0.01f);
	// THE CONTROL.
	TestEqual(TEXT("CONTROL: no ground"), None.Zones, 0);
	TestTrue(TEXT("CONTROL: B stands"), None.bBStands);
	TestEqual(TEXT("CONTROL: the enemy beside B loses nothing"), None.NearBLost, 0.0f, 0.001f);
	TestEqual(TEXT("CONTROL: no Fervour"), None.Fervour, 0.0f, 0.01f);
	return true;
}

// ==========================================================================
// Ripened
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationRipenedTest,
	"Cataclysm.NecromancerDetonation.RipenedExplodesAMinionWhoseTimeEndsAtItsChanceForItsOwnFullFigure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ripened, `Necromancer_basic_ch_b2`: at 100% (ten points), an Imp whose lifespan ends explodes for its own blow times
 * its type's share, the figure `ACataclysmMinion::Explode` deals, read off the Imp; and nothing is noted as a
 * detonation. THE CONTROL: at nought it goes quietly and the enemy beside it loses nothing.
 */
bool FCataclysmNecromancerDetonationRipenedTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDetonationPlayersSide Side(World, FVector(0.0f, -500.0f * Metre, 0.0f));
	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	const auto Expire = [&](float ChancePercent, float AcrossMetres, float& OutOwnFigure, int32& OutDetonations) -> float
	{
		const float Y = AcrossMetres * Metre;
		FDetonationFighter Summoner(World, FVector(0.0f, Y, 0.0f));
		SideWithThePlayers(Summoner, Side);
		if (ChancePercent > 0.0f)
		{
			Summoner.Lines({{UCataclysmDetonation::ExplodesAtLifespanEndChanceStat, Flat(ChancePercent)}});
		}
		FDetonationFighter Near(World, FVector(1.5f * Metre, Y, 0.0f));
		ACataclysmMinion* Imp = ImpOf(Summoner.Actor, FVector(0.0f, Y, 0.0f));
		Made.Add(Imp);
		if (!Imp)
		{
			return -1.0f;
		}
		OutOwnFigure = Imp->OwnDamagePerHit * Imp->ExplosionPercentOfOwnDamage / 100.0f;
		const int32 DetonationsBefore = UCataclysmDetonation::DetonationsMade;
		const float Before = Near.Health();
		Imp->LifeSpanExpired();
		OutDetonations = UCataclysmDetonation::DetonationsMade - DetonationsBefore;
		return Before - Near.Health();
	};

	float OwnFigure = -1.0f;
	int32 Detonations = -1;
	const float Ripe = Expire(100.0f, 0.0f, OwnFigure, Detonations);
	if (!TestTrue(TEXT("set-up: the Imp and its own figure"), Ripe >= 0.0f && OwnFigure > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("RIPENED: the enemy beside it loses the Imp's own blow times its type's share"), Ripe, OwnFigure,
			  OwnFigure * 0.01f);
	TestEqual(TEXT("RIPENED: and it is no detonation"), Detonations, 0);

	float Unused = -1.0f;
	int32 UnusedCount = -1;
	const float Quiet = Expire(0.0f, 100.0f, Unused, UnusedCount);
	TestEqual(TEXT("CONTROL: at nought the enemy beside it loses nothing"), Quiet, 0.0f, 0.001f);
	return true;
}

// ==========================================================================
// Living Bombs
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationLivingBombsTest,
	"Cataclysm.NecromancerDetonation.ALivingBombRunsInAndDetonatesAtContactAndMayRaiseAHuskForEightSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Living Bombs, the Second Rite's option 2, by hand at a husk chance of 100: the Imp's attack reach is cut to contact
 * (`UCataclysmDetonation::ContactAllowanceCm`); an attack on an enemy at its body detonates it, the enemy is struck, and
 * a husk rises where it went off, lasting 8 seconds. THE CONTROLS: an attack on an enemy 2.5 m away strikes it as a blow
 * and leaves the Imp standing; and an Imp whose summoner holds no vow keeps its own reach.
 */
bool FCataclysmNecromancerDetonationLivingBombsTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDetonationPlayersSide Side(World, FVector(0.0f, -500.0f * Metre, 0.0f));
	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	FDetonationFighter Summoner(World, FVector::ZeroVector);
	SideWithThePlayers(Summoner, Side);
	Summoner.Lines({{UCataclysmDetonation::DetonateOnContactStat, Flat(1.0f)},
					{UCataclysmDetonation::RaisesHuskChanceStat, Flat(100.0f)},
					{UCataclysmDetonation::HuskSecondsStat, Flat(8.0f)}});
	ACataclysmMinion* Bomb = ImpOf(Summoner.Actor, FVector(10.0f * Metre, 0.0f, 0.0f));
	ACataclysmMinion* Held = ImpOf(Summoner.Actor, FVector(30.0f * Metre, 0.0f, 0.0f));
	Made.Append({Bomb, Held});
	FDetonationFighter Touching(World, FVector(10.6f * Metre, 0.0f, 0.0f));
	FDetonationFighter Apart(World, FVector(32.5f * Metre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the bomb"), Bomb) || !TestNotNull(TEXT("set-up: the one held off"), Held))
	{
		return false;
	}

	TestEqual(TEXT("LIVING BOMBS: its reach is cut to contact"), Bomb->AttackReachCm(),
			  FMath::Min(Bomb->ReachCm, UCataclysmDetonation::ContactAllowanceCm), 0.001f);

	// CONTROL FIRST: an enemy 2.5 m away is struck as a blow.
	const float ApartBefore = Apart.Health();
	Held->AttackTarget(Apart.Actor);
	TestTrue(TEXT("CONTROL: an Imp 2.5 m from its enemy stands"), IsValid(Held) && !Held->IsActorBeingDestroyed());
	TestTrue(TEXT("CONTROL: and struck it as a blow"), Apart.Health() < ApartBefore);

	const float TouchingBefore = Touching.Health();
	Bomb->AttackTarget(Touching.Actor);
	TestFalse(TEXT("LIVING BOMBS: an Imp at its enemy's body detonates"), IsValid(Bomb) && !Bomb->IsActorBeingDestroyed());
	TestTrue(TEXT("LIVING BOMBS: and the enemy is struck"), Touching.Health() < TouchingBefore);
	const TArray<ACataclysmMinion*> Husks = UCataclysmCommand::MinionsOfTypeCommandedBy(Summoner.Actor, TEXT("Husk"));
	Made.Append(Husks);
	if (TestEqual(TEXT("LIVING BOMBS: a husk rises (needs DT_WeaponSkills' husk row)"), Husks.Num(), 1))
	{
		TestEqual(TEXT("LIVING BOMBS: lasting 8 seconds"), Husks[0]->GetLifeSpan(), 8.0f, 0.05f);
		TestTrue(TEXT("LIVING BOMBS: where it went off"),
				 FVector::Dist2D(Husks[0]->GetActorLocation(), FVector(10.0f * Metre, 0.0f, 0.0f)) < 1.0f * Metre);
	}

	// CONTROL: no vow, its own reach.
	FDetonationFighter Plain(World, FVector(0.0f, 100.0f * Metre, 0.0f));
	SideWithThePlayers(Plain, Side);
	ACataclysmMinion* PlainImp = ImpOf(Plain.Actor, FVector(2.0f * Metre, 100.0f * Metre, 0.0f));
	Made.Add(PlainImp);
	if (TestNotNull(TEXT("set-up: the plain Imp"), PlainImp))
	{
		TestEqual(TEXT("CONTROL: without the vow an Imp keeps its own reach"), PlainImp->AttackReachCm(),
				  PlainImp->ReachCm, 0.001f);
	}
	return true;
}

// ==========================================================================
// Teeming Grave
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationTeemingGraveTest,
	"Cataclysm.NecromancerDetonation.TeemingGraveRaisesOneMoreAtItsChanceAndTheOneMoreRaisesNoOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Teeming Grave's stat, `minion_extra_raise_chance`, shared with Rise Together (`UCataclysmSummonSkill::
 * RaiseOneMoreIfRolled`): at 100, one summon of an Imp reserving 10 raises two, and no more -- the one more does not roll
 * again. THE CONTROL: at nought, one.
 */
bool FCataclysmNecromancerDetonationTeemingGraveTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDetonationPlayersSide Side(World, FVector(0.0f, -500.0f * Metre, 0.0f));
	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	const auto Raise = [&](float ChancePercent, float AcrossMetres, int32& OutExtra) -> int32
	{
		FDetonationFighter Caster(World, FVector(0.0f, AcrossMetres * Metre, 0.0f));
		SideWithThePlayers(Caster, Side);
		if (ChancePercent > 0.0f)
		{
			Caster.Lines({{UCataclysmDetonation::ExtraRaiseChanceStat, Flat(ChancePercent)}});
		}
		UCataclysmSummonSkill* Summon = Grant<UCataclysmSummonSkill>(
			Caster, ECataclysmAbilitySlot::Special, TEXT("Minions=Imp:1; Duration=20; FervourReserve=10"),
			TEXT("A summon"));
		if (!Summon)
		{
			return -1;
		}
		Made.Add(Summon->SummonOne());
		const TArray<ACataclysmMinion*> Imps = UCataclysmCommand::MinionsOfTypeCommandedBy(Caster.Actor, TEXT("Imp"));
		Made.Append(Imps);
		OutExtra = Summon->ExtraMinionsRaised;
		return Imps.Num();
	};

	int32 Extra = -1;
	const int32 Teeming = Raise(100.0f, 0.0f, Extra);
	TestEqual(TEXT("TEEMING GRAVE: at 100%, one summon raises two Imps"), Teeming, 2);
	TestEqual(TEXT("TEEMING GRAVE: one of them the one more"), Extra, 1);
	int32 NoExtra = -1;
	const int32 Plain = Raise(0.0f, 100.0f, NoExtra);
	TestEqual(TEXT("CONTROL: at nought, one"), Plain, 1);
	TestEqual(TEXT("CONTROL: and none more"), NoExtra, 0);
	return true;
}

// ==========================================================================
// The Horde Ascendant
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationHordeAscendantTest,
	"Cataclysm.NecromancerDetonation.TheHordeAscendantHasEveryFollowerWithinTenMetresCastTheSkillAtItsShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Horde Ascendant, as written (`UCataclysmHordeAscendant::EveryFollowerNearCasts`): a player holding the vow at
 * 10 m and a share of S casts a 3 m, all-round strike stating 100% damage; its Imp 5 m away casts it too, and the enemy
 * 1.5 m from that Imp is struck. THE SHARE FROM THE RATIO, NOT A LITERAL: the enemy struck at a share of 10 loses a tenth
 * of what the enemy struck at a share of 100 loses. THE CONTROLS: its Imp 15 m away and its Bolt Turret 5 m away cast
 * nothing (one cast in all), and a player holding no vow has nothing cast.
 */
bool FCataclysmNecromancerDetonationHordeAscendantTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerDetonationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FDetonationPlayersSide Side(World, FVector(0.0f, -500.0f * Metre, 0.0f));
	TArray<ACataclysmMinion*> Made;
	ON_SCOPE_EXIT { Clear(Made); };

	FCataclysmWeaponSkill Skill;
	Skill.Name = TEXT("An all-round strike");
	Skill.Slot = ECataclysmAbilitySlot::Heavy;
	Skill.Shape = ECataclysmSkillShape::Strike;
	Skill.Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Angle=360"));
	Skill.DamagePercent = 100.0f;

	struct FCast
	{
		int32 Casts = -1;
		float NearLost = -1.0f;
		float FarLost = -1.0f;
		bool bSetUp = false;
	};
	const auto CastWith = [&](float ReachMetres, float SharePercent, float AcrossMetres) -> FCast
	{
		FCast Out;
		const float Y = AcrossMetres * Metre;
		FDetonationFighter Player(World, FVector(0.0f, Y, 0.0f));
		SideWithThePlayers(Player, Side);
		if (ReachMetres > 0.0f)
		{
			Player.Lines({{UCataclysmHordeAscendant::ReachMetresStat, Flat(ReachMetres)},
						  {UCataclysmHordeAscendant::DamagePercentStat, Flat(SharePercent)}});
		}
		ACataclysmMinion* NearImp = ImpOf(Player.Actor, FVector(5.0f * Metre, Y, 0.0f));
		ACataclysmMinion* FarImp = ImpOf(Player.Actor, FVector(-15.0f * Metre, Y, 0.0f));
		ACataclysmMinion* Turret = ACataclysmMinion::Spawn(Player.Actor, FVector(0.0f, Y + 5.0f * Metre, 0.0f), 20.0f,
														   false, FString(TEXT("BoltTurret")));
		Made.Append({NearImp, FarImp, Turret});
		FDetonationFighter NearEnemy(World, FVector(6.5f * Metre, Y, 0.0f));
		FDetonationFighter FarEnemy(World, FVector(-16.5f * Metre, Y, 0.0f));
		if (!NearImp || !FarImp || !Turret)
		{
			return Out;
		}
		Out.bSetUp = true;
		const float NearBefore = NearEnemy.Health();
		const float FarBefore = FarEnemy.Health();
		Out.Casts = UCataclysmHordeAscendant::EveryFollowerNearCasts(Player.Actor, Skill, NearEnemy.Actor->GetActorLocation());
		Out.NearLost = NearBefore - NearEnemy.Health();
		Out.FarLost = FarBefore - FarEnemy.Health();
		return Out;
	};

	const FCast Tenth = CastWith(10.0f, 10.0f, 0.0f);
	const FCast Whole = CastWith(10.0f, 100.0f, 100.0f);
	const FCast NoVow = CastWith(0.0f, 0.0f, 200.0f);
	if (!TestTrue(TEXT("set-up: each scene"), Tenth.bSetUp && Whole.bSetUp && NoVow.bSetUp))
	{
		return false;
	}
	TestEqual(TEXT("THE HORDE ASCENDANT: one cast, the Imp within 10 m; the Imp 15 m away and the turret cast nothing"),
			  Tenth.Casts, 1);
	TestTrue(TEXT("THE HORDE ASCENDANT: the enemy beside the near Imp is struck"), Tenth.NearLost > 0.0f);
	TestEqual(TEXT("CONTROL: the enemy beside the far Imp is not"), Tenth.FarLost, 0.0f, 0.001f);
	TestEqual(TEXT("THE SHARE: a share of 10 deals a tenth of what a share of 100 deals"), Tenth.NearLost,
			  Whole.NearLost * 0.1f, Whole.NearLost * 0.002f);
	TestEqual(TEXT("CONTROL: a player holding no vow has nothing cast"), NoVow.Casts, 0);
	TestEqual(TEXT("CONTROL: and its enemy loses nothing"), NoVow.NearLost, 0.0f, 0.001f);
	return true;
}

// ==========================================================================
// The real rows
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNecromancerDetonationRowsTest,
	"Cataclysm.NecromancerDetonation.TheTwelveNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The rows of the ten Charnel nodes and the two vows, read from `DT_PassiveEffects` (the asset step rebuilds it): each
 * stat a row names, at its value, scale, step, cap, reach, first point and event.
 */
bool FCataclysmNecromancerDetonationRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Node;
		int32 Option;
		const TCHAR* Stat;
		const TCHAR* Kind;
		float Value;
		int32 Rows;
		const TCHAR* Scale;
		int32 Cap;
		float Reach;
		const TCHAR* Event;
	};
	const FExpected Expected[] = {
		{TEXT("Necromancer_basic_ch_stem0"), 0, TEXT("weapon_skills_detonate_own_minions"), TEXT("flat"), 1.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_stem1"), 0, TEXT("minion_detonates_when_released"), TEXT("flat"), 1.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_stem1"), 0, TEXT("minion_detonation_area"), TEXT("increased"), 2.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_stem1"), 0, TEXT("minion_detonation_damage"), TEXT("increased"), 2.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_stem2"), 0, TEXT("minion_extra_raise_chance"), TEXT("flat"), 8.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_a0"), 0, TEXT("minion_detonation_damage"), TEXT("increased"), 1.0f, 1, TEXT("minions_near_the_minion"), 5, 3.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_a1"), 0, TEXT("minion_detonation_rot_seconds"), TEXT("flat"), 1.0f, 4, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_a1"), 0, TEXT("minion_detonation_rot_percent"), TEXT("flat"), 10.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_a2"), 0, TEXT("minion_detonation_damage"), TEXT("increased"), 1.0f, 1, TEXT("own_stacks"), 5, -1.0f, TEXT("minion_detonated")},
		{TEXT("Necromancer_basic_ch_b1"), 0, TEXT("minion_detonation_damage"), TEXT("increased"), 1.0f, 1, TEXT("minion_seconds_active"), 5, -1.0f, TEXT("")},
		{TEXT("Necromancer_basic_ch_b2"), 0, TEXT("minion_explodes_at_lifespan_end_chance"), TEXT("flat"), 10.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_keystone_ch_kA"), 0, TEXT("minion_detonation_spreads_percent"), TEXT("flat"), 40.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_keystone_ch_kA"), 0, TEXT("minion_detonation_spread_radius_metres"), TEXT("flat"), 3.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_keystone_ch_kC"), 0, TEXT("fervour_on_minion_detonated_reserve_multiple"), TEXT("flat"), 2.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_capstone_50"), 2, TEXT("minions_detonate_on_contact"), TEXT("flat"), 1.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_capstone_50"), 2, TEXT("minion_detonation_raises_husk_chance"), TEXT("flat"), 50.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_capstone_50"), 2, TEXT("minion_detonation_husk_seconds"), TEXT("flat"), 8.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_capstone_200"), 3, TEXT("minions_cast_your_skills_within_metres"), TEXT("flat"), 10.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
		{TEXT("Necromancer_capstone_200"), 3, TEXT("minion_skill_copy_damage_percent"), TEXT("flat"), 10.0f, 1, TEXT(""), 0, -1.0f, TEXT("")},
	};
	for (const FExpected& Want : Expected)
	{
		int32 Found = 0;
		const FString Label = FString::Printf(TEXT("%s option %d %s"), Want.Node, Want.Option, Want.Stat);
		for (const FCataclysmPassiveEffectRow* Effect : UCataclysmPassiveTree::EffectsFor(Effects, FName(Want.Node)))
		{
			if (Effect->Option != Want.Option || Effect->Stat != Want.Stat)
			{
				continue;
			}
			++Found;
			TestEqual(*(Label + TEXT(": its kind")), Effect->ValueKind.ToLower(), FString(Want.Kind));
			TestEqual(*(Label + TEXT(": its value")), Effect->ValuePerPoint, Want.Value, 0.001f);
			TestEqual(*(Label + TEXT(": its scale")), Effect->Scale, FString(Want.Scale));
			TestEqual(*(Label + TEXT(": its cap")), Effect->ScaleMaxSteps, Want.Cap);
			TestEqual(*(Label + TEXT(": its reach")), Effect->ReachMetres, Want.Reach, 0.001f);
			TestEqual(*(Label + TEXT(": its event")), Effect->ActionEvent, FString(Want.Event));
		}
		TestEqual(*(Label + TEXT(": its rows")), Found, Want.Rows);
	}

	// LINGERING ROT'S SECONDS, ONE A STEP: from 2, 4, 6 and 8 points.
	TArray<int32> From;
	for (const FCataclysmPassiveEffectRow* Effect :
		 UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("Necromancer_basic_ch_a1"))))
	{
		if (Effect->Stat == TEXT("minion_detonation_rot_seconds"))
		{
			From.Add(Effect->MinPoints);
		}
	}
	From.Sort();
	TestTrue(TEXT("Lingering Rot's seconds come at 2, 4, 6 and 8 points"), From == TArray<int32>({2, 4, 6, 8}));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
