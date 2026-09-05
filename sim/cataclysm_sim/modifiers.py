"""Dungeon modifiers, a copy of `game/Data/DungeonModifiers.csv`.

Each entry is (name, score). The sheet's 'Weight' column is used as the
modifier's score, which feeds straight into calculateScores' modifierScore.

THIS IS A COPY AND IT DRIFTED. It held 116 rows against the data file's 117
from issue #504 until issue #1282, missing the Corrupted Stalker, and nothing
compared the two. `tools/tests/test_dungeon_modifier_port.py` now does, in the
shape of the four guards that already existed for the power model, the day
clock, the surge scheduler and the empire map. Add a row here and to the data
file together, or that guard fails.

WHAT THE WEIGHT COLUMN MEANS IS NOT SETTLED IN `docs/`. This module consumes it
as a danger score, higher being more dangerous, which is what feeds
modifierScore. `game/Source/Cataclysm/Data/CataclysmDataRows.h` describes the
same column as "Selection weight. Higher is more common." Those cannot both be
right, and `docs/DECISIONS.md` records neither. Issue #1298 -- do not change
this module to match the C++ on the strength of the comment alone.
"""

from __future__ import annotations

#: The Cataclysm Type meaning "every Cataclysm draws this", rather than one.
#: `tools/generate_datatables.py` already lists it in its own CATACLYSM_TYPES,
#: and ten enemy modifiers use it the same way. The project owner chose it for
#: the Corrupted Stalker on 2026-08-14 -- see docs/DECISIONS.md, "The Corrupted
#: Stalker is a Generic dungeon modifier at weight 20".
GENERIC = "Generic"

