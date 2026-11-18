// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmHitLog.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The hit log: one line for every blow the player's character takes, and a death line that names the blow that killed.
 * Issue #2749 slice 1, ruled by the coordinating session on 2026-11-18 (B1, Q2, Q3). See `FCataclysmHitLog`.
 *
 * THE FIRST TWO NEED NO WORLD: they read the words and the one-second totals straight off `FCataclysmHitLog`. The other
 * four put a real player character and a real creature in a world, strike with `UCataclysmSkillEffects::ApplyDirectDamage`
 * as the game does, and read what `LogCataclysm` printed.
 */
namespace CataclysmHitLogTest
{
	/**
	 * Every `LogCataclysm` line printed while it lives, with its verbosity. Named for this file because the unity build
	 * puts several test files in one translation unit; `CataclysmCharacterLevelTests.cpp` has its own `FCapture`.
	 */
	struct FHitLogCapture : public FOutputDevice
	{
		FHitLogCapture() { if (GLog) { GLog->AddOutputDevice(this); } }
		virtual ~FHitLogCapture() override { if (GLog) { GLog->RemoveOutputDevice(this); } }

		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == FName(TEXT("LogCataclysm")))
			{
				Lines.Add({FString(Text), Verbosity});
			}
		}

		/** The lines holding every one of `Needles`, in the order printed. */
		TArray<TPair<FString, ELogVerbosity::Type>> Holding(std::initializer_list<FString> Needles)
		{
			if (GLog)
			{
				GLog->Flush();
			}
			TArray<TPair<FString, ELogVerbosity::Type>> Found;
			for (const TPair<FString, ELogVerbosity::Type>& Line : Lines)
			{
				bool bAll = true;
				for (const FString& Needle : Needles)
				{
					bAll = bAll && Line.Key.Contains(Needle, ESearchCase::CaseSensitive);
				}
				if (bAll)
				{
					Found.Add(Line);
				}
			}
			return Found;
		}

		/** Where in the printed order the first line holding `Needle` is, or -1. */
		int32 IndexOf(const FString& Needle) const
		{
			return Lines.IndexOfByPredicate([&Needle](const TPair<FString, ELogVerbosity::Type>& Line)
			{
				return Line.Key.Contains(Needle, ESearchCase::CaseSensitive);
			});
		}

		TArray<TPair<FString, ELogVerbosity::Type>> Lines;
	};

	/** A player character with its ability system on its player state, as `CataclysmDeathTests.cpp` builds one. */
	ACataclysmPlayerCharacter* SpawnHitLogPlayer(UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Player =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (State == nullptr || Player == nullptr)
		{
			return nullptr;
		}
		Player->SetPlayerState(State);
		Player->OnRep_PlayerState();
		return Player;
	}

	/** A creature on the monsters' side with health, at a rarity step. */
	ACataclysmEnemyCharacter* SpawnHitLogCreature(UWorld* World, int32 RarityStep)
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Creature != nullptr)
		{
			Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Creature->SetHealth(1000.0f);
			// READ BY THE HIT LOG ONLY, so it is written straight onto the creature rather than through the rarity
			// ladder, which would also change its health and its damage.
			Creature->RarityStep = RarityStep;
		}
		return Creature;
	}

	float HitLogHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** A damage over time tick of `Ailment` from nobody, landing `Amount` on health. */
	FCataclysmHitNotice HitLogTick(const FGameplayTagContainer& Granted, float Amount)
	{
		FCataclysmHitNotice Notice;
		Notice.Landed = Amount;
		Notice.DealtToHealth = Amount;
		Notice.bDamageOverTime = true;
		Notice.GrantedTags = &Granted;
		return Notice;
	}
}

#define CATACLYSM_HIT_LOG_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * The words: who struck, with its rarity; what struck; how much landed and how much reached health; the flags B1 names;
 * and the health before. Prints nothing; every figure is given.
 */
