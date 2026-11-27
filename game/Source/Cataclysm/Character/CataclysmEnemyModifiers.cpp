// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyModifiers.h"

#include "AbilitySystem/CataclysmMovement.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmTelegraphMarker.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "EngineUtils.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
// For Starving Aura's debuff, built here. Issue #2539.
#include "AbilitySystem/CataclysmDebuffs.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "UObject/Package.h"
#include "Empire/CataclysmSeedMix.h"
// For the eleven Death modifiers, 2026-11-24: the pyre, the resistances the Haunting copy cuts, the side a zone and a
// curse reach, the player a curse finds, and the zones Lingering Decay leaves.
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmBonePyreCharacter.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
// For the Death modifiers on any wearer, issue #2899: the rows Wearer of the Dead lends.
#include "AbilitySystem/CataclysmFervour.h"

const TCHAR* UCataclysmEnemyModifiers::GenericCataclysm = TEXT("Generic");

const TCHAR* UCataclysmEnemyModifiers::TitanicResolveRow =
	TEXT("Generic_Titanic_Resolve");
const TCHAR* UCataclysmEnemyModifiers::OverpoweredRow =
	TEXT("Generic_Overpowered");
const TCHAR* UCataclysmEnemyModifiers::BloodthirstyRow =
	TEXT("Generic_Bloodthirsty");
const TCHAR* UCataclysmEnemyModifiers::ThornsOfGlassRow =
	TEXT("Generic_Thorns_of_Glass");
const TCHAR* UCataclysmEnemyModifiers::HellfireAuraRow =
	TEXT("Demonic_Hellfire_Aura");
const TCHAR* UCataclysmEnemyModifiers::UnyieldingRow =
	TEXT("Generic_Unyielding");
const TCHAR* UCataclysmEnemyModifiers::AbyssalAuraRow =
	TEXT("Demonic_Abyssal_Aura");
const TCHAR* UCataclysmEnemyModifiers::RelentlessRow =
	TEXT("Generic_Relentless");
const TCHAR* UCataclysmEnemyModifiers::ShielderRow =
	TEXT("Generic_Shielder");
const TCHAR* UCataclysmEnemyModifiers::PerfectAimRow =
	TEXT("Generic_Perfect_Aim");
const TCHAR* UCataclysmEnemyModifiers::HordeLeaderRow =
	TEXT("Generic_Horde_Leader");
const TCHAR* UCataclysmEnemyModifiers::PhasewalkerRow =
	TEXT("Generic_Phasewalker");
const TCHAR* UCataclysmEnemyModifiers::InfernalBrandRow =
	TEXT("Demonic_Infernal_Brand");
const TCHAR* UCataclysmEnemyModifiers::WitheringBlowsRow =
	TEXT("Famine_Withering_Blows");
const TCHAR* UCataclysmEnemyModifiers::TouchOfNothingRow =
	TEXT("Void_Touch_of_Nothing");
const TCHAR* UCataclysmEnemyModifiers::BeguilingRow =
	TEXT("Demonic_Beguiling");
const TCHAR* UCataclysmEnemyModifiers::InfernalSacrificeRow =
	TEXT("Demonic_Infernal_Sacrifice");
const TCHAR* UCataclysmEnemyModifiers::UnholySigilsRow =
	TEXT("Demonic_Unholy_Sigils");
const TCHAR* UCataclysmEnemyModifiers::SacrificialBondRow =
	TEXT("Demonic_Sacrificial_Bond");
const TCHAR* UCataclysmEnemyModifiers::InfernoChargeRow =
	TEXT("Demonic_Inferno_Charge");
const TCHAR* UCataclysmEnemyModifiers::StarvingAuraRow =
	TEXT("Famine_Starving_Aura");
const TCHAR* UCataclysmEnemyModifiers::HauntingRow =
	TEXT("Death_Haunting");
const TCHAR* UCataclysmEnemyModifiers::GraveTouchRow =
	TEXT("Death_Grave_Touch");
const TCHAR* UCataclysmEnemyModifiers::UndyingWillRow =
	TEXT("Death_Undying_Will");
const TCHAR* UCataclysmEnemyModifiers::SoulEaterRow =
	TEXT("Death_Soul_Eater");
const TCHAR* UCataclysmEnemyModifiers::ShadowStepRow =
	TEXT("Death_Shadow_Step");
const TCHAR* UCataclysmEnemyModifiers::SpectralShiftRow =
	TEXT("Death_Spectral_Shift");
const TCHAR* UCataclysmEnemyModifiers::LingeringDecayRow =
	TEXT("Death_Lingering_Decay");
const TCHAR* UCataclysmEnemyModifiers::FinalCurseRow =
	TEXT("Death_Final_Curse");
const TCHAR* UCataclysmEnemyModifiers::FuneralPyreRow =
	TEXT("Death_Funeral_Pyre");
const TCHAR* UCataclysmEnemyModifiers::SoulTetherRow =
	TEXT("Death_Soul_Tether");
const TCHAR* UCataclysmEnemyModifiers::MortalReminderRow =
	TEXT("Death_Mortal_Reminder");
const TCHAR* UCataclysmEnemyModifiers::CommanderRow =
	TEXT("War_Commander");
const TCHAR* UCataclysmEnemyModifiers::BattleCryRow =
	TEXT("War_Battle_Cry");
const TCHAR* UCataclysmEnemyModifiers::ArmoredJuggernautRow =
	TEXT("War_Armored_Juggernaut");
const TCHAR* UCataclysmEnemyModifiers::DrumsOfWarRow =
	TEXT("War_Drums_of_War");
const TCHAR* UCataclysmEnemyModifiers::ShieldWallRow =
	TEXT("War_Shield_Wall");
const TCHAR* UCataclysmEnemyModifiers::ArtilleryBarrageRow =
	TEXT("War_Artillery_Barrage");
const TCHAR* UCataclysmEnemyModifiers::BattleScarredRow =
	TEXT("War_Battle_Scarred");
const TCHAR* UCataclysmEnemyModifiers::PhalanxRow =
	TEXT("War_Phalanx");

const UDataTable* UCataclysmEnemyModifiers::LoadEnemyModifierTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(
		nullptr, TEXT("/Game/Data/DT_EnemyModifiers.DT_EnemyModifiers"));
	if (!Table)
	{
		// NAMES BOTH SCRIPTS, because the two failures look the same from here:
		// the workbook never produced the CSV, or the CSV was never imported as
		// an asset. Every other loader in this project says the same thing.
		UE_LOG(LogCataclysm, Error,
			TEXT("Could not load DT_EnemyModifiers. It is produced by "
				 "tools/generate_datatable_assets.py from "
				 "game/Data/EnemyModifiers.csv, which "
				 "tools/generate_datatables.py produces from the design "
				 "workbook."));
	}
	return Table;
}

int32 UCataclysmEnemyModifiers::CountForRarityStep(int32 RarityStep)
{
	// THE STEP ITSELF. `docs/Cataclysm_GDD_v2.md` gives 0, 1, 2, 3, 4, 5 for
	// Common through Cataclysm Boss and `game/Data/EnemyRarities.csv` numbers
	// those same rungs 0 to 5, so the two ladders are one ladder.
	//
	// NOT CAPPED AT FIVE HERE. A step above the table's own is a caller error
	// rather than a rung, and `ACataclysmEnemyCharacter::SetRarityStep` is where
	// a step is bounded. Capping here as well would hide that.
	return FMath::Max(0, RarityStep);
}

bool UCataclysmEnemyModifiers::IsDrawableBy(
	const FCataclysmEnemyModifierRow& Row, FName CataclysmType)
{
	// THE WHOLE POOL RULE, IN ONE PLACE. `docs/Cataclysm_GDD_v2.md`: an enemy's
	// modifiers are "drawn from its own Cataclysm's column and the Generic one".
	return Row.CataclysmType.Equals(CataclysmType.ToString(),
									ESearchCase::IgnoreCase)
		|| Row.CataclysmType.Equals(GenericCataclysm, ESearchCase::IgnoreCase);
}

TArray<FName> UCataclysmEnemyModifiers::PoolFor(
	const UDataTable* EnemyModifierTable, FName CataclysmType)
{
	TArray<FName> Pool;
	if (!EnemyModifierTable)
	{
		return Pool;
	}

	EnemyModifierTable->ForeachRow<FCataclysmEnemyModifierRow>(
		TEXT("UCataclysmEnemyModifiers::PoolFor"),
		[&Pool, CataclysmType](const FName& Key,
							   const FCataclysmEnemyModifierRow& Row)
		{
			if (IsDrawableBy(Row, CataclysmType))
			{
				Pool.Add(Key);
			}
		});

	// SORTED, BECAUSE A DataTable IS A MAP. See the header: an unsorted walk
	// would hand the same seed a different modifier on a different run, which is
	// the fault `UCataclysmEnemyRarity::SpawnableSteps` sorts to avoid.
	Pool.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	return Pool;
}

TArray<FName> UCataclysmEnemyModifiers::Draw(
	const UDataTable* EnemyModifierTable, FName CataclysmType, int32 Count,
	FRandomStream& Stream, const TArray<FName>& Already)
{
	TArray<FName> Drawn;
	if (Count <= 0)
	{
		return Drawn;
	}

	TArray<FName> Pool = PoolFor(EnemyModifierTable, CataclysmType);

	// WHAT THE CREATURE ALREADY HAS IS OUT OF THE POOL BEFORE ANYTHING IS
	// DRAWN, rather than being drawn and rejected. Rejecting would make the
	// number of draws depend on what was already carried, so the same seed on
	// two creatures that had been given different starting lists would produce
	// unrelated results for the rest of the draw.
	Pool.RemoveAll([&Already](const FName& Key)
	{
		return Already.Contains(Key);
	});

	while (Drawn.Num() < Count && Pool.Num() > 0)
	{
		const int32 Index = Stream.RandRange(0, Pool.Num() - 1);
		Drawn.Add(Pool[Index]);

		// REMOVED SO IT CANNOT COME UP AGAIN. `RemoveAtSwap` would be cheaper
		// and would reorder what is left, which makes the draw depend on the
		// order things were removed in as well as on the stream.
		Pool.RemoveAt(Index);
	}

	return Drawn;
}

TArray<FName> UCataclysmEnemyModifiers::ClosedByInnateEffects(const UDataTable* EnemyModifierTable,
																FName CataclysmType,
																const TArray<FString>& InnateEffects)
{
	TArray<FName> Closed;
	if (!EnemyModifierTable || InnateEffects.IsEmpty())
	{
		return Closed;
	}

	EnemyModifierTable->ForeachRow<FCataclysmEnemyModifierRow>(
		TEXT("UCataclysmEnemyModifiers::ClosedByInnateEffects"),
		[&Closed, &InnateEffects, CataclysmType](const FName& Key, const FCataclysmEnemyModifierRow& Row)
		{
			// ONLY A ROW THE CREATURE COULD DRAW: "a modifier belonging to a different Cataclysm is not a clash,
			// because that enemy can never roll it" (the design's own words), so nothing else is closed.
			if (!IsDrawableBy(Row, CataclysmType))
			{
				return;
			}
			for (const FString& Innate : InnateEffects)
			{
				if (Row.ModifierName.Equals(Innate, ESearchCase::IgnoreCase))
				{
					Closed.Add(Key);
					return;
				}
			}
		});

	// SORTED, AS `PoolFor` IS, so what is closed does not depend on the order a map is walked in.
	Closed.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});
	return Closed;
}

const FCataclysmEnemyModifierRow* UCataclysmEnemyModifiers::FindRow(
	const UDataTable* EnemyModifierTable, FName Key)
{
	if (!EnemyModifierTable || Key.IsNone())
	{
		return nullptr;
	}

	return EnemyModifierTable->FindRow<FCataclysmEnemyModifierRow>(
		Key, TEXT("UCataclysmEnemyModifiers::FindRow"), /*bWarnIfMissing=*/false);
}

// ---------------------------------------------------------------------------
// What the carried modifiers do
// ---------------------------------------------------------------------------

bool UCataclysmEnemyModifiers::Carries(const TArray<FName>& Rows,
									   const TCHAR* RowKey)
{
	return RowKey != nullptr && Rows.Contains(FName(RowKey));
}

bool UCataclysmEnemyModifiers::CarriedBy(const AActor* Character, const TCHAR* RowKey)
{
	const ACataclysmCharacterBase* Wearer = Cast<ACataclysmCharacterBase>(Character);
	if (Wearer == nullptr || RowKey == nullptr)
	{
		return false;
	}
	const FName Row(RowKey);
	if (Wearer->ModifierRows.Contains(Row))
	{
		return true;
	}
	// AND A ROW WEARER OF THE DEAD LENT IT, read off its ability system, which for a player is on its player state.
	const UCataclysmAbilitySystemComponent* Lent =
		Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Wearer));
	return Lent != nullptr && Lent->LentModifierSecondsLeft(Row) > 0.0f;
}

TArray<FName> UCataclysmEnemyModifiers::RowsCarriedBy(const AActor* Character)
{
	TArray<FName> Rows;
	const ACataclysmCharacterBase* Wearer = Cast<ACataclysmCharacterBase>(Character);
	if (Wearer == nullptr)
	{
		return Rows;
	}
	Rows = Wearer->ModifierRows;
	if (const UCataclysmAbilitySystemComponent* Lent =
			Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Wearer)))
	{
		for (const FName& Row : UCataclysmFervour::LentEnemyModifierRows())
		{
			if (Lent->LentModifierSecondsLeft(Row) > 0.0f)
			{
				Rows.AddUnique(Row);
			}
		}
	}
	return Rows;
}

float UCataclysmEnemyModifiers::MaxHealthMultiplier(const TArray<FName>& Rows)
{
	// ONE MODIFIER MOVES THIS TODAY. Written as a running multiplier rather
	// than as a branch, so a second one that also scales health multiplies with
	// it rather than replacing it.
	float Multiplier = 1.0f;

	if (Carries(Rows, TitanicResolveRow))
	{
		Multiplier *= TitanicResolveHealthMultiplier;
	}

	return Multiplier;
}

float UCataclysmEnemyModifiers::ForcedCritChance(const TArray<FName>& Rows)
{
	if (Carries(Rows, OverpoweredRow))
	{
		return OverpoweredCritChance;
	}

	return -1.0f;
}

float UCataclysmEnemyModifiers::LifeLeechPercent(const TArray<FName>& Rows)
{
	// A SUM, because two sources of leech should add rather than one winning.
	float Percent = 0.0f;

	if (Carries(Rows, BloodthirstyRow))
	{
		Percent += BloodthirstyLeechPercent;
	}

	return Percent;
}

bool UCataclysmEnemyModifiers::StrikesBack(const TArray<FName>& Rows)
{
	return Carries(Rows, ThornsOfGlassRow);
}

float UCataclysmEnemyModifiers::HealthRegenShareOfMaximum(
	const TArray<FName>& Rows)
{
	// A SUM, so a second modifier that also heals adds rather than replacing.
	float Share = 0.0f;

	if (Carries(Rows, RelentlessRow))
	{
		Share += RelentlessHealthPerSecondShare;
	}

	return Share;
}

float UCataclysmEnemyModifiers::EnergyShieldShareOfHealth(
	const TArray<FName>& Rows)
{
	float Share = 0.0f;

	if (Carries(Rows, ShielderRow))
	{
		Share += ShielderShareOfHealth;
	}

	return Share;
}

bool UCataclysmEnemyModifiers::AttacksCannotBeDodged(const TArray<FName>& Rows)
{
	return Carries(Rows, PerfectAimRow);
}

