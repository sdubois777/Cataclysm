// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmSilence.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Silence on a creature, the Null Emperor set's first layer. Ruled 2026-10-17: the owner's O1 and O2 and the
 * coordinating session's O3 to O6, recorded in `docs/DECISIONS.md`.
 *
 * WHAT IS READ. A creature's choice of ability is read from `ACataclysmEnemyController::ChooseAbility`, which is the
 * one gate a silence adds; what the creature does is read from `Think`, as `Cataclysm.AI.*` reads it, and its swing
 * from `AttacksOrdered`, which counts the swings `Think` ordered. A world built for a test never ticks, so the clock is
 * moved by hand: `SilenceAdvanceClock` moves only the time, where a test must keep a silence from running out, and
 * `CataclysmTestWorld::RunClock` moves the time and the timers together, where a silence or an aura must run.
 *
 * EVERY TEST NEEDS `State.Silenced` IN THE TAG VOCABULARY and asks for it as a set-up first: the enchantment session
 * adds it to the workbook's Tags sheet below this layer. Without it no character is ever silenced, and a test that
 * did not ask would pass having measured nothing.
 *
 * STANDING. Each creature and the character it fights are at least a metre apart; a creature is spawned where it
 * stands, and what it fights is spawned far away and moved, because two capsules made close together push apart.
 * Separate scenes in one world stand 40 metres apart.
 */
namespace CataclysmSilenceTest
{
	constexpr float M = 100.0f;

	/** Whether the tag is registered; adds the reason as an error when it is not. */
	bool SilenceTagIsRegistered(FAutomationTestBase& Test)
	{
		if (UCataclysmSilence::SilencedTag().IsValid())
		{
			return true;
		}
		Test.AddError(TEXT("State.Silenced is not in the tag vocabulary, so nothing can be silenced. The enchantment "
						   "session adds it to the workbook's Tags sheet; regenerate the gameplay tags with "
						   "python tools/generate_gameplay_tags.py"));
		return false;
	}

	/** Spawn parameters that always place the actor where it is asked. */
	FActorSpawnParameters SilenceAlwaysSpawn()
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return Params;
	}

	/**
	 * A plain creature of a side, with this much health and attack, standing here. Spawned 300 metres away and then
	 * moved, so it never spawns against another capsule.
	 */
	ACataclysmEnemyCharacter* SilenceSpawnOn(UWorld* World, ECataclysmTeam Side, const FVector& Where,
											 float Health = 100000.0f, float AttackDamage = 0.0f)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(30000.0f, 30000.0f, 0.0f), FRotator::ZeroRotator,
			SilenceAlwaysSpawn());
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(Side));
			Made->SetHealth(Health);
			Made->SetAttackDamage(AttackDamage);
			Made->SetActorLocation(Where);
		}
		return Made;
	}

	template <typename TCreature>
	TCreature* SilenceSpawnCreature(UWorld* World, const FVector& Where)
	{
		return World->SpawnActor<TCreature>(TCreature::StaticClass(), Where, FRotator::ZeroRotator,
											SilenceAlwaysSpawn());
	}

	ACataclysmEnemyController* SilenceBrainOf(const APawn* Creature)
	{
		return Creature ? Cast<ACataclysmEnemyController>(Creature->GetController()) : nullptr;
	}

	int32 SilenceDid(ACataclysmEnemyController* Brain)
	{
		return static_cast<int32>(Brain->Think());
	}

	constexpr int32 SilenceAttacking = static_cast<int32>(ECataclysmBrainAction::Attacking);
	constexpr int32 SilenceChasing = static_cast<int32>(ECataclysmBrainAction::Chasing);
	constexpr int32 SilenceWindingUp = static_cast<int32>(ECataclysmBrainAction::WindingUp);

	/** Move the world's clock forward without firing a timer, so no effect runs out. */
	void SilenceAdvanceClock(UWorld* World, double Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	float SilenceHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	UCataclysmAbilitySystemComponent* SilenceSystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** How many running effects grant `State.Silenced` on `Actor`. */
	int32 SilenceEffectsOn(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
							FGameplayTagContainer(UCataclysmSilence::SilencedTag()))).Num()
					  : -1;
	}

	/** One flat line of `Stat` on a base of nothing, replacing whatever was worn. */
	void SilenceWearOneStat(UCataclysmAbilitySystemComponent* System, const TCHAR* Stat, float Value)
	{
		if (!System)
		{
			return;
		}
		FCataclysmStatModifier Flat;
		Flat.Bucket = ECataclysmStatBucket::Flat;
		Flat.Source = ECataclysmModifierSource::PassiveKeystone;
		Flat.Value = Value;
		TMap<FName, FCataclysmStatInputs> Stats;
		FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Stat));
		Line.Base = 0.0f;
		Line.Modifiers = {Flat};
		System->SetStatInputs(MoveTemp(Stats));
	}

	/** How many times one event is raised on a component while this is alive. */
	struct FSilenceEventCount
	{
		FSilenceEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
			: System(InSystem), Event(InEvent)
		{
			if (System)
			{
				Handle = System->OnActionEvent.AddLambda([this](FName Raised)
				{
					Count += Raised == Event ? 1 : 0;
				});
			}
		}
		~FSilenceEventCount()
		{
			if (System)
			{
				System->OnActionEvent.Remove(Handle);
			}
		}
		UCataclysmAbilitySystemComponent* System = nullptr;
		FName Event;
		int32 Count = 0;
		FDelegateHandle Handle;
	};

	/** A player pawn with its ability system on a player state, as the game wires it, with an attack of 300. */
	ACataclysmPlayerCharacter* SilenceSpawnPlayer(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!State || !Actor)
		{
			return nullptr;
		}
		Actor->SetPlayerState(State);
		Actor->OnRep_PlayerState();
		if (UCataclysmAbilitySystemComponent* System = SilenceSystemOf(Actor))
		{
			TMap<FName, FCataclysmStatInputs> Lines;
			Lines.FindOrAdd(FName(TEXT("attack_damage"))).Base = 300.0f;
			System->SetStatInputs(MoveTemp(Lines));
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 300.0f);
		}
		return Actor;
	}

	/** A creature of the monsters' side with a thousand health and no evasion, armour or block, so a blow lands. */
	ACataclysmEnemyCharacter* SilenceSpawnStruck(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = SilenceSpawnOn(World, ECataclysmTeam::Monsters, Where, 1000.0f, 0.0f);
		if (Made)
		{
			Made->SetArmour(0.0f);
			if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Made))
			{
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			}
		}
		return Made;
	}

	/** The Null Emperor 2-piece's row, made by hand: `apply_status` "Silence" on `hit_dealt`, "not a spell". */
	FCataclysmPoolAction SilenceRow(float Chance)
	{
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("hit_dealt"));
		Row.Pool = FName(UCataclysmAbilitySystemComponent::ApplyStatusAction);
		Row.ApplyStatus = ECataclysmApplyStatus::Chance;
		Row.StatusName = UCataclysmAbilitySystemComponent::SilenceStatus;
		Row.Percent = Chance;
		Row.Condition = ECataclysmStatCondition::HitDealtIsNotSpell;
		// NOUGHT HERE, so several events in one instant each roll; the row's 0.25 is the generator's to carry.
		Row.TriggerCooldownSeconds = 0.0f;
		Row.TriggerKey = FName(TEXT("Test:apply_status:Silence:hit_dealt"));
		return Row;
	}
}

