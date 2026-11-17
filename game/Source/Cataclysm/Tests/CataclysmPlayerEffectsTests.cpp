// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmPlayerEffects.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Issue #1591: the list of what is affecting the player, drawn above the player's own bars. Ruled 2026-11-01 by the
 * coordinating session: debuffs first, then buffs; each entry with its stacks and its seconds left; every text the
 * line above the bars showed before, in that text's own words; Forced March's stacks; at most eight lines and then
 * "+N more". The list is read here through `UCataclysmPlayerEffects`, because the heads-up display never draws under
 * the automation suite.
 */
namespace CataclysmPlayerEffectsTest
{
	/** A player character on its player state, as the Withering tests make one. */
	ACataclysmPlayerCharacter* PlayerEffectsPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Character = World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Character)
		{
			return nullptr;
		}
		Character->SetPlayerState(PlayerState);
		Character->OnRep_PlayerState();
		if (UCataclysmAbilitySystemComponent* System = PlayerState->GetCataclysmAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		}
		return Character;
	}

	/** A creature five metres away, to lay things on the player. */
	ACataclysmEnemyCharacter* PlayerEffectsCreature(UWorld* World)
	{
		FActorSpawnParameters Always;
		Always.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Always);
	}

	UCataclysmAbilitySystemComponent* PlayerEffectsSystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	FGameplayTag PlayerEffectsTagNamed(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/** Every entry's text, joined by ", ", so one assertion reads the whole list and its order. */
	FString PlayerEffectsTexts(const TArray<FCataclysmPlayerEffect>& Entries)
	{
		TArray<FString> Texts;
		for (const FCataclysmPlayerEffect& Entry : Entries)
		{
			Texts.Add(UCataclysmPlayerEffects::TextFor(Entry));
		}
		return FString::Join(Texts, TEXT(", "));
	}

	/** Every entry's kind, "D" for a debuff and "B" for a buff, joined by spaces. */
	FString PlayerEffectsKinds(const TArray<FCataclysmPlayerEffect>& Entries)
	{
		TArray<FString> Kinds;
		for (const FCataclysmPlayerEffect& Entry : Entries)
		{
			Kinds.Add(Entry.bDebuff ? TEXT("D") : TEXT("B"));
		}
		return FString::Join(Kinds, TEXT(" "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerEffectsNothingTest,
	"Cataclysm.PlayerEffects.NothingAppliedListsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerEffectsNothingTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerEffectsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = PlayerEffectsPlayer(World);
	if (!TestNotNull(TEXT("set-up: a player with an ability system"), PlayerEffectsSystemOf(Player)))
	{
		return false;
	}

	// THE CONTROL EVERY OTHER TEST HERE LEANS ON: a fresh player is listed as carrying nothing, so what those tests
	// find was put there by them.
	TestEqual(TEXT("a player with nothing applied lists nothing"),
			  PlayerEffectsTexts(UCataclysmPlayerEffects::EntriesFor(Player)), FString());
	TestEqual(TEXT("and the screen shows no line"),
			  UCataclysmPlayerEffects::ShownFrom(UCataclysmPlayerEffects::EntriesFor(Player)).Num(), 0);
	TestEqual(TEXT("no actor lists nothing"), UCataclysmPlayerEffects::EntriesFor(nullptr).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerEffectsBleedTest,
	"Cataclysm.PlayerEffects.ABleedIsListedAsADebuffWithItsSecondsLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerEffectsBleedTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerEffectsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = PlayerEffectsPlayer(World);
	ACataclysmEnemyCharacter* Creature = PlayerEffectsCreature(World);
	if (!TestTrue(TEXT("set-up: a player with an ability system, and a creature"),
				  PlayerEffectsSystemOf(Player) && Creature))
	{
		return false;
	}
	TestEqual(TEXT("control: before the bleed, nothing is listed"), UCataclysmPlayerEffects::EntriesFor(Player).Num(),
			  0);

	// A REAL BLEED, BY THE CALL EVERY BLEED IN THE GAME GOES THROUGH: one point a tick, for four seconds.
	if (!TestTrue(TEXT("set-up: the bleed was applied"),
				  UCataclysmSkillEffects::ApplyDamageOverTime(Creature, Player, 1.0f, 4.0f,
															  UCataclysmDebuffs::BleedTag())))
	{
		return false;
	}

	const TArray<FCataclysmPlayerEffect> Entries = UCataclysmPlayerEffects::EntriesFor(Player);
	if (!TestEqual(TEXT("the bleed is the one entry"), Entries.Num(), 1))
	{
		return false;
	}
	const FCataclysmPlayerEffect& Bleed = Entries[0];
	TestEqual(TEXT("it is named Bleed"), Bleed.Name, FString(TEXT("Bleed")));
	TestTrue(TEXT("it is a debuff"), Bleed.bDebuff);
	TestTrue(TEXT("it has seconds left"), Bleed.SecondsLeft > 0.0f);
	TestEqual(TEXT("its text is the name and the seconds, rounded up"), UCataclysmPlayerEffects::TextFor(Bleed),
			  FString::Printf(TEXT("Bleed %ds"), FMath::CeilToInt(Bleed.SecondsLeft)));

	// AND THE SECONDS ARE A LIVE CLOCK: a second and a half later there is that much less.
	World->TimeSeconds += 1.5f;
	const TArray<FCataclysmPlayerEffect> Later = UCataclysmPlayerEffects::EntriesFor(Player);
	if (TestEqual(TEXT("1.5 seconds later the bleed is still the one entry"), Later.Num(), 1))
	{
		TestEqual(TEXT("with 1.5 seconds less left"), Later[0].SecondsLeft, Bleed.SecondsLeft - 1.5f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerEffectsCrowdControlTest,
	"Cataclysm.PlayerEffects.ACrowdControlStateIsListed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerEffectsCrowdControlTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerEffectsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = PlayerEffectsPlayer(World);
	ACataclysmEnemyCharacter* Creature = PlayerEffectsCreature(World);
	UCataclysmAbilitySystemComponent* System = PlayerEffectsSystemOf(Player);
	if (!TestTrue(TEXT("set-up: a player with an ability system, and a creature"), System && Creature))
	{
		return false;
	}

	// A KNOCKDOWN FOR TWO SECONDS, LAID AS EVERY KNOCKDOWN IS, and a pin as a bare tag: neither is under a debuff root,
	// so before this list nothing on screen named either.
	if (!TestTrue(TEXT("set-up: the knockdown was laid"),
				  UCataclysmSkillEffects::ApplyTagForDuration(Creature, Player, UCataclysmSkillEffects::KnockedDownTag(),
															  2.0f)))
	{
		return false;
	}
	System->AddLooseGameplayTag(UCataclysmSkillEffects::PinnedTag());

	const TArray<FCataclysmPlayerEffect> Entries = UCataclysmPlayerEffects::EntriesFor(Player);
	if (!TestEqual(TEXT("two entries"), Entries.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the knockdown first, named from its tag"), Entries[0].Name, FString(TEXT("Knocked Down")));
	TestTrue(TEXT("a debuff, with seconds left"), Entries[0].bDebuff && Entries[0].SecondsLeft > 0.0f);
	TestEqual(TEXT("its text"), UCataclysmPlayerEffects::TextFor(Entries[0]),
			  FString::Printf(TEXT("Knocked Down %ds"), FMath::CeilToInt(Entries[0].SecondsLeft)));
	TestEqual(TEXT("then the pin, which has no end time and so shows none"), UCataclysmPlayerEffects::TextFor(Entries[1]),
			  FString(TEXT("Pinned")));
	TestTrue(TEXT("and is a debuff"), Entries[1].bDebuff);

	System->RemoveLooseGameplayTag(UCataclysmSkillEffects::PinnedTag());
	TestEqual(TEXT("the pin taken off, it is not listed"), PlayerEffectsTexts(UCataclysmPlayerEffects::EntriesFor(Player)),
			  UCataclysmPlayerEffects::TextFor(Entries[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerEffectsOrderTest,
	"Cataclysm.PlayerEffects.ABuffIsListedAfterEveryDebuff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerEffectsOrderTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerEffectsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = PlayerEffectsPlayer(World);
	UCataclysmAbilitySystemComponent* System = PlayerEffectsSystemOf(Player);
	const FGameplayTag Commander = PlayerEffectsTagNamed(TEXT("Status.Buff.Commander"));
	const FGameplayTag Quarry = PlayerEffectsTagNamed(TEXT("Status.Debuff.Quarry"));
	if (!TestTrue(TEXT("set-up: a player with an ability system, and both tags registered"),
				  System && Commander.IsValid() && Quarry.IsValid()))
	{
		return false;
	}

	// THE BUFF PUT ON FIRST, so an order that followed arrival would list it first.
	System->AddLooseGameplayTag(Commander);
	TestEqual(TEXT("control: the buff alone is listed, as a buff"),
			  PlayerEffectsKinds(UCataclysmPlayerEffects::EntriesFor(Player)), FString(TEXT("B")));
	System->AddLooseGameplayTag(Quarry);
	System->AddLooseGameplayTag(UCataclysmSkillEffects::StaggeredTag());

	const TArray<FCataclysmPlayerEffect> Entries = UCataclysmPlayerEffects::EntriesFor(Player);
	TestEqual(TEXT("the stagger, then the curse, then the buff"), PlayerEffectsTexts(Entries),
			  FString(TEXT("Staggered, Quarry, Commander")));
	TestEqual(TEXT("two debuffs, then one buff"), PlayerEffectsKinds(Entries), FString(TEXT("D D B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerEffectsCapTest,
	"Cataclysm.PlayerEffects.PastEightTheRestAreCountedAsMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerEffectsCapTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerEffectsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = PlayerEffectsPlayer(World);
	UCataclysmAbilitySystemComponent* System = PlayerEffectsSystemOf(Player);
	if (!TestNotNull(TEXT("set-up: a player with an ability system"), System))
	{
		return false;
	}

	// TEN NAMED CURSES NO TEXT FUNCTION NAMES, AND ONE BUFF: eleven entries.
	TArray<FGameplayTag> Curses;
	for (const TCHAR* Name : {TEXT("Status.Debuff.AbyssalAura"), TEXT("Status.Debuff.BattleCry"),
							  TEXT("Status.Debuff.Beguiling"), TEXT("Status.Debuff.Contagion"),
							  TEXT("Status.Debuff.Echoing"), TEXT("Status.Debuff.FinalCurse"),
							  TEXT("Status.Debuff.Haunting"), TEXT("Status.Debuff.InfernalBrand"),
							  TEXT("Status.Debuff.Manic"), TEXT("Status.Debuff.Nullifying")})
	{
		Curses.Add(PlayerEffectsTagNamed(Name));
	}
	const FGameplayTag Commander = PlayerEffectsTagNamed(TEXT("Status.Buff.Commander"));
	for (const FGameplayTag& Tag : Curses)
	{
		System->AddLooseGameplayTag(Tag);
	}
	System->AddLooseGameplayTag(Commander);
	if (!TestEqual(TEXT("set-up: eleven entries"), UCataclysmPlayerEffects::EntriesFor(Player).Num(), 11))
	{
		return false;
	}

	const TArray<FCataclysmPlayerEffect> Shown =
		UCataclysmPlayerEffects::ShownFrom(UCataclysmPlayerEffects::EntriesFor(Player));
	TestEqual(TEXT("eight entries and then the count of the three left out"), PlayerEffectsTexts(Shown),
			  FString(TEXT("Abyssal Aura, Battle Cry, Beguiling, Contagion, Echoing, Final Curse, Haunting, "
						   "Infernal Brand, +3 more")));
	TestEqual(TEXT("the eight are debuffs and the count line is drawn as a buff"), PlayerEffectsKinds(Shown),
			  FString(TEXT("D D D D D D D D B")));

	// AT EXACTLY EIGHT NOTHING IS LEFT OUT, so no count line: the boundary, reached.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		System->RemoveLooseGameplayTag(Curses[Index]);
	}
	const TArray<FCataclysmPlayerEffect> AtEight =
		UCataclysmPlayerEffects::ShownFrom(UCataclysmPlayerEffects::EntriesFor(Player));
	TestEqual(TEXT("at eight entries, eight lines"), AtEight.Num(), 8);
	TestEqual(TEXT("and the last is the buff, not a count"), PlayerEffectsTexts(AtEight),
			  FString(TEXT("Contagion, Echoing, Final Curse, Haunting, Infernal Brand, Manic, Nullifying, Commander")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerEffectsWitheringTest,
	"Cataclysm.PlayerEffects.WitheringIsListedInTheOverlaysOwnWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerEffectsWitheringTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerEffectsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = PlayerEffectsPlayer(World);
	UCataclysmAbilitySystemComponent* System = PlayerEffectsSystemOf(Player);
	const FGameplayTag Withering = PlayerEffectsTagNamed(TEXT("Status.Debuff.Withering"));
	if (!TestTrue(TEXT("set-up: a player with an ability system, and Withering registered"),
				  System && Withering.IsValid()))
	{
		return false;
	}

	// THREE STACKS AND THE STATUS, as Famine's Withering Blows leave them; the status as a bare tag, so it has no
	// end time and the entry is the text's words alone.
	for (int32 Note = 0; Note < 3; ++Note)
	{
		UCataclysmStacks::NoteWithering(System);
	}
	System->AddLooseGameplayTag(Withering);
	const FString Overlays = UCataclysmCombatOverlay::WitheringTextFor(Player);
	if (!TestEqual(TEXT("set-up: the overlay's own text"), Overlays, FString(TEXT("Withering 3/10"))))
	{
		return false;
	}

	const TArray<FCataclysmPlayerEffect> Entries = UCataclysmPlayerEffects::EntriesFor(Player);
	TestEqual(TEXT("the list holds the overlay's words, and nothing else"), PlayerEffectsTexts(Entries), Overlays);
	TestTrue(TEXT("as a debuff"), Entries.Num() == 1 && Entries[0].bDebuff);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPlayerEffectsForcedMarchTest,
	"Cataclysm.PlayerEffects.ForcedMarchStacksAreReadThroughTheList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPlayerEffectsForcedMarchTest::RunTest(const FString&)
{
	using namespace CataclysmPlayerEffectsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	ACataclysmPlayerCharacter* Player = PlayerEffectsPlayer(World);
	UCataclysmAbilitySystemComponent* System = PlayerEffectsSystemOf(Player);
	if (!TestTrue(TEXT("set-up: a dungeon game mode, and a player with an ability system"), Mode && System))
	{
		return false;
	}
	const FName ForcedMarch(UCataclysmDungeonModifierEffects::ForcedMarchKey);

	// THE PLAYER MOVES NOW AND THEN STANDS STILL. 4.5 seconds is two stacks: one at three seconds, one more at four.
	System->NoteMovedMetres(1.0f);
	World->TimeSeconds += 4.5f;
	TestEqual(TEXT("control: on a floor without the row, nothing is listed"),
			  PlayerEffectsTexts(UCataclysmPlayerEffects::EntriesFor(Player)), FString());

	Mode->FloorBrief.Modifiers.Add(ForcedMarch);
	const TArray<FCataclysmPlayerEffect> Entries = UCataclysmPlayerEffects::EntriesFor(Player);
	TestEqual(TEXT("with the row, two stacks of five"), PlayerEffectsTexts(Entries),
			  FString(TEXT("Forced March 2/5")));
	TestTrue(TEXT("as a debuff"), Entries.Num() == 1 && Entries[0].bDebuff);

	World->TimeSeconds += 10.0f;
	TestEqual(TEXT("standing still longer reaches the cap and stays there"),
			  PlayerEffectsTexts(UCataclysmPlayerEffects::EntriesFor(Player)), FString(TEXT("Forced March 5/5")));

	System->NoteMovedMetres(1.0f);
	TestEqual(TEXT("moving clears every stack, and the entry with them"),
			  PlayerEffectsTexts(UCataclysmPlayerEffects::EntriesFor(Player)), FString());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