MODIFIERS: dict[str, list[tuple[str, float]]] = {
    'Celestial': [
        ('Edict of Silence', 20),
        ('Divine Resurgence', 15),
        ('Eternal Chorus', 15),
        ('Forced Tithes', 15),
        ('Holy Repercussions', 15),
        ('Sanctioned Passage', 15),
        ('Angelic Wardens', 10),
        ('Divine Wrath', 10),
        ('Hallowed Groundfall', 10),
        ('Heaven’s Quake', 10),
        ('Lightforged Walls', 10),
        ('Trial of Endurance', 10),
        ('Golden Spires', 5),
        ('Judgment Zones', 5),
        ('Wings of the Host', 5),
    ],
    'Chaos': [
        ('Fragmented Reality', 20),
        ('Reality Twister', 20),
        ('Chaos Touched', 15),
        ('Rule of Chaos', 15),
        ('Unstable Portal', 15),
        ('Chaotic Loot', 10),
        ("Pandora's Box", 10),
        ('Trick or Treat', 10),
        ('Unstable Dimensions', 10),
        ('Volatile Evolution', 10),
        ('Wild Magic', 10),
        ('Echo Chamber', 5),
        ('Illusory Enemies', 5),
        ('Reality Rifts', 5),
        ('The Labrynth', 5),
    ],
    'Death': [
        ('The Reaper', 20),
        ('Echoes of the Past', 15),
        ('Mortal Decay', 15),
        ('Dead Rising', 10),
        ("Death's Embrace", 10),
        ('Dirge Resonance', 10),
        ('Grave Tide', 10),
        ('Necrotic Bloom', 10),
        ('Necrotic Ground', 10),
        ('Obsidian Sarcophagi', 10),
        ('Soul Chains', 10),
        ('Vengful Wraiths', 10),
        ('Cryptquake', 5),
        ('Funereal Procession', 5),
        ('Grim Totems', 5),
    ],
    'Demonic': [
        ('Blood Bond', 20),
        ('Infernal Brand', 20),
        ('Blood Altar', 15),
        ('Abyssal Rifts', 10),
        ('Blood Gates', 10),
        ('Demonic Guide', 10),
        ('Hellfire', 10),
        ('Infernal Beacons', 10),
        ('Infernal Rain', 10),
        ('Infernal Seals', 10),
        ('Soul Harvest', 10),
        ('Blood Price', 5),
        ('Demon Prince', 5),
        ('Pact of Temptation', 5),
    ],
    'Famine': [
        ('Hard Mode', 20),
        ('Starvation', 20),
        ('Dehydration', 15),
        ('Ravenous Hoard', 15),
        ('Scarcity', 15),
        ('Withering Touch', 15),
        ('Desperate Measures', 10),
        ('Diminishing Returns', 10),
        ('Famished Beasts', 10),
        ('Starvation Curse', 10),
        ('Suffering Aura', 10),
        ('Swarm of Locusts', 10),
        ('Luxury Hoarders', 5),
        ('Recession', 5),
        ('Withered Ground', 5),
    ],
    # One row, and the only Generic dungeon modifier. Every Cataclysm can draw
    # it, which is what `pool_for` below is for.
    'Generic': [
        ('Corrupted Stalker', 20),
    ],
    'Pestilence': [
        ('Plague Convergence', 20),
        ('Infection Bloom', 15),
        ('The Plaguebearer', 15),
        ('Carrion Feast', 10),
        ('Contagious Touch', 10),
        ('Epidemic', 10),
        ('Infested Veins', 10),
        ('Leech Spores', 10),
        ('Pestilent Empowerment', 10),
        ('Plague Harbingers', 10),
        ('Raw Sewage', 10),
        ('Spore Clouds', 10),
        ('Fungal Overgrowth', 5),
        ('Quarantine Breach', 5),
        ('The Infested Hoard', 5),
    ],
    'Void': [
        ('The Blackest Shadow', 20),
        ('Those in the Dark', 20),
        ('Insanity Bursts', 15),
        ('Shadowy Enemies', 15),
        ('Singularity Wells', 15),
        ("The Nihil's Embrace", 15),
        ('All Consuming', 10),
        ('Anti-Magic Zones', 10),
        ('Mind-Shattering Illusions', 10),
        ('Portal Unleashing', 10),
        ('Void Parasite', 10),
        ('Grasping Tentacles', 5),
    ],
    'War': [
        ('Supply Lines', 20),
        ('Battlefield Relics', 10),
        ('Battlefield Tactics', 10),
        ('Blood Debt', 10),
        ('Blood-Forged Champions', 10),
        ('Field Medic', 10),
        ('March of Progress', 10),
        ('Royal Guard', 10),
        ('War Banner', 10),
        ('Warzone Control Points', 10),
        ('Artillery Strike', 5),
        ("Commander's Aura", 5),
        ('Fog of War', 5),
        ('Forced March', 5),
        ('Morale Break', 5),
    ],
}

#: Every key of MODIFIERS, which INCLUDES `Generic`. That is not the roster of
#: eight Cataclysms a run picks from -- `config.CATACLYSM_ROSTER` is. Nothing
#: reads this today; it is kept because the data file has the same column.
CATACLYSM_TYPES = tuple(sorted(MODIFIERS))


def pool_for(active_types) -> list[tuple[str, float]]:
    """Every modifier the given Cataclysms can draw from.

    THE RULE LIVES HERE AND NOT IN ITS CALLERS. Issue #1282 is a table that
    drifted from its source because there were two copies of it; there were also
    two copies of this rule, in `engine.Simulation.__init__` and in
    `analyse_dungeons.pool_for`, and adding the Generic row would have fixed
    neither. One rule, one place.

    A Generic modifier belongs to every Cataclysm rather than to one, so it goes
    in ONCE however many are active. Twice would be worse than cosmetic:
    `_make_dungeon` draws with `random.sample`, which treats equal values at
    different positions as distinct, so a duplicated entry could fill two of a
    dungeon's modifier slots with the same modifier.

    WHAT THIS DOES NOT SETTLE. Whether the Corrupted Stalker competes for one of
    those slots or is granted on top of them. The 2026-08-14 decision that made
    it Generic says that is unsettled and that "nothing depends on the answer
    yet". Drawing it from the pool is the reading its own word "drawable" gives,
    and it is now the first thing that does depend. Issue #1282 says so.
    """
    pool = [m for t in active_types for m in MODIFIERS.get(t, [])]
    pool += MODIFIERS.get(GENERIC, [])
    return pool