int32 UCataclysmEnemyModifiers::RallyAlliesOnDeath(AActor* Character)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
	if (Enemy == nullptr || !Carries(Enemy->ModifierRows, HordeLeaderRow))
	{
		return 0;
	}

	const UWorld* World = Enemy->GetWorld();
	if (World == nullptr)
	{
		return 0;
	}

	// THE COMMANDER BUFF, WHICH ALREADY EXISTS AND ALREADY SAYS THIS. Its own
	// row reads "All nearby allies gain 20% increased movement speed and attack
	// speed", which is what Horde Leader describes, and the Succubus already
	// grants it. A second buff meaning the same thing would be two names for one
	// effect.
	const FGameplayTag Buff =
		UCataclysmSkillShapes::StatusTagFor(TEXT("Commander"));
	if (!Buff.IsValid())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Horde Leader cannot rally: there is no Status.Buff.Commander "
				 "tag. See game/Config/Tags/CataclysmTags.ini."));
		return 0;
	}

	// ALLIES AND NOT ENEMIES. `FindAlliesInSphere` excludes the creature itself,
	// which is what is wanted here: a dead creature is not its own ally and the
	// modifier is about what its death does for the rest of the pack.
	const TArray<AActor*> Allies = UCataclysmTargeting::FindAlliesInSphere(
		World, Enemy, Enemy->GetActorLocation(), AuraRadiusCm);

	int32 Rallied = 0;
	for (AActor* Ally : Allies)
	{
		if (UCataclysmSkillEffects::ApplyTagForDuration(Enemy, Ally, Buff,
													   HordeLeaderSeconds))
		{
			++Rallied;
		}
	}

	if (Rallied > 0)
	{
		UE_LOG(LogCataclysm, Log,
			TEXT("%s died carrying Horde Leader and rallied %d all%s."),
			*Enemy->GetName(), Rallied, Rallied == 1 ? TEXT("y") : TEXT("ies"));
	}

	return Rallied;
}

bool UCataclysmEnemyModifiers::PhaseStep(AActor* Character, float StepSeconds)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
	if (Enemy == nullptr || !Carries(Enemy->ModifierRows, PhasewalkerRow))
	{
		return false;
	}

	// A DEAD CREATURE DOES NOT TELEPORT, the same guard the aura step carries
	// and for the same reason: a step already in flight would otherwise land
	// after the creature died.
	if (UCataclysmSkillEffects::IsDead(Enemy))
	{
		return false;
	}

	Enemy->SecondsSincePhase += StepSeconds;
	if (Enemy->SecondsSincePhase < PhasewalkerIntervalSeconds)
	{
		return false;
	}
	Enemy->SecondsSincePhase = 0.0f;

	// A SEEDED DRAW RATHER THAN FRandRange, so two creatures phasing on the same
	// step do not both go the same way. The same shape the modifier draw uses.
	const UWorld* World = Enemy->GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	// Its identity, the draw's salt and the clock, through the mix: issue #2749's placement seeding slice; until it this was their XOR, so creatures made or killed together, with neighbouring identities and one clock, drew related rolls.
	FRandomStream Stream(CataclysmSeedMix::Mix(static_cast<int32>(Enemy->GetUniqueID()), PhaseDrawSalt,
		static_cast<int32>(Now * 1000.0f)));

	const float Angle = Stream.FRandRange(0.0f, 360.0f);
	const FVector Step(FMath::Cos(FMath::DegreesToRadians(Angle)),
					   FMath::Sin(FMath::DegreesToRadians(Angle)), 0.0f);

	// SWEPT, so it cannot phase into a wall. A blocked teleport moves it as far
	// as it can go, which reads as the creature reappearing against the wall
	// rather than inside it.
	//
	// NOT WHEN IT WOULD LAND FARTHER FROM A HOLDER OF NOWHERE TO RUN. Issue #1515,
	// ruled 2026-09-25: a phase is the creature moving itself, and a held
	// creature may not move itself away. The step is simply not made; its
	// interval has started again, so it tries a new direction next time.
	if (!Enemy->MayMoveItselfTo(Enemy->GetActorLocation() + Step * PhasewalkerDistanceCm))
	{
		return false;
	}
	const FVector Was = Enemy->GetActorLocation();
	UCataclysmMovement::MoveWithoutSticking(Enemy, Step * PhasewalkerDistanceCm);

	// A PHASE IS AN INSTANT RELOCATION, not ground walked. Issue #2388.
	UCataclysmMovement::NoteRelocatedInstantly(Enemy, Was);

	return true;
}

namespace
{
	/**
	 * Starving Aura's debuff on one target: the row's Strength ADDED to
	 * `UCataclysmVitalAttributeSet::HealingReceivedReduction` for the row's
	 * duration, under `Status.Debuff.StarvingAura`. Issue #2539.
	 *
	 * NOT THROUGH `UCataclysmSkillEffects::ApplyNamedEffect`, which only LOWERS
	 * the stats a row's `MovesStat` names. The row leaves that column empty and
	 * the stat is named here, ruled 2026-10-26; #1144 is the open concern.
	 *
	 * BUILT AS `ApplyNamedEffectOnly` BUILDS ITS EFFECT, with the public calls
	 * that file's private helpers make: an object of its own (#1501), a
	 * duration, one additive modifier, the tag, and whatever already grants the
	 * tag taken off first. So TWO STARVING CREATURES DO NOT STACK (ruled): the
	 * later application replaces the earlier and the cut stays at the row's 50.
	 *
	 * ON THE CURRENT VALUE, NEVER THE BASE, which is why it ADDS to the floor
	 * rules' share. Death's Embrace and Necrotic Ground reach the attribute as
	 * its base through the stat refresh's `SetNumericAttributeBase`, which leaves
	 * a running modifier standing, and this leaves their base alone. `TopUp`
	 * clamps the sum at 100.
	 *
	 * AND THE DEBUFF RULES ABYSSAL AURA'S PATH ASKS: a row refusing debuffs
	 * refuses it, the target's own stat sets how long it lasts, and the laying
	 * is recorded.
	 */
	bool EnemyModifiersApplyStarvingAura(AActor* Starver, AActor* Starved,
										 const FGameplayTag& StarvingTag,
										 float StarvingSeconds, float StarvingPoints)
	{
		UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Starved);
		const FGameplayAttribute StarvedStat =
			UCataclysmVitalAttributeSet::GetHealingReceivedReductionAttribute();
		if (!Defender || !Defender->HasAttributeSetForAttribute(StarvedStat)
			|| UCataclysmDebuffs::RefusedByARow(Starver, Starved, StarvingTag))
		{
			return false;
		}
		const float StarvedSeconds = UCataclysmDebuffs::DurationOn(
			Defender, StarvingSeconds, FGameplayTagContainer(StarvingTag));
		if (StarvedSeconds <= 0.0f)
		{
			return false;
		}
		UCataclysmDebuffs::NoteLaid(Starver, Starved, StarvingTag, /*bIsASpreadCopy=*/false);
		UCataclysmSkillEffects::RemoveEffectsGranting(Starved, StarvingTag);

		UObject* StarvingOuter = GetTransientPackage();
		UGameplayEffect* Effect = NewObject<UGameplayEffect>(
			StarvingOuter, MakeUniqueObjectName(StarvingOuter, UGameplayEffect::StaticClass(),
												FName(TEXT("CataclysmStatus_StarvingAura"))));
		Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		Effect->DurationMagnitude =
			FGameplayEffectModifierMagnitude(FScalableFloat(StarvedSeconds));

		FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = StarvedStat;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FScalableFloat(StarvingPoints);

		UTargetTagsGameplayEffectComponent& TagsComponent =
			Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
		FInheritedTagContainer Granted;
		Granted.Added.AddTag(StarvingTag);
		TagsComponent.SetAndApplyTargetTagChanges(Granted);

		// IN THE CREATURE'S NAME, as `ApplyStating` applies Abyssal Aura's.
		UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Starver);
		FGameplayEffectContextHandle Context = (Source ? Source : Defender)->MakeEffectContext();
		Context.AddInstigator(Starver, Starver);
		const FGameplayEffectSpec Spec(Effect, Context, /*Level=*/1.0f);
		Defender->ApplyGameplayEffectSpecToSelf(Spec);
		return true;
	}

	/**
	 * War's Commander and Drums of War on one pulse: the named buff granted to every ally of `Inspirer` within
	 * `InspiringReachCm`, for `UCataclysmEnemyModifiers::WarAuraGrantSeconds`. 2026-11-25.
	 *
	 * ALLIES AND NOT ITSELF: `FindAlliesInSphere` leaves out the creature it searches for and the dead, which is the
	 * rows' "allies". THE CREATURE FIRST AND THE ALLY SECOND, the order the Commander's Aura dungeon rule records
	 * getting wrong: (Instigator, Target). REFRESHED RATHER THAN STACKED, one effect per tag, so an ally between two
	 * carriers of one row gains it once.
	 *
	 * NOT HOSTILE, so a creature that takes no hostile action still grants it: the medic's rule (#1680) is about what
	 * harms the player, and a buff on its own side harms nobody. A judgement, recorded.
	 *
	 * @return how many allies were granted it
	 */
	int32 EnemyModifiersInspireAlliesWithin(ACataclysmEnemyCharacter* Inspirer, const TCHAR* InspiringBuffName,
											float InspiringReachCm)
	{
		const UWorld* InspiringWorld = Inspirer ? Inspirer->GetWorld() : nullptr;
		const FGameplayTag InspiringBuff = UCataclysmSkillShapes::StatusTagFor(InspiringBuffName);
		if (InspiringWorld == nullptr || !InspiringBuff.IsValid())
		{
			if (Inspirer && !InspiringBuff.IsValid())
			{
				UE_LOG(LogCataclysm, Warning,
					   TEXT("%s cannot grant %s: there is no such status tag. See game/Config/Tags/CataclysmTags.ini "
							"and tools/generate_gameplay_tags.py."),
					   *Inspirer->GetName(), InspiringBuffName);
			}
			return 0;
		}

		int32 Inspired = 0;
		for (AActor* Ally : UCataclysmTargeting::FindAlliesInSphere(
				 InspiringWorld, Inspirer, Inspirer->GetActorLocation(), InspiringReachCm))
		{
			if (UCataclysmSkillEffects::ApplyTagForDuration(Inspirer, Ally, InspiringBuff,
															UCataclysmEnemyModifiers::WarAuraGrantSeconds))
			{
				++Inspired;
			}
		}
		return Inspired;
	}
}

int32 UCataclysmEnemyModifiers::AuraStep(AActor* Character, float StepSeconds)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
	if (Enemy == nullptr)
	{
		// THE PLAYER COMES THROUGH HERE EVERY STEP, because the step this hangs
		// off is on the shared character base.
		return 0;
	}

	// A CREATURE THAT STARTS NOTHING HOSTILE BURNS AND STRIPS NOBODY, however
	// its modifiers were drawn. Issue #1680.
	//
	// THIS IS NOT AN EDGE CASE. The floor's medic is chosen as the rarest
	// creature there, and rarity is how many modifiers a creature draws, so a
	// medic is the creature most likely to have drawn one of these: 17% at
	// Elite rising to 55% at Boss, out of a pool of 18 rows of which three are
	// hostile with no decision behind them.
	//
	// SO WITHOUT THIS, MOST MEDICS WOULD WALK UP AND BURN THE PLAYER while
	// never swinging, and the row exists to put a NON-THREATENING enemy on the
	// floor.
	const bool bHostile = !Enemy->TakesNoHostileAction();

	const bool bBurns =
		bHostile && Carries(Enemy->ModifierRows, HellfireAuraRow);
	const bool bStripsResistance =
		bHostile && Carries(Enemy->ModifierRows, AbyssalAuraRow);
	const bool bStarves =
		bHostile && Carries(Enemy->ModifierRows, StarvingAuraRow);

	// AND WHETHER THE FLOOR MADE THIS ONE ITS MEDIC. TODAY THIS IS EXACTLY THE
	// OPPOSITE OF `bHostile` ABOVE, AND BOTH NAMES ARE KEPT ON PURPOSE. They
	// ask different questions -- one is "may this creature harm anybody", the
	// other is "does this creature heal its allies" -- and they happen to have
	// the same answer only because the medic is the one creature that takes no
	// hostile action. A second such creature would separate them. Collapsing
	// them into one would read as tidying and would be wrong.
	//
	// IT IS NOT A MODIFIER
	// ROW. Being a medic comes from `game/Data/DungeonModifiers.csv` and is
	// set at spawn; `ModifierRows` holds keys from a different table. The
	// field's own comment says why the two are kept apart.
	const bool bHealsAllies = Enemy->bHealsAlliesForTheFloorRule;

	// AND WAR'S TWO BUFFS ON ITS ALLIES, 2026-11-25: Commander and Drums of War. NOT GATED ON `bHostile`: a buff on
	// its own side harms nobody, so a medic carrying one still grants it. See `EnemyModifiersInspireAlliesWithin`.
	const bool bCommands = Carries(Enemy->ModifierRows, CommanderRow);
	const bool bDrums = Carries(Enemy->ModifierRows, DrumsOfWarRow);

	if (!bBurns && !bStripsResistance && !bStarves && !bHealsAllies && !bCommands && !bDrums)
	{
		// EVERY COMMON CREATURE COMES THROUGH HERE, carrying no aura and not
		// being the floor's medic. The check that used to stand here asked
		// whether `ModifierRows` was empty, which a medic can be.
		return 0;
	}

	// A DEAD CREATURE BURNS NOBODY. The regeneration timer is cleared on death,
	// but a step already in flight would otherwise land afterwards, which is the
	// guard `ACataclysmSuccubusCharacter::PulseDominion` carries for the same
	// reason.
	if (UCataclysmSkillEffects::IsDead(Enemy))
	{
		return 0;
	}

	Enemy->SecondsSinceAuraPulse += StepSeconds;
	if (!AuraPulseIsDue(Enemy->SecondsSinceAuraPulse))
	{
		return 0;
	}
	Enemy->SecondsSinceAuraPulse = 0.0f;

	// ONE CLOCK, ADVANCED AND CLEARED IN ONE PLACE, AND EVERYTHING DUE FIRES
	// FROM IT. The medic's pulse kept a second copy of those three lines
	// until issue #1648 was wired up, so a creature that was both the floor's
	// medic and carried an aura advanced the one clock twice every step and
	// fired everything at twice the rate the design states.
	int32 Touched = 0;
	if (bHealsAllies)
	{
		Touched += HealAlliesPulse(Enemy);
	}

	// COMMANDER AT ITS EIGHT METRES AND DRUMS OF WAR AT HALF THAT, each its own buff, on the one pulse. A creature
	// carrying both grants both, and an ally inside four metres of it then holds both.
	if (bCommands)
	{
		Touched += EnemyModifiersInspireAlliesWithin(Enemy, TEXT("Commander"), CommanderReachCm);
	}
	if (bDrums)
	{
		Touched += EnemyModifiersInspireAlliesWithin(Enemy, TEXT("Drums of War"), DrumsOfWarReachCm);
	}

	if (!bBurns && !bStripsResistance && !bStarves)
	{
		// A MEDIC CARRYING NO AURA IS FINISHED, and so is a creature whose only
		// aura buffs its own side. What follows searches for this creature's
		// ENEMIES, which for either is work with no result.
		return Touched;
	}

	const UWorld* World = Enemy->GetWorld();
	if (World == nullptr)
	{
		return 0;
	}

	// EVERYTHING THIS CREATURE COUNTS AS AN ENEMY, which from a creature's point
	// of view is the player. `FindEnemiesInSphere` is the same search every
	// skill in the game uses, so a target that is out of reach of a swing is out
	// of reach of this too.
	const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
		World, Enemy, Enemy->GetActorLocation(), AuraRadiusCm);

	// THE ABYSSAL AURA'S DEBUFF AND HOW LONG IT LASTS, read from its own row
	// rather than written here. `Debuff_Abyssal_Aura` in
	// `game/Data/StatusEffects.csv` carries a strength of 25, a duration of 2
	// seconds, and the two resistances it cuts. Every one of those was empty
	// until 2026-09-05, so the effect applied nothing however it was reached.
	FGameplayTag AbyssalTag;
	float AbyssalSeconds = 0.0f;
	if (bStripsResistance)
	{
		AbyssalTag = UCataclysmSkillShapes::StatusTagFor(TEXT("Abyssal Aura"));
		AbyssalSeconds =
			UCataclysmSkillEffects::NumbersForEffectTag(AbyssalTag).DurationSeconds;
	}

	// AND STARVING AURA'S, from `Debuff_Starving_Aura` in the same table: a
	// strength of 50 and a duration of 2 seconds. Issue #2539.
	FGameplayTag StarvingTag;
	FCataclysmStatusEffectNumbers Starving;
	if (bStarves)
	{
		StarvingTag = UCataclysmSkillShapes::StatusTagFor(TEXT("Starving Aura"));
		Starving = UCataclysmSkillEffects::NumbersForEffectTag(StarvingTag);
	}

	// AND HELLFIRE AURA'S BURN ROW, for its duration: `DoT_Burn`, four seconds. Its per-tick figure is the carrier's
	// own since issue #2749's enemy burn slice; see `HellfireAuraBurnPerTick`.
	FCataclysmStatusEffectNumbers Burn;
	if (bBurns)
	{
		Burn = UCataclysmSkillEffects::BurnNumbers();
	}

	for (AActor* Target : Caught)
	{
		bool bAnythingLanded = false;

		// THE BURN IS DESIGNED, which is what the parameter means: "true when
		// the skill's own row states it burns". Hellfire Aura's row is "Emits a
		// burning aura that deals constant fire damage to nearby players", and
		// `DoT_Burn`'s own row names this modifier as one of the two that apply
		// it. So there is no hit to measure a threshold against and none is
		// wanted -- an aura that only caught a player who had just been hit hard
		// would not be an aura.
		//
		// SIZED FROM THE CARRIER SINCE ISSUE #2749'S ENEMY BURN SLICE, NOT THE TABLE'S 25: a tick is
		// `HellfireAuraBurnPerTick` of the carrier's weapon damage, so the burn grows with the tier, ring, floor, rarity
		// and kind its hits already carry. Ruled by the coordinating session on 2026-11-19; see the constant. The
		// table's row still gives the duration and the one stack, and the burn still skips the threshold, as above.
		if (bBurns && Burn.bUsable)
		{
			const float PerTick = HellfireAuraBurnPerTick(
				UCataclysmSkillEffects::WeaponDamageOf(UCataclysmTargeting::AbilitySystemOf(Enemy)));
			if (UCataclysmSkillEffects::ApplyDamageOverTime(Enemy, Target, PerTick, Burn.DurationSeconds,
															UCataclysmSkillEffects::BurnTag(),
															/*bScalesWithInstigator=*/true))
			{
				bAnythingLanded = true;
			}
		}

		// AND THE RESISTANCE CUT, THROUGH THE SHARED NAMED-EFFECT PATH. That
		// path reads which stats to move out of the row's own `MovesStat`
		// column, added for issue #1144, and this is the effect that made the
		// column necessary: it cuts TWO resistances, which no rule written in
		// C++ for one stat could have said.
		//
		// NO MAGNITUDE PASSED, so the row's own strength of 25 is used. The
		// modifier states no figure of its own beyond what the debuff says.
		//
		// RE-APPLIED EVERY PULSE AND THAT IS THE POINT. The effect is a single
		// stack, so a second application refreshes the one that is there rather
		// than adding a second. A player standing in the aura keeps it; one who
		// walks out loses it when its two seconds run out.
		if (bStripsResistance && AbyssalSeconds > 0.0f
			&& UCataclysmSkillEffects::ApplyNamedEffect(
				Enemy, Target, AbyssalTag, AbyssalSeconds, /*Magnitude=*/0.0f,
				Enemy->DamageType))
		{
			bAnythingLanded = true;
		}

		// AND STARVING AURA'S CUT TO THE HEALTH THAT ARRIVES, re-applied every
		// pulse as Abyssal Aura's is, so it holds while the target stands in the
		// aura and lapses two seconds after it leaves. Issue #2539.
		if (bStarves && Starving.DurationSeconds > 0.0f && Starving.Strength > 0.0f
			&& EnemyModifiersApplyStarvingAura(Enemy, Target, StarvingTag,
											   Starving.DurationSeconds, Starving.Strength))
		{
			bAnythingLanded = true;
		}

		if (bAnythingLanded)
		{
			++Touched;
		}
	}

	return Touched;
}