CATACLYSM_HIT_LOG_TEST(FCataclysmHitLogWordsTest,
	"Cataclysm.HitLog.TheLineSaysWhoWhatHowMuchAndTheFlags")
{
	using FBlow = FCataclysmHitLog::FBlow;

	FBlow Struck;
	Struck.Attacker = TEXT("CataclysmBruteCharacter_3 (Boss)");
	Struck.What = TEXT("melee attack");
	Struck.Landed = 114.0f;
	Struck.ToHealth = 100.0f;
	Struck.bCritical = true;
	Struck.HealthBefore = 130.0f;
	Struck.MaxHealth = 130.0f;
	TestEqual(TEXT("a blow"), FCataclysmHitLog::Line(Struck),
			  FString(TEXT("HIT: took 114 from CataclysmBruteCharacter_3 (Boss), melee attack: 100 to health; critical; "
						   "health 130 of 130 before it")));

	FBlow Second;
	Second.Attacker = TEXT("CataclysmImpCharacter_9 (Legendary)");
	Second.What = TEXT("burn");
	Second.Landed = 27.0f;
	Second.ToHealth = 27.0f;
	Second.bDamageOverTime = true;
	Second.HealthBefore = 60.0f;
	Second.MaxHealth = 130.0f;
	Second.Ticks = 4;
	TestEqual(TEXT("a second of ticks"), FCataclysmHitLog::Line(Second),
			  FString(TEXT("HIT: took 27 from CataclysmImpCharacter_9 (Legendary), burn, 4 ticks within one second: 27 "
						   "to health; damage over time; health 60 of 130 before the first")));

	// SMALL FIGURES KEEP A DECIMAL, so a blow a shield took whole does not read as nought landed.
	FBlow Returned;
	Returned.Attacker = TEXT("CataclysmSuccubusCharacter_1 (Legendary)");
	Returned.What = TEXT("attack");
	Returned.Landed = 6.5f;
	Returned.bBlocked = true;
	Returned.bRetaliation = true;
	Returned.HealthBefore = 9.5f;
	Returned.MaxHealth = 130.0f;
	TestEqual(TEXT("a returned blow a shield took"), FCataclysmHitLog::Line(Returned),
			  FString(TEXT("HIT: took 6.5 from CataclysmSuccubusCharacter_1 (Legendary), attack: 0.0 to health; blocked; "
						   "reflected or retaliation; health 9.5 of 130 before it")));

	// WHO: nobody, itself, and an actor that is not a creature, by its object name.
	const AActor* Someone = GetDefault<AActor>();
	TestEqual(TEXT("no attacker"), FCataclysmHitLog::AttackerText(nullptr, Someone), FString(TEXT("nobody")));
	TestEqual(TEXT("the character itself"), FCataclysmHitLog::AttackerText(Someone, Someone), FString(TEXT("itself")));
	TestEqual(TEXT("another actor"), FCataclysmHitLog::AttackerText(Someone, nullptr), Someone->GetName());

	// WHAT: a skill by name; the ailment of a tick, with the skill that applied it when one did; and a blow with no
	// skill by the announcement's own facts.
	FCataclysmHitNotice Notice;
	TestEqual(TEXT("no skill and no kind"), FCataclysmHitLog::WhatText(Notice), FString(TEXT("attack")));
	Notice.bIsMelee = true;
	TestEqual(TEXT("melee"), FCataclysmHitLog::WhatText(Notice), FString(TEXT("melee attack")));
	Notice.bIsMelee = false;
	Notice.bIsRanged = true;
	TestEqual(TEXT("ranged"), FCataclysmHitLog::WhatText(Notice), FString(TEXT("ranged attack")));
	Notice.bIsSpell = true;
	TestEqual(TEXT("a spell that fires a projectile is a spell"), FCataclysmHitLog::WhatText(Notice),
			  FString(TEXT("spell")));
	Notice.SkillName = FName(TEXT("Molten Cleave"));
	TestEqual(TEXT("a skill by name"), FCataclysmHitLog::WhatText(Notice), FString(TEXT("Molten Cleave")));

	FGameplayTagContainer Burning;
	const FGameplayTag Burn = FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Burn")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: the burn tag exists"), Burn.IsValid()))
	{
		return false;
	}
	Burning.AddTag(Burn);
	FCataclysmHitNotice Tick = CataclysmHitLogTest::HitLogTick(Burning, 3.0f);
	TestEqual(TEXT("a tick by its ailment"), FCataclysmHitLog::WhatText(Tick), FString(TEXT("burn")));
	Tick.SkillName = FName(TEXT("Molten Cleave"));
	TestEqual(TEXT("and by the skill that applied it"), FCataclysmHitLog::WhatText(Tick),
			  FString(TEXT("burn of Molten Cleave")));
	const FGameplayTagContainer Nothing;
	TestEqual(TEXT("a tick naming no ailment"), FCataclysmHitLog::WhatText(CataclysmHitLogTest::HitLogTick(Nothing, 3.0f)),
			  FString(TEXT("damage over time")));
	return true;
}

