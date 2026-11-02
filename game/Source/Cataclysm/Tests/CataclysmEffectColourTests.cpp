// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmGroundEffect.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmProjectileEffect.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayAbilitySpec.h"
#include "GameplayTagContainer.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A PLAYER'S SKILL HAS A DAMAGE TYPE AND ITS EFFECTS SHOULD BE DRAWN IN IT.
 *
 * Issue #803. Every player skill names a damage type in
 * `game/Data/WeaponSkills.csv` -- 51 of the 58 shaped rows are Demonic and 7 are
 * War -- and nothing read that column for a visual effect. So every bolt and
 * every burst a player produced drew in the authored default, which is white,
 * and a Demonic skill and a War skill looked identical.
 *
 * THE REASON IT WAS HARD IS THAT ONE QUESTION WAS ANSWERING TWO. A player's
 * damage reaches the defender untyped on purpose: an enemy holds one generic
 * resistance and has nothing to choose between. That ruling is about
 * resistances and says nothing about colour, but the same function was giving
 * both answers.
 *
 * WHAT THESE TESTS PIN. The two answers are now separate and both are still
 * right: a player's hit draws in the skill's colour AND arrives untyped, and an
 * enemy's hit is unchanged in both respects.
 */
namespace CataclysmEffectColourTest
{
	/** Every part of a fighter these tests need, on a plain actor. */
	struct FScopedFighter
	{
		FScopedFighter(UWorld* World, float AttackDamage)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem =
				NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);

			Vitals = NewVitals;
			Combat = NewCombat;
			Resistances = NewResist;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Vitals->SetMaxHealth(1'000'000.0f);
			Vitals->SetHealth(1'000'000.0f);
			LastHealth = 1'000'000.0f;

			Combat->SetAttackDamage(AttackDamage);
		}

		~FScopedFighter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Health lost since this was last read. */
		float TakeDamageReading()
		{
			const float Now = Vitals->GetHealth();
			const float Lost = LastHealth - Now;
			LastHealth = Now;
			return Lost;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
		TObjectPtr<UCataclysmResistanceAttributeSet> Resistances = nullptr;
		float LastHealth = 0.0f;
	};

	/**
	 * Critical strikes pinned off for the duration.
	 *
	 * WITHOUT THIS EVERY DAMAGE ASSERTION BELOW READS HALF AS MUCH AGAIN, AT
	 * RANDOM. The same guard exists in CataclysmDamageTypeTests.cpp for the same
	 * reason.
	 */
	struct FScopedNoCriticalStrikes
	{
		FScopedNoCriticalStrikes()
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Cataclysm.CritRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(100.0f, ECVF_SetByConsole);
			}
		}

		~FScopedNoCriticalStrikes()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** The tag container a skill row of that damage type produces. */
	static FGameplayTagContainer TagsForElement(const TCHAR* Element)
	{
		FGameplayTagContainer Tags;
		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(Element), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid())
		{
			Tags.AddTag(Tag);
		}
		return Tags;
	}

	/** The health every creature the ground tests make starts with: no sweep comes near it. */
	constexpr float GroundColourCreatureHealth = 1'000'000.0f;

	/**
	 * A creature of the monsters' side with no armour, evasion, block or resistance, standing at `Where`, as
	 * `Cataclysm.NoSkillDotTags.` makes one. A CHARACTER AND NOT A BARE ACTOR because a zone finds who is standing
	 * in it by a pawn overlap, which a bare actor has nothing to answer with.
	 *
	 * WITH `DemonicResistance` above nought, it also holds the player's eight-figure resistance set with that
	 * figure in its Demonic slot. No creature holds that set in play; it is here because it is the only defender a
	 * typed blow and an untyped one reach differently, and that difference is what the ground test reads.
	 */
	static ACataclysmEnemyCharacter* GroundColourCreature(UWorld* World, const FVector& Where,
														  float DemonicResistance)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(GroundColourCreatureHealth);
		Made->SetArmour(0.0f);
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), GroundColourCreatureHealth);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), GroundColourCreatureHealth);
		if (DemonicResistance > 0.0f)
		{
			Its->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Made));
			Its->SetNumericAttributeBase(UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(),
										 DemonicResistance);
		}
		return Made;
	}

	/** A creature's health now, or a negative figure when it has no ability system. */
	static float GroundColourHealthOf(ACataclysmEnemyCharacter* Creature)
	{
		const UAbilitySystemComponent* Its = Creature ? Creature->GetAbilitySystemComponent() : nullptr;
		return Its ? Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}
}