float UCataclysmEnemyModifiers::CrowdControlResistancePercent(
	const TArray<FName>& Rows)
{
	float Percent = 0.0f;

	if (Carries(Rows, UnyieldingRow))
	{
		Percent += UnyieldingCrowdControlResistance;
	}

	return Percent;
}

float UCataclysmEnemyModifiers::BrandOnHit(AActor* Attacker, AActor* Target)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Attacker);
	if (Enemy == nullptr || !Carries(Enemy->ModifierRows, InfernalBrandRow))
	{
		return 0.0f;
	}

	// THE EXPLOSION IS NOT A HIT THAT BRANDS. Issue #1534. It is dealt further
	// down as an ordinary blow from this creature, so the target's attribute
	// set calls this function again from inside it. Branding there would put a
	// stack back onto the count the explosion had just spent, where the row
	// says the explosion consumes them all, and the next explosion would come
	// one hit early. Refused before the tag as well as the count, so the
	// explosion does not refresh the brand's visible status either.
	if (Enemy->bInfernalBrandExploding)
	{
		return 0.0f;
	}

	// NOR IS A TARGET ALREADY DEAD BRANDED. Issue #2874. The attribute set calls this after the blow's health write, so
	// for the blow that kills, after `HandleDeath` has marked the player dead: the owner's log of 2026-11-24 shows
	// "Infernal Brand reached five stacks on CataclysmPlayerState_0 and dealt 367" two milliseconds after the player's
	// death line. The brand was not part of the kill -- the swing alone took the health -- but it stacked and burst on a
	// corpse. A dead target takes no stack, no visible tag and no explosion.
	if (UCataclysmSkillEffects::IsDead(Target))
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* Branded =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Target));
	if (Branded == nullptr)
	{
		return 0.0f;
	}

	// THE TAG AS WELL AS THE COUNT. The count is what explodes; the tag is what a
	// player can see on themselves and what `Debuff_Infernal_Brand` in
	// `game/Data/StatusEffects.csv` names. Without it the brand would be
	// invisible until it went off.
	const FGameplayTag Brand =
		UCataclysmSkillShapes::StatusTagFor(TEXT("Infernal Brand"));
	if (Brand.IsValid())
	{
		UCataclysmSkillEffects::ApplyTagForDuration(
			Attacker, Target, Brand,
			UCataclysmStacks::WindowSecondsFor(
				ECataclysmStackKind::InfernalBrand));
	}

	if (!UCataclysmStacks::NoteInfernalBrand(Branded))
	{
		return 0.0f;
	}

	// AND THE BRAND'S TAG GOES WITH ITS STACKS. Issue #1537: the stacks were
	// spent above and the tag stayed for the rest of its eight seconds, so a
	// spent brand still counted as a debuff carried. Before the blow, so the
	// explosion's own damage does not see it either.
	UCataclysmSkillEffects::RemoveEffectsGranting(Target, Brand);

	// FIVE TIMES THIS CREATURE'S OWN ATTACK DAMAGE, which the project owner
	// chose: one hit's worth banked per stack. Read off the creature rather than
	// written here, so a Herald's brand is a Herald's brand.
	const UAbilitySystemComponent* Own =
		UCataclysmTargeting::AbilitySystemOf(Attacker);
	const float PerHit = Own
		? Own->GetNumericAttribute(
			  UCataclysmCombatAttributeSet::GetAttackDamageAttribute())
		: 0.0f;

	const float Damage = PerHit * InfernalBrandExplosionHits;
	if (Damage <= 0.0f)
	{
		return 0.0f;
	}

	// WHILE IT RESOLVES, THIS CREATURE IS MARKED AS EXPLODING, so the call the
	// blow makes back into this function is refused at the top. The guard puts
	// the flag back when this function returns, and the blow has resolved by
	// then: the effect carrying it is instant.
	TGuardValue<bool> Exploding(Enemy->bInfernalBrandExploding, true);

	// AN ORDINARY DIRECT BLOW. The damage TYPE is not a parameter here: it is
	// read off the instigator when the hit resolves, which is what makes the
	// explosion count as this creature's own element without saying so twice.
	//
	// AND NEVER A CRITICAL STRIKE, ruled by the coordinating session on 2026-11-24 (issue #2874), as the overkill
	// explosion and a minion's death blast were ruled on 2026-10-07: the owner chose five hits' worth, one banked per
	// stack, "a figure a player can reason about", and a critical made it ten. The owner's Boss Brute's explosion sent
	// 367, struck critically, and took the 396 health a level 6 character had left. Said here because a direct blow
	// from a creature rolls its critical strike chance otherwise.
	FCataclysmHitDelivery Explosion;
	Explosion.bCannotCriticallyStrike = true;
	UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Target, Damage, Explosion);

	UE_LOG(LogCataclysm, Log,
		   TEXT("%s's Infernal Brand reached five stacks on %s and dealt %.0f."),
		   *Enemy->GetName(), *GetNameSafe(Target), Damage);

	return Damage;
}

bool UCataclysmEnemyModifiers::WitherOnHit(AActor* Attacker, AActor* Target)
{
	const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Attacker);
	if (Enemy == nullptr || !Carries(Enemy->ModifierRows, WitheringBlowsRow))
	{
		return false;
	}

	// NOR A TARGET ALREADY DEAD, as Infernal Brand refuses one (issue #2874): the blow that kills reaches this after the
	// death. A judgement of the writing session, by the brand's reasoning: a stack on a corpse is cleared at the respawn
	// and does nothing before it.
	if (UCataclysmSkillEffects::IsDead(Target))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* Withered =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Target));
	if (Withered == nullptr)
	{
		return false;
	}

	// THE COUNT'S OWN STATUS, FOR THE COUNT'S OWN WINDOW, as Infernal Brand's
	// tag is: put on, or refreshed, by every hit that adds a stack, so it lasts
	// exactly as long as the count does and goes when the count lapses.
	const FGameplayTag Withering =
		UCataclysmSkillShapes::StatusTagFor(TEXT("Withering"));
	if (Withering.IsValid())
	{
		UCataclysmSkillEffects::ApplyTagForDuration(
			Attacker, Target, Withering,
			UCataclysmStacks::WindowSecondsFor(ECataclysmStackKind::Withering));
	}

	if (!UCataclysmStacks::NoteWithering(Withered))
	{
		return false;
	}

	// AT TEN THE BUILDING STATUS GOES, as the brand's tag goes with its stacks
	// (issue #1537), and the slow comes. Its figure and its seconds are the
	// `Debuff_Wither` row's, read here and stated on the application, so a
	// re-tune is a data change and the reader finds what was applied.
	UCataclysmSkillEffects::RemoveEffectsGranting(Target, Withering);

	const FGameplayTag Wither = UCataclysmSkillShapes::StatusTagFor(TEXT("Wither"));
	if (!Wither.IsValid())
	{
		return false;
	}
	const FCataclysmStatusEffectNumbers Numbers =
		UCataclysmSkillEffects::NumbersForEffectTag(Wither);
	UCataclysmSkillEffects::ApplyTagForDuration(Attacker, Target, Wither,
												Numbers.DurationSeconds, Numbers.Strength);

	UE_LOG(LogCataclysm, Log,
		   TEXT("%s's Withering Blows reached ten stacks on %s: %.0f%% slower for %.1f s."),
		   *Enemy->GetName(), *GetNameSafe(Target), Numbers.Strength, Numbers.DurationSeconds);

	return true;
}

bool UCataclysmEnemyModifiers::TouchOfNothingOnHit(AActor* Attacker, AActor* Target)
{
	const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Attacker);
	if (Enemy == nullptr || !Carries(Enemy->ModifierRows, TouchOfNothingRow))
	{
		return false;
	}

	// NOR A TARGET ALREADY DEAD, as Infernal Brand and Withering Blows refuse one (issue #2874). The same judgement.
	if (UCataclysmSkillEffects::IsDead(Target))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* Touched =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Target));
	if (Touched == nullptr)
	{
		return false;
	}

	// THE STATUS FOR THE STACKS' WINDOW, put on or refreshed by every hit that adds one, as Withering's is: it
	// lasts as long as the count and is what the count is shown beside. `NegatesTheNextBuff` takes it off with
	// the last stack. It does nothing by itself.
	const FGameplayTag Status = UCataclysmSkillShapes::StatusTagFor(TEXT("Touch of Nothing"));
	if (Status.IsValid())
	{
		UCataclysmSkillEffects::ApplyTagForDuration(
			Attacker, Target, Status,
			UCataclysmStacks::WindowSecondsFor(ECataclysmStackKind::TouchOfNothing));
	}
	UCataclysmStacks::NoteTouchOfNothing(Touched);
	return true;
}

bool UCataclysmEnemyModifiers::CharmWhoeverStruck(AActor* Struck,
												  AActor* Striker)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Struck);
	if (Enemy == nullptr || !Carries(Enemy->ModifierRows, BeguilingRow))
	{
		return false;
	}

	if (Striker == nullptr || UCataclysmSkillEffects::IsDead(Enemy))
	{
		return false;
	}

	// **THE COOLDOWN IS WHAT STOPS IT BEING PERMANENT.** The charm triggers on
	// the creature TAKING damage, so without one a player attacking at any
	// reasonable speed would re-apply it before it expired and never be out of
	// it. The project owner added it on 2026-09-05; the row states none.
	const UWorld* World = Enemy->GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	if (Enemy->CharmLastAppliedAt >= 0.0f
		&& Now - Enemy->CharmLastAppliedAt < BeguilingCooldownSeconds)
	{
		return false;
	}

	const FGameplayTag Charm =
		UCataclysmSkillShapes::StatusTagFor(TEXT("Beguiling"));
	if (!Charm.IsValid())
	{
		return false;
	}

	if (!UCataclysmSkillEffects::ApplyTagForDuration(Enemy, Striker, Charm,
													BeguilingSeconds))
	{
		return false;
	}

	Enemy->CharmLastAppliedAt = Now;

	UE_LOG(LogCataclysm, Log, TEXT("%s beguiled %s for %.0f seconds."),
		   *Enemy->GetName(), *GetNameSafe(Striker), BeguilingSeconds);

	return true;
}

float UCataclysmEnemyModifiers::ShareOfDamageKept(AActor* Character, TArray<AActor*>* OutSharing)
{
	if (OutSharing)
	{
		OutSharing->Reset();
	}

	const ACataclysmEnemyCharacter* Enemy =
		Cast<ACataclysmEnemyCharacter>(Character);
	if (Enemy == nullptr || !Carries(Enemy->ModifierRows, SacrificialBondRow))
	{
		return 1.0f;
	}

	const UWorld* World = Enemy->GetWorld();
	if (World == nullptr)
	{
		return 1.0f;
	}

	TArray<AActor*> Allies = UCataclysmTargeting::FindAlliesInSphere(
		World, Enemy, Enemy->GetActorLocation(), SacrificialBondReach);

	// ONLY A CREATURE THAT CAN LOSE HEALTH SHARES. Issue #1559, ruled 2026-09-30.
	// A floor-rule object is not an ally in the row's sense, and a bonded
	// creature must not push damage onto an objective the player is breaking. A
	// creature that cannot be hurt, or is shrouded, would take its share and
	// lose nothing, which would halve the hit for free. A player's thrall or
	// minion is never here: it is on the player's team, and the search asks for
	// this creature's.
	//
	// AND NOT ONE BEHIND A CLOSED SECTION BARRIER. Issues #1820 and #41, ruled 2026-10-08. The search above has no wall
	// test, so on a floor in sections it finds an ally through a barrier, and a bonded creature that cannot be
	// reached would then share every blow with one that can. Left out when both creatures carry a section, the
	// sections differ, and a barrier between them is closed. A creature with no section is treated as it always
	// was, and so is every creature on a floor with no sections: the game mode is asked only when this creature
	// carries a section, and is found by walking the level, as the player's revival finds it.
	const ACataclysmDungeonGameMode* SectionsOf = Enemy->FloorSection != INDEX_NONE
		? ACataclysmDungeonGameMode::InWorld(Enemy->GetWorld())
		: nullptr;
	Allies.RemoveAll([Enemy, SectionsOf](const AActor* Ally)
	{
		const ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Ally);
		return Creature == nullptr
			|| Creature->IsA<ACataclysmFloorSourceCharacter>()
			|| Creature->bCannotBeHurt
			|| Creature->bShrouded
			|| (SectionsOf != nullptr && SectionsOf->AClosedBarrierStandsBetween(Enemy, Creature));
	});

	// NOBODY TO SHARE WITH MEANS IT KEEPS ALL OF IT, which is what makes the
	// modifier answerable: pull the creature away from its pack, or kill the
	// pack first.
	if (Allies.IsEmpty())
	{
		return 1.0f;
	}

	// DIVIDED EVENLY BETWEEN THE CREATURE AND ITS ALLIES, reading the row as
	// written: one ally halves what it takes, three leave it a quarter.
	if (OutSharing)
	{
		*OutSharing = Allies;
	}
	return 1.0f / static_cast<float>(Allies.Num() + 1);
}