/**
 * Damage over time is added up per attacker and ailment for one second and said once; any other blow is said at once;
 * a blow that landed nought says nothing; and what is still open is said whole at a death or the end of play. Every
 * moment is given, so no world and no timer is needed.
 */
CATACLYSM_HIT_LOG_TEST(FCataclysmHitLogTotalsTest,
	"Cataclysm.HitLog.DamageOverTimeIsAddedUpPerSourceEachSecond")
{
	using CataclysmHitLogTest::HitLogTick;

	const FGameplayTag Burn = FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Burn")), false);
	const FGameplayTag Bleed = FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Bleed")), false);
	if (!TestTrue(TEXT("set-up: the burn and bleed tags exist"), Burn.IsValid() && Bleed.IsValid()))
	{
		return false;
	}
	FGameplayTagContainer Burning;
	Burning.AddTag(Burn);
	FGameplayTagContainer Bleeding;
	Bleeding.AddTag(Bleed);

	FCataclysmHitLog Log;
	const TArray<FString> First = Log.Note(HitLogTick(Burning, 5.0f), 100.0f, 130.0f, 10.0);
	const TArray<FString> Second = Log.Note(HitLogTick(Burning, 5.0f), 95.0f, 130.0f, 10.4);
	const TArray<FString> OtherAilment = Log.Note(HitLogTick(Bleeding, 3.0f), 90.0f, 130.0f, 10.5);

	FCataclysmHitNotice Struck;
	Struck.Landed = 20.0f;
	Struck.DealtToHealth = 20.0f;
	const TArray<FString> AtOnce = Log.Note(Struck, 87.0f, 130.0f, 10.6);

	FCataclysmHitNotice Nought;
	const TArray<FString> NoughtLanded = Log.Note(Nought, 67.0f, 130.0f, 10.7);

	const TArray<FString> NotYet = Log.Due(10.9);
	const TArray<FString> OneSecond = Log.Due(11.0);
	const TArray<FString> Reopened = Log.Note(HitLogTick(Burning, 5.0f), 67.0f, 130.0f, 11.2);
	const bool bOpenBeforeTheEnd = Log.HasOpenTotals();
	const TArray<FString> AtTheEnd = Log.All();

	AddInfo(FString::Printf(TEXT("HIT LOG TOTALS: at once [%s]; after one second [%s]; at the end [%s]"),
							*FString::Join(AtOnce, TEXT(" | ")), *FString::Join(OneSecond, TEXT(" | ")),
							*FString::Join(AtTheEnd, TEXT(" | "))));

	TestEqual(TEXT("ticks inside their second say nothing"), First.Num() + Second.Num() + OtherAilment.Num(), 0);
	TestEqual(TEXT("a blow that is not damage over time is said at once, alone"), AtOnce.Num(), 1);
	if (AtOnce.Num() == 1)
	{
		TestEqual(TEXT("as itself"), AtOnce[0],
				  FString(TEXT("HIT: took 20 from nobody, attack: 20 to health; health 87 of 130 before it")));
	}
	TestEqual(TEXT("a blow that landed nought says nothing"), NoughtLanded.Num(), 0);
	TestEqual(TEXT("CONTROL: nothing is due before a total has run its second"), NotYet.Num(), 0);
	TestEqual(TEXT("the burn's second has run; the bleed's has not"), OneSecond.Num(), 1);
	if (OneSecond.Num() == 1)
	{
		TestEqual(TEXT("the burn's two ticks, added up, with the health before the first"), OneSecond[0],
				  FString(TEXT("HIT: took 10 from nobody, burn, 2 ticks within one second: 10 to health; damage over "
							   "time; health 100 of 130 before the first")));
	}
	TestEqual(TEXT("a tick after its total closed opens a new one"), Reopened.Num(), 0);
	TestTrue(TEXT("so totals are open before the end"), bOpenBeforeTheEnd);
	TestEqual(TEXT("the end says every open total, oldest first"), AtTheEnd.Num(), 2);
	if (AtTheEnd.Num() == 2)
	{
		TestTrue(TEXT("the bleed first"), AtTheEnd[0].StartsWith(TEXT("HIT: took 3.0 from nobody, bleed, 1 tick ")));
		TestTrue(TEXT("then the new burn"), AtTheEnd[1].StartsWith(TEXT("HIT: took 5.0 from nobody, burn, 1 tick ")));
	}
	TestFalse(TEXT("and none is left open"), Log.HasOpenTotals());
	return true;
}

