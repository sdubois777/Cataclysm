// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmWeaponMeshes.h"
#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "UObject/SoftObjectPath.h"

const TCHAR* UCataclysmWeaponMeshes::TableAssetPath =
	TEXT("/Game/Data/DT_WeaponMeshes.DT_WeaponMeshes");

const FName UCataclysmWeaponMeshes::RightHandSocket = TEXT("HandGrip_R");
const FName UCataclysmWeaponMeshes::LeftHandSocket = TEXT("HandGrip_L");

const UDataTable* UCataclysmWeaponMeshes::LoadTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, TableAssetPath);
	if (!Table)
	{
		// NAMING BOTH SCRIPTS, because the two failures look the same from
		// here: the workbook never produced the CSV, or the CSV was never
		// imported as an asset. The same wording and the same reason as
		// UCataclysmItemModifiers::LoadBaseTable.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load %s. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/WeaponMeshes.csv, which "
				 "tools/generate_datatables.py produces from the Weapon Meshes "
				 "sheet of docs/All_Things_Cataclysm.xlsx. No weapon will be "
				 "drawn in anybody's hand until it loads."),
			TableAssetPath);
	}
	return Table;
}

namespace
{
	/** The row for a base, or null. Shared by the two functions below so they
	 *  cannot disagree about what "found" means. */
	const FCataclysmWeaponMeshRow* FindRow(const UDataTable* Table, FName Base)
	{
		if (!Table || Base.IsNone())
		{
			return nullptr;
		}

		// No warning on a miss. Both callers below decide for themselves what a
		// miss means, and a warning here would fire for the ordinary case as
		// well as the faulty one.
		return Table->FindRow<FCataclysmWeaponMeshRow>(
			Base, TEXT("UCataclysmWeaponMeshes"), /*bWarnIfRowMissing=*/false);
	}
}

UStaticMesh* UCataclysmWeaponMeshes::MeshFor(const UDataTable* Table, FName Base,
											 float& OutScale)
{
	const FCataclysmWeaponMeshRow* Row = FindRow(Table, Base);
	if (!Row || Row->Mesh.IsEmpty())
	{
		// OutScale IS LEFT ALONE rather than written to 1. A caller that passes
		// a scale it is already using keeps it, and a caller that passes 1 keeps
		// that. Writing here would be the only way this function could change
		// something while answering "nothing to draw".
		return nullptr;
	}

	// AN UNREAL ASSET PATH REPEATS THE ASSET'S NAME after the package path, and
	// the table holds only the package path so that the sheet a person edits
	// does not have to say everything twice.
	FString Path = Row->Mesh;
	int32 LastSlash = INDEX_NONE;
	if (Path.FindLastChar(TEXT('/'), LastSlash))
	{
		Path = FString::Printf(TEXT("%s.%s"), *Row->Mesh,
							   *Row->Mesh.Mid(LastSlash + 1));
	}

	UStaticMesh* Mesh = Cast<UStaticMesh>(FSoftObjectPath(Path).TryLoad());
	if (!Mesh)
	{
		// NOT FATAL, AND EXPECTED ON A MACHINE WITHOUT THE PACK. The weapons
		// pack is third-party content and `.gitignore` excludes it, exactly as
		// it excludes the Paragon packs, so a fresh checkout has none of it.
		// The character fights identically with an empty hand.
		UE_LOG(LogCataclysm, Warning,
			TEXT("%s names the mesh %s, which did not load, so nothing will be "
				 "drawn in the hand. This is expected without the Dark Fantasy "
				 "Weapons pack; see game/docs/weapon-source-assets.md."),
			*Row->BaseName, *Path);
		return nullptr;
	}

	OutScale = Row->Scale > 0.0f ? Row->Scale : 1.0f;
	return Mesh;
}

bool UCataclysmWeaponMeshes::HasRowFor(const UDataTable* Table, FName Base)
{
	return FindRow(Table, Base) != nullptr;
}
