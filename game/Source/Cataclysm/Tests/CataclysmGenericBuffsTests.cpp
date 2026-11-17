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
 * The owner, 2026-10-25: "we shouldn't have anything that says "increased X damage". It should all be generic for this
 * reason." And of Burning Wrath's self buff and Conflagration's aura on allies: "Make them generic". Ruled by the
 * coordinating session the same day (Q-P2, Q-B1): every self buff and shared buff `GrantIncrease` reaches, and an aura's
 * ally bonus, raises ALL damage, not only the skill's own element's.
 *
 * WHAT IS MEASURED. `UCataclysmSkillEffects::ModifiedDamage`, the function a skill's hit, a damage-over-time tick and a
 * minion's blow price their damage through, asked with a skill's tags: a blow of 100 from a Void strike
 * (`Element.Void, Type.Strike`) and from a Demonic one (`Element.Demonic, Type.Strike`). The buffs are written with
 * Burning Wrath's and Conflagration's own tags, so each carries `Element.Demonic`, and Void is the other element: it is
 * neither the buffs' element nor War. Before this change the Void blow was not raised at all.
 *
 * THE CHARACTERS ARE REAL: player pawns with their ability system on a player state, as the game wires them, so each is
 * on the players' side and each is the other's ally. Nobody is refreshed, so nobody holds a class line or any other
 * runtime modifier, and a blow of 100 comes to 100 until a buff is given; every figure is still taken against a control
 * read first. The one creature, which a self buff counts as burning, is on the monsters' side. Every character stands at
 * least 1 m from every other.
 *
 * NO WAIT ON A TICK. The shared buffs' step is called where the regeneration step would call it, and an aura's pulse is
 * called by hand; a test world is not ticked.
 */
namespace CataclysmGenericBuffsTest
{
	/** Centimetres in a metre, so a case can place a character in metres. */
	constexpr float GenericBuffsMetre = 100.0f;

	/**
	 * Burning Wrath's number: 4% more damage for each burning enemy within 15 m. Its two riders that set things alight,
	 * `Burn=1; BurnsAttackers=1`, are left out, as the neighbouring tests of the buff leave them out: nothing here
	 * strikes the caster.
	 */
	const TCHAR* const GenericBuffsBurningWrathRow = TEXT("Duration=10; Radius=15; MoreDamagePer=4; ScalingSource=Burning");

	/** Burning Wrath's tags, as the Weapon Skills sheet states them: it carries `Element.Demonic`. */
	const TCHAR* const GenericBuffsBurningWrathTags =
		TEXT("Item.Weapon.Greataxe, Element.Demonic, Type.Buff, Stat.Offense.Global, Keyword.DoT.Burn, Trigger.OnHit");

	/** Conflagration's row: allies inside its 10 m ring are given 8% increased damage. */
	const TCHAR* const GenericBuffsConflagrationRow =
		TEXT("Radius=10; Interval=1; Burn=1; Effect=Shred; EffectMagnitude=15; AllyIncreasedDamage=8");

	/** Conflagration's tags, as the sheet states them: it carries `Element.Demonic`. */
	const TCHAR* const GenericBuffsConflagrationTags =
		TEXT("Element.Demonic, Type.AOE.Aura, Keyword.DoT.Burn, Stat.Defense.Resist, Stat.Offense.Global, "
			 "Stat.Resource.Mana, Slot.Aura");

	/** A strike of the other element, Void, and one of the buffs' own, Demonic. */
	const TCHAR* const GenericBuffsVoidStrikeTags = TEXT("Element.Void, Type.Strike");
	const TCHAR* const GenericBuffsDemonicStrikeTags = TEXT("Element.Demonic, Type.Strike");

	FVector GenericBuffsAt(float XMetres, float YMetres)
	{
		return FVector(XMetres * GenericBuffsMetre, YMetres * GenericBuffsMetre, 0.0f);
	}