#define CATACLYSM_SILENCE_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * O1. Two Brutes, 40 m apart, each with a player 2 m off, inside its 3.5 m stomp and outside its 2.58 m rock minimum.
 * The control is unsilenced: it chooses the stomp and starts its wind-up. The other is silenced: it chooses nothing,
 * starts no wind-up, and chases, because 2 m is past its 90 cm swing.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceStartsNoAbilityTest,
	"Cataclysm.Silence.ASilencedCreatureStartsNoAbilityItOtherwiseStarts")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmBruteCharacter* Plain = SilenceSpawnCreature<ACataclysmBruteCharacter>(World, FVector::ZeroVector);
	ACataclysmBruteCharacter* Silenced =
		SilenceSpawnCreature<ACataclysmBruteCharacter>(World, FVector(0, 40 * M, 0));
	ACataclysmEnemyCharacter* PlainsPlayer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(2 * M, 0, 0));
	ACataclysmEnemyCharacter* SilencedsPlayer =
		SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(2 * M, 40 * M, 0));
	ACataclysmEnemyController* PlainBrain = SilenceBrainOf(Plain);
	ACataclysmEnemyController* SilencedBrain = SilenceBrainOf(Silenced);
	if (!TestNotNull(TEXT("set-up: a Brute with a brain"), PlainBrain)
		|| !TestNotNull(TEXT("set-up: another"), SilencedBrain)
		|| !TestNotNull(TEXT("set-up: a player"), PlainsPlayer) || !TestNotNull(TEXT("set-up: another"), SilencedsPlayer))
	{
		return false;
	}

	TestEqual(TEXT("control: unsilenced, at 2 m it chooses the stomp"), PlainBrain->ChooseAbility(2 * M),
			  int32(ACataclysmBruteCharacter::StompAbility));
	TestEqual(TEXT("control: and starts its wind-up"), SilenceDid(PlainBrain), SilenceWindingUp);
	TestEqual(TEXT("control: committed to the stomp"), PlainBrain->WindingUpAbility,
			  int32(ACataclysmBruteCharacter::StompAbility));

	if (!TestTrue(TEXT("set-up: the silence lands"),
				  UCataclysmSilence::ApplySilence(SilencedsPlayer, Silenced, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}
	TestEqual(TEXT("silenced, at 2 m it chooses no ability"), SilencedBrain->ChooseAbility(2 * M), int32(INDEX_NONE));
	TestEqual(TEXT("and it chases rather than winding up"), SilenceDid(SilencedBrain), SilenceChasing);
	TestEqual(TEXT("and is committed to nothing"), SilencedBrain->WindingUpAbility, int32(INDEX_NONE));
	return true;
}

/**
 * O1. The three creatures whose basic attack is an entry of their abilities, 40 m apart. For each, the control reads
 * the unsilenced choice first at a distance where another ability wins; then, silenced, the basic attack is chosen
 * there. The Gatekeeper's sweep reaches 2 m, so it is read at 1 m, where it is the only phase 1 ability, and its lob
 * at 8.74 m is shown refused.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceKeepsTheBasicAttackTest,
	"Cataclysm.Silence.EachCreatureWhoseBasicAttackIsAnAbilityStillUsesItWhileSilenced")
{
	using namespace CataclysmSilenceTest;
	using Sentinel_t = ACataclysmCorruptedSentinelCharacter;
	using Succubus_t = ACataclysmSuccubusCharacter;
	using Gatekeeper_t = ACataclysmGatekeeperCharacter;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	Sentinel_t* Sentinel = SilenceSpawnCreature<Sentinel_t>(World, FVector::ZeroVector);
	Succubus_t* Succubus = SilenceSpawnCreature<Succubus_t>(World, FVector(0, 40 * M, 0));
	Gatekeeper_t* Gatekeeper = SilenceSpawnCreature<Gatekeeper_t>(World, FVector(0, 80 * M, 0));
	ACataclysmEnemyCharacter* Silencer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(0, -40 * M, 0));
	ACataclysmEnemyController* SentinelBrain = SilenceBrainOf(Sentinel);
	ACataclysmEnemyController* SuccubusBrain = SilenceBrainOf(Succubus);
	ACataclysmEnemyController* GatekeeperBrain = SilenceBrainOf(Gatekeeper);
	if (!TestNotNull(TEXT("set-up: a Corrupted Sentinel with a brain"), SentinelBrain)
		|| !TestNotNull(TEXT("set-up: a Succubus with a brain"), SuccubusBrain)
		|| !TestNotNull(TEXT("set-up: a Gatekeeper with a brain"), GatekeeperBrain)
		|| !TestNotNull(TEXT("set-up: something to silence them"), Silencer))
	{
		return false;
	}
	Gatekeeper->SetHealth(1000.0f);
	if (!TestEqual(TEXT("set-up: the Gatekeeper is in phase 1"), Gatekeeper->CurrentPhase(), 1))
	{
		return false;
	}

	// THE FLAG, READ OFF EACH LIST: true on the basic attack and on nothing else.
	const TArray<FCataclysmEnemyAbility> SentinelAbilities = Sentinel->EnemyAbilities();
	const TArray<FCataclysmEnemyAbility> SuccubusAbilities = Succubus->EnemyAbilities();
	const TArray<FCataclysmEnemyAbility> GatekeeperAbilities = Gatekeeper->EnemyAbilities();
	if (!TestEqual(TEXT("set-up: the Sentinel has two abilities"), SentinelAbilities.Num(), 2)
		|| !TestEqual(TEXT("set-up: the Succubus has two"), SuccubusAbilities.Num(), 2)
		|| !TestEqual(TEXT("set-up: the Gatekeeper has four"), GatekeeperAbilities.Num(), 4))
	{
		return false;
	}
	TestTrue(TEXT("Siege Bolt is the Sentinel's basic attack"),
			 SentinelAbilities[Sentinel_t::SiegeBoltAbility].bIsBasicAttack);
	TestFalse(TEXT("and Brimstone Mortar is not"), SentinelAbilities[Sentinel_t::BrimstoneMortarAbility].bIsBasicAttack);
	TestTrue(TEXT("Soulfire is the Succubus's basic attack"), SuccubusAbilities[Succubus_t::SoulfireAbility].bIsBasicAttack);
	TestFalse(TEXT("and Wither the Living is not"), SuccubusAbilities[Succubus_t::WitherTheLivingAbility].bIsBasicAttack);
	TestTrue(TEXT("Dread Cleave is the Gatekeeper's basic attack"),
			 GatekeeperAbilities[Gatekeeper_t::DreadCleaveAbility].bIsBasicAttack);
	TestFalse(TEXT("and Soulfall is not"), GatekeeperAbilities[Gatekeeper_t::SoulfallAbility].bIsBasicAttack);
	TestFalse(TEXT("nor Call the Damned"), GatekeeperAbilities[Gatekeeper_t::CallTheDamnedAbility].bIsBasicAttack);
	TestFalse(TEXT("nor Soul Harvest"), GatekeeperAbilities[Gatekeeper_t::SoulHarvestAbility].bIsBasicAttack);

	// THE CONTROLS, UNSILENCED.
	const float SentinelAt = 7 * M;
	const float SuccubusAt = 4 * M;
	const float Close = 1 * M;
	const float Lobbing = (Gatekeeper_t::SoulfallMinimumRangeCm + Gatekeeper_t::SoulfallRangeCm) / 2.0f;
	TestEqual(TEXT("control: unsilenced, at 7 m the Sentinel lobs its mortar"), SentinelBrain->ChooseAbility(SentinelAt),
			  int32(Sentinel_t::BrimstoneMortarAbility));
	TestEqual(TEXT("control: unsilenced, at 4 m the Succubus curses"), SuccubusBrain->ChooseAbility(SuccubusAt),
			  int32(Succubus_t::WitherTheLivingAbility));
	TestEqual(TEXT("control: unsilenced, at 1 m the Gatekeeper sweeps"), GatekeeperBrain->ChooseAbility(Close),
			  int32(Gatekeeper_t::DreadCleaveAbility));
	TestEqual(TEXT("control: unsilenced, further out it lobs"), GatekeeperBrain->ChooseAbility(Lobbing),
			  int32(Gatekeeper_t::SoulfallAbility));

	// SILENCED.
	if (!TestTrue(TEXT("set-up: the Sentinel is silenced"),
				  UCataclysmSilence::ApplySilence(Silencer, Sentinel, UCataclysmSilence::SilenceSeconds))
		|| !TestTrue(TEXT("set-up: the Succubus is silenced"),
					 UCataclysmSilence::ApplySilence(Silencer, Succubus, UCataclysmSilence::SilenceSeconds))
		|| !TestTrue(TEXT("set-up: the Gatekeeper is silenced"),
					 UCataclysmSilence::ApplySilence(Silencer, Gatekeeper, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}
	TestEqual(TEXT("silenced, at 7 m the Sentinel fires Siege Bolt"), SentinelBrain->ChooseAbility(SentinelAt),
			  int32(Sentinel_t::SiegeBoltAbility));
	TestEqual(TEXT("silenced, at 4 m the Succubus fires Soulfire"), SuccubusBrain->ChooseAbility(SuccubusAt),
			  int32(Succubus_t::SoulfireAbility));
	TestEqual(TEXT("silenced, at 1 m the Gatekeeper still sweeps"), GatekeeperBrain->ChooseAbility(Close),
			  int32(Gatekeeper_t::DreadCleaveAbility));
	TestEqual(TEXT("and further out, where only its lob reaches, it chooses nothing"),
			  GatekeeperBrain->ChooseAbility(Lobbing), int32(INDEX_NONE));
	return true;
}

/**
 * O1. Two plain creatures, which have no abilities and whose basic attack is the swing, 40 m apart, each fighting a
 * player 1.5 m off, inside the 2 m reach. The control is unsilenced; the other is silenced. Each swings once on its
 * first pass; then each player is moved to 5 m and each creature chases.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceSwingAndChaseTest,
	"Cataclysm.Silence.ASilencedCreaturesOwnSwingAndItsChaseAreUnchanged")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Plain =
		SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector::ZeroVector, 1000.0f, 10.0f);
	ACataclysmEnemyCharacter* Silenced =
		SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(0, 40 * M, 0), 1000.0f, 10.0f);
	ACataclysmEnemyCharacter* PlainsPlayer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(1.5f * M, 0, 0));
	ACataclysmEnemyCharacter* SilencedsPlayer =
		SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(1.5f * M, 40 * M, 0));
	ACataclysmEnemyController* PlainBrain = SilenceBrainOf(Plain);
	ACataclysmEnemyController* SilencedBrain = SilenceBrainOf(Silenced);
	if (!TestNotNull(TEXT("set-up: a creature with a brain"), PlainBrain)
		|| !TestNotNull(TEXT("set-up: another"), SilencedBrain)
		|| !TestNotNull(TEXT("set-up: a player"), PlainsPlayer) || !TestNotNull(TEXT("set-up: another"), SilencedsPlayer))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the silence lands on the second creature"),
				  UCataclysmSilence::ApplySilence(SilencedsPlayer, Silenced, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}

	TestEqual(TEXT("control: unsilenced and in reach, it attacks"), SilenceDid(PlainBrain), SilenceAttacking);
	TestEqual(TEXT("control: and ordered one swing"), PlainBrain->AttacksOrdered, 1);
	TestEqual(TEXT("silenced and in reach, it attacks the same"), SilenceDid(SilencedBrain), SilenceAttacking);
	TestEqual(TEXT("and ordered one swing as well"), SilencedBrain->AttacksOrdered - PlainBrain->AttacksOrdered, 0);

	PlainsPlayer->SetActorLocation(FVector(5 * M, 0, 0));
	SilencedsPlayer->SetActorLocation(FVector(5 * M, 40 * M, 0));
	TestEqual(TEXT("control: unsilenced, with the player 5 m off, it chases"), SilenceDid(PlainBrain), SilenceChasing);
	TestEqual(TEXT("silenced, it chases the same"), SilenceDid(SilencedBrain), SilenceChasing);
	TestTrue(TEXT("set-up: it was silenced throughout"), UCataclysmSilence::IsSilenced(Silenced));
	return true;
}

/**
 * O1, a timed behaviour: the Succubus's Dominion, swept by a timer every half second. Two Succubi, 40 m apart, each
 * with an ally spawned after it, 4 m off, so the sweep Dominion makes as the Succubus spawns has not reached it. The
 * control is unsilenced; the other is silenced. The clock runs 0.6 s, timers included, and each ally then holds
 * Commander.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceTimedBehaviourTest,
	"Cataclysm.Silence.ATimedBehaviourKeepsRunningWhileSilenced")
{
	using namespace CataclysmSilenceTest;
	using Succubus_t = ACataclysmSuccubusCharacter;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}
	const FGameplayTag Commander = UCataclysmSkillShapes::StatusTagFor(Succubus_t::DominionEffectName);
	if (!Commander.IsValid())
	{
		AddError(TEXT("Status.Buff.Commander is not in the tag vocabulary, so Dominion could grant nothing."));
		return false;
	}

	Succubus_t* Plain = SilenceSpawnCreature<Succubus_t>(World, FVector::ZeroVector);
	Succubus_t* Silenced = SilenceSpawnCreature<Succubus_t>(World, FVector(0, 40 * M, 0));
	ACataclysmEnemyCharacter* PlainsAlly = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(4 * M, 0, 0));
	ACataclysmEnemyCharacter* SilencedsAlly =
		SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(4 * M, 40 * M, 0));
	ACataclysmEnemyCharacter* Silencer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(0, -40 * M, 0));
	if (!TestNotNull(TEXT("set-up: a Succubus"), Plain) || !TestNotNull(TEXT("set-up: another"), Silenced)
		|| !TestNotNull(TEXT("set-up: an ally"), PlainsAlly) || !TestNotNull(TEXT("set-up: another"), SilencedsAlly)
		|| !TestNotNull(TEXT("set-up: something to silence with"), Silencer))
	{
		return false;
	}
	if (!TestFalse(TEXT("set-up: neither ally holds Commander before the timer runs"),
				   UCataclysmSkillEffects::HasTag(PlainsAlly, Commander)
					   || UCataclysmSkillEffects::HasTag(SilencedsAlly, Commander))
		|| !TestTrue(TEXT("set-up: the second Succubus is silenced"),
					 UCataclysmSilence::ApplySilence(Silencer, Silenced, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}

	CataclysmTestWorld::RunClock(World, Succubus_t::DominionRefreshSeconds + 0.1f);

	TestTrue(TEXT("control: unsilenced, its timer's sweep gave its ally Commander"),
			 UCataclysmSkillEffects::HasTag(PlainsAlly, Commander));
	TestTrue(TEXT("set-up: the second Succubus is still silenced"), UCataclysmSilence::IsSilenced(Silenced));
	TestTrue(TEXT("silenced, its timer's sweep gave its ally Commander as well"),
			 UCataclysmSkillEffects::HasTag(SilencedsAlly, Commander));
	return true;
}

/**
 * O3. Two Brutes, 40 m apart, each with a player 2 m off, each starting its stomp. Half a second in, the second is
 * silenced. Past the 1.4 s wind-up, each lands its stomp. The clock is moved without timers, so the silence does not
 * run out during the wind-up.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceWindUpFinishesTest,
	"Cataclysm.Silence.AWindUpAlreadyRunningWhenSilenceLandsFinishes")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmBruteCharacter* Plain = SilenceSpawnCreature<ACataclysmBruteCharacter>(World, FVector::ZeroVector);
	ACataclysmBruteCharacter* Silenced =
		SilenceSpawnCreature<ACataclysmBruteCharacter>(World, FVector(0, 40 * M, 0));
	ACataclysmEnemyCharacter* PlainsPlayer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(2 * M, 0, 0));
	ACataclysmEnemyCharacter* SilencedsPlayer =
		SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(2 * M, 40 * M, 0));
	ACataclysmEnemyController* PlainBrain = SilenceBrainOf(Plain);
	ACataclysmEnemyController* SilencedBrain = SilenceBrainOf(Silenced);
	if (!TestNotNull(TEXT("set-up: a Brute with a brain"), PlainBrain)
		|| !TestNotNull(TEXT("set-up: another"), SilencedBrain)
		|| !TestNotNull(TEXT("set-up: a player"), PlainsPlayer) || !TestNotNull(TEXT("set-up: another"), SilencedsPlayer))
	{
		return false;
	}
	Plain->SetAttackDamage(35.0f);
	Silenced->SetAttackDamage(35.0f);

	if (!TestEqual(TEXT("set-up: the first Brute begins a stomp"), SilenceDid(PlainBrain), SilenceWindingUp)
		|| !TestEqual(TEXT("set-up: and so does the second"), SilenceDid(SilencedBrain), SilenceWindingUp))
	{
		return false;
	}

	SilenceAdvanceClock(World, 0.5);
	if (!TestTrue(TEXT("set-up: half a second in, the second is silenced"),
				  UCataclysmSilence::ApplySilence(SilencedsPlayer, Silenced, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}
	TestEqual(TEXT("silenced, it is still winding up"), SilenceDid(SilencedBrain), SilenceWindingUp);
	TestEqual(TEXT("and still committed to the stomp"), SilencedBrain->WindingUpAbility,
			  int32(ACataclysmBruteCharacter::StompAbility));

	const float PlainsBefore = SilenceHealthOf(PlainsPlayer);
	const float SilencedsBefore = SilenceHealthOf(SilencedsPlayer);
	SilenceAdvanceClock(World, ACataclysmBruteCharacter::StompWindUpSeconds);

	TestEqual(TEXT("control: unsilenced, the stomp lands"), SilenceDid(PlainBrain), SilenceAttacking);
	TestTrue(FString::Printf(TEXT("control: and its player lost health (%.0f to %.0f)"), PlainsBefore,
							 SilenceHealthOf(PlainsPlayer)),
			 SilenceHealthOf(PlainsPlayer) < PlainsBefore);
	TestTrue(TEXT("set-up: the second Brute is still silenced when its wind-up ends"),
			 UCataclysmSilence::IsSilenced(Silenced));
	TestEqual(TEXT("silenced, its stomp lands as well"), SilenceDid(SilencedBrain), SilenceAttacking);
	TestTrue(FString::Printf(TEXT("and its player lost health (%.0f to %.0f)"), SilencedsBefore,
							 SilenceHealthOf(SilencedsPlayer)),
			 SilenceHealthOf(SilencedsPlayer) < SilencedsBefore);
	TestFalse(TEXT("and the stomp was spent: it is cooling down"),
			  SilencedBrain->IsAbilityReady(ACataclysmBruteCharacter::StompAbility));
	return true;
}

/**
 * O5. Two creatures, 40 m apart. The first is silenced once: held at 0.9 s, gone at 1.1 s. The second is silenced, and
 * silenced again at 0.5 s: held at 1.4 s, past where the first silence would have ended, gone at 1.6 s, before two
 * seconds, and held by one effect throughout. The clock runs with its timers.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceExpiresAndRefreshesTest,
	"Cataclysm.Silence.ItLastsOneSecondAndAReapplicationRefreshesItWithoutStacking")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Silencer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(0, -40 * M, 0));
	ACataclysmEnemyCharacter* Once = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Twice = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(0, 40 * M, 0));
	if (!TestNotNull(TEXT("set-up: a silencer"), Silencer) || !TestNotNull(TEXT("set-up: a creature"), Once)
		|| !TestNotNull(TEXT("set-up: another"), Twice))
	{
		return false;
	}
	TestFalse(TEXT("control: before anything, neither is silenced"),
			  UCataclysmSilence::IsSilenced(Once) || UCataclysmSilence::IsSilenced(Twice));
	if (!TestTrue(TEXT("set-up: the first silence lands"),
				  UCataclysmSilence::ApplySilence(Silencer, Once, UCataclysmSilence::SilenceSeconds))
		|| !TestTrue(TEXT("set-up: and the second's first"),
					 UCataclysmSilence::ApplySilence(Silencer, Twice, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}

	CataclysmTestWorld::RunClock(World, 0.5f);
	if (!TestTrue(TEXT("set-up: at 0.5 s the second is reapplied"),
				  UCataclysmSilence::ApplySilence(Silencer, Twice, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}
	TestEqual(TEXT("and it is held by one effect, not two"), SilenceEffectsOn(Twice), 1);

	CataclysmTestWorld::RunClock(World, 0.4f);
	TestTrue(TEXT("at 0.9 s the first is still silenced"), UCataclysmSilence::IsSilenced(Once));

	CataclysmTestWorld::RunClock(World, 0.2f);
	TestFalse(TEXT("at 1.1 s the first has run out: it lasts one second"), UCataclysmSilence::IsSilenced(Once));
	TestTrue(TEXT("and the reapplied one still holds"), UCataclysmSilence::IsSilenced(Twice));

	CataclysmTestWorld::RunClock(World, 0.3f);
	TestTrue(TEXT("at 1.4 s the reapplied one still holds: it was refreshed to a full second from 0.5 s"),
			 UCataclysmSilence::IsSilenced(Twice));
	TestEqual(TEXT("by one effect"), SilenceEffectsOn(Twice), 1);

	CataclysmTestWorld::RunClock(World, 0.2f);
	TestFalse(TEXT("at 1.6 s it has run out: 1.5 s, not 2 s"), UCataclysmSilence::IsSilenced(Twice));
	return true;
}

/**
 * O2. A boss on the Boss rung. The control: a designed stun on it is refused. Then a silence lands on it and holds for
 * the full second: held at 0.9 s, gone at 1.1 s.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceBossTest, "Cataclysm.Silence.ABossIsSilencedForTheFullSecond")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Silencer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Boss = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(3 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a silencer"), Silencer) || !TestNotNull(TEXT("set-up: a creature"), Boss))
	{
		return false;
	}
	Boss->SetRarityStep(4);
	if (!TestTrue(TEXT("set-up: the Boss rung is a boss"), Boss->IsBoss()))
	{
		return false;
	}

	TestFalse(TEXT("control: a designed stun on the boss is refused"),
			  UCataclysmSkillEffects::ApplyStun(Silencer, Boss, 1.0f, 0.0f, /*bStunIsDesigned=*/true));
	TestTrue(TEXT("a silence on the same boss lands"),
			 UCataclysmSilence::ApplySilence(Silencer, Boss, UCataclysmSilence::SilenceSeconds));
	CataclysmTestWorld::RunClock(World, 0.9f);
	TestTrue(TEXT("and at 0.9 s it is still silenced"), UCataclysmSilence::IsSilenced(Boss));
	CataclysmTestWorld::RunClock(World, 0.2f);
	TestFalse(TEXT("and at 1.1 s it has run out: the full second, not shortened"), UCataclysmSilence::IsSilenced(Boss));
	return true;
}

