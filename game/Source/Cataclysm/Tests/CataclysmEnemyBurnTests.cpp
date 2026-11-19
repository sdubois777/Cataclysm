// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmConsumptionDoubleCharacter.h"
#include "Character/CataclysmCorruptedStalkerCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Damage over time a creature applies to the player, sized from what applies it. Issue #2749, the enemy burn slice,
 * ruled by the coordinating session on 2026-11-19: a floor creature's Hellfire Aura burns for 9% of its carrier's
 * weapon damage a tick, where it burned for the status table's flat 25; a Corrupted Stalker's kit damage over time is
 * the table figure times the damage multiplier its hits carry; the corrupted double carries no multiplier, so its stays
 * the player's own figure. And the question the project owner's play log of 2026-11-19 left: what cut its 25 ticks to
 * about 20.5.
 *
 * REGISTERED UNDER `Cataclysm.EnemyBurn.`, with the trailing dot when narrowing a run.
 */
namespace CataclysmEnemyBurnTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	constexpr float BurnMetre = 100.0f;

	void BurnSet(const AActor* Actor, const FGameplayAttribute& Attribute, float Value)
	{
		UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		if (System != nullptr && System->HasAttributeSetForAttribute(Attribute))
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}
	}

	float BurnRead(const AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System && System->HasAttributeSetForAttribute(Attribute) ? System->GetNumericAttribute(Attribute) : -1.0f;
	}

	/** A player pawn with its ability system on a player state, with a lot of health and no energy shield. */
	ACataclysmPlayerCharacter* SpawnBurnPlayer(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (State == nullptr || Player == nullptr)
		{
			return nullptr;
		}
		Player->SetPlayerState(State);
		Player->OnRep_PlayerState();
		BurnSet(Player, Vital::GetMaxHealthAttribute(), 10000.0f);
		BurnSet(Player, Vital::GetHealthAttribute(), 10000.0f);
		BurnSet(Player, Vital::GetEnergyShieldAttribute(), 0.0f);
		return Player;
	}

	/** A creature of the monsters' side, with this attack damage written onto its attribute. */
	ACataclysmEnemyCharacter* SpawnBurnCreature(UWorld* World, const FVector& Where, float AttackDamage)
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Creature == nullptr)
		{
			return nullptr;
		}
		Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Creature->SetHealth(500.0f);
		BurnSet(Creature, Combat::GetAttackDamageAttribute(), AttackDamage);
		return Creature;
	}

	/**
	 * What one tick of the one running effect granting `Granted` on `Actor` deals, before defences, or -1 when there is
	 * not exactly one or it has more than one modifier. A copy of `CataclysmApplyStatusTest::PerTickOn`.
	 */
	float BurnPerTickOn(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		if (!System)
		{
			return -1.0f;
		}
		const TArray<FActiveGameplayEffectHandle> Found = System->GetActiveEffects(
			FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted)));
		const FActiveGameplayEffect* Active = Found.Num() == 1 ? System->GetActiveGameplayEffect(Found[0]) : nullptr;
		return Active && Active->Spec.Def && Active->Spec.Def->Modifiers.Num() == 1
			? Active->Spec.GetModifierMagnitude(0)
			: -1.0f;
	}

	/** The authored Demonic Stalker entry, or an empty one. */
	FCataclysmStalkerEntry BurnDemonicEntry()
	{
		for (const FCataclysmStalkerEntry& Entry : ACataclysmCorruptedStalkerCharacter::Entries())
		{
			if (Entry.DamageType == FName(TEXT("Demonic")))
			{
				return Entry;
			}
		}
		return FCataclysmStalkerEntry();
	}
}

#define CATACLYSM_ENEMY_BURN_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * A HELLFIRE AURA'S TICK IS 9% OF ITS CARRIER'S WEAPON DAMAGE. A carrier of 200 attack damage, a player 2 m away, and
 * one second of the aura's step: the burn on the player deals 18 a tick before defences. THE CONTROLS: it is not the
 * table's 25, and a carrier with no weapon damage burns nobody.
 */