	UCataclysmAbilitySystemComponent* GenericBuffsSystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** A player pawn with its ability system on a player state, holding 10000 mana for every cast a case makes. */
	ACataclysmPlayerCharacter* GenericBuffsPlayerAt(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Pawn = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!State || !Pawn)
		{
			return nullptr;
		}
		Pawn->SetPlayerState(State);
		Pawn->OnRep_PlayerState();
		if (UCataclysmAbilitySystemComponent* System = GenericBuffsSystemOf(Pawn))
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 10000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 10000.0f);
		}
		return Pawn;
	}

	/** A creature of the monsters' side 10 m along -X, set alight by `Caster`, so a Burning Wrath cast after it counts one. */
	ACataclysmEnemyCharacter* GenericBuffsBurningCreature(UWorld* World, ACataclysmPlayerCharacter* Caster)
	{
		ACataclysmEnemyCharacter* Alight =
			World->SpawnActor<ACataclysmEnemyCharacter>(GenericBuffsAt(-10.0f, 0.0f), FRotator::ZeroRotator);
		if (Alight)
		{
			Alight->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Alight->SetHealth(100000.0f);
			UCataclysmSkillEffects::ApplyBurn(Caster, Alight, 100.0f,
				/*bScalesWithInstigator=*/true, /*bBurnIsDesigned=*/true);
		}
		return Alight;
	}

	/** Write this character's stat line, as worn rows would: a flat base per stat. */
	void GenericBuffsHold(const AActor* Actor, const TArray<TPair<const TCHAR*, float>>& Lines)
	{
		UCataclysmAbilitySystemComponent* System = GenericBuffsSystemOf(Actor);
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

	/** Grant Burning Wrath, with its own row and tags, in the Support slot and use it. */
	UCataclysmSelfBuffSkill* GenericBuffsCastBurningWrath(ACataclysmPlayerCharacter* Caster)
	{
		UCataclysmAbilitySystemComponent* System = GenericBuffsSystemOf(Caster);
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
		Skill->SkillName = TEXT("Burning Wrath");
		Skill->Params = UCataclysmSkillShapes::ParseParams(GenericBuffsBurningWrathRow);
		Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(GenericBuffsBurningWrathTags);
		System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		return Skill;
	}

	/** Grant Conflagration, with its own row and tags, in the Aura slot and switch it on. Null if it is not running. */
	UCataclysmAuraSkill* GenericBuffsRaiseConflagration(ACataclysmPlayerCharacter* Caster)
	{
		UCataclysmAbilitySystemComponent* System = GenericBuffsSystemOf(Caster);
		if (!System)
		{
			return nullptr;
		}
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			UCataclysmAuraSkill::StaticClass(), ECataclysmAbilitySlot::Aura, /*Level=*/100, Caster);
		FGameplayAbilitySpec* Spec = System->FindAbilitySpecFromHandle(Handle);
		UCataclysmAuraSkill* Aura = Spec ? Cast<UCataclysmAuraSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (!Aura)
		{
			return nullptr;
		}
		Aura->SkillName = TEXT("Conflagration");
		Aura->Params = UCataclysmSkillShapes::ParseParams(GenericBuffsConflagrationRow);
		Aura->SkillTags = UCataclysmSkillShapes::TagsFromCell(GenericBuffsConflagrationTags);
		System->TryActivateAbility(Aura->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
		return Aura->IsActive() ? Aura : nullptr;
	}

	/** What a blow of 100 from a skill carrying this tag cell comes to through this character's own modifiers. */
	float GenericBuffsBlowOf100(const AActor* Actor, const TCHAR* StrikeTags)
	{
		return UCataclysmSkillEffects::ModifiedDamage(
			GenericBuffsSystemOf(Actor), 100.0f, UCataclysmSkillShapes::TagsFromCell(StrikeTags));
	}

	/** True when this character carries exactly one runtime modifier and it requires no tag at all. */
	bool GenericBuffsCarriesOneUnscopedModifier(const AActor* Actor)
	{
		const UCataclysmAbilitySystemComponent* Its = GenericBuffsSystemOf(Actor);
		return Its && Its->GetStatModifiers().Num() == 1 && Its->GetStatModifiers()[0].RequiredTags.IsEmpty();
	}
}

#define CATACLYSM_GENERIC_BUFFS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString&)

