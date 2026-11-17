// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmPotions.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmCorruptedStalkerCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
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
 * The mechanisms the Corrupted Stalker's six weapon kits need. Issue #2580, ruled by the owner on 2026-11-17.
 *
 * NO ROW OF THE TABLE STATES ANY OF THEM YET: the 36 rows are authored later, by another branch. So every skill here is
 * built from a hand-written Shape Params cell through `UCataclysmSkillShapes::ParseParams`, which is the parse the game
 * runs on a generated row. Each key is shown doing what it says, beside a CONTROL showing the old behaviour without it.
 *
 *   [N1] `Ailment=`: a landed blow lays the ailment outright, from a strike, a fired shot and a zero-damage aura.
 *   The potion exemption: a Corrupted Stalker's Necrosis spares a potion and still denies every other heal, and a
 *   Necrosis from any other source still denies the potion.
 *   [N3] `DamageRollMin`, `DamageRollMax`: the figure is drawn per use between the two.
 *   [N4] `EffectPick=Random`: one named effect per application, not all.
 *   [N5] `Effect=RandomBuff`: a self buff holds one buff from the game's own pool, for its own duration.
 *   And the four uses the draft called unproven: a timed aura in the Support slot, a pull and a knockback from an
 *   aura's pulse, Withered Touch through `Effect=`, and Shred, Weaken and Stun from another character's skill landing
 *   on the player.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the branch: every figure below is reasoned from the code, and the run that
 * proves them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmStalkerKitTest
{
	constexpr float M = 100.0f;

	/** What a kit fighter's weapon deals, so a damage figure is exact. */
	constexpr float KitWeaponDamage = 100.0f;

	/**
	 * An actor a sphere overlap can find, holding the attribute sets a skill reads and pays from. The shape of
	 * `FScopedFighter` in `CataclysmSkillTemplateTests.cpp`, under its own name so the two files can share a unity
	 * build. It belongs to no team, which reads as hostile to everyone: it stands for a creature casting at the
	 * player, and for a target of a player's skill.
	 */
	struct FKitFighter
	{
		FKitFighter(UWorld* World, const FVector& Where)
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
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), KitWeaponDamage);
		}

		~FKitFighter()
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

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Health() const { return Get(UCataclysmVitalAttributeSet::GetHealthAttribute()); }
		float Mana() const { return Get(UCataclysmVitalAttributeSet::GetManaAttribute()); }
		bool Carries(const FGameplayTag& Tag) const { return Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Grant a template into a slot at level 100, its cell parsed as the game parses a row's. */
	template <typename T>
	T* GrantKitSkill(UCataclysmAbilitySystemComponent* AbilitySystem, AActor* Owner, ECataclysmAbilitySlot Slot,
					 const FString& ParamText, const FString& TagCell = FString())
	{
		const FGameplayAbilitySpecHandle Handle =
			AbilitySystem->GiveAbilityInSlot(T::StaticClass(), Slot, /*Level=*/100, Owner);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = TEXT("Stalker Kit Test Skill");
			Instance->Params = UCataclysmSkillShapes::ParseParams(ParamText);
			Instance->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		}
		return Instance;
	}

	template <typename T>
	T* GrantKitSkill(const FKitFighter& Caster, ECataclysmAbilitySlot Slot, const FString& ParamText,
					 const FString& TagCell = FString())
	{
		return GrantKitSkill<T>(Caster.AbilitySystem, Caster.Actor, Slot, ParamText, TagCell);
	}

	bool ActivateKitSkill(UAbilitySystemComponent* AbilitySystem, UGameplayAbility* Ability)
	{
		return AbilitySystem && Ability
			&& AbilitySystem->TryActivateAbility(Ability->GetCurrentAbilitySpecHandle(),
												 /*bAllowRemoteActivation=*/false);
	}

	/** A tag by name, or an invalid one this build does not know. */
	FGameplayTag KitTag(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/**
	 * Pins one console variable for the life of this object, AT THE CONSOLE'S OWN PRIORITY, as
	 * `CataclysmTestWorld::FScopedAilmentRoll` does and for the reason `FScopedDifficultyTier` gives: a lower
	 * priority write is discarded once any test has written the variable at console priority.
	 */
	struct FKitPin
	{
		FKitPin(const TCHAR* Name, float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Write(Value);
			}
		}
		~FKitPin()
		{
			if (Variable)
			{
				Write(Previous);
			}
		}
		/** A whole number to a whole-number variable, so a pick of 3 is not written as the text "3.0". */
		void Write(float Value) const
		{
			if (Variable->IsVariableInt())
			{
				Variable->Set(FMath::RoundToInt(Value), ECVF_SetByConsole);
			}
			else
			{
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** A player character on its own player state and controller, possessed as the running game wires one. */
	struct FKitPlayer
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		bool IsUsable() const { return Character && AbilitySystem; }
		float Get(const FGameplayAttribute& Attribute) const { return AbilitySystem->GetNumericAttribute(Attribute); }
		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}
		float Health() const { return Get(UCataclysmVitalAttributeSet::GetHealthAttribute()); }
		bool Carries(const FGameplayTag& Tag) const { return Tag.IsValid() && AbilitySystem->HasMatchingGameplayTag(Tag); }
	};

	FKitPlayer SpawnKitPlayer(UWorld* World, const FVector& Where)
	{
		FKitPlayer Made;
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Made.Character = World->SpawnActor<ACataclysmPlayerCharacter>(
			ACataclysmPlayerCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (!State || !Controller || !Made.Character)
		{
			Made.Character = nullptr;
			return Made;
		}
		Controller->SetPlayerState(State);
		Controller->Possess(Made.Character);
		Made.AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Made.Character->GetAbilitySystemComponent());
		return Made;
	}

	/** The healing a character's Necrosis has denied it so far. */
	float KitDenied(const FKitPlayer& Player)
	{
		return Player.AbilitySystem ? Player.AbilitySystem->NecrosisDeniedHealing : -1.0f;
	}

	/** Maximum health 1000, health 500, no regeneration, so a heal has room and nothing else adds to it. */
	constexpr float KitMaximum = 1000.0f;
	constexpr float KitStart = 500.0f;

	void HalfHealthWithNoRegeneration(const FKitPlayer& Player)
	{
		Player.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), KitMaximum);
		Player.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), KitStart);
		Player.Set(UCataclysmVitalAttributeSet::GetHealthRegenAttribute(), 0.0f);
	}

	/** Drink slot 1 with its charges filled, and pay the whole heal in one step. What the step paid. */
	float DrinkAndPayTheWholeHeal(const FKitPlayer& Player, FAutomationTestBase& Test)
	{
		UCataclysmPotions::RefillAll(Player.Character);
		if (!Test.TestEqual(TEXT("set-up: the potion is drunk"),
							static_cast<int32>(UCataclysmPotions::Drink(Player.Character, 0)),
							static_cast<int32>(ECataclysmPotionRefusal::None)))
		{
			return -1.0f;
		}
		// A STEP LONGER THAN THE TIME LEFT PAYS IT ALL: see `UCataclysmPotions::HealStep`.
		return UCataclysmPotions::HealStep(Player.Character, UCataclysmPotions::HealSeconds + 1.0f);
	}

	/** A Corrupted Stalker standing here, of the monsters' team, wearing nothing: the class is what is asked. */
	ACataclysmCorruptedStalkerCharacter* SpawnKitStalker(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmCorruptedStalkerCharacter* Stalker = World->SpawnActor<ACataclysmCorruptedStalkerCharacter>(
			ACataclysmCorruptedStalkerCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (Stalker)
		{
			Stalker->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		}
		return Stalker;
	}

	/** Lay a Necrosis on the player through the route `Ailment=Necrosis` takes: `UCataclysmAilments::Apply`. */
	bool LayNecrosis(AActor* Applier, const FKitPlayer& Player)
	{
		const FCataclysmAilmentKind* Necrosis = UCataclysmAilments::KindNamedInASkillRow(TEXT("Necrosis"));
		return Necrosis && UCataclysmAilments::Apply(Applier, Player.Character, *Necrosis, /*Magnitude=*/1.0f);
	}
}