CATACLYSM_ENEMY_BURN_TEST(FCataclysmEnemyBurnAuraTest,
	"Cataclysm.EnemyBurn.AHellfireAurasTickIsNinePercentOfItsCarriersWeaponDamage")
{
	using namespace CataclysmEnemyBurnTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnBurnPlayer(World, FVector(2.0f * BurnMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Carrier = SpawnBurnCreature(World, FVector::ZeroVector, 200.0f);
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a carrier"), Carrier)
		|| !TestTrue(TEXT("set-up: the burn tag"), Burn.IsValid()))
	{
		return false;
	}
	Carrier->ModifierRows.Add(FName(UCataclysmEnemyModifiers::HellfireAuraRow));
	BurnSet(Carrier, Combat::GetAttackDamageAttribute(), 200.0f);
	TestEqual(TEXT("set-up: the carrier's weapon damage is 200"),
			  UCataclysmSkillEffects::WeaponDamageOf(UCataclysmTargeting::AbilitySystemOf(Carrier)), 200.0f, 0.01f);

	int32 Touched = 0;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Touched += UCataclysmEnemyModifiers::AuraStep(Carrier, 0.25f);
	}
	TestEqual(TEXT("one second of the aura's step burns the player once"), Touched, 1);
	const float PerTick = BurnPerTickOn(Player, Burn);
	TestEqual(TEXT("the burn deals 9% of the carrier's 200 a tick, 18"), PerTick,
			  200.0f * UCataclysmEnemyModifiers::HellfireAuraBurnShareOfWeaponDamage, 0.01f);
	TestEqual(TEXT("typed: 18"), PerTick, 18.0f, 0.01f);
	TestTrue(FString::Printf(TEXT("CONTROL: and not the table's 25 (%.2f)"), PerTick), FMath::Abs(PerTick - 25.0f) > 1.0f);

	// A CARRIER WITH NO WEAPON DAMAGE BURNS NOBODY: nought a tick is no burn.
	ACataclysmPlayerCharacter* Second = SpawnBurnPlayer(World, FVector(0.0f, 2.0f * BurnMetre, 0.0f));
	ACataclysmEnemyCharacter* Unarmed = SpawnBurnCreature(World, FVector(0.0f, 30.0f * BurnMetre, 0.0f), 0.0f);
	if (!TestNotNull(TEXT("set-up: a second player"), Second) || !TestNotNull(TEXT("set-up: an unarmed carrier"), Unarmed))
	{
		return false;
	}
	Second->SetActorLocation(Unarmed->GetActorLocation() + FVector(2.0f * BurnMetre, 0.0f, 0.0f));
	Unarmed->ModifierRows.Add(FName(UCataclysmEnemyModifiers::HellfireAuraRow));
	int32 TouchedUnarmed = 0;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		TouchedUnarmed += UCataclysmEnemyModifiers::AuraStep(Unarmed, 0.25f);
	}
	TestEqual(TEXT("a carrier with no weapon damage burns nobody"), TouchedUnarmed, 0);
	return true;
}

/**
 * WHAT CUT THE OWNER'S 25 TICKS TO ABOUT 20.5? The table's burn, 25 a tick, from a creature with no damage over time stats
 * of its own, on a level 2 Ravager wearing nothing: the class line alone, with its regeneration held at nought so the
 * clock heals nothing. Every tick that lands is read off the announcement of the blow. REGISTERED BEFORE THE RUN: it
 * arrives as 25 times what the Ravager's armour leaves times what its flat damage reduction leaves, 23.40, if armour
 * meets a tick, or as 25 times the reduction's share alone, 24.24, if it does not; the test computes both from the
 * attributes it reads back and passes on either. If the arriving tick is neither, that is a third figure, and the run
 * says so. Whichever it is, what the owner's character lost beyond it came from something it carried that the log does
 * not record.
 */