/**
 * A self buff, through `UCataclysmSelfBuffSkill::GrantIncrease`. Burning Wrath, a Demonic skill, cast with one enemy
 * burning, raises its caster's Void blow by its 4% More, as it raises a Demonic one; a character without the buff, the
 * control, is not raised at all. Before this change the Void blow stayed at 100.
 *
 * STANDING: the caster at the origin, the character without the buff 20 m along Y, the burning creature 10 m along -X.
 */
CATACLYSM_GENERIC_BUFFS_TEST(FCataclysmGenericSelfBuffTest,
	"Cataclysm.GenericBuffs.ASelfBuffOfOneElementRaisesABlowOfAnother")
{
	using namespace CataclysmGenericBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Caster = GenericBuffsPlayerAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Without = GenericBuffsPlayerAt(World, GenericBuffsAt(0.0f, 20.0f));
	if (!TestNotNull(TEXT("set-up: a caster"), Caster) || !TestNotNull(TEXT("set-up: a character without the buff"), Without)
		|| !TestNotNull(TEXT("set-up: something burning"), GenericBuffsBurningCreature(World, Caster)))
	{
		return false;
	}

	// THE CONTROL, READ FIRST.
	const float WithoutVoid = GenericBuffsBlowOf100(Without, GenericBuffsVoidStrikeTags);
	const float WithoutDemonic = GenericBuffsBlowOf100(Without, GenericBuffsDemonicStrikeTags);
	TestEqual(TEXT("control: before the cast, the caster's Void blow of 100 comes to what the other character's does"),
		GenericBuffsBlowOf100(Caster, GenericBuffsVoidStrikeTags), WithoutVoid, 0.01f);

	const UCataclysmSelfBuffSkill* Wrath = GenericBuffsCastBurningWrath(Caster);
	if (!TestTrue(TEXT("set-up: Burning Wrath runs"), Wrath && Wrath->IsActive())
		|| !TestEqual(TEXT("set-up: and grants 4% More for the one burning enemy"), Wrath->GrantedIncrease, 4.0f, 0.001f))
	{
		return false;
	}

	TestFalse(TEXT("the buff's increase is scoped to no tag, though the skill carries Element.Demonic"),
		Wrath->GrantedScope.IsValid());
	TestTrue(TEXT("and the one modifier on its caster requires no tag"), GenericBuffsCarriesOneUnscopedModifier(Caster));
	TestEqual(TEXT("the caster's Void blow of 100 is 4% more than the control's (it was not raised before)"),
		GenericBuffsBlowOf100(Caster, GenericBuffsVoidStrikeTags), WithoutVoid * 1.04f, 0.01f);
	TestEqual(TEXT("and its Demonic blow of 100 is 4% more too, as before"),
		GenericBuffsBlowOf100(Caster, GenericBuffsDemonicStrikeTags), WithoutDemonic * 1.04f, 0.01f);
	TestEqual(TEXT("control: the character without the buff still deals its Void blow unraised"),
		GenericBuffsBlowOf100(Without, GenericBuffsVoidStrikeTags), WithoutVoid, 0.01f);
	return true;
}

/**
 * An aura's ally bonus, through `UCataclysmAuraSkill::HelpAlliesInside`. Conflagration, a Demonic aura, raises an
 * ally's Void blow inside its ring by its 8% increased, as it raises a Demonic one. The control is the same ally's blows
 * before the first pulse. Before this change the Void blow stayed at 100.
 *
 * STANDING: the caster at the origin, the ally 3 m along X, inside the 10 m ring. No creature.
 */