bool UCataclysmEnemyModifiers::IsProtectedBySigil(const AActor* Character)
{
	const ACataclysmEnemyCharacter* Standing =
		Cast<ACataclysmEnemyCharacter>(Character);
	if (Standing == nullptr)
	{
		return false;
	}

	const UWorld* World = Standing->GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	// ASKED OF WHOEVER IS ABOUT TO DIE, by looking for a sigil-caster it is an
	// ally of. That is the opposite of keeping a flag on the protected creature,
	// and it is deliberate: a creature that walks out of the sigil is killable
	// on the next blow with nothing to clear and nothing to expire.
	for (TActorIterator<ACataclysmEnemyCharacter> It(
			 const_cast<UWorld*>(World)); It; ++It)
	{
		const ACataclysmEnemyCharacter* Caster = *It;
		if (Caster == nullptr || Caster == Standing)
		{
			continue;
		}

		if (!Carries(Caster->ModifierRows, UnholySigilsRow)
			|| Caster->SigilSecondsLeft <= 0.0f)
		{
			continue;
		}

		if (Caster->GetTeamAttitudeTowards(*Standing) != ETeamAttitude::Friendly)
		{
			continue;
		}

		const float Away =
			(Standing->GetActorLocation() - Caster->SigilCentre).Size();
		if (Away <= SigilRadiusCm)
		{
			return true;
		}
	}

	return false;
}

namespace
{
	/**
	 * One step of Death's Shadow Step on one creature: hidden while it is not attacking, seen from the moment an attack
	 * begins until `ShadowStepSeenSecondsAfterAnAttack` after it ends. 2026-11-24.
	 *
	 * HIDDEN AS THE VISION SYSTEM HIDES A CREATURE BEYOND THE PLAYER'S SIGHT, with `SetActorHiddenInGame`, so the bar,
	 * the click and the status line all already answer it. AND ONLY UNDONE WHERE SHADOW STEP DID IT, which is what
	 * `bHiddenByShadowStep` is for: a creature the vision system hid is never shown here. When the vision system shows
	 * one this hid, the mark is dropped and the next step hides it again, a quarter of a second at worst.
	 *
	 * A CREATURE THAT TAKES NO HOSTILE ACTION IS NEVER HIDDEN, the medic's rule (#1680): it never attacks, so it would
	 * be invisible for the whole floor, and the floor's medic is a creature the player has to be able to find.
	 *
	 * @return whether it changed what is drawn
	 */
	bool EnemyModifiersShadowStepFor(ACataclysmEnemyCharacter* Stepping, float StepSeconds)
	{
		const bool bCarries =
			UCataclysmEnemyModifiers::CarriedBy(Stepping, UCataclysmEnemyModifiers::ShadowStepRow)
			&& !Stepping->TakesNoHostileAction();

		// THE VISION SYSTEM SHOWED IT SINCE: the mark is stale, and the creature is not this rule's to show.
		if (Stepping->bHiddenByShadowStep && !Stepping->IsHidden())
		{
			Stepping->bHiddenByShadowStep = false;
		}

		if (!bCarries)
		{
			if (Stepping->bHiddenByShadowStep)
			{
				Stepping->SetActorHiddenInGame(false);
				Stepping->bHiddenByShadowStep = false;
				return true;
			}
			return false;
		}

		// ATTACKING IS EVERY STATE IN WHICH A BLOW IS COMING: an ordinary swing's blow waiting, an ability winding up, a
		// charge running, Inferno Charge's channel. The brain also reveals it the moment it orders either kind of
		// attack, so a swing whose blow lands inside one step is still seen.
		const ACataclysmEnemyController* Brain = Cast<ACataclysmEnemyController>(Stepping->GetController());
		const bool bAttacking = Stepping->HasPendingBasicAttack() || Stepping->IsCharging()
			|| Stepping->IsChannellingInfernoCharge() || (Brain && Brain->WindingUpAbility >= 0);
		if (bAttacking)
		{
			Stepping->ShadowStepSeenSecondsLeft = UCataclysmEnemyModifiers::ShadowStepSeenSecondsAfterAnAttack;
		}
		else
		{
			Stepping->ShadowStepSeenSecondsLeft =
				FMath::Max(0.0f, Stepping->ShadowStepSeenSecondsLeft - StepSeconds);
		}

		if (Stepping->ShadowStepSeenSecondsLeft > 0.0f)
		{
			if (Stepping->bHiddenByShadowStep)
			{
				Stepping->SetActorHiddenInGame(false);
				Stepping->bHiddenByShadowStep = false;
				return true;
			}
			return false;
		}

		if (!Stepping->IsHidden())
		{
			Stepping->SetActorHiddenInGame(true);
			Stepping->bHiddenByShadowStep = true;
			return true;
		}
		return false;
	}

	/**
	 * One step of Death's Shadow Step on a wearer that is NOT A CREATURE, a player above all. Issue #2899 (2026-11-26).
	 *
	 * NEVER HIDDEN FROM ITS OWN SCREEN: nothing here touches what is drawn. It is hidden from creatures' sight, which
	 * `UCataclysmEnemyModifiers::IsHiddenByShadowStep` answers from the seconds left seen, and this runs them down. Its
	 * attacks are every skill it uses, which `UCataclysmCombatEvents::NoteSkillUsed` reveals; there is no brain to ask
	 * about a swing waiting.
	 *
	 * @return whether it went from seen to hidden this step
	 */
	bool EnemyModifiersShadowStepForAWearer(ACataclysmCharacterBase* Stepping, float StepSeconds)
	{
		if (!UCataclysmEnemyModifiers::CarriedBy(Stepping, UCataclysmEnemyModifiers::ShadowStepRow)
			|| Stepping->TakesNoHostileAction())
		{
			Stepping->ShadowStepSeenSecondsLeft = 0.0f;
			return false;
		}
		const bool bWasSeen = Stepping->ShadowStepSeenSecondsLeft > 0.0f;
		Stepping->ShadowStepSeenSecondsLeft = FMath::Max(0.0f, Stepping->ShadowStepSeenSecondsLeft - StepSeconds);
		return bWasSeen && Stepping->ShadowStepSeenSecondsLeft <= 0.0f;
	}

	/** The side a necrotic zone left by `Wearer` cuts the healing of: the other one. Issue #2899. */
	FGenericTeamId EnemyModifiersSideADecayZoneReaches(const AActor* Wearer)
	{
		return UCataclysmTeams::TeamOf(Wearer) == UCataclysmTeams::IdFor(ECataclysmTeam::Players)
			? UCataclysmTeams::IdFor(ECataclysmTeam::Monsters)
			: UCataclysmTeams::IdFor(ECataclysmTeam::Players);
	}

	/**
	 * The Death modifiers that run on a clock of their own, on any living wearer, a creature or a player: Spectral
	 * Shift, Shadow Step and Lingering Decay, and Undying Will's rising made ready again once it is no longer carried.
	 * Written 2026-11-24 for creatures inside `UCataclysmEnemyModifiers::TimedStep`; its own function since issue #2899
	 * (2026-11-26), which made each work on any wearer. Each reads its row through `UCataclysmEnemyModifiers::
	 * CarriedBy`, so a player wearing one Wearer of the Dead lent runs it as a creature carrying it does.
	 *
	 * @return how many of them acted this step
	 */
	int32 EnemyModifiersDeathClocksFor(ACataclysmCharacterBase* Wearer, float StepSeconds)
	{
		int32 Acted = 0;

		// -- Spectral Shift, 2026-11-24 ---------------------------------------
		// OUT OF EXISTENCE FOR FIVE SECONDS IN EVERY FIFTEEN. Run down first, so a phase begun this step is not shortened
		// by it. While seconds are left, `UCataclysmEnemyModifiers::IsPhasedOut` answers true and every blow is emptied
		// where the Reaper's and a shrouded creature's are (a creature's through `ACataclysmEnemyCharacter::
		// TakesNoDamage`). THE CLOCK STARTS AGAIN FROM NOUGHT WHEN THE ROW IS NOT CARRIED (issue #2899), so a player lent
		// it for 20 seconds shifts out at 15 for the last 5 of them, every time it is lent; a creature always carries its
		// own, so nothing changes for one.
		if (Wearer->SpectralShiftSecondsLeft > 0.0f)
		{
			Wearer->SpectralShiftSecondsLeft = FMath::Max(0.0f, Wearer->SpectralShiftSecondsLeft - StepSeconds);
		}
		if (UCataclysmEnemyModifiers::CarriedBy(Wearer, UCataclysmEnemyModifiers::SpectralShiftRow))
		{
			Wearer->SecondsSinceSpectralShift += StepSeconds;
			if (Wearer->SecondsSinceSpectralShift >= UCataclysmEnemyModifiers::SpectralShiftIntervalSeconds)
			{
				Wearer->SecondsSinceSpectralShift = 0.0f;
				Wearer->SpectralShiftSecondsLeft = UCataclysmEnemyModifiers::SpectralShiftOutSeconds;
				++Acted;
				UE_LOG(LogCataclysm, Log, TEXT("%s shifted out of existence for %.0f seconds."),
					   *Wearer->GetName(), UCataclysmEnemyModifiers::SpectralShiftOutSeconds);
			}
		}
		else
		{
			Wearer->SecondsSinceSpectralShift = 0.0f;
		}

		// -- Shadow Step, 2026-11-24 ------------------------------------------
		// A CREATURE IS HIDDEN FROM SIGHT; ANY OTHER WEARER FROM CREATURES' SIGHT ONLY.
		if (ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Wearer))
		{
			if (EnemyModifiersShadowStepFor(Creature, StepSeconds))
			{
				++Acted;
			}
		}
		else if (EnemyModifiersShadowStepForAWearer(Wearer, StepSeconds))
		{
			++Acted;
		}

		// -- Lingering Decay, 2026-11-24 --------------------------------------
		// A NECROTIC ZONE EVERY FIVE SECONDS WHERE IT STANDS, on the floor under it, through the zone every creature's
		// ground already uses, so it is drawn. It deals nothing; a heal asks whether its receiver stands in one
		// (`LingeringDecayHealingLessPercentAt`). NOT FROM A WEARER THAT TAKES NO HOSTILE ACTION, the medic's rule
		// (#1680). IT CUTS THE OTHER SIDE'S HEALING (issue #2899): a creature's cuts the players' side, the row's
		// "players", and a player's cuts the creatures'.
		if (UCataclysmEnemyModifiers::CarriedBy(Wearer, UCataclysmEnemyModifiers::LingeringDecayRow)
			&& !Wearer->TakesNoHostileAction())
		{
			Wearer->SecondsSinceDecayZone += StepSeconds;
			if (Wearer->SecondsSinceDecayZone >= UCataclysmEnemyModifiers::LingeringDecaySecondsBetweenZones)
			{
				Wearer->SecondsSinceDecayZone = 0.0f;
				const FVector Feet = ACataclysmEnemyController::FloorUnder(Wearer, Wearer->GetActorLocation());
				if (ACataclysmGroundZone* Decay = ACataclysmGroundZone::Spawn(
						Wearer, Feet, UCataclysmEnemyModifiers::LingeringDecayRadiusCm,
						UCataclysmEnemyModifiers::LingeringDecayZoneSeconds, /*DamagePerTick=*/0.0f))
				{
					Decay->HealingLessPercentInside = UCataclysmEnemyModifiers::LingeringDecayHealingLessPercent;
					Decay->HealingLessReachesTeamId = EnemyModifiersSideADecayZoneReaches(Wearer).GetId();
					++Acted;
					UE_LOG(LogCataclysm, Log, TEXT("%s left a necrotic zone lasting %.0f seconds."),
						   *Wearer->GetName(), UCataclysmEnemyModifiers::LingeringDecayZoneSeconds);
				}
			}
		}
		else
		{
			Wearer->SecondsSinceDecayZone = 0.0f;
		}