/**
 * O4, the refusals. Each target is a creature at full health, 40 m from the others. The control silences a creature
 * with nothing in its way first. Then: refused from a silencer carrying `crowd_control_health_ceiling_reduction` at
 * 50, which a full-health enemy is above; refused on a creature at 100 crowd-control resistance.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceRefusedTest,
	"Cataclysm.Silence.ItIsRefusedAboveTheHealthCeilingAndAtFullCrowdControlResistance")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Silencer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(0, -40 * M, 0));
	ACataclysmEnemyCharacter* Ceilinged = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(0, -80 * M, 0));
	ACataclysmEnemyCharacter* Plain = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Full = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(0, 40 * M, 0));
	ACataclysmEnemyCharacter* Resistant = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(0, 80 * M, 0));
	UCataclysmAbilitySystemComponent* CeilingSystem = SilenceSystemOf(Ceilinged);
	UAbilitySystemComponent* ResistantSystem = UCataclysmTargeting::AbilitySystemOf(Resistant);
	if (!TestNotNull(TEXT("set-up: a silencer"), Silencer) || !TestNotNull(TEXT("set-up: one with a ceiling"), CeilingSystem)
		|| !TestNotNull(TEXT("set-up: a creature"), Plain) || !TestNotNull(TEXT("set-up: another"), Full)
		|| !TestNotNull(TEXT("set-up: a resistant one"), ResistantSystem))
	{
		return false;
	}
	SilenceWearOneStat(CeilingSystem, UCataclysmSkillEffects::CrowdControlHealthCeilingStat, 50.0f);
	ResistantSystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 100.0f);

	TestTrue(TEXT("control: a silencer with no ceiling silences a creature at full health"),
			 UCataclysmSilence::ApplySilence(Silencer, Plain, UCataclysmSilence::SilenceSeconds));
	TestFalse(TEXT("one whose ceiling is 50% does not silence a creature at full health"),
			  UCataclysmSilence::ApplySilence(Ceilinged, Full, UCataclysmSilence::SilenceSeconds));
	TestFalse(TEXT("and that creature is not silenced"), UCataclysmSilence::IsSilenced(Full));
	TestFalse(TEXT("a creature at 100 crowd-control resistance is not silenced"),
			  UCataclysmSilence::ApplySilence(Silencer, Resistant, UCataclysmSilence::SilenceSeconds));
	TestFalse(TEXT("and is not"), UCataclysmSilence::IsSilenced(Resistant));
	return true;
}

/**
 * O4, the stun window. Two creatures, 40 m apart. The first is stunned for a second and the stun runs out: the control
 * reads that a second stun is refused inside the window; a silence still lands. The second is silenced first, and a
 * stun straight after lands: the silence opened no window.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceStunWindowTest,
	"Cataclysm.Silence.ItIsNotRefusedInsideTheStunWindowAndOpensNone")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Silencer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector(0, -40 * M, 0));
	ACataclysmEnemyCharacter* Stunned = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector::ZeroVector);
	ACataclysmEnemyCharacter* SilencedFirst = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(0, 40 * M, 0));
	if (!TestNotNull(TEXT("set-up: a silencer"), Silencer) || !TestNotNull(TEXT("set-up: a creature"), Stunned)
		|| !TestNotNull(TEXT("set-up: another"), SilencedFirst))
	{
		return false;
	}

	if (!TestTrue(TEXT("set-up: a one-second stun lands on the first"),
				  UCataclysmSkillEffects::ApplyStun(Silencer, Stunned, 1.0f, 0.0f, /*bStunIsDesigned=*/true)))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 1.5f);
	if (!TestFalse(TEXT("set-up: the stun has run out"), UCataclysmSkillEffects::IsStunned(Stunned)))
	{
		return false;
	}
	TestFalse(TEXT("control: inside the five-second window a second stun is refused"),
			  UCataclysmSkillEffects::ApplyStun(Silencer, Stunned, 1.0f, 0.0f, /*bStunIsDesigned=*/true));
	TestTrue(TEXT("inside the same window a silence lands"),
			 UCataclysmSilence::ApplySilence(Silencer, Stunned, UCataclysmSilence::SilenceSeconds));

	TestTrue(TEXT("a silence lands on the second creature"),
			 UCataclysmSilence::ApplySilence(Silencer, SilencedFirst, UCataclysmSilence::SilenceSeconds));
	TestTrue(TEXT("and a stun straight after it lands too: the silence opened no window"),
			 UCataclysmSkillEffects::ApplyStun(Silencer, SilencedFirst, 1.0f, 0.0f, /*bStunIsDesigned=*/true));
	return true;
}

