// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCreaturePanel.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmCombatOverlay.h"

// ---------------------------------------------------------------------------
// The colours, as six-digit sRGB hex, for the reason
// UCataclysmCombatOverlay.cpp gives: they read straight against section XIII of
// docs/Cataclysm_GDD_v2.md, and FColor::FromHex is what parses them. The
// reasoning for each is on its declaration in the header.
// ---------------------------------------------------------------------------

const TCHAR* UCataclysmCreaturePanel::PanelHex = TEXT("0A0F12");
const TCHAR* UCataclysmCreaturePanel::EdgeHex = TEXT("8F98A6");
const TCHAR* UCataclysmCreaturePanel::InkHex = TEXT("E8E4DE");
const TCHAR* UCataclysmCreaturePanel::UnnamedCreature = TEXT("Creature");

namespace
{
	/**
	 * Whether the panel describing the creature under the cursor is drawn.
	 *
	 * ITS OWN SWITCH RATHER THAN THE OVERLAY'S, and for a sharper reason than
	 * the bars and the rarity words have. This is the only thing in the game
	 * that will ever say what a creature's modifiers are, so folding it into
	 * another switch would mean somebody turning off damage numbers also losing
	 * the only way to find out that the thing in front of them charms on hit.
	 */
	TAutoConsoleVariable<int32> CVarShowCreaturePanel(
		TEXT("Cataclysm.Overlay.CreaturePanel"),
		1,
		TEXT("Whether a panel at the top of the screen describes the creature "
			 "under the cursor. 1 draws it, 0 does not. Nothing is ever drawn "
			 "for a corpse or for the player's own character, whatever this is "
			 "set to."),
		ECVF_Default);
}

bool UCataclysmCreaturePanel::CreaturePanelEnabled()
{
	return CVarShowCreaturePanel.GetValueOnAnyThread() != 0;
}

bool UCataclysmCreaturePanel::ShouldShowFor(const AActor* Creature,
											const AActor* LocalPlayerPawn,
											float Health, float MaxHealth)
{
	// THE PLAYER'S OWN PAWN AND ANYTHING MARKED DEAD ARE REFUSED HERE, by the
	// same function the overhead bars use, so there is one answer to "is this a
	// creature worth drawing something over" rather than two that can disagree.
	if (!UCataclysmCombatOverlay::IsOverheadBarCandidate(Creature,
														 LocalPlayerPawn))
	{
		return false;
	}

	// NO HEALTH POOL MEANS THE CREATURE IS NOT READY YET. Its ability system
	// arrives some frames after the actor does on a client.
	if (MaxHealth <= 0.0f)
	{
		return false;
	}

	// A CORPSE GETS NOTHING. See the header: an enemy destroys itself on the
	// tick AFTER it dies, so without this the panel flashes for one frame at the
	// end of every fight.
	if (Health <= 0.0f)
	{
		return false;
	}

	// AND NOTHING ELSE IS ASKED. In particular this does NOT ask the rarity.
	// A Common creature the player pointed at gets a panel, unlike the word over
	// the head, which is refused for a Common on purpose.
	return true;
}

const UDataTable* UCataclysmCreaturePanel::LoadEnemyArchetypeTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_EnemyArchetypes.DT_EnemyArchetypes"));
	if (!Table)
	{
		// NAMES BOTH SCRIPTS, because the two failures look the same from here:
		// the model never produced the CSV, or the CSV was never imported as an
		// asset. Every other loader in this project says the same thing.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_EnemyArchetypes. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/EnemyArchetypes.csv, which "
				 "tools/generate_datatables.py produces from the design "
				 "workbook."));
	}
	return Table;
}

const UDataTable* UCataclysmCreaturePanel::LoadEnemyModifierTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_EnemyModifiers.DT_EnemyModifiers"));
	if (!Table)
	{
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_EnemyModifiers. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/EnemyModifiers.csv, which "
				 "tools/generate_datatables.py produces from the design "
				 "workbook."));
	}
	return Table;
}

