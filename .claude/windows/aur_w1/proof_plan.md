# Window aur-w1: the C++ guard proof, as registered and approved 2026-11-27

ONE `prove_cpp_guard` call with all three breaks, as the coordinating session ruled ("Three separate calls cost twelve
builds, and one costs four").

- **Prefix:** `Cataclysm.AuraReservation.+Cataclysm.NoCaps.+Cataclysm.Skills.AnAuraReservesHealth`
  - performs 20 tests: 13 + 6 + 1, counted from the registration strings on 7bc095034.
- **Breaks, each conditional, none an early return:**
  - CA, `game/Source/Cataclysm/AbilitySystem/CataclysmCommand.cpp`, `ReservedAfterReductions`:
    `FMath::Min(PercentLess, MostReservePercentLess)` -> `PercentLess`
  - CB, `game/Source/Cataclysm/AbilitySystem/CataclysmAbilitySystemComponent.cpp`, `RepriceAuraReservations`:
    `Held.Amount = Ability->ManaReservationFor(this);` ->
    `if (Held.Amount <= 0.0f) { Held.Amount = Ability->ManaReservationFor(this); }`
  - CC, `game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplates.cpp`, `UCataclysmAuraSkill::CheckCost`:
    `return HasRoomToReserve(AbilitySystem);` -> `return HasRoomToReserve(AbilitySystem) || bHeld == false;`
- **Predicted named failures, all eight and no others (short names, as the engine log prints them):**
  1. TheSummedReductionStopsAtSeventyFivePercentAndTheFloorIsOne (CA)
  2. NoReductionTakesAReserveBelowOnePoint (CA)
  3. ALevelGainedLowersAHeldAurasReservationAtOnce (CB)
  4. AReductionTakenOffRaisesAHeldAurasReservationAtOnceAndHoldsTheMana (CB)
  5. AReservationThatNoLongerFitsTheWholeMaximumSwitchesTheAuraOff (CB)
  6. RoomIsMeasuredAgainstTheMaximumAndNotTheManaInHand (CC)
  7. TwoAurasFitAPoolOfAHundredAndAThirdDoesNot (CC)
  8. AnAuraReservesHealthWhenTheManaPoolBecameHealth (CC)
- **Restored half:** 20 performed, 20 succeeded.

Start only after BOTH messages from the coordinating session: the Soul Collector PR merged (merge development, re-run
`tools/apply_aura_reservation_2026_11_27.py` and the generator), and the machine is this session's.
