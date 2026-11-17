// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmElementVisuals.generated.h"

class UDataTable;

/**
 * The spellings of the seven standard Niagara user parameters, in one place.
 *
 * WHY THESE ARE CHARACTER ARRAYS AND NOT FNAMES. Two translation units build
 * FNames from them -- the impact effect and the projectile effect -- and a
 * `const FName` in one file initialised from a `const FName` in another depends
 * on which of the two static initialisers runs first, which the standard does
 * not decide. A compile-time character array has no initialiser to order, so
 * both sides get the same spelling whatever order the linker chooses.
 *
 * WHY ONE PLACE AT ALL. docs/Niagara_Conventions.md section 2: "The names are
 * identical across every system. A skill row sets them without knowing which
 * system it spawned. The moment one system calls it `Colour` and another calls
 * it `Tint`, the data-driven path is dead." Niagara ignores a parameter a system
 * does not expose, silently -- no warning, no error, no effect -- so a
 * disagreement between two copies of a spelling would not fail, it would just
 * stop the colours arriving in one of the two systems.
 *
 * The bare names, without the `User.` prefix, which Niagara adds itself.
 */
namespace CataclysmEffectParameterNames
{
	inline constexpr TCHAR ElementColour[] = TEXT("ElementColour");
	inline constexpr TCHAR ElementColourDark[] = TEXT("ElementColourDark");
	inline constexpr TCHAR Intensity[] = TEXT("Intensity");
	inline constexpr TCHAR Scale[] = TEXT("Scale");
	inline constexpr TCHAR Duration[] = TEXT("Duration");
	inline constexpr TCHAR ImpactNormal[] = TEXT("ImpactNormal");
	inline constexpr TCHAR TargetPosition[] = TEXT("TargetPosition");
}

/**
 * The two colours a damage type draws with, read from DT_ElementVisuals.
 *
 * SHARED BY EVERY EFFECT AND NOT OWNED BY ANY ONE OF THEM. It began inside
 * UCataclysmImpactEffect, which was correct while one system existed. The
 * second system, NS_Proj_Body, needs exactly the same lookup, and the choice
 * was between a projectile asking a class called "impact effect" for its
 * colours or the lookup moving to its own class. This is the second.
 *
 * ONE SYSTEM SERVES ALL EIGHT DAMAGE TYPES, which is the whole arrangement
 * docs/Niagara_Conventions.md section 5 sets out: eight shapes times eight
 * damage types is 64 assets built the wrong way and 8 assets plus 8 data rows
 * built this way. A Demonic bolt and a Void bolt are the same asset with
 * different rows, so changing a colour is a data edit and touches no asset.
 */
UCLASS()
class CATACLYSM_API UCataclysmElementVisuals : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The eight damage types' colours, built in issue #549. */
	static const TCHAR* AssetPath;

	/** Null when the table asset is missing. Loaded once and kept. */
	static const UDataTable* LoadTable();

	/**
	 * The two colours for a damage type, looked up by the leaf of its
	 * `Element.*` tag -- `Demonic`, `Void` and so on, which is exactly what
	 * FCataclysmIncomingHit::DamageType holds.
	 *
	 * Returns false and writes nothing when the type has no row, which includes
	 * every untyped hit. Player damage carries no damage type by design, so
	 * that is a normal case and not a fault: see
	 * UCataclysmSkillEffects::DamageTypeOf.
	 */
	static bool ColoursFor(FName DamageType, FLinearColor& OutPrimary,
						   FLinearColor& OutSecondary);
};
