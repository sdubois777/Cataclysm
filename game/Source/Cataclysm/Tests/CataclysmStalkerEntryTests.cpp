// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmCorruptedStalkerCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * The six Corrupted Stalker entries issue #2580 authored, one for each of Death, Pestilence, Famine, Celestial, Chaos
 * and Void, and the 36 Weapon Skills rows they wear. Ruled by the owner on 2026-11-17.
 *
 * THREE THINGS ARE SHOWN. Each entry is what `docs/DECISIONS.md` says it is: its weapon, the shared `Default` class
 * line, no passives, the eighteen pieces. Each wears its six skills through `UCataclysmWeaponSkills::SkillsFor`, one in
 * each slot of the brain's order, and a Stalker of each type chooses and casts each of them. And the key behaviours the
 * rows rely on, each on the generated row itself: the Death heavy's Necrosis lands on the player, the Chaos support
 * lays one of its two curses a pulse, and the Void and Celestial auras move a target as their sentences say under the
 * five-second halving rule.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the branch: every figure is reasoned from the code and the rows, and the run that
 * proves them is registered in `docs/DECISIONS.md` before it happens. Every test here reads the generated
 * `DT_WeaponSkills` asset, so the window must rebuild it first.
 */
namespace CataclysmStalkerEntryTest
{
	constexpr float M = 100.0f;
	using Slot = ECataclysmAbilitySlot;

	/** One authored entry as `docs/DECISIONS.md` records it, and its six skill names in the brain's order. */
	struct FExpectedEntry
	{
		const TCHAR* DamageType;
		const TCHAR* Name;
		const TCHAR* WeaponType;
		const TCHAR* WeaponBase;
		const TCHAR* Skills[6]; // aura, support, movement, ultimate, heavy, special
	};

	const TArray<FExpectedEntry>& TheSixKitEntries()
	{
		static const TArray<FExpectedEntry> Six = {
			{TEXT("Death"), TEXT("Morwen the Gravewalker"), TEXT("Dagger"), TEXT("Weapon_Dagger"),
			 {TEXT("Pall of Ending"), TEXT("Grave Shroud"), TEXT("Veilstep"), TEXT("Reaping Hour"), TEXT("Gravepoint"),
			  TEXT("Wraith Knife")}},
			{TEXT("Pestilence"), TEXT("Oswin the Carrier"), TEXT("Fist"), TEXT("Weapon_Fist"),
			 {TEXT("Miasma"), TEXT("Fevered Blood"), TEXT("Retch Rush"), TEXT("Pandemic"), TEXT("Septic Hook"),
			  TEXT("Bile Flask")}},
			{TEXT("Famine"), TEXT("Hesk the Starveling"), TEXT("Sword"), TEXT("Weapon_Sword"),
			 {TEXT("Gnawing Want"), TEXT("Blood Hunger"), TEXT("Lean Lunge"), TEXT("The Lean Years"), TEXT("Hollow Cut"),
			  TEXT("Tithe of Want")}},
			{TEXT("Celestial"), TEXT("Aurel the Unbowed"), TEXT("Warhammer"), TEXT("Weapon_Warhammer"),
			 {TEXT("Exposing Light"), TEXT("Aegis of Dawn"), TEXT("Descent"), TEXT("Heaven's Verdict"), TEXT("Sentence"),
			  TEXT("Hammer of Dawn")}},
			{TEXT("Chaos"), TEXT("Tamsin the Unreasoning"), TEXT("Whip"), TEXT("Weapon_Whip"),
			 {TEXT("Unreason"), TEXT("Coin of Misrule"), TEXT("Lurch"), TEXT("Pandemonium"), TEXT("Wild Lash"),
			  TEXT("Hex of Discord")}},
			{TEXT("Void"), TEXT("Ysolde the Starless"), TEXT("Greatsword"), TEXT("Weapon_Greatsword"),
			 {TEXT("Undertow"), TEXT("Swallowed Light"), TEXT("Fall Inward"), TEXT("Collapse"), TEXT("Erasure"),
			  TEXT("Dark Star")}},
		};
		return Six;
	}

	/** The entry for a damage type, or an empty one. */
	FCataclysmStalkerEntry EntryOf(const TCHAR* DamageType)
	{
		for (const FCataclysmStalkerEntry& Entry : ACataclysmCorruptedStalkerCharacter::Entries())
		{
			if (Entry.DamageType == FName(DamageType))
			{
				return Entry;
			}
		}
		return FCataclysmStalkerEntry();
	}

