# Passive Abilities

English reference for mod authors, AI assistants, and maintainers. Passive definitions are registered by ID, then bound to units or granted through supported gameplay actions.

[Manual home](../manual.md) · [Configuration sources](setup.md#installation-and-configuration-sources) · [Lua guide](lua.md) · [Research guide](research.md)

## Contents

- [Quick start](#quick-start)
- [Passive definition fields](#passive-definition-fields)
- [Triggers, targets, and event execution](#triggers-targets-and-event-execution)
- [Filters and state conditions](#filters-and-state-conditions)
- [Continuous modifiers and passive ownership](#continuous-modifiers-and-passive-ownership)
- [Action reference](#action-reference)
- [Land before vehicle transformation](#land-before-vehicle-transformation)
- [Numeric formulas](#numeric-formulas)
- [Warhead grants](#warhead-grants)
- [Passive visual fields](#passive-visual-fields)
- [Troubleshooting](#troubleshooting)

## Quick start

This example gives every Grizzly Tank a passive that heals it by 50 health after it destroys an enemy. Merge these entries into `Starfall.ini` or the loaded rules, using an unused registry number and appending to any existing `Starfall.Passives` list. `MTNK` must be registered in the target ruleset.

```ini
[Starfall.PassiveTypes]
0=Demo_FieldRepair

[MTNK]
Starfall.Passives=Demo_FieldRepair

[Demo_FieldRepair]
Trigger=OnKill
Filter.TargetHouse=Enemies
Action=Heal
Action.Target=Self
Action.Amount=50
```

Start a match, build a Grizzly Tank and damage it. Destroy an enemy with that tank and observe the healing. If the passive fails to load or execute, inspect `Starfall.log` and the [troubleshooting section](#troubleshooting).

## Passive definition fields

A passive section is registered in `[Starfall.PassiveTypes]` and named by its ID. Configure either an event action or a continuous `WhileState` result using the fields appropriate for that context. `Action.*` fields belong to an event action. `Modifier.*` fields belong to a continuous state.

```ini
[Starfall.PassiveTypes]
0=(passive ID)
; Registers a passive under an unused nonnegative numeric key.

[UnitTypeID]
Starfall.Passives=(comma-separated passive IDs)
; Binds registered passives to this unit type.

[PassiveID]
Trigger=(trigger ID)
; Selects the event or continuous state.

Action=(action ID)
; Selects a single event action. Action-specific fields use the Action. prefix.

Actions=(comma-separated action section IDs)
; Runs an ordered list of action sections. Repeated IDs remain repeated steps.

```

Use `Action` for a single result or `Actions` for an ordered list. See [multiple actions](#multiple-actions). The default action target depends on the action and event. See [target names](#target-names). A visuals-only passive follows the rules in [passive visual fields](#passive-visual-fields).

## Triggers, targets, and event execution

### Trigger catalog

Choose one trigger for each passive. Each entry below is an alternative. The first comment states the timing, and the second identifies the event counterpart.

```ini
[PassiveID]
Trigger=OnKill
; Holder receives a kill.
; Killed unit; recorded damage is zero.

Trigger=OnBeforeDealDamage
; Synchronous attacker interception before positive health loss.
; Actual victim.

Trigger=OnDealDamage
; Holder causes actual health loss.
; Victim.

Trigger=OnBeforeTakeDamage
; Synchronous victim interception before positive health loss.
; Attacker, possibly absent.

Trigger=OnTakeDamage
; Holder actually loses health.
; Attacker, possibly absent.

Trigger=OnDeath
; Holder is registered as destroyed; once per death.
; Killer, possibly absent.

Trigger=OnFire
; A standard shot is emitted, including supported spawned missiles. Burst shots count individually.
; Aimed unit; a ground target can supply a recorded point.

Trigger=OnBeforeFire
; Before the first shot of a standard volley.
; Aimed object; see the restricted replacement contract below.

Trigger=OnDeploy
; Supported infantry/simple-deployer posture enters deployed, vehicle deploys into a building, or deploy-fire completes.
; No counterpart; action target aliases resolve to the resulting holder/building.

Trigger=OnPromote
; Observed rank increases. A direct two-rank jump is one generic event.
; None.

Trigger=OnPromoteVeteran
; Observed promotion reaches veteran.
; None.

Trigger=OnPromoteElite
; Observed promotion reaches elite.
; None.

Trigger=OnPassiveGained
; This passive becomes newly available on the holder.
; None.

Trigger=OnPassengerConsumed
; Holder's direct passenger is consumed by Starfall's action.
; Consumed passenger's saved information.

Trigger=OnBuildingUpgrade
; A native building upgrade is successfully installed.
; Host building; Self is also the host. Bind this passive to the upgrade type, not merely the host type.

Trigger=OnBuilt
; A factory-produced standalone building is successfully placed, before its buildup finishes. Human and AI factory paths are supported.
; Newly placed building as Self; no counterpart.

Trigger=OnSignal
; A queued signal matching Signal is delivered.
; Optional unit attached to the signal; sender is separate.

Trigger=OnInterval
; Holder's positive Interval elapses while eligible and on-map.
; Current aimed unit or ground target at that time.

Trigger=WhileState
; Continuous evaluation while eligible.
; Current target, where a formula needs it. No action list.

Trigger=Active
; A validated player command activates a selected holder.
; Selected unit for unit-target skills. Requires Lua.

```

Use `Active`, not the internal name `OnCast`. `OnSuperWeapon`, `OnMapTrigger`, and `OnScheduled` are internal execution contexts, not ordinary `Trigger` values to register.

An already captured `OnKill` still executes owner/location effects if its holder is removed before queued dispatch (for example, a terror drone exiting a destroyed vehicle). Live-unit actions still require a valid target. Casterless Starfall detonations retain the captured house for native damage filtering and passive grants.

Newly created veteran/elite units and rank restored from a save do not count as promotions. Renewing a passive, overlapping sources, reapplying stacks, or loading a save does not repeat acquisition events. Completely losing the passive and later acquiring it can trigger acquisition again.

`OnBuildingUpgrade` obtains its skill list from the installed upgrade type's bindings, then executes against the host. Its event stack snapshot is 1. Failed installations do not emit this event. It does not grant the upgrade's entire passive list permanently to the host.


`OnBuilt` fires once on successful factory placement, not when the production timer completes. It does not fire for failed placement, preplaced buildings, captures, `SpawnBuilding`, transformations, upgrade attachments, walls/pavement, or loading a save. Bind it to the produced building type, or grant it through research. Research acquired after placement does not award previous construction. Actions run through the normal synchronized event queue; nearby gifts use `SpawnBuilding`'s normal deterministic placement and can fail when no valid space exists.

### Target names

| Name | Ordinary event meaning |
| --- | --- |
| `Self` | Holder. For death actions, the saved owner/location remains usable by appropriate actions. |
| `Self.Occupants` | Each direct member of the living holder: transport passengers (including carried vehicles), infantry in a native or BetterGarrison building, and the fully parked tank in a tank bunker. |
| `Target` | Counterpart: victim for outgoing damage/kill; attacker for incoming damage/death; aimed unit for firing/interval; attached unit for signals. |
| `Attacker` | Recorded attacker. It need not be alive when the queued action executes. |
| `Victim` | Recorded victim. Positional actions can fall back to its recorded location. |
| `VictimLocation` | Recorded impact/victim position; only positional actions accept this. |
| `Sender` | Signal sender; allowed only in `OnSignal`. |

For deploy, promotion, and passive-acquisition actions, `Target` resolves to `Self`, although their event counterpart for filtering/formulas is absent. Building-upgrade events explicitly use the host as both holder and counterpart.

Default `Action.Target` is `Target` for `ConsumeTarget`, `StealPassive` and `OnBeforeDealDamage`; otherwise it is `Self`. `SelfDestruct` and `ReplaceWeapon` require `Self`. Damage-interception targeting is more restrictive than ordinary targeting.

**Example. Heal each occupant by 10 every 30 game frames.** Register this passive in `[Starfall.PassiveTypes]` and add it to the transport or building’s `Starfall.Passives` list.

```ini
[CrewRecovery]
Trigger=OnInterval
Interval=30
Action=Heal
Action.Target=Self.Occupants
Action.Amount=10
```

Each action takes a fresh snapshot of direct members in engine order. A carried transport receives the action itself. Its own passengers are a separate group. An empty group contributes no success, and one successful member is enough for the action to succeed. Trigger chance, cooldown, filters and formula variables keep their ordinary event semantics. Actions that need a map position skip members in limbo. For `RunScript`, iterate `ctx.self:occupants()` in Lua. `Transform` with `LandFirst=yes` requires a single recipient.

The positional actions are exactly `FireWeapon`, `DetonateWarhead`, `SpawnUnit`, `SpawnBuilding`, `SpawnCrate`, `SpawnPickup`, `RemovePickup`, `PlayAnim`, `PlaySound`, `ShakeScreen`, and `SpawnParticleSystem`. They accept `VictimLocation`; Lua can supply a point table as their `Target`. A missing/dead unit does not become a valid heal or transformation target merely because its old position is known.

### Event pipeline and snapshots

Filters and conditions must pass together. Values within one filter list are alternatives. Cooldown suppresses counting. Qualifying events increment `Condition.Count`; reaching the threshold resets the count and rolls `Chance`. Failed probability or an entirely ineffective action group does not start cooldown. Successful action groups start cooldown and count toward `MaxTriggers`.

Ordinary events are queued. Event health, cost, ammo, passenger count, stack count, and relevant position/state values are snapshots. Earlier healing or stacking in the same action group does not rewrite the original formula snapshot. Native actions still check live object eligibility. `ConsumePassenger` is a specific exception: it supplies the consumed passenger's snapshot to subsequent steps in that action context.

Except for supported death/location/owner paths, an ordinary queued callback needs a living holder. Healing a dead holder does not resurrect it. Area damage is evaluated separately for each actual victim. Damage caused by a Starfall passive is protected against normal passive-damage feedback; do not design an unbounded retaliation chain around it.

### Synchronous damage interception

Order: attacker `OnBeforeDealDamage`, then victim `OnBeforeTakeDamage`, then health loss, then actual-loss events. `Event.Damage` in interception is the current pending health loss after preceding changes, rather than the weapon's nominal damage.

- Allowed INI actions: `SetDamage`, `BlockDamage`, and `RunScript` callbacks using the interception API.
- Outgoing interception targets `Target` or `Victim`; incoming interception targets `Self`.
- Lua native action calls during interception are limited to `SetDamage` and `BlockDamage`, acting on this victim.
- `SetDamage` requires a nonnegative integer or supported formula. `BlockDamage` takes no amount.
- These actions cannot be moved into an ordinary delayed callback to alter damage that already happened.
- Zero/negative engine damage, healing, forced damage that bypasses the relevant engine path, and damage absorbed before health loss are not interchangeable with this path.

**Fragment** to halve incoming pending damage:

```ini
[SF_HalfDamage]
Trigger=OnBeforeTakeDamage
Action=SetDamage
Action.Target=Self
Action.Amount=$(Event.Damage * 50%)
```

### Synchronous weapon replacement

`OnBeforeFire` requires exactly one `ReplaceWeapon` action and a valid `Action.Weapon`. It rejects `Actions`, non-default count/probability/cooldown/trigger limits/minimum damage, and `TriggerAnim`. Use default `Condition.Count=1`, `Chance=100%`, `Cooldown=0`, `MaxTriggers=0`, and `Condition.MinDamage=0`.

The first qualifying replacement is considered. If it cannot fire at the target, the original weapon is used; subsequent replacements are not tried. Original targeting/pursuit does not gain the replacement weapon's longer range. The selected weapon remains fixed through the volley and is saved mid-burst. Standard primary/secondary slots are the supported scope; unusual extension-specific firing paths require separate validation.

See the [Weapon firing scripts](lua.md#weapon-firing-scripts) in the Lua guide.

## Filters and state conditions

### Ordinary event fields

```ini
[PassiveID]
Filter.TargetHouse=(list)
; List: All,Self,Allies,Enemies,Neutral,None. Self means same owner; Allies means other allies.
; Default: All

Filter.TargetKind=(list)
; List: All,Infantry,Vehicle,Aircraft,Building,Other,None.
; Default: All

Condition.MinDamage=(number)
; Nonnegative integer. Actual lost health for damage events; pending loss for interception.
; Default: 0

Condition.Count=(number)
; Positive integer; per unit and passive.
; Default: 1

Chance=(number)
; Integer 0..100, optional % suffix.
; Default: 100%

Cooldown=(number)
; Nonnegative frame count after success.
; Default: 0

MaxTriggers=(number)
; Nonnegative successful-trigger cap; zero means unlimited.
; Default: 0

Interval=(number)
; Positive frames, required only for OnInterval; invalid on other triggers.
; Default: none

Stack.Max=(number)
; Positive stack cap, including the initial layer. First acquisition gives one layer.
; Default: 2147483647

Condition.Formula=(formula)
; $(...); nonzero means true. See formula restrictions.
; Default: none

```

`None` matches an absent counterpart/owner. For deploy/promotion/acquisition, leave filters at `All` unless deliberately testing absence. `Vehicle` includes ships; flying infantry remain `Infantry` because categories follow engine object types.

### Shared holder-state conditions

```ini
[PassiveID]
Condition.OwnerPower=(Sufficient or Low)
; Owning house's power situation.
; Allowed values: Sufficient, Low

Condition.SelfPowered=(boolean)
; Holder's own online/powered state.
; Allowed values: yes, no

Condition.SelfMoving=(boolean)
; Actual locomotor movement; buildings are stationary.
; Allowed values: yes, no

Condition.SelfCloak=(cloak state)
; Holder's own cloak state, distinct from enemy detection.
; Allowed values: Uncloaked, Cloaking, Cloaked, Uncloaking

Condition.Formula=(formula)
; Event snapshot for events; current values for continuous evaluation.
; Allowed values: expression

```

### WhileState-only conditions

```ini
[PassiveID]
Condition.SelfDeployed=(boolean)
; yes / no for supported infantry/simple-deployer postures. A stationary unit or a deployed-into building is not automatically a deployed-posture unit.
; Default: omitted

Condition.StateDurationSeconds=(number)
; Nonnegative decimal game seconds, up to three decimal places; multiply by 15 and round fractional frames upward. Interruption resets the wait.
; Default: 0

Condition.Nearby.Range=(number)
; Required when using nearby conditions; integer 1..32 cells.
; Default: none

Condition.Nearby.Houses=(list)
; List: All,Self,Allies,Enemies,Neutral.
; Default: All

Condition.Nearby.Kinds=(list)
; List: All,Infantry,Vehicle,Aircraft,Building.
; Default: All

Condition.Nearby.Types=(comma-separated type or category list)
; Concrete unit IDs or research categories such as @Infantry; combined with house and kind filters.
; Default: *

Condition.Nearby.CountAtLeast=(integer)
; Nonnegative inclusive bound.
; Default: 1, or 0 if only an upper bound is supplied

Condition.Nearby.CountAtMost=(integer)
; Inclusive bound, at least the lower bound.
; Default: 2147483647

Condition.Nearby.ScanInterval=(number)
; Positive frames; staggered sampling, cached between scans. Before first sample, condition is false.
; Default: 10

```

Nearby counting uses a planar circular cell range, excludes the holder, and deduplicates objects/buildings. Hostile cloaked/disguised objects must satisfy the visibility/detection checks. Nearby cache progress is saved.

A `WhileState` definition needs a modifier or supported visual result. When no state condition is specified, it is active continuously; `Condition.StateDurationSeconds` still applies when specified. Event-only fields such as `Action.*`, `Actions`, `Filter.*`, `Chance`, and `Condition.Count` are invalid in `WhileState`.

Leaving the map or entering limbo normally suspends continuous evaluation and removes its modifiers; re-entry restarts any wait. Interval progress restarts when the holder becomes eligible again. Timed grant expiry still advances while the unit is inside a transport or building. See the passenger-support exception below.

## Continuous modifiers and passive ownership

### Modifier catalog

All following fields belong to a `WhileState` passive. Omitted numeric multipliers contribute identity `1`; omitted switches contribute no effect.

```ini
[PassiveID]
Modifier.SpeedMultiplier=(number or formula)
; Mobile unit speed.
; Accepted value: Number or formula, 0 < value <= 100

Modifier.FirepowerMultiplier=(number or formula)
; Firepower.
; Accepted value: Number or formula, 0 < value <= 100

Modifier.TooltipText=(text with optional formulas)
; Append one line below the native name when the mouse hovers over this unit and the passive is active. Each formula reads this unit's state. If a value is unavailable, omit that passive's line.
; Accepted value: Text with optional inline $(...) formulas

Modifier.TooltipDecimals=(number)
; Fixed decimal places for all numbers in the line. The default rounds to two decimal places and trims trailing zeros.
; Accepted value: Integer 0..6, requires Modifier.TooltipText

Modifier.ArmorMultiplier=(number or formula)
; Armor strength; higher means more resistant.
; Accepted value: Number or formula, 0 < value <= 100

Modifier.WeaponRangeMultiplier=(number or formula)
; Standard maximum weapon range.
; Accepted value: Number or formula, 0 < value <= 100

Modifier.FireRateMultiplier=(number or formula)
; Divide standard volley rearm delay by this value; does not rewrite intra-burst delays.
; Accepted value: Number or formula, 0 < value <= 100

Modifier.ExperienceMultiplier=(number or formula)
; Scale the experience value of each enemy killed by the holder before the game distributes it.
; 1.5 grants 50% more experience. 0 grants none. Earned experience is retained.
; Accepted value: Number or formula, 0 <= value <= 100 (default 1).

Modifier.PowerMultiplier=(number or formula)
; This building's supply or drain; zero is allowed.
; Accepted value: Number or formula, 0 <= value <= 100

Modifier.VisualScale=(decimal multiplier)
; Unit/building drawing scale, not footprint. Building body, voxel turret and native owned animations follow the same scale. Set voxel turret offsets (TurretAnimX, TurretAnimY) in ordinary-size pixels. The renderer scales the mounting point and barrel with the body. Configure the foundation and fire offsets separately in art.
; Accepted value: Fixed number 0.01..2.00

Modifier.ProjectileScale=(decimal multiplier)
; Projectile, impact explosion, and existing CellSpread scale at launch.
; Accepted value: Fixed number 0.50..2.00

MultiShot.PreferSameLayer=(boolean)
; With MultiShot, prefer targets matching the primary target’s current air/ground status. A cell target counts as ground. Fill remaining slots from the other layer. Within each layer, retain each branch’s eligible target and fill vacancies by distance. An active passive contributing at least one extra target enables this preference for the shot.
; Accepted value: yes / no (default)

Modifier.MultiShot.PreferSameLayer=(boolean)
; With MultiShot, prefer targets matching the primary target’s current air/ground status. A cell target counts as ground. Fill remaining slots from the other layer. Within each layer, retain each branch’s eligible target and fill vacancies by distance. An active passive contributing at least one extra target enables this preference for the shot.
; Accepted value: yes / no (default)

MultiShot=(number)
; Extra distinct targets; specify only one alias. yes means $(This.Stack), no means zero.
; Accepted value: Nonnegative integer, formula, yes, or no

Modifier.MultiShot=(number)
; Extra distinct targets; specify only one alias. yes means $(This.Stack), no means zero.
; Accepted value: Nonnegative integer, formula, yes, or no

Modifier.DisablePrimary=(boolean)
; Disable standard primary slot.
; Accepted value: yes / no

Modifier.DisableSecondary=(boolean)
; Disable standard secondary slot.
; Accepted value: yes / no

Modifier.PrimaryWeapon=(text or ID)
; Replace standard primary slot.
; Accepted value: Existing weapon ID

Modifier.SecondaryWeapon=(text or ID)
; Replace standard secondary slot.
; Accepted value: Existing weapon ID

Modifier.EMP=(boolean)
; Starfall-managed stoppage; airborne vehicles/aircraft can crash.
; Accepted value: yes / no

Modifier.Taunt.Target=Source
; The recipient attacks the unit that granted this passive. WhileState conditions are optional.
; Mobile units pursue. Deployed units and buildings hold position. The recipient drops other orders, including stop, deploy and active skills.
; A warhead uses its firer. A Lua GrantPassive call may select a different unit with TauntTarget.

Modifier.Taunt.Priority=(nonnegative integer)
; Defaults to 0. The highest eligible priority wins. Equal priorities keep the current enemy.
; Each caster has its own duration. Loss of the current enemy falls back to another valid taunt.
; Removing the passive clears all its taunts. The current enemy and grant timers survive a save/load.

Modifier.Berserk=(boolean)
; Non-building berserk behavior, including friendly attacks.
; Accepted value: yes / no

Modifier.Mirage=(boolean)
; Vehicle stationary disguise behavior.
; Accepted value: yes / no

Modifier.Cloakable=(boolean)
; Grant native cloaking capability while the state is active; retain native cloak/decloak rules.
; Accepted value: yes / no, default no

Modifier.Immune=(boolean)
; Native-style immunity to ordinary health damage.
; Accepted value: yes / no

Modifier.ProjectileShield.Radius=(number)
; Required. Sphere radius in map cells, greater than 0 and at most 512.

Modifier.ProjectileShield.HeightOffset=(number)
; Vertical offset of the center from the holder in map cells, -512 to 512. Default 0.

Modifier.ProjectileShield.BlockInviso=(boolean)
; Include instant Inviso projectiles. Default no.

Modifier.ProjectileShield.DamageMultiplier=(number)
; Nonnegative decimal. Default 1. Zero completely absorbs the projectile.

Modifier.ImmuneToMindControl=(boolean)
; Block supported new mind-control attempts; does not release existing control.
; Accepted value: yes / no

Modifier.SpawnPassives=(list)
; Give passives to direct native Spawns= children.
; Accepted value: Registered passive ID list

Modifier.LinkedPassives=(list)
; While the parent state holds, the holder owns each child once. The link grants no stacks.
; Accepted value: Comma-separated registered passive IDs

Modifier.TransportPassives=(list)
; Passenger grants passives to its direct ground-vehicle transport.
; Accepted value: Registered passive ID list

Modifier.TransportPassives.Stack=(boolean)
; Each contributing passenger provides one source-owned layer when enabled. Requires Modifier.TransportPassives.
; Accepted value: yes / no, default no

```

To display the current stack count on a Rhino Tank, add the following fields to its registered stacking passive. A tank with five stacks shows its native name on the first line and `Range +5` on the second.

```ini
[RhinoRange]
Trigger=WhileState
Condition.Formula=$(This.Stack > 0)
Modifier.TooltipText=Range +$(This.Stack)
```

Each active passive can add a separate line. Literal text and multiple formulas can share a line, for example `Modifier.TooltipText=HP $(Self.Health) / $(Self.MaxHealth)`. The field controls display only. Configure weapon range separately. The complete tooltip fits up to 255 UTF-16 code units including the name and line breaks.

Continuous multipliers from different active passives multiply. Permanent event changes are a separate base: accumulated firepower 120% and continuous multiplier 1.5 produce 180%; leaving the state returns to 120%.

Permanent event bases are bounded by `[Starfall.PassiveLimits]`. Each upper/lower limit is a positive integer percentage, optionally suffixed with `%`; lower must not exceed upper. These limits bound accumulated event values, not the final product with continuous multipliers.

```ini
[Starfall.PassiveLimits]
Speed=(positive integer percentage)
; Sets the upper limit for Speed.
; Default: 300%

Speed.Min=(positive integer percentage)
; Sets the lower limit for Speed.
; Default: 1%

Firepower=(positive integer percentage)
; Sets the upper limit for Firepower.
; Default: 1000%

Firepower.Min=(positive integer percentage)
; Sets the lower limit for Firepower.
; Default: 1%

Armor=(positive integer percentage)
; Sets the upper limit for Armor.
; Default: 1000%

Armor.Min=(positive integer percentage)
; Sets the lower limit for Armor.
; Default: 1%

Scale=(positive integer percentage)
; Sets the upper limit for Scale.
; Default: 300%

Scale.Min=(positive integer percentage)
; Sets the lower limit for Scale.
; Default: 25%

ProjectileScale=(positive integer percentage)
; Sets the upper limit for ProjectileScale.
; Default: 300%

ProjectileScale.Min=(positive integer percentage)
; Sets the lower limit for ProjectileScale.
; Default: 1%

WeaponRange=(positive integer percentage)
; Sets the upper limit for WeaponRange.
; Default: 300%

WeaponRange.Min=(positive integer percentage)
; Sets the lower limit for WeaponRange.
; Default: 1%

FireRate=(positive integer percentage)
; Sets the upper limit for FireRate.
; Default: 300%

FireRate.Min=(positive integer percentage)
; Sets the lower limit for FireRate.
; Default: 1%

```

For example, `[Starfall.PassiveLimits] Firepower=200%` caps accumulated event firepower at twice its base. Invalid limits are logged and replaced by defaults, with the default minimum reduced if necessary to fit the maximum.

Weapon-slot disabling takes precedence over replacement. Multiple active replacements use the last in processing order; do not use registry numbering as an assumed ordering API. Native rank/deploy weapon changes are still resolved. `FireWeapon` uses its explicitly named weapon instead of the slot replacement.

Ivan bombs snapshot the planter’s firepower and projectile scale at planting, applying the scale to the native explosion radius. Native rocket-locomotor impacts (including V3 missiles) also honor the missile’s firepower and projectile scale; their impact animations scale with the blast. The missile’s own VisualScale controls its body. Shared warhead radii are restored after each synchronous impact.

Projectile scale is captured at launch; later state changes do not resize an already launched shot. Scaling an original `CellSpread=0` does not create an area. Each multishot branch keeps its own eligible enemy while the primary target, weapon and firing slot stay the same. Vacant branches acquire the nearest remaining eligible enemies on the next volley. Saving and loading preserves these assignments. Each enemy receives at most one extra projectile. Contributions add, fractional counts round down, and negative formula results contribute zero. Extra shots do not consume additional ammo or recursively emit `OnFire`. Electric bolts, lasers, radiation beams and directed particles are drawn separately for each target. Sonic weapons retain their native primary wave. Each extra hit sends three curved refraction fronts toward the target position recorded at launch. Weapons with `IsMagBeam=yes` fire only at their primary target, regardless of the multishot count. Native spawner, disk laser and `LimboLaunch` weapons keep their own firing paths.

Starfall EMP is separate from Ares timers and native `IsUnderEMP`. Multiple Starfall sources must all expire before it releases. Native iron curtain/force shield presentation takes precedence over tint; ordinary immunity does not promise to block forced damage or every third-party effect.

`Modifier.Cloakable=yes` grants the ability to cloak, not immediate or undetectable invisibility. Native cloak delay, `CloakStop`, firing, detection, and object-specific restrictions still apply. Multiple active sources combine with OR; losing one does not cancel another. When all Starfall sources end, the engine reevaluates cloaking using the remaining native/extension capabilities and normally decloaks if none permits it. The modifier never overwrites the unit's native `Cloakable` flag or its shared type, so innate and crate-granted capability is preserved. `no` contributes no capability; it does not force decloak or disable another source. Off-map/transported holders suspend this modifier. Active capability and state-wait progress survive save/load.

**Fragment**: after remaining stationary for two game seconds, a unit gains cloaking capability. Register and bind `SF_StationaryCloak` to the intended unit types. Movement breaks this condition and resets its wait; re-entry still follows native cloaking rules. Avoid using `Condition.SelfCloak=Uncloaked` as the sole condition unless an oscillating grant/withdraw cycle is intended.

```ini
[SF_StationaryCloak]
Trigger=WhileState
Condition.SelfMoving=no
Condition.StateDurationSeconds=2
Modifier.Cloakable=yes
```

Register this passive in `[Starfall.PassiveTypes]` and add `SF_StationaryCloak` to the tank’s `Starfall.Passives` list.

### Projectile shield

Register the passive and bind it to the guard type. Merge both lists with any existing entries.

```ini
[Starfall.PassiveTypes]
0=SF_GuardShield

[HTNK]
Starfall.Passives=SF_GuardShield

[SF_GuardShield]
Trigger=WhileState
Modifier.ProjectileShield.Radius=3
Modifier.ProjectileShield.BlockInviso=yes
Modifier.ProjectileShield.DamageMultiplier=0.3
```

The active sphere follows its holder in three dimensions. A hostile projectile crossing the surface from outside hits the first shield on its flight path. Shots leaving the sphere and allied shots pass through. Enabling `BlockInviso` also checks the firing line of instant projectiles, including ordinary lasers, electric bolts and radiation beams using an Inviso projectile. Their visual ends at the shield surface, and the holder takes the configured damage. Disk lasers, railguns, magnetic beams, temporal beams and sonic weapons keep their usual behavior.

With the settings above a 100-damage hit supplies 30 base damage to the guard, then the guard's own armor applies. The original target avoids the direct hit. The warhead can still inflict scaled splash damage near the point of contact. The guard takes its direct damage once. A multiplier of `0` plays the original warhead animation at the point of contact and absorbs all direct damage, splash and warhead effects. If the warhead has no applicable animation, the shot disappears. At `1` the guard receives the full base hit. Coincident shields from different holders use the lower engine object ID. Coincident shields on the same holder use the lowest multiplier.

### Aura fields

```ini
[PassiveID]
Modifier.Aura.Passives=(comma-separated passive IDs)
; Registered non-aura passive IDs.
; Default: required

Modifier.Aura.Range=(number)
; Integer 1..32 cells or -1 for whole map.
; Default: required

Modifier.Aura.Houses=(list)
; Nonempty list: Self,Allies,Enemies,Neutral,All.
; Default: Self

Modifier.Aura.TargetTypes=(comma-separated type or category list)
; Concrete IDs, *, @Infantry, @Vehicle, @Aircraft, @Building; mixed lists allowed.
; Default: *

Modifier.Aura.IncludeSelf=(boolean)
; yes / no; holder must also pass the filters.
; Default: no

```

Aura coverage uses horizontal center distance, includes the boundary, and refreshes every 15 frames. Loss of the source or its condition withdraws that source. An aura cannot directly grant a passive that itself defines an aura. A transport-support passive cannot directly grant another transport-support passive.

Passenger support is evaluated for passengers even while they are transported. It cannot use spatial nearby conditions or target/distance-dependent conditions. Normal on-map continuous effects are suspended off-map, but supported passenger combat evaluation can retain firepower, weapon range, fire rate, and multishot for relevant firing paths; spatial/distance restrictions still apply. Do not assume every off-map modifier remains active. The direct transport relationship matters, not arbitrary nesting.

A linked child runs its own trigger. Removing or stealing an independently owned copy leaves the linked copy active. The final source disappearing ends ownership. Overlapping links preserve counters, cooldowns and acquisition history. Missing IDs and cycles disable the affected parents.

```ini
[SF_CombatPackage]
Trigger=WhileState
Condition.Formula=$(1)
Modifier.LinkedPassives=SF_KillHeal,SF_FieldArmor
```

### Sources, stacks, and removal

Sources include unit bindings, map bindings, research, temporary/permanent grants, auras, spawn parents, and passenger support. One unit executes one copy of a given passive ID even when sources overlap.

- First acquisition gives one layer. `This.Stack` includes that layer, and `Stack.Max` caps the total. A stackable passenger or repeatable research grant can supply the first layer.
- `ChangePassiveStack` changes the manual additions to a held passive. A reduction that reaches or passes zero removes an ordinary passive, including its initial layer. Layers supplied by passengers or research are protected, and an active parent keeps its linked child.
- `StackOnReapply=yes` adds one layer only if the recipient already holds the passive. First acquisition gives one layer.
- Temporary grants merge durations by keeping the longer remaining duration; permanent wins. Expiry removes that source, not independent sources.
- Losing the last source clears the relevant progress; reacquisition starts fresh. A completed permanent action, such as extra maximum health, is not undone when the granting passive expires.
- `RemovePassive` removes/suppresses the current held passive and clears its stacks/counting/wait state. It is not a global deletion of the registered definition; later valid grants can make it available again.
- `StealPassive` transfers manual layers from a live donor; passenger-support layers are not transferable inventory. Stealing the last transferable layer of an ordinary passive removes it from the donor. The first layer acquired by the receiver counts toward the transfer.
- After `ConsumePassenger`, stealing uses the consumed passenger's recorded layers and consumes that record, preventing repeated inheritance of the same layers.

## Action reference

### Action catalog

Each action below is an alternative for an ordinary INI action section. The comments list its fields and behavior. Context restrictions in the trigger, active-skill, Lua, and superweapon sections take precedence.

```ini
[PassiveID or ActionSectionID]
Action=Heal
; Action.Amount: positive integer or formula, e.g. $(Event.Damage * 25%) on a damage trigger.
; Heal up to current maximum health.

Action=Damage
; Same amount choice as Heal; optional Action.Warhead.
; Damage one selected unit; specified warhead supplies armor verses and death behavior.

Action=SetDamage
; Action.Amount: nonnegative integer or formula.
; Replace current pending health loss; damage interception only.

Action=BlockDamage
; No amount field.
; Set pending loss to zero; damage interception only.

Action=ApplyIronCurtain
; Action.Duration: positive frames.
; Apply native iron curtain, preserving a longer existing duration; existing force shield takes precedence.

Action=GiveCredits
; Same amount choice as Heal.
; Action.ShowText=yes/no (default yes). The credited player sees green +$50 above the recipient.
; Add money to recipient's owner, or the explicit owner context in a sequence.

Action=GiveResearchPoints
; Same amount choice as Heal.
; Action.ShowText=yes/no (default yes). Light-blue + 50 RP shows the actual increase.
; Add research points; respects the research enable switch.

Action=GrantFreeResearch
; Optional Action.ResearchOptions (comma-separated project IDs) and Action.Rarity=All.
; Give the recipient’s owner one free choice through the research button. See free research choices.

Action=GrantResearch
; Action.Research=<one project ID>
; Immediately give the named project to the target owner, free of charge. In a map or sequence action use Target=Owner. Checks ownership, availability, country and prerequisites.

Action=GrantRandomResearch
; Optional Action.Rarity=All (All,Common,Rare,Epic,Insane); no amount field.
; Immediately acquire one free random project for the recipient's owner. See random research rewards below.

Action=ChangeMaxHealth
; Nonzero signed integer Action.Amount or Action.MaxHealthPercent=1%..10000%.
; Permanently change maximum health; percentage only increases it.

Action=ChangeAmmo
; Nonzero signed integer Action.Amount.
; Change finite ammo, clamped to magazine bounds.

Action=ChangeSpeed
; Nonzero signed integer percentage points or formula in Action.Percent.
; Permanently change speed base.

Action=ChangeFirepower
; Same Action.Percent contract.
; Permanently change firepower base.

Action=ChangeArmor
; Same Action.Percent contract.
; Permanently change armor-strength base.

Action=ChangeWeaponRange
; Same Action.Percent contract.
; Permanently change standard weapon range base.

Action=ChangeFireRate
; Same Action.Percent contract.
; Permanently change standard fire-rate base.

Action=ChangeScale
; Nonzero signed integer percentage points in Action.Percent; no formula.
; Permanently change mobile-unit drawing size.

Action=ChangeProjectileScale
; Nonzero signed integer percentage points in Action.Percent; no formula.
; Change future projectile/explosion/spread scale.

Action=SpawnUnit
; Action.UnitType or Action.UnitTypes; optional Action.Weights, Action.RandomCount, and Action.Owner=Self.
; Spawn infantry, vehicle, or aircraft, searching up to three rings if necessary; no buildings. Random candidates use matching nonnegative weights (equal by default, not all zero); RandomCount defaults to 1 and supports 1–256 independent draws with replacement.

Action=RemoveAttachedUnit
; Optional Action.Target, Action.Slot, Action.UnitType, Action.Count.
; Silently remove matching children of the selected parent. See Removing attached units.

Action=AttachUnit
; Action.UnitType, placement and inheritance fields below.
; Create and mount one child on a live unit or building. See Attached units.

Action=SpawnBuilding
; Action.BuildingType; optional Action.Owner=Self, Action.SearchRadius=3.
; Create one completed standalone building at the target cell or the nearest valid empty foundation within the search radius.

Action=Transform
; Action.UnitType (also used for building IDs).
; INI event sequences: Action.LandFirst=yes lets a Jumpjet (including BalloonHover) land before converting to Drive.
; Searches up to 3 cells, times out after 450 frames, and resumes remaining results only on success. Default no.
; With the public Phobos conversion API, Drive and landable Jumpjet vehicles (BalloonHover=no) can switch in either direction while stationary on passable ground, off bridges.
; Convert a living object within its class. Buildings require exactly matching occupied foundation cells; see conversion restrictions.

Action=ChangeOwner
; Optional Action.Owner=Self.
; Transfer a living unit; skip if already owned by that house or under mind control.

Action=SpawnPickup
; Required Action.PickupType, an ID registered in [Starfall.PickupTypes].
; Place a non-blocking ground pickup at the selected cell, searching outward up to 3 cells if necessary.

Action=RemovePickup
; Optional Action.Range=0 and Action.PickupTypes=* (comma-separated registered IDs or * alone).
; Remove matching ground pickups without granting their rewards. 0 selects the exact cell, positive integers select an inclusive circular radius in cells, and -1 selects the whole map without a center. Native crates are unaffected.

Action=SpawnCrate
; Optional Action.CrateType=Random.
; Place a crate near the target position.

Action=SelfDestruct
; Action.Target=Self only.
; Destroy the holder through the supported normal death path.

Action=ConsumeTarget
; Defaults to Action.Target=Target. Accepts a live map unit or building, including an explicit Lua handle.
; Remove the selected object directly. It produces no damage, death, kill or passenger-consumed event.

Action=ConsumePassenger
; No Action.Amount.
; Consume first direct passenger and its nested cargo; emit one direct-passenger event.

Action=GrantPassive
; Action.Passive; optional Action.Duration=-1, Action.StackOnReapply=no.
; Give one registered passive, permanently or for positive frame duration.

Action=StealPassive
; Action.Passive list; optional positive integer Action.Amount=1.
; Transfer at most that many layers per ID from recipient/donor to holder.

Action=RemovePassive
; Action.Passive list.
; Remove matching held IDs; continue past absent IDs.

Action=ChangePassiveStack
; Action.Passive, nonzero signed integer Action.Amount.
; Change manual layers of an already held passive.

Action=ResetStateTimer
; Action.Passive.
; Restart an existing timed WhileState wait; no effect if not held/timed/started.

Action=Promote
; Optional Action.Levels=1, allowed 1 or 2.
; Promote a trainable unit, capped at elite.

Action=RunScript
; Action.Script, Action.Function.
; Invoke a discovered Lua module callback.

Action=SendSignal
; Action.Signal plus optional signal fields below.
; Queue a signal to qualifying receivers.

Action=ReplaceWeapon
; Action.Weapon, target Self.
; Replace one volley; OnBeforeFire only.

Action=FireWeapon
; Action.Weapon.
; Emit the specified weapon toward a unit or position.

Action=DetonateWarhead
; Action.Warhead; optional nonnegative integer Action.Amount=0.
; Detonate at the target position; amount is base damage, not a formula here.

Action=PlayAnim
; Action.Anim.
; Play a registered animation once; a live-unit attachment follows that unit.

Action=PlaySound
; Action.Sound.
; Native sound ID or wav: followed by a PCM WAV path.

Action=ShakeScreen
; Action.ShakeX=0, Action.ShakeY=4, Action.ShakeLocal=yes.
; Native screen shake; axes 0..32, at least one nonzero.

Action=SpawnParticleSystem
; Action.ParticleSystem.
; Create an existing particle-system type at or toward the target.

Action=ForceMove
; Action.ForceMove.Duration plus Action.ForceMove.Mode or Action.ForceMove.Offset. See all 12 fields and example.
; Schedule forced ground-unit movement/lift.

Action=ChronoTeleport
; Action.Destination=X,Y or Target. Optional Action.SearchRadius=3, Action.Instant=no.
; Teleport a supported on-map ground mobile unit to an available cell.

```

For signed integer amounts, `+10` and `-10` are valid; zero is invalid where the table says nonzero. Percent-of-damage amounts round down. Fixed event percentage changes accumulate in percentage points: two `+10%` actions produce 120% from a 100% base, not 121%.

Maximum-health increases also add the increase to current health; decreases only trim health that exceeds the new maximum. Percentage growth uses the pre-action maximum. Native iron curtain can kill infantry/organic targets according to the target ruleset; it is not a universal harmless invulnerability buff.

`Damage` uses the `Super` warhead when `Action.Warhead` is omitted. It damages only the selected unit. Warhead `AffectsAllies`, `AffectsOwner` (fallback to `AffectsAllies`), `AffectsEnemies` (default yes), and `AffectsNeutral` constrain ownership relationships. Sequence damage uses the launching owner even without a unit caster.

`Action.Owner` accepts `Self,Attacker,Victim,Neutral,Player1,Player2,Player3,Player4,Player5,Player6,Player7,Player8`. Player numbers refer to starting positions. The `Owner` target/owner alias belongs to sequence contexts, not arbitrary ordinary passives.

### Changing a research counter

```ini
[SpendTen]
Trigger=OnDeploy
Action=ChangeResearchCounter
Action.Counter=AntiArmor.Vehicle_Kills
Action.Amount=-10
```

The event owner's project must declare the counter in `Counters`. Use a nonzero signed integer or a numeric formula for the delta. Formula deltas truncate toward zero. The resulting total is clamped to 0–2147483647. See [shared research counters](research.md#shared-research-counters) for the full project.

### Custom ground pickups

See [custom ground pickups](miscellaneous.md#custom-ground-pickups).

### Attached units

`AttachUnit` creates one real infantry or vehicle as a child on a live infantry unit, vehicle, aircraft or building on the map. Each successful activation installs one child. Attachments cannot host further children. Use the same installer for different parent types.

```ini
[Starfall.PassiveTypes]
0=InstallTurret

[InstallTurret]
Trigger=OnPassiveGained
Action=AttachUnit
Action.UnitType=MTNK
Action.Slot=Auto
Action.ExcludeParentPassives=Overdrive

[BFRT]
AttachSlots=Left,Right
AttachNode.Left.FLH=24,96,120
AttachNode.Right.FLH=24,-96,120

[CARRIER]
AttachSlots=BowLeft,BowRight,SternLeft,SternRight
AttachNode.BowLeft.FLH=160,96,120
AttachNode.BowRight.FLH=160,-96,120
AttachNode.SternLeft.FLH=-160,96,120
AttachNode.SternRight.FLH=-160,-96,120
```

```ini
[MountActionID]
Action.UnitType=(text or ID)
; Required infantry or vehicle type ID.

Action.Target=(target selector)
; Parent unit or building. Defaults to Self. Lua may supply a live unit handle.

Action.Slot=(text or ID)
; Auto claims the first available entry in the parent's AttachSlots. Another name reserves that single slot at the free placement. Omit for unlimited unnamed free placements.

Action.Node=(node name)
; Claim this named node directly. Mutually exclusive with Action.Slot. A missing node, occupied slot or full automatic layout returns false.

Action.Anchor=(text or ID)
; Free placement uses Body (default) or Turret. Named nodes use AttachNode.<name>.Anchor on the parent type.

Action.FLH=(forward,left,height integer offsets)
; Signed forward, left and height offsets, default 0,0,0. One horizontal cell equals 256 leptons. Each component accepts −8192 through 8192. Added to the named node's FLH when present.

Action.TransportFire=(FollowTransport or Disabled)
; FollowTransport (default) permits fire from an open-topped transport carrying the parent. Disabled suppresses mounted fire during transport.

Action.ExcludeParentPassives=(list)
; Comma-separated parent passive IDs to omit from ordinary state inheritance. Defaults to empty.

```

For example, add the following section to give an Allied Power Plant one fixed roof mount when it acquires the installer. Building FLH starts at the building's native center. A body anchor faces map north, and a turret anchor rotates with the building's native turret. Buildings without a turret use the body orientation for either anchor. Capturing the building transfers its children. A same-foundation Starfall transformation preserves installed mounts and their offsets. Selling or destroying the building removes its children.

```ini
[GAPOWR]
Starfall.Passives=InstallTurret
AttachSlots=Roof
AttachNode.Roof.FLH=0,0,128
```

The sample installs one child when the passive is first gained. To install two on acquisition, move the `Action` and `Action.*` fields into `[InstallOne]` and set `Actions=InstallOne,InstallOne` in `[InstallTurret]`.

Each node uses `AttachNode.<name>.FLH` and optionally `AttachNode.<name>.Anchor`. An entry in `AttachSlots` with no node fields defaults to the body origin. For a free turret-relative placement, omit `Action.Node` and `Action.Slot` and set `Action.Anchor=Turret`, `Action.FLH=100,0,64`.

The child follows its parent's position, rotation and ownership. It aims independently and prioritizes a reachable, legal target from a player attack order to its parent (or the open-topped carrier). A transported parent and its attachments use only the parent's normal seat cost. The child hides with its cloaked or disguised parent. Destroying one child frees its slot. Removing the parent removes its attachments.

The child keeps its native type passives. Only its direct parent can grant, remove or change its passives and stacks. Parent state inheritance shares eligible firepower, armor, fire rate, range, multishot, visual and projectile scale, tint, weapon-slot disabling, EMP and berserk effects. Conditions and formulas use the parent as the source. The same passive ID is evaluated once. Movement speed, power output, weapon replacement, attached animations and passive distribution stay with their original holder. Every attachment resists mind control. Damage immunity applies while either the parent or the child is immune, regardless of the exclusion list.

### Removing attached units

`Action=RemoveAttachedUnit` selects the **parent** with `Action.Target` (default `Self`). It supports buildings and transported parents. Each successful removal releases the slot immediately, ignores the child's damage immunity and bypasses damage, death, kill and passenger-consumption events. The parent retains its own seat when transported.

```ini
[DismountActionID]
Action.Slot=(text or ID)
; Optional actual slot name, including the node or slot chosen during installation. Omit for unnamed free mounts or to search any slot. Auto is only valid during installation.

Action.UnitType=(text or ID)
; Optional infantry or vehicle type ID of the installed child. When combined with a slot, both must match.

Action.Count=(number)
; Default 1. A positive integer removes up to that many matches, newest installation first. All removes every match. Save/load preserves the order.

```

With no filters the action removes the most recently installed child. With only `Action.Count=All` it clears the parent. Removing at least one child succeeds, including when fewer than the requested count exist. Zero removals returns false and stops the current ordered INI `Actions` or Lua `ctx:actions` list. Previously completed actions stay applied.

The following acquisition replaces an MTNK on the named roof slot with an E1. An empty slot or a different installed type stops the upgrade. If the new unit cannot be installed after successful removal, the slot stays empty.

```ini
[UpgradeRoof]
Trigger=OnPassiveGained
Actions=RemoveRoofGun,InstallRoofGun

[RemoveRoofGun]
Action=RemoveAttachedUnit
Action.Slot=Roof
Action.UnitType=MTNK

[InstallRoofGun]
Action=AttachUnit
Action.Node=Roof
Action.UnitType=E1
```

### Building creation

`SpawnBuilding` uses the target cell as the foundation's top-left anchor (not the building's center). `Action.Target` defaults to `Self`; it also accepts the usual positional recipients, including `VictimLocation`. Lua can pass `Target={x=60,y=62}` or `Target=ctx.point`. `Action.BuildingType` must name an existing standalone building type. Upgrade attachments (`PowersUpBuilding`), overlay walls (`ToOverlay`), and pavement (`ToTile`) are rejected; `Action.UnitType` and `Action.Destination` do not apply.

`Action.SearchRadius` is an integer `0..32`, default `3`, measured in map cells. Zero tries only the requested anchor. Search is circular, ordered by squared distance from the anchor, then increasing Y and X for equal distances. Every candidate must fit the complete native foundation, including its bib, inside the playable map. Occupants, bridges, slopes, unequal cell heights, and terrain rejected by the native building placement check make a site unusable. Existing units and buildings are preserved. A completed building is placed immediately with normal owner tracking and power behavior; this is a free scripted grant rather than a purchase from the construction sidebar.

If no candidate works, the result fails (`false` in Lua) without leaving a limbo building. Invalid/defeated/observer owners also fail. Buildings are saved by the native game; pending passive events retain the existing save format. Site selection never depends on local selection, camera, or visibility state.

```ini
[SF_CreatePowerPlant]
Trigger=OnDeploy
Action=SpawnBuilding
Action.BuildingType=GAPOWR
Action.Target=Self
Action.Owner=Self
Action.SearchRadius=5
```

Register the passive in `[Starfall.PassiveTypes]` and grant/bind it to the deploying unit. For a Lua point-targeted active skill:

```lua
local created = ctx:action("SpawnBuilding", {
    BuildingType = "GAPOWR", Target = ctx.point, Owner = "Self", SearchRadius = 5,
})
```

### Building transformation

Use `Action=Transform` and `Action.UnitType=<building ID>` on a living building. The destination must be a different standalone building. Source and destination must have exactly the same occupied-cell offsets, both without and with the bib. Bib settings and extents must also match explicitly because the vanilla engine ignores the foundation API's bib argument. Equal width/height or equal cell count alone is insufficient; ordering of foundation entries is irrelevant. There is no relocation or nearby search during conversion. In the standard game, `GAPOWR` and `NAHAND` have matching 2x2 foundations, while `NAPOWR` has a different 3x2 foundation. Always check the actual art rules used by the mod.

```ini
[SF_BuildingConversion]
Trigger=OnFire
Action=Transform
Action.Target=Self
Action.UnitType=NAHAND
```

Lua uses the same result, e.g. `ctx:action("Transform", {Target=building, UnitType="NAHAND"})`. A rejected conversion returns `false` and preserves the original building. The engine replaces its native object at the same site, handing the original public ID to the new object. Starfall rebinds its live-object references before retiring the previous object. Lua unit handles, `ctx.state`, queued callbacks and their data, granted passives, stacks, cooldowns, timers and controlled Lua animations therefore continue using the same identity. Temporary grants keep their original expiry frame; conversion does not renew their duration. Owner, veterancy, proportional health, extra maximum-health points and native iron-curtain time are retained. Native building placement updates the owner's building counts, power and production availability for the new type. The private save format is unchanged.

Construction/selling, active production, passengers, garrison occupants, upgrades, docking/radio links, mind-control/spawn/slave/temporal controllers, C4, attached map-trigger tags and other linked native states can prevent conversion. Third-party extension-owned per-object effects are outside this preservation contract. Building conversion uses native creation/placement rather than the FootClass-only Phobos conversion API.

### Land before vehicle transformation

Use `Action.LandFirst=yes` on an INI `Transform` action to land an eligible jumpjet vehicle before converting it to a compatible ground vehicle. This option requires the public Phobos conversion interface. The action searches for a suitable landing cell within three cells and waits for the native landing to complete. If no cell is available, the destination becomes blocked, the order changes, or the 450-frame limit expires, the conversion fails.

```ini
[ActionSectionID]
Action=Transform
Action.UnitType=(registered destination vehicle ID)
; Selects the ground vehicle type.

Action.LandFirst=(boolean)
; Lands an eligible jumpjet vehicle before converting it.
; Default: no
```

The destination must be a compatible Drive locomotor vehicle. A pending landing can defer subsequent results in the INI action list. Lua action calls are synchronous and reject `LandFirst`; use the INI result list for this behavior.

### Multiple actions
`RandomActions` selects action sections by weight. `RandomActions.Weights` provides nonnegative integer weights matching the list (equal by default, not all zero); `RandomActions.Count` defaults to 1 and permits 1–256 independent draws with replacement. Failed actions are not retried. Cooldown starts if any action succeeds. `Action`, `Actions`, and `RandomActions` are mutually exclusive.

```ini
[SF_RandomReward]
Trigger=OnKill
RandomActions=SF_Heal,SF_Money
RandomActions.Weights=60,40
RandomActions.Count=2

[SF_Heal]
Action=Heal
Action.Amount=50

[SF_Money]
Action=GiveCredits
Action.Amount=200
```

**Fragment**; register and bind only `SF_KillReward`:

```ini
[SF_KillReward]
Trigger=OnKill
Filter.TargetHouse=Enemies
Actions=SF_HealStep,SF_MoneyStep

[SF_HealStep]
Action=Heal
Action.Target=Self
Action.Amount=50

[SF_MoneyStep]
Action=GiveCredits
Action.Target=Self
Action.Amount=100
```

`Actions` preserves order and duplicate section IDs. The parent cannot also contain `Action` or `Action.*`; child sections contain only one `Action` and its fields, with no nested `Actions`. Empty items, missing sections, unknown action fields, or invalid steps reject the passive. All steps share one count/chance/cooldown decision. One successful step is enough for success; a failed heal does not prevent the money step. No transactional rollback is promised for already executed steps.

For a fusion script, call `ctx:action("ConsumeTarget", {Target=material})` for a selected unit and check its boolean result. Targets must be on the map and outside a transport. A target with passengers, garrison occupants, a bunker link or an attached parasite cannot be consumed. Its handle becomes invalid on success. A failed consumption stops the rest of an ordered INI action list or Lua `ctx:actions` call. Earlier successful actions remain applied.

`ConsumePassenger` has a deliberate exception: failure stops the remaining steps in that INI `Actions` list or Lua `ctx:actions` call. Previous steps are not rolled back. Success replaces the following steps' counterpart snapshot with the consumed passenger, so a live-target action does not accidentally fall back to the original enemy. The direct passenger's recorded layers are shared with its queued consumption event and can only be transferred once. Nested cargo is removed without emitting a separate consumption event for every nested unit. This interface operates on passenger lists, not a building's separate garrison list, and does not synthesize ordinary passenger kill/death passive events.

### Passenger ejection

`Action=EjectPassengers` gives a transport vehicle a point-targeted infantry launcher. Register and bind the skill as in the [Battle Fortress example](../../examples/PassengerEjection/Starfall.ini).

```ini
[SF_PassengerEjection]
Trigger=Active
Active.Target=Point
Active.CastMode=All
Active.Range=8
Cooldown=120
Action=EjectPassengers
Action.Eject.Interval=9
Action.Eject.HorizontalSpeed=1
Action.Eject.OpenHeight=3
```

One order selects the infantry directly aboard at cast time. The first eligible rider launches immediately. Each subsequent rider launches from the vehicle's current position after `Action.Eject.Interval` simulation frames (default 9, range 1–3600). `Action.Eject.HorizontalSpeed` sets horizontal speed in cells per game second (default 1, minimum 1/256). Each rider takes `max(1, ceil(horizontal distance in cells × 15 / speed))` frames from its actual launch position to its assigned opening point. The default takes 60 frames for 4 cells and 120 frames for 8 cells. If a landing cell becomes occupied in flight, the rider diverts at the configured speed to a free cell within four cells on the same surface. If that surface disappears or native parachute placement rejects it, the rider searches outward for a clear opening site. Each rider opens its native parachute above its own destination at `Action.Eject.OpenHeight` cells above the landing surface (default 3, range 1–128). During ejection, the native paradrop shadow appears as a small dot on the ground or bridge deck directly below the rider. Infantry use their art sequence `Crawl` before opening when available. The native paradrop takes over the descent.

For a single unit already on the map, use `Action=EjectUnit` with an infantry or ground-vehicle target and the same destination, range, speed and opening height. See the [Lua action example](lua.md#queries-and-action-calls). and a 120-frame cooldown. Cooldown starts with the first successful launch. Vehicles in the hold and infantry without a legal landing slot stay aboard. Units that board after the command wait for the next cast. If the transport is lost, airborne passengers continue independently. Pending launches, airborne infantry and parachute landing reservations survive saving and loading.

For both ejection actions, use `Action.Destination=X,Y` for a fixed cell or `Action.Destination=Target` for the event target’s cell at execution time. An absent or off-map target makes the action fail. Set the maximum distance with `Action.Eject.Range` (1–128 cells, default 8). Without a destination it uses the event impact point.

### Vehicle jump

Bind this point-targeted active skill to a ground vehicle. Merge the binding with any existing skills on the unit.

```ini
[Starfall.PassiveTypes]
VehicleJump=SF_VehicleJump

[MTNK]
Starfall.Passives=SF_VehicleJump

[SF_VehicleJump]
Trigger=Active
Active.Name=Jump
Active.Target=Point
Active.CastMode=All
Active.Range=6
Cooldown=120
Action=JumpTo
Action.Jump.Height=3
Action.Jump.Duration=15
Action.Jump.TakeoffAnim=SMKPUFF
Action.Jump.TakeoffSound=HornetTakeoff
Action.Jump.LandAnim=DROPLAND
Action.Jump.LandSound=HornetLanding
```

`Active.Range` sets the horizontal limit (default 6 cells). `Action.Jump.Height` is the arc height above the line joining takeoff and landing, in cells (default 3). Both accept integers from 1 to 128. `Action.Jump.Duration` accepts 1 to 3600 simulation frames (default 15). The native active jump starts its cooldown (default 120 frames) only after takeoff succeeds. Takeoff interrupts the vehicle's orders.

The four optional presentation fields work independently. `Action.Jump.TakeoffAnim` and `Action.Jump.TakeoffSound` play once at the actual launch position on successful takeoff. `Action.Jump.LandAnim` and `Action.Jump.LandSound` play once at the final landing position, including after diversion or a mid-flight reload. Omit a field to skip that effect. Use a registered animation ID for animations and a sound ID or `wav:filename.wav` for sounds.

With `Active.CastMode=All`, selected vehicles reserve distinct landing cells within two cells of the click on its connected landing surface. Flight can cross water, cliffs or walls when the arc clears the obstacle. Bridge decks and raised platform decks are eligible landing surfaces. Only the clicked cell permits landing on enemies the vehicle can normally crush. If an ally moves into a reserved cell during the flight, the jumper looks for another landing cell. Saving in flight preserves the reservation.

Use `Action.Destination=X,Y` for a fixed cell or `Action.Destination=Target` to jump toward the event target’s cell at execution time. An absent or off-map target makes the jump fail. Set the maximum distance with `Action.Jump.Range` (default 6). With no explicit destination the action needs an event impact point. For Lua see [action arguments](lua.md#active-skills).

### ForceMove fields

All INI fields in this table have the full `Action.ForceMove.` prefix. They are not native unit locomotor settings.

```ini
[ForceMoveActionID]
Action.ForceMove.Offset=(dx,dy decimal cells)
; dx,dy, each finite -512..512 cells; supports fractions. Mutually exclusive with Mode, Center, and Distance.
; Default: none

Action.ForceMove.Mode=(Push, Pull, or Launch)
; Push, Pull, Launch. Do not write Mode=Offset; supply Offset instead.
; Default: required if Offset omitted

Action.ForceMove.Center=(Self or Impact)
; Self or Impact. Only for non-Offset modes.
; Default: Self

Action.ForceMove.Distance=(decimal cells)
; Finite 0..512 cells. Push/Pull require a positive effective distance; Launch can omit it.
; Default: 0

Action.ForceMove.Height=(decimal cells)
; Finite 0..512 cells of lift; also raises the targeting position.
; Default: 1 for Launch, otherwise 0

Action.ForceMove.AirborneHeight=(decimal cells)
; Finite 0..512 cells. At or above this current lift, the unit is an air target; below it, a ground target. 0 disables air classification. Supports fractions.
; Default: 1

Action.ForceMove.Duration=(number)
; Positive integer simulation frames.
; Default: required

Action.ForceMove.Easing=(easing mode)
; Linear,EaseIn,EaseOut,EaseInOut; affects horizontal progress.
; Default: Linear

Action.ForceMove.Tilt=(boolean)
; yes / no; supported voxel vehicles lean and settle afterward.
; Default: yes

Action.ForceMove.IgnoreCollision=(boolean)
; Pass through obstacles and resolve landing, or stop before blocking obstacles when no.
; Default: yes

Action.ForceMove.BlowWeight=(decimal weight)
; Finite threshold; target native Weight must be **strictly less**. Omitted means no weight threshold.
; Default: omitted

Action.ForceMove.AffectsIronCurtain=(boolean)
; Allow true iron-curtained targets if yes; force shield is not classified as true iron curtain by this check.
; Default: no

```

At least one effective horizontal offset/distance or height must be nonzero. Coordinates are rounded to engine sub-cell units. Pull does not overshoot the chosen center. Targets must be supported living, on-map ground infantry/vehicles; carried, naturally flying, deploying, sinking, warping, and other incompatible locomotor states are rejected. Units already airborne through ForceMove can receive a replacement ForceMove.

During movement, ordinary movement, firing, deployment, and active skills are blocked, but damage can still occur. A new ForceMove replaces the current movement from its current location/lift. With `IgnoreCollision=yes`, landing can crush ground units regardless of owner; a building receives base impact damage based on the falling unit's current health and the falling unit dies; impassable landing terrain can also kill it. With `no`, horizontal travel stops at an obstacle, lift completes, and landing collision damage is not applied. Both modes respect map boundaries.

Horizontal motion updates the real position, cell chain, native threat-region registration, sensors and neighbouring-cell counts as cells change. Target coordinates follow the current horizontal position and lift. At or above `AirborneHeight`, the unit joins native aerial scans and uses normal anti-air weapon eligibility and aerial damage calculations. Descending below the threshold restores ground targeting immediately. The threshold is measured above the current terrain/bridge deck, not absolute map elevation. Unit type and locomotor remain unchanged. These tracking updates do not execute ordinary cell-entry actions during transit; landing rules are unchanged.

A replacement ForceMove preserves current lift and applies its own threshold immediately. Saves preserve the configured threshold and rebuild aerial tracking without duplicates. Motions loaded from older saves retain ground classification until replaced or finished; subsequent ForceMove actions use the new default.

**Fragment**:

```ini
[SF_Knockback]
Trigger=OnDealDamage
Action=ForceMove
Action.Target=Target
Action.ForceMove.Mode=Push
Action.ForceMove.Center=Self
Action.ForceMove.Distance=3
Action.ForceMove.Height=1
Action.ForceMove.AirborneHeight=1
Action.ForceMove.Duration=12
Action.ForceMove.Easing=EaseOut
Action.ForceMove.IgnoreCollision=no
Action.ForceMove.BlowWeight=5
```

`ChronoTeleport` is a separate action. `Action.Target` selects the unit to teleport. Set `Action.Destination=X,Y` for a fixed cell (integer coordinates 0–511), or `Action.Destination=Target` for the event target’s cell at execution time. For example, with `OnTakeDamage` this is the attacker. An absent or off-map target makes the action fail. `Action.SearchRadius` is an integer `0..32`, default 3. It tries the center then the nearest valid cell in the circular search, avoiding reserved teleport destinations. Zero radius only tries the exact cell. Failure leaves the unit in place.

`Action.Instant=yes` uses the same landing validation but relocates immediately, without native chronosphere freezing or warp-weapon triggers. It clears current movement/orders, updates cell occupancy, threat/sensor registration, and resumes Guard with the same unit identity, health and effects. Incompatible borrowed locomotion and parasite victims are rejected. Default `no` preserves native `ChronoWarpTo` behavior. Lua uses `Instant=true`. Do not relocate inside synchronous damage interception; schedule it for a later frame.

### Signals

```ini
[SignalPassiveID]
Action.Signal=(text or ID)
; Nonempty case-insensitive ID, at most 127 bytes; no registry needed.
; Default: required

Action.Target=(target selector)
; Receiver or broadcast center unit.
; Default: Self

Action.Signal.Range=(number)
; Integer: 0 direct to center, 1..128 local broadcast, -1 whole map.
; Default: 0

Action.Signal.Houses=(list)
; List: Self,Allies,Enemies,Neutral,All, relative to sender.
; Default: Self

Action.Signal.IncludeSelf=(boolean)
; Include sender in broadcast; direct self-send is independent of this flag.
; Default: no

Action.Signal.Target=(unit selector)
; Attached unit: Self,Target,Attacker,Victim; also Sender inside OnSignal.
; Default: none

Action.Signal.Value=(formula)
; Finite number or formula; computed when sending.
; Default: 0

Signal=(text or ID)
; ID to match.
; Default: required for OnSignal

Filter.SenderHouse=(house selector list)
; Sender house filter, relative to receiver; same enum as Filter.TargetHouse.
; Default: All

Filter.SenderKind=(unit kind list)
; Sender kind filter; same enum as Filter.TargetKind.
; Default: All

```

On receipt, `Self` is receiver, `Target` is attached unit, and `Sender` is sender. `Filter.Target*` tests the attached unit, not sender. `Action.Target=Sender` requires that sender to remain valid for live-unit actions.

Receivers must be living on-map units; transported/garrisoned units are not separate broadcast recipients. Delivery is queued. Sender/target snapshots and value remain readable after objects disappear, but live-target actions can fail. A signal chain is limited to 16 levels and 1,024 deliveries; excess is stopped and logged. Chain progress and queued signals are saved.

## Numeric formulas

Formulas are a small expression language, not Lua. Wrap the complete expression in `$(...)`.

| Supported location | Interpretation |
| --- | --- |
| `Condition.Formula` | Nonzero true, zero false. |
| `Modifier.SpeedMultiplier`, `Modifier.FirepowerMultiplier`, `Modifier.ArmorMultiplier`, `Modifier.WeaponRangeMultiplier`, `Modifier.FireRateMultiplier`, `Modifier.PowerMultiplier`, `Modifier.ExperienceMultiplier` | Multiplier result within the field's allowed range. |
| `Action.Percent` for speed/firepower/armor/range/fire rate | Fraction converted to percentage-point change: `$(10%)` adds 10 points. No formula for Scale/ProjectileScale. |
| `Action.Amount` for Heal/Damage/GiveCredits/GiveResearchPoints/SetDamage | Nonnegative finite amount, floored to integer; range overflow or negative result fails. |
| `MultiShot` / `Modifier.MultiShot` | Extra-target count, floored; negative contributes zero. |
| `Action.Signal.Value` | Finite numeric payload. |

Operators: unary `+ -`, arithmetic `+ - * /`, parentheses, comparisons `> >= < <= == !=`. Functions: `min(a,b)`, `max(a,b)`, `clamp(x,low,high)`, `floor(x)` (round toward negative infinity), and `if(condition,thenValue,elseValue)`. Names are case-insensitive. Decimal literals and percentage literals are supported; `1.5%` is `0.015`.

Comparisons bind less tightly than arithmetic and return 0 or 1. Chained comparisons such as `1 < x < 5` are invalid. Combine comparisons explicitly, for example `$( (Self.Ammo >= 1) * (Self.Ammo <= 3) )`. There are no `&&`, `||`, Lua `and/or`, arbitrary functions, or arbitrary unit properties.

`if` evaluates only the selected branch. However, context restrictions still apply to every referenced variable at configuration validation time. A forbidden `Event.Damage` reference is not made valid by putting it in an unselected branch.

| Variable | Value and scope |
| --- | --- |
| `Self.Health`, `Self.MaxHealth` | Holder health and extended current maximum. |
| `Research.ProjectID.CounterName` | Current value for the state holder’s house or the event owner captured at trigger time. |
| `Self.Cost` | Holder type's base Cost, before country/factory/research discounts. |
| `Self.Ammo` | Ammo count; unlimited ammo is `-1`. |
| `Self.Passengers` | Direct passenger count. |
| `Self.Rank` | Current rank: 0 rookie, 1 veteran, 2 elite. **WhileState only.** |
| `Self.GattlingStage`, `Self.MaxGattlingStage` | **WhileState only.** Current weapon stage (1 through `WeaponStages`) and stage count. An uncharged Gattling starts at 1. Both values are 0 for units without configured Gattling stages. Reevaluated at each state tick, including while idle. |
| `Owner.PowerOutput`, `Owner.PowerDrain`, `Owner.PowerSurplus` | Current owning house's total supply, total drain, and supply minus drain (may be negative). **WhileState only.** Recomputed with continuous states after native power-grid updates; no local-player data or RNG. |
| `Target.Health`, `Target.MaxHealth`, `Target.Cost` | Counterpart snapshot/current target values, not Action.Target's values. A real unit is required. |
| `This.Stack` | Executing passive's stack snapshot/current effective count. |
| `Event.Damage` | Actual lost health for OnDealDamage/OnTakeDamage; current pending loss for the two before-damage events. Restricted to those damage triggers. |
| `Event.Distance` | Horizontal distance in cells from holder to counterpart/target point; ignores height. |
| `Sender.Health`, `Sender.MaxHealth`, `Sender.Cost` | Signal sender snapshot; OnSignal only. |
| `Event.SignalValue` | Captured signal payload; OnSignal only. |

Missing variables, division by zero, non-finite results, reversed clamp bounds, or a result outside the destination field's range fail the affected runtime calculation. Missing data in older saves is not fabricated. Invalid syntax disables the passive. The parser limits a formula to 512 bytes, 96 compiled instructions, and nesting depth 24.

Examples:

```ini
; Separate fragments: merge a line only into a compatible definition.
Condition.Formula=$(Self.Health <= Self.MaxHealth * 50%)
Modifier.FirepowerMultiplier=$(1 + This.Stack * 10%)
Modifier.FireRateMultiplier=$(1 + clamp(floor(Owner.PowerSurplus / 500), 0, 5) * 10%)
Action.Amount=$(min(Event.Damage * 25%, 100))
Action.Percent=$(5%)
```

The amount example requires a supported damage event. A constant formula does not require a valid target merely because other expressions can reference one.

### Warhead grants

```ini
[WarheadID]
Starfall.GrantPassives=(comma-separated passive IDs)
; Grants registered passives on eligible hits. One invalid ID disables the rule.
; Default: none

Starfall.GrantPassives.Duration=(integer frames)
; Sets the duration of each grant. Positive values are temporary.
; Default: -1, permanent

Starfall.GrantPassives.StackOnReapply=(boolean)
; Adds one manual layer when the recipient already owns the passive.
; Default: no

Starfall.GrantPassives.Houses=(comma-separated house selectors)
; Selects All, Self, Allies, Enemies, or Neutral relative to the firing owner. Warhead ownership flags also apply.
; Default: All

Starfall.GrantPassives.TargetTypes=(comma-separated type IDs or research categories)
; Filters passive recipients. Warhead damage uses its own rules.
; Default: *
```

`CellSpread=0` grants only to a directly hit unit, not an empty ground point. Positive spread tests eligible recipients around impact; buildings use occupied cells. Zero-damage support hits can grant. Dead/off-map/iron-curtained recipients and zero-verses armor matches are excluded. A single detonation grants each passive at most once to each unit; independent detonations in the same frame remain independent. It does not make an otherwise untargetable unit targetable by the support weapon.

### Passive visual fields

```ini
[PassiveID]
Anim=(animation ID)
; Attached looping animation; follows holder and its drawing scale.
; Default: none

Anim.Always=(boolean)
; yes ignores passive state conditions for the attached animation; still subject to visibility/lifetime. Requires Anim.
; Default: no

Anim.WhileReady=(boolean)
; yes hides the attached animation during the passive event's cooldown. Requires Anim and a positive Cooldown.
; Default: no

GainAnim=(animation ID)
; Play once when newly acquired on-map; transport acquisition waits until on-map.
; Default: none

TriggerAnim=(animation ID)
; Play after trigger checks, before the action group; for WhileState, on state entry after its wait.
; Default: none

TriggerAnim.Target=(Self or Target)
; Self or Target; unavailable target/location skips playback.
; Default: Self

Tint.Color=(red,green,blue integers)
; Target RGB color. Each component is an integer from 0 to 255. Use 0,0,0 for black or 255,255,255 for white.
; Default: none

Tint.Strength=(number)
; Requires Tint.Color. Integer from 0% to 100%. Each body pixel blends from its original color to the target, rounded to the game’s color depth. 0% preserves the original, 50% gives their midpoint, and 100% gives the target.
; Default: 50%

```

Registered animation IDs must exist. A visuals-only passive can omit Trigger when its section contains only supported Anim/GainAnim/Tint/Stack.Max fields. A conditional visuals-only passive can use WhileState with a condition. An event can omit actions when TriggerAnim supplies the visual result. For an action-bearing event, animation playback alone does not replace the action-success cooldown rule.

Attached animations hide while cloaked/off-map and disappear when ownership is lost. The strongest active tint wins. Ties use the first passive ID in case-insensitive lexical order. Cloaking hides the tint, and iron curtain or force shield takes visual precedence. Tint does not alter damage or ownership. Passive visuals, state-entry tracking, and supported animation state persist across saves.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Passive does not appear or execute | Check registration, the exact ID, unit binding or grant, `Trigger`, required result fields, and validation messages in `Starfall.log`. |
| Event does not trigger | Check ownership and target filters, cooldown, count, chance, per-unit trigger limit, event timing, and target eligibility. |
| Continuous modifier does not apply | Check `Trigger=WhileState`, the matching state conditions, required `Modifier.*` field, state wait, and map/transport eligibility. |
| Action runs on the wrong object | Check the event counterpart and `Action.Target` defaults in [target names](#target-names). |
| Temporary bonus remains or vanishes unexpectedly | Check grant duration, overlapping ownership sources, manual stacks, and [sources, stacks, and removal](#sources-stacks-and-removal). |
| Formula is rejected | Check the `$(...)` wrapper, destination field, variables available in that event, and error messages. |

The [configuration diagnostics](setup.md#diagnostics-and-removed-interfaces) section lists additional checks.