CATACLYSM_GENERIC_BUFFS_TEST(FCataclysmGenericAuraAllyBonusTest,
	"Cataclysm.GenericBuffs.AnAurasAllyBonusRaisesAnAllysBlowOfAnotherElement")
{
	using namespace CataclysmGenericBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Caster = GenericBuffsPlayerAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Ally = GenericBuffsPlayerAt(World, GenericBuffsAt(3.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a caster"), Caster) || !TestNotNull(TEXT("set-up: an ally"), Ally))
	{
		return false;
	}
	UCataclysmAuraSkill* Aura = GenericBuffsRaiseConflagration(Caster);
	if (!TestNotNull(TEXT("set-up: Conflagration runs"), Aura))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: before the first pulse the ally carries nothing.
	const float BeforeVoid = GenericBuffsBlowOf100(Ally, GenericBuffsVoidStrikeTags);
	const float BeforeDemonic = GenericBuffsBlowOf100(Ally, GenericBuffsDemonicStrikeTags);
	TestEqual(TEXT("control: before the first pulse the aura helps nobody"), Aura->AlliesHelped(), 0);

	Aura->Pulse();
	if (!TestTrue(TEXT("set-up: the pulse helps the ally 3 m off"), Aura->IsHelping(Ally)))
	{
		return false;
	}
	TestTrue(TEXT("the ally carries one modifier and it requires no tag, though the aura carries Element.Demonic"),
		GenericBuffsCarriesOneUnscopedModifier(Ally));
	TestEqual(TEXT("the ally's Void blow of 100 is 8% increased against the control (it was not raised before)"),
		GenericBuffsBlowOf100(Ally, GenericBuffsVoidStrikeTags), BeforeVoid * 1.08f, 0.01f);
	TestEqual(TEXT("and its Demonic blow of 100 is 8% increased too, as before"),
		GenericBuffsBlowOf100(Ally, GenericBuffsDemonicStrikeTags), BeforeDemonic * 1.08f, 0.01f);
	return true;
}

/**
 * A shared buff, through `UCataclysmSharedBuffs::Step`. "Applying a buff to yourself also applies it to all allies
 * within 8 meters", held as `self_buff_shared_within_metres` at 8: the copy of Burning Wrath's 4% More an ally 3 m off
 * is given raises the ally's Void blow, as it raises a Demonic one. The control is the same ally's blows before the
 * step. Before this change the copy carried the buff's Demonic scope and the Void blow stayed at 100.
 *
 * STANDING: the wearer at the origin, the ally 3 m along X, the burning creature 10 m along -X, 13 m from the ally.
 */
CATACLYSM_GENERIC_BUFFS_TEST(FCataclysmGenericSharedCopyTest,
	"Cataclysm.GenericBuffs.ASharedBuffsCopyRaisesAnAllysBlowOfAnotherElement")
{
	using namespace CataclysmGenericBuffsTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Wearer = GenericBuffsPlayerAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Ally = GenericBuffsPlayerAt(World, GenericBuffsAt(3.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: an ally"), Ally)
		|| !TestNotNull(TEXT("set-up: something burning"), GenericBuffsBurningCreature(World, Wearer)))
	{
		return false;
	}
	GenericBuffsHold(Wearer, {{UCataclysmAbilitySystemComponent::SelfBuffSharedWithinMetresStat, 8.0f}});
	const UCataclysmSelfBuffSkill* Wrath = GenericBuffsCastBurningWrath(Wearer);
	if (!TestTrue(TEXT("set-up: Burning Wrath runs"), Wrath && Wrath->IsActive())
		|| !TestEqual(TEXT("set-up: and grants its caster 4% More"), Wrath->GrantedIncrease, 4.0f, 0.001f))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: before the step the ally carries nothing.
	const float BeforeVoid = GenericBuffsBlowOf100(Ally, GenericBuffsVoidStrikeTags);
	const float BeforeDemonic = GenericBuffsBlowOf100(Ally, GenericBuffsDemonicStrikeTags);
	TestEqual(TEXT("control: before the step the wearer holds no copy"), GenericBuffsSystemOf(Wearer)->SharedBuffCopies.Num(), 0);

	UCataclysmSharedBuffs::Step(Wearer);
	TestTrue(TEXT("the ally carries one copy and it requires no tag, though the buff carries Element.Demonic"),
		GenericBuffsCarriesOneUnscopedModifier(Ally));
	TestEqual(TEXT("the ally's Void blow of 100 is 4% more against the control (it was not raised before)"),
		GenericBuffsBlowOf100(Ally, GenericBuffsVoidStrikeTags), BeforeVoid * 1.04f, 0.01f);
	TestEqual(TEXT("and its Demonic blow of 100 is 4% more too, as before"),
		GenericBuffsBlowOf100(Ally, GenericBuffsDemonicStrikeTags), BeforeDemonic * 1.04f, 0.01f);
	return true;
}

#undef CATACLYSM_GENERIC_BUFFS_TEST

#endif // WITH_AUTOMATION_TESTS
