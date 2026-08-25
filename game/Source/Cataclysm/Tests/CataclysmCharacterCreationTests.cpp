// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmCharacterCreation.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmCharacterCreationWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * Character creation: choosing a starting weapon type and a damage type.
 *
 * WHAT THESE GUARD. Issue #50 and `docs/Cataclysm_GDD_v2.md` section IV. Before
 * this, two hard-coded strings on `UCataclysmWeaponSlotsComponent` were the whole
 * answer -- `StartingWeaponType` was `Greataxe` and `DamageType` was `Demonic` --
 * and both said in their own comments that they were standing in for a character
 * creator that did not exist.
 *
 * THE ONE THAT MATTERS MOST IS THE ONE ABOUT A CHARACTER THAT CHOSE NOTHING.
 * Every other automation test in this project stands a character up without
 * going anywhere near the creator, so if the defaults stopped answering, several
 * hundred tests would change behaviour at once and this feature would be the
 * cause. `ACharacterThatChoseNothingIsExactlyTheCharacterItWasBefore` is that
 * check, and it is deliberately the plainest test in the file.
 *
 * THE SECOND MOST IMPORTANT IS THE ONE THAT USES A REAL POSSESSED PAWN.
 * `TheChoiceReachesARealCharacter` drives `ApplyCreationChoice` on a spawned
 * player and reads what the character is actually holding afterwards. Every
 * other test here works on plain values, and the handoff of 2026-08-24 records
 * why that is not enough on its own: the Enemy Score award was fully covered by
 * tests that computed a score, and deleting the whole award from the one place
 * it happened passed all of them.
 *
 * WHAT IS DELIBERATELY NOT COVERED. Anything a person can see. The automation
 * command in `tools/unreal_build.py` passes `-nullrhi` and runs with no editor,
 * so `WBP_CharacterCreation` cannot be loaded and no widget draws. The screen's
 * logic is reached by constructing `UCataclysmCharacterCreationWidget` with no
 * Blueprint at all, which is why every bound pointer in it is checked before it
 * is used.
 */