/**
 * A creature's blow on the player says one HIT line naming the creature and its rarity, at Display. CONTROL: the
 * player's blow on the creature says none, because the log is of blows the player takes.
 */
CATACLYSM_HIT_LOG_TEST(FCataclysmHitLogOneLineTest,
	"Cataclysm.HitLog.ACreaturesBlowOnThePlayerSaysOneLine")
{
	using namespace CataclysmHitLogTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnHitLogPlayer(World);
	ACataclysmEnemyCharacter* Creature = SpawnHitLogCreature(World, /*RarityStep=*/4);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a creature"), Creature))
	{
		return false;
	}

	FHitLogCapture Capture;
	const float HealthBefore = HitLogHealthOf(Player);
	UCataclysmSkillEffects::ApplyDirectDamage(Creature, Player, 5.0f);
	const float HealthAfter = HitLogHealthOf(Player);
	const TArray<TPair<FString, ELogVerbosity::Type>> Taken = Capture.Holding({TEXT("HIT: took ")});

	UCataclysmSkillEffects::ApplyDirectDamage(Player, Creature, 5.0f);
	const TArray<TPair<FString, ELogVerbosity::Type>> AfterDealing = Capture.Holding({TEXT("HIT: took ")});

	AddInfo(FString::Printf(TEXT("HIT LOG ONE LINE: health %.1f to %.1f; lines [%s]"), HealthBefore, HealthAfter,
							Taken.Num() > 0 ? *Taken[0].Key : TEXT("none")));

	TestTrue(TEXT("set-up: the blow reached the player"), HealthAfter < HealthBefore);
	if (TestEqual(TEXT("one line for one blow taken"), Taken.Num(), 1))
	{
		TestTrue(TEXT("naming the creature and its rarity"),
				 Taken[0].Key.Contains(FString::Printf(TEXT("from %s (Boss)"), *Creature->GetName())));
		TestTrue(TEXT("with the health before it"), Taken[0].Key.EndsWith(TEXT("before it")));
		TestEqual(TEXT("at Display"), static_cast<int32>(Taken[0].Value), static_cast<int32>(ELogVerbosity::Display));
	}
	TestEqual(TEXT("CONTROL: a blow the player dealt says no line"), AfterDealing.Num(), Taken.Num());
	return true;
}

