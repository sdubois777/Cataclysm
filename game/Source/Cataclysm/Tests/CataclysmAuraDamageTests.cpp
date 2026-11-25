// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"

/**
 * What an aura's own row says it deals, read off the generated rows themselves. Issues #2869 and #2875, both ruled by
 * the owner on 2026-11-24.
 *
 *   #2869, "4 m, 10% a second (Recommended)": Conflagration's ring is 4 metres and its pulse is 10% of weapon damage
 *   a second, with its burn, its 15 Shred and its allies' 8% unchanged.
 *
 *   #2875, "No damage unless stated (Recommended)": an aura deals damage only if its description says so. Exposing
 *   Light, Miasma, Pall of Ending, Unreason and Gnawing Want are pure effects, and so is Undertow, whose sentence names
 *   no damage either. `UCataclysmSkillTemplate::GetDamagePercent` answers nought for an aura stating no figure.
 *
 *   Living Pyre, #2875, "400% spread over 6 pulses (Recommended)": its row states 66.67% a pulse.
 *
 * WHY THE GENERATED ROWS AND NOT A ROW TYPED INTO THE TEST. Every older aura test grants the template with parameters
 * written in the test, so it proves the machinery and says nothing about what the sheet supplies; a radius of 10 m
 * typed into a test still passes after the row says 4. These read `DT_WeaponSkills`, so THE WINDOW MUST REBUILD THAT
 * ASSET FIRST: before it is rebuilt the asset holds the old row, and the Conflagration and Living Pyre tests fail on
 * their set-up lines, which name the figure they found.
 *
 * EVERY FIGHTER IS A PLAIN ACTOR with a weapon dealing 100, no armour and no resistance, so a pulse's damage is its
 * percentage of 100 exactly. The critical strike roll is pinned off. A pulse is `Pulse` called by the test; nothing
 * waits, so no burn or poison has ticked when health is read.
 */
namespace CataclysmAuraDamageTest
{
	constexpr float AuraDamageMetre = 100.0f;

	/** What every fighter's weapon deals, so a pulse's damage is its percentage exactly. */
	constexpr float AuraDamageWeapon = 100.0f;

	/**
	 * An actor a sphere overlap can find, holding the sets a skill reads and pays from: the shape of `FEntryFighter`
	 * in `CataclysmStalkerEntryTests.cpp`, under its own name so the two files can share a unity build. It belongs to
	 * no team, which reads as hostile to everyone; an actor it owns is its ally.
	 */
	struct FAuraDamageFighter
	{
		FAuraDamageFighter(UWorld* World, const FVector& Where)
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
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 10000.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), AuraDamageWeapon);
		}

		~FAuraDamageFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		bool Carries(const FGameplayTag& Tag) const
		{
			return Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** One generated row's skill, by its weapon type, damage type and designed slot, or an empty one. */
	FCataclysmWeaponSkill AuraDamageRow(const TCHAR* WeaponType, const TCHAR* DamageType, ECataclysmAbilitySlot In)
	{
		for (const FCataclysmWeaponSkill& Skill :
			 UCataclysmWeaponSkills::SkillsFor(UCataclysmWeaponSkills::LoadGeneratedTable(), WeaponType, DamageType))
		{
			if (UCataclysmWeaponSkills::DesignedSlotOf(Skill) == In)
			{
				return Skill;
			}
		}
		return FCataclysmWeaponSkill();
	}

	/**
	 * The generated row granted to `Caster` in its designed slot at level 100, stamped as a weapon's grant stamps it,
	 * and activated. Null when the row is not an aura, the grant is missing, or the activation is refused.
	 */
	UCataclysmAuraSkill* AuraDamageActivatedFromTheRow(const FAuraDamageFighter& Caster,
														const FCataclysmWeaponSkill& Row)
	{
		if (Row.Shape != ECataclysmSkillShape::Aura)
		{
			return nullptr;
		}
		const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
			UCataclysmAuraSkill::StaticClass(), UCataclysmWeaponSkills::DesignedSlotOf(Row), /*Level=*/100,
			Caster.Actor);
		FGameplayAbilitySpec* Spec =
			Handle.IsValid() ? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmAuraSkill* Aura = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Aura)
		{
			return nullptr;
		}
		UCataclysmWeaponSkills::StampOnto(*Aura, Row);
		return Caster.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false) ? Aura : nullptr;
	}

	/** The gameplay tag an ailment named in a skill row grants, or an invalid tag. */
	FGameplayTag AuraDamageAilmentTag(const TCHAR* Written)
	{
		const FCataclysmAilmentKind* Kind = UCataclysmAilments::KindNamedInASkillRow(Written);
		return Kind ? UGameplayTagsManager::Get().RequestGameplayTag(FName(Kind->TagName), /*ErrorIfNotFound=*/false)
					: FGameplayTag();
	}
}

