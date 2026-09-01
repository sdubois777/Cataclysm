// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmWeaponMeshes.generated.h"

class UDataTable;
class UStaticMesh;

/**
 * Which mesh is drawn in the hand for a weapon base, and how big.
 *
 * WHAT THIS EXISTS FOR. Issue #1125. No weapon was drawn anywhere in this game.
 * A player equipped a Greataxe, its stats and its six skills changed, and
 * nothing on screen changed at all. A search of the whole codebase for mesh
 * attachment found two socket uses in total: the camera boom on the player, and
 * `ACataclysmBruteCharacter` holding a rock.
 *
 * THE MAPPING IS DATA, NOT CODE, so changing which mesh a Greatsword draws costs
 * no rebuild. It lives in the Weapon Meshes sheet of
 * `docs/All_Things_Cataclysm.xlsx`, becomes `game/Data/WeaponMeshes.csv`, and
 * becomes `DT_WeaponMeshes`. `game/docs/weapon-source-assets.md` records the
 * measurements behind each choice.
 *
 * WHY A SEPARATE TABLE FROM ITEM BASES. A mesh path is an art binding rather
 * than a design decision, so a new weapons pack moves one table instead of
 * editing the design's own sheet. `FCataclysmWeaponMeshRow` says more about it.
 */
UCLASS()
class CATACLYSM_API UCataclysmWeaponMeshes : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Where DT_WeaponMeshes lives. */
	static const TCHAR* TableAssetPath;

	/**
	 * The socket on the Mannequin's right and left hands.
	 *
	 * BOTH SHIP WITH THE SKELETON and neither had to be authored. `SK_Mannequin`
	 * carries `HandGrip_R` on the `hand_r` bone and `HandGrip_L` on `hand_l`,
	 * along with `weapon_r_muzzle` for the firearm animation sets this project
	 * does not use. Read out of the mesh on 2026-09-01 rather than assumed.
	 */
	static const FName RightHandSocket;
	static const FName LeftHandSocket;

	/** Loads DT_WeaponMeshes, saying loudly in the log when it cannot. */
	static const UDataTable* LoadTable();

	/**
	 * The mesh a base draws, and the scale to draw it at.
	 *
	 * @param Table     DT_WeaponMeshes, or null
	 * @param Base      the ItemBases row name, which is this table's row name too
	 * @param OutScale  the scale to draw at. Left alone when nothing is found,
	 *                  so a caller that ignores the return value still has 1.
	 * @return the mesh, or null both when the base draws nothing on purpose and
	 *         when it has no row at all. **Those two are not the same** and the
	 *         caller cannot tell them apart from here; `HasRowFor` below can.
	 */
	static UStaticMesh* MeshFor(const UDataTable* Table, FName Base,
								float& OutScale);

	/**
	 * Whether the table has a row for this base at all.
	 *
	 * SEPARATE FROM MeshFor BECAUSE "DRAWS NOTHING" AND "NOBODY SAID" ARE
	 * DIFFERENT and issue #1125 asked for the difference to be visible:
	 * "Drawing nothing is a reasonable answer; drawing nothing silently is
	 * not." A Fist draws nothing by design. A base with no row is a mistake,
	 * and the player character logs it as one.
	 */
	static bool HasRowFor(const UDataTable* Table, FName Base);
};