/**
 * A blow returned for a blow taken carries the retaliation mark to the announcement, and its line says so; a plain blow
 * from the same creature carries neither. Paid by `UCataclysmRetaliation::PayReflected`, the path Thorns of Glass's
 * retaliation and a reflected melee hit both take.
 */
CATACLYSM_HIT_LOG_TEST(FCataclysmHitLogReturnedBlowTest,
	"Cataclysm.HitLog.AReturnedBlowIsMarkedAndAPlainOneIsNot")
{
	using namespace CataclysmHitLogTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnHitLogPlayer(World);
	ACataclysmEnemyCharacter* Creature = SpawnHitLogCreature(World, /*RarityStep=*/2);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	UAbilitySystemComponent* CreatureSystem = UCataclysmTargeting::AbilitySystemOf(Creature);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a creature"), Creature)
		|| !TestNotNull(TEXT("the announcer"), Events) || !TestNotNull(TEXT("the creature's ability system"), CreatureSystem))
	{
		return false;
	}

	// EVERY ANNOUNCED BLOW ON THE PLAYER, with whether it carried the mark.
	TArray<bool> Marks;
	const FDelegateHandle Listening = Events->OnHit.AddLambda([&Marks, Player](const FCataclysmHitNotice& Notice)
	{
		if (Notice.Target == Player && Notice.Landed > 0.0f)
		{
			Marks.Add(Notice.bRetaliation);
		}
	});
	ON_SCOPE_EXIT { Events->OnHit.Remove(Listening); };

	FHitLogCapture Capture;
	UCataclysmSkillEffects::ApplyDirectDamage(Creature, Player, 5.0f);
	const int32 PlainLines = Capture.Holding({TEXT("HIT: took "), TEXT("reflected or retaliation")}).Num();
	const float Returned = UCataclysmRetaliation::PayReflected(CreatureSystem, Creature, Player, 5.0f);
	const TArray<TPair<FString, ELogVerbosity::Type>> ReturnedLines =
		Capture.Holding({TEXT("HIT: took "), TEXT("reflected or retaliation")});

	AddInfo(FString::Printf(TEXT("HIT LOG RETURNED: %d blow(s) announced, marks %s; the payment took %.1f; line [%s]"),
							Marks.Num(),
							Marks.Num() == 2 ? *FString::Printf(TEXT("%d %d"), static_cast<int32>(Marks[0]),
																static_cast<int32>(Marks[1]))
											 : TEXT("?"),
							Returned, ReturnedLines.Num() > 0 ? *ReturnedLines[0].Key : TEXT("none")));

	if (TestEqual(TEXT("set-up: two blows on the player were announced"), Marks.Num(), 2))
	{
		TestFalse(TEXT("CONTROL: the plain blow carries no mark"), Marks[0]);
		TestTrue(TEXT("the returned blow carries the mark"), Marks[1]);
	}
	TestEqual(TEXT("CONTROL: the plain blow's line does not say it was returned"), PlainLines, 0);
	if (TestEqual(TEXT("the returned blow's line says so"), ReturnedLines.Num(), 1))
	{
		TestTrue(TEXT("and names the creature that returned it"),
				 ReturnedLines[0].Key.Contains(FString::Printf(TEXT("from %s ("), *Creature->GetName())));
	}
	return true;
}

/**
 * A death by a blow: the blow's HIT line, then the death line at Display naming that blow as the killing blow.
 */