		// -- Undying Will, issue #2899 ----------------------------------------
		// A WEARER THAT IS NOT A CREATURE MAY RISE ONCE FOR EACH TIME IT IS WORN: once it no longer carries the row, its
		// rising is ready again and the damage it rose with is gone. See `RiseThroughUndyingWill`.
		if ((Wearer->bUndyingWillSpent || Wearer->bRoseThroughUndyingWill)
			&& !UCataclysmEnemyModifiers::CarriedBy(Wearer, UCataclysmEnemyModifiers::UndyingWillRow))
		{
			Wearer->bUndyingWillSpent = false;
			Wearer->bRoseThroughUndyingWill = false;
			++Acted;
		}
		return Acted;
	}

	/**
	 * Death's Lingering Decay on a character of the players' side: `Status.Debuff.LingeringDecay` laid, or refreshed,
	 * while it stands in a necrotic zone, so the player's effects list names the cut. Added 2026-11-25 with the
	 * Debuffs sheet's row, which the owner accepted on 2026-11-24 ("These are fine").
	 *
	 * THE CUT IS NOT THIS TAG'S. `UCataclysmEnemyModifiers::LingeringDecayHealingLessPercentAt` still asks where the
	 * character stands, so the cut holds exactly while it is inside; the tag is laid for
	 * `LingeringDecayStatusSeconds` on every quarter-second step it is inside, so it shows within a step of entering
	 * and goes within that time of leaving. IN THE NAME OF THE CREATURE THAT LEFT THE DEEPEST ZONE COVERING IT, as
	 * every debuff is laid in its applier's name; when that creature has gone, nothing is laid, and the cut alone
	 * remains for the rest of the zone's life. A judgement, recorded.
	 *
	 * @return whether the tag was laid or refreshed
	 */
	bool EnemyModifiersMarkLingeringDecay(AActor* Standing)
	{
		// ANY SIDE A ZONE REACHES, since issue #2899: a zone a player left cuts creatures' healing. A character of no
		// side is reached by none.
		if (!IsValid(Standing) || UCataclysmSkillEffects::IsDead(Standing)
			|| UCataclysmTeams::TeamOf(Standing) == FGenericTeamId::NoTeam)
		{
			return false;
		}
		UWorld* StandingWorld = Standing->GetWorld();
		if (StandingWorld == nullptr)
		{
			return false;
		}
		const uint8 StandingSide = UCataclysmTeams::TeamOf(Standing).GetId();

		const ACataclysmGroundZone* Deepest = nullptr;
		float DeepestCut = 0.0f;
		for (TActorIterator<ACataclysmGroundZone> It(StandingWorld); It; ++It)
		{
			const ACataclysmGroundZone* Zone = *It;
			if (!IsValid(Zone) || Zone->HealingLessPercentInside <= DeepestCut
				|| Zone->HealingLessReachesTeamId != StandingSide)
			{
				continue;
			}
			if (Zone->Covers(Standing->GetActorLocation()))
			{
				DeepestCut = Zone->HealingLessPercentInside;
				Deepest = Zone;
			}
		}

		AActor* LeftBy = Deepest ? Deepest->GetOwner() : nullptr;
		const FGameplayTag Decay = UCataclysmSkillShapes::StatusTagFor(TEXT("Lingering Decay"));
		if (!IsValid(LeftBy) || !Decay.IsValid())
		{
			return false;
		}
		return UCataclysmSkillEffects::ApplyTagForDuration(LeftBy, Standing, Decay,
														   UCataclysmEnemyModifiers::LingeringDecayStatusSeconds);
	}

	/**
	 * War's Battle Cry, one shout: `Status.Debuff.BattleCry` on every enemy of `Shouter` within
	 * `BattleCryReachCm`, through the named-effect path, which cuts each resistance its row names by
	 * `BattleCryResistanceCut`, never below nought, for `BattleCrySeconds`. 2026-11-25.
	 *
	 * STATED HERE AND IN THE ROW, ONE FIGURE: the row's `MovesStat` says which stats move and this says by how much and
	 * for how long, the figures `tools/tests/test_war_enemy_modifiers_match_their_rows.py` holds equal to the row's.
	 *
	 * @param OutFound  how many enemies were within reach, whether or not each took the debuff: a shout at an
	 *                  enemy that refuses debuffs is still a shout
	 * @return how many took it
	 */
	int32 EnemyModifiersShoutBattleCry(ACataclysmEnemyCharacter* Shouter, const UWorld* ShoutWorld, int32& OutFound)
	{
		OutFound = 0;
		const FGameplayTag Cry = UCataclysmSkillShapes::StatusTagFor(TEXT("Battle Cry"));
		if (!Cry.IsValid() || ShoutWorld == nullptr)
		{
			return 0;
		}

		const TArray<AActor*> Heard = UCataclysmTargeting::FindEnemiesInSphere(
			ShoutWorld, Shouter, Shouter->GetActorLocation(), UCataclysmEnemyModifiers::BattleCryReachCm);
		OutFound = Heard.Num();

		int32 Cowed = 0;
		for (AActor* Target : Heard)
		{
			if (UCataclysmSkillEffects::ApplyNamedEffect(Shouter, Target, Cry, UCataclysmEnemyModifiers::BattleCrySeconds,
														 UCataclysmEnemyModifiers::BattleCryResistanceCut,
														 Shouter->DamageType))
			{
				++Cowed;
			}
		}

		if (OutFound > 0)
		{
			UE_LOG(LogCataclysm, Log, TEXT("%s let out a Battle Cry: %d of %d within reach lose %.0f of every resistance "
										   "for %.0f seconds."),
				   *Shouter->GetName(), Cowed, OutFound, UCataclysmEnemyModifiers::BattleCryResistanceCut,
				   UCataclysmEnemyModifiers::BattleCrySeconds);
		}
		return Cowed;
	}

	/** Takes away every circle a barrage drew and forgets where its shells were to land. */
	void EnemyModifiersCallOffArtilleryBarrage(ACataclysmEnemyCharacter* Caller)
	{
		for (const TWeakObjectPtr<ACataclysmTelegraphMarker>& Marker : Caller->ArtilleryShellMarkers)
		{
			if (ACataclysmTelegraphMarker* Drawn = Marker.Get())
			{
				Drawn->Dismiss();
			}
		}
		Caller->ArtilleryShellMarkers.Reset();
		Caller->ArtilleryShellsAt.Reset();
		Caller->ArtilleryShellsSecondsLeft = 0.0f;
	}

	/**
	 * War's Artillery Barrage, the call: `ArtilleryBarrageShells` points at random within
	 * `ArtilleryBarrageLandsWithinCm` of the nearest enemy within `ArtilleryBarrageReachCm`, each drawn as a circle on
	 * the floor for `ArtilleryBarrageWarningSeconds`. 2026-11-25.
	 *
	 * FIXED WHEN CALLED AND DRAWN AT ONCE, the general telegraph rule: a shell lands where its circle was drawn, not
	 * where the player has gone. A SEEDED DRAW, as Phasewalker's direction is, salted so it is not the creature's other
	 * draws on the same clock.
	 *
	 * @return whether a volley was called: false with nobody in reach, so the clock waits for one
	 */
	bool EnemyModifiersCallArtilleryBarrage(ACataclysmEnemyCharacter* Caller, const UWorld* CallWorld)
	{
		if (CallWorld == nullptr)
		{
			return false;
		}
		const TArray<AActor*> Nearest = UCataclysmTargeting::FindEnemiesInSphere(
			CallWorld, Caller, Caller->GetActorLocation(), UCataclysmEnemyModifiers::ArtilleryBarrageReachCm,
			/*MaxTargets=*/1);
		if (Nearest.IsEmpty() || Nearest[0] == nullptr)
		{
			return false;
		}

		EnemyModifiersCallOffArtilleryBarrage(Caller);
		const FVector Centre = Nearest[0]->GetActorLocation();
		FRandomStream Stream(CataclysmSeedMix::Mix(static_cast<int32>(Caller->GetUniqueID()),
												   UCataclysmEnemyModifiers::ArtilleryBarrageDrawSalt,
												   static_cast<int32>(CallWorld->GetTimeSeconds() * 1000.0f)));
		for (int32 Shell = 0; Shell < UCataclysmEnemyModifiers::ArtilleryBarrageShells; ++Shell)
		{
			const float Angle = Stream.FRandRange(0.0f, 2.0f * PI);
			const float Away = Stream.FRandRange(0.0f, UCataclysmEnemyModifiers::ArtilleryBarrageLandsWithinCm);
			const FVector Aimed(Centre.X + Away * FMath::Cos(Angle), Centre.Y + Away * FMath::Sin(Angle), Centre.Z);
			const FVector Where = ACataclysmEnemyController::FloorUnder(Caller, Aimed);
			Caller->ArtilleryShellsAt.Add(Where);
			Caller->ArtilleryShellMarkers.Emplace(ACataclysmTelegraphMarker::ShowCircle(
				Caller, Where, UCataclysmEnemyModifiers::ArtilleryBarrageShellRadiusCm,
				UCataclysmEnemyModifiers::ArtilleryBarrageWarningSeconds));
		}
		Caller->ArtilleryShellsSecondsLeft = UCataclysmEnemyModifiers::ArtilleryBarrageWarningSeconds;

		UE_LOG(LogCataclysm, Log, TEXT("%s called an Artillery Barrage of %d shells on %s, landing in %.1f seconds."),
			   *Caller->GetName(), UCataclysmEnemyModifiers::ArtilleryBarrageShells, *Nearest[0]->GetName(),
			   UCataclysmEnemyModifiers::ArtilleryBarrageWarningSeconds);
		return true;
	}

	/**
	 * War's Artillery Barrage, the landing: each shell an area blow of `ArtilleryBarrageShellHits` of the caller's
	 * own attack damage on every enemy of its side inside its circle. 2026-11-25.
	 *
	 * EACH SHELL ITS OWN BLOW, so a player where two circles overlap takes two, as Diablo III's mortars each strike.
	 * AN AREA BLOW, which cannot be evaded and can be blocked, AND NEVER A CRITICAL STRIKE: the figure is hits banked,
	 * as Funeral Pyre's and Infernal Brand's are (#2874). Of the caller's own damage type, read off it as any of its
	 * blows is. READ AS THE SHELLS LAND, so a creature made stronger meanwhile lands stronger shells.
	 *
	 * @return how many blows landed
	 */
	int32 EnemyModifiersLandArtilleryShells(ACataclysmEnemyCharacter* Caller, const UWorld* LandWorld)
	{
		int32 Landed = 0;
		const UAbilitySystemComponent* Own = UCataclysmTargeting::AbilitySystemOf(Caller);
		const float ShellDamage = Own
			? FMath::Max(0.0f, Own->GetNumericAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute()))
				* UCataclysmEnemyModifiers::ArtilleryBarrageShellHits
			: 0.0f;
		if (LandWorld != nullptr && ShellDamage > 0.0f)
		{
			FCataclysmHitDelivery Shell;
			Shell.bIsArea = true;
			Shell.bCannotCriticallyStrike = true;
			for (const FVector& Where : Caller->ArtilleryShellsAt)
			{
				for (AActor* Target : UCataclysmTargeting::FindEnemiesInSphere(
						 LandWorld, Caller, Where, UCataclysmEnemyModifiers::ArtilleryBarrageShellRadiusCm))
				{
					if (UCataclysmSkillEffects::ApplyDirectDamage(Caller, Target, ShellDamage, Shell))
					{
						++Landed;
					}
				}
			}
		}

		UE_LOG(LogCataclysm, Log, TEXT("%s's Artillery Barrage landed: %d blows of %.0f."), *Caller->GetName(), Landed,
			   ShellDamage);
		EnemyModifiersCallOffArtilleryBarrage(Caller);
		return Landed;
	}
}

