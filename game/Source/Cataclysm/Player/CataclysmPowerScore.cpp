// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPowerScore.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"

const TArray<int32>& UCataclysmPowerScore::TierAnchors()
{
	// Indexed by tier, so entry 0 is unused and entry 8 is the end of the game.
	// Authoritative, from `scoring.PLAYER_MAX_SCORES` in the simulation.
	static const TArray<int32> Anchors = {
		0, 385, 883, 1508, 2225, 3078, 4057, 5120, 6327,
	};
	return Anchors;
}

namespace
{
	/**
	 * The five weights, derived once from the two end anchors and the four
	 * shares. Mirrors `_derive_weights` in sim/cataclysm_sim/player_power.py.
	 */
	struct FDerivedWeights
	{
		double Level = 0.0;
		double Gear = 0.0;
		double UpgradeFactor = 0.0;
		double Gem = 0.0;
		double Resistance = 0.0;
	};

	FDerivedWeights DeriveWeights()
	{
		using FScore = UCataclysmPowerScore;

		const double Tier1 = FScore::TierAnchors()[1];
		const double Tier8 = FScore::TierAnchors()[8];

		// The reference character's score works out to a*tier^2 + b*tier, with
		// no constant. It is quadratic for one structural reason: gear rarity
		// and upgrade level both rise with the tier and multiply each other,
		// and gem rarity rises while the number of filled sockets rises too.
		// Level and resistances are linear.
		//
		// Both ends are pinned rather than least-squares fitting all eight.
		// Tier 1 is where a new character is measured and tier 8 is the end of
		// the game, so those two have to be exact.
		//   a + b = Tier1  and  64a + 8b = Tier8
		const double A = (Tier8 - 8.0 * Tier1) / 56.0;

		FDerivedWeights Out;

		// Level, gems and resistances are pinned by their tier 8 totals alone.
		Out.Level = FScore::ShareLevel * Tier8 / FScore::MaxLevel;
		Out.Gem = FScore::ShareGems * Tier8
				/ (static_cast<double>(FScore::TotalSockets) * FScore::MaxRarity);
		Out.Resistance = FScore::ShareResistances * Tier8
					   / (static_cast<double>(FScore::ResistanceCount)
						  * FScore::ResistanceCap);

		// Curvature comes from two places: gems, where rarity and the number of
		// filled sockets both climb, and gear, where rarity and upgrade level
		// both climb. Gems take whatever their share implies; gear supplies the
		// rest.
		const double CurvatureFromGems =
			(static_cast<double>(FScore::TotalSockets) / FScore::MaxRarity) * Out.Gem;
		const double CurvatureFromGear = A - CurvatureFromGems;

		// CurvatureFromGear = GearPieces * GearWeight * UpgradeFactor
		const double GearTimesFactor = CurvatureFromGear / FScore::GearPieces;

		// Gear at tier 8 is 18 pieces of rarity 8 at +10:
		//   ShareGear * Tier8 = 18 * 8 * GearWeight * (1 + 10 * UpgradeFactor)
		const double GearAtTier8 = FScore::ShareGear * Tier8;
		Out.Gear = (GearAtTier8
					- static_cast<double>(FScore::GearPieces) * FScore::MaxRarity
					  * FScore::MaxUpgrade * GearTimesFactor)
				 / (static_cast<double>(FScore::GearPieces) * FScore::MaxRarity);

		Out.UpgradeFactor = GearTimesFactor / Out.Gear;
		return Out;
	}

	const FDerivedWeights& Weights()
	{
		static const FDerivedWeights Derived = DeriveWeights();
		return Derived;
	}
}

float UCataclysmPowerScore::LevelWeight()      { return static_cast<float>(Weights().Level); }
float UCataclysmPowerScore::GearWeight()       { return static_cast<float>(Weights().Gear); }
float UCataclysmPowerScore::UpgradeFactor()    { return static_cast<float>(Weights().UpgradeFactor); }
float UCataclysmPowerScore::GemWeight()        { return static_cast<float>(Weights().Gem); }
float UCataclysmPowerScore::ResistanceWeight() { return static_cast<float>(Weights().Resistance); }