namespace CataclysmCreationTest
{
	// BEGUN PLAY, BECAUSE A CHARACTER'S STARTING WEAPON IS PUT ON IN
	// BeginPlay. `ACataclysmPlayerCharacter::GiveStartingWeapon` runs from
	// there rather than from possession, so in a world that has not begun
	// play the character would be holding nothing and every test below
	// about what comes off would be testing an empty hand.
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/**
	 * A weapon skill matrix this test controls.
	 *
	 * BUILT RATHER THAN LOADED, so a case can be set up that the real matrix
	 * does not contain and so a change to the design workbook cannot silently
	 * change what these tests mean. The real matrix is exercised instead by
	 * `tools/tests/test_character_creation_matches_the_design.py`, which
	 * compares it against the design document.
	 *
	 * THE SIX SLOTS OF ONE PAIRING ARE THE INTERESTING PART. `Sword` and `War`
	 * carries four rows with a skill name written and two without, so
	 * `DesignedSkillCount` has something to count that is neither nothing nor
	 * everything.
	 */
	UDataTable* MakeWeaponSkillTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmWeaponSkillRow::StaticStruct();

		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,WeaponType,DamageType,Slot,SkillName,SkillDescription,Tags,Shape,ShapeParams,CritChancePercent,DamagePercent,Cooldown,ManaCost\r\n"
			// Sword: War with four skills written, Death with none.
			"War_Sword_Heavy,Sword,War,Heavy,Precise Cut,Cuts.,,Strike,Radius=4,-1,-1,-1,-1\r\n"
			"War_Sword_Special,Sword,War,Special,Wild Swing,Swings.,,Strike,Radius=4,-1,-1,-1,-1\r\n"
			"War_Sword_Support,Sword,War,Support,Guard,Guards.,,Strike,Radius=4,-1,-1,-1,-1\r\n"
			"War_Sword_Aura,Sword,War,Aura,Banner,Flies.,,Strike,Radius=4,-1,-1,-1,-1\r\n"
			"War_Sword_Ultimate,Sword,War,Ultimate,,,,Strike,Radius=4,-1,-1,-1,-1\r\n"
			"War_Sword_Movement,Sword,War,Movement,,,,Strike,Radius=4,-1,-1,-1,-1\r\n"
			"Death_Sword_Heavy,Sword,Death,Heavy,,,,Strike,Radius=4,-1,-1,-1,-1\r\n"
			// Staff: two casters and no War, which is the design's own rule --
			// "Wand and Staff are excluded from War, which has no caster build".
			"Death_Staff_Heavy,Staff,Death,Heavy,Grave Bolt,Bolts.,,Projectile,Range=8,-1,-1,-1,-1\r\n"
			"Demonic_Staff_Heavy,Staff,Demonic,Heavy,Hellfire,Burns.,,Projectile,Range=8,-1,-1,-1,-1\r\n"
			// Greataxe and Demonic: the pair a character has when nobody chose.
			"Demonic_Greataxe_Heavy,Greataxe,Demonic,Heavy,Cleave,Cleaves.,,Strike,Radius=4,-1,-1,-1,-1\r\n"
			// A Shield does carry skills. It is excluded for a different reason.
			"War_Shield_Heavy,Shield,War,Heavy,Shield Bash,Bashes.,,Strike,Radius=2,-1,-1,-1,-1\r\n"
			// The auras, which do not care what is held. Counting this row as a
			// weapon would give every weapon every damage type.
			"Void_All_Aura,All,Void,Aura,Dread,Spreads.,,Strike,Radius=4,-1,-1,-1,-1\r\n"));

		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/**
	 * An item bases table this test controls, with four weapon types in it.
	 *
	 * ONE OF THEM IS THE SHIELD, deliberately, so the exclusion is something
	 * this test can watch happen rather than something it assumes.
	 */
	UDataTable* MakeItemBaseTable(FAutomationTestBase& Test)
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmItemBaseRow::StaticStruct();

		const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
			"Name,BaseName,Slot,Hands,SubType,WeaponType,MaxDamageTypes,AttackSpeed,BasicShape,BasicShapeParams,CellsWide,CellsHigh,Implicit1Stat,Implicit1Kind,Implicit1Value,Implicit2Stat,Implicit2Kind,Implicit2Value\r\n"
			"Weapon_Sword,Sword,Weapon,1,Slashing,Sword,4,1.3,Strike,Radius=1.8,1,3,attack_damage,flat,40.0,,,0.0\r\n"
			"Weapon_Shield,Shield,Weapon,1,Blunt,Shield,4,1.0,Strike,Radius=1.5,2,3,armor,flat,300.0,,,0.0\r\n"
			"Weapon_Greataxe,Greataxe,Weapon,2,Slashing,Greataxe,8,1.1,Strike,Radius=2.7,2,6,attack_damage,flat,72.0,,,0.0\r\n"
			"Weapon_Staff,Staff,Weapon,2,Magic,Staff,8,1.3,Projectile,Range=7.2,2,6,attack_damage,flat,66.0,,,0.0\r\n"
			"Head_Helm,Helm,Head,0,,,0,0.0,,,2,2,armor,flat,200.0,,,0.0\r\n"));

		for (const FString& Problem : Problems)
		{
			Test.AddError(Problem);
		}
		return Problems.Num() == 0 ? Table : nullptr;
	}

	/** A choice, in one expression. */
	FCataclysmCreationChoice Choose(const TCHAR* WeaponType, const TCHAR* DamageType)
	{
		FCataclysmCreationChoice Made;
		Made.WeaponType = FName(WeaponType);
		Made.DamageType = FName(DamageType);
		return Made;
	}

	/** A possessed player character in this world, or null if any part failed.
	 *
	 *  THE SAME HELPER `CataclysmLootStatsTest` USES, and for the same reason:
	 *  `AController::Possess` rather than `APawn::PossessedBy`, because only the
	 *  first tells the controller which pawn it has. */
	ACataclysmPlayerCharacter* SpawnPossessedPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState =
			World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);

		if (!PlayerState || !Controller || !Character)
		{
			return nullptr;
		}

		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Character);
		return Character;
	}
}