#define CATACLYSM_STALKER_KIT_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ==========================================================================
// [N1] Ailment=
// ==========================================================================

/**
 * EACH OF THE OWNER'S FOUR AILMENTS IS LAID BY A LANDED BLOW OF A ROW STATING IT. Four casters, each striking the
 * target two metres in front with a Heavy ring stating one of `Poison`, `Disease`, `Necrosis` and `VoidSplinter`; each
 * target carries that ailment's tag, and none of the other three. THE CONTROL is a fifth caster striking with the same
 * ring and no `Ailment`: its target is hurt, so the blow landed, and carries none of the four.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitAilmentStrikeTest,
	"Cataclysm.StalkerKit.AStatedAilmentIsLaidByALandedBlow")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	struct FOne { const TCHAR* Written; const TCHAR* Tag; };
	const FOne Four[] = {
		{TEXT("Poison"), TEXT("Keyword.DoT.Poison")},
		{TEXT("Disease"), TEXT("Keyword.DoT.Disease")},
		{TEXT("Necrosis"), TEXT("Keyword.DoT.Necrosis")},
		{TEXT("VoidSplinter"), TEXT("Keyword.DoT.VoidSplinter")},
	};
	for (const FOne& One : Four)
	{
		if (!TestTrue(FString::Printf(TEXT("set-up: %s is a tag this build knows"), One.Tag), KitTag(One.Tag).IsValid()))
		{
			return false;
		}
	}

	const auto CarriesAnyOfTheFour = [&Four](const FKitFighter& Who)
	{
		for (const FOne& One : Four)
		{
			if (Who.Carries(KitTag(One.Tag)))
			{
				return true;
			}
		}
		return false;
	};

	// THE CONTROL FIRST: the same ring with no ailment.
	{
		FKitFighter Caster(World, FVector(0.0f, -20.0f * M, 0.0f));
		FKitFighter Target(World, FVector(2.0f * M, -20.0f * M, 0.0f));
		UCataclysmStrikeSkill* Plain = GrantKitSkill<UCataclysmStrikeSkill>(
			Caster, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"));
		const float Before = Target.Health();
		if (!TestNotNull(TEXT("set-up: the plain ring"), Plain)
			|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Plain))
			|| !TestTrue(TEXT("CONTROL: its blow landed, so the target is hurt"), Target.Health() < Before))
		{
			return false;
		}
		TestFalse(TEXT("CONTROL: and with no Ailment it carries none of the four"), CarriesAnyOfTheFour(Target));
	}

	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Four)); ++Index)
	{
		const FOne& One = Four[Index];
		FKitFighter Caster(World, FVector(0.0f, Index * 20.0f * M, 0.0f));
		FKitFighter Target(World, FVector(2.0f * M, Index * 20.0f * M, 0.0f));
		UCataclysmStrikeSkill* Stating = GrantKitSkill<UCataclysmStrikeSkill>(
			Caster, ECataclysmAbilitySlot::Heavy, FString::Printf(TEXT("Radius=4; Angle=360; Ailment=%s"), One.Written));
		if (!TestNotNull(FString::Printf(TEXT("set-up: the ring stating %s"), One.Written), Stating)
			|| !TestTrue(FString::Printf(TEXT("set-up: Ailment=%s parses"), One.Written), Stating->Params.bValid)
			|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Stating)))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("Ailment=%s lays %s"), One.Written, One.Tag), Target.Carries(KitTag(One.Tag)));
		for (const FOne& Other : Four)
		{
			if (&Other != &One)
			{
				TestFalse(FString::Printf(TEXT("and Ailment=%s lays no %s"), One.Written, Other.Tag),
						  Target.Carries(KitTag(Other.Tag)));
			}
		}
	}
	return true;
}

/**
 * A FIRED SHOT CARRIES ITS ROW'S AILMENT TO WHAT IT LANDS ON, AS IT CARRIES ITS BURN. A projectile with a speed never
 * passes through `HitTargets`: its contact is `ACataclysmProjectile::HitOne`. Two shots are fired at two targets and
 * flown by hand, one by a skill stating `Ailment=Poison` and one by the same skill without it. THE CONTROL is the
 * second: it lands, hurting its target, and poisons nothing.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitAilmentShotTest,
	"Cataclysm.StalkerKit.AFiredShotCarriesTheAilmentItsRowStates")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	const FGameplayTag Poison = KitTag(TEXT("Keyword.DoT.Poison"));

	FKitFighter Stating(World, FVector::ZeroVector);
	FKitFighter Poisoned(World, FVector(4.0f * M, 0.0f, 0.0f));
	FKitFighter Plain(World, FVector(0.0f, 30.0f * M, 0.0f));
	FKitFighter Control(World, FVector(4.0f * M, 30.0f * M, 0.0f));
	UCataclysmProjectileSkill* Flask = GrantKitSkill<UCataclysmProjectileSkill>(
		Stating, ECataclysmAbilitySlot::Special, TEXT("Range=12; Radius=0.5; Speed=1200; Ailment=Poison"));
	UCataclysmProjectileSkill* Unstated = GrantKitSkill<UCataclysmProjectileSkill>(
		Plain, ECataclysmAbilitySlot::Special, TEXT("Range=12; Radius=0.5; Speed=1200"));
	if (!TestTrue(TEXT("set-up: Keyword.DoT.Poison"), Poison.IsValid())
		|| !TestNotNull(TEXT("set-up: the skill stating Ailment=Poison"), Flask)
		|| !TestNotNull(TEXT("set-up: the same skill stating none"), Unstated))
	{
		return false;
	}

	const auto FlyAt = [](const FKitFighter& From, const FKitFighter& To, const UGameplayAbility* Skill)
	{
		ACataclysmProjectile* Shot = ACataclysmProjectile::Fire(
			From.Actor, From.Actor->GetActorLocation(), To.Actor->GetActorLocation() + FVector(6.0f * M, 0.0f, 0.0f),
			/*InRadiusCm=*/50.0f, /*InSpeed=*/1200.0f, /*InPierce=*/0, /*bInReturns=*/false,
			/*InDamagePercent=*/100.0f, FGameplayTagContainer(), /*bInBurns=*/false, /*InBodyMesh=*/nullptr,
			/*InFlightSeconds=*/0.0f, /*InCritChancePercent=*/0.0f, /*InSkillHealthCostPercent=*/-1.0f, Skill);
		for (int32 Step = 0; Shot && Step < 100 && Shot->Step(0.05f); ++Step)
		{
		}
		return Shot != nullptr;
	};

	const float ControlBefore = Control.Health();
	if (!TestTrue(TEXT("set-up: the plain shot is fired"), FlyAt(Plain, Control, Unstated))
		|| !TestTrue(TEXT("CONTROL: the plain shot landed, so its target is hurt"), Control.Health() < ControlBefore))
	{
		return false;
	}
	TestFalse(TEXT("CONTROL: and a skill stating no ailment poisons nothing"), Control.Carries(Poison));

	const float Before = Poisoned.Health();
	if (!TestTrue(TEXT("set-up: the stating shot is fired"), FlyAt(Stating, Poisoned, Flask)))
	{
		return false;
	}
	TestTrue(TEXT("the shot landed"), Poisoned.Health() < Before);
	TestTrue(TEXT("and laid the Poison its row states"), Poisoned.Carries(Poison));
	return true;
}