	/** One generated row's skill, by its weapon type, damage type and designed slot, or an empty one. */
	FCataclysmWeaponSkill RowSkill(const TCHAR* WeaponType, const TCHAR* DamageType, Slot In)
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

	int32 ChoiceIndexOf(Slot In)
	{
		return ACataclysmCorruptedStalkerCharacter::ChoiceOrder().IndexOfByKey(In);
	}

	/** A Corrupted Stalker of the monsters' team at `Where`, wearing `Entry` at tier 4 on seed 2, or null. */
	ACataclysmCorruptedStalkerCharacter* AStalkerWearing(UWorld* World, const FCataclysmStalkerEntry& Entry,
														 const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmCorruptedStalkerCharacter* Stalker = World->SpawnActor<ACataclysmCorruptedStalkerCharacter>(
			ACataclysmCorruptedStalkerCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (!Stalker)
		{
			return nullptr;
		}
		Stalker->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		FRandomStream Stream(2);
		return Stalker->WearTheBuild(Entry, /*DifficultyTier=*/4, Stream) ? Stalker : nullptr;
	}

	float Read(const AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Actor);
		return Abilities && Abilities->HasAttributeSetForAttribute(Attribute) ? Abilities->GetNumericAttribute(Attribute)
																			   : 0.0f;
	}

	void Write(AActor* Actor, const FGameplayAttribute& Attribute, float Value)
	{
		if (UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			Abilities->SetNumericAttributeBase(Attribute, Value);
		}
	}

	/** Its mana and health as shares of their maxima, and its Fervour out of 100: the brain's three conditions. */
	void StalkerAt(ACataclysmCorruptedStalkerCharacter* Stalker, float ManaShare, float HealthShare, float Fervour)
	{
		using Vital = UCataclysmVitalAttributeSet;
		using Resource = UCataclysmClassResourceAttributeSet;
		Write(Stalker, Vital::GetManaAttribute(), Read(Stalker, Vital::GetMaxManaAttribute()) * ManaShare);
		Write(Stalker, Vital::GetHealthAttribute(), Read(Stalker, Vital::GetMaxHealthAttribute()) * HealthShare);
		Write(Stalker, Resource::GetMaxClassResourceAttribute(), 100.0f);
		Write(Stalker, Resource::GetClassResourceAttribute(), Fervour);
	}

	/** A player character on its own player state and controller, possessed as the running game wires one. */
	ACataclysmPlayerCharacter* APlayerAt(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
			ACataclysmPlayerCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (!State || !Controller || !Character)
		{
			return nullptr;
		}
		Controller->SetPlayerState(State);
		Controller->Possess(Character);
		// HEALTH NO KIT'S BLOW CAN EMPTY, so a blow's riders are read off a living player.
		Write(Character, UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000000.0f);
		Write(Character, UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000000.0f);
		return Character;
	}

	/**
	 * An actor a sphere overlap can find, holding the sets a skill reads and pays from: the shape of `FKitFighter` in
	 * `CataclysmStalkerKitMechanismTests.cpp`, under its own name so the two files can share a unity build. It
	 * belongs to no team, which reads as hostile to everyone.
	 */
	struct FEntryFighter
	{
		FEntryFighter(UWorld* World, const FVector& Where)
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
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FEntryFighter()
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