int32 UCataclysmEnemyModifiers::TimedStep(AActor* Character, float StepSeconds)
{
	// A BONE PYRE'S FUSE RUNS ON THIS STEP TOO, and a pyre carries no modifier, so it is asked before the test below
	// that turns away every creature carrying none. Death's Funeral Pyre, 2026-11-24.
	if (ACataclysmBonePyreCharacter* Pyre = Cast<ACataclysmBonePyreCharacter>(Character))
	{
		return PyreStep(Pyre, StepSeconds);
	}

	// AND DEATH'S LINGERING DECAY'S STATUS ON THE PLAYERS' SIDE, which comes through this step as every character does,
	// so it is asked before the test below that turns away every character carrying no modifier. Nought for anything
	// not of the players' side. 2026-11-25. See `EnemyModifiersMarkLingeringDecay`.
	int32 Acted = EnemyModifiersMarkLingeringDecay(Character) ? 1 : 0;

	// THE DEATH MODIFIERS THAT RUN ON A CLOCK, ON ANY WEARER: issue #2899 (2026-11-26), the owner's rule that every enemy
	// modifier works on any wearer, the player included. Asked of every living character this step reaches, before the
	// test below that turns away everything but a creature carrying a modifier of its own: a player's are lent to it.
	ACataclysmCharacterBase* Wearer = Cast<ACataclysmCharacterBase>(Character);
	if (Wearer == nullptr || UCataclysmSkillEffects::IsDead(Wearer))
	{
		return Acted;
	}

	UWorld* World = Wearer->GetWorld();
	if (World == nullptr)
	{
		return Acted;
	}

	Acted += EnemyModifiersDeathClocksFor(Wearer, StepSeconds);

	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
	if (Enemy == nullptr || Enemy->ModifierRows.IsEmpty())
	{
		return Acted;
	}

	// A SIGIL THAT IS UP RUNS DOWN, whether or not this creature carries the
	// modifier any more. Counted first so a sigil placed this step is not
	// expired by the same step.
	if (Enemy->SigilSecondsLeft > 0.0f)
	{
		Enemy->SigilSecondsLeft =
			FMath::Max(0.0f, Enemy->SigilSecondsLeft - StepSeconds);
	}

	// -- Phalanx, 2026-11-25 ----------------------------------------------
	// "10% DAMAGE REDUCTION FOR EVERY OTHER ENEMY WITHIN 5 METERS", counted every step and written to the creature's own
	// damage reduction, the stat step 5 of the damage calculation reads and caps at 75. ONLY ON A CHANGE, and nothing
	// else writes a creature's damage reduction (`ApplyStartingAttributes` never does), so the figure is the row's
	// alone. Not gated on hostility: it defends.
	if (Carries(Enemy->ModifierRows, PhalanxRow))
	{
		Enemy->PhalanxAlliesCounted = CountPhalanxAllies(Enemy);
		UAbilitySystemComponent* Braced = Enemy->GetAbilitySystemComponent();
		const FGameplayAttribute Reduction = UCataclysmCombatAttributeSet::GetDamageReductionAttribute();
		const float Wanted = PhalanxDamageReductionFor(Enemy->PhalanxAlliesCounted);
		if (Braced && Braced->HasAttributeSetForAttribute(Reduction)
			&& !FMath::IsNearlyEqual(Braced->GetNumericAttributeBase(Reduction), Wanted))
		{
			Braced->SetNumericAttributeBase(Reduction, Wanted);
			++Acted;
		}
	}

	// -- Battle Cry, 2026-11-25 -------------------------------------------
	// A SHOUT EVERY EIGHT SECONDS, ONLY WITH SOMEBODY TO HEAR IT: the clock waits at the interval until an enemy is in
	// reach, as Inferno Charge's waits for somebody to charge. NOT FROM A CREATURE THAT TAKES NO HOSTILE ACTION, the
	// medic's rule (#1680).
	if (Carries(Enemy->ModifierRows, BattleCryRow) && !Enemy->TakesNoHostileAction())
	{
		Enemy->SecondsSinceBattleCry += StepSeconds;
		if (Enemy->SecondsSinceBattleCry >= BattleCryIntervalSeconds)
		{
			int32 Heard = 0;
			EnemyModifiersShoutBattleCry(Enemy, World, Heard);
			if (Heard > 0)
			{
				Enemy->SecondsSinceBattleCry = 0.0f;
				++Acted;
			}
		}
	}

	// -- Artillery Barrage, 2026-11-25 ------------------------------------
	// A VOLLEY IN THE AIR IS COUNTED DOWN AND LANDS, AND THE CLOCK RUNS THROUGH ITS WARNING, so a volley is called every
	// six seconds, Bombardier's "6 second cooldown", the warning being shorter. A VOLLEY ALREADY CALLED LANDS whatever
	// the creature does next; one called off by its death never does (`NoteDeathOfACarrier`). NOT CALLED BY A CREATURE
	// THAT TAKES NO HOSTILE ACTION, the medic's rule (#1680).
	if (Enemy->ArtilleryShellsSecondsLeft > 0.0f)
	{
		Enemy->SecondsSinceArtilleryBarrage += StepSeconds;
		Enemy->ArtilleryShellsSecondsLeft -= StepSeconds;
		if (Enemy->ArtilleryShellsSecondsLeft <= KINDA_SMALL_NUMBER)
		{
			EnemyModifiersLandArtilleryShells(Enemy, World);
			++Acted;
		}
	}
	else if (Carries(Enemy->ModifierRows, ArtilleryBarrageRow) && !Enemy->TakesNoHostileAction())
	{
		Enemy->SecondsSinceArtilleryBarrage += StepSeconds;
		if (Enemy->SecondsSinceArtilleryBarrage >= ArtilleryBarrageIntervalSeconds
			&& EnemyModifiersCallArtilleryBarrage(Enemy, World))
		{
			Enemy->SecondsSinceArtilleryBarrage = 0.0f;
			++Acted;
		}
	}

	// -- Unholy Sigils --------------------------------------------------
	if (Carries(Enemy->ModifierRows, UnholySigilsRow))
	{
		Enemy->SecondsSinceSigil += StepSeconds;
		if (Enemy->SecondsSinceSigil >= SigilIntervalSeconds)
		{
			Enemy->SecondsSinceSigil = 0.0f;
			Enemy->SigilSecondsLeft = SigilSeconds;

			// WHERE IT STANDS WHEN IT CASTS, and it stays there. A sigil that
			// followed the creature would be an aura, and the row calls it a
			// sigil ON THE GROUND.
			Enemy->SigilCentre = Enemy->GetActorLocation();
			++Acted;

			UE_LOG(LogCataclysm, Log,
				   TEXT("%s laid an Unholy Sigil lasting %.0f seconds."),
				   *Enemy->GetName(), SigilSeconds);
		}
	}

	// -- Infernal Sacrifice ---------------------------------------------
	// A MEDIC DOES NOT EAT ITS OWN PATIENTS, AND THAT IS A SEPARATE RULING
	// FROM THE ONE ABOVE. Consuming an ally is plainly not an attack, so
	// "it does not attack" does not reach it. What reaches it is the other
	// half of the same row: the medic "constantly heals all other enemies in
	// a large radius". A trait that consumes one contradicts what the row
	// says the creature DOES, not merely what it is for. Issue #1680.
	if (Carries(Enemy->ModifierRows, InfernalSacrificeRow)
		&& !Enemy->TakesNoHostileAction())
	{
		Enemy->SecondsSinceSacrifice += StepSeconds;
		if (Enemy->SecondsSinceSacrifice >= SacrificeIntervalSeconds)
		{
			Enemy->SecondsSinceSacrifice = 0.0f;

			const TArray<AActor*> Allies =
				UCataclysmTargeting::FindAlliesInSphere(
					World, Enemy, Enemy->GetActorLocation(), AuraRadiusCm);

			// ONE AT A TIME AND THE NEAREST, which `FindAlliesInSphere` returns
			// first. A creature that ate its whole pack at once would replace
			// the fight rather than change it.
			for (AActor* Ally : Allies)
			{
				ACataclysmEnemyCharacter* Victim =
					Cast<ACataclysmEnemyCharacter>(Ally);
				// NOR ONE THAT CANNOT BE HURT, NOR ONE WHOSE DEATH IS ALREADY BEING MARKED. The coordinating
				// session, 2026-11-24: `HandleDeath` called directly skips the health hold that keeps such a
				// creature standing (`UCataclysmVitalAttributeSet::PreAttributeChange`), so a creature a rule had made
				// unhurtable (`bCannotBeHurt`) could be eaten by a carrier beside it. Passed over, as a dead ally is,
				// and the next nearest is taken. Issue #2874's branch.
				if (Victim == nullptr
					|| UCataclysmSkillEffects::IsDead(Victim)
					|| UCataclysmSkillEffects::IsBeingMarkedDead(Victim)
					|| Victim->bCannotBeHurt)
				{
					continue;
				}

				Victim->HandleDeath();

				// THROUGH TopUp, AS THE FIELD MEDIC'S HEAL IS, and not by writing
				// the attribute. Issue #1611: the write reached none of the rules
				// that stand between a restoration and a health bar -- the healing
				// received, its reduction, an ailment's rider on it, the ceiling --
				// so a diseased creature ate its pack-mate and healed the full
				// quarter. TopUp also stops at the maximum, as the write did.
				UAbilitySystemComponent* Own =
					UCataclysmTargeting::AbilitySystemOf(Enemy);
				if (Own)
				{
					const float Max = Own->GetNumericAttribute(
						UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
					UCataclysmRegeneration::TopUp(
						*Own, UCataclysmVitalAttributeSet::GetHealthAttribute(),
						UCataclysmVitalAttributeSet::GetMaxHealthAttribute(),
						Max * SacrificeHealShareOfHealth);
				}

				// AND COMMANDER, WHICH THE DESIGN CHOSE FOR THE ROW'S "temporary damage buffs": "The damage buff
				// is Commander" (docs/DECISIONS.md, 2026-09-05), rather than inventing a second buff. Commander is
				// 20% increased movement speed and attack speed (`Buff_Commander`, the owner's two stats of
				// 2026-08-20), so it hits more often, not harder. The row's own text still says "damage buffs";
				// issue #2876 is that wording, which lives in the design workbook and is changed there.
				const FGameplayTag Buff =
					UCataclysmSkillShapes::StatusTagFor(TEXT("Commander"));
				if (Buff.IsValid())
				{
					UCataclysmSkillEffects::ApplyTagForDuration(
						Enemy, Enemy, Buff, SacrificeBuffSeconds);
				}

				++Acted;

				UE_LOG(LogCataclysm, Log,
					   TEXT("%s sacrificed %s and healed."),
					   *Enemy->GetName(), *Victim->GetName());
				break;
			}
		}
	}

	// -- Inferno Charge -------------------------------------------------
	// A CHANNEL THE CREATURE MAY NO LONGER FINISH IS ABANDONED, rather than
	// left counting nowhere. The brain stands a channelling creature still, so
	// a channel frozen by the gate below would stand it still for good.
	if (Enemy->IsChannellingInfernoCharge()
		&& (!Carries(Enemy->ModifierRows, InfernoChargeRow)
			|| Enemy->TakesNoHostileAction()))
	{
		Enemy->InfernoChannelSecondsLeft = 0.0f;
		if (ACataclysmTelegraphMarker* Marker = Enemy->InfernoChargeMarker.Get())
		{
			Marker->Dismiss();
		}
		Enemy->InfernoChargeMarker = nullptr;
	}

	// AND A MEDIC NEVER CHARGES. Issue #1680, the same reading: a creature
	// that dashes at the player and deals damage on arrival is threatening.
	if (Carries(Enemy->ModifierRows, InfernoChargeRow) && !Enemy->IsCharging()
		&& !Enemy->TakesNoHostileAction())
	{
		if (Enemy->IsChannellingInfernoCharge())
		{
			// THE CHANNEL RUNS OUT, THEN THE CHARGE SETS OFF. Issue #1560. The
			// row: "Channel for 2 seconds, then dash towards the player".
			Enemy->InfernoChannelSecondsLeft -= StepSeconds;
			if (Enemy->InfernoChannelSecondsLeft <= KINDA_SMALL_NUMBER)
			{
				Enemy->InfernoChannelSecondsLeft = 0.0f;
				if (ACataclysmTelegraphMarker* Marker =
						Enemy->InfernoChargeMarker.Get())
				{
					Marker->Dismiss();
				}
				Enemy->InfernoChargeMarker = nullptr;

				// THE CHARGE EVERY CREATURE ALREADY HAS. `BeginCharge` is on the
				// base class and the Hellhound's own charge goes through it, so
				// the lane, the damage and the shove are one implementation a
				// player has already learned to read.
				//
				// TO THE POINT FIXED WHEN THE CHANNEL BEGAN, not where the player
				// is now: a player who walked out of the drawn lane is not
				// followed, which is what the two seconds buy.
				Enemy->BeginCharge(Enemy->InfernoChargeTo,
								   InfernoChargeSpeedCmPerSecond,
								   InfernoChargeHalfWidthCm,
								   InfernoChargeDamagePercent);

				// AND THE PATH BURNS, ONLY IF THE CHARGE REALLY SET OFF. A
				// creature standing in a pit is refused the charge, and a path
				// of fire where nothing ran would be a lie on the floor.
				//
				// LAID NOW, ALONG THE WHOLE RUN, AND PRICED ONCE, for the
				// reasons the Hellhound's `UseEnemyAbility` gives at length: a
				// patch appearing only on arrival burns nobody who stood in it
				// during the run, and a patch outlives the blow that left it.
				// It burns the player and nobody on the creature's own side.
				if (Enemy->IsCharging())
				{
					const float PerSecond =
						UCataclysmSkillEffects::WeaponDamageOf(
							UCataclysmTargeting::AbilitySystemOf(Enemy))
						* InfernoPathPercent / 100.0f;
					Enemy->LastInfernoPathLeftBurning =
						ACataclysmGroundZone::SpawnAlong(
							Enemy, Enemy->InfernoChargeFrom,
							Enemy->InfernoChargeTo, InfernoChargeHalfWidthCm,
							InfernoPathSeconds, PerSecond,
							/*bBurnsEveryone=*/false);
				}
				++Acted;

				UE_LOG(LogCataclysm, Log, TEXT("%s began an Inferno Charge."),
					   *Enemy->GetName());
			}
		}
		else
		{
			Enemy->SecondsSinceInfernoCharge += StepSeconds;

			// NOT WHILE ITS OWN ABILITY IS WINDING UP. The brain stands the
			// creature still for the channel above everything else it does, so
			// a channel begun mid wind-up would hold that wind-up past the
			// marker it drew. The timer keeps running and the channel begins
			// after it.
			const ACataclysmEnemyController* Brain =
				Cast<ACataclysmEnemyController>(Enemy->GetController());
			const bool bWindingUp = Brain && Brain->WindingUpAbility >= 0;

			if (!bWindingUp
				&& Enemy->SecondsSinceInfernoCharge
					   >= InfernoChargeIntervalSeconds - InfernoChannelSeconds)
			{
				const TArray<AActor*> Ahead =
					UCataclysmTargeting::FindEnemiesInSphere(
						World, Enemy, Enemy->GetActorLocation(),
						/*RadiusCm=*/2000.0f, /*MaxTargets=*/1);

				// NOTHING TO CHARGE MEANS THE TIMER KEEPS RUNNING, so the
				// creature channels as soon as somebody comes into range rather
				// than waiting another twelve seconds after they do.
				if (!Ahead.IsEmpty() && Ahead[0])
				{
					// ZERO, AND NOT COUNTED DURING THE CHANNEL, so it is still
					// zero when the charge sets off and the next one is twelve
					// seconds after this one, as it was before the channel.
					Enemy->SecondsSinceInfernoCharge = 0.0f;
					Enemy->InfernoChannelSecondsLeft = InfernoChannelSeconds;

					// THE LANE IS FIXED NOW AND DRAWN ON THE FLOOR, from the
					// creature's feet to the player's, through the one function
					// every enemy marker goes through. See
					// `ACataclysmEnemyController::FloorUnder`.
					Enemy->InfernoChargeFrom = ACataclysmEnemyController::FloorUnder(
						Enemy, Enemy->GetActorLocation());
					Enemy->InfernoChargeTo = ACataclysmEnemyController::FloorUnder(
						Enemy, Ahead[0]->GetActorLocation());
					Enemy->InfernoChargeMarker = ACataclysmTelegraphMarker::ShowLine(
						Enemy, Enemy->InfernoChargeFrom, Enemy->InfernoChargeTo,
						InfernoChargeHalfWidthCm, InfernoChannelSeconds);
					++Acted;

					UE_LOG(LogCataclysm, Log,
						   TEXT("%s began channelling an Inferno Charge."),
						   *Enemy->GetName());
				}
			}
		}
	}

	return Acted;
}

int32 UCataclysmEnemyModifiers::HealAlliesPulse(AActor* Character)
{
	ACataclysmEnemyCharacter* Medic = Cast<ACataclysmEnemyCharacter>(Character);
	if (Medic == nullptr)
	{
		// THE PLAYER AND ANY OTHER ACTOR COME THROUGH HERE HARMLESSLY, the same
		// refusal AuraStep makes for the same reason: the step this hangs off
		// is on the shared character base.
		return 0;
	}

	// A DEAD MEDIC HEALS NOBODY. The regeneration timer is cleared on death,
	// but a step already in flight would otherwise land afterwards -- the same
	// guard AuraStep carries above.
	//
	// AND IT IS WHY NO DEATH HOOK IS NEEDED. A creature-carried aura ends when
	// the creature does because nothing calls this for a dead one, rather than
	// because something noticed the death. That is the opposite of a floor
	// hazard, which must outlive the death that created it -- issue #1605.
	if (UCataclysmSkillEffects::IsDead(Medic))
	{
		return 0;
	}

	const UWorld* World = Medic->GetWorld();
	if (World == nullptr)
	{
		return 0;
	}

	// ALLIES AND NOT ENEMIES, and not itself: FindAlliesInSphere excludes the
	// instigator, which is what "all other enemies" asks for. It also excludes
	// the dead, so a corpse is not healed back into the fight.
	//
	// THE SAME RADIUS EVERY AURA IN THIS GAME USES. Six metres, the project
	// owner's decision of 2026-09-05, and the reason it is one number is that a
	// player learns the distance once.
	const TArray<AActor*> Allies = UCataclysmTargeting::FindAlliesInSphere(
		World, Medic, Medic->GetActorLocation(), AuraRadiusCm);

	int32 Healed = 0;
	for (AActor* Ally : Allies)
	{
		UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Ally);
		if (AbilitySystem == nullptr)
		{
			continue;
		}

		// A SHARE OF EACH ALLY'S OWN MAXIMUM RATHER THAN A FLAT AMOUNT, so one
		// figure works for a Common creature and for a boss without being
		// meaningless to one of them.
		const float Maximum = AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		const float Gain = Maximum * MedicHealPercentOfMaximumPerPulse / 100.0f;
		if (Gain <= 0.0f)
		{
			// A CREATURE WITH NO MAXIMUM HEALTH IS NOT HEALED, rather than being
			// given nothing and counted as though it had been.
			continue;
		}

		// THROUGH TopUp RATHER THAN WRITING THE ATTRIBUTE, because that is what
		// honours the maximum, the healing-received stat and everything else
		// standing between a restoration and a health bar.
		//
		// NO Keyword.Regeneration TAG. That tag scopes a passive node to a
		// character's own regeneration, and this is one creature healing
		// another. Leech passes nothing for the same reason.
		UCataclysmRegeneration::TopUp(
			*AbilitySystem, UCataclysmVitalAttributeSet::GetHealthAttribute(),
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Gain);
		++Healed;
	}

	if (Healed > 0)
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("%s healed %d all%s for %.0f%% of maximum each."),
			*Medic->GetName(), Healed, Healed == 1 ? TEXT("y") : TEXT("ies"),
			MedicHealPercentOfMaximumPerPulse);
	}

	return Healed;
}

float UCataclysmEnemyModifiers::HellfireAuraBurnPerTick(float CarrierWeaponDamage)
{
	return FMath::Max(0.0f, CarrierWeaponDamage) * HellfireAuraBurnShareOfWeaponDamage;
}

bool UCataclysmEnemyModifiers::AuraPulseIsDue(float SecondsSinceLastPulse)
{
	// AT OR PAST THE INTERVAL, not strictly past it. The per-character step runs
	// at a fixed 0.25 seconds and the interval is a whole second, so the
	// accumulated figure lands exactly on 1.0 rather than near it, and a strict
	// comparison would make every pulse wait an extra step.
	return SecondsSinceLastPulse >= AuraPulseIntervalSeconds;
}

// ---------------------------------------------------------------------------
// The Death modifiers, written 2026-11-24 ahead of their window
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * Haunting's cut on one target: each resistance it holds taken down by `HauntingResistanceCut`, never below
	 * nought, for `HauntingSeconds`, under `Status.Debuff.Haunting`. 2026-11-24.
	 *
	 * BUILT AS STARVING AURA'S DEBUFF IS (`EnemyModifiersApplyStarvingAura` above), with the public calls
	 * `ApplyNamedEffectOnly` makes, because the status row names no stat to move (`MovesStat` is empty and the row is
	 * still "(planned)"), so the named-effect path would lay the tag and cut nothing. The workbook is not this
	 * change's to edit; the stats are named here.
	 *
	 * NEVER BELOW NOUGHT, the rule `ApplyNamedEffectOnly` states for every resistance a debuff cuts ("THE REDUCTION
	 * CANNOT TAKE A RESISTANCE PAST ZERO"), measured with the earlier Haunting taken off first, which is the lesson
	 * that function records: sized against a resistance an earlier cut had already lowered, a second cut raised it.
	 *
	 * EVERY RESISTANCE THE TARGET HOLDS: a player's eight, and the one a creature or a minion holds instead.
	 *
	 * THE TAG IS LAID EVEN WHEN NOTHING IS LEFT TO CUT, as the named-effect path lays a tag whose stats a target
	 * does not hold, so the status line still says the player is haunted.
	 */
	bool EnemyModifiersApplyHauntingCut(AActor* Haunter, AActor* Haunted)
	{
		UAbilitySystemComponent* Defender = UCataclysmTargeting::AbilitySystemOf(Haunted);
		const FGameplayTag HauntingTag = UCataclysmSkillShapes::StatusTagFor(TEXT("Haunting"));
		if (!Defender || !HauntingTag.IsValid() || UCataclysmDebuffs::RefusedByARow(Haunter, Haunted, HauntingTag))
		{
			return false;
		}
		const float HauntedSeconds = UCataclysmDebuffs::DurationOn(
			Defender, UCataclysmEnemyModifiers::HauntingSeconds, FGameplayTagContainer(HauntingTag));
		if (HauntedSeconds <= 0.0f)
		{
			return false;
		}
		UCataclysmDebuffs::NoteLaid(Haunter, Haunted, HauntingTag, /*bIsASpreadCopy=*/false);
		UCataclysmSkillEffects::RemoveEffectsGranting(Haunted, HauntingTag);

		UObject* HauntingOuter = GetTransientPackage();
		UGameplayEffect* Effect = NewObject<UGameplayEffect>(
			HauntingOuter, MakeUniqueObjectName(HauntingOuter, UGameplayEffect::StaticClass(),
												FName(TEXT("CataclysmStatus_Haunting"))));
		Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		Effect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(HauntedSeconds));

		const FGameplayAttribute HauntedResistances[] = {
			UCataclysmResistanceAttributeSet::GetWarResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetPestilenceResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetFamineResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetCelestialResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetChaosResistanceAttribute(),
			UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute(),
			UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(),
		};
		for (const FGameplayAttribute& Resistance : HauntedResistances)
		{
			if (!Defender->HasAttributeSetForAttribute(Resistance))
			{
				continue;
			}
			const float Taken = FMath::Min(UCataclysmEnemyModifiers::HauntingResistanceCut,
										   FMath::Max(0.0f, Defender->GetNumericAttribute(Resistance)));
			if (Taken <= 0.0f)
			{
				continue;
			}
			FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
			Modifier.Attribute = Resistance;
			Modifier.ModifierOp = EGameplayModOp::Additive;
			Modifier.ModifierMagnitude = FScalableFloat(-Taken);
		}

		UTargetTagsGameplayEffectComponent& TagsComponent =
			Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
		FInheritedTagContainer Granted;
		Granted.Added.AddTag(HauntingTag);
		TagsComponent.SetAndApplyTargetTagChanges(Granted);

		UAbilitySystemComponent* Source = UCataclysmTargeting::AbilitySystemOf(Haunter);
		FGameplayEffectContextHandle Context = (Source ? Source : Defender)->MakeEffectContext();
		Context.AddInstigator(Haunter, Haunter);
		const FGameplayEffectSpec Spec(Effect, Context, /*Level=*/1.0f);
		Defender->ApplyGameplayEffectSpecToSelf(Spec);
		return true;
	}

	/** Shows a creature Shadow Step hid. The one place that undoes the hide, so the mark and the drawing agree. */
	void EnemyModifiersShowWhatShadowStepHid(ACataclysmEnemyCharacter* Hidden)
	{
		if (Hidden && Hidden->bHiddenByShadowStep)
		{
			Hidden->SetActorHiddenInGame(false);
			Hidden->bHiddenByShadowStep = false;
		}
	}

	/** How far from whoever struck, towards the creature, Funeral Pyre places its pyre: a metre, so the two bodies do
	 *  not overlap, and never past the creature itself. A judgement; the row says only "summon". */
	constexpr float FuneralPyreBesideStrikerCm = 100.0f;
}