CATACLYSM_ENEMY_BURN_TEST(FCataclysmEnemyBurnArrivingTickTest,
	"Cataclysm.EnemyBurn.ATableBurnFromACreatureArrivesAtWhatTheDefendersStepsLeave")
{
	using namespace CataclysmEnemyBurnTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnBurnPlayer(World, FVector(2.0f * BurnMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Burner = SpawnBurnCreature(World, FVector::ZeroVector, 0.0f);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	UAbilitySystemComponent* PlayersSystem = UCataclysmTargeting::AbilitySystemOf(Player);
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a creature"), Burner)
		|| !TestNotNull(TEXT("set-up: the announcer"), Events) || !TestNotNull(TEXT("set-up: its system"), PlayersSystem))
	{
		return false;
	}

	// A LEVEL 2 RAVAGER WEARING NOTHING, as the owner's character was at its death, its regeneration held at nought.
	TestTrue(TEXT("set-up: the Ravager's class line applies at level 2"),
			 UCataclysmPlayerClassStats::ApplyTo(PlayersSystem, UCataclysmPlayerClassStats::LoadTable(),
												 TEXT("Ravager"), 2) > 0);
	BurnSet(Player, Vital::GetHealthRegenAttribute(), 0.0f);
	BurnSet(Player, Vital::GetEnergyShieldAttribute(), 0.0f);
	const float Armour = BurnRead(Player, Combat::GetArmorAttribute());
	const float Reduction = BurnRead(Player, Combat::GetDamageReductionAttribute());
	const float Resistance = BurnRead(Player, UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute());
	const float ArmourKeeps = 1.0f - UCataclysmDamageCalculation::ArmorReduction(Armour, 1) / 100.0f;
	const float ReductionKeeps = 1.0f - UCataclysmDamageCalculation::EffectiveDamageReduction(Reduction) / 100.0f;
	const float IfArmourMeetsTicks = 25.0f * ArmourKeeps * ReductionKeeps;
	const float IfNot = 25.0f * ReductionKeeps;

	TArray<float> Ticks;
	const FDelegateHandle Listening = Events->OnHit.AddLambda([&Ticks, Player](const FCataclysmHitNotice& Notice)
	{
		if (Notice.Target == Player && Notice.bDamageOverTime)
		{
			Ticks.Add(Notice.Landed);
		}
	});
	ON_SCOPE_EXIT { Events->OnHit.Remove(Listening); };

	if (!TestTrue(TEXT("set-up: the table's burn is laid on the player"),
				  UCataclysmSkillEffects::ApplyBurn(Burner, Player, /*HitDamage=*/0.0f, /*bScalesWithInstigator=*/true,
													/*bBurnIsDesigned=*/true)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: it states 25 a tick before defences"), BurnPerTickOn(Player, UCataclysmSkillEffects::BurnTag()),
			  25.0f, 0.01f);
	CataclysmTestWorld::RunClock(World, 2.1f);

	AddInfo(FString::Printf(TEXT("ENEMY BURN TICK: armour %.2f keeps %.4f; flat reduction %.2f keeps %.4f; all-resistance "
								 "%.2f; %d ticks landed, the first %.4f; 23.40 is predicted if armour meets a tick, 24.24 "
								 "if not (computed %.4f and %.4f)"),
							Armour, ArmourKeeps, Reduction, ReductionKeeps, Resistance, Ticks.Num(),
							Ticks.Num() > 0 ? Ticks[0] : -1.0f, IfArmourMeetsTicks, IfNot));
	if (!TestTrue(FString::Printf(TEXT("set-up: at least one tick landed in 2.1 seconds (%d)"), Ticks.Num()),
				  Ticks.Num() > 0))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("the tick arrives at what the defender's own steps leave, %.4f, either %.4f or %.4f"),
							 Ticks[0], IfArmourMeetsTicks, IfNot),
			 FMath::IsNearlyEqual(Ticks[0], IfArmourMeetsTicks, 0.02f) || FMath::IsNearlyEqual(Ticks[0], IfNot, 0.02f));
	TestTrue(FString::Printf(TEXT("and it is not the log's 20.5 (%.4f): that came from something the character carried"),
							 Ticks[0]),
			 Ticks[0] > 22.0f);
	return true;
}