// --------------------------------------------------------------------------
// The bolt in flight
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerBoltTakesTheSkillsColour,
	"Cataclysm.Effects.APlayersBoltIsColouredByTheSkillThatFiredIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerBoltTakesTheSkillsColour::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// A PLAYER STANDS IN AS A PLAIN ACTOR, which is exactly what
	// UCataclysmSkillEffects::DamageTypeOf sees: anything that is not an
	// ACataclysmEnemyCharacter is untyped.
	FScopedFighter Player(World, 100.0f);

	ACataclysmProjectile* Bolt = ACataclysmProjectile::Fire(
		Player.Actor, FVector::ZeroVector, FVector(1'000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/40.0f, /*InSpeed=*/1'500.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		TagsForElement(TEXT("Element.Demonic")), /*bInBurns=*/false);

	if (!TestNotNull(TEXT("a bolt in the air"), Bolt))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Bolt)) { Bolt->Destroy(); } };

	// THE WHOLE OF ISSUE #803 IN ONE ASSERTION. Before this the answer was None
	// and NS_Proj_Body kept its authored white.
	TestEqual(TEXT("a player's bolt is drawn as its skill's damage type"),
		UCataclysmProjectileEffect::DamageTypeFor(Bolt), FName(TEXT("Demonic")));

	// AND A SKILL OF A DIFFERENT TYPE IS A DIFFERENT COLOUR, which is the half
	// that would still fail if the answer were hard-coded. 7 of the 58 shaped
	// rows are War.
	ACataclysmProjectile* WarBolt = ACataclysmProjectile::Fire(
		Player.Actor, FVector::ZeroVector, FVector(1'000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/40.0f, /*InSpeed=*/1'500.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		TagsForElement(TEXT("Element.War")), /*bInBurns=*/false);
	if (TestNotNull(TEXT("a second bolt"), WarBolt))
	{
		TestEqual(TEXT("a War skill's bolt is drawn as War"),
			UCataclysmProjectileEffect::DamageTypeFor(WarBolt),
			FName(TEXT("War")));
		WarBolt->Destroy();
	}

	// A SKILL THAT NAMES NO TYPE STILL DRAWS THE AUTHORED DEFAULT rather than
	// guessing one. Nothing in the shipped data does this, and the answer still
	// has to be defined.
	ACataclysmProjectile* Plain = ACataclysmProjectile::Fire(
		Player.Actor, FVector::ZeroVector, FVector(1'000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/40.0f, /*InSpeed=*/1'500.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		FGameplayTagContainer(), /*bInBurns=*/false);
	if (TestNotNull(TEXT("an untagged bolt"), Plain))
	{
		TestEqual(TEXT("a skill with no damage type names none"),
			UCataclysmProjectileEffect::DamageTypeFor(Plain), FName(NAME_None));
		Plain->Destroy();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyBoltStillTakesTheFirersColour,
	"Cataclysm.Effects.AnEnemysBoltIsStillColouredByTheEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyBoltStillTakesTheFirersColour::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Enemy)) { Enemy->Destroy(); } };

	Enemy->DamageType = FName(TEXT("Void"));

	// THE FIRER WINS OVER THE SKILL, and it has to. An enemy's damage type is
	// what its hits are resisted as, so a bolt drawn in a different colour would
	// mislead in the one case where the colour tells the player something they
	// can act on. The skill tag below deliberately disagrees.
	ACataclysmProjectile* Bolt = ACataclysmProjectile::Fire(
		Enemy, FVector::ZeroVector, FVector(1'000.0f, 0.0f, 0.0f),
		/*InRadiusCm=*/40.0f, /*InSpeed=*/1'500.0f,
		/*InPierce=*/0, /*bInReturns=*/false, /*InDamagePercent=*/100.0f,
		TagsForElement(TEXT("Element.Demonic")), /*bInBurns=*/false);

	if (TestNotNull(TEXT("a bolt in the air"), Bolt))
	{
		TestEqual(TEXT("an enemy's bolt is drawn as the enemy's own type"),
			UCataclysmProjectileEffect::DamageTypeFor(Bolt),
			FName(TEXT("Void")));
		Bolt->Destroy();
	}

	return true;
}

// --------------------------------------------------------------------------
// The burst where the blow lands
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerBurstTakesTheSkillsColour,
	"Cataclysm.Effects.APlayersHitBurstIsColouredByTheSkillThatLandedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerBurstTakesTheSkillsColour::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	const FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Player(World, 1'000.0f);
	FScopedFighter Struck(World, 0.0f);

	// THE DEFENDER RESISTS DEMONIC HITS. It is standing in for a player-shaped
	// defender on purpose, because that is the only shape whose resistance
	// depends on the hit's type at all -- an enemy's generic resistance meets
	// everything and would hide the difference this test is about.
	Struck.Resistances->SetDemonicResistance(40.0f);
	Struck.LastHealth = Struck.Vitals->GetHealth();

	const int32 AskedBefore = UCataclysmImpactEffect::TimesAsked;
	UCataclysmImpactEffect::LastDamageTypeAsked = NAME_None;

	// EVERY STEP THE REAL GAME TAKES. ApplyHit builds the effect, puts the tags
	// on it, applies it, and PostGameplayEffectExecute reads them back and asks
	// for the burst.
	UCataclysmSkillEffects::ApplyHit(Player.Actor, Struck.Actor, 100.0f,
									 TagsForElement(TEXT("Element.Demonic")));

	TestEqual(TEXT("the landed blow asked for a burst"),
		UCataclysmImpactEffect::TimesAsked, AskedBefore + 1);

	// THE FIRST HALF OF ISSUE #803. Before this the answer was None and
	// NS_Impact_Point kept its authored white spark and black core.
	TestEqual(TEXT("and asked for it in the skill's own damage type"),
		UCataclysmImpactEffect::LastDamageTypeAsked, FName(TEXT("Demonic")));

	// THE SECOND HALF, AND THE ONE THAT MUST NOT HAVE BROKEN. The damage still
	// arrives untyped, so the defender's 40 percent Demonic resistance does not
	// meet it and the full 1,000 lands. If the colour tag were being read as a
	// damage type this would read 600.
	TestEqual(TEXT("and the hit still arrived untyped, so nothing resisted it"),
		Struck.TakeDamageReading(), 1'000.0f, 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyHitIsStillResistedByItsType,
	"Cataclysm.Effects.AnEnemysHitIsStillResistedByItsOwnDamageType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyHitIsStillResistedByItsType::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	const FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Enemy)) { Enemy->Destroy(); } };

	Enemy->SetAttackDamage(1'000.0f);
	Enemy->DamageType = FName(TEXT("Demonic"));

	FScopedFighter Struck(World, 0.0f);
	Struck.Resistances->SetDemonicResistance(40.0f);
	Struck.LastHealth = Struck.Vitals->GetHealth();

	UCataclysmImpactEffect::LastDamageTypeAsked = NAME_None;

	UCataclysmSkillEffects::ApplyHit(Enemy, Struck.Actor, 100.0f);

	// THE RULE THAT PAYS FOR EVERYTHING ELSE. An enemy's hit is typed, so the
	// player's resistance to that type applies. Issue #486 was this not
	// happening, and the marker added for issue #803 must not undo it.
	TestEqual(TEXT("the 40 percent Demonic resistance still applies"),
		Struck.TakeDamageReading(), 600.0f, 1.0f);

	TestEqual(TEXT("and the burst is drawn as the enemy's type, as before"),
		UCataclysmImpactEffect::LastDamageTypeAsked, FName(TEXT("Demonic")));

	return true;
}

// --------------------------------------------------------------------------
// The ground a skill leaves. Issue #818.
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayersSkillGroundTakesTheSkillsColour,
	"Cataclysm.Effects.APlayersSkillGroundIsDrawnInTheSkillsTypeAndItsDamageStaysUntyped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayersSkillGroundTakesTheSkillsColour::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	// A DEMONIC STRIKE WHOSE ROW LEAVES GROUND, cast by a bare actor at the origin standing in for a player, as
	// `Cataclysm.NoSkillDotTags.AGroundZonesOwnAilmentCarriesTheZonesSkillTagsAndNoNameAndABareZonesCarriesNone` casts
	// one: an ability system, the combat and vital sets, a Demonic weapon slot and an attack damage of 100. A bare
	// actor is untyped to `UCataclysmSkillEffects::DamageTypeOf`, exactly as a player is. ITS GROUND IS 3 M ACROSS THE
	// ORIGIN, and two creatures stand in it 1.2 m either side of the caster along Y, outside the strike's 120 degree
	// arc ahead: one resisting nothing, one holding 40 Demonic resistance in the player's eight-figure set.
	const FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	for (const TCHAR* Name : {TEXT("Element.Demonic"), TEXT("Item.Weapon.Greataxe"), TEXT("Type.Strike"),
							  TEXT("Type.Melee")})
	{
		if (!TestTrue(*FString::Printf(TEXT("set-up: the tag %s exists"), Name),
				UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false).IsValid()))
		{
			return false;
		}
	}

	AActor* Caster = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: a caster"), Caster))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* Casting = NewObject<UCataclysmAbilitySystemComponent>(Caster);
	Casting->RegisterComponent();
	Casting->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Caster));
	Casting->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Caster));
	Casting->InitAbilityActorInfo(Caster, Caster);
	UCataclysmWeaponSlotsComponent* Slots = NewObject<UCataclysmWeaponSlotsComponent>(Caster);
	Slots->RegisterComponent();
	Slots->SetDamageType(TEXT("Demonic"));
	Casting->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	ACataclysmEnemyCharacter* Plain = GroundColourCreature(World, FVector(0.0f, 120.0f, 0.0f), 0.0f);
	ACataclysmEnemyCharacter* Resisting = GroundColourCreature(World, FVector(0.0f, -120.0f, 0.0f), 40.0f);
	if (!TestNotNull(TEXT("set-up: a creature resisting nothing"), Plain)
		|| !TestNotNull(TEXT("set-up: a creature resisting Demonic"), Resisting)
		|| !TestTrue(TEXT("set-up: the caster is untyped, as a player is"),
			UCataclysmSkillEffects::DamageTypeOf(Caster).IsNone()))
	{
		return false;
	}

	const FGameplayAbilitySpecHandle Handle =
		Casting->GiveAbilityInSlot(UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::BasicAttack,
								   /*Level=*/100, Caster);
	FGameplayAbilitySpec* Spec = Casting->FindAbilitySpecFromHandle(Handle);
	UCataclysmStrikeSkill* Strike = Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a strike that leaves ground"), Strike))
	{
		return false;
	}
	Strike->SkillName = TEXT("Molten Trail");
	Strike->SkillTags = FGameplayTagContainer();
	for (const TCHAR* Name : {TEXT("Item.Weapon.Greataxe"), TEXT("Element.Demonic"), TEXT("Type.Strike"),
							  TEXT("Type.Melee")})
	{
		Strike->SkillTags.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(FName(Name)));
	}
	Strike->Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Radius=2.4; Angle=120; MaxTargets=1; GroundRadius=3; GroundDuration=4; GroundPercent=25"));

	// SET BACK BEFORE THE ACT, as the declaration says: otherwise this reads whatever drew before it.
	UCataclysmGroundEffect::LastDamageTypeAsked = NAME_None;
	if (!TestTrue(TEXT("set-up: the strike started"),
			Casting->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false)))
	{
		return false;
	}
	TArray<ACataclysmGroundZone*> Zones;
	for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
	{
		Zones.Add(*It);
	}
	if (!TestEqual(TEXT("set-up: the strike left one zone"), Zones.Num(), 1)
		|| !TestTrue(TEXT("set-up: the zone is the strike's own ground"), Zones[0]->SkillThatLeftIt.Get() == Strike))
	{
		return false;
	}
	ACataclysmGroundZone* Zone = Zones[0];

	// THE WHOLE OF ISSUE #818 IN ONE ASSERTION. Before it the ground asked for None, from its owner, and
	// `NS_Impact_Ground` kept its authored white. Asked inside `BeginPlay`, so this is the first drawing's colour.
	TestEqual(TEXT("the skill's ground was asked to be drawn in the skill's own damage type"),
		UCataclysmGroundEffect::LastDamageTypeAsked, FName(TEXT("Demonic")));
	TestEqual(TEXT("the ground holds the skill's type as its own colour"), Zone->DrawnAsType, FName(TEXT("Demonic")));
	TestEqual(TEXT("and every later drawing is in it too"), Zone->TypeItIsDrawnAs(), FName(TEXT("Demonic")));

	// THE HALF THAT MUST NOT HAVE MOVED: ONLY AN ENEMY'S DAMAGE IS TYPED (ruled 2026-08-12). The colour is held apart
	// from the damage type, which stays unset for a skill's own ground.
	TestTrue(TEXT("the ground's damage type stays unset"), Zone->DamageType.IsNone());

	// AND THE SWEEP SAYS SO WHERE IT COUNTS. An untyped blow meets no slot of the eight-figure set, so the creature
	// resisting Demonic takes exactly what the one resisting nothing takes. Typed Demonic, it would take 60 percent.
	const float PlainBefore = GroundColourHealthOf(Plain);
	const float ResistingBefore = GroundColourHealthOf(Resisting);
	Zone->Sweep();
	const float PlainTook = PlainBefore - GroundColourHealthOf(Plain);
	const float ResistingTook = ResistingBefore - GroundColourHealthOf(Resisting);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the sweep burned the creature resisting nothing (took %.3f)"),
			PlainTook), PlainTook > 1.0f))
	{
		return false;
	}
	TestEqual(TEXT("and the creature resisting Demonic took as much: the ground's damage arrived untyped"),
		ResistingTook, PlainTook, PlainTook * 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGroundColourOwnerThenSkill,
	"Cataclysm.Effects.AGroundIsDrawnInItsOwnersTypeFirstAndThenItsSkills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGroundColourOwnerThenSkill::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	// `UCataclysmGroundEffect::DamageTypeFor`, the answer a skill's own ground is drawn in: the owner first, then the
	// skill, the shape the bolt, the swing arc and the cast burst already use. A bare actor stands in for a player.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FScopedFighter Player(World, 100.0f);
	const FGameplayTag Demonic = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Element.Demonic")), /*ErrorIfNotFound=*/false);
	const FGameplayTag War = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Element.War")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: Element.Demonic exists"), Demonic.IsValid())
		|| !TestTrue(TEXT("set-up: Element.War exists"), War.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("a player's Demonic skill's ground is drawn as Demonic"),
		UCataclysmGroundEffect::DamageTypeFor(Player.Actor, Demonic), FName(TEXT("Demonic")));
	// AND A SKILL OF A DIFFERENT TYPE IS A DIFFERENT COLOUR, the half that would still fail if the answer were fixed.
	TestEqual(TEXT("a player's War skill's ground is drawn as War"),
		UCataclysmGroundEffect::DamageTypeFor(Player.Actor, War), FName(TEXT("War")));
	TestEqual(TEXT("a skill naming no damage type names none"),
		UCataclysmGroundEffect::DamageTypeFor(Player.Actor, FGameplayTag()), FName(NAME_None));

	// THE OWNER WINS OVER THE SKILL. A creature casting a player's skill -- the Corrupted Stalker holding a player's
	// build -- has its ground drawn in its own type, as it was before issue #818. The skill tag deliberately disagrees.
	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(0.0f, 2'000.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: an enemy"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Enemy)) { Enemy->Destroy(); } };
	Enemy->DamageType = FName(TEXT("Void"));
	TestEqual(TEXT("an enemy's ground is drawn as the enemy's own type, whatever the skill"),
		UCataclysmGroundEffect::DamageTypeFor(Enemy, Demonic), FName(TEXT("Void")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemysGroundKeepsTheEnemysColour,
	"Cataclysm.Effects.AnEnemysGroundIsStillDrawnInTheEnemysType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemysGroundKeepsTheEnemysColour::RunTest(const FString&)
{
	using namespace CataclysmEffectColourTest;

	// THE CONTROL. A creature's burning ground -- the Gatekeeper's Soulfall and the Hellhound's lane are made this way,
	// by `ACataclysmGroundZone::Spawn` with no colour and no damage type -- is drawn in the creature's own type, as it
	// was before issue #818, and is given no colour of its own.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: an enemy"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Enemy)) { Enemy->Destroy(); } };
	Enemy->DamageType = FName(TEXT("Void"));

	UCataclysmGroundEffect::LastDamageTypeAsked = NAME_None;
	ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
		Enemy, FVector(0.0f, 2'000.0f, 0.0f), /*RadiusCm=*/150.0f, /*Duration=*/4.0f, /*DamagePerTick=*/10.0f);
	if (!TestNotNull(TEXT("set-up: the enemy's ground"), Zone))
	{
		return false;
	}

	TestEqual(TEXT("an enemy's ground is asked to be drawn in the enemy's own type"),
		UCataclysmGroundEffect::LastDamageTypeAsked, FName(TEXT("Void")));
	TestTrue(TEXT("and is given no colour of its own"), Zone->DrawnAsType.IsNone());
	TestEqual(TEXT("so it is drawn in its owner's type"), Zone->TypeItIsDrawnAs(), FName(TEXT("Void")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