// ==========================================================================
// The potion exemption
// ==========================================================================

/**
 * A CORRUPTED STALKER'S NECROSIS SPARES A POTION, AND DENIES EVERY OTHER HEAL. The owner, 2026-11-17: "A Stalker's
 * Necrosis spares potions. It blocks regeneration and leech, but not potions." A player at 500 of 1000 carries a
 * Necrosis a Corrupted Stalker laid through `Ailment=Necrosis`'s route. It drinks a potion and the whole heal is paid:
 * 350 restored, and nothing owed to be dealt when the Necrosis ends. THE CONTROL on the same player under the same
 * Necrosis is a heal that is not a potion's, 100 through `TopUp` as regeneration and leech come: healed nothing, and
 * the 100 owed.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitNecrosisSparesAPotionTest,
	"Cataclysm.StalkerKit.AStalkersNecrosisSparesAPotionAndDeniesEveryOtherHeal")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();
	const FKitPlayer Player = SpawnKitPlayer(World, FVector::ZeroVector);
	ACataclysmCorruptedStalkerCharacter* Stalker = SpawnKitStalker(World, FVector(20.0f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: Keyword.DoT.Necrosis"), Necrosis.IsValid())
		|| !TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable())
		|| !TestNotNull(TEXT("set-up: a Corrupted Stalker"), Stalker))
	{
		return false;
	}
	HalfHealthWithNoRegeneration(Player);
	if (!TestTrue(TEXT("set-up: the Stalker lays a Necrosis on the player"), LayNecrosis(Stalker, Player))
		|| !TestTrue(TEXT("set-up: the player carries it"), Player.Carries(Necrosis))
		|| !TestTrue(TEXT("set-up: and the rule reads it as a Stalker's"),
					 UCataclysmRegeneration::NecrosisSparesPotions(*Player.AbilitySystem)))
	{
		return false;
	}

	const float Paid = DrinkAndPayTheWholeHeal(Player, *this);
	const float Expected = KitMaximum * UCataclysmPotions::HealShareOfMaximum;
	TestEqual(TEXT("the potion's whole heal is paid"), Paid, Expected, 0.01f);
	TestEqual(TEXT("and restored, under the Stalker's Necrosis"), Player.Health(), KitStart + Expected, 0.01f);
	TestEqual(TEXT("and none of it is owed, so none is dealt when the Necrosis ends"), KitDenied(Player), 0.0f, 0.001f);

	// THE CONTROL: THE SAME NECROSIS STILL DENIES A HEAL THAT IS NOT A POTION'S.
	// MEASURED AS A CHANGE, so this half says only what the regeneration did, whatever the potion above did.
	const float BeforeTheRegeneration = Player.Health();
	const float OwedBefore = KitDenied(Player);
	UCataclysmRegeneration::TopUp(*Player.AbilitySystem, UCataclysmVitalAttributeSet::GetHealthAttribute(),
								  UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100.0f);
	TestEqual(TEXT("CONTROL: a heal of 100 that is not a potion's restores nothing"),
			  Player.Health(), BeforeTheRegeneration, 0.01f);
	TestEqual(TEXT("CONTROL: and the 100 is owed"), KitDenied(Player) - OwedBefore, 100.0f, 0.01f);
	return true;
}

/**
 * A NECROSIS FROM ANY OTHER SOURCE STILL DENIES A POTION. The same player, the same Necrosis by the same route, laid by
 * an ordinary creature: the potion is drunk and restores nothing, and its 350 is owed, as before 2026-11-17. THE
 * CONTROL that the potion would have healed is the player before the Necrosis, where the rule answers false: no
 * Necrosis, nothing spared and nothing denied.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitOtherNecrosisDeniesAPotionTest,
	"Cataclysm.StalkerKit.ANecrosisFromAnyOtherSourceStillDeniesAPotion")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();
	const FKitPlayer Player = SpawnKitPlayer(World, FVector::ZeroVector);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
		ACataclysmEnemyCharacter::StaticClass(), FVector(20.0f * M, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	if (!TestTrue(TEXT("set-up: Keyword.DoT.Necrosis"), Necrosis.IsValid())
		|| !TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable())
		|| !TestNotNull(TEXT("set-up: an ordinary creature"), Creature))
	{
		return false;
	}
	Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	HalfHealthWithNoRegeneration(Player);
	TestFalse(TEXT("CONTROL: with no Necrosis, nothing is spared"),
			  UCataclysmRegeneration::NecrosisSparesPotions(*Player.AbilitySystem));

	if (!TestTrue(TEXT("set-up: the creature lays a Necrosis on the player"), LayNecrosis(Creature, Player))
		|| !TestTrue(TEXT("set-up: the player carries it"), Player.Carries(Necrosis)))
	{
		return false;
	}
	TestFalse(TEXT("the rule does not read it as a Stalker's"),
			  UCataclysmRegeneration::NecrosisSparesPotions(*Player.AbilitySystem));

	DrinkAndPayTheWholeHeal(Player, *this);
	const float Expected = KitMaximum * UCataclysmPotions::HealShareOfMaximum;
	TestEqual(TEXT("the potion restores nothing under another source's Necrosis"), Player.Health(), KitStart, 0.01f);
	TestEqual(TEXT("and its whole heal is owed, as before"), KitDenied(Player), Expected, 0.01f);
	return true;
}

// ==========================================================================
// [N3] DamageRollMin, DamageRollMax
// ==========================================================================

/**
 * A SKILL THAT ROLLS ITS DAMAGE DEALS THE FIGURE THIS USE DREW. Three casters each strike a target two metres in
 * front with a Heavy ring rolling 50 to 450, the draw pinned at the least, the middle and the most: 50, 250 and 450
 * of a weapon dealing 100. Before its first use the skill answers the middle, 250, and has drawn nothing. THE CONTROL
 * is the same ring with no roll under the same pin: the Heavy slot's 250, the pin moving nothing.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitDamageRollTest,
	"Cataclysm.StalkerKit.ARolledSkillDealsTheFigureThisUseDrew")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const auto Dealt = [this, World](float Pin, const TCHAR* Cell, float Y, float& OutBefore) -> float
	{
		const FKitPin Pinned(TEXT("Cataclysm.SkillDamageRoll"), Pin);
		FKitFighter Caster(World, FVector(0.0f, Y, 0.0f));
		FKitFighter Target(World, FVector(2.0f * M, Y, 0.0f));
		UCataclysmStrikeSkill* Strike =
			GrantKitSkill<UCataclysmStrikeSkill>(Caster, ECataclysmAbilitySlot::Heavy, Cell);
		if (!TestNotNull(TEXT("set-up: the ring"), Strike) || !TestNotNull(TEXT("set-up: the pin"), Pinned.Variable))
		{
			return -1.0f;
		}
		OutBefore = Strike->GetDamagePercent();
		const float HealthBefore = Target.Health();
		if (!TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Strike)))
		{
			return -1.0f;
		}
		return HealthBefore - Target.Health();
	};

	const TCHAR* Rolled = TEXT("Radius=4; Angle=360; DamageRollMin=50; DamageRollMax=450");
	float Unused = 0.0f;
	float BeforeAnyUse = 0.0f;
	TestEqual(TEXT("pinned at the least, the use deals 50% of 100"), Dealt(0.0f, Rolled, 0.0f, BeforeAnyUse), 50.0f, 0.01f);
	TestEqual(TEXT("before its first use the skill answers the middle of its range"), BeforeAnyUse, 250.0f, 0.01f);
	TestEqual(TEXT("pinned at the middle, 250"), Dealt(0.5f, Rolled, 20.0f * M, Unused), 250.0f, 0.01f);
	TestEqual(TEXT("pinned at the most, 450"), Dealt(1.0f, Rolled, 40.0f * M, Unused), 450.0f, 0.01f);
	TestEqual(TEXT("CONTROL: the same ring with no roll deals the Heavy slot's 250 under the same pin"),
			  Dealt(0.0f, TEXT("Radius=4; Angle=360"), 60.0f * M, Unused), 250.0f, 0.01f);
	return true;
}

// ==========================================================================
// [N4] EffectPick=Random
// ==========================================================================

/**
 * A ROW PICKING AT RANDOM LAYS ONE OF ITS NAMED EFFECTS, NOT ALL. A Heavy ring naming `Shred, Weaken` with
 * `EffectPick=Random`, the pick pinned to each place in turn: the first lays Shred and no Weaken, the second Weaken
 * and no Shred. Unpinned, each of four targets carries exactly one of the two. THE CONTROL is the same ring without
 * the pick: its target carries both, as Anathema's two curses land together.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitEffectPickTest,
	"Cataclysm.StalkerKit.APickLaysOneNamedEffectAndNotAll")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Shred = UCataclysmSkillShapes::StatusTagFor(TEXT("Shred"));
	const FGameplayTag Weaken = UCataclysmSkillShapes::StatusTagFor(TEXT("Weaken"));
	if (!TestTrue(TEXT("set-up: the Shred and Weaken tags"), Shred.IsValid() && Weaken.IsValid()))
	{
		return false;
	}

	// WHAT ONE STRIKE LEFT ON ITS TARGET, as {Shred, Weaken}.
	const auto Struck = [this, World, &Shred, &Weaken](float Pin, const TCHAR* Cell, float Y) -> TPair<bool, bool>
	{
		const FKitPin Pinned(TEXT("Cataclysm.SkillEffectPick"), Pin);
		FKitFighter Caster(World, FVector(0.0f, Y, 0.0f));
		FKitFighter Target(World, FVector(2.0f * M, Y, 0.0f));
		// A RESISTANCE TO CUT, so a Shred has something to take as well as a tag to grant.
		Target.Set(UCataclysmResistanceAttributeSet::GetChaosResistanceAttribute(), 40.0f);
		UCataclysmStrikeSkill* Strike = GrantKitSkill<UCataclysmStrikeSkill>(
			Caster, ECataclysmAbilitySlot::Heavy, Cell, TEXT("Element.Chaos"));
		if (!TestNotNull(TEXT("set-up: the ring"), Strike) || !TestNotNull(TEXT("set-up: the pin"), Pinned.Variable)
			|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Strike)))
		{
			return TPair<bool, bool>(false, false);
		}
		return TPair<bool, bool>(Target.Carries(Shred), Target.Carries(Weaken));
	};

	const TCHAR* Picking = TEXT("Radius=4; Angle=360; Effect=Shred, Weaken; EffectMagnitude=20; EffectPick=Random");
	const TPair<bool, bool> All = Struck(-1.0f, TEXT("Radius=4; Angle=360; Effect=Shred, Weaken; EffectMagnitude=20"),
										 0.0f);
	if (!TestTrue(TEXT("CONTROL: without the pick, both effects are laid"), All.Key && All.Value))
	{
		return false;
	}

	const TPair<bool, bool> First = Struck(0.0f, Picking, 20.0f * M);
	TestTrue(TEXT("pinned to the first place, the Shred is laid"), First.Key);
	TestFalse(TEXT("and the Weaken is not"), First.Value);

	const TPair<bool, bool> Second = Struck(1.0f, Picking, 40.0f * M);
	TestFalse(TEXT("pinned to the second place, the Shred is not laid"), Second.Key);
	TestTrue(TEXT("and the Weaken is"), Second.Value);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const TPair<bool, bool> Drawn = Struck(-1.0f, Picking, (60.0f + Index * 20.0f) * M);
		TestTrue(FString::Printf(TEXT("unpinned, target %d carries exactly one of the two"), Index),
				 Drawn.Key != Drawn.Value);
	}
	return true;
}

// ==========================================================================
// [N5] Effect=RandomBuff
// ==========================================================================

/**
 * A SELF BUFF WRITING `Effect=RandomBuff` HOLDS ONE BUFF FROM THE GAME'S OWN POOL, FOR ITS OWN DURATION. The draw is
 * pinned to Commander, place 3 of `RandomBuffTable`. A Support self buff of 8 seconds is cast: the caster holds
 * Commander, with 8 seconds left and not the pool's own 30. THE CONTROL is the same self buff without the effect, on
 * another caster: it holds no random buff.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitRandomBuffTest,
	"Cataclysm.StalkerKit.ASelfBuffDrawingARandomBuffHoldsOneForItsOwnDuration")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	constexpr int32 CommanderPlace = 3;
	const FKitPin Pinned(TEXT("Cataclysm.RandomBuffPick"), static_cast<float>(CommanderPlace));
	const TArray<UCataclysmAbilitySystemComponent::FRandomBuff>& Table =
		UCataclysmAbilitySystemComponent::RandomBuffTable();
	if (!TestNotNull(TEXT("set-up: the pin"), Pinned.Variable)
		|| !TestTrue(TEXT("set-up: the pool holds place 3"), Table.IsValidIndex(CommanderPlace))
		|| !TestEqual(TEXT("set-up: and it is Commander"), FName(Table[CommanderPlace].Name), FName(TEXT("Commander"))))
	{
		return false;
	}

	FKitFighter Plain(World, FVector(0.0f, 30.0f * M, 0.0f));
	UCataclysmSelfBuffSkill* Unstated =
		GrantKitSkill<UCataclysmSelfBuffSkill>(Plain, ECataclysmAbilitySlot::Support, TEXT("Duration=8"));
	if (!TestNotNull(TEXT("set-up: the plain self buff"), Unstated)
		|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Plain.AbilitySystem, Unstated)))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: a self buff naming no random buff holds none"),
			 Plain.AbilitySystem->HeldRandomBuff().IsNone());

	FKitFighter Caster(World, FVector::ZeroVector);
	UCataclysmSelfBuffSkill* Dice = GrantKitSkill<UCataclysmSelfBuffSkill>(
		Caster, ECataclysmAbilitySlot::Support, TEXT("Duration=8; Effect=RandomBuff"));
	if (!TestNotNull(TEXT("set-up: the self buff drawing a random buff"), Dice)
		|| !TestTrue(TEXT("set-up: Effect=RandomBuff parses"), Dice->Params.bValid)
		|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Dice)))
	{
		return false;
	}
	TestEqual(TEXT("the caster holds the buff drawn from the pool"), Caster.AbilitySystem->HeldRandomBuff(),
			  FName(TEXT("Commander")));
	TestEqual(TEXT("for the self buff's 8 seconds, not the pool's 30"),
			  Caster.AbilitySystem->HeldRandomBuffSecondsLeft(), 8.0f, 0.01f);
	return true;
}

// ==========================================================================
// The four uses the draft called unproven
// ==========================================================================

/**
 * (a) A TIMED AURA IN THE SUPPORT SLOT PAYS ONE USE'S COST, AND NOT A DRAIN A SECOND. Grave Shroud's shape: `Radius=4;
 * Duration=6; Interval=1; Ailment=Necrosis` in the Support slot. Activating it spends the slot's cost once; a pulse
 * spends nothing more. Its pulse deals the Support slot's 0% and still lays the Necrosis, the rule of #917. THE
 * CONTROL is the same aura with no Duration, a toggle, in the same slot on another caster: its pulse drains the
 * slot's cost a second.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitTimedSupportAuraTest,
	"Cataclysm.StalkerKit.ATimedAuraInTheSupportSlotPaysOneUseAndLaysItsAilment")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();

	FKitFighter Caster(World, FVector::ZeroVector);
	FKitFighter Inside(World, FVector(2.0f * M, 0.0f, 0.0f));
	UCataclysmAuraSkill* Shroud = GrantKitSkill<UCataclysmAuraSkill>(
		Caster, ECataclysmAbilitySlot::Support, TEXT("Radius=4; Duration=6; Interval=1; Ailment=Necrosis"));
	if (!TestTrue(TEXT("set-up: Keyword.DoT.Necrosis"), Necrosis.IsValid())
		|| !TestNotNull(TEXT("set-up: the timed aura"), Shroud))
	{
		return false;
	}
	const float Cost = Shroud->GetManaCost();
	if (!TestTrue(FString::Printf(TEXT("set-up: the Support slot costs mana (%.1f)"), Cost), Cost > 0.0f))
	{
		return false;
	}

	const float BeforeTheCast = Caster.Mana();
	if (!TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Shroud)))
	{
		return false;
	}
	TestEqual(TEXT("the cast spends one use's cost"), Caster.Mana(), BeforeTheCast - Cost, 0.01f);

	const float BeforeThePulse = Caster.Mana();
	const float InsideBefore = Inside.Health();
	Shroud->Pulse();
	TestEqual(TEXT("and a pulse of a timed aura spends nothing more"), Caster.Mana(), BeforeThePulse, 0.01f);
	TestEqual(TEXT("the pulse deals the Support slot's nothing"), Inside.Health(), InsideBefore, 0.01f);
	TestTrue(TEXT("and still lays the Necrosis its row states"), Inside.Carries(Necrosis));

	// THE CONTROL: THE SAME AURA AS A TOGGLE DRAINS ON EVERY PULSE.
	FKitFighter Toggler(World, FVector(0.0f, 30.0f * M, 0.0f));
	UCataclysmAuraSkill* Toggle = GrantKitSkill<UCataclysmAuraSkill>(
		Toggler, ECataclysmAbilitySlot::Support, TEXT("Radius=4; Interval=1"));
	if (!TestNotNull(TEXT("set-up: the toggle"), Toggle)
		|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Toggler.AbilitySystem, Toggle)))
	{
		return false;
	}
	const float ToggleBefore = Toggler.Mana();
	Toggle->Pulse();
	TestEqual(TEXT("CONTROL: a toggle's pulse drains the slot's cost a second"), Toggler.Mana(), ToggleBefore - Cost,
			  0.01f);
	return true;
}

/**
 * (b) AN AURA'S PULSE PULLS TOWARD ITS CASTER, AND A KNOCKBACK PUSHES AWAY. Undertow's shape: `Radius=8; Interval=1;
 * ForcedMovement=Pull; ForcedMovementDistance=1`, a target 5 metres along +X. One pulse brings it 1 metre in. A second
 * pulse inside the same five seconds brings it half as far, 0.5 metres: the design's diminishing-returns rule on every
 * displacement, which a pull each second never escapes. A knockback aura stating `Knockback=2` pushes a target 5
 * metres along +Y out to 7. THE CONTROL is an aura stating neither: its target does not move.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitAuraDisplacementTest,
	"Cataclysm.StalkerKit.AnAuraPulsePullsTowardItsCasterAndAKnockbackPushesAway")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	constexpr float Tolerance = 5.0f;

	// THE CONTROL FIRST.
	{
		FKitFighter Caster(World, FVector(0.0f, -40.0f * M, 0.0f));
		FKitFighter Target(World, FVector(5.0f * M, -40.0f * M, 0.0f));
		UCataclysmAuraSkill* Still = GrantKitSkill<UCataclysmAuraSkill>(
			Caster, ECataclysmAbilitySlot::Aura, TEXT("Radius=8; Interval=1"));
		const FVector Where = Target.Actor->GetActorLocation();
		if (!TestNotNull(TEXT("set-up: the plain aura"), Still)
			|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Still))
			|| !TestTrue(TEXT("set-up: its pulse finds the target"), Still->Pulse() == 1))
		{
			return false;
		}
		TestEqual(TEXT("CONTROL: an aura stating no displacement moves nothing"),
				  FVector::Dist(Target.Actor->GetActorLocation(), Where), 0.0f, Tolerance);
	}

	{
		FKitFighter Caster(World, FVector::ZeroVector);
		FKitFighter Target(World, FVector(5.0f * M, 0.0f, 0.0f));
		UCataclysmAuraSkill* Undertow = GrantKitSkill<UCataclysmAuraSkill>(
			Caster, ECataclysmAbilitySlot::Aura,
			TEXT("Radius=8; Interval=1; ForcedMovement=Pull; ForcedMovementDistance=1"));
		if (!TestNotNull(TEXT("set-up: the pulling aura"), Undertow)
			|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Undertow)))
		{
			return false;
		}
		Undertow->Pulse();
		TestEqual(TEXT("one pulse pulls the target 1 metre toward the caster"),
				  Target.Actor->GetActorLocation().X, 4.0f * M, Tolerance);
		Undertow->Pulse();
		TestEqual(TEXT("and a second inside five seconds half as far, 0.5 metres"),
				  Target.Actor->GetActorLocation().X, 3.5f * M, Tolerance);
	}

	{
		FKitFighter Caster(World, FVector(0.0f, 40.0f * M, 0.0f));
		FKitFighter Target(World, FVector(0.0f, 45.0f * M, 0.0f));
		UCataclysmAuraSkill* Aegis = GrantKitSkill<UCataclysmAuraSkill>(
			Caster, ECataclysmAbilitySlot::Aura, TEXT("Radius=8; Interval=1; Knockback=2"));
		if (!TestNotNull(TEXT("set-up: the shoving aura"), Aegis)
			|| !TestTrue(TEXT("set-up: it activates"), ActivateKitSkill(Caster.AbilitySystem, Aegis)))
		{
			return false;
		}
		Aegis->Pulse();
		TestEqual(TEXT("a pulse stating Knockback=2 pushes the target 2 metres away from the caster"),
				  Target.Actor->GetActorLocation().Y, 47.0f * M, Tolerance);
	}
	return true;
}

/**
 * (c) WITHERED TOUCH NAMED IN `Effect=` REACHES THE PLAYER AS THE SUCCUBUS'S DOES. Gnawing Want names it. A caster
 * strikes the player with a ring naming it: the player carries `Status.Debuff.WitheredTouch`, its maximum mana falls
 * by the row's 20%, from 1000 to 800, and its own blows keep 80%. THE CONTROL is the same ring naming nothing, which
 * lands first: the player is hurt, carries no Withered Touch, keeps 1000 and keeps all of its blows.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitWitheredTouchTest,
	"Cataclysm.StalkerKit.WitheredTouchNamedByASkillReachesThePlayer")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Withered = UCataclysmSkillShapes::StatusTagFor(TEXT("Withered Touch"));
	const FGameplayAttribute MaxMana = UCataclysmVitalAttributeSet::GetMaxManaAttribute();
	const FKitPlayer Player = SpawnKitPlayer(World, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: Status.Debuff.WitheredTouch"), Withered.IsValid())
		|| !TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
	Player.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
	Player.Set(MaxMana, 1000.0f);

	FKitFighter Plain(World, FVector(0.0f, 1.0f * M, 0.0f));
	FKitFighter Famine(World, FVector(0.0f, -1.0f * M, 0.0f));
	UCataclysmStrikeSkill* Unstated = GrantKitSkill<UCataclysmStrikeSkill>(
		Plain, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"), TEXT("Element.Famine"));
	UCataclysmStrikeSkill* Wither = GrantKitSkill<UCataclysmStrikeSkill>(
		Famine, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360; Effect=Withered Touch"),
		TEXT("Element.Famine"));
	if (!TestNotNull(TEXT("set-up: the plain ring"), Unstated) || !TestNotNull(TEXT("set-up: the withering ring"), Wither))
	{
		return false;
	}

	const float Before = Player.Health();
	if (!TestTrue(TEXT("set-up: the plain ring activates"), ActivateKitSkill(Plain.AbilitySystem, Unstated))
		|| !TestTrue(TEXT("CONTROL: its blow landed on the player"), Player.Health() < Before))
	{
		return false;
	}
	TestFalse(TEXT("CONTROL: a ring naming nothing lays no Withered Touch"), Player.Carries(Withered));
	TestEqual(TEXT("CONTROL: the player keeps 1000 maximum mana"), Player.Get(MaxMana), 1000.0f, 0.01f);
	TestEqual(TEXT("CONTROL: and all of its blows"), UCataclysmSkillEffects::KeptUnderWitheredTouch(Player.Character),
			  1.0f, 0.0001f);

	if (!TestTrue(TEXT("set-up: the withering ring activates"), ActivateKitSkill(Famine.AbilitySystem, Wither)))
	{
		return false;
	}
	TestTrue(TEXT("Withered Touch named in Effect= is laid on the player"), Player.Carries(Withered));
	TestEqual(TEXT("its maximum mana falls by the row's 20%"), Player.Get(MaxMana), 800.0f, 0.01f);
	TestEqual(TEXT("and its own blows keep 80%"), UCataclysmSkillEffects::KeptUnderWitheredTouch(Player.Character),
			  0.8f, 0.0001f);
	return true;
}

/**
 * (d) SHRED, WEAKEN AND STUN FROM ANOTHER CHARACTER'S SKILL LAND ON THE PLAYER. Three casters, each with a Death ring:
 * `Effect=Shred; EffectMagnitude=20` takes the player's Death resistance from 40 to 20 (the source's own element);
 * `Effect=Weaken; EffectMagnitude=40` takes its attack damage from 100 to 60; `StunSeconds=1; Effect=Stun` stuns it.
 * THE CONTROL is a fourth ring naming nothing, which lands first: the player is hurt, keeps 40 and 100, and is not
 * stunned.
 */