/**
 * A CORRUPTED STALKER'S DAMAGE OVER TIME IS THE TABLE FIGURE TIMES ITS DAMAGE MULTIPLIER. A Stalker wearing the Demonic
 * entry at tier 1, whose hits carry 0.06, lays 25 a tick with its own damage over time stats left out (so only the
 * multiplier is measured): the burn deals 1.5 a tick. THE CONTROL: the same burn from a plain creature deals 25, and
 * replaces the Stalker's as the stronger. A share of the target's health (Void Splinter) takes no multiplier. And a
 * Stalker worn on a Bulwark keeps the Bulwark's multiplier for its damage over time, as its hits do since the ring slice.
 */
CATACLYSM_ENEMY_BURN_TEST(FCataclysmEnemyBurnStalkerTest,
	"Cataclysm.EnemyBurn.ACorruptedStalkersDamageOverTimeIsTheTableFigureTimesItsDamageMultiplier")
{
	using namespace CataclysmEnemyBurnTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnBurnPlayer(World, FVector(2.0f * BurnMetre, 0.0f, 0.0f));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmCorruptedStalkerCharacter* Stalker = World->SpawnActor<ACataclysmCorruptedStalkerCharacter>(
		ACataclysmCorruptedStalkerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	ACataclysmEnemyCharacter* Plain = SpawnBurnCreature(World, FVector(0.0f, 30.0f * BurnMetre, 0.0f), 50.0f);
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a Stalker"), Stalker)
		|| !TestNotNull(TEXT("set-up: a plain creature"), Plain))
	{
		return false;
	}
	Stalker->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	FRandomStream Stream(7);
	if (!TestTrue(TEXT("set-up: the Stalker wears the Demonic entry at tier 1"),
				  Stalker->WearTheBuild(BurnDemonicEntry(), 1, Stream)))
	{
		return false;
	}
	const float Multiplier = UCataclysmDungeonModifierEffects::CorruptedStalkerDamageMultiplier(1);
	TestEqual(TEXT("its damage over time multiplier is its hits' at tier 1, 0.06"), Stalker->DamageOverTimeMultiplier(),
			  0.06f, 0.0001f);

	TestTrue(TEXT("set-up: the Stalker lays the table's burn"),
			 UCataclysmSkillEffects::ApplyDamageOverTime(Stalker, Player, 25.0f, 4.0f, Burn,
														 /*bScalesWithInstigator=*/false));
	TestEqual(TEXT("it deals 25 times 0.06 a tick, 1.5"), BurnPerTickOn(Player, Burn), 25.0f * Multiplier, 0.001f);

	TestTrue(TEXT("set-up: a plain creature lays the same burn"),
			 UCataclysmSkillEffects::ApplyDamageOverTime(Plain, Player, 25.0f, 4.0f, Burn,
														 /*bScalesWithInstigator=*/false));
	TestEqual(TEXT("CONTROL: a plain creature's deals the table's 25, and replaces the weaker"), BurnPerTickOn(Player, Burn),
			  25.0f, 0.001f);

	// A SHARE OF THE TARGET'S HEALTH TAKES NO MULTIPLIER: the coordinating session, 2026-11-19, "EXEMPT
	// percentage-of-health effects from the Stalker's multiplier ... A share of the target's health already sizes
	// itself to the target". The Stalker's Void Splinter, 1% of current health a tick, on an undefended player of
	// 10,000 health: its first tick is 1% of the health it met, not 0.06% of it.
	ACataclysmPlayerCharacter* Splintered = SpawnBurnPlayer(World, FVector(0.0f, 2.0f * BurnMetre, 0.0f));
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	const FGameplayTag Splinter = UCataclysmDebuffs::VoidSplinterTag();
	if (!TestNotNull(TEXT("set-up: a second player"), Splintered) || !TestNotNull(TEXT("set-up: the announcer"), Events)
		|| !TestTrue(TEXT("set-up: the Void Splinter tag"), Splinter.IsValid()))
	{
		return false;
	}
	BurnSet(Splintered, Combat::GetArmorAttribute(), 0.0f);
	BurnSet(Splintered, Combat::GetDamageReductionAttribute(), 0.0f);
	BurnSet(Splintered, Vital::GetHealthRegenAttribute(), 0.0f);
	// WHAT EACH TICK SENT, WITH THE HEALTH IT MET, READ WHEN IT IS ANNOUNCED. In window burn-w1 (2026-11-19) this
	// compared what the first tick LANDED, 98.33, with 1% of the health read before the clock ran, 100. Two things lie
	// between those: the Stalker is a live creature and struck this player while the clock ran (its Pyroclasm met it at
	// 10,000 and at 9,916), and a share-of-health tick passes the defender's steps after it is sized
	// (`UCataclysmVitalAttributeSet::PostGameplayEffectExecute`). The rule this checks is neither: it is that the share
	// is sized from the health the tick met and takes no multiplier. So it reads what the tick SENT, the figure the
	// defender's calculation was given (`FCataclysmHitNotice::Sent`, set from the share times the health at that
	// moment), and the health read in this listener, which is the health the tick met:
	// `UCataclysmCombatEvents::NoteBlow` announces a blow before `UCataclysmVitalAttributeSet` writes it to health, as
	// `ACataclysmPlayerCharacter::NoteBlowTakenInTheHitLog` relies on. ONLY THE VOID SPLINTER'S TICKS, by the tag the
	// effect grants, so a burn the Stalker lays on this player is not counted as one. What landed is reported beside
	// them and not asserted, so the player's defences are neither asserted nor assumed away.
	TArray<float> SplinterTicks;
	TArray<float> LandedEachTick;
	TArray<float> HealthEachTickMet;
	const FDelegateHandle Listening = Events->OnHit.AddLambda(
		[&SplinterTicks, &LandedEachTick, &HealthEachTickMet, Splintered, Splinter](const FCataclysmHitNotice& Notice)
		{
			if (Notice.Target == Splintered && Notice.bDamageOverTime && Notice.GrantedTags != nullptr
				&& Notice.GrantedTags->HasTag(Splinter))
			{
				SplinterTicks.Add(Notice.Sent);
				LandedEachTick.Add(Notice.Landed);
				HealthEachTickMet.Add(BurnRead(Splintered, Vital::GetHealthAttribute()));
			}
		});
	ON_SCOPE_EXIT { Events->OnHit.Remove(Listening); };
	TestTrue(TEXT("set-up: the Stalker lays a Void Splinter of 1% a tick"),
			 UCataclysmSkillEffects::ApplyShareOfHealthOverTime(Stalker, Splintered, 0.01f, 4.0f, Splinter,
																/*bScalesWithInstigator=*/false));
	CataclysmTestWorld::RunClock(World, 1.1f);
	if (TestTrue(FString::Printf(TEXT("set-up: a Void Splinter tick landed (%d)"), SplinterTicks.Num()),
				 SplinterTicks.Num() > 0 && HealthEachTickMet.Num() == SplinterTicks.Num()))
	{
		AddInfo(FString::Printf(TEXT("the first Void Splinter tick: sent %.3f, landed %.3f, on health %.2f"),
								SplinterTicks[0], LandedEachTick[0], HealthEachTickMet[0]));
		TestTrue(FString::Printf(TEXT("set-up: the tick met a player with health (%.1f)"), HealthEachTickMet[0]),
				 HealthEachTickMet[0] > 1000.0f);
		TestEqual(TEXT("the Stalker's Void Splinter tick still sends 1% of the target's current health"),
				  SplinterTicks[0], HealthEachTickMet[0] * 0.01f, 0.05f);
	}

	// AND ON ITS FLOOR'S RING, as its hits are since the ring slice: a Stalker worn on a Bulwark keeps the Bulwark's
	// multiplier, 0.132 at tier 1, for its damage over time. The ring's parameter defaults to an Outpost, so a call
	// that leaves it out still compiles; the rebase onto the ring slice produced exactly that, and this fails on it.
	ACataclysmCorruptedStalkerCharacter* OnABulwark = World->SpawnActor<ACataclysmCorruptedStalkerCharacter>(
		ACataclysmCorruptedStalkerCharacter::StaticClass(), FVector(0.0f, -30.0f * BurnMetre, 0.0f), FRotator::ZeroRotator,
		Params);
	FRandomStream BulwarkStream(7);
	if (TestNotNull(TEXT("set-up: a second Stalker"), OnABulwark)
		&& TestTrue(TEXT("set-up: it wears the Demonic entry at tier 1 on a Bulwark"),
					OnABulwark->WearTheBuild(BurnDemonicEntry(), 1, BulwarkStream, ECataclysmCityTier::Bulwark)))
	{
		const float OnTheBulwark =
			UCataclysmDungeonModifierEffects::CorruptedStalkerDamageMultiplier(1, ECataclysmCityTier::Bulwark);
		TestEqual(TEXT("a Bulwark Stalker's damage over time multiplier is its hits', 0.132"),
				  OnABulwark->DamageOverTimeMultiplier(), OnTheBulwark, 0.0001f);
		TestTrue(TEXT("CONTROL: and not an Outpost's 0.06"),
				 FMath::Abs(OnABulwark->DamageOverTimeMultiplier() - Multiplier) > 0.05f);
	}
	return true;
}