FString UCataclysmCreaturePanel::ArchetypeNameForRow(
	const UDataTable* EnemyArchetypeTable, FName Row)
{
	if (!EnemyArchetypeTable || Row.IsNone())
	{
		return FString();
	}

	// FindRow RATHER THAN A WALK, because unlike a rarity step the key IS the
	// row name, so there is nothing to search for.
	const FCataclysmEnemyArchetypeRow* Found =
		EnemyArchetypeTable->FindRow<FCataclysmEnemyArchetypeRow>(
			Row, TEXT("UCataclysmCreaturePanel::ArchetypeNameForRow"),
			/*bWarnIfRowMissing=*/false);

	return Found ? Found->ArchetypeName : FString();
}

FString UCataclysmCreaturePanel::ModifierNameForRow(
	const UDataTable* EnemyModifierTable, FName Row)
{
	if (!EnemyModifierTable || Row.IsNone())
	{
		return FString();
	}

	const FCataclysmEnemyModifierRow* Found =
		EnemyModifierTable->FindRow<FCataclysmEnemyModifierRow>(
			Row, TEXT("UCataclysmCreaturePanel::ModifierNameForRow"),
			/*bWarnIfRowMissing=*/false);

	return Found ? Found->ModifierName : FString();
}

void UCataclysmCreaturePanel::ModifierNamesFor(
	const UDataTable* EnemyModifierTable, const TArray<FName>& Rows,
	TArray<FString>& OutNames)
{
	OutNames.Reset();

	for (const FName& Row : Rows)
	{
		const FString Name = ModifierNameForRow(EnemyModifierTable, Row);
		if (!Name.IsEmpty())
		{
			// A ROW THE TABLE DOES NOT HOLD IS DROPPED, not drawn blank. See
			// the header for why a missing line beats an empty one.
			OutNames.Add(Name);
		}
	}
}

FString UCataclysmCreaturePanel::TitleFor(const FString& ArchetypeName,
										  const FString& RarityName)
{
	// A CREATURE WITH NO ARCHETYPE IS STILL NAMED. The sandbox's training
	// dummies are the base enemy class and carry no archetype row.
	const FString Name =
		ArchetypeName.IsEmpty() ? FString(UnnamedCreature) : ArchetypeName;

	if (RarityName.IsEmpty())
	{
		// A RUNG THE TABLE DOES NOT NAME LEAVES THE CREATURE'S NAME ALONE,
		// rather than putting a stray space in front of it.
		return Name;
	}

	return RarityName + TEXT(" ") + Name;
}

FString UCataclysmCreaturePanel::HealthTextFor(float Health, float MaxHealth)
{
	if (MaxHealth <= 0.0f)
	{
		return FString();
	}

	// A LIVING CREATURE NEVER READS ZERO. See the header: health is an unrounded
	// float, so 0.3 health is alive, hittable, and would print "0" from
	// rounding alone.
	const int32 Current = Health > 0.0f
		? FMath::Max(1, FMath::RoundToInt(Health))
		: 0;

	return FString::Printf(TEXT("%d / %d"), Current,
						   FMath::RoundToInt(MaxHealth));
}

FBox2D UCataclysmCreaturePanel::PanelBoxFor(const FVector2D& Viewport,
											float ContentWidth,
											float ContentHeight)
{
	const float Floor = static_cast<float>(Viewport.X) * MinimumWidthShare;
	float Width = FMath::Max(ContentWidth + PaddingPx * 2.0f, Floor);

	// NEVER WIDER THAN THE SCREEN. A modifier name long enough to overflow would
	// otherwise put the panel's left edge off the left of the viewport, and the
	// title is centred on the panel rather than on the screen, so it would go
	// with it.
	Width = FMath::Min(Width, static_cast<float>(Viewport.X));

	const float Height = ContentHeight + PaddingPx * 2.0f;
	const float Left = FMath::Max((static_cast<float>(Viewport.X) - Width) * 0.5f,
								  0.0f);

	return FBox2D(FVector2D(Left, TopMarginPx),
				  FVector2D(Left + Width, TopMarginPx + Height));
}