CATACLYSM_HIT_LOG_TEST(FCataclysmHitLogKillingBlowTest,
	"Cataclysm.HitLog.TheDeathLineNamesTheKillingBlow")
{
	using namespace CataclysmHitLogTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnHitLogPlayer(World);
	ACataclysmEnemyCharacter* Creature = SpawnHitLogCreature(World, /*RarityStep=*/0);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a creature"), Creature))
	{
		return false;
	}

	FHitLogCapture Capture;
	UCataclysmSkillEffects::ApplyDirectDamage(Creature, Player, 100000.0f);
	const FString From = FString::Printf(TEXT("from %s ("), *Creature->GetName());
	const TArray<TPair<FString, ELogVerbosity::Type>> Deaths = Capture.Holding({TEXT(" died at ")});
	const TArray<TPair<FString, ELogVerbosity::Type>> Named =
		Capture.Holding({TEXT(" died at "), TEXT("; the killing blow: "), From});
	const int32 HitAt = Capture.IndexOf(TEXT("HIT: took "));
	const int32 DeathAt = Capture.IndexOf(TEXT(" died at "));

	AddInfo(FString::Printf(TEXT("HIT LOG KILLING BLOW: dead %s; death line [%s]"),
							UCataclysmSkillEffects::IsDead(Player) ? TEXT("yes") : TEXT("NO"),
							Deaths.Num() > 0 ? *Deaths[0].Key : TEXT("none")));

	TestTrue(TEXT("set-up: the blow killed the player"), UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("one death line"), Deaths.Num(), 1);
	if (TestEqual(TEXT("naming the creature's blow as the killing blow"), Named.Num(), 1))
	{
		TestEqual(TEXT("at Display"), static_cast<int32>(Named[0].Value), static_cast<int32>(ELogVerbosity::Display));
	}
	TestTrue(TEXT("after the blow's own line"), HitAt >= 0 && DeathAt > HitAt);
	return true;
}

/**
 * A death no blow caused -- health written to nought -- says no blow killed it, and names the last blow taken.
 */
CATACLYSM_HIT_LOG_TEST(FCataclysmHitLogNoBlowTest,
	"Cataclysm.HitLog.ADeathNoBlowCausedSaysSo")
{
	using namespace CataclysmHitLogTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnHitLogPlayer(World);
	ACataclysmEnemyCharacter* Creature = SpawnHitLogCreature(World, /*RarityStep=*/0);
	UAbilitySystemComponent* PlayerSystem = UCataclysmTargeting::AbilitySystemOf(Player);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a creature"), Creature)
		|| !TestNotNull(TEXT("the player's ability system"), PlayerSystem))
	{
		return false;
	}

	FHitLogCapture Capture;
	UCataclysmSkillEffects::ApplyDirectDamage(Creature, Player, 5.0f);
	const bool bAliveAfterTheBlow = !UCataclysmSkillEffects::IsDead(Player);
	PlayerSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);

	const TArray<TPair<FString, ELogVerbosity::Type>> Deaths = Capture.Holding({TEXT(" died at ")});
	const TArray<TPair<FString, ELogVerbosity::Type>> Said = Capture.Holding(
		{TEXT(" died at "), TEXT("; no blow killed it: "), TEXT("the last blow taken was "),
		 FString::Printf(TEXT("from %s ("), *Creature->GetName())});

	AddInfo(FString::Printf(TEXT("HIT LOG NO BLOW: dead %s; death line [%s]"),
							UCataclysmSkillEffects::IsDead(Player) ? TEXT("yes") : TEXT("NO"),
							Deaths.Num() > 0 ? *Deaths[0].Key : TEXT("none")));

	TestTrue(TEXT("set-up: the blow alone did not kill"), bAliveAfterTheBlow);
	TestTrue(TEXT("set-up: writing health to nought killed"), UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("one death line"), Deaths.Num(), 1);
	TestEqual(TEXT("saying no blow killed it, and naming the last blow taken"), Said.Num(), 1);
	TestEqual(TEXT("CONTROL: it names no killing blow"), Capture.Holding({TEXT("the killing blow: ")}).Num(), 0);
	return true;
}

#undef CATACLYSM_HIT_LOG_TEST

#endif // WITH_AUTOMATION_TESTS