FCataclysmPowerBreakdown UCataclysmPowerScore::Breakdown(
	const FCataclysmScoredCharacter& Character)
{
	const FDerivedWeights& W = Weights();

	double FromLevel = W.Level * FMath::Clamp(Character.Level, 0, MaxLevel);

	// Upgrade level MULTIPLIES rarity rather than adding to it, because a fully
	// upgraded Cataclysmic item should be worth far more than a fully upgraded
	// Everyday one. This is the only place two inputs multiply, and the only
	// reason the score curve bends.
	double FromGear = 0.0;
	for (const FCataclysmScoredGear& Piece : Character.Gear)
	{
		const int32 Rarity = FMath::Clamp(Piece.Rarity, 1, MaxRarity);
		const int32 Upgrade = FMath::Clamp(Piece.Upgrade, 0, MaxUpgrade);
		FromGear += Rarity * (1.0 + W.UpgradeFactor * Upgrade);
	}
	FromGear *= W.Gear;

	// Socket count has no weight of its own: it is the number of terms in this
	// sum, so filling a socket is what a socket contributes.
	double FromGems = 0.0;
	for (const int32 Rarity : Character.Gems)
	{
		FromGems += FMath::Clamp(Rarity, 1, MaxRarity);
	}
	FromGems *= W.Gem;

	double FromResistances = 0.0;
	for (const float Resistance : Character.Resistances)
	{
		FromResistances += FMath::Min(Resistance, ResistanceCap);
	}
	FromResistances *= W.Resistance;

	// A character with no level is not a character. Scoring one as zero is more
	// honest than clamping it up to level 1 and reporting six points of power
	// for something that does not exist; ReferenceCharacter returns one for a
	// tier outside 1 to 8.
	if (Character.Level <= 0)
	{
		FromLevel = 0.0;
	}

	FCataclysmPowerBreakdown Out;
	Out.FromLevel = static_cast<float>(FromLevel);
	Out.FromGear = static_cast<float>(FromGear);
	Out.FromGems = static_cast<float>(FromGems);
	Out.FromResistances = static_cast<float>(FromResistances);

	// Rounded the way the enemy scoring model rounds, so a player score and an
	// enemy score can be compared without one of them being half a point out.
	const double Total = FromLevel + FromGear + FromGems + FromResistances;
	Out.Total = static_cast<int32>(FMath::FloorToDouble(Total + 0.5));
	return Out;
}

int32 UCataclysmPowerScore::Score(const FCataclysmScoredCharacter& Character)
{
	return Breakdown(Character).Total;
}

FCataclysmScoredCharacter UCataclysmPowerScore::ReferenceCharacter(int32 Tier)
{
	FCataclysmScoredCharacter Out;
	if (Tier < 1 || Tier > 8)
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("Power Score reference character asked for tier %d, which "
					"is outside 1-8"), Tier);
		// Level 0 rather than the struct's default of 1, so this scores nothing
		// rather than reporting six points of power for a character that was
		// never asked for.
		Out.Level = 0;
		return Out;
	}

	// Gear and gem rarity equal the difficulty tier, because there are eight of
	// each and the best upgrade stone that can drop is capped by the tier. Gear
	// level is tier plus two capped at +10, which clears every rarity gate the
	// design states and reaches exactly +10 at tier 8.
	const int32 Rarity = Tier;
	const int32 Upgrade = FMath::Min(MaxUpgrade, Tier + 2);

	// Level, filled sockets and resistances all rise evenly to their maximum at
	// the end of tier 8.
	//
	// ROUNDED HALF TO EVEN, which is not Unreal's usual rounding and is not an
	// arbitrary choice. The Python model uses Python's round(), which rounds a
	// value ending in exactly .5 toward the even neighbour: round(12.5) is 12,
	// not 13. FMath::RoundToInt rounds half away from zero and gives 13. Three
	// of the eight tiers land on exactly .5 -- tier 1 at level 12.5, tier 4 at
	// 22.5 sockets, tier 5 at level 62.5 -- so a naive port disagrees with the
	// model on three of eight anchors.
	Out.Level = FMath::Max(1, static_cast<int32>(
		FMath::RoundHalfToEven(static_cast<double>(MaxLevel) * Tier / 8.0)));

	Out.Gear.Reserve(GearPieces);
	for (int32 Index = 0; Index < GearPieces; ++Index)
	{
		FCataclysmScoredGear Piece;
		Piece.Rarity = Rarity;
		Piece.Upgrade = Upgrade;
		Out.Gear.Add(Piece);
	}

	const int32 Filled = static_cast<int32>(
		FMath::RoundHalfToEven(static_cast<double>(TotalSockets) * Tier / 8.0));
	Out.Gems.Init(Rarity, Filled);

	Out.Resistances.Init(ResistanceCap * Tier / 8.0f, ResistanceCount);
	return Out;
}

