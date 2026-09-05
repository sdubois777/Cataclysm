// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyModifiers.h"

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "EngineUtils.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

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

float UCataclysmEnemyModifiers::RetaliationPercent(const TArray<FName>& Rows)
{
	float Percent = 0.0f;

	if (Carries(Rows, ThornsOfGlassRow))
	{
		Percent += ThornsOfGlassRetaliationPercent;
	}

	return Percent;
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
	FRandomStream Stream(Enemy->GetUniqueID()
		^ static_cast<int32>(Now * 1000.0f) ^ PhaseDrawSalt);

	const float Angle = Stream.FRandRange(0.0f, 360.0f);
	const FVector Step(FMath::Cos(FMath::DegreesToRadians(Angle)),
					   FMath::Sin(FMath::DegreesToRadians(Angle)), 0.0f);

	// SWEPT, so it cannot phase into a wall. A blocked teleport moves it as far
	// as it can go, which reads as the creature reappearing against the wall
	// rather than inside it.
	Enemy->AddActorWorldOffset(Step * PhasewalkerDistanceCm, /*bSweep=*/true);

	return true;
}

int32 UCataclysmEnemyModifiers::AuraStep(AActor* Character, float StepSeconds)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
	if (Enemy == nullptr || Enemy->ModifierRows.IsEmpty())
	{
		// THE PLAYER COMES THROUGH HERE EVERY STEP, because the step this hangs
		// off is on the shared character base. So does every Common creature.
		return 0;
	}

	const bool bBurns = Carries(Enemy->ModifierRows, HellfireAuraRow);
	const bool bStripsResistance =
		Carries(Enemy->ModifierRows, AbyssalAuraRow);
	if (!bBurns && !bStripsResistance)
	{
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

	int32 Touched = 0;
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
		if (bBurns
			&& UCataclysmSkillEffects::ApplyBurn(Enemy, Target, /*HitDamage=*/0.0f,
												 /*bScalesWithInstigator=*/true,
												 /*bBurnIsDesigned=*/true))
		{
			bAnythingLanded = true;
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
	const ACataclysmEnemyCharacter* Enemy =
		Cast<ACataclysmEnemyCharacter>(Attacker);
	if (Enemy == nullptr || !Carries(Enemy->ModifierRows, InfernalBrandRow))
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

	// AN ORDINARY DIRECT BLOW. The damage TYPE is not a parameter here: it is
	// read off the instigator when the hit resolves, which is what makes the
	// explosion count as this creature's own element without saying so twice.
	UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Target, Damage,
											  FCataclysmHitDelivery());

	UE_LOG(LogCataclysm, Log,
		   TEXT("%s's Infernal Brand reached five stacks on %s and dealt %.0f."),
		   *Enemy->GetName(), *GetNameSafe(Target), Damage);

	return Damage;
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

float UCataclysmEnemyModifiers::ShareOfDamageKept(AActor* Character)
{
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

	const TArray<AActor*> Allies = UCataclysmTargeting::FindAlliesInSphere(
		World, Enemy, Enemy->GetActorLocation(), SacrificialBondReach);

	// NOBODY TO SHARE WITH MEANS IT KEEPS ALL OF IT, which is what makes the
	// modifier answerable: pull the creature away from its pack, or kill the
	// pack first.
	if (Allies.IsEmpty())
	{
		return 1.0f;
	}

	// DIVIDED EVENLY BETWEEN THE CREATURE AND ITS ALLIES, reading the row as
	// written: one ally halves what it takes, three leave it a quarter.
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

int32 UCataclysmEnemyModifiers::TimedStep(AActor* Character, float StepSeconds)
{
	ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
	if (Enemy == nullptr || Enemy->ModifierRows.IsEmpty())
	{
		return 0;
	}

	if (UCataclysmSkillEffects::IsDead(Enemy))
	{
		return 0;
	}

	UWorld* World = Enemy->GetWorld();
	if (World == nullptr)
	{
		return 0;
	}

	int32 Acted = 0;

	// A SIGIL THAT IS UP RUNS DOWN, whether or not this creature carries the
	// modifier any more. Counted first so a sigil placed this step is not
	// expired by the same step.
	if (Enemy->SigilSecondsLeft > 0.0f)
	{
		Enemy->SigilSecondsLeft =
			FMath::Max(0.0f, Enemy->SigilSecondsLeft - StepSeconds);
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
	if (Carries(Enemy->ModifierRows, InfernalSacrificeRow))
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
				if (Victim == nullptr
					|| UCataclysmSkillEffects::IsDead(Victim))
				{
					continue;
				}

				Victim->HandleDeath();

				UAbilitySystemComponent* Own =
					UCataclysmTargeting::AbilitySystemOf(Enemy);
				if (Own)
				{
					const float Max = Own->GetNumericAttribute(
						UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
					const float Now = Own->GetNumericAttribute(
						UCataclysmVitalAttributeSet::GetHealthAttribute());

					Own->SetNumericAttributeBase(
						UCataclysmVitalAttributeSet::GetHealthAttribute(),
						FMath::Min(Max,
								   Now + Max * SacrificeHealShareOfHealth));
				}

				// AND THE DAMAGE BUFF THE ROW PROMISES. Commander is the buff
				// this game has for "nearby allies hit harder and move faster";
				// granting it to the creature itself is the closest thing to a
				// temporary damage buff without inventing a second one.
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
	if (Carries(Enemy->ModifierRows, InfernoChargeRow) && !Enemy->IsCharging())
	{
		Enemy->SecondsSinceInfernoCharge += StepSeconds;
		if (Enemy->SecondsSinceInfernoCharge >= InfernoChargeIntervalSeconds)
		{
			const TArray<AActor*> Ahead =
				UCataclysmTargeting::FindEnemiesInSphere(
					World, Enemy, Enemy->GetActorLocation(),
					/*RadiusCm=*/2000.0f, /*MaxTargets=*/1);

			// NOTHING TO CHARGE MEANS THE TIMER KEEPS RUNNING, so the creature
			// charges as soon as somebody comes into range rather than waiting
			// another twelve seconds after they do.
			if (!Ahead.IsEmpty() && Ahead[0])
			{
				Enemy->SecondsSinceInfernoCharge = 0.0f;

				// THE CHARGE EVERY CREATURE ALREADY HAS. `BeginCharge` is on the
				// base class and the Hellhound's own charge goes through it, so
				// the lane, the damage, the shove and the telegraph are one
				// implementation a player has already learned to read.
				Enemy->BeginCharge(Ahead[0]->GetActorLocation(),
								   InfernoChargeSpeedCmPerSecond,
								   InfernoChargeHalfWidthCm,
								   InfernoChargeDamagePercent);
				++Acted;

				UE_LOG(LogCataclysm, Log, TEXT("%s began an Inferno Charge."),
					   *Enemy->GetName());
			}
		}
	}

	return Acted;
}

bool UCataclysmEnemyModifiers::AuraPulseIsDue(float SecondsSinceLastPulse)
{
	// AT OR PAST THE INTERVAL, not strictly past it. The per-character step runs
	// at a fixed 0.25 seconds and the interval is a whole second, so the
	// accumulated figure lands exactly on 1.0 rather than near it, and a strict
	// comparison would make every pulse wait an extra step.
	return SecondsSinceLastPulse >= AuraPulseIntervalSeconds;
}