CATACLYSM_STALKER_KIT_TEST(FCataclysmStalkerKitDebuffsOnThePlayerTest,
	"Cataclysm.StalkerKit.ShredWeakenAndStunFromACreaturesSkillLandOnThePlayer")
{
	using namespace CataclysmStalkerKitTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayAttribute Death = UCataclysmDamageCalculation::ResistanceAttributeFor(FName(TEXT("Death")));
	const FGameplayAttribute Attack = UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	const FKitPlayer Player = SpawnKitPlayer(World, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable())
		|| !TestTrue(TEXT("set-up: the player holds a Death resistance"),
					 Death.IsValid() && Player.AbilitySystem->HasAttributeSetForAttribute(Death))
		|| !TestTrue(TEXT("set-up: and an attack damage"), Player.AbilitySystem->HasAttributeSetForAttribute(Attack)))
	{
		return false;
	}
	Player.Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
	Player.Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
	Player.Set(Death, 40.0f);
	Player.Set(Attack, 100.0f);

	const TCHAR* DeathTags = TEXT("Element.Death");
	FKitFighter Plain(World, FVector(0.0f, 1.5f * M, 0.0f));
	FKitFighter Shredder(World, FVector(0.0f, -1.5f * M, 0.0f));
	FKitFighter Weakener(World, FVector(0.0f, 0.5f * M, 0.0f));
	FKitFighter Stunner(World, FVector(0.0f, -0.5f * M, 0.0f));
	UCataclysmStrikeSkill* Unstated = GrantKitSkill<UCataclysmStrikeSkill>(
		Plain, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"), DeathTags);
	UCataclysmStrikeSkill* Shred = GrantKitSkill<UCataclysmStrikeSkill>(
		Shredder, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360; Effect=Shred; EffectMagnitude=20"), DeathTags);
	UCataclysmStrikeSkill* Weaken = GrantKitSkill<UCataclysmStrikeSkill>(
		Weakener, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360; Effect=Weaken; EffectMagnitude=40"), DeathTags);
	UCataclysmStrikeSkill* Stun = GrantKitSkill<UCataclysmStrikeSkill>(
		Stunner, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360; StunSeconds=1; Effect=Stun"), DeathTags);
	if (!TestNotNull(TEXT("set-up: the plain ring"), Unstated) || !TestNotNull(TEXT("set-up: the Shred"), Shred)
		|| !TestNotNull(TEXT("set-up: the Weaken"), Weaken) || !TestNotNull(TEXT("set-up: the stun"), Stun))
	{
		return false;
	}

	const float Before = Player.Health();
	if (!TestTrue(TEXT("set-up: the plain ring activates"), ActivateKitSkill(Plain.AbilitySystem, Unstated))
		|| !TestTrue(TEXT("CONTROL: its blow landed on the player"), Player.Health() < Before))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: the player keeps its Death resistance of 40"), Player.Get(Death), 40.0f, 0.01f);
	TestEqual(TEXT("CONTROL: and its attack damage of 100"), Player.Get(Attack), 100.0f, 0.01f);
	TestFalse(TEXT("CONTROL: and is not stunned"), UCataclysmSkillEffects::IsStunned(Player.Character));

	TestTrue(TEXT("set-up: the Shred activates"), ActivateKitSkill(Shredder.AbilitySystem, Shred));
	TestEqual(TEXT("a creature's Shred cuts the player's resistance to its own element, 40 to 20"),
			  Player.Get(Death), 20.0f, 0.01f);
	TestTrue(TEXT("set-up: the Weaken activates"), ActivateKitSkill(Weakener.AbilitySystem, Weaken));
	TestEqual(TEXT("a creature's Weaken cuts the player's attack damage by 40%, 100 to 60"),
			  Player.Get(Attack), 60.0f, 0.01f);
	TestTrue(TEXT("set-up: the stun activates"), ActivateKitSkill(Stunner.AbilitySystem, Stun));
	TestTrue(TEXT("a creature's stated stun stuns the player"), UCataclysmSkillEffects::IsStunned(Player.Character));
	return true;
}

#undef CATACLYSM_STALKER_KIT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