FCataclysmScoredCharacter UCataclysmPowerScore::ScoredCharacterFrom(int32 Level, const TArray<FCataclysmItem>& Worn,
																	const TArray<float>& Resistances)
{
	FCataclysmScoredCharacter Out;
	Out.Level = Level;

	const auto Scored = [](const FCataclysmItem& Item, FCataclysmScoredGear& OutPiece)
	{
		ECataclysmRarity Rarity = ECataclysmRarity::Everyday;
		if (Item.Base.IsNone() || !UCataclysmItemModifiers::RarityOfItem(Item, Rarity))
		{
			return false;
		}
		OutPiece.Rarity = static_cast<int32>(Rarity) + 1;
		OutPiece.Upgrade = Item.GearLevel;
		return true;
	};
	const auto Value = [](const FCataclysmScoredGear& Piece)
	{
		return Piece.Rarity * (1.0 + static_cast<double>(UpgradeFactor()) * Piece.Upgrade);
	};

	const int32 FirstHand = static_cast<int32>(ECataclysmGearSlot::Weapon1);
	const int32 SecondHand = static_cast<int32>(ECataclysmGearSlot::Weapon2);
	TArray<int32> HandGems;
	bool bHandHeld = false;
	FCataclysmScoredGear Hand;
	for (int32 Index = 0; Index < Worn.Num() && Index < static_cast<int32>(ECataclysmGearSlot::Count); ++Index)
	{
		const FCataclysmItem& Item = Worn[Index];
		FCataclysmScoredGear Piece;
		if (!Scored(Item, Piece))
		{
			continue;
		}
		const bool bInTheHands = Index == FirstHand || Index == SecondHand;
		for (const FCataclysmSocket& Socket : Item.Sockets)
		{
			if (!Socket.Gem.IsNone())
			{
				(bInTheHands ? HandGems : Out.Gems).Add(static_cast<int32>(Socket.GemRarity) + 1);
			}
		}
		if (!bInTheHands)
		{
			Out.Gear.Add(Piece);
		}
		// THE HANDS AS ONE PIECE: the higher-scoring of the two (Q4).
		else if (!bHandHeld || Value(Piece) > Value(Hand))
		{
			Hand = Piece;
			bHandHeld = true;
		}
	}
	if (bHandHeld)
	{
		Out.Gear.Add(Hand);
	}
	// AND SIX OF THE HANDS' GEMS, the highest.
	HandGems.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 Index = 0; Index < HandGems.Num() && Index < HandSockets; ++Index)
	{
		Out.Gems.Add(HandGems[Index]);
	}

	for (const float Resistance : Resistances)
	{
		Out.Resistances.Add(FMath::Clamp(Resistance, 0.0f, ResistanceCap));
	}
	return Out;
}

FCataclysmScoredCharacter UCataclysmPowerScore::ScoredCharacterOf(const ACataclysmPlayerCharacter* Character,
																  int32 DifficultyTier)
{
	if (Character == nullptr)
	{
		FCataclysmScoredCharacter None;
		None.Level = 0;
		return None;
	}
	const ACataclysmPlayerState* State = Character->GetPlayerState<ACataclysmPlayerState>();
	const UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Character);

	TArray<float> Resistances;
	if (Abilities != nullptr)
	{
		for (const FName& DamageType : UCataclysmItemModifiers::DamageTypeNames())
		{
			Resistances.Add(
				UCataclysmDamageCalculation::ResistanceBeforeTheCapFor(Abilities, DamageType, DifficultyTier));
		}
	}
	return ScoredCharacterFrom(State ? State->GetCharacterLevel() : 0,
							   Equipment ? Equipment->GetSlots() : TArray<FCataclysmItem>(), Resistances);
}