/**
 * O4, crowd control. A creature not yet silenced is not under crowd control, and its silencer has raised no
 * `crowd_control` event. Silenced, it is under crowd control, and the event was raised once on the silencer. When the
 * silence runs out, it is not.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceIsCrowdControlTest,
	"Cataclysm.Silence.ItIsCrowdControlAndRaisesTheCrowdControlEventOnce")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Silencer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(3 * M, 0, 0));
	UCataclysmAbilitySystemComponent* SilencerSystem = SilenceSystemOf(Silencer);
	if (!TestNotNull(TEXT("set-up: a silencer with an ability system of ours"), SilencerSystem)
		|| !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	const FSilenceEventCount Controlled(SilencerSystem, TEXT("crowd_control"));

	TestFalse(TEXT("control: not yet silenced, it is not under crowd control"),
			  UCataclysmSkillEffects::IsCrowdControlled(Creature));
	TestEqual(TEXT("control: and no crowd_control event has been raised"), Controlled.Count, 0);

	if (!TestTrue(TEXT("set-up: the silence lands"),
				  UCataclysmSilence::ApplySilence(Silencer, Creature, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}
	TestTrue(TEXT("silenced, it is under crowd control"), UCataclysmSkillEffects::IsCrowdControlled(Creature));
	TestEqual(TEXT("and the crowd_control event was raised once on the silencer"), Controlled.Count, 1);
	TestFalse(TEXT("and it is not a hard stop: it can still act"), UCataclysmSkillEffects::CannotAct(Creature));

	CataclysmTestWorld::RunClock(World, 1.1f);
	TestFalse(TEXT("once it runs out, it is not under crowd control"), UCataclysmSkillEffects::IsCrowdControlled(Creature));
	return true;
}

/** O5. The creature's status line reads "Silenced" while the silence holds, and not before or after. */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceStatusLineTest, "Cataclysm.Silence.TheStatusLineReadsSilencedWhileItHolds")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Silencer = SilenceSpawnOn(World, ECataclysmTeam::Players, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SilenceSpawnOn(World, ECataclysmTeam::Monsters, FVector(3 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a silencer"), Silencer) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}

	TestEqual(TEXT("control: before the silence, nothing says silenced"),
			  UCataclysmCombatOverlay::SilencedTextFor(Creature), FString());
	TestFalse(TEXT("control: nor does its status line"),
			  UCataclysmCombatOverlay::StatusLineFor(Creature).Contains(TEXT("Silenced")));
	if (!TestTrue(TEXT("set-up: the silence lands"),
				  UCataclysmSilence::ApplySilence(Silencer, Creature, UCataclysmSilence::SilenceSeconds)))
	{
		return false;
	}
	TestEqual(TEXT("silenced, the text is \"Silenced\""), UCataclysmCombatOverlay::SilencedTextFor(Creature),
			  FString(TEXT("Silenced")));
	TestTrue(TEXT("and the status line holds it"),
			 UCataclysmCombatOverlay::StatusLineFor(Creature).Contains(TEXT("Silenced")));
	CataclysmTestWorld::RunClock(World, 1.1f);
	TestFalse(TEXT("once it runs out, the status line does not"),
			  UCataclysmCombatOverlay::StatusLineFor(Creature).Contains(TEXT("Silenced")));
	return true;
}

/**
 * O6, the row. A possessed player character wearing `apply_status` "Silence" at a chance of 100 on `hit_dealt` under
 * `hit_dealt_is_not_spell`, made by hand, and four creatures 3 m off in four directions. The control first: the same
 * player's real blow with no row worn silences nothing. Then, with the row: a real blow, which no skill dealt, so not
 * a spell, silences; an event carrying `Type.Melee` silences; an event carrying `Type.Spell` does not.
 */
CATACLYSM_SILENCE_TEST(FCataclysmSilenceRowTest,
	"Cataclysm.Silence.TheRowSilencesOnAHitThatIsNotASpellAndNotOnASpell")
{
	using namespace CataclysmSilenceTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	if (!SilenceTagIsRegistered(*this))
	{
		return false;
	}
	UCataclysmCombatEvents::In(World);
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	ACataclysmPlayerCharacter* Player = SilenceSpawnPlayer(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* PlayerSystem = SilenceSystemOf(Player);
	ACataclysmEnemyCharacter* Unworn = SilenceSpawnStruck(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Struck = SilenceSpawnStruck(World, FVector(-3 * M, 0, 0));
	ACataclysmEnemyCharacter* Melee = SilenceSpawnStruck(World, FVector(0, 3 * M, 0));
	ACataclysmEnemyCharacter* Spell = SilenceSpawnStruck(World, FVector(0, -3 * M, 0));
	const FGameplayTag MeleeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")), false);
	const FGameplayTag SpellTag = UCataclysmSkillEffects::SpellTag();
	if (!TestNotNull(TEXT("set-up: a player's ability system"), PlayerSystem)
		|| !TestNotNull(TEXT("set-up: a creature"), Unworn) || !TestNotNull(TEXT("set-up: another"), Struck)
		|| !TestNotNull(TEXT("set-up: a third"), Melee) || !TestNotNull(TEXT("set-up: a fourth"), Spell)
		|| !TestTrue(TEXT("set-up: Type.Melee and Type.Spell are registered"), MeleeTag.IsValid() && SpellTag.IsValid()))
	{
		return false;
	}

	// THE CONTROL: no row worn, a real blow lands and silences nothing.
	const float UnwornBefore = SilenceHealthOf(Unworn);
	UCataclysmSkillEffects::ApplyHit(Player, Unworn, /*DamagePercent=*/10.0f);
	if (!TestTrue(TEXT("set-up: the blow with no row worn lands"), SilenceHealthOf(Unworn) < UnwornBefore))
	{
		return false;
	}
	TestFalse(TEXT("control: with no row worn, it silences nothing"), UCataclysmSilence::IsSilenced(Unworn));

	PlayerSystem->SetPoolActions({SilenceRow(100.0f)});

	const float StruckBefore = SilenceHealthOf(Struck);
	UCataclysmSkillEffects::ApplyHit(Player, Struck, /*DamagePercent=*/10.0f);
	if (!TestTrue(TEXT("set-up: the blow with the row worn lands"), SilenceHealthOf(Struck) < StruckBefore))
	{
		return false;
	}
	TestTrue(TEXT("with the row worn, a blow no skill dealt silences: it is not a spell"),
			 UCataclysmSilence::IsSilenced(Struck));

	const FGameplayTagContainer MeleeTags(MeleeTag);
	const FGameplayTagContainer SpellTags(SpellTag);
	PlayerSystem->ActOnEvent(FName(TEXT("hit_dealt")), &MeleeTags, /*EventAmount=*/100.0f, /*bLanded=*/true, Melee);
	TestTrue(TEXT("a hit_dealt carrying Type.Melee silences"), UCataclysmSilence::IsSilenced(Melee));
	PlayerSystem->ActOnEvent(FName(TEXT("hit_dealt")), &SpellTags, /*EventAmount=*/100.0f, /*bLanded=*/true, Spell);
	TestFalse(TEXT("a hit_dealt carrying Type.Spell does not"), UCataclysmSilence::IsSilenced(Spell));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
