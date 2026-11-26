// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The three Ritualist keystones that change what an energy shield does. Issue
 * #1515.
 *
 *   Ritualist_keystone_c_kA  Warded
 *       "Your Energy Shield absorbs damage that would bypass it." (the owner's
 *        choice of 2026-11-26; until then "Your Energy Shield absorbs damage
 *        over time as well as hits.")
 *   Ritualist_keystone_c_kB  Ablative
 *       "Your Energy Shield recharges while you are taking damage, at half its
 *        usual rate."
 *   Ritualist_keystone_d_kA  The Long Game
 *       "Your Mana Regeneration also restores your Energy Shield, at half its
 *        rate."
 *
 * THEY ARE THREE DIFFERENT MECHANISMS AND NOT ONE SHAPE, which is the thing
 * these tests exist to hold in place. Warded changes whether a blow that
 * would bypass the shield reaches it, in the damage calculation. Ablative changes WHEN the shield may
 * recharge, in the regeneration step. The Long Game adds a SECOND SOURCE of
 * recharge, in the same step but outside the wait the shield normally serves.
 * All three are flags; the halved rates two of them state are constants at their
 * read sites.
 *
 * EACH IS TESTED ALONE AND THEN ALL THREE TOGETHER. The last test is the one
 * that would catch a flag wired to another flag's attribute: three single tests
 * all pass if every flag turns on every behaviour, and only asserting the other
 * two behaviours ABSENT says the three are distinct.
 *
 * THE CONTROLS FOR TWO OF THEM ALREADY EXISTED AND ARE LEFT UNTOUCHED.
 * `Cataclysm.Regeneration.AnEnergyShieldDoesNotRefillWhileTheWaitIsRunning` is
 * Ablative's: without the node the shield stays put for three seconds, and that
 * has to remain true.
 * `Cataclysm.Damage.EnergyShieldAbsorbsBeforeHealthAndEveryTickAndHoldsABleedOff`
 * is Warded's: without the flag a tick that ignores the shield goes to health.
 * Every character in them reads zero for all three flags. Warded's own test
 * below asserts its control as well.
 */
namespace CataclysmShieldKeystoneTest
{
	using Combat = UCataclysmCombatAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	/** A shield large enough that nothing below reaches its ceiling. */
	constexpr float ShieldCeiling = 1000.0f;

	/** The shield's own recharge rate, chosen so its half is a whole number. */
	constexpr float ShieldRate = 10.0f;

	/** Mana regeneration, chosen so its half is a different whole number. */
	constexpr float ManaRate = 8.0f;

	/**
	 * One second since the character was last damaged.
	 *
	 * INSIDE THE THREE-SECOND WAIT, which is the whole point: it is the window
	 * in which an ordinary shield recharges nothing, so anything that appears
	 * there came from a keystone.
	 */
	constexpr float JustHurt = 1.0f;

	/** Well past the wait, where an ordinary shield recharges normally. */
	constexpr float LongSinceHurt = 60.0f;

	static ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerCharacter* Pawn =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
														 FRotator::ZeroRotator);
		ACataclysmPlayerState* State =
			World->SpawnActor<ACataclysmPlayerState>();
		if (Pawn && State)
		{
			Pawn->SetPlayerState(State);
			Pawn->OnRep_PlayerState();
		}
		return Pawn;
	}

	static UAbilitySystemComponent* SystemOf(AActor* Actor)
	{
		// Through the interface, because a player's ability system lives on its
		// player state rather than on the pawn.
		return UCataclysmTargeting::AbilitySystemOf(Actor);
	}

	static void Write(AActor* Actor, const FGameplayAttribute& Attribute,
					  float Value)
	{
		if (UAbilitySystemComponent* System = SystemOf(Actor))
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}
	}

	static float Read(AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* System = SystemOf(Actor);
		return System ? System->GetNumericAttribute(Attribute) : -1.0f;
	}

	/**
	 * A character with a shield to refill, a rate to refill it at, mana
	 * regeneration to draw on, and no keystone.
	 *
	 * THE SHIELD STARTS EMPTY so every figure below is what came back rather
	 * than what was already there.
	 */
	static void MakeShieldedCharacter(AActor* Actor)
	{
		Write(Actor, Vital::GetMaxEnergyShieldAttribute(), ShieldCeiling);
		Write(Actor, Vital::GetEnergyShieldAttribute(), 0.0f);
		Write(Actor, Vital::GetEnergyShieldRegenAttribute(), ShieldRate);
		Write(Actor, Vital::GetMaxManaAttribute(), 1000.0f);
		Write(Actor, Vital::GetManaAttribute(), 0.0f);
		Write(Actor, Vital::GetManaRegenAttribute(), ManaRate);
	}

	/** Empty the shield again between two readings in one test. */
	static void EmptyTheShield(AActor* Actor)
	{
		Write(Actor, Vital::GetEnergyShieldAttribute(), 0.0f);
	}

	/** One second of regeneration at a stated time since the last damage. */
	static void RunOneSecond(AActor* Actor, float SecondsSinceLastDamage)
	{
		UCataclysmRegeneration::ApplyStep(Actor, /*SecondsInStep=*/1.0f,
										  SecondsSinceLastDamage);
	}

	/** One tick or blow of `Damage`, resolved against `Actor`'s pools. */
	static FCataclysmDamageResult ResolveOne(AActor* Actor, const FCataclysmIncomingHit& Hit)
	{
		return UCataclysmDamageCalculation::Resolve(
			Hit, SystemOf(Actor), /*Tier=*/1,
			/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f);
	}

	/** A tick of BLEED, which an ordinary shield holds off: it deals nothing while the shield stands (2026-11-26). */
	static FCataclysmDamageResult ResolveBleed(AActor* Actor, float Damage)
	{
		FCataclysmIncomingHit Tick;
		Tick.Damage = Damage;
		Tick.bIsDamageOverTime = true;
		Tick.bIsBleed = true;
		return ResolveOne(Actor, Tick);
	}

	/**
	 * A blow or tick that would bypass the shield, as a Void Splinter's tick and a Soul Eater creature's blow do
	 * (`FCataclysmIncomingHit::bIgnoresEnergyShield`). `bTick` says which.
	 */
	static FCataclysmDamageResult ResolveBypassing(AActor* Actor, float Damage, bool bTick)
	{
		FCataclysmIncomingHit Hit;
		Hit.Damage = Damage;
		Hit.bIsDamageOverTime = bTick;
		Hit.bIgnoresEnergyShield = true;
		return ResolveOne(Actor, Hit);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWardedAbsorbsDamageOverTimeTest,
	"Cataclysm.ShieldKeystones.WardedAbsorbsWhatWouldBypassTheShieldButNotABleed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_c_kA` Warded: "Your Energy Shield absorbs damage that would bypass it." The owner's choice of
 * 2026-11-26.
 *
 * THE READING WITHOUT THE NODE IS HALF THE TEST. A Void Splinter's tick and a Soul Eater creature's blow going past
 * the shield to health is the rule for every character in the game, and this test would pass against a build that
 * always absorbed them if it only checked the half with the flag on.
 *
 * AND A BLEED STAYS HELD OFF WITH IT: Warded must not turn a bleed, which does nothing against a standing shield,
 * into a cost.
 */
bool FCataclysmWardedAbsorbsDamageOverTimeTest::RunTest(const FString&)
{
	using namespace CataclysmShieldKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	Write(Player, Vital::GetMaxHealthAttribute(), 100000.0f);
	Write(Player, Vital::GetHealthAttribute(), 100000.0f);
	Write(Player, Vital::GetMaxEnergyShieldAttribute(), 400.0f);
	Write(Player, Vital::GetEnergyShieldAttribute(), 400.0f);

	// THE CONTROL: WITHOUT THE KEYSTONE, BOTH GO STRAIGHT PAST THE SHIELD.
	const FCataclysmDamageResult TickWithout = ResolveBypassing(Player, 1000.0f, /*bTick=*/true);
	TestEqual(TEXT("control: without Warded the shield absorbs none of a Void Splinter's tick"),
			  TickWithout.AbsorbedByShield, 0.0f);
	TestEqual(TEXT("control: and the tick reaches health in full"), TickWithout.DealtToHealth, 1000.0f);
	const FCataclysmDamageResult BlowWithout = ResolveBypassing(Player, 1000.0f, /*bTick=*/false);
	TestEqual(TEXT("control: without Warded the shield absorbs none of a Soul Eater creature's blow"),
			  BlowWithout.AbsorbedByShield, 0.0f);
	TestEqual(TEXT("control: and the blow reaches health in full"), BlowWithout.DealtToHealth, 1000.0f);

	// WITH IT, EACH COMES OFF THE SHIELD LIKE ANY OTHER BLOW.
	Write(Player, Combat::GetShieldAbsorbsWhatBypassesItAttribute(), 1.0f);

	const FCataclysmDamageResult TickWith = ResolveBypassing(Player, 1000.0f, /*bTick=*/true);
	TestEqual(TEXT("with Warded the shield absorbs what it can of a Void Splinter's tick"),
			  TickWith.AbsorbedByShield, 400.0f);
	TestEqual(TEXT("and only the remainder reaches health"), TickWith.DealtToHealth, 600.0f);
	const FCataclysmDamageResult BlowWith = ResolveBypassing(Player, 1000.0f, /*bTick=*/false);
	TestEqual(TEXT("with Warded the shield absorbs what it can of a Soul Eater creature's blow"),
			  BlowWith.AbsorbedByShield, 400.0f);
	TestEqual(TEXT("and only the remainder reaches health"), BlowWith.DealtToHealth, 600.0f);

	// A BLEED IS STILL HELD OFF, AND COSTS THE SHIELD NOTHING.
	const FCataclysmDamageResult Bleed = ResolveBleed(Player, 1000.0f);
	TestEqual(TEXT("with Warded a bleed still takes nothing from the shield"), Bleed.AbsorbedByShield, 0.0f);
	TestEqual(TEXT("and nothing from health while the shield stands"), Bleed.DealtToHealth, 0.0f);

	// AND TURNING IT OFF AGAIN PUTS THE RULE BACK, which would catch a build that read the flag once and remembered it.
	Write(Player, Combat::GetShieldAbsorbsWhatBypassesItAttribute(), 0.0f);
	const FCataclysmDamageResult Again = ResolveBypassing(Player, 1000.0f, /*bTick=*/true);
	TestEqual(TEXT("and without it again the shield absorbs nothing of the tick"), Again.AbsorbedByShield, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBleedDrainsTheShieldTest,
	"Cataclysm.ShieldKeystones.TheBleedDrawbackLetsABleedDrainTheShieldThenHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `bleed_drains_energy_shield`, the drawback "Energy shield can now be effected by bleed": a bleed on its holder is
 * not stopped by the shield, and its ticks come off the shield one for one, then off health. The coordinating
 * session's ruling of 2026-11-26, which keeps the row a drawback under the owner's rule that a bleed does nothing
 * against a standing shield. THE CONTROL: without the flag the same bleed deals nothing at all.
 */
bool FCataclysmBleedDrainsTheShieldTest::RunTest(const FString&)
{
	using namespace CataclysmShieldKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}
	Write(Player, Vital::GetMaxHealthAttribute(), 100000.0f);
	Write(Player, Vital::GetHealthAttribute(), 100000.0f);
	Write(Player, Vital::GetMaxEnergyShieldAttribute(), 400.0f);
	Write(Player, Vital::GetEnergyShieldAttribute(), 400.0f);

	const FCataclysmDamageResult Ordinary = ResolveBleed(Player, 1000.0f);
	TestEqual(TEXT("control: without the drawback a bleed takes nothing from the shield"), Ordinary.AbsorbedByShield, 0.0f);
	TestEqual(TEXT("control: and nothing from health while the shield stands"), Ordinary.DealtToHealth, 0.0f);

	Write(Player, Combat::GetBleedDrainsEnergyShieldAttribute(), 1.0f);
	const FCataclysmDamageResult Drawback = ResolveBleed(Player, 1000.0f);
	TestEqual(TEXT("with the drawback the bleed comes off the shield as far as it goes"), Drawback.AbsorbedByShield, 400.0f);
	TestEqual(TEXT("and the rest off health"), Drawback.DealtToHealth, 600.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAblativeRechargesUnderFireTest,
	"Cataclysm.ShieldKeystones.AblativeRechargesTheShieldWhileTheWaitIsRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_c_kB` Ablative. Issue #1515.
 *
 * IT SUPPLIES A HALF RATE INSIDE THE WAIT RATHER THAN SHORTENING THE WAIT, and
 * the difference is the node. A shorter wait would recharge at the FULL rate
 * sooner, so this test reads the figure and not merely "something came back":
 * five a second where the shield's own rate is ten.
 *
 * AND THE FULL RATE OUTSIDE THE WAIT IS UNCHANGED, which is the half a build
 * that replaced the rate rather than scaling it inside the window would fail.
 */
bool FCataclysmAblativeRechargesUnderFireTest::RunTest(const FString&)
{
	using namespace CataclysmShieldKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}
	MakeShieldedCharacter(Player);

	// NO MANA REGENERATION FOR THIS TEST, so the only thing that can move the
	// shield is the recharge under test. The Long Game has its own test.
	Write(Player, Vital::GetManaRegenAttribute(), 0.0f);

	// WITHOUT THE KEYSTONE, ONE SECOND AFTER BEING HURT, NOTHING COMES BACK.
	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("without Ablative nothing recharges inside the wait"),
			  Read(Player, Vital::GetEnergyShieldAttribute()), 0.0f, 0.001f);

	// WITH IT, HALF THE RATE.
	Write(Player, Combat::GetShieldRechargesWhileDamagedAttribute(), 1.0f);

	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("with Ablative the shield recharges at half its rate inside "
				   "the wait"),
			  Read(Player, Vital::GetEnergyShieldAttribute()),
			  ShieldRate / 2.0f, 0.001f);

	// AND OUTSIDE THE WAIT IT IS STILL THE FULL RATE, not half of it. A build
	// that halved the rate everywhere rather than supplying it inside the window
	// would read five here.
	EmptyTheShield(Player);
	RunOneSecond(Player, LongSinceHurt);
	TestEqual(TEXT("and outside the wait the rate is unchanged at its full "
				   "value"),
			  Read(Player, Vital::GetEnergyShieldAttribute()),
			  ShieldRate, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoDelayRechargesAtFullRateTest,
	"Cataclysm.ShieldKeystones.NoDelayRechargesAtTheFullRateInsideTheWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The flag the enchantment "Energy shield regeneration begins immediately after
 * taking damage with no delay" sets. Issue #1833, the small engine halves. That
 * the worn row sets it is
 * `Cataclysm.Enchantments.TheNoShieldDelayRowSetsItsFlag`.
 *
 * THE WHOLE RATE INSIDE THE WAIT, where Ablative supplies half of it: ten a
 * second one second after being hurt. And with Ablative held as well, still
 * ten and not five, because no wait at all is the stronger of the two.
 */
bool FCataclysmNoDelayRechargesAtFullRateTest::RunTest(const FString&)
{
	using namespace CataclysmShieldKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}
	MakeShieldedCharacter(Player);
	Write(Player, Vital::GetManaRegenAttribute(), 0.0f);

	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("without the flag nothing recharges inside the wait"),
			  Read(Player, Vital::GetEnergyShieldAttribute()), 0.0f, 0.001f);

	Write(Player, Combat::GetShieldRechargeHasNoDelayAttribute(), 1.0f);
	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("with it the shield recharges at its whole rate inside the wait"),
			  Read(Player, Vital::GetEnergyShieldAttribute()), ShieldRate, 0.001f);

	EmptyTheShield(Player);
	Write(Player, Combat::GetShieldRechargesWhileDamagedAttribute(), 1.0f);
	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("and with Ablative held as well, still the whole rate"),
			  Read(Player, Vital::GetEnergyShieldAttribute()), ShieldRate, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLongGameFeedsTheShieldTest,
	"Cataclysm.ShieldKeystones.TheLongGameFeedsTheShieldFromManaRegeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Ritualist_keystone_d_kA` The Long Game. Issue #1515.
 *
 * WHAT IT GRANTS IS READ INSIDE THE WAIT, WHICH IS THE RULING THIS TEST HOLDS
 * IN PLACE. `docs/DECISIONS.md` records it: mana regeneration is itself ungated
 * and the row makes mana regeneration the thing that acts, so the shield gain it
 * grants does not serve the shield's three-second wait. Inside the wait an
 * ordinary shield recharges nothing, so anything read there came from this node.
 *
 * IF IT WERE WIRED INSIDE THE WAIT INSTEAD, this test reads zero -- and that
 * build would be "increased Energy Shield Regeneration", which is
 * `Ritualist_basic_spine_008` Warded Mind, a BASIC node in the same tree.
 *
 * THE SHIELD'S OWN RATE IS LEFT ON AND CONTRIBUTES NOTHING HERE, because the
 * wait stops it. That is deliberate: zeroing it would have hidden a build that
 * paid the shield's rate inside the wait by mistake.
 */
bool FCataclysmLongGameFeedsTheShieldTest::RunTest(const FString&)
{
	using namespace CataclysmShieldKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}
	MakeShieldedCharacter(Player);

	// WITHOUT THE KEYSTONE, MANA REGENERATION FEEDS MANA AND NOTHING ELSE.
	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("without The Long Game the shield gets nothing from mana "
				   "regeneration"),
			  Read(Player, Vital::GetEnergyShieldAttribute()), 0.0f, 0.001f);
	TestEqual(TEXT("and the mana itself still came back in full"),
			  Read(Player, Vital::GetManaAttribute()), ManaRate, 0.001f);

	// WITH IT, HALF THE MANA RATE REACHES THE SHIELD, INSIDE THE WAIT.
	Write(Player, Combat::GetManaRegenRestoresShieldAttribute(), 1.0f);
	Write(Player, Vital::GetManaAttribute(), 0.0f);

	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("with The Long Game the shield gains half the mana rate "
				   "even inside the wait"),
			  Read(Player, Vital::GetEnergyShieldAttribute()),
			  ManaRate / 2.0f, 0.001f);

	// AND THE MANA IS NOT TAKEN TO PAY FOR IT. The row says mana regeneration
	// ALSO restores the shield, not instead.
	TestEqual(TEXT("and the mana regeneration itself is not reduced to pay for "
				   "it"),
			  Read(Player, Vital::GetManaAttribute()), ManaRate, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShieldKeystonesAreDistinctTest,
	"Cataclysm.ShieldKeystones.ThreeEnergyShieldKeystonesDoThreeDifferentThings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The three keystones are three separate rules. Issue #1515.
 *
 * WHAT THIS CATCHES THAT THE THREE TESTS ABOVE DO NOT. Each of those turns one
 * flag on and checks one behaviour appears. All three still pass if every flag
 * is wired to the same attribute, or if one flag reads another's: a build where
 * any flag turns on all three behaviours satisfies every one of them.
 *
 * SO EACH FLAG IS TURNED ON ALONE AND THE OTHER TWO BEHAVIOURS ARE ASSERTED
 * ABSENT. That is the claim "three different things" actually makes, and it is
 * the one a player holding exactly one of them depends on.
 *
 * THE THREE READINGS ARE DELIBERATELY DIFFERENT NUMBERS -- 400 absorbed, 5 a
 * second, 4 a second -- so no two can be confused for each other and a reading
 * cannot pass by coincidence.
 */
bool FCataclysmShieldKeystonesAreDistinctTest::RunTest(const FString&)
{
	using namespace CataclysmShieldKeystoneTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}
	MakeShieldedCharacter(Player);
	Write(Player, Vital::GetMaxHealthAttribute(), 100000.0f);
	Write(Player, Vital::GetHealthAttribute(), 100000.0f);

	const FGameplayAttribute Absorbs =
		Combat::GetShieldAbsorbsWhatBypassesItAttribute();
	const FGameplayAttribute Recharges =
		Combat::GetShieldRechargesWhileDamagedAttribute();
	const FGameplayAttribute FromMana =
		Combat::GetManaRegenRestoresShieldAttribute();

	const FGameplayAttribute Shield = Vital::GetEnergyShieldAttribute();

	const auto OnlyThisOne = [&](const FGameplayAttribute& Wanted)
	{
		Write(Player, Absorbs, Wanted == Absorbs ? 1.0f : 0.0f);
		Write(Player, Recharges, Wanted == Recharges ? 1.0f : 0.0f);
		Write(Player, FromMana, Wanted == FromMana ? 1.0f : 0.0f);
	};

	// --- WARDED ALONE ------------------------------------------------------
	OnlyThisOne(Absorbs);

	Write(Player, Shield, 400.0f);
	TestEqual(TEXT("Warded alone absorbs a tick that would bypass the shield"),
			  ResolveBypassing(Player, 1000.0f, /*bTick=*/true).AbsorbedByShield, 400.0f);

	Write(Player, Shield, 0.0f);
	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("and Warded alone recharges nothing inside the wait, "
				   "neither Ablative's half rate nor The Long Game's share"),
			  Read(Player, Shield), 0.0f, 0.001f);

	// --- ABLATIVE ALONE ----------------------------------------------------
	OnlyThisOne(Recharges);

	Write(Player, Shield, 400.0f);
	TestEqual(TEXT("Ablative alone absorbs none of a tick that would bypass the shield"),
			  ResolveBypassing(Player, 1000.0f, /*bTick=*/true).AbsorbedByShield, 0.0f);

	Write(Player, Shield, 0.0f);
	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("and Ablative alone recharges exactly half the shield's own "
				   "rate, with nothing added from mana"),
			  Read(Player, Shield), ShieldRate / 2.0f, 0.001f);

	// --- THE LONG GAME ALONE -----------------------------------------------
	OnlyThisOne(FromMana);

	Write(Player, Shield, 400.0f);
	TestEqual(TEXT("The Long Game alone absorbs none of a tick that would bypass the shield"),
			  ResolveBypassing(Player, 1000.0f, /*bTick=*/true).AbsorbedByShield, 0.0f);

	Write(Player, Shield, 0.0f);
	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("and The Long Game alone gives half the MANA rate inside "
				   "the wait, not half the shield's"),
			  Read(Player, Shield), ManaRate / 2.0f, 0.001f);

	// --- AND TWO TOGETHER ADD UP -------------------------------------------
	// Not a fourth rule, but it says the two that share the regeneration step
	// are two terms rather than one overriding the other.
	Write(Player, Recharges, 1.0f);
	Write(Player, Shield, 0.0f);
	RunOneSecond(Player, JustHurt);
	TestEqual(TEXT("Ablative and The Long Game together give both amounts"),
			  Read(Player, Shield), ShieldRate / 2.0f + ManaRate / 2.0f, 0.001f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
