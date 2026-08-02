# Empire Skill Tree Keystones

> Exported from Google Drive (Doc `1qhS-gsZvWEXgjefh4mrbrbezwyulD3L_dGwo_ybFgcM`),
> last modified 2026-02-10. Source of truth is the Drive doc; this copy exists so
> the sim and the Unreal data pipeline can be diffed against it in-repo.

### Tier 1: Foundations of the Empire

- **The Aegis of Hope:** Your Outposts have 50% increased defenses.
- **The Delver:** Reduces the number of days it takes to clear a dungeon by **5**, to a minimum of **1**.
- **The Hoarder:** Loot rarity is increased by **25%** and quantity by **100%**.

### Tier 2: The Edicts of Power

- **The Sentinel:** All of your cities gain **10%** damage reduction against the Cataclysm type with the most dungeons in that city.
- **The Collector's Decree:** Add 10 floors to all dungeons.
- **The Soul Forge:** When improving the level of an item, you have a **15%** chance to increase its level by **2** instead of 1, up to the maximum of 10.

### Tier 3: The Imperial Vanguard

- **The Warlord:** When you clear a dungeon, there is a **15%** chance to instantly remove another dungeon in the same city.
- **The Master Tinkerer:** When enchanting, you have a **5%** chance to hit a critical success, doubling the rolls of the enchantment and completing the craft instantly.
- **The Midas Touch:** All enemies drop 50% more gold.

### Tier 4: The Imperial Zenith

- **Imperial Prowess:** You can now take an additional keystone from any of the previous tiers.
- **The Last Stand:** When you enter a dungeon in a city that is within 7 days of falling, the time it takes to run that dungeon is reduced to **one day**.
- **The Flood Barrier:** Add 20 days to the surge timer. This applies to every surge.

## The Empire Development Tree: Final Design

The Empire Development Tree is a circular, four-branched skill tree that visually represents an empire expanding outwards from a central capital. The player invests points to progress through the tree, with each branch offering a unique path to victory.

The tree is divided into four distinct "pie slices," with each slice representing a core strategic path. The passives within each slice are tiered, radiating outwards from the center of the tree.

### The Four Branches

1. **Architect:** Focuses on city defense, population management, and the overall resilience of the empire.
2. **Explorer:** Focuses on dungeon speed, efficiency, and loot.
3. **Treasurer:** Focuses on economy, gold drops, and generating resources.
4. **Artisan:** Focuses on all aspects of crafting, enchanting, and item modification.

### The Final Capstones

The final ability in each branch is a powerful capstone passive. These are not mutually exclusive; a player can acquire all four of them by investing enough points into each respective branch.

- **Architect: The Royal Engineer's Blessing** — For every city that has a defensive upgrade, all cities in your empire gain a **2%** passive defense bonus.
- **Explorer: The Cartographer's Blessing** — For every city that has an Explorer-based city upgrade, loot quantity and rarity are increased by 5%.
- **Tyrant: The Golden Touch** — For every city that has an economic upgrade, all gold and crafting materials dropped from dungeons are increased by **10%**.
- **Artisan: The Alchemist's Transmutation** — Increase the stats of a crafted affix on an item by 1% per passive point allocated in the Artisan branch.

## Architect Quadrant

### Tier 1: Core Foundation

- **Foundation (Max 10 pts):** +5% city Defense per point.
- **Structural Integrity (Max 10 pts):** -3% Damage from dungeons per point.
- **Urban Planning (Max 10 pts):** +5% Base Outpost Population per point.
- **NOTABLE: Emergency Shelters:** +3 Days to Resolution Timers for Outposts.
- **NOTABLE: Scaffolding:** City upgrades cost 50% less gold, but take 2x longer to build.

### Tier 2: Expansion & Logistics

- **Masonry Techniques (Max 15 pts):** +10% max city defense for Bulwarks and Sanctuaries per point.
- **Urban Fortification (Max 15 pts):** -4% Damage from Surge Events per point.
- **Imperial Census (Max 10 pts):** +10% Base Population for Bulwarks per point.
- **NOTABLE: Public Works:** -20% Gold cost for all City Upgrades.
- **NOTABLE: Rapid Renovation:** -1 Day to city upgrade construction time (Min 1 day).

### Tier 3: The Adaptive Bulwark (Decision Tier)

- **The 4 Decision Nodes:** (Demonic/Celestial, etc.) — Choose one for each to get 25% Resistance.
- **Global Vigilance (Max 20 pts):** -2% Damage from all sources (Surges/Dungeons) per point.
- **Metropolitan Growth (Max 10 pts):** +15% Base City Population per point.
- **NOTABLE: Martial Law:** At <10% City Health, Dungeon Resolution Timer is extended by +5 Days.
- **NOTABLE: Imperial Decree:** +20% City Health, -10% to population.
- **NOTABLE: Scorched Earth:** You can "manually" destroy a city's upgrade to instantly reset a dungeon's resolution timer to its max value. (Sacrifice the wall to save the people.)

### Tier 4: The Imperial Rim