// ---------------------------------------------------------------------------
// What the creator offers
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationOffersEveryWeaponButTheShieldTest,
	"Cataclysm.Creation.EveryWeaponTypeExceptTheShieldIsOffered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationOffersEveryWeaponButTheShieldTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UDataTable* Bases = MakeItemBaseTable(*this);
	if (!Bases)
	{
		return false;
	}

	const TArray<FName> Offered =
		UCataclysmCharacterCreation::StartingWeaponTypes(Bases);

	// THE HELM IS NOT A WEAPON AND IS NOT COUNTED. Its WeaponType column is
	// empty, which is how the table says "this is armour".
	TestEqual(TEXT("three of the four weapon rows are offered"), Offered.Num(), 3);
	TestFalse(TEXT("the Shield is not offered"),
			  Offered.Contains(FName(TEXT("Shield"))));
	TestTrue(TEXT("the Sword is"), Offered.Contains(FName(TEXT("Sword"))));
	TestTrue(TEXT("the Greataxe is"), Offered.Contains(FName(TEXT("Greataxe"))));
	TestTrue(TEXT("the Staff is"), Offered.Contains(FName(TEXT("Staff"))));

	// ONE-HANDED FIRST, THEN TWO-HANDED, ALPHABETICAL WITHIN EACH. The order is
	// what a screen draws its buttons in, so it has to be stable rather than
	// whatever a TMap happens to hand back.
	TestEqual(TEXT("the one-handed weapon comes first"), Offered[0],
			  FName(TEXT("Sword")));
	TestEqual(TEXT("then the two-handed ones, alphabetically"), Offered[1],
			  FName(TEXT("Greataxe")));
	TestEqual(TEXT("Staff after Greataxe"), Offered[2], FName(TEXT("Staff")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationShieldRefusalSaysWhyTest,
	"Cataclysm.Creation.AShieldIsRefusedAndTheRefusalSaysWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationShieldRefusalSaysWhyTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UDataTable* Bases = MakeItemBaseTable(*this);
	UDataTable* Skills = MakeWeaponSkillTable(*this);
	if (!Bases || !Skills)
	{
		return false;
	}

	// THE MATRIX DOES GIVE A SHIELD WAR SKILLS, so this is refused by the
	// creator's own rule rather than by the pairing being illegal. That is what
	// makes the test worth writing: the two refusals are different and the
	// message has to be the right one.
	const FString Refusal = UCataclysmCharacterCreation::RefusalFor(
		Skills, Bases, Choose(TEXT("Shield"), TEXT("War")));

	TestFalse(TEXT("a Shield is refused as a starting weapon"),
			  Refusal.IsEmpty());
	TestTrue(TEXT("and the refusal says it grants no attack damage"),
			 Refusal.Contains(TEXT("no attack damage")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationDamageTypesComeFromTheMatrixTest,
	"Cataclysm.Creation.AWeaponOffersOnlyTheDamageTypesTheMatrixGivesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationDamageTypesComeFromTheMatrixTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UDataTable* Skills = MakeWeaponSkillTable(*this);
	UDataTable* Bases = MakeItemBaseTable(*this);
	if (!Skills || !Bases)
	{
		return false;
	}

	const TArray<FName> OnASword =
		UCataclysmCharacterCreation::DamageTypesFor(Skills, FName(TEXT("Sword")));
	TestEqual(TEXT("a Sword carries the two the matrix gives it"),
			  OnASword.Num(), 2);

	// IN THE DESIGN'S ORDER, WHICH IS WAR THEN DEATH, and not in the order the
	// rows happen to sit in the table.
	TestEqual(TEXT("War first"), OnASword[0], FName(TEXT("War")));
	TestEqual(TEXT("then Death"), OnASword[1], FName(TEXT("Death")));

	const TArray<FName> OnAStaff =
		UCataclysmCharacterCreation::DamageTypesFor(Skills, FName(TEXT("Staff")));
	TestFalse(TEXT("a Staff cannot carry War, which the design excludes"),
			  OnAStaff.Contains(FName(TEXT("War"))));
	TestTrue(TEXT("a Staff carries Death"),
			 OnAStaff.Contains(FName(TEXT("Death"))));

	// THE AURA ROW MUST NOT LEAK. It has WeaponType `All`, and counting it would
	// give every weapon Void.
	TestFalse(TEXT("the weapon-independent aura row gives a Sword no Void"),
			  OnASword.Contains(FName(TEXT("Void"))));
	TestEqual(TEXT("and asking about `All` itself answers nothing"),
			  UCataclysmCharacterCreation::DamageTypesFor(
				  Skills, FName(TEXT("All"))).Num(), 0);

	// AND THE SAME QUESTION FROM THE OTHER SIDE AGREES WITH IT.
	const TArray<FName> CarryWar =
		UCataclysmCharacterCreation::WeaponTypesFor(Skills, Bases,
													FName(TEXT("War")));
	TestTrue(TEXT("a Sword is among the weapons that carry War"),
			 CarryWar.Contains(FName(TEXT("Sword"))));
	TestFalse(TEXT("and the Shield is not, because it is never offered"),
			  CarryWar.Contains(FName(TEXT("Shield"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationCountsDesignedSkillsTest,
	"Cataclysm.Creation.TheDesignedSkillCountIsCountedFromTheMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationCountsDesignedSkillsTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UDataTable* Skills = MakeWeaponSkillTable(*this);
	if (!Skills)
	{
		return false;
	}

	// A ROW WITH NO SKILL NAME IS A PAIRING THE DESIGN ALLOWS AND NOBODY HAS
	// WRITTEN. Six of the seven legal pairings in the real matrix are like that,
	// so a player picking one gets a character that cannot do anything, and the
	// screen says so before they choose. Issues #62 and #836.
	TestEqual(TEXT("Sword and War has four of its six written"),
			  UCataclysmCharacterCreation::DesignedSkillCount(
				  Skills, FName(TEXT("Sword")), FName(TEXT("War"))), 4);

	TestEqual(TEXT("Sword and Death has none, though the pairing is legal"),
			  UCataclysmCharacterCreation::DesignedSkillCount(
				  Skills, FName(TEXT("Sword")), FName(TEXT("Death"))), 0);

	const FString Summary = UCataclysmCharacterCreation::SummaryFor(
		Skills, Choose(TEXT("Sword"), TEXT("Death")));
	TestTrue(TEXT("and the summary line says so in words"),
			 Summary.Contains(TEXT("no skills are designed")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationClassesPerDamageTypeTest,
	"Cataclysm.Creation.EveryDamageTypeUnlocksThreeNamedClasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationClassesPerDamageTypeTest::RunTest(const FString&)
{
	const TMap<FName, TArray<FName>>& Map =
		UCataclysmCharacterCreation::ClassesByDamageType();

	TestEqual(TEXT("eight damage types"), Map.Num(), 8);

	TSet<FName> EveryClass;
	for (const TPair<FName, TArray<FName>>& Pair : Map)
	{
		TestEqual(*FString::Printf(TEXT("%s unlocks three classes"),
								   *Pair.Key.ToString()),
				  Pair.Value.Num(), 3);
		for (const FName& Class : Pair.Value)
		{
			EveryClass.Add(Class);
		}
	}

	// TWENTY-FOUR DISTINCT NAMES. A class belongs to exactly one damage type, so
	// a repeat here is a copying mistake rather than a design.
	TestEqual(TEXT("twenty-four distinct classes"), EveryClass.Num(), 24);

	// THE VERTICAL SLICE'S OWN THREE, BY NAME, because they are the three that
	// have stat lines in `game/Data/ClassStats.csv` and the ones a change to
	// this list is most likely to be about.
	const TArray<FName>& Demonic =
		UCataclysmCharacterCreation::ClassesFor(FName(TEXT("Demonic")));
	TestEqual(TEXT("Demonic unlocks the Ravager first"), Demonic[0],
			  FName(TEXT("Ravager")));
	TestEqual(TEXT("then the Ritualist"), Demonic[1], FName(TEXT("Ritualist")));
	TestEqual(TEXT("then the Masochist"), Demonic[2], FName(TEXT("Masochist")));

	TestEqual(TEXT("a name that is not a damage type unlocks nothing"),
			  UCataclysmCharacterCreation::ClassesFor(FName(TEXT("Slashing")))
				  .Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// The screen's state, with no Blueprint and therefore no widgets at all
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationScreenDropsAnImpossibleDamageTypeTest,
	"Cataclysm.Creation.ChangingWeaponDropsADamageTypeItCannotCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationScreenDropsAnImpossibleDamageTypeTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UDataTable* Skills = MakeWeaponSkillTable(*this);
	UDataTable* Bases = MakeItemBaseTable(*this);
	if (!Skills || !Bases)
	{
		return false;
	}

	UCataclysmCharacterCreationWidget* Screen =
		NewObject<UCataclysmCharacterCreationWidget>();
	Screen->SetTables(Skills, Bases);

	TestFalse(TEXT("no damage type is available before a weapon is chosen"),
			  Screen->DamageTypeIsAvailable(FName(TEXT("War"))));
	TestFalse(TEXT("and choosing one is refused"),
			  Screen->ChooseDamageType(FName(TEXT("War"))));

	TestTrue(TEXT("a Sword can be chosen"),
			 Screen->ChooseWeaponType(FName(TEXT("Sword"))));
	TestTrue(TEXT("and then War is available"),
			 Screen->DamageTypeIsAvailable(FName(TEXT("War"))));
	TestTrue(TEXT("and can be chosen"),
			 Screen->ChooseDamageType(FName(TEXT("War"))));
	TestTrue(TEXT("which is enough to confirm"), Screen->CanConfirm());

	// THE POINT OF THE TEST. A Staff cannot carry War, so changing to one has to
	// let War go. Keeping it would leave the screen saying War is chosen, War is
	// unavailable, and the choice is refused -- three statements of one fact,
	// two of which contradict the third.
	TestTrue(TEXT("changing to a Staff is allowed"),
			 Screen->ChooseWeaponType(FName(TEXT("Staff"))));
	TestEqual(TEXT("and War was let go"), Screen->GetChoice().DamageType,
			  FName(NAME_None));
	TestFalse(TEXT("so the choice can no longer be confirmed"),
			  Screen->CanConfirm());
	TestTrue(TEXT("and the refusal asks for a damage type"),
			 Screen->RefusalText().ToString().Contains(TEXT("damage type")));

	// A DAMAGE TYPE THE NEW WEAPON DOES CARRY IS KEPT. The rule is "drop what
	// cannot be carried", not "drop it every time".
	TestTrue(TEXT("Death is available on a Staff"),
			 Screen->ChooseDamageType(FName(TEXT("Death"))));
	TestTrue(TEXT("and a Sword carries Death too"),
			 Screen->ChooseWeaponType(FName(TEXT("Sword"))));
	TestEqual(TEXT("so Death was kept across the change"),
			  Screen->GetChoice().DamageType, FName(TEXT("Death")));

	// THE SHIELD IS NOT OFFERED, so the screen refuses it before any table is
	// consulted about damage types.
	TestFalse(TEXT("the screen will not take a Shield"),
			  Screen->ChooseWeaponType(FName(TEXT("Shield"))));
	TestEqual(TEXT("and the Sword is still chosen"),
			  Screen->GetChoice().WeaponType, FName(TEXT("Sword")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationScreenSaysWhatIsUnlockedTest,
	"Cataclysm.Creation.TheScreenNamesTheThreeClassTreesTheChoiceUnlocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationScreenSaysWhatIsUnlockedTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UDataTable* Skills = MakeWeaponSkillTable(*this);
	UDataTable* Bases = MakeItemBaseTable(*this);
	if (!Skills || !Bases)
	{
		return false;
	}

	UCataclysmCharacterCreationWidget* Screen =
		NewObject<UCataclysmCharacterCreationWidget>();
	Screen->SetTables(Skills, Bases);

	TestTrue(TEXT("nothing is said before anything is chosen"),
			 Screen->UnlockedClassesText().IsEmpty());

	Screen->ChooseWeaponType(FName(TEXT("Staff")));
	Screen->ChooseDamageType(FName(TEXT("Demonic")));

	const FString Unlocked = Screen->UnlockedClassesText().ToString();
	TestTrue(TEXT("the Ravager is named"), Unlocked.Contains(TEXT("Ravager")));
	TestTrue(TEXT("the Ritualist is named"), Unlocked.Contains(TEXT("Ritualist")));
	TestTrue(TEXT("the Masochist is named"), Unlocked.Contains(TEXT("Masochist")));

	// ALL EIGHT DAMAGE TYPES ARE ALWAYS SHOWN, whatever is chosen. Only which
	// of them can be TAKEN moves, which is what teaches a player the
	// relationship between a weapon and a damage type.
	TestEqual(TEXT("all eight are offered"), Screen->OfferedDamageTypes().Num(), 8);

	return true;
}

// ---------------------------------------------------------------------------
// Recording the choice, and what a character that chose nothing has
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationDefaultsAreUnchangedTest,
	"Cataclysm.Creation.ACharacterThatChoseNothingIsExactlyTheCharacterItWasBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationDefaultsAreUnchangedTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("a player state was spawned"), State))
	{
		return false;
	}

	// THIS IS THE TEST EVERY OTHER TEST IN THE PROJECT DEPENDS ON. Several
	// hundred automation tests stand a character up without going near the
	// creator, and all of them expect a Greataxe carrying Demonic skills. If
	// these two answers ever move, this feature is the reason.
	TestFalse(TEXT("nobody has chosen"), State->HasChosenAtCreation());
	TestEqual(TEXT("the weapon type is still the Greataxe"),
			  State->GetChosenWeaponType(), FName(TEXT("Greataxe")));
	TestEqual(TEXT("the damage type is still Demonic"),
			  State->GetChosenDamageType(), FName(TEXT("Demonic")));

	// AND THE TWO CONSTANTS THE REST OF THE PROJECT READS AGREE WITH THAT. The
	// weapon slots component's own defaults were the whole answer before the
	// creator existed and are still what a character has.
	TestEqual(TEXT("and they are the constants the creator names"),
			  State->GetChosenWeaponType(),
			  UCataclysmCharacterCreation::DefaultWeaponType);
	TestEqual(TEXT("both of them"), State->GetChosenDamageType(),
			  UCataclysmCharacterCreation::DefaultDamageType);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationPlayerStateRefusesAnIllegalPairTest,
	"Cataclysm.Creation.APlayerStateRefusesAnIllegalPairAndKeepsWhatItHad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationPlayerStateRefusesAnIllegalPairTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UDataTable* Skills = MakeWeaponSkillTable(*this);
	UDataTable* Bases = MakeItemBaseTable(*this);
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	if (!Skills || !Bases || !State)
	{
		return false;
	}

	FString Reason;
	TestTrue(TEXT("a Sword carrying War is taken"),
			 State->ChooseAtCreation(Skills, Bases, FName(TEXT("Sword")),
									 FName(TEXT("War")), Reason));
	TestTrue(TEXT("and somebody has now chosen"), State->HasChosenAtCreation());

	// REFUSED WHOLE, AND WHAT WAS THERE STAYS. A Staff cannot carry War, so this
	// is a pairing the design forbids rather than a weapon that does not exist.
	TestFalse(TEXT("a Staff carrying War is refused"),
			  State->ChooseAtCreation(Skills, Bases, FName(TEXT("Staff")),
									  FName(TEXT("War")), Reason));
	TestTrue(TEXT("and the reason says a Staff cannot carry War"),
			 Reason.Contains(TEXT("cannot carry")));
	TestEqual(TEXT("the Sword is still what was chosen"),
			  State->GetChosenWeaponType(), FName(TEXT("Sword")));
	TestEqual(TEXT("and War with it"), State->GetChosenDamageType(),
			  FName(TEXT("War")));

	// A SAVED CHOICE IS PUT BACK WITHOUT BEING JUDGED, which is the difference
	// between loading a record and making a choice. A record holds whatever was
	// last written to it, and refusing it would load a different character.
	State->SetCreationChoice(FName(TEXT("Staff")), FName(TEXT("War")));
	TestEqual(TEXT("a loaded record is taken as it is"),
			  State->GetChosenWeaponType(), FName(TEXT("Staff")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationReachesTheSaveRecordTest,
	"Cataclysm.Creation.AChosenPairIsWrittenToTheSaveRecordAndAnUnchosenOneIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationReachesTheSaveRecordTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UDataTable* Skills = MakeWeaponSkillTable(*this);
	UDataTable* Bases = MakeItemBaseTable(*this);
	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!Skills || !Bases || !Character)
	{
		return false;
	}

	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();

	// NOBODY CHOSE, SO NOTHING IS WRITTEN, and that is the interesting half.
	// `GetChosenWeaponType` answers the default for such a character, and
	// writing that would turn "nobody chose" into "chose the Greataxe" on the
	// first save -- a decision the player never made, recorded permanently, on
	// every character an automation test has ever stood up.
	TestTrue(TEXT("the record was gathered"),
			 FCataclysmSaveGather::CharacterFrom(*Character, *Record));
	TestTrue(TEXT("and the weapon type field is still empty"),
			 Record->StartingWeaponType.IsNone());
	TestTrue(TEXT("and the damage type field with it"),
			 Record->StartingDamageType.IsNone());

	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("the character has a player state"), State))
	{
		return false;
	}

	FString Reason;
	if (!TestTrue(TEXT("a choice is taken"),
				  State->ChooseAtCreation(Skills, Bases, FName(TEXT("Staff")),
										  FName(TEXT("Death")), Reason)))
	{
		AddError(Reason);
		return false;
	}

	TestTrue(TEXT("the record was gathered again"),
			 FCataclysmSaveGather::CharacterFrom(*Character, *Record));
	TestEqual(TEXT("and now carries the chosen weapon type"),
			  Record->StartingWeaponType, FName(TEXT("Staff")));
	TestEqual(TEXT("and the chosen damage type"), Record->StartingDamageType,
			  FName(TEXT("Death")));

	return true;
}

// ---------------------------------------------------------------------------
// The one real entry point: a possessed character actually changing
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationReachesARealCharacterTest,
	"Cataclysm.Creation.TheChoiceReachesARealCharacterAndChangesWhatItHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationReachesARealCharacterTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Character = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("a possessed player character"), Character))
	{
		return false;
	}

	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	UCataclysmInventoryComponent* Inventory = Character->GetInventory();
	UCataclysmWeaponSlotsComponent* Slots =
		Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
	ACataclysmPlayerState* State =
		Character->GetPlayerState<ACataclysmPlayerState>();
	if (!Equipment || !Inventory || !Slots || !State)
	{
		AddError(TEXT("The spawned character is missing one of its components."));
		return false;
	}

	// WHERE IT STARTS. A character wears a real Greataxe since issue #840, and
	// nothing has chosen anything.
	TestEqual(TEXT("it starts holding a Greataxe"),
			  Equipment->EquippedWeaponType(), FString(TEXT("Greataxe")));
	TestFalse(TEXT("applying a choice nobody made does nothing"),
			  Character->ApplyCreationChoice());
	TestEqual(TEXT("so it is still holding the Greataxe"),
			  Equipment->EquippedWeaponType(), FString(TEXT("Greataxe")));

	const int32 FreeBefore = Inventory->NumFreeSlots();

	// THE REAL TABLES HERE, not built ones, because this is the end-to-end
	// path and the item it wears has to be a row the game actually has.
	FString Reason;
	if (!TestTrue(TEXT("a Staff carrying Demonic is a legal choice"),
				  State->ChooseAtCreation(
					  UCataclysmWeaponSkills::LoadGeneratedTable(),
					  UCataclysmItemModifiers::LoadBaseTable(),
					  FName(TEXT("Staff")), FName(TEXT("Demonic")), Reason)))
	{
		AddError(Reason);
		return false;
	}

	if (!TestTrue(TEXT("the choice is applied to the character"),
				  Character->ApplyCreationChoice()))
	{
		return false;
	}

	// WHAT THE PLAYER WOULD SEE. A different weapon in hand, drawn by the gear
	// panel and listed by `Cataclysm.ShowEquipment`.
	TestEqual(TEXT("the character is now holding a Staff"),
			  Equipment->EquippedWeaponType(), FString(TEXT("Staff")));

	// AND THE SIX SKILLS FOLLOW THE DAMAGE TYPE, not only the weapon.
	TestEqual(TEXT("the weapon slots component carries the chosen damage type"),
			  Slots->GetDamageType(), FString(TEXT("Demonic")));

	// THE OLD WEAPON WENT INTO THE BAG RATHER THAN BEING DESTROYED, which is
	// the rule `UCataclysmWearing` exists to keep and the one failure here that
	// a player could not undo.
	TestEqual(TEXT("one carried slot was used by the axe that came off"),
			  Inventory->NumFreeSlots(), FreeBefore - 1);

	bool bFoundTheAxe = false;
	for (const FCataclysmCarriedSlot& Slot : Inventory->GetSlots())
	{
		if (Slot.Item.Base == FName(TEXT("Weapon_Greataxe")))
		{
			bFoundTheAxe = true;
			break;
		}
	}
	TestTrue(TEXT("and the Greataxe is the item in it"), bFoundTheAxe);

	// APPLYING THE SAME CHOICE AGAIN CHANGES NOTHING. Possession runs more than
	// once on a listen server and the confirm button can be pressed twice, so
	// this must not fill the bag with staves.
	TestTrue(TEXT("applying it a second time succeeds"),
			 Character->ApplyCreationChoice());
	TestEqual(TEXT("and put no second Staff anywhere"),
			  Inventory->NumFreeSlots(), FreeBefore - 1);
	TestEqual(TEXT("and the character still holds one Staff"),
			  Equipment->EquippedWeaponType(), FString(TEXT("Staff")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreationSurvivesPossessionTest,
	"Cataclysm.Creation.ACharacterComingBackGetsItsChoiceWithoutAnybodyAskingAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreationSurvivesPossessionTest::RunTest(const FString&)
{
	using namespace CataclysmCreationTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE CHOICE IS ON THE PLAYER STATE BEFORE THE PAWN EXISTS, which is the
	// situation this test is about. A character that dies respawns at the
	// capital as a new pawn, and a character loaded from a save is a new pawn
	// too; in both the player state is what carried the choice across, and
	// possession is where it has to reach the new body.
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!State || !Controller)
	{
		return false;
	}

	State->SetCreationChoice(FName(TEXT("Staff")), FName(TEXT("Demonic")));
	Controller->SetPlayerState(State);

	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
													 FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("a player character was spawned"), Character))
	{
		return false;
	}

	// NOTHING IS CALLED BY HAND AFTER THIS LINE. That is the whole point: this
	// is the one real entry point on the path a player actually walks, and a
	// test that called `ApplyCreationChoice` itself would pass even if
	// `PossessedBy` had stopped calling it. The 2026-08-24 handoff records the
	// Enemy Score award being fully covered and completely uncalled for exactly
	// this reason.
	Controller->Possess(Character);

	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	UCataclysmWeaponSlotsComponent* Slots =
		Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
	if (!Equipment || !Slots)
	{
		AddError(TEXT("The spawned character is missing a component."));
		return false;
	}

	TestEqual(TEXT("possession alone put the chosen weapon in its hands"),
			  Equipment->EquippedWeaponType(), FString(TEXT("Staff")));
	TestEqual(TEXT("and gave it the chosen damage type"), Slots->GetDamageType(),
			  FString(TEXT("Demonic")));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