FCataclysmPowerBreakdown UCataclysmPowerScore::LiveBreakdownOf(const ACataclysmPlayerCharacter* Character)
{
	return Breakdown(ScoredCharacterOf(Character, Character ? ACataclysmGameMode::DifficultyTierIn(Character) : 1));
}

int32 UCataclysmPowerScore::LiveScoreOf(const ACataclysmPlayerCharacter* Character)
{
	if (Character == nullptr)
	{
		return INDEX_NONE;
	}

	// THE FOUR THINGS THE SCORE IS WORKED OUT FROM, read cheaply; the score itself is worked out only when one moved.
	// Issue #2824, slice b. See the header.
	const UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	const ACataclysmPlayerState* State = Character->GetPlayerState<ACataclysmPlayerState>();
	const int32 EquipmentChanges = Equipment ? Equipment->ChangeCount() : INDEX_NONE;
	const int32 Refreshes = Equipment ? Equipment->RefreshCount() : INDEX_NONE;
	const int32 Level = State ? State->GetCharacterLevel() : 0;
	const int32 Tier = ACataclysmGameMode::DifficultyTierIn(Character);

	ACataclysmPlayerCharacter::FLiveScoreCache& Cache = Character->LiveScoreCache();
	if (Cache.Score == INDEX_NONE || Cache.EquipmentChanges != EquipmentChanges || Cache.Refreshes != Refreshes
		|| Cache.Level != Level || Cache.Tier != Tier)
	{
		Cache.Score = Breakdown(ScoredCharacterOf(Character, Tier)).Total;
		Cache.EquipmentChanges = EquipmentChanges;
		Cache.Refreshes = Refreshes;
		Cache.Level = Level;
		Cache.Tier = Tier;
	}
	return Cache.Score;
}

float UCataclysmPowerScore::OverwhelmFor(int32 PlayerScore, int32 EnemyScore, double TierWidth)
{
	if (TierWidth <= 0.0)
	{
		return 0.0f;
	}
	const double Gap = FMath::Max(0.0, static_cast<double>(EnemyScore) - static_cast<double>(PlayerScore));
	return static_cast<float>(FMath::Min(static_cast<double>(OverwhelmCap), OverwhelmRate * Gap / TierWidth));
}

float UCataclysmPowerScore::OverwhelmPointsAt(int32 PlayerScore, const FCataclysmScoredFloor& Floor, int32 RarityStep)
{
	if (PlayerScore == INDEX_NONE)
	{
		return 0.0f;
	}
	return 100.0f
		* OverwhelmFor(PlayerScore, UCataclysmEnemyScore::ScoreFor(Floor, RarityStep),
					   UCataclysmEnemyScore::TierWidth(Floor.DifficultyTier));
}

float UCataclysmPowerScore::OverwhelmPointsAgainst(const ACataclysmPlayerCharacter* Defender,
												   const ACataclysmEnemyCharacter* Attacker)
{
	if (Defender == nullptr || Attacker == nullptr)
	{
		return 0.0f;
	}
	// THE RUNG IT STANDS AT, NOT THE ONE ITS DEATH PAYS AS: the Corrupted Stalker pays as a Boss while it stands, and
	// hits, as a Common (`ACataclysmEnemyCharacter::RarityStepItPaysAs`). The coordinating session's judgement, slice b.
	return OverwhelmPointsAt(LiveScoreOf(Defender), UCataclysmEnemyScore::FloorIn(Attacker), Attacker->RarityStep);
}

FString UCataclysmPowerScore::OverwhelmText(int32 PlayerScore, const FCataclysmScoredFloor& Floor)
{
	if (PlayerScore == INDEX_NONE)
	{
		return FString();
	}
	// THE TWO ENDS OF THE LADDER: the rung met most often and the strongest. Every rung between applies between them.
	const float FromCommon = OverwhelmPointsAt(PlayerScore, Floor, 0);
	const float FromCataclysmBoss =
		OverwhelmPointsAt(PlayerScore, Floor, UCataclysmEnemyScore::RarityStepCount - 1);
	if (FromCataclysmBoss <= 0.0f)
	{
		return TEXT("Overwhelm here: none, your Power Score is at or above every creature's.");
	}
	return FString::Printf(TEXT("Overwhelm here: %.1f from a Common, %.1f from a Cataclysm Boss, in points off your "
								"resistance, armour, block and evasion."),
						   FromCommon, FromCataclysmBoss);
}