float UCataclysmEnemyModifiers::GraveTouchMultiplier(const TArray<FName>& Rows, float TargetHealth,
													 float TargetMaximumHealth)
{
	if (!Carries(Rows, GraveTouchRow) || TargetMaximumHealth <= 0.0f)
	{
		return 1.0f;
	}

	// "LESS THAN 50%", STRICTLY: a target holding exactly half is not below it.
	return TargetHealth < TargetMaximumHealth * GraveTouchBelowHealthShare ? GraveTouchDamageMultiplier : 1.0f;
}

bool UCataclysmEnemyModifiers::IgnoresEnergyShield(const TArray<FName>& Rows)
{
	return Carries(Rows, SoulEaterRow);
}

bool UCataclysmEnemyModifiers::MortalReminderOnHit(AActor* Attacker, AActor* Target)
{
	// ANY WEARER, since issue #2899: a player carrying it holds a creature it hits to the health the hit left, as a
	// creature carrying it holds the player. The ceiling is on the struck character's ability system, which every heal of
	// health asks (`UCataclysmRegeneration::HealthHealingCeiling`), a creature's as a player's.
	// NEVER ON ITSELF: a player's own blow on itself (Final Curse's self hit) caps nothing. Compared by ability system,
	// because a player's struck set names its player state and its blow its pawn.
	if (Attacker == nullptr || !CarriedBy(Attacker, MortalReminderRow)
		|| UCataclysmTargeting::AbilitySystemOf(Attacker) == UCataclysmTargeting::AbilitySystemOf(Target))
	{
		return false;
	}

	// NOR A TARGET ALREADY DEAD, as Infernal Brand refuses one (#2874): the blow that kills reaches this after the death.
	if (UCataclysmSkillEffects::IsDead(Target))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* Reminded =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Target));
	const UWorld* World = Target ? Target->GetWorld() : nullptr;
	if (Reminded == nullptr || World == nullptr)
	{
		return false;
	}

	// THE HEALTH THE HIT LEFT, which is "their current HP" once the attack has landed: this is called after the blow's
	// health is written. A LATER HIT SETS IT AGAIN, lower, and restarts the four seconds; a heal under the ceiling
	// cannot have raised it in between, so the newest figure is always the lowest.
	Reminded->MortalReminderCeiling =
		Reminded->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	Reminded->MortalReminderEndsAtSeconds = World->GetTimeSeconds() + MortalReminderSeconds;

	// AND ITS STATUS, SO THE PLAYER'S EFFECTS LIST NAMES IT: `Status.Debuff.MortalReminder`, from the Debuffs sheet's
	// row added 2026-11-25, which the owner accepted on 2026-11-24 ("These are fine"). For the same four seconds, and
	// refreshed by a later hit as the ceiling is. THE CEILING STAYS ON THE ABILITY SYSTEM: a tag carries no figure, and
	// the ceiling is the health the hit left.
	const FGameplayTag Reminder = UCataclysmSkillShapes::StatusTagFor(TEXT("Mortal Reminder"));
	if (Reminder.IsValid())
	{
		UCataclysmSkillEffects::ApplyTagForDuration(Attacker, Target, Reminder, MortalReminderSeconds);
	}

	UE_LOG(LogCataclysm, Log, TEXT("%s's Mortal Reminder holds %s's healing to %.0f health for %.0f seconds."),
		   *Attacker->GetName(), *GetNameSafe(Target), Reminded->MortalReminderCeiling, MortalReminderSeconds);
	return true;
}

float UCataclysmEnemyModifiers::RecoveryCeilingOn(const UAbilitySystemComponent& AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Reminded = Cast<const UCataclysmAbilitySystemComponent>(&AbilitySystem);
	const UWorld* World = AbilitySystem.GetWorld();
	if (Reminded == nullptr || World == nullptr || Reminded->MortalReminderCeiling < 0.0f
		|| World->GetTimeSeconds() >= Reminded->MortalReminderEndsAtSeconds)
	{
		return -1.0f;
	}
	return Reminded->MortalReminderCeiling;
}

float UCataclysmEnemyModifiers::TetherOnHealthLost(AActor* Tethered, AActor* Striker, float HealthLost)
{
	// ANY WEARER, since issue #2899: `Tethered` is the body struck, a creature or a player, and a player carrying it sends
	// a tenth of the health a creature's blow took back to that creature.
	ACataclysmCharacterBase* Wearer = Cast<ACataclysmCharacterBase>(Tethered);
	if (Wearer == nullptr || !CarriedBy(Wearer, SoulTetherRow) || Wearer->TakesNoHostileAction()
		|| HealthLost <= 0.0f || Striker == nullptr || Striker == Tethered
		|| UCataclysmSkillEffects::IsDead(Striker) || !UCataclysmTargeting::IsHostileTo(Striker, Wearer))
	{
		return 0.0f;
	}

	const FGameplayTag Tether = UCataclysmSkillShapes::StatusTagFor(TEXT("Soul Tether"));
	if (!Tether.IsValid())
	{
		return 0.0f;
	}

	const float Laid = HealthLost * SoulTetherShareOfHealthLost;

	// ADDED TO WHAT AN EARLIER TETHER STILL HAS TO DEAL, AND THE WHOLE SPREAD OVER A FRESH FOUR SECONDS. The row
	// transfers "a portion of damage taken", so every blow's portion arrives; the ordinary rule for a damage over
	// time, that a weaker application only refreshes a stronger one (#1503), would throw the later portions away.
	float StillToCome = 0.0f;
	UCataclysmSkillEffects::FRunningAilment Running;
	if (UCataclysmSkillEffects::RunningAilmentOn(Striker, Tether, Running))
	{
		StillToCome = FMath::Max(0.0f, Running.DamagePerSecond * Running.SecondsLeft);
		UCataclysmSkillEffects::RemoveEffectsGranting(Striker, Tether);
	}

	// ONE TICK A SECOND, the base rate, and NOT SCALED BY THE CREATURE'S OWN DAMAGE OVER TIME STATS: the figure is
	// already the health it lost. Of the creature's own damage type, met by the striker's defences as a tick is, and
	// a tick reaches the energy shield (#2014).
	const float PerTick = (StillToCome + Laid) * UCataclysmSkillEffects::BaseSecondsPerTick / SoulTetherSeconds;
	if (!UCataclysmSkillEffects::ApplyDamageOverTime(Wearer, Striker, PerTick, SoulTetherSeconds, Tether,
													 /*bScalesWithInstigator=*/false))
	{
		return 0.0f;
	}

	UE_LOG(LogCataclysm, Log, TEXT("%s's Soul Tether sends %.1f of the %.1f it lost back to %s over %.0f seconds."),
		   *Wearer->GetName(), Laid, HealthLost, *GetNameSafe(Striker), SoulTetherSeconds);
	return Laid;
}

bool UCataclysmEnemyModifiers::IsPhasedOut(const AActor* Character)
{
	const ACataclysmCharacterBase* Wearer = Cast<ACataclysmCharacterBase>(Character);
	return Wearer != nullptr && Wearer->SpectralShiftSecondsLeft > 0.0f;
}

void UCataclysmEnemyModifiers::RevealForShadowStep(AActor* Character)
{
	ACataclysmCharacterBase* Wearer = Cast<ACataclysmCharacterBase>(Character);
	if (Wearer == nullptr || !CarriedBy(Wearer, ShadowStepRow))
	{
		return;
	}

	// SEEN NOW AND FOR THE SECOND AFTER, which the timed step then runs down once no attack is under way. A creature is
	// drawn again; a player was never hidden from its own screen.
	Wearer->ShadowStepSeenSecondsLeft = ShadowStepSeenSecondsAfterAnAttack;
	EnemyModifiersShowWhatShadowStepHid(Cast<ACataclysmEnemyCharacter>(Wearer));
}

bool UCataclysmEnemyModifiers::IsHiddenByShadowStep(const AActor* Character)
{
	if (const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character))
	{
		return Enemy->bHiddenByShadowStep && Enemy->IsHidden();
	}
	// ANY OTHER WEARER, a player: hidden from creatures' sight while it carries the row, takes hostile action and has no
	// seconds left seen. Never a dead one. Issue #2899.
	const ACataclysmCharacterBase* Wearer = Cast<ACataclysmCharacterBase>(Character);
	return Wearer != nullptr && Wearer->ShadowStepSeenSecondsLeft <= 0.0f && !Wearer->TakesNoHostileAction()
		&& !UCataclysmSkillEffects::IsDead(Wearer) && CarriedBy(Wearer, ShadowStepRow);
}

bool UCataclysmEnemyModifiers::NoteDeathOfACarrier(AActor* Dying)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Dying);
	if (Enemy == nullptr)
	{
		return false;
	}

	// A CREATURE SHADOW STEP HID IS SEEN TO FALL, and one out of existence is back in it: its timed step never runs
	// again, so nothing else would undo either.
	EnemyModifiersShowWhatShadowStepHid(Enemy);
	Enemy->SpectralShiftSecondsLeft = 0.0f;

	// AND A VOLLEY OF WAR'S ARTILLERY BARRAGE STILL IN THE AIR IS CALLED OFF, its circles taken away: its timed step
	// never runs again, so the shells would never land and the circles would stand for nothing. Killing the caller in
	// the warning spares the player, which is an answer the player can choose. 2026-11-25, a judgement, recorded.
	if (Enemy->ArtilleryShellsSecondsLeft > 0.0f || !Enemy->ArtilleryShellMarkers.IsEmpty())
	{
		EnemyModifiersCallOffArtilleryBarrage(Enemy);
	}

	// -- Final Curse ------------------------------------------------------
	// "UPON DEATH, CURSE THE PLAYER", whoever or whatever killed it and wherever the player stands: the row names no
	// killer and no reach. The player is found as the death's other readers find them, through the first player
	// controller. NOT A CREATURE THAT TAKES NO HOSTILE ACTION, the medic's rule (#1680).
	//
	// ANY CREATURE THAT WEARS IT, since issue #2899, read through `CarriedBy`; A PLAYER NEVER DOES: the owner ruled on
	// 2026-11-26 that Final Curse and Haunting are never lent to the player, and a player's death ends the walk. AND A
	// WEARER ON THE PLAYER'S OWN SIDE -- a thrall -- CURSES NOBODY: the player is its ally, and the row's curse is on the
	// wearer's enemy. A judgement, recorded.
	if (!CarriedBy(Enemy, FinalCurseRow) || Enemy->TakesNoHostileAction())
	{
		return false;
	}
	UWorld* World = Enemy->GetWorld();
	APlayerController* Watching = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Cursed = Watching ? Watching->GetPawn() : nullptr;
	if (Cursed == nullptr || UCataclysmSkillEffects::IsDead(Cursed) || !UCataclysmTargeting::IsHostileTo(Cursed, Enemy))
	{
		return false;
	}
	const FGameplayTag Curse = UCataclysmSkillShapes::StatusTagFor(TEXT("Final Curse"));
	if (!Curse.IsValid() || !UCataclysmSkillEffects::ApplyTagForDuration(Enemy, Cursed, Curse, FinalCurseSeconds))
	{
		return false;
	}

	UE_LOG(LogCataclysm, Log, TEXT("%s died carrying Final Curse and cursed %s for %.0f seconds."),
		   *Enemy->GetName(), *Cursed->GetName(), FinalCurseSeconds);
	return true;
}

float UCataclysmEnemyModifiers::FinalCurseOnSkillUse(AActor* User, bool bWasTheBasicAttack)
{
	// "WHEN USING ABILITIES": every skill a player chooses to use, and not the basic attack, which the game uses for
	// them. A judgement, listed for the owner.
	if (bWasTheBasicAttack || User == nullptr || UCataclysmSkillEffects::IsDead(User))
	{
		return 0.0f;
	}

	const FGameplayTag Curse = UCataclysmSkillShapes::StatusTagFor(TEXT("Final Curse"));
	if (!Curse.IsValid() || !UCataclysmSkillEffects::HasTag(User, Curse))
	{
		return 0.0f;
	}

	const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(User);
	const float Damage = System
		? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
			* FinalCurseMaximumHealthPercentPerUse / 100.0f
		: 0.0f;
	if (Damage <= 0.0f)
	{
		return 0.0f;
	}

	// DEALT AS ECHO CHAMBER'S SELF HIT IS: the user's own blow on itself, an area so it is never evaded, no critical
	// strike, no leech, nothing struck back. OF DEATH, the curse's Cataclysm, so the user's Death resistance meets it.
	FCataclysmHitDelivery Delivery;
	Delivery.bIsArea = true;
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCannotLeech = true;
	Delivery.bCannotBeRetaliatedAgainst = true;
	// AND NO WEAPON SUB-TYPE, issue #2931: the curse is no swing of the user's weapon. Until 2026-11-26 the blow carried
	// the sub-type of the weapon the user held, so a player with a slashing weapon paid 10% more to health (the
	// sub-type's bonus in `UCataclysmDamageCalculation`): window b4-w3 read 55.00 where the curse's 5% of 1000 is 50.
	// Retaliation and the overkill explosion leave it off for the same reason.
	Delivery.bCarriesNoWeaponSubType = true;
	Delivery.DamageType = FName(TEXT("Death"));
	if (!UCataclysmSkillEffects::ApplyDirectDamage(User, User, Damage, Delivery))
	{
		return 0.0f;
	}

	UE_LOG(LogCataclysm, Log, TEXT("%s used a skill under Final Curse and paid %.0f."), *User->GetName(), Damage);
	return Damage;
}