/**
 * THE CORRUPTED DOUBLE'S DAMAGE OVER TIME IS THE PLAYER'S OWN FIGURE: it carries the player's numbers and no multiplier
 * (R4, ruled 2026-11-02), so "the multiplier its hits carry" is one. A double wearing a Greataxe build lays 25 a tick
 * with its own damage over time stats left out: 25 a tick.
 */
CATACLYSM_ENEMY_BURN_TEST(FCataclysmEnemyBurnDoubleTest,
	"Cataclysm.EnemyBurn.ACorruptedDoublesDamageOverTimeIsThePlayersOwnFigure")
{
	using namespace CataclysmEnemyBurnTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnBurnPlayer(World, FVector(2.0f * BurnMetre, 0.0f, 0.0f));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmConsumptionDoubleCharacter* Double = World->SpawnActor<ACataclysmConsumptionDoubleCharacter>(
		ACataclysmConsumptionDoubleCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a double"), Double))
	{
		return false;
	}
	Double->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));

	FCataclysmItem Axe;
	Axe.Base = FName(TEXT("Weapon_Greataxe"));
	Axe.GearLevel = 3;
	Axe.DamageTypes = {FName(TEXT("Demonic"))};
	FCataclysmWornItem Worn;
	Worn.Slot = ECataclysmGearSlot::Weapon1;
	Worn.Item = Axe;
	FCataclysmPlayerBuild Build;
	Build.Worn = {Worn};
	Build.Level = 20;
	Build.ClassName = TEXT("Ravager");
	Build.CreationDamageType = FName(TEXT("Demonic"));
	if (!TestTrue(TEXT("set-up: the double wears the build"), Double->WearThePlayersBuild(Build)))
	{
		return false;
	}
	TestEqual(TEXT("its damage over time multiplier is one"), Double->DamageOverTimeMultiplier(), 1.0f, 0.0001f);
	TestTrue(TEXT("set-up: the double lays the table's burn"),
			 UCataclysmSkillEffects::ApplyDamageOverTime(Double, Player, 25.0f, 4.0f, Burn,
														 /*bScalesWithInstigator=*/false));
	TestEqual(TEXT("it deals the table's 25 a tick, the player's own figure"), BurnPerTickOn(Player, Burn), 25.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