- **Bastion Spirit (Max 15 pts):** +1 Days to all dungeon Resolution Timers per point.
- **Sovereign's Might (Max 25 pts):** +2% Global Damage Reduction for all cities per point.
- **Manifest Destiny (Max 20 pts):** +2% Total Empire Population per point (Multiplicative).
- **NOTABLE: Imperial Command:** Cities with 2+ upgrades take 40% less Surge damage.
- **NOTABLE: Beacon of Hope:** Cities within 2 hexes of The Pillar have +50% Max Health.
- **NOTABLE: Last Stand Protocols:** Sanctuary defenses survive at 1 on first fatal dungeon resolution. (Resets every tier.)
- **NOTABLE: The Architect's Legacy:** Reclaiming a fallen city now restores it to **75%** of its original Population/Defense instead of 50%.
- **NOTABLE: The Mason's Guild:** Every 20 days, the Outpost or Bulwark with the lowest defense restores 20% of its Max Defense and 15% of its Max Population. This can target the same city multiple times.

## Treasury Quadrant

### Tier 1: The Merchant's Quarter

*Focus: Raw gold scaling and basic dungeon profit.*

- **Coinage (Max 10 pts):** +5% Gold found per point.
- **Master Trader (Max 10 pts):** +5% Gold gained when selling items in the Market per point.
- **War Funding (Max 10 pts):** -2% cost for the first upgrade in a city per point.
- **NOTABLE: Cleanup Crew:** When a dungeon is cleared, 10% of the Gold you *didn't* pick up off the ground is automatically sent to your stash.
- **NOTABLE: Bounties:** Every dungeon has a 10% chance to spawn a "Bounty Target" (Elite) that drops a massive sack of Gold.

### Tier 2: Urban Investment

*Focus: Scaling the city-gold relationship.*

- **Municipal Bonds (Max 15 pts):** -3% Gold cost for City Upgrades per point.
- **Global Credit (Max 5 pts):** Allows you to start a city upgrade or crafting project even if you are short by up to 5% of the gold cost (Debt). The debt is repaid from your next gold drops.
- **NOTABLE: Commercial Hubs:** When running a dungeon in a city with a **Treasury Upgrade**, Gold drops are increased by 20%.
- **NOTABLE: Rapid Funding:** You can pay 150% of an upgrade's cost to reduce its construction time by 2 days (Min 1 day).

### Tier 3: The High Chancellor's Office

*Focus: Spending gold to manipulate the clock and materials.*

- **Salvage Rights (Max 20 pts):** Gain 50 Gold for every "Floor" cleared in a dungeon upon completion.
- **Thrifty (Max 25 pts):** Items in the Market cost 1% less gold per point.
- **Wealth Interest (Max 10 pts):** Every 20 days, gain 0.5% interest on your total Gold Treasury, capped at 5,000 gold per tick.
- **NOTABLE: Bribery:** Once per "Surge" cycle, you can spend a large Gold fee to reset a single dungeon resolution timer back to its maximum value.
- **NOTABLE: Black Market:** A special tab appears in Market that allows you to buy high-tier Artisan materials directly for Gold.
- **NOTABLE: Reinvestment:** Every 25,000 Gold spent on City Upgrades grants a 1% permanent boost to your Empire XP modifier (Capped).

### Tier 4: The Sovereign's Vault

*Focus: The "Endgame" money-sinks and safety nets.*

- **Imperial Reserve (Max 25 pts):** +1% Global Damage Reduction for all cities for every 100,000 Gold in your Treasury (Capped at 25%).
- **Golden Age (Max 25 pts):** +1% Total Empire Population per point (Wealth attracts people).
- **NOTABLE: Economic Bailout:** If a City's health hits 0, you can pay a massive Gold fee (that scales with your level) to prevent it from becoming a **Fallen City**. (Once per city per "Tier".)
- **NOTABLE: The Gilded Path:** Gain +1% Loot Rarity in dungeons for every 5% "Gold Cost Reduction" you have across the entire Empire Tree.
- **NOTABLE: Stimulus Package:** If your Empire Population falls below 50% of its maximum possible value, Gold drops in dungeons are doubled until you reclaim a city.
- **NOTABLE: Pax Imperialis:** For every 1,000 surviving citizens in your Empire, you generate 5 Gold every 1 day.

## Explorer Quadrant

### Tier 1: The Scout

- **Pacing (Max 10 pts):** -1 days from dungeon run time per point (Min 1).
- **Eyes of the Empire (Max 10 pts):** +5% Loot Rarity per point.
- **NOTABLE: Fleet Footed:** -5 days from dungeon run time.
- **NOTABLE: Field Depot (Rank 1):** A Stash appears every 30 floors in dungeons.
- **NOTABLE: Tactical Entry:** If a dungeon has more than 50 floors, the days it takes to run are halved.

### Tier 2: The Wayfinder