/**
 * CONFLAGRATION'S OWN ROW, #2869: a 4 m ring and 10% of weapon damage a second. A caster at the origin, one enemy 3 m
 * along +X and one 5 m along -X, whose body's near edge is 4.66 m out. One pulse finds the near one alone, takes 10
 * from it, sets it alight and lays the row's Shred; the far one loses nothing and carries neither. A second pulse takes
 * another 10. THE CONTROLS are the far enemy, and the set-up lines reading the row's radius and figure off the table.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraDamageConflagrationRowTest,
	"Cataclysm.AuraDamage.ConflagrationsRowHitsWithinFourMetresForTenPercentOfWeaponDamageAndNotAtFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAuraDamageConflagrationRowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAuraDamageTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FCataclysmWeaponSkill Row = AuraDamageRow(TEXT("Fist"), TEXT("Demonic"), ECataclysmAbilitySlot::Aura);
	const FGameplayTag Shred = UCataclysmSkillShapes::StatusTagFor(TEXT("Shred"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestEqual(TEXT("set-up: the Demonic aura row is Conflagration"), Row.Name, FString(TEXT("Conflagration")))
		|| !TestEqual(TEXT("set-up: its row states a ring of 4 m. If 10, DT_WeaponSkills is older than the CSV"),
					  Row.Params.RadiusCm, 4.0f * AuraDamageMetre, 0.01f)
		|| !TestEqual(TEXT("set-up: and 10% of weapon damage. If -1, DT_WeaponSkills is older than the CSV"),
					  Row.DamagePercent, 10.0f, 0.001f)
		|| !TestTrue(TEXT("set-up: the Shred and burn tags"), Shred.IsValid() && Burn.IsValid()))
	{
		return false;
	}

	FAuraDamageFighter Caster(World, FVector::ZeroVector);
	FAuraDamageFighter Near(World, FVector(3.0f * AuraDamageMetre, 0.0f, 0.0f));
	FAuraDamageFighter Far(World, FVector(-5.0f * AuraDamageMetre, 0.0f, 0.0f));
	UCataclysmAuraSkill* Aura = AuraDamageActivatedFromTheRow(Caster, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted and switched on"), Aura))
	{
		return false;
	}
	TestEqual(TEXT("the switched-on aura deals the row's 10% of weapon damage"), Aura->GetDamagePercent(), 10.0f,
			  0.001f);

	const float NearBefore = Near.Health();
	const float FarBefore = Far.Health();
	TestEqual(TEXT("one pulse finds the one enemy within 4 m"), Aura->Pulse(), 1);
	TestEqual(TEXT("and takes 10 from it: 10% of a weapon dealing 100, for the pulse's one second"),
			  NearBefore - Near.Health(), 10.0f, 0.01f);
	TestTrue(TEXT("it sets that enemy alight"), Near.Carries(Burn));
	TestTrue(TEXT("and lays the row's Shred on it"), Near.Carries(Shred));

	TestEqual(TEXT("CONTROL: the enemy 5 m away loses nothing"), Far.Health(), FarBefore, 0.0f);
	TestFalse(TEXT("CONTROL: is not set alight"), Far.Carries(Burn));
	TestFalse(TEXT("CONTROL: and carries no Shred"), Far.Carries(Shred));

	// THE FIGURE IS PER PULSE. The Shred the first pulse laid cuts nothing, because the enemy has no resistance.
	const float BeforeTheSecond = Near.Health();
	Aura->Pulse();
	TestEqual(TEXT("a second pulse takes another 10"), BeforeTheSecond - Near.Health(), 10.0f, 0.01f);
	return true;
}

/**
 * MIASMA'S OWN ROW, #2875: its sentence names no damage, so its pulse hurts nothing and still does what the sentence
 * says. A caster at the origin, its ally 2 m along +Y, one enemy 3 m along +X. The pulse finds the enemy, takes no
 * health from it, poisons it and helps the ally. THE CONTROL is the same aura given a figure by hand, the Aura slot's
 * 25: its next pulse takes 25, so nothing else in the rig stops a pulse hurting.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraDamageMiasmaRowTest,
	"Cataclysm.AuraDamage.MiasmasRowPoisonsAndHelpsWhatIsInsideAndDealsNoDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAuraDamageMiasmaRowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAuraDamageTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FCataclysmWeaponSkill Row = AuraDamageRow(TEXT("Fist"), TEXT("Pestilence"), ECataclysmAbilitySlot::Aura);
	const FGameplayTag Poison = AuraDamageAilmentTag(TEXT("Poison"));
	if (!TestEqual(TEXT("set-up: the Pestilence aura row is Miasma"), Row.Name, FString(TEXT("Miasma")))
		|| !TestEqual(TEXT("set-up: its row states no damage"), Row.DamagePercent, -1.0f, 0.001f)
		|| !TestEqual(TEXT("set-up: and the poison its sentence promises"), Row.Params.Ailment,
					  FString(TEXT("Poison")))
		|| !TestTrue(TEXT("set-up: the poison tag"), Poison.IsValid()))
	{
		return false;
	}

	FAuraDamageFighter Caster(World, FVector::ZeroVector);
	FAuraDamageFighter Ally(World, FVector(0.0f, 2.0f * AuraDamageMetre, 0.0f));
	FAuraDamageFighter Enemy(World, FVector(3.0f * AuraDamageMetre, 0.0f, 0.0f));
	Ally.Actor->SetOwner(Caster.Actor);
	UCataclysmAuraSkill* Aura = AuraDamageActivatedFromTheRow(Caster, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted and switched on"), Aura))
	{
		return false;
	}
	TestEqual(TEXT("an aura whose row states no damage deals nothing"), Aura->GetDamagePercent(), 0.0f, 0.0f);

	const float EnemyBefore = Enemy.Health();
	TestEqual(TEXT("its pulse still finds the enemy inside"), Aura->Pulse(), 1);
	TestEqual(TEXT("and takes no health from it"), Enemy.Health(), EnemyBefore, 0.0f);
	TestTrue(TEXT("but poisons it, as its sentence says"), Enemy.Carries(Poison));
	TestTrue(TEXT("and helps the ally inside, as its sentence says"), Aura->IsHelping(Ally.Actor));

	// THE CONTROL: THE SAME AURA WITH A FIGURE HURTS.
	Aura->DamagePercentOverride = 25.0f;
	const float BeforeTheControl = Enemy.Health();
	Aura->Pulse();
	TestEqual(TEXT("CONTROL: given the Aura slot's 25 by hand, its next pulse takes 25"),
			  BeforeTheControl - Enemy.Health(), 25.0f, 0.01f);
	return true;
}

/**
 * EVERY AURA ROW IN THE TABLE, #2875. Each row whose Shape is Aura, read through `SkillsOfDamageType` for each of the
 * eight damage types and stamped onto an aura as a grant stamps it, deals what its row states and nought when it
 * states none: Conflagration 10, Living Pyre 66.67, the other eleven nought. Thirteen rows, counted, so a new aura row
 * fails here until somebody says what it deals. The thirteenth is the Death Greatsword's Grave Muster, a Cripple and an
 * ally bonus with no damage named (issue #2885, 2026-11-24).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraDamageEveryRowTest,
	"Cataclysm.AuraDamage.EveryAuraRowDealsOnlyTheDamageItsRowStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAuraDamageEveryRowTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill table loads"), Table))
	{
		return false;
	}

	TMap<FString, float> Stating;
	Stating.Add(TEXT("Conflagration"), 10.0f);
	Stating.Add(TEXT("Living Pyre"), 66.67f);

	TArray<FString> Seen;
	for (const TCHAR* DamageType : {TEXT("War"), TEXT("Demonic"), TEXT("Death"), TEXT("Pestilence"), TEXT("Famine"),
									TEXT("Celestial"), TEXT("Chaos"), TEXT("Void")})
	{
		for (const FCataclysmWeaponSkill& Skill : UCataclysmWeaponSkills::SkillsOfDamageType(Table, DamageType))
		{
			if (Skill.Shape != ECataclysmSkillShape::Aura)
			{
				continue;
			}
			UCataclysmAuraSkill* Aura = NewObject<UCataclysmAuraSkill>(GetTransientPackage());
			UCataclysmWeaponSkills::StampOnto(*Aura, Skill);
			const float* Stated = Stating.Find(Skill.Name);
			const float Expected = Stated ? *Stated : 0.0f;
			TestEqual(FString::Printf(TEXT("%s (%s) deals %.0f%% of weapon damage a second"), *Skill.Name,
									  *Skill.RowName.ToString(), Expected),
					  Aura->GetDamagePercent(), Expected, 0.001f);
			Seen.Add(Skill.Name);
		}
	}

	TestEqual(TEXT("thirteen rows of the table have the Aura shape"), Seen.Num(), 13);
	TestTrue(TEXT("Conflagration is one of them"), Seen.Contains(FString(TEXT("Conflagration"))));
	TestTrue(TEXT("and Living Pyre"), Seen.Contains(FString(TEXT("Living Pyre"))));
	TestTrue(TEXT("and Miasma, which states nothing"), Seen.Contains(FString(TEXT("Miasma"))));
	TestTrue(TEXT("and Grave Muster, which states nothing"), Seen.Contains(FString(TEXT("Grave Muster"))));
	return true;
}

/**
 * LIVING PYRE'S OWN ROW, #2875: its sentence says enemies within 4 metres are continuously set alight and that every
 * hit its holder takes raises the pyre's fire damage by 8% with no cap and returns 25% of that hit as health. The
 * figure a pulse deals is not in the sentence, and the owner ruled it on 2026-11-24, "400% spread over 6 pulses
 * (Recommended)": the row states 66.67% a pulse. A holder at the origin, hurt to half health, one enemy 2 m along +X
 * with 1,000 health, so a figure with two decimals reads back to within a ten-thousandth, and one 5 m along -X. A
 * pulse takes 66.67 from the near enemy and sets it alight; a hit of 200 on the holder returns 50; the next pulse
 * takes 8% more, 72.0036. THE CONTROL is the far enemy, untouched.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAuraDamageLivingPyreRowTest,
	"Cataclysm.AuraDamage.LivingPyresRowBurnsWithinFourMetresForItsStatedFigureAndGrowsEightPercentAHitTaken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAuraDamageLivingPyreRowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmAuraDamageTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FCataclysmWeaponSkill Row = AuraDamageRow(TEXT("Fist"), TEXT("Demonic"), ECataclysmAbilitySlot::Ultimate);
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestEqual(TEXT("set-up: the Demonic Fist Ultimate row is Living Pyre"), Row.Name,
				   FString(TEXT("Living Pyre")))
		|| !TestEqual(TEXT("set-up: its row states a ring of 4 m"), Row.Params.RadiusCm, 4.0f * AuraDamageMetre,
					  0.01f)
		|| !TestEqual(TEXT("set-up: and 66.67% a pulse. If -1 or 400, DT_WeaponSkills is older than the CSV"),
					  Row.DamagePercent, 66.67f, 0.001f)
		|| !TestTrue(TEXT("set-up: the burn tag"), Burn.IsValid()))
	{
		return false;
	}

	FAuraDamageFighter Holder(World, FVector::ZeroVector);
	FAuraDamageFighter Near(World, FVector(2.0f * AuraDamageMetre, 0.0f, 0.0f));
	FAuraDamageFighter Far(World, FVector(-5.0f * AuraDamageMetre, 0.0f, 0.0f));
	// A SMALL POOL ON THE NEAR ENEMY, so a loss of 66.67 is read back to well inside the tolerance: at 100,000 health
	// a float holds health only to the nearest 0.008.
	Near.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	Near.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
	// THE ULTIMATE'S FERVOUR, and room to heal into so a returned share can be seen.
	Holder.Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
	Holder.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
	Holder.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 50000.0f);
	UCataclysmAuraSkill* Pyre = AuraDamageActivatedFromTheRow(Holder, Row);
	if (!TestNotNull(TEXT("set-up: the row is granted and lit"), Pyre))
	{
		return false;
	}

	const float NearBefore = Near.Health();
	const float FarBefore = Far.Health();
	TestEqual(TEXT("a pulse finds the one enemy within 4 m"), Pyre->Pulse(), 1);
	TestEqual(TEXT("and takes the row's 66.67% of a weapon dealing 100 from it"), NearBefore - Near.Health(), 66.67f,
			  0.01f);
	TestTrue(TEXT("and sets it alight, as the sentence says"), Near.Carries(Burn));
	TestEqual(TEXT("CONTROL: the enemy 5 m away loses nothing"), Far.Health(), FarBefore, 0.0f);
	TestFalse(TEXT("CONTROL: and is not set alight"), Far.Carries(Burn));

	TestEqual(TEXT("a hit of 200 on the holder returns 25% of it, 50, as health"), Pyre->NoteBlowTaken(200.0f), 50.0f,
			  0.01f);
	TestEqual(TEXT("and is counted"), Pyre->BlowsTaken, 1);

	const float BeforeTheHotter = Near.Health();
	Pyre->Pulse();
	TestEqual(TEXT("and the next pulse is 8% hotter: 66.67 times 1.08, 72.0036"), BeforeTheHotter - Near.Health(),
			  66.67f * 1.08f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