ACataclysmBonePyreCharacter* UCataclysmEnemyModifiers::PyreWhenStruck(AActor* Struck, AActor* Striker)
{
	// ANY WEARER, since issue #2899: `Struck` is the body struck, a creature or a player. A player's pyre stands on the
	// player's side and explodes on creatures.
	ACataclysmCharacterBase* Enemy = Cast<ACataclysmCharacterBase>(Struck);
	if (Enemy == nullptr || !CarriedBy(Enemy, FuneralPyreRow) || Enemy->TakesNoHostileAction()
		|| UCataclysmSkillEffects::IsDead(Enemy) || Striker == nullptr || Striker == Struck
		|| !UCataclysmTargeting::IsHostileTo(Striker, Enemy))
	{
		return nullptr;
	}

	UWorld* World = Enemy->GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// AT MOST ONE EVERY EIGHT SECONDS. Without it every blow would raise a pyre, and a player attacking at any speed
	// would stand in a field of them: the reason the owner gave Beguiling, the other modifier set off by being hit, a
	// cooldown of its own on 2026-09-05.
	const float Now = World->GetTimeSeconds();
	if (Enemy->FuneralPyreLastAt >= 0.0f && Now - Enemy->FuneralPyreLastAt < FuneralPyreCooldownSeconds)
	{
		return nullptr;
	}

	// AT THE FEET OF WHOEVER STRUCK, a metre towards the creature so the two bodies do not start inside each other;
	// the spawn moves it clear of anything else. Under the striker, as Path of Exile's Crystal-skinned fires its
	// crystals "under players when they hit you", so a player who attacks from range is answered too.
	FVector Towards = Enemy->GetActorLocation() - Striker->GetActorLocation();
	Towards.Z = 0.0f;
	const FVector Where = Striker->GetActorLocation() + Towards.GetClampedToMaxSize(FuneralPyreBesideStrikerCm);

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ACataclysmBonePyreCharacter* Pyre = World->SpawnActor<ACataclysmBonePyreCharacter>(
		ACataclysmBonePyreCharacter::StaticClass(), Where, FRotator::ZeroRotator, Spawn);
	if (Pyre == nullptr)
	{
		return nullptr;
	}

	// RAISED BY A RULE AND DYING UNPAID, as every floor source a rule places is: a save does not keep it, and breaking
	// it drops nothing and grants nothing. On the wearer's side, and of a creature wearer's damage type, so the
	// explosion finds the wearer's enemies and is met by their resistance to it. A PLAYER'S PYRE KEEPS THE PYRE'S OWN
	// TYPE: a player's damage is untyped (`UCataclysmSkillEffects::DamageTypeOf`), and a creature meets every type with
	// its one resistance, so which type it is changes nothing it takes.
	Pyre->bRaisedByARule = true;
	Pyre->bDiesUnpaid = true;
	if (const ACataclysmEnemyCharacter* CreatureWearer = Cast<ACataclysmEnemyCharacter>(Enemy))
	{
		Pyre->DamageType = CreatureWearer->DamageType;
	}
	Pyre->SetGenericTeamId(UCataclysmTeams::TeamOf(Enemy));

	const UAbilitySystemComponent* Own = UCataclysmTargeting::AbilitySystemOf(Enemy);
	const float MaximumHealth =
		Own ? Own->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : 0.0f;
	const float AttackDamage =
		Own ? Own->GetNumericAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute()) : 0.0f;
	Pyre->SetHealth(FMath::Max(1.0f, MaximumHealth * FuneralPyreShareOfHealth));
	Pyre->PyreFuseSecondsLeft = FuneralPyreFuseSeconds;
	Pyre->PyreExplosionDamage = FMath::Max(0.0f, AttackDamage) * FuneralPyreExplosionHits;
	Pyre->PyreKindledBy = Enemy;
	Enemy->FuneralPyreLastAt = Now;

	UE_LOG(LogCataclysm, Log, TEXT("%s was struck by %s and raised a bone pyre that explodes in %.0f seconds for %.0f."),
		   *Enemy->GetName(), *Striker->GetName(), FuneralPyreFuseSeconds, Pyre->PyreExplosionDamage);
	return Pyre;
}

int32 UCataclysmEnemyModifiers::PyreStep(ACataclysmBonePyreCharacter* Pyre, float StepSeconds)
{
	// A PYRE DESTROYED FIRST NEVER EXPLODES: a dead pyre takes no step, and its death is what destroying it means.
	if (Pyre == nullptr || Pyre->bPyreSpent || UCataclysmSkillEffects::IsDead(Pyre))
	{
		return 0;
	}

	Pyre->PyreFuseSecondsLeft -= StepSeconds;
	if (Pyre->PyreFuseSecondsLeft > KINDA_SMALL_NUMBER)
	{
		return 0;
	}

	// SPENT BEFORE THE BLAST, so nothing the blast sets off can explode it twice.
	Pyre->bPyreSpent = true;
	Pyre->PyreFuseSecondsLeft = 0.0f;

	int32 Struck = 0;
	UWorld* World = Pyre->GetWorld();
	if (World != nullptr && Pyre->PyreExplosionDamage > 0.0f)
	{
		// EVERY ENEMY OF THE PYRE'S SIDE WITHIN THREE METRES, the player and the player's minions, through the search
		// every skill uses. AN AREA BLOW, which cannot be evaded and can be blocked, and never a critical strike: the
		// figure is three hits banked, as Infernal Brand's explosion is five and does not crit (#2874).
		const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
			World, Pyre, Pyre->GetActorLocation(), FuneralPyreRadiusCm);
		FCataclysmHitDelivery Blast;
		Blast.bIsArea = true;
		Blast.bCannotCriticallyStrike = true;
		for (AActor* Target : Caught)
		{
			if (UCataclysmSkillEffects::ApplyDirectDamage(Pyre, Target, Pyre->PyreExplosionDamage, Blast))
			{
				++Struck;
			}
		}
	}

	UE_LOG(LogCataclysm, Log, TEXT("A bone pyre exploded for %.0f and caught %d."), Pyre->PyreExplosionDamage, Struck);

	// GONE AT ONCE, AND REMOVED RATHER THAN KILLED: it was not destroyed by anyone, so no death is announced, nothing
	// is credited and nothing is dropped. Hidden and its collision off now; the actor goes on the next tick, as a
	// creature with no death clip does, because this runs inside the pyre's own step.
	Pyre->SetActorHiddenInGame(true);
	Pyre->SetActorEnableCollision(false);
	World = Pyre->GetWorld();
	if (World != nullptr)
	{
		FTimerDelegate Removal = FTimerDelegate::CreateWeakLambda(Pyre, [Pyre]()
		{
			Pyre->Destroy();
		});
		World->GetTimerManager().SetTimerForNextTick(Removal);
	}
	return Struck;
}

bool UCataclysmEnemyModifiers::HauntOnHit(AActor* Attacker, AActor* Target)
{
	const ACataclysmEnemyCharacter* Copy = Cast<ACataclysmEnemyCharacter>(Attacker);
	if (Copy == nullptr || !Copy->bIsASpectralCopy || UCataclysmSkillEffects::IsDead(Target))
	{
		return false;
	}

	if (!EnemyModifiersApplyHauntingCut(Attacker, Target))
	{
		return false;
	}

	UE_LOG(LogCataclysm, Log, TEXT("%s, a spectral copy, haunted %s: every resistance %.0f lower for %.0f seconds."),
		   *Copy->GetName(), *GetNameSafe(Target), HauntingResistanceCut, HauntingSeconds);
	return true;
}

bool UCataclysmEnemyModifiers::UndyingWillRaises(const AActor* Fallen)
{
	// ONLY A DEATH THAT PAYS, so a creature a rule brought back -- Undying Will's own among them -- never rises again,
	// and one resurrection is the most the row gives: the guard Dead Rising takes for its own revivals. A floor source
	// is never one, whatever it carries.
	// ANY CREATURE THAT WEARS IT, through `CarriedBy` (issue #2899); a wearer that is not a creature rises through
	// `RiseThroughUndyingWill` instead, because only a creature is brought back as a new one.
	const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Fallen);
	return Enemy != nullptr && CarriedBy(Enemy, UndyingWillRow) && Enemy->PaysForItsDeath()
		&& !Enemy->IsA<ACataclysmFloorSourceCharacter>();
}

bool UCataclysmEnemyModifiers::HauntingLeavesACopy(const AActor* Fallen)
{
	// THE SAME GUARD, so a copy never leaves a copy and a creature brought back leaves none. ANY CREATURE THAT WEARS IT
	// (issue #2899); never a player, which the owner ruled is never lent it (2026-11-26) and whose death ends the walk.
	const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Fallen);
	return Enemy != nullptr && CarriedBy(Enemy, HauntingRow) && Enemy->PaysForItsDeath()
		&& !Enemy->IsA<ACataclysmFloorSourceCharacter>();
}

bool UCataclysmEnemyModifiers::RiseThroughUndyingWill(AActor* Wearer)
{
	ACataclysmCharacterBase* Rising = Cast<ACataclysmCharacterBase>(Wearer);
	if (Rising == nullptr || Rising->IsA<ACataclysmEnemyCharacter>() || Rising->bUndyingWillSpent
		|| !CarriedBy(Rising, UndyingWillRow))
	{
		return false;
	}
	Rising->bUndyingWillSpent = true;
	Rising->bRoseThroughUndyingWill = true;
	UE_LOG(LogCataclysm, Log, TEXT("%s rose through Undying Will at %.0f%% of its health, its blows %.0f%% stronger."),
		   *Rising->GetName(), UndyingWillHealthShare * 100.0f, UndyingWillDamageIncreasedPercent);
	return true;
}

float UCataclysmEnemyModifiers::UndyingWillDamageMultiplierFor(const AActor* Striker)
{
	const ACataclysmCharacterBase* Risen = Cast<ACataclysmCharacterBase>(Striker);
	return Risen != nullptr && Risen->bRoseThroughUndyingWill && !Risen->IsA<ACataclysmEnemyCharacter>()
		&& CarriedBy(Risen, UndyingWillRow)
		? 1.0f + UndyingWillDamageIncreasedPercent / 100.0f
		: 1.0f;
}

void UCataclysmEnemyModifiers::MakeUndyingWillRisen(ACataclysmEnemyCharacter* Risen)
{
	if (Risen == nullptr)
	{
		return;
	}

	// RISEN FROM THE DEAD, SO ITS SECOND DEATH PAYS NOTHING: the owner's decision of 2026-09-17, one kill pays once.
	Risen->bRisenFromTheDead = true;
	Risen->bRisenByUndyingWill = true;

	// "INCREASED DAMAGE OUTPUT", IN THE INCREASED BUCKET, as a rule's part so every later write of its damage keeps it
	// (#2723). Written before the health, which a part does not touch.
	ACataclysmEnemyCharacter::FRuleDamagePart Part;
	Part.IncreasedPercent = UndyingWillDamageIncreasedPercent;
	Risen->SetRuleDamagePart(ACataclysmEnemyCharacter::UndyingWillDamageSource, Part);

	// "REDUCED HEALTH": half its maximum, written after the rung, because setting a rung refills health, as Divine
	// Resurgence writes its half.
	if (UAbilitySystemComponent* System = Risen->GetAbilitySystemComponent())
	{
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) * UndyingWillHealthShare);
	}
}

void UCataclysmEnemyModifiers::MakeSpectralCopy(ACataclysmEnemyCharacter* Copy)
{
	if (Copy == nullptr)
	{
		return;
	}

	// A COPY PAYS NOTHING, as a creature a rule brought back pays nothing (2026-09-17): the creature it copies paid.
	Copy->bRisenFromTheDead = true;
	Copy->bIsASpectralCopy = true;
	if (UAbilitySystemComponent* System = Copy->GetAbilitySystemComponent())
	{
		System->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) * HauntingCopyHealthShare);
	}
}

float UCataclysmEnemyModifiers::LingeringDecayHealingLessPercentAt(const AActor* Healed)
{
	// "PLAYERS STANDING IN THEM": a zone a creature left reaches the players' side, which is the player and the player's
	// minions, as Starving Aura's cut reaches both (ruled 2026-10-26), and a creature standing in one is healed in full.
	// AND A ZONE A PLAYER LEFT REACHES THE CREATURES' SIDE INSTEAD, since issue #2899 (2026-11-26): every enemy modifier
	// works on any wearer, so it cuts the healing of the wearer's enemies. Each zone names the side it reaches.
	if (!IsValid(Healed) || UCataclysmTeams::TeamOf(Healed) == FGenericTeamId::NoTeam)
	{
		return 0.0f;
	}

	UWorld* World = Healed->GetWorld();
	if (World == nullptr)
	{
		return 0.0f;
	}
	const uint8 HealedSide = UCataclysmTeams::TeamOf(Healed).GetId();

	// THE LARGEST, NEVER A SUM, as `ACataclysmGroundZone::RegenerationScaleFor` reads its patches: two overlapping
	// zones are one zone to whoever stands in both. THE FIGURE BEFORE THE GEOMETRY, so every other patch in the level
	// costs one comparison.
	float Most = 0.0f;
	for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
	{
		const ACataclysmGroundZone* Zone = *It;
		if (!IsValid(Zone) || Zone->HealingLessPercentInside <= Most || Zone->HealingLessReachesTeamId != HealedSide)
		{
			continue;
		}
		if (Zone->Covers(Healed->GetActorLocation()))
		{
			Most = Zone->HealingLessPercentInside;
		}
	}
	return FMath::Clamp(Most, 0.0f, 100.0f);
}

// ---------------------------------------------------------------------------
// The War modifiers, written 2026-11-25 ahead of their window
// ---------------------------------------------------------------------------

float UCataclysmEnemyModifiers::ArmourMultiplier(const TArray<FName>& Rows)
{
	// A RUNNING MULTIPLIER, AS `MaxHealthMultiplier` IS, so a second modifier that also scales armour multiplies with
	// this rather than replacing it.
	float Multiplier = 1.0f;
	if (Carries(Rows, ArmoredJuggernautRow))
	{
		Multiplier *= ArmoredJuggernautArmourMultiplier;
	}
	return Multiplier;
}

float UCataclysmEnemyModifiers::CritChanceMultiplier(const TArray<FName>& Rows)
{
	// "INCREASED", ONE PLUS THE SUM OF THE INCREASES, the bucket the word names.
	float IncreasedPercent = 0.0f;
	if (Carries(Rows, BattleScarredRow))
	{
		IncreasedPercent += BattleScarredCritChanceIncreasedPercent;
	}
	return 1.0f + IncreasedPercent / 100.0f;
}

float UCataclysmEnemyModifiers::AddedResistance(const TArray<FName>& Rows)
{
	// A SUM, so a second modifier that also adds resistance adds rather than replacing.
	float Added = 0.0f;
	if (Carries(Rows, BattleScarredRow))
	{
		Added += BattleScarredResistance;
	}
	return Added;
}

float UCataclysmEnemyModifiers::ShareOfProjectileDamageTaken(const AActor* Target)
{
	const ACataclysmEnemyCharacter* Walled = Cast<ACataclysmEnemyCharacter>(Target);
	return Walled != nullptr && Carries(Walled->ModifierRows, ShieldWallRow) ? ShieldWallProjectileDamageShare : 1.0f;
}

float UCataclysmEnemyModifiers::PhalanxDamageReductionFor(int32 Others)
{
	// NOT CAPPED HERE: the damage calculation caps every source of damage reduction together, at 75, and a second cap
	// here could only disagree with it.
	return FMath::Max(0, Others) * PhalanxDamageReductionPerAlly;
}

int32 UCataclysmEnemyModifiers::CountPhalanxAllies(const AActor* Character)
{
	const ACataclysmEnemyCharacter* Braced = Cast<ACataclysmEnemyCharacter>(Character);
	const UWorld* BracedWorld = Braced ? Braced->GetWorld() : nullptr;
	if (BracedWorld == nullptr)
	{
		return 0;
	}

	// "EVERY OTHER ENEMY": the creatures of its side beside it, which `FindAlliesInSphere` finds without the creature
	// itself and without the dead. A CREATURE THAT CAN LOSE HEALTH, the reading of "ally" ruled for Sacrificial Bond on
	// 2026-09-30 (#1559): a floor-rule object -- a spire, a pyre, a beacon -- is not one, nor is a creature that cannot
	// be hurt. A shrouded creature still stands in the line and counts.
	int32 Others = 0;
	for (const AActor* Ally : UCataclysmTargeting::FindAlliesInSphere(
			 BracedWorld, Braced, Braced->GetActorLocation(), PhalanxReachCm))
	{
		const ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Ally);
		if (Creature != nullptr && !Creature->IsA<ACataclysmFloorSourceCharacter>() && !Creature->bCannotBeHurt)
		{
			++Others;
		}
	}
	return Others;
}

bool UCataclysmEnemyModifiers::IsArtilleryBarrageInTheAir(const AActor* Character)
{
	const ACataclysmEnemyCharacter* Caller = Cast<ACataclysmEnemyCharacter>(Character);
	return Caller != nullptr && Caller->ArtilleryShellsSecondsLeft > 0.0f;
}
