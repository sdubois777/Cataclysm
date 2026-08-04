// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "CataclysmCharacterBase.generated.h"

class UAttributeSet;
class UCataclysmAbilitySet;
class UCataclysmAbilitySystemComponent;

/**
 * Shared base for anything with abilities: the player and every enemy.
 *
 * Does not own an ability system component. Where the component lives differs
 * between the player (player state, survives respawn) and enemies (the pawn
 * itself), so ownership is decided by the subclass and reached through
 * GetAbilitySystemComponent().
 */
UCLASS(Abstract)
class CATACLYSM_API ACataclysmCharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACataclysmCharacterBase();

	/**
	 * How large one of the engine's basic shapes is, in centimetres.
	 *
	 * `/Engine/BasicShapes/Cylinder` and the rest occupy a 100cm cube, so a
	 * scale of 1 is 100cm and a component sized to a capsule has to divide by
	 * this. Both the player and every enemy build a stand-in body from those
	 * shapes because this project's Content folder holds no meshes at all.
	 *
	 * HERE RATHER THAN IN EACH .cpp, and that is not tidiness. It was a
	 * `constexpr` in an anonymous namespace in two different .cpp files, and
	 * Unreal's unity build concatenates .cpp files into one translation unit --
	 * so the two anonymous namespaces merged and the second definition was a
	 * redefinition error. It compiled only while the build happened to put the
	 * two files in different unity chunks, which is not something a change can
	 * control.
	 */
	static constexpr float BasicShapeSize = 100.0f;

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

protected:
	/**
	 * Points the ability system at this pawn and grants the starting kit.
	 * Must run on both server and client; where it is called from differs, which
	 * is the single most common source of bugs in this area.
	 */
	virtual void InitAbilityActorInfo();

	/** Granted on the server once the ability system is initialised. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySet> StartingAbilitySet;
};