- **Bounty (Max 15 pts):** +5% Loot Quantity per point.
- **Exclusionary Mapping (Max 10 pts):** -1 floors to dungeons per point.
- **Deep Boring (Max 10 pts):** +1 floors to dungeons per point.
- **Gamble (Max 3 pts):** Every 20 days get one dungeon modifier reroll per point.
- **NOTABLE: Efficiency Premium:** +5% Loot Quantity for every **Day** removed from a dungeon's default run time (capped at 50%).
- **NOTABLE: Quality over Quantity:** +2% Loot Rarity for every **Floor** removed from the dungeon's default depth.
- **NOTABLE: Architectural Insight:** +1 floors to dungeons for every 10 points invested in the **Architect** branch.

### Tier 3: The Trailblazer

- **Architect of Greed (Max 20 pts):** +1 floors to dungeons per point.
- **Overclock (Max 20 pts):** -1 day from dungeon run time per point.
- **NOTABLE: Field Depot (Rank 2):** A Stash appears every 15 floors.
- **NOTABLE: Temporal Efficiency:** Every 5 points invested in the **Artisan** branch increases loot quantity by 10%.
- **NOTABLE: Weightless Spoils:** Adds 10 inventory slots.
- **NOTABLE: The High Roller:** For every 10 floors **Added** to a dungeon, gain a 5% chance for the Boss to drop an additional Legendary or above item.

### Tier 4: Master Explorer

- **Temporal Mastery (Max 25 pts):** -1 Day from dungeon run time per point.
- **Manifest Wealth (Max 25 pts):** +1% Loot Rarity per floor cleared in the current dungeon.
- **NOTABLE: Field Depot (Rank 3):** A Stash appears every 5 floors.
- **DECISION NOTABLE: Auto-Seller:** Items hidden by your loot filter are automatically sold for 25% gold value **OR** **Auto-Shatter:** Items hidden by your loot filter are automatically converted into crafting materials upon drop.
- **NOTABLE: One-Day Specialist:** If a dungeon's run time is reduced to the **1-Day minimum**, all Loot Quantity/Rarity modifiers from the Explorer branch are doubled for that dungeon.
- **NOTABLE: Sovereign's Haste:** Gain +1% Movement Speed for every **10,000 Population** currently alive in your Empire.

## Artisan Quadrant

### Tier 1: The Workshop Floor

- **Apprentice Smith (Max 10 pts):** 1% chance per point to upgrade a random affix on an item to the next tier when using the smith.
- **Scavenger (Max 10 pts):** Increases drop quantity of t3 and below crafting materials by 5% per point.
- **Material Conservation (Max 10 pts):** 2% chance per point to not consume t2 and below crafting materials.
- **NOTABLE: Apprentice Oversight:** Reduces the Gold cost of all Tier 1 and Tier 2 crafts by 20%.
- **NOTABLE: Limited Knowledge:** Reduces the CR gain during crafting by 10%.

### Tier 2: Guild Specialization

- **Industrial Speed (Max 5 pts):** -10% of Days required for any crafting project per point invested.
- **Refining Techniques (Max 15 pts):** +5% chance for a crafted item to roll with a "Higher Tier" base stat.
- **Salvage Protocol (Max 10 pts):** Dismantling items now yields 25% more crafting materials.
- **NOTABLE: Explorer Synergy:** Every 10 points in the **Explorer** branch increases the chance to "Double Craft" (get two items for the cost of one) by +2%.

### Tier 3: The Grand Manufactory

- **Master Craftsman (Max 10 pts):** -1 Day to crafting time per point.
- **Resource Management (Max 20 pts):** +1% chance per point to not consume **Rare** materials (Boss drops, Essence) when crafting.
- **Expanded Station (Jeweler, Max 3 pts):** Adds +1 Worker to the Jeweler station.
- **Expanded Station (Smith, Max 3 pts):** Adds +1 Worker to the Smith station.
- **Expanded Station (Enchantress, Max 3 pts):** Adds +1 Worker to the Enchantress station.
- **NOTABLE: Master's Touch:** 1% chance per 100 CR on an item to reset the CR to 0 when crafting.
- **NOTABLE: Architectural Synergy:** Every 10 points in the **Treasurer** branch reduces crafting gold costs by 5%.
- **NOTABLE: Sovereign's Assembly:** Reduce total crafting time required by 50%.

### Tier 4: The Sovereign's Forge

- **Heat Shielding (Max 5 pts):** Reduces the CR gain of all crafting actions by an additional 5% per point.
- **Flawless Production (Max 25 pts):** +1% chance per point for a crafted item to become "Exalted" (Max rolls on all baseline stats).
- **Residue Protocols (Max 10 pts):** Ignore 5% of Cataclysmic Residue per point when crafting.
- **NOTABLE: The Hidden Stash:** 5% chance when a crafting project finishes to duplicate the item.
- **NOTABLE: Treasury Synergy:** For every 100,000 Gold spent on crafting, gain a permanent 1% discount on all future projects (Capped at 30%).
- **NOTABLE: Assembly Line:** You can now queue crafting projects. I.E. step 1: enchant an item, step 2: level the item etc.