		float Get(const FGameplayAttribute& Attribute) const { return AbilitySystem->GetNumericAttribute(Attribute); }
		bool Carries(const FGameplayTag& Tag) const { return Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * The generated row granted to `Caster` in its designed slot at level 100, stamped as a weapon's grant stamps it,
	 * and activated. Null when the row or the grant is missing, or the activation is refused.
	 */
	UCataclysmAuraSkill* ActivatedAuraFromTheRow(const FEntryFighter& Caster, const FCataclysmWeaponSkill& Row)
	{
		if (Row.Shape != ECataclysmSkillShape::Aura)
		{
			return nullptr;
		}
		const Slot In = UCataclysmWeaponSkills::DesignedSlotOf(Row);
		const FGameplayAbilitySpecHandle Handle =
			Caster.AbilitySystem->GiveAbilityInSlot(UCataclysmAuraSkill::StaticClass(), In, /*Level=*/100, Caster.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmAuraSkill* Aura = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Aura)
		{
			return nullptr;
		}
		UCataclysmWeaponSkills::StampOnto(*Aura, Row);
		return Caster.AbilitySystem->TryActivateAbility(Handle, /*bAllowRemoteActivation=*/false) ? Aura : nullptr;
	}

	/** Pins one console variable at the console's own priority for the life of this object. */
	struct FEntryPin
	{
		FEntryPin(const TCHAR* Name, int32 Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Variable)
			{
				Previous = Variable->GetInt();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FEntryPin()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		int32 Previous = -1;
	};
}

// ==========================================================================
// The six entries
// ==========================================================================

/**
 * EACH OF THE SIX IS AUTHORED AS `docs/DECISIONS.md` RECORDS IT: its name, its weapon type, the shared `Default` class
 * line (no class line exists for its type, and the 2026-10-25 ruling gives an entry "a class or passives only where
 * designed"), no passive points, and eighteen pieces in eighteen slots, each base and affix a row, its weapon the
 * entry's own. THE CONTROLS: War's slot stays empty under the standing no-War order, and Kaedric the Hollowed is still
 * a Ravager with the Greataxe and his thirteen points.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerEntriesAuthoredTest,
	"Cataclysm.StalkerEntries.EachOfTheSixKitEntriesIsAuthoredWithItsWeaponAndNoClassLineOrPassives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerEntriesAuthoredTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStalkerEntryTest;

	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	const UDataTable* Affixes = UCataclysmDropRoll::LoadAffixTable();
	if (!TestNotNull(TEXT("set-up: the base table"), Bases) || !TestNotNull(TEXT("set-up: the affix table"), Affixes))
	{
		return false;
	}

	TestEqual(TEXT("seven entries are authored"), ACataclysmCorruptedStalkerCharacter::AuthoredEntries().Num(), 7);
	TestFalse(TEXT("CONTROL: War's slot is named and empty"), EntryOf(TEXT("War")).IsAuthored());
	const FCataclysmStalkerEntry Kaedric = EntryOf(TEXT("Demonic"));
	TestEqual(TEXT("CONTROL: the Demonic entry is still Kaedric the Hollowed"), Kaedric.Name,
			  FString(TEXT("Kaedric the Hollowed")));
	TestEqual(TEXT("CONTROL: a Ravager"), Kaedric.ClassName, FString(TEXT("Ravager")));
	TestEqual(TEXT("CONTROL: with the Greataxe"), Kaedric.WeaponType, FString(TEXT("Greataxe")));
	TestEqual(TEXT("CONTROL: and his thirteen points in two nodes"), Kaedric.Allocation.Nodes.Num(), 2);

	for (const FExpectedEntry& Expected : TheSixKitEntries())
	{
		const FCataclysmStalkerEntry Entry = EntryOf(Expected.DamageType);
		const FString Type = Expected.DamageType;
		if (!TestTrue(FString::Printf(TEXT("%s's entry is authored"), *Type), Entry.IsAuthored()))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s's is called %s"), *Type, Expected.Name), Entry.Name,
				  FString(Expected.Name));
		TestEqual(FString::Printf(TEXT("%s's is on the shared Default class line"), *Type), Entry.ClassName,
				  UCataclysmClassStats::DefaultClassName);
		TestEqual(FString::Printf(TEXT("%s's carries a %s"), *Type, Expected.WeaponType), Entry.WeaponType,
				  FString(Expected.WeaponType));
		TestEqual(FString::Printf(TEXT("%s's spends no passive points"), *Type), Entry.Allocation.Nodes.Num(), 0);
		TestEqual(FString::Printf(TEXT("%s's wears eighteen pieces"), *Type), Entry.Pieces.Num(), 18);

		TSet<ECataclysmGearSlot> Slots;
		for (const FCataclysmStalkerPiece& Piece : Entry.Pieces)
		{
			Slots.Add(Piece.Slot);
			TestTrue(FString::Printf(TEXT("%s: %s is a base"), *Type, *Piece.Base.ToString()),
					 Bases->GetRowMap().Contains(Piece.Base));
			for (const FName& Affix : Piece.Affixes)
			{
				TestTrue(FString::Printf(TEXT("%s: %s is an affix"), *Type, *Affix.ToString()),
						 Affixes->GetRowMap().Contains(Affix));
			}
			if (Piece.Slot == ECataclysmGearSlot::Weapon1)
			{
				TestEqual(FString::Printf(TEXT("%s's weapon is its %s"), *Type, Expected.WeaponBase), Piece.Base,
						  FName(Expected.WeaponBase));
			}
		}
		TestEqual(FString::Printf(TEXT("%s's pieces are each in their own slot"), *Type), Slots.Num(), 18);

		// MELEE, BECAUSE A STALKER WALKS TO ITS BASIC ATTACK'S RADIUS BEFORE IT SWINGS (`AttackReachCm`).
		const FCataclysmWeaponSkill Basic = UCataclysmWeaponSkills::BasicAttackFor(Bases, Entry.WeaponType);
		TestTrue(FString::Printf(TEXT("%s's weapon has a basic attack"), *Type), Basic.Slot != Slot::None);
		TestEqual(FString::Printf(TEXT("%s's basic attack is a strike"), *Type), static_cast<int32>(Basic.Shape),
				  static_cast<int32>(ECataclysmSkillShape::Strike));
	}
	return true;
}

/**
 * EACH WEARS ITS SIX SKILLS THROUGH `SkillsFor`, THE CALL `WearTheBuild` MAKES: the weapon's five rows and the type's
 * Aura, one in each slot of the brain's order, each named as the row is and each with a shape, so no slot is the
 * placeholder the brain skips. THE CONTROL: the same call for a weapon the type's kit does not cover, Death's Axe,
 * answers nothing, so the six come from the authored rows and not from the call.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerEntriesSkillsTest,
	"Cataclysm.StalkerEntries.EachOfTheSixKitEntriesWearsItsSixSkillsThroughSkillsFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerEntriesSkillsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStalkerEntryTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill table"), Table))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: Death's Axe, which no kit covers, holds no skill"),
			  UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Axe"), TEXT("Death")).Num(), 0);

	const TArray<Slot> Order = ACataclysmCorruptedStalkerCharacter::ChoiceOrder();
	for (const FExpectedEntry& Expected : TheSixKitEntries())
	{
		const FCataclysmStalkerEntry Entry = EntryOf(Expected.DamageType);
		const TArray<FCataclysmWeaponSkill> Skills =
			UCataclysmWeaponSkills::SkillsFor(Table, Entry.WeaponType, Entry.DamageType.ToString());
		const FString Type = Expected.DamageType;
		if (!TestEqual(FString::Printf(TEXT("%s's %s with its Aura is six skills"), *Type, Expected.WeaponType),
					   Skills.Num(), 6))
		{
			continue;
		}
		for (int32 At = 0; At < Order.Num(); ++At)
		{
			const FCataclysmWeaponSkill* Found = Skills.FindByPredicate([&Order, At](const FCataclysmWeaponSkill& Each)
				{ return UCataclysmWeaponSkills::DesignedSlotOf(Each) == Order[At]; });
			const FString InSlot = UEnum::GetValueAsString(Order[At]);
			if (!TestNotNull(FString::Printf(TEXT("%s holds a skill in %s"), *Type, *InSlot), Found))
			{
				continue;
			}
			TestEqual(FString::Printf(TEXT("%s's %s is %s"), *Type, *InSlot, Expected.Skills[At]), Found->Name,
					  FString(Expected.Skills[At]));
			TestTrue(FString::Printf(TEXT("%s's %s has a shape, so its brain may choose it"), *Type, *InSlot),
					 Found->Shape != ECataclysmSkillShape::None);
		}
	}
	return true;
}

// ==========================================================================
// A Stalker of each type chooses and casts each slot
// ==========================================================================

/**
 * ITS BRAIN CHOOSES EACH OF THE SIX, under the conditions `TheCorruptedStalkersBrainChoosesTheFirstSkillItMayUseInTheRuled
 * Order` uses for Kaedric: below 30% mana, full health and no Fervour, the heavy skill at 2 m (every heavy reaches 2.5 m
 * or more), the special at 8 m (every heavy reaches 5 m at most and every special 12 m), the movement skill at 20 m;
 * with Fervour, the ultimate at 2 m (every ultimate reaches 4 m or more); below half its health, the support skill;
 * with its mana, the aura. THE CONTROL: at 14 m, where nothing it holds reaches, it chooses nothing and chases.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerEntriesChooseTest,
	"Cataclysm.StalkerEntries.AStalkerOfEachTypeChoosesEachOfItsSixSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerEntriesChooseTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStalkerEntryTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	float Along = 0.0f;
	for (const FExpectedEntry& Expected : TheSixKitEntries())
	{
		const FString Type = Expected.DamageType;
		ACataclysmCorruptedStalkerCharacter* Stalker =
			AStalkerWearing(World, EntryOf(Expected.DamageType), FVector(Along, 0.0f, 0.0f));
		Along += 100.0f * M;
		ACataclysmEnemyController* Brain = Stalker ? Cast<ACataclysmEnemyController>(Stalker->GetController()) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("set-up: a %s Stalker wears its build"), *Type), Stalker)
			|| !TestNotNull(FString::Printf(TEXT("set-up: the %s Stalker has a brain"), *Type), Brain))
		{
			continue;
		}

		StalkerAt(Stalker, 0.2f, 1.0f, 0.0f);
		TestEqual(FString::Printf(TEXT("%s: at 2 m, the heavy skill"), *Type), Brain->ChooseAbility(2.0f * M),
				  ChoiceIndexOf(Slot::Heavy));
		TestEqual(FString::Printf(TEXT("%s: at 8 m, the special"), *Type), Brain->ChooseAbility(8.0f * M),
				  ChoiceIndexOf(Slot::Special));
		TestEqual(FString::Printf(TEXT("%s: at 20 m, the movement skill"), *Type), Brain->ChooseAbility(20.0f * M),
				  ChoiceIndexOf(Slot::Movement));
		TestEqual(FString::Printf(TEXT("%s: CONTROL: at 14 m nothing reaches, so it chases"), *Type),
				  Brain->ChooseAbility(14.0f * M), INDEX_NONE);

		StalkerAt(Stalker, 0.2f, 1.0f, 100.0f);
		TestEqual(FString::Printf(TEXT("%s: with its Fervour, at 2 m, the ultimate"), *Type),
				  Brain->ChooseAbility(2.0f * M), ChoiceIndexOf(Slot::Ultimate));
		StalkerAt(Stalker, 0.2f, 0.45f, 100.0f);
		TestEqual(FString::Printf(TEXT("%s: below half its health, the support skill"), *Type),
				  Brain->ChooseAbility(2.0f * M), ChoiceIndexOf(Slot::Support));
		StalkerAt(Stalker, 1.0f, 0.45f, 100.0f);
		TestEqual(FString::Printf(TEXT("%s: with its mana, the aura"), *Type), Brain->ChooseAbility(2.0f * M),
				  ChoiceIndexOf(Slot::Aura));
	}
	return true;
}

/**
 * AND CASTS EACH OF THE SIX AT THE PLAYER, 2 m in front of it, through `UseEnemyAbility`, the call its brain makes: each
 * cast pays its mana, which every template takes in `CommitAndBegin` before it does anything else, so a paid cast is a
 * skill that started. Its mana is refilled, its health held below half and its Fervour at 100 before each, so every
 * slot's condition holds. The aura and the support are then running. THE CONTROL: a Stalker that wears no build casts
 * nothing and pays nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerEntriesCastTest,
	"Cataclysm.StalkerEntries.AStalkerOfEachTypeCastsEachOfItsSixSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerEntriesCastTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStalkerEntryTest;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	// THE CONTROL FIRST.
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmCorruptedStalkerCharacter* Bare = World->SpawnActor<ACataclysmCorruptedStalkerCharacter>(
			ACataclysmCorruptedStalkerCharacter::StaticClass(), FVector(0.0f, -100.0f * M, 0.0f), FRotator::ZeroRotator,
			Params);
		ACataclysmPlayerCharacter* Player = APlayerAt(World, FVector(2.0f * M, -100.0f * M, 0.0f));
		if (!TestNotNull(TEXT("set-up: a bare Stalker"), Bare) || !TestNotNull(TEXT("set-up: a player"), Player))
		{
			return false;
		}
		Write(Bare, Vital::GetMaxManaAttribute(), 500.0f);
		Write(Bare, Vital::GetManaAttribute(), 500.0f);
		Bare->UseEnemyAbility(ChoiceIndexOf(Slot::Heavy), Player, Player->GetActorLocation());
		TestEqual(TEXT("CONTROL: a Stalker wearing no build casts nothing and pays nothing"),
				  Read(Bare, Vital::GetManaAttribute()), 500.0f, 0.01f);
	}

	float Along = 0.0f;
	for (const FExpectedEntry& Expected : TheSixKitEntries())
	{
		const FString Type = Expected.DamageType;
		ACataclysmCorruptedStalkerCharacter* Stalker =
			AStalkerWearing(World, EntryOf(Expected.DamageType), FVector(Along, 0.0f, 0.0f));
		ACataclysmPlayerCharacter* Player = APlayerAt(World, FVector(Along + 2.0f * M, 0.0f, 0.0f));
		Along += 100.0f * M;
		if (!TestNotNull(FString::Printf(TEXT("set-up: a %s Stalker wears its build"), *Type), Stalker)
			|| !TestNotNull(FString::Printf(TEXT("set-up: a player before the %s Stalker"), *Type), Player))
		{
			continue;
		}
		for (const Slot Each : ACataclysmCorruptedStalkerCharacter::ChoiceOrder())
		{
			StalkerAt(Stalker, 1.0f, 0.45f, 100.0f);
			const float Before = Read(Stalker, Vital::GetManaAttribute());
			Stalker->UseEnemyAbility(ChoiceIndexOf(Each), Player, Player->GetActorLocation());
			const float After = Read(Stalker, Vital::GetManaAttribute());
			TestTrue(FString::Printf(TEXT("%s's %s (%s) was cast and paid for: mana %.1f to %.1f"), *Type,
									 *UEnum::GetValueAsString(Each),
									 Stalker->SkillIn(Each) ? *Stalker->SkillIn(Each)->Name : TEXT("none"), Before,
									 After),
					 After < Before);
		}
		TestTrue(FString::Printf(TEXT("%s: its aura is running"), *Type), Stalker->IsRunningASkillIn(Slot::Aura));
		TestTrue(FString::Printf(TEXT("%s: and its support"), *Type), Stalker->IsRunningASkillIn(Slot::Support));
	}
	return true;
}

// ==========================================================================
// What the rows rely on, each on its generated row
// ==========================================================================

/**
 * THE DEATH STALKER'S GRAVEPOINT LAYS NECROSIS ON THE PLAYER. Morwen casts her heavy skill at a player 2 m in front of
 * her, inside its 2.5 m and 60 degrees: the player carries `Keyword.DoT.Necrosis` (`Ailment=Necrosis`, #2730's [N1]).
 * THE CONTROL is Kaedric's heavy skill, Molten Cleave, at another player: it lands and lays no Necrosis.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerEntriesNecrosisTest,
	"Cataclysm.StalkerEntries.TheDeathStalkersHeavySkillLaysNecrosisOnThePlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerEntriesNecrosisTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStalkerEntryTest;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	const FGameplayTag Necrosis =
		UGameplayTagsManager::Get().RequestGameplayTag(FName(TEXT("Keyword.DoT.Necrosis")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: the Necrosis tag"), Necrosis.IsValid()))
	{
		return false;
	}

	const auto CastHeavy = [this, World](const TCHAR* Type, float Y, bool& bHurt) -> bool
	{
		ACataclysmCorruptedStalkerCharacter* Stalker = AStalkerWearing(World, EntryOf(Type), FVector(0.0f, Y, 0.0f));
		ACataclysmPlayerCharacter* Player = APlayerAt(World, FVector(2.0f * M, Y, 0.0f));
		UAbilitySystemComponent* Abilities = Player ? Player->GetAbilitySystemComponent() : nullptr;
		if (!TestNotNull(TEXT("set-up: the Stalker"), Stalker) || !TestNotNull(TEXT("set-up: the player"), Abilities))
		{
			return false;
		}
		const float Before = Read(Player, Vital::GetHealthAttribute());
		Stalker->UseEnemyAbility(ChoiceIndexOf(Slot::Heavy), Player, Player->GetActorLocation());
		bHurt = Read(Player, Vital::GetHealthAttribute()) < Before;
		const FGameplayTag Tag =
			UGameplayTagsManager::Get().RequestGameplayTag(FName(TEXT("Keyword.DoT.Necrosis")), false);
		return Abilities->HasMatchingGameplayTag(Tag);
	};

	bool bControlHurt = false;
	const bool bControlNecrosis = CastHeavy(TEXT("Demonic"), 40.0f * M, bControlHurt);
	TestTrue(TEXT("CONTROL: Kaedric's heavy skill lands on the player"), bControlHurt);
	TestFalse(TEXT("CONTROL: and lays no Necrosis"), bControlNecrosis);

	bool bHurt = false;
	const bool bNecrosis = CastHeavy(TEXT("Death"), 0.0f, bHurt);
	TestTrue(TEXT("Morwen's Gravepoint lands on the player"), bHurt);
	TestTrue(TEXT("and lays Necrosis"), bNecrosis);
	return true;
}

/**
 * THE CHAOS SUPPORT, COIN OF MISRULE, LAYS ONE OF ITS TWO CURSES A PULSE, the generated row granted to a caster and
 * pulsed by hand (`Effect=Shred, Weaken; EffectPick=Random; EffectMagnitude=30`). Pinned to the first place it lays
 * Shred alone, cutting the target's Chaos resistance from 40 to 10; pinned to the second, Weaken alone, its attack
 * damage from 100 to 70. Unpinned, each of four targets carries exactly one. THE CONTROL is the pin's own: in each
 * pinned pulse the other curse is absent.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerEntriesChaosPickTest,
	"Cataclysm.StalkerEntries.TheChaosSupportLaysOneOfItsTwoCursesAPulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerEntriesChaosPickTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStalkerEntryTest;
	using Combat = UCataclysmCombatAttributeSet;
	using Resist = UCataclysmResistanceAttributeSet;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FCataclysmWeaponSkill Coin = RowSkill(TEXT("Whip"), TEXT("Chaos"), Slot::Support);
	const FGameplayTag Shred = UCataclysmSkillShapes::StatusTagFor(TEXT("Shred"));
	const FGameplayTag Weaken = UCataclysmSkillShapes::StatusTagFor(TEXT("Weaken"));
	if (!TestEqual(TEXT("set-up: the Chaos support row is Coin of Misrule"), Coin.Name, FString(TEXT("Coin of Misrule")))
		|| !TestTrue(TEXT("set-up: the Shred and Weaken tags"), Shred.IsValid() && Weaken.IsValid()))
	{
		return false;
	}

	// ONE CASTER AND ONE TARGET A PULSE, 3 m apart and 30 m from the next pair, beyond the row's 4 m.
	const auto Pulsed = [this, World, &Coin](int32 Pin, float Y, FEntryFighter*& OutTarget) -> FEntryFighter*
	{
		const FEntryPin Pinned(TEXT("Cataclysm.SkillEffectPick"), Pin);
		FEntryFighter* Caster = new FEntryFighter(World, FVector(0.0f, Y, 0.0f));
		OutTarget = new FEntryFighter(World, FVector(3.0f * M, Y, 0.0f));
		OutTarget->Set(Resist::GetChaosResistanceAttribute(), 40.0f);
		UCataclysmAuraSkill* Aura = ActivatedAuraFromTheRow(*Caster, Coin);
		if (!TestNotNull(TEXT("set-up: the row activates"), Aura) || !TestEqual(TEXT("set-up: its pulse finds the target"),
																			   Aura ? Aura->Pulse() : 0, 1))
		{
			return Caster;
		}
		return Caster;
	};

	{
		FEntryFighter* Target = nullptr;
		TUniquePtr<FEntryFighter> Caster(Pulsed(0, 0.0f, Target));
		TUniquePtr<FEntryFighter> Owned(Target);
		TestTrue(TEXT("pinned to the first place, it lays Shred"), Target->Carries(Shred));
		TestFalse(TEXT("and not Weaken"), Target->Carries(Weaken));
		TestEqual(TEXT("cutting Chaos resistance from 40 to 10"), Target->Get(Resist::GetChaosResistanceAttribute()),
				  10.0f, 0.01f);
	}
	{
		FEntryFighter* Target = nullptr;
		TUniquePtr<FEntryFighter> Caster(Pulsed(1, 30.0f * M, Target));
		TUniquePtr<FEntryFighter> Owned(Target);
		TestTrue(TEXT("pinned to the second place, it lays Weaken"), Target->Carries(Weaken));
		TestFalse(TEXT("and not Shred"), Target->Carries(Shred));
		TestEqual(TEXT("cutting attack damage from 100 to 70"), Target->Get(Combat::GetAttackDamageAttribute()), 70.0f,
				  0.01f);
	}
	for (int32 Each = 0; Each < 4; ++Each)
	{
		FEntryFighter* Target = nullptr;
		TUniquePtr<FEntryFighter> Caster(Pulsed(-1, (60.0f + 30.0f * Each) * M, Target));
		TUniquePtr<FEntryFighter> Owned(Target);
		TestTrue(FString::Printf(TEXT("unpinned, target %d carries exactly one of the two"), Each),
				 Target->Carries(Shred) != Target->Carries(Weaken));
	}
	return true;
}

/**
 * THE VOID AURA, UNDERTOW, DRAGS A TARGET 2 m EVERY 6 SECONDS, AS ITS SENTENCE SAYS. The generated row, granted and
 * pulsed by hand: a target 7 m out is dragged to 5 m, and a pulse 6 s later, the row's own interval, drags it the whole
 * 2 m again, to 3 m, because 6 s is outside the 5 s window in which a second displacement moves half as far. THE
 * CONTROL is a second pair pulsed 1 s apart, as the draft's one-second interval would: the second pull is halved, 5 m to
 * 4 m. And the row's interval is held above that window, which is the whole of the retune.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerEntriesUndertowTest,
	"Cataclysm.StalkerEntries.TheVoidAuraDragsATargetTwoMetresEverySixSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerEntriesUndertowTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStalkerEntryTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	constexpr float Tolerance = 5.0f;

	const FCataclysmWeaponSkill Undertow = RowSkill(TEXT("Greatsword"), TEXT("Void"), Slot::Aura);
	if (!TestEqual(TEXT("set-up: the Void aura row is Undertow"), Undertow.Name, FString(TEXT("Undertow"))))
	{
		return false;
	}
	TestEqual(TEXT("its interval is 6 s"), Undertow.Params.Interval, 6.0f, 0.001f);
	TestTrue(TEXT("which is longer than the 5 s displacement window"),
			 Undertow.Params.Interval > UCataclysmSkillEffects::StunImmunityWindowSeconds);

	const auto Pulled = [this, World, &Undertow](float Y, double SecondsBetween, float& First, float& Second)
	{
		FEntryFighter Caster(World, FVector(0.0f, Y, 0.0f));
		FEntryFighter Target(World, FVector(7.0f * M, Y, 0.0f));
		UCataclysmAuraSkill* Aura = ActivatedAuraFromTheRow(Caster, Undertow);
		if (!TestNotNull(TEXT("set-up: the row activates"), Aura))
		{
			return;
		}
		Aura->Pulse();
		First = static_cast<float>(Target.Actor->GetActorLocation().X);
		World->TimeSeconds += SecondsBetween;
		Aura->Pulse();
		Second = static_cast<float>(Target.Actor->GetActorLocation().X);
	};

	float ControlFirst = 0.0f;
	float ControlSecond = 0.0f;
	Pulled(-40.0f * M, 1.0, ControlFirst, ControlSecond);
	TestEqual(TEXT("CONTROL: the first pull is 2 m, 7 m to 5 m"), ControlFirst, 5.0f * M, Tolerance);
	TestEqual(TEXT("CONTROL: a pull 1 s later is halved, 5 m to 4 m"), ControlSecond, 4.0f * M, Tolerance);

	float First = 0.0f;
	float Second = 0.0f;
	Pulled(0.0f, Undertow.Params.Interval, First, Second);
	TestEqual(TEXT("one pulse drags the target 2 m, 7 m to 5 m"), First, 5.0f * M, Tolerance);
	TestEqual(TEXT("and the next, 6 s later, the whole 2 m again, to 3 m"), Second, 3.0f * M, Tolerance);
	return true;
}

/**
 * THE CELESTIAL SUPPORT, AEGIS OF DAWN, PUSHES 3 m, THEN HALF AS FAR WITHIN 5 SECONDS, AS ITS SENTENCE SAYS. The
 * generated row, granted and pulsed by hand: a target 1 m away is pushed to 4 m. Put back at 1 m, as if it walked in
 * again, a pulse 1 s later pushes it half as far, 1.5 m, to 2.5 m. Put back once more, a pulse 6 s after that, beyond
 * the window, pushes the whole 3 m again, to 4 m. THE CONTROL is the first push itself: the full stated distance.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStalkerEntriesAegisTest,
	"Cataclysm.StalkerEntries.TheCelestialSupportPushesThreeMetresThenHalfAsFarWithinFiveSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStalkerEntriesAegisTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmStalkerEntryTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	constexpr float Tolerance = 5.0f;

	const FCataclysmWeaponSkill Aegis = RowSkill(TEXT("Warhammer"), TEXT("Celestial"), Slot::Support);
	if (!TestEqual(TEXT("set-up: the Celestial support row is Aegis of Dawn"), Aegis.Name, FString(TEXT("Aegis of Dawn"))))
	{
		return false;
	}

	FEntryFighter Caster(World, FVector::ZeroVector);
	FEntryFighter Target(World, FVector(1.0f * M, 0.0f, 0.0f));
	UCataclysmAuraSkill* Aura = ActivatedAuraFromTheRow(Caster, Aegis);
	if (!TestNotNull(TEXT("set-up: the row activates"), Aura))
	{
		return false;
	}
	Aura->Pulse();
	TestEqual(TEXT("the first pulse pushes the target 3 m, 1 m to 4 m"),
			  static_cast<float>(Target.Actor->GetActorLocation().X), 4.0f * M, Tolerance);

	Target.Actor->SetActorLocation(FVector(1.0f * M, 0.0f, 0.0f));
	World->TimeSeconds += 1.0;
	Aura->Pulse();
	TestEqual(TEXT("a pulse 1 s later pushes it half as far, 1 m to 2.5 m"),
			  static_cast<float>(Target.Actor->GetActorLocation().X), 2.5f * M, Tolerance);

	Target.Actor->SetActorLocation(FVector(1.0f * M, 0.0f, 0.0f));
	World->TimeSeconds += 6.0;
	Aura->Pulse();
	TestEqual(TEXT("and one 6 s after that, beyond the window, the whole 3 m again, to 4 m"),
			  static_cast<float>(Target.Actor->GetActorLocation().X), 4.0f * M, Tolerance);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
