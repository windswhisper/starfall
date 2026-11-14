# Starfall Lab: Configuration and Lua Reference

Document language: English. Audience: mod authors, AI assistants, and maintainers.
Applies to: Starfall Lab 0.36, as implemented in this repository on 2026-11-14.
Runtime: a 32-bit DLL extension for Command & Conquer: Yuri's Revenge.

This is a reference for the implemented interfaces, not a proposal for future features. Field names in code font are literal configuration keys. `required` means that omission makes the described configuration invalid. Defaults apply when a field is omitted, subject to configuration inheritance. Native game fields such as `Spawns`, `Locomotor`, and `SuperWeapon` belong to the game or another extension; Starfall does not redefine them.

## Contents

1. [Authoring contract](#authoring-contract)
2. [Installation and configuration sources](#installation-and-configuration-sources)
3. [Registries and a complete passive example](#registries-and-a-complete-passive-example)
4. [Triggers, targets, and event execution](#triggers-targets-and-event-execution)
5. [Filters and state conditions](#filters-and-state-conditions)
6. [Continuous modifiers and passive ownership](#continuous-modifiers-and-passive-ownership)
7. [Action reference](#action-reference)
8. [Numeric formulas](#numeric-formulas)
9. [Research projects](#research-projects)
10. [External grants and map integration](#external-grants-and-map-integration)
11. [Active skills](#active-skills)
12. [Lua API](#lua-api)
13. [ActionSequence superweapons](#actionsequence-superweapons)
14. [Animation, icons, and audio](#animation-icons-and-audio)
15. [Persistence, multiplayer, and compatibility](#persistence-multiplayer-and-compatibility)
16. [Diagnostics and removed interfaces](#diagnostics-and-removed-interfaces)
17. [Build, tests, and source map](#build-tests-and-source-map)

## Authoring contract

When generating configuration or scripts:

- Register a passive ID, define its section, and give it to a unit through a binding or grant. Registration alone does not give the passive to anyone.
- Use distinct, stable IDs for passives, research projects, action sections, and grant presets. References use IDs, not localized display names.
- Choose the correct execution context before choosing an action: ordinary event, damage interception, continuous state, active skill, or superweapon/map script.
- Use only the keys and enumerations documented for that context. Unknown `Action.*` fields and incompatible combinations can disable the entire passive.
- Use `Action` for one native result, `Actions` for an ordered list of action sections, and `Effects` for research effects. These three interfaces are different.
- Use `Trigger=WhileState` with `Modifier.*` for reversible conditions. Event actions such as `ChangeFirepower` permanently change the individual unit's accumulated value.
- Distinguish percentage points, multipliers, and percentages of a value. `Action.Percent=+25%`, `Modifier.FirepowerMultiplier=1.25`, and `$(25%)` have different roles.
- Explicitly select the action target when intent could be ambiguous. `Filter.TargetKind` filters the event counterpart; it does not select the action recipient.
- In Lua, use colon calls, ordered arrays with `ipairs`, and Starfall's synchronized random functions. Do not invent engine methods or assume unrestricted Lua libraries.
- Keep delayed state in `ctx.state` or task `data`. Module locals and globals do not persist between callbacks.
- Merge registry entries into unused slots and append unit bindings to existing lists. Do not replace another feature's bindings accidentally.
- Treat examples marked **fragment** as partial configuration. Native unit, weapon, warhead, animation, and country IDs must exist in the target ruleset.
- For implementation questions, inspect the source map at the end of this document. Do not infer an unsupported option from similar Ares or Phobos syntax.

Terminology:

| Term | Meaning |
| --- | --- |
| Holder / `Self` | Unit whose passive is executing; special contexts are identified explicitly below. |
| Counterpart / formula `Target` | Other participant in the event, independently of `Action.Target`. |
| House | A player's owning faction instance, identified by a runtime index. A house index is not a starting-position number. |
| Cell | Map coordinate, not a screen pixel. Integer cell coordinates are in `0..511` and must be inside the playable map. |
| Frame | Simulation frame. Starfall's seconds-based settings use 15 frames per game second; wall-clock duration varies with game speed. |
| Snapshot | Data captured for an event. It can remain usable after the original object disappears. |
| Unit handle | Lua reference to a unit ID. Check `unit:alive()` before relying on live properties. |
| Owned passive | A passive available through at least one source. Owning it does not imply a positive stack count. |

## Installation and configuration sources

Build with Visual Studio 2022 / MSVC v143, x86 C++ tools, and a Windows SDK. Run `build.cmd` at the repository root. The output is `build/Starfall.dll`.

Place the DLL beside `gamemd.exe` and load it with a compatible Syringe loader. The handshake requires game executable CRC `0x54CC0A13`. The game and loader are not supplied by this project. With Phobos, load Starfall before Phobos; the automated tests use explicit extension order. See [compatibility](#persistence-multiplayer-and-compatibility) for conversion and mind-control restrictions.

| Location | Purpose |
| --- | --- |
| Game-root `Starfall.ini` | Optional Starfall-specific definitions and overrides. |
| Engine-loaded `rulesmd.ini` and merged rules | Starfall fields can coexist with native rules. Native object definitions must be loaded by the game. |
| Map unit-type sections | `Starfall.Passives` adds map-specific unit bindings; these bindings are saved. |
| Map action definitions | Supported ActionSequence and map-script fields have a persisted map overlay. |
| Game-root `*.lua` | Lua source files, top directory only; no recursive discovery. |
| `artmd.ini` animation section | Native animation properties. |
| Game-root `Starfall.log` | Configuration and runtime diagnostics. |

Important lookup rules:

1. Ordinary passive fields first read `Starfall.ini`, then fall back to the engine's merged rules if the file value is empty. An empty passive override is therefore not a reliable way to erase an inherited rules value.
2. Research configuration has a separate reader: an explicitly present file field wins, including an empty value; otherwise it falls back to merged rules.
3. ActionSequence fields merge engine rules, then file fields, then the captured map overlay. Do not assume this precedence applies to every subsystem.
4. Passive registry values from the available sources are collected and normalized. Research registration uses nonnegative numeric keys; file entries replace merged-rule entries with the same numeric key.

Use UTF-8 for authored files. IDs and most enumerated values are normalized case-insensitively; prefer consistent ASCII IDs. Display text and `Script.Param.*` values preserve case. UTF-8 display text falls back to the Windows code page if necessary. Semicolons introduce configuration comments; do not depend on semicolons inside a string value being preserved.

Use `yes` or `no` for booleans unless a field explicitly documents additional forms. Most passive booleans do not accept `true` or `1`. Lists normally use ASCII commas. List duplicate behavior is context-specific: ordinary ID lists deduplicate; passive `Actions` preserves repeated steps; research `Effects` deduplicates effect IDs.

Keep IDs and values short. Skill/project IDs should not exceed 127 bytes. Many ordinary INI fields use a 2,048-byte read buffer. Avoid huge action lists and split complex logic into scripts or reusable sections.

## Registries and a complete passive example

| Registry / binding | Entry format | Meaning |
| --- | --- | --- |
| `[Starfall.PassiveTypes]` | `0=SF_KillHeal` | Register a passive or active skill section. |
| Unit type: `Starfall.Passives` | `SF_KillHeal,SF_LastStand` | Give those IDs to every unit of that type, regardless of owner. |
| `[Starfall.ResearchTypes]` | `0=SF_FieldRepair` | Register a research project section. |
| `[Starfall.PassiveGrants]` | `1=SF_RepairGrant` | Register a grant preset; map/team action 9601 uses the numeric key. |
| `[Starfall.MapScripts]` | `1=SF_WaveScript` | Register a map Lua preset; map action 9602 uses the numeric key. |
| Native `[SuperWeaponTypes]` | `SF01=FieldSupply` | Register an engine superweapon that may use `Type=ActionSequence`. |

For grant and map-script preset numbers, use canonical nonnegative decimal integers without leading zeros. Use unused numeric slots in passive/research registries when merging into an existing mod.

**Complete Starfall.ini example**, assuming the native `MTNK` unit type exists:

```ini
[Starfall.PassiveTypes]
0=SF_KillHeal
1=SF_LastStand

[MTNK]
Starfall.Passives=SF_KillHeal,SF_LastStand

[SF_KillHeal]
Trigger=OnKill
Filter.TargetHouse=Enemies
Condition.Count=3
Action=Heal
Action.Target=Self
Action.Amount=50

[SF_LastStand]
Trigger=WhileState
Condition.Formula=$(Self.Health <= Self.MaxHealth * 50%)
Modifier.FirepowerMultiplier=1.25
```

Each tank counts its own qualifying kills. Every third kill attempts to heal that tank by 50. The continuous modifier adds a separate 1.25 multiplier only while the tank is at or below half health.

## Triggers, targets, and event execution

### Trigger catalog

| `Trigger` | When it runs | Event counterpart / formula `Target` |
| --- | --- | --- |
| `OnKill` | Holder receives a kill. | Killed unit; recorded damage is zero. |
| `OnBeforeDealDamage` | Synchronous attacker interception before positive health loss. | Actual victim. |
| `OnDealDamage` | Holder causes actual health loss. | Victim. |
| `OnBeforeTakeDamage` | Synchronous victim interception before positive health loss. | Attacker, possibly absent. |
| `OnTakeDamage` | Holder actually loses health. | Attacker, possibly absent. |
| `OnDeath` | Holder is registered as destroyed; once per death. | Killer, possibly absent. |
| `OnFire` | A standard shot is emitted, including supported spawned missiles. Burst shots count individually. | Aimed unit; a ground target can supply a recorded point. |
| `OnBeforeFire` | Before the first shot of a standard volley. | Aimed object; see the restricted replacement contract below. |
| `OnDeploy` | Supported infantry/simple-deployer posture enters deployed, vehicle deploys into a building, or deploy-fire completes. | No counterpart; action target aliases resolve to the resulting holder/building. |
| `OnPromote` | Observed rank increases. A direct two-rank jump is one generic event. | None. |
| `OnPromoteVeteran` | Observed promotion reaches veteran. | None. |
| `OnPromoteElite` | Observed promotion reaches elite. | None. |
| `OnPassiveGained` | This passive becomes newly available on the holder. | None. |
| `OnPassengerConsumed` | Holder's direct passenger is consumed by Starfall's action. | Consumed passenger's saved information. |
| `OnBuildingUpgrade` | A native building upgrade is successfully installed. | Host building; `Self` is also the host. Bind this passive to the **upgrade type**, not merely the host type. |
| `OnSignal` | A queued signal matching `Signal` is delivered. | Optional unit attached to the signal; sender is separate. |
| `OnInterval` | Holder's positive `Interval` elapses while eligible and on-map. | Current aimed unit or ground target at that time. |
| `WhileState` | Continuous evaluation while eligible. | Current target, where a formula needs it. No action list. |
| `Active` | A validated player command activates a selected holder. | Selected unit for unit-target skills. Requires Lua. |

Use `Active`, not the internal name `OnCast`. `OnSuperWeapon`, `OnMapTrigger`, and `OnScheduled` are internal execution contexts, not ordinary `Trigger` values to register.

Newly created veteran/elite units and rank restored from a save do not count as promotions. Renewing a passive, overlapping sources, reapplying stacks, or loading a save does not repeat acquisition events. Completely losing the passive and later acquiring it can trigger acquisition again.

`OnBuildingUpgrade` obtains its skill list from the installed upgrade type's bindings, then executes against the host. Its event stack snapshot is 1. Failed installations do not emit this event. It does not grant the upgrade's entire passive list permanently to the host.

### Target names

| Name | Ordinary event meaning |
| --- | --- |
| `Self` | Holder. For death actions, the saved owner/location remains usable by appropriate actions. |
| `Target` | Counterpart: victim for outgoing damage/kill; attacker for incoming damage/death; aimed unit for firing/interval; attached unit for signals. |
| `Attacker` | Recorded attacker. It need not be alive when the queued action executes. |
| `Victim` | Recorded victim. Positional actions can fall back to its recorded location. |
| `VictimLocation` | Recorded impact/victim position; only positional actions accept this. |
| `Sender` | Signal sender; allowed only in `OnSignal`. |

For deploy, promotion, and passive-acquisition actions, `Target` resolves to `Self`, although their event counterpart for filtering/formulas is absent. Building-upgrade events explicitly use the host as both holder and counterpart.

Default `Action.Target` is `Target` for `StealPassive` and `OnBeforeDealDamage`; otherwise it is `Self`. `SelfDestruct` and `ReplaceWeapon` require `Self`. Damage-interception targeting is more restrictive than ordinary targeting.

The positional actions are exactly `FireWeapon`, `DetonateWarhead`, `SpawnUnit`, `SpawnCrate`, `PlayAnim`, `PlaySound`, `ShakeScreen`, and `SpawnParticleSystem`. They accept `VictimLocation`; Lua can supply a point table as their `Target`. A missing/dead unit does not become a valid heal or transformation target merely because its old position is known.

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

## Filters and state conditions

### Ordinary event fields

| Field | Default | Type and constraint |
| --- | --- | --- |
| `Filter.TargetHouse` | `All` | List: `All,Self,Allies,Enemies,Neutral,None`. `Self` means same owner; `Allies` means other allies. |
| `Filter.TargetKind` | `All` | List: `All,Infantry,Vehicle,Aircraft,Building,Other,None`. |
| `Condition.MinDamage` | `0` | Nonnegative integer. Actual lost health for damage events; pending loss for interception. |
| `Condition.Count` | `1` | Positive integer; per unit and passive. |
| `Chance` | `100%` | Integer `0..100`, optional `%` suffix. |
| `Cooldown` | `0` | Nonnegative frame count after success. |
| `MaxTriggers` | `0` | Nonnegative successful-trigger cap; zero means unlimited. |
| `Interval` | none | Positive frames, required only for `OnInterval`; invalid on other triggers. |
| `Stack.Max` | `2147483647` | Positive stack cap. Initial stacks are zero. |
| `Condition.Formula` | none | `$(...)`; nonzero means true. See formula restrictions. |

`None` matches an absent counterpart/owner. For deploy/promotion/acquisition, leave filters at `All` unless deliberately testing absence. `Vehicle` includes ships; flying infantry remain `Infantry` because categories follow engine object types.

### Shared holder-state conditions

| Field | Allowed values | Meaning |
| --- | --- | --- |
| `Condition.OwnerPower` | `Sufficient`, `Low` | Owning house's power situation. |
| `Condition.SelfPowered` | `yes`, `no` | Holder's own online/powered state. |
| `Condition.SelfMoving` | `yes`, `no` | Actual locomotor movement; buildings are stationary. |
| `Condition.SelfCloak` | `Uncloaked`, `Cloaking`, `Cloaked`, `Uncloaking` | Holder's own cloak state, distinct from enemy detection. |
| `Condition.Formula` | expression | Event snapshot for events; current values for continuous evaluation. |

### WhileState-only conditions

| Field | Default | Constraint / behavior |
| --- | --- | --- |
| `Condition.SelfDeployed` | omitted | `yes` / `no` for supported infantry/simple-deployer postures. A stationary unit or a deployed-into building is not automatically a deployed-posture unit. |
| `Condition.StateDurationSeconds` | `0` | Nonnegative decimal game seconds, up to three decimal places; multiply by 15 and round fractional frames upward. Interruption resets the wait. |
| `Condition.Nearby.Range` | none | Required when using nearby conditions; integer `1..32` cells. |
| `Condition.Nearby.Houses` | `All` | List: `All,Self,Allies,Enemies,Neutral`. |
| `Condition.Nearby.Kinds` | `All` | List: `All,Infantry,Vehicle,Aircraft,Building`. |
| `Condition.Nearby.CountAtLeast` | `1`, or `0` if only an upper bound is supplied | Nonnegative inclusive bound. |
| `Condition.Nearby.CountAtMost` | `2147483647` | Inclusive bound, at least the lower bound. |
| `Condition.Nearby.ScanInterval` | `10` | Positive frames; staggered sampling, cached between scans. Before first sample, condition is false. |

Nearby counting uses a planar circular cell range, excludes the holder, and deduplicates objects/buildings. Hostile cloaked/disguised objects must satisfy the visibility/detection checks. Nearby cache progress is saved.

A `WhileState` definition needs a state condition and a modifier or supported visual result. Use `Condition.Formula=$(1)` for an always-true condition. Passenger support via `Modifier.TransportPassives` is the exception that can omit a condition. Event-only fields such as `Action.*`, `Actions`, `Filter.*`, `Chance`, and `Condition.Count` are invalid in `WhileState`.

Leaving the map or entering limbo normally suspends continuous evaluation and removes its modifiers; re-entry restarts any wait. Interval progress restarts when the holder becomes eligible again. Timed grant expiry still advances while the unit is inside a transport or building. See the passenger-support exception below.

## Continuous modifiers and passive ownership

### Modifier catalog

All following fields belong to a `WhileState` passive. Omitted numeric multipliers contribute identity `1`; omitted switches contribute no effect.

| Field | Accepted value | Effect |
| --- | --- | --- |
| `Modifier.SpeedMultiplier` | Number or formula, `0 < value <= 100` | Mobile unit speed. |
| `Modifier.FirepowerMultiplier` | Number or formula, `0 < value <= 100` | Firepower. |
| `Modifier.ArmorMultiplier` | Number or formula, `0 < value <= 100` | Armor strength; higher means more resistant. |
| `Modifier.WeaponRangeMultiplier` | Number or formula, `0 < value <= 100` | Standard maximum weapon range. |
| `Modifier.FireRateMultiplier` | Number or formula, `0 < value <= 100` | Divide standard volley rearm delay by this value; does not rewrite intra-burst delays. |
| `Modifier.PowerMultiplier` | Number or formula, `0 <= value <= 100` | This building's supply or drain; zero is allowed. |
| `Modifier.VisualScale` | Fixed number `0.01..2.00` | Mobile-unit drawing scale, not footprint. |
| `Modifier.ProjectileScale` | Fixed number `0.50..2.00` | Projectile, impact explosion, and existing `CellSpread` scale at launch. |
| `MultiShot` or `Modifier.MultiShot` | Nonnegative integer, formula, `yes`, or `no` | Extra distinct targets; specify only one alias. `yes` means `$(This.Stack)`, `no` means zero. |
| `Modifier.DisablePrimary` | `yes` / `no` | Disable standard primary slot. |
| `Modifier.DisableSecondary` | `yes` / `no` | Disable standard secondary slot. |
| `Modifier.PrimaryWeapon` | Existing weapon ID | Replace standard primary slot. |
| `Modifier.SecondaryWeapon` | Existing weapon ID | Replace standard secondary slot. |
| `Modifier.EMP` | `yes` / `no` | Starfall-managed stoppage; airborne vehicles/aircraft can crash. |
| `Modifier.Berserk` | `yes` / `no` | Non-building berserk behavior, including friendly attacks. |
| `Modifier.Mirage` | `yes` / `no` | Vehicle stationary disguise behavior. |
| `Modifier.Cloakable` | `yes` / `no`, default `no` | Grant native cloaking capability while the state is active; retain native cloak/decloak rules. |
| `Modifier.Immune` | `yes` / `no` | Native-style immunity to ordinary health damage. |
| `Modifier.ImmuneToMindControl` | `yes` / `no` | Block supported new mind-control attempts; does not release existing control. |
| `Modifier.SpawnPassives` | Registered passive ID list | Give passives to direct native `Spawns=` children. |
| `Modifier.TransportPassives` | Registered passive ID list | Passenger grants passives to its direct ground-vehicle transport. |
| `Modifier.TransportPassives.Stack` | `yes` / `no`, default `no` | Each contributing passenger provides one source-owned layer when enabled. Requires `Modifier.TransportPassives`. |

Continuous multipliers from different active passives multiply. Permanent event changes are a separate base: accumulated firepower 120% and continuous multiplier 1.5 produce 180%; leaving the state returns to 120%.

Permanent event bases are bounded by `[Starfall.PassiveLimits]`. Each upper/lower limit is a positive integer percentage, optionally suffixed with `%`; lower must not exceed upper. These limits bound accumulated event values, not the final product with continuous multipliers.

| Upper-limit field | Default upper | Lower-limit field | Default lower |
| --- | --- | --- | --- |
| `Speed` | `300%` | `Speed.Min` | `1%` |
| `Firepower` | `300%` | `Firepower.Min` | `1%` |
| `Armor` | `300%` | `Armor.Min` | `1%` |
| `Scale` | `300%` | `Scale.Min` | `25%` |
| `ProjectileScale` | `300%` | `ProjectileScale.Min` | `1%` |
| `WeaponRange` | `300%` | `WeaponRange.Min` | `1%` |
| `FireRate` | `300%` | `FireRate.Min` | `1%` |

For example, `[Starfall.PassiveLimits] Firepower=200%` caps accumulated event firepower at twice its base. Invalid limits are logged and replaced by defaults, with the default minimum reduced if necessary to fit the maximum.

Weapon-slot disabling takes precedence over replacement. Multiple active replacements use the last in processing order; do not use registry numbering as an assumed ordering API. Native rank/deploy weapon changes are still resolved. `FireWeapon` uses its explicitly named weapon instead of the slot replacement.

Projectile scale is captured at launch; later state changes do not resize an already launched shot. Scaling an original `CellSpread=0` does not create an area. Multishot chooses nearest eligible extra enemies, at most one extra projectile per target; contributions add, fractional counts round down, and negative formula results contribute zero. Extra shots do not consume additional ammo or recursively emit `OnFire`. Native spawner and `LimboLaunch` weapons keep their own behavior.

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

See [the complete cloaking example](../examples/Cloakable/Starfall.ini) for registration and a tank binding.

### Aura fields

| Field | Default | Constraint |
| --- | --- | --- |
| `Modifier.Aura.Passives` | required | Registered non-aura passive IDs. |
| `Modifier.Aura.Range` | required | Integer `1..32` cells or `-1` for whole map. |
| `Modifier.Aura.Houses` | `Self` | Nonempty list: `Self,Allies,Enemies,Neutral,All`. |
| `Modifier.Aura.TargetTypes` | `*` | Concrete IDs, `*`, `@Infantry`, `@Vehicle`, `@Aircraft`, `@Building`; mixed lists allowed. |
| `Modifier.Aura.IncludeSelf` | `no` | `yes` / `no`; holder must also pass the filters. |

Aura coverage uses horizontal center distance, includes the boundary, and refreshes every 15 frames. Loss of the source or its condition withdraws that source. An aura cannot directly grant a passive that itself defines an aura. A transport-support passive cannot directly grant another transport-support passive.

Passenger support is evaluated for passengers even while they are transported. It cannot use spatial nearby conditions or target/distance-dependent conditions. Normal on-map continuous effects are suspended off-map, but supported passenger combat evaluation can retain firepower, weapon range, fire rate, and multishot for relevant firing paths; spatial/distance restrictions still apply. Do not assume every off-map modifier remains active. The direct transport relationship matters, not arbitrary nesting.

### Sources, stacks, and removal

Sources include unit bindings, map bindings, research, temporary/permanent grants, auras, spawn parents, and passenger support. One unit executes one copy of a given passive ID even when sources overlap.

- Manual stacks start at zero, are separate from ownership, and are capped by `Stack.Max` together with effective contributions.
- `ChangePassiveStack` changes manual layers of a passive already held by the recipient.
- `StackOnReapply=yes` adds one layer only if the recipient already holds the passive. First acquisition still starts at zero.
- Temporary grants merge durations by keeping the longer remaining duration; permanent wins. Expiry removes that source, not independent sources.
- Losing the last source clears the relevant progress; reacquisition starts fresh. A completed permanent action, such as extra maximum health, is not undone when the granting passive expires.
- `RemovePassive` removes/suppresses the current held passive and clears its stacks/counting/wait state. It is not a global deletion of the registered definition; later valid grants can make it available again.
- `StealPassive` transfers manual layers from a live donor; passenger-support layers are not transferable inventory. A zero-stack base ability can transfer once if the receiver does not already own it.
- After `ConsumePassenger`, stealing uses the consumed passenger's recorded layers and consumes that record, preventing repeated inheritance of the same layers.

## Action reference

### Action catalog

The following table describes ordinary INI action sections. Context restrictions in the trigger, active-skill, Lua, and superweapon sections take precedence.

| `Action` | Required / optional action fields | Result |
| --- | --- | --- |
| `Heal` | Exactly one of positive `Action.Amount` or positive `Action.PercentOfDamage`; amount can be a formula. | Heal up to current maximum health. |
| `Damage` | Same amount choice as `Heal`; optional `Action.Warhead`. | Damage one selected unit; specified warhead supplies armor verses and death behavior. |
| `SetDamage` | `Action.Amount`: nonnegative integer or formula. | Replace current pending health loss; damage interception only. |
| `BlockDamage` | No amount field. | Set pending loss to zero; damage interception only. |
| `ApplyIronCurtain` | `Action.Duration`: positive frames. | Apply native iron curtain, preserving a longer existing duration; existing force shield takes precedence. |
| `GiveCredits` | Same amount choice as `Heal`. | Add money to recipient's owner, or the explicit owner context in a sequence. |
| `GiveResearchPoints` | Same amount choice as `Heal`. | Add research points; respects the research enable switch. |
| `ChangeMaxHealth` | Nonzero signed integer `Action.Amount` **or** `Action.MaxHealthPercent=1%..10000%`. | Permanently change maximum health; percentage only increases it. |
| `ChangeAmmo` | Nonzero signed integer `Action.Amount`. | Change finite ammo, clamped to magazine bounds. |
| `ChangeSpeed` | Nonzero signed integer percentage points or formula in `Action.Percent`. | Permanently change speed base. |
| `ChangeFirepower` | Same `Action.Percent` contract. | Permanently change firepower base. |
| `ChangeArmor` | Same `Action.Percent` contract. | Permanently change armor-strength base. |
| `ChangeWeaponRange` | Same `Action.Percent` contract. | Permanently change standard weapon range base. |
| `ChangeFireRate` | Same `Action.Percent` contract. | Permanently change standard fire-rate base. |
| `ChangeScale` | Nonzero signed integer percentage points in `Action.Percent`; no formula. | Permanently change mobile-unit drawing size. |
| `ChangeProjectileScale` | Nonzero signed integer percentage points in `Action.Percent`; no formula. | Change future projectile/explosion/spread scale. |
| `SpawnUnit` | `Action.UnitType`; optional `Action.Owner=Self`. | Spawn infantry, vehicle, or aircraft, searching up to three rings if necessary; no buildings. |
| `Transform` | `Action.UnitType`. | Convert a living unit in place; see conversion restrictions. |
| `ChangeOwner` | Optional `Action.Owner=Self`. | Transfer a living unit; skip if already owned by that house or under mind control. |
| `SpawnCrate` | Optional `Action.CrateType=Random`. | Place a crate near the target position. |
| `SelfDestruct` | `Action.Target=Self` only. | Destroy the holder through the supported normal death path. |
| `ConsumePassenger` | No `Action.Amount`. | Consume first direct passenger and its nested cargo; emit one direct-passenger event. |
| `GrantPassive` | `Action.Passive`; optional `Action.Duration=-1`, `Action.StackOnReapply=no`. | Give one registered passive, permanently or for positive frame duration. |
| `StealPassive` | `Action.Passive` list; optional positive integer `Action.Amount=1`. | Transfer at most that many layers per ID from recipient/donor to holder. |
| `RemovePassive` | `Action.Passive` list. | Remove matching held IDs; continue past absent IDs. |
| `ChangePassiveStack` | `Action.Passive`, nonzero signed integer `Action.Amount`. | Change manual layers of an already held passive. |
| `ResetStateTimer` | `Action.Passive`. | Restart an existing timed `WhileState` wait; no effect if not held/timed/started. |
| `Promote` | Optional `Action.Levels=1`, allowed `1` or `2`. | Promote a trainable unit, capped at elite. |
| `RunScript` | `Action.Script`, `Action.Function`. | Invoke a discovered Lua module callback. |
| `SendSignal` | `Action.Signal` plus optional signal fields below. | Queue a signal to qualifying receivers. |
| `ReplaceWeapon` | `Action.Weapon`, target `Self`. | Replace one volley; `OnBeforeFire` only. |
| `FireWeapon` | `Action.Weapon`. | Emit the specified weapon toward a unit or position. |
| `DetonateWarhead` | `Action.Warhead`; optional nonnegative integer `Action.Amount=0`. | Detonate at the target position; amount is base damage, not a formula here. |
| `PlayAnim` | `Action.Anim`. | Play a registered animation once; a live-unit attachment follows that unit. |
| `PlaySound` | `Action.Sound`. | Native sound ID or `wav:` followed by a PCM WAV path. |
| `ShakeScreen` | `Action.ShakeX=0`, `Action.ShakeY=4`, `Action.ShakeLocal=yes`. | Native screen shake; axes `0..32`, at least one nonzero. |
| `SpawnParticleSystem` | `Action.ParticleSystem`. | Create an existing particle-system type at or toward the target. |
| `ForceMove` | `Action.ForceMove.*` fields below. | Schedule forced ground-unit movement/lift. |
| `ChronoTeleport` | `Action.Destination=X,Y`; optional `Action.SearchRadius=3`. | Teleport a supported on-map ground mobile unit to an available cell. |

For signed integer amounts, `+10` and `-10` are valid; zero is invalid where the table says nonzero. Percent-of-damage amounts round down. Fixed event percentage changes accumulate in percentage points: two `+10%` actions produce 120% from a 100% base, not 121%.

Maximum-health increases also add the increase to current health; decreases only trim health that exceeds the new maximum. Percentage growth uses the pre-action maximum. Native iron curtain can kill infantry/organic targets according to the target ruleset; it is not a universal harmless invulnerability buff.

`Damage` defaults to the game's `C4Warhead` path if no warhead is specified. It damages only the selected unit. Warhead `AffectsAllies`, `AffectsOwner` (fallback to `AffectsAllies`), `AffectsEnemies` (default yes), and `AffectsNeutral` constrain ownership relationships. Sequence damage uses the launching owner even without a unit caster.

`Action.Owner` accepts `Self,Attacker,Victim,Neutral,Player1,Player2,Player3,Player4,Player5,Player6,Player7,Player8`. Player numbers refer to starting positions. The `Owner` target/owner alias belongs to sequence contexts, not arbitrary ordinary passives.

`Action.CrateType` accepts `Random,Money,Unit,HealBase,Cloak,Explosion,Napalm,Squad,Darkness,Reveal,Armor,Speed,Firepower,ICBM,Invulnerability,Veteran,IonStorm,Gas,Tiberium,Pod`. `Random` uses the current native `[Powerups]` weights.

### Multiple actions

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

`ConsumePassenger` has a deliberate exception: failure stops the remaining steps in that INI `Actions` list or Lua `ctx:actions` call. Previous steps are not rolled back. Success replaces the following steps' counterpart snapshot with the consumed passenger, so a live-target action does not accidentally fall back to the original enemy. The direct passenger's recorded layers are shared with its queued consumption event and can only be transferred once. Nested cargo is removed without emitting a separate consumption event for every nested unit. This interface operates on passenger lists, not a building's separate garrison list, and does not synthesize ordinary passenger kill/death passive events.

### ForceMove fields

All INI fields in this table have the full `Action.ForceMove.` prefix. They are not native unit locomotor settings.

| Field | Default | Constraint / meaning |
| --- | --- | --- |
| `Action.ForceMove.Offset` | none | `dx,dy`, each finite `-512..512` cells; supports fractions. Mutually exclusive with Mode, Center, and Distance. |
| `Action.ForceMove.Mode` | required if Offset omitted | `Push`, `Pull`, `Launch`. Do not write `Mode=Offset`; supply Offset instead. |
| `Action.ForceMove.Center` | `Self` | `Self` or `Impact`. Only for non-Offset modes. |
| `Action.ForceMove.Distance` | `0` | Finite `0..512` cells. Push/Pull require a positive effective distance; Launch can omit it. |
| `Action.ForceMove.Height` | `1` for Launch, otherwise `0` | Finite `0..512` cells of visual lift. |
| `Action.ForceMove.Duration` | required | Positive integer simulation frames. |
| `Action.ForceMove.Easing` | `Linear` | `Linear,EaseIn,EaseOut,EaseInOut`; affects horizontal progress. |
| `Action.ForceMove.Tilt` | `yes` | `yes` / `no`; supported voxel vehicles lean and settle afterward. |
| `Action.ForceMove.IgnoreCollision` | `yes` | Pass through obstacles and resolve landing, or stop before blocking obstacles when `no`. |
| `Action.ForceMove.BlowWeight` | omitted | Finite threshold; target native `Weight` must be **strictly less**. Omitted means no weight threshold. |
| `Action.ForceMove.AffectsIronCurtain` | `no` | Allow true iron-curtained targets if `yes`; force shield is not classified as true iron curtain by this check. |

At least one effective horizontal offset/distance or height must be nonzero. Coordinates are rounded to engine sub-cell units. Pull does not overshoot the chosen center. Targets must be supported living, on-map ground infantry/vehicles; carried, airborne, deploying, sinking, warping, and other incompatible locomotor states are rejected.

During movement, ordinary movement, firing, deployment, and active skills are blocked, but damage can still occur. A new ForceMove replaces the current movement from its current location/lift. With `IgnoreCollision=yes`, landing can crush ground units regardless of owner; a building receives base impact damage based on the falling unit's current health and the falling unit dies; impassable landing terrain can also kill it. With `no`, horizontal travel stops at an obstacle, lift completes, and landing collision damage is not applied. Both modes respect map boundaries.

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
Action.ForceMove.Duration=12
Action.ForceMove.Easing=EaseOut
Action.ForceMove.IgnoreCollision=no
Action.ForceMove.BlowWeight=5
```

`ChronoTeleport` is a separate action. Its `Action.Destination` uses integer cells `0..511`; `Action.SearchRadius` is an integer `0..32`, default 3. It tries the center then the nearest valid cell in the circular search, avoiding reserved teleport destinations. Zero radius only tries the exact cell. Failure leaves the unit in place.

### Signals

| Field | Default | Constraint / meaning |
| --- | --- | --- |
| `Action.Signal` | required | Nonempty case-insensitive ID, at most 127 bytes; no registry needed. |
| `Action.Target` | `Self` | Receiver or broadcast center unit. |
| `Action.Signal.Range` | `0` | Integer: 0 direct to center, `1..128` local broadcast, `-1` whole map. |
| `Action.Signal.Houses` | `Self` | List: `Self,Allies,Enemies,Neutral,All`, relative to sender. |
| `Action.Signal.IncludeSelf` | `no` | Include sender in broadcast; direct self-send is independent of this flag. |
| `Action.Signal.Target` | none | Attached unit: `Self,Target,Attacker,Victim`; also `Sender` inside OnSignal. |
| `Action.Signal.Value` | `0` | Finite number or formula; computed when sending. |
| Receiver `Signal` | required for OnSignal | ID to match. |
| `Filter.SenderHouse` | `All` | Sender house filter, relative to receiver; same enum as Filter.TargetHouse. |
| `Filter.SenderKind` | `All` | Sender kind filter; same enum as Filter.TargetKind. |

On receipt, `Self` is receiver, `Target` is attached unit, and `Sender` is sender. `Filter.Target*` tests the attached unit, not sender. `Action.Target=Sender` requires that sender to remain valid for live-unit actions.

Receivers must be living on-map units; transported/garrisoned units are not separate broadcast recipients. Delivery is queued. Sender/target snapshots and value remain readable after objects disappear, but live-target actions can fail. A signal chain is limited to 16 levels and 1,024 deliveries; excess is stopped and logged. Chain progress and queued signals are saved.

## Numeric formulas

Formulas are a small expression language, not Lua. Wrap the complete expression in `$(...)`.

| Supported location | Interpretation |
| --- | --- |
| `Condition.Formula` | Nonzero true, zero false. |
| `Modifier.SpeedMultiplier`, `Modifier.FirepowerMultiplier`, `Modifier.ArmorMultiplier`, `Modifier.WeaponRangeMultiplier`, `Modifier.FireRateMultiplier`, `Modifier.PowerMultiplier` | Multiplier result within the field's allowed range. |
| `Action.Percent` for speed/firepower/armor/range/fire rate | Fraction converted to percentage-point change: `$(10%)` adds 10 points. No formula for Scale/ProjectileScale. |
| `Action.Amount` for Heal/Damage/GiveCredits/GiveResearchPoints/SetDamage | Nonnegative finite amount, floored to integer; range overflow or negative result fails. |
| `MultiShot` / `Modifier.MultiShot` | Extra-target count, floored; negative contributes zero. |
| `Action.Signal.Value` | Finite numeric payload. |

Operators: unary `+ -`, arithmetic `+ - * /`, parentheses, comparisons `> >= < <= == !=`. Functions: `min(a,b)`, `max(a,b)`, `clamp(x,low,high)`, and `if(condition,thenValue,elseValue)`. Names are case-insensitive. Decimal literals and percentage literals are supported; `1.5%` is `0.015`.

Comparisons bind less tightly than arithmetic and return 0 or 1. Chained comparisons such as `1 < x < 5` are invalid. Combine comparisons explicitly, for example `$( (Self.Ammo >= 1) * (Self.Ammo <= 3) )`. There are no `&&`, `||`, Lua `and/or`, arbitrary functions, or arbitrary unit properties.

`if` evaluates only the selected branch. However, context restrictions still apply to every referenced variable at configuration validation time. A forbidden `Event.Damage` reference is not made valid by putting it in an unselected branch.

| Variable | Value and scope |
| --- | --- |
| `Self.Health`, `Self.MaxHealth` | Holder health and extended current maximum. |
| `Self.Cost` | Holder type's base Cost, before country/factory/research discounts. |
| `Self.Ammo` | Ammo count; unlimited ammo is `-1`. |
| `Self.Passengers` | Direct passenger count. |
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
Action.Amount=$(min(Event.Damage * 25%, 100))
Action.Percent=$(5%)
```

The amount example requires a supported damage event. A constant formula does not require a valid target merely because other expressions can reference one.

## Research projects

### Project schema

Register every project in `[Starfall.ResearchTypes]`. Ordinary purchase costs exactly **100 research points** per project, once per house/project ID. There is no configurable `Price` field.

| Field | Default | Meaning |
| --- | --- | --- |
| `Name` | Project ID, except built-in legacy defaults | Display name. |
| `Summary` | empty, except legacy defaults | Short card description. |
| `Detail` | generated where supported | Hover description; supported placeholders below. |
| `Icon` | built-in fallback | PNG or engine-loadable PCX path. |
| `Countries` | unrestricted | Country IDs or `*`; comma-separated. |
| `Prerequisites` | none | Research project IDs, all owned by the purchasing house. |
| `Rarity` | `Common` | `Common,Rare,Epic,Insane`. Invalid value excludes project. |
| `Effect` | `GrantPassive` | One effect from the next table. |
| `Effects` | absent | Ordered list of effect sections; overrides the single-effect fields when present. Empty list is invalid. |
| `Passive` | required for GrantPassive | Registered passive ID. |
| `TargetTypes` | required where specified below | Recipient or production-type list, depending on effect. |
| `Credits` | `0` | GiveCredits amount; must be positive for that effect. |
| `Technology` | none | UnlockTech's existing techno type ID. |
| `FromType`, `ToType` | none | Conversion source/destination IDs. |
| `CostModifier`, `Category` | none | Cost effect parameters. |

Unknown prerequisites, cycles, and chains depending on invalid references block affected projects. Country restrictions and prerequisites are evaluated separately from immediate target availability. A valid conversion project may be visible while temporarily having no eligible conversion targets.

### Effect catalog

| `Effect` | Required fields | Behavior |
| --- | --- | --- |
| `GrantPassive` | `Passive`, `TargetTypes` | Current and future matching units of purchaser receive the passive; ownership changes use the new owner's research. |
| `GiveCredits` | Positive integer `Credits` | One-time money award. |
| `UnlockTech` | `Technology` | Purchaser gains production permission for one existing type. Factory compatibility, funds, and build limits still apply. |
| `ConvertInfantry` | Different valid infantry `FromType`, `ToType` | Convert currently eligible owned infantry; future production is unchanged. |
| `ConvertVehicle` | Different valid vehicle `FromType`, `ToType` | Convert currently eligible owned vehicles, including supported ships; future production is unchanged. |
| `ModifyTypeCost` | Concrete type IDs in `TargetTypes`; nonzero signed integer `CostModifier`, optionally `%` | Permanent per-house production price change for those types. |
| `ModifyCategoryCost` | `Category`; nonzero signed percentage `CostModifier` | Permanent price change for one sidebar category. |

GrantPassive `TargetTypes` accepts concrete IDs, `*`, `@Infantry`, `@Vehicle`, `@Aircraft`, and `@Building`; lists may mix them. Unknown `@` categories are invalid. These selectors are not universally supported by other fields with the same name. Cost targeting requires real type IDs.

Cost categories: `Building` means ordinary buildings; `Defense` means `BuildCat=Combat` buildings; `Infantry` means infantry; `Vehicle` includes ground vehicles, ships, and aircraft. After native country/factory modifiers, add all research percentage deltas, then fixed credit deltas, round to nearest integer, and clamp at zero. A native quote of 700 with `-20%` and `-100` becomes 460. Production orders lock their price when started; cancellation refunds the paid amount.

Effects lists deduplicate repeated effect section IDs while preserving first occurrence. Each effect section must explicitly define its `Effect`; it does not inherit the parent project's fields. Any invalid effect makes the project unavailable. At purchase time, temporarily inapplicable effects are skipped; at least one successful effect is needed to charge points and record ownership. Executed effects are not rolled back if a later effect fails, and skipped instant effects are not granted later.

Use canonical `UnlockTech`. The parser retains `UnlockTeck` as a compatibility spelling, but new configuration should not generate it. The legacy project IDs `SF_Repair`, `SF_Drain`, and `SF_Riposte` have built-in text/passive defaults; they still need registration and usable referenced passives. Define all fields explicitly for new projects.

### Selection, refresh, and global settings

Candidates exclude owned, invalid, wrong-country, and unmet-prerequisite projects. First select one nonempty rarity according to its weight, then offer up to three projects from that rarity. Opening the local panel does not consume the simulation RNG. Chosen project IDs, seen cards, and used refresh slots are saved.

Refresh requires a balance of at least 100, but is free. Each card slot may refresh once per offer round, only to an eligible unseen project of the same rarity. No alternative means no refresh allowance is consumed. Closing/reopening retains the board. Purchasing starts the next round.

Fields in `[Starfall.Research]`:

| Field | Default | Constraint / behavior |
| --- | --- | --- |
| `Enabled` | `yes` | `yes,true,1` enable; `no,false,0` disable research UI, point awards, purchases, and free building research. Stored ownership/points are retained. |
| `AllowCardRefresh` | `yes` | Same boolean forms; controls ordinary card refresh. |
| `Title` | Built-in Chinese text meaning “Choose an upgrade” | Set explicitly for an English interface. |
| `InitialPoints` | `0` | Nonnegative integer initial points per participating house. |
| `PeriodicPoints` | `0` | Nonnegative integer award each period. |
| `PeriodicIntervalSeconds` | `0` | Nonnegative integer game seconds, at most `INT_MAX/15`; both amount and interval must be positive to enable. |
| `RarityWeight.Common` | `5` | Nonnegative integer. |
| `RarityWeight.Rare` | `4` | Nonnegative integer. |
| `RarityWeight.Epic` | `1` | Nonnegative integer. |
| `RarityWeight.Insane` | `0` | Nonnegative integer; zero disables this rarity. |

Initial/periodic awards include human and AI houses in player slots; neutral, defeated, and observer houses are excluded. Campaign handling also considers the current player. Pause does not advance the timer. Saves preserve progress and prevent duplicate initial awards; older saves lacking that record do not receive a retroactive initial award.

**Complete research configuration**, assuming `MTNK` exists:

```ini
[Starfall.PassiveTypes]
0=SF_ResearchRepair

[SF_ResearchRepair]
Trigger=OnKill
Filter.TargetHouse=Enemies
Action=Heal
Action.Target=Self
Action.Amount=50

[Starfall.ResearchTypes]
0=SF_FieldRepair
1=SF_SupplyPackage

[Starfall.Research]
Title=Choose an upgrade
InitialPoints=100
PeriodicPoints=10
PeriodicIntervalSeconds=30

[SF_FieldRepair]
Name=Field Repair
Summary=Owned tanks heal after a kill.
Rarity=Common
Effect=GrantPassive
Passive=SF_ResearchRepair
TargetTypes=MTNK

[SF_SupplyPackage]
Name=Supply Package
Rarity=Common
Effects=SF_SupplyMoney,SF_SupplyDiscount

[SF_SupplyMoney]
Effect=GiveCredits
Credits=500

[SF_SupplyDiscount]
Effect=ModifyTypeCost
TargetTypes=MTNK
CostModifier=-10%
```

Do not also bind `SF_ResearchRepair` to `MTNK` if the intended design requires research first.

### Buildings and free research

| Building-type field | Default | Behavior |
| --- | --- | --- |
| `Starfall.ResearchPoints` | `0` | Positive points per completed building interval. |
| `Starfall.ResearchInterval` | `150` | Positive frames; both fields must be positive. |
| `Starfall.ResearchOptions` | empty | One to three registered project IDs; more than three invalidates the reward list. |
| `Starfall.ResearchOptions.AllowEngineerCapture` | `no` | Permit reward authorization after successful engineer capture. |
| `Starfall.ResearchOptions.AllowSpyInfiltration` | `no` | Permit authorization for the spy's owner after successful infiltration. |

Each working building has its own income clock. EMP, offline power, or chrono removal pauses it; construction, sale, or leaving the map clears progress. Ownership change restarts the clock for the new owner.

Each physical reward building can be claimed once. Ownership change invalidates old authorization; generic ownership transfer is not a substitute for an authorized capture/infiltration. Claiming is free and ignores project country/prerequisite restrictions, but still requires a valid unowned project and applicable effect. Existing claimed state persists. Older unclaimed authorization without a recorded acquisition source is cleared on load. The building must separately support native capture/infiltration mechanics.

## External grants and map integration

### Warhead grants

| Warhead field | Default | Meaning |
| --- | --- | --- |
| `Starfall.GrantPassives` | none | Registered passive ID list; one invalid ID disables the rule. |
| `Starfall.GrantPassives.Duration` | `-1` | Permanent, or positive frames. |
| `Starfall.GrantPassives.StackOnReapply` | `no` | `yes` / `no`; existing ownership adds one manual layer when enabled. |
| `Starfall.GrantPassives.Houses` | `All` | List: `All,Self,Allies,Enemies,Neutral`, relative to firing owner and constrained by warhead ownership flags. |

`CellSpread=0` grants only to a directly hit unit, not an empty ground point. Positive spread tests eligible recipients around impact; buildings use occupied cells. Zero-damage support hits can grant. Dead/off-map/iron-curtained recipients and zero-verses armor matches are excluded. A single detonation grants each passive at most once to each unit; independent detonations in the same frame remain independent. It does not make an otherwise untargetable unit targetable by the support weapon.

### Grant presets

**Fragment**, assuming `SF_KillHeal` is already registered:

```ini
[Starfall.PassiveGrants]
1=SF_FieldGrant

[SF_FieldGrant]
Passives=SF_KillHeal
Duration=450
TargetTypes=MTNK,HTNK
```

`Passives` is required. `Duration` defaults to permanent `-1`; positive values are frames. Optional `TargetTypes` is a list of concrete native type IDs, not the research `@Category` syntax. Omission allows all otherwise eligible types, including buildings. Grants affect currently living, on-map recipients, not future units.

### Map action numbers

Each example below is one action under an existing trigger's `[Actions]` entry. Replace the trigger ID, preserve the engine's action-record layout, and update the leading action count when merging multiple actions.

| Action | Parameter 3 | Parameter 4 | Purpose |
| --- | --- | --- | --- |
| `9600` | Positive point amount | House selector | Award research points. |
| `9601` | Grant preset number | Recipient selector | Apply one grant preset. |
| `9602` | Map-script preset number | House selector | Execute one map Lua callback. |

House selectors: `0` is trigger owner; `4475..4482` are player starting positions 1..8. Grant recipient selectors additionally accept `-1` for the trigger's associated object and `-2` for on-map units whose tags reference this trigger. For grants, house selectors select that house's current on-map units.

```ini
[Actions]
0F510005=1,9600,0,0,90,4475,0,0,A
0F510006=1,9601,0,0,1,4475,0,0,A
0F510007=1,9602,0,0,1,4475,0,0,A
```

These are three separate example trigger records, not a complete map. See [the research test map](../tests/maps/Starfall_ResearchPoints.map) for event/tag setup.

Team script action `9601` takes only the preset number and affects current eligible team members, then advances to the next line:

```ini
[MyTeamScript]
0=9601,1
```

### Map Lua preset

```ini
[Starfall.MapScripts]
1=SF_MapWave

[SF_MapWave]
Script=Defense.lua
Function=onWave
Script.Param.Credits=200
```

Map-script registry keys must be canonical nonnegative integers (`1`, not `01`). Referenced preset section IDs are limited to 100 bytes. `Script` and `Function` are required; the callback must exist in the discovered module. `Script.Param.*` values follow the Lua parameter rules below.

`Defense.lua` in the game root:

```lua
return {
    onWave = function(ctx)
        local house = ctx:house()
        if house and house.powerBalance >= 0 then
            ctx:action("GiveCredits", {
                Target = "Owner", Amount = ctx.params.credits,
            })
        end
    end,
}
```

Map callbacks have `ctx.owner`, optional associated `ctx.self`, optional valid `ctx.point`, no initial `ctx.target`, and no `ctx.superWeapon`. Delayed map callbacks survive removal of the associated unit; `ctx.self` then becomes nil. Missing presets/owners are logged. They use sequence-context action restrictions, not ordinary event-target assumptions.

## Active skills

Active skills share `[Starfall.PassiveTypes]` registration and passive ownership sources. They require `Trigger=Active` and exactly one `Action=RunScript`. The accepted parent keys are `Trigger`, `Cooldown`, `Action`, `Action.Script`, `Action.Function`, `Script.Param.*`, the `Active.*` keys below, `GainAnim`, and `Tint.Color`. Do not add ordinary filters, Chance, Actions, state conditions, or Stack.Max to an active definition.

| Field | Default | Constraint |
| --- | --- | --- |
| `Active.Name` | Skill ID | Display label. |
| `Active.Icon` | built-in fallback | PNG/PCX resource path. |
| `Active.CastMode` | `Single` | `Single` or `All`. Single picks the closest eligible selected caster, with unit ID as tie-breaker. |
| `Active.InterruptOrders` | `yes` | `yes` / `no`. Accepted casts may stop current movement/attack orders. |
| `Active.Target` | `None` | `None,Unit,Point,Area,Direction`. |
| `Active.Range` | `0` | Integer `0..128`; zero unlimited for Unit/Point/Area. Must be zero for None/Direction. |
| `Active.Houses` | all | List `Self,Allies,Enemies,Neutral,All`; Unit target only. |
| `Active.Radius` | `0` | Required positive `1..128` for Area; otherwise must be zero. |
| `Active.Shape` | circle behavior | If supplied, must be `Circle` with Area. |
| `Active.Distance` | `0` | Required positive `1..128` for Direction; otherwise must be zero. |
| `Cooldown` | `0` | Nonnegative frames, independently tracked per holder. |

Casters must belong to the issuing player, hold the skill, be alive/on-map/outside transports, and be free of incompatible EMP, forced movement, sinking, or removal states. Invalid/duplicate/foreign commands are rejected. Unit targets obey relevant cloak/disguise visibility rules. Direction clicks cannot equal the caster's cell; the endpoint is shortened at the map edge.

Same-ID skills share a UI button in mixed selection. All mode issues separate casts for eligible selected holders. Left click confirms a target; right click cancels. A successful native action, persistent-state change, or task scheduling can count as callback success; returning `true` from Lua alone does not. Cooldown starts on reported success. `Active.InterruptOrders=yes` interrupts after caster/aim validation and before running the script, so a script that ultimately makes no change can still have interrupted orders.

**Complete active example**, assuming `MTNK` exists:

```ini
[Starfall.PassiveTypes]
0=SF_Recall

[MTNK]
Starfall.Passives=SF_Recall

[SF_Recall]
Trigger=Active
Active.Name=Recall
Active.Target=Point
Active.Range=12
Active.CastMode=Single
Cooldown=300
Action=RunScript
Action.Script=Recall.lua
Action.Function=onCast
```

`Recall.lua`:

```lua
return {
    onCast = function(ctx)
        if ctx.self and ctx.self:alive() and ctx.point then
            ctx:action("ChronoTeleport", {
                Target = ctx.self, Destination = ctx.point, SearchRadius = 3,
            })
        end
    end,
}
```

For Area, `ctx.area={center=point,radius=...}`. For Direction, `ctx.point` is the projected endpoint and `ctx.direction={x=...,y=...}` is the unit direction vector. Copy any of these values into task data if delayed callbacks need the original aim.

## Lua API

### Module lifecycle and configuration

The embedded runtime is Lua 5.4.8. Each UTF-8 `.lua` file must return a table of callback functions. A UTF-8 BOM is accepted. Files are discovered from the game root at setup/load, with a 1 MiB per-file limit. `Action.Script` can omit `.lua`. INI function lookup is case-insensitive but rejects ambiguous names differing only by case; scheduled callback names must exactly match a returned function name.

Every callback creates a fresh Lua state and runs the cached module prototype again. Module/global changes are not persistent. Do not use module initialization for game mutations or assume an initialized `ctx` exists at top level. Persistent data belongs in the interfaces below.

`Script.Param.<Name>` is read from the executing parent skill or sequence step, not automatically from an unrelated reusable action section. It appears as `ctx.params.<lowercase-name>`. Numeric strings become Lua numbers; other values remain case-preserved strings. Thus an INI parameter `yes` is a string, not a Lua boolean. Scheduled callbacks use the definition's current parameters.

Legacy Lua metadata (`id`, `trigger`, `callback`, `units`, `cooldown`, `interval`, `signal`, and active targeting metadata) is still recognized. Explicit INI registration of the same ID takes precedence. New authoring should use INI registration and a returned function table.

### Context values

| Value | Availability / semantics |
| --- | --- |
| `ctx.self` | Holder handle; can be absent in casterless contexts. A death-event handle can exist while `:alive()` is false. |
| `ctx.target` | Counterpart handle, or nil; not the last unit passed to an action. |
| `ctx.damage` | Current event damage; successful interception actions update it. Ordinary scheduled callbacks are new contexts, not pending-damage interception. |
| `ctx.stack` | Executing passive's event/current task stack value. |
| `ctx.event.selfHealth`, `ctx.event.selfMaxHealth` | Snapshot values. |
| `ctx.event.targetHealth`, `ctx.event.targetMaxHealth` | Present when a counterpart health snapshot exists. |
| `ctx.event.targetPoint` | Recorded target cell for supported OnFire events, including ground fire. |
| `ctx.params` | Read parameters described above. |
| `ctx.frame` | Current simulation frame. |
| `ctx.taskId` | Present during a scheduled callback. |
| `ctx.point`, `ctx.area`, `ctx.direction` | Active targeting data where applicable; not automatically preserved into ordinary scheduled callbacks. |
| `ctx.owner` | Sequence/map launching house index. For ordinary unit callbacks use `ctx:house()` or `ctx.self:owner()`. |
| `ctx.superWeapon`, `ctx.castId` | Sequence identity/cast number; map contexts have castId but omit superWeapon. |
| `ctx.state` | Persistent state proxy; see below. |

There is no general `ctx.sender`, `ctx.trigger`, `ctx.attacker`, `ctx.victim`, or arbitrary engine object API. Formula `Sender` does not imply an identically named Lua property.

### Unit handles and house snapshots

All unit methods use `:`. `unit:id()` returns its stable ID; `unit:alive()` checks current validity. For an invalid unit, other property methods return nil. Handles support equality by unit ID.

| Method | Result |
| --- | --- |
| `unit:health()`, `unit:maxHealth()` | Current live health / extended maximum. |
| `unit:ammo()` | Live ammo; `-1` for unlimited. |
| `unit:passengers()` | Current direct passenger count. |
| `unit:rank()` | `0` rookie, `1` veteran, `2` elite. |
| `unit:type()` | Native type ID string. |
| `unit:kind()` | Lowercase category such as `infantry`, `vehicle`, `aircraft`, `building`. |
| `unit:cost()` | Current type's base cost. |
| `unit:owner()` | House array index, or `-1` if owner absent. |
| `unit:trainable()` | Native trainability boolean. |
| `unit:hasPassive(id)` | Whether this live unit currently owns the passive. |
| `unit:stack(id)` | Current effective layers; zero if not held. |
| `unit:position()` | Integer `{x=...,y=...}` map cell, or nil off-map/in limbo. |

`ctx:house(index)` returns a new snapshot with `index`, `powerOutput`, `powerDrain`, `powerBalance`, `credits`, and `researchPoints`. Omit index for the holder/launching owner. Invalid index returns nil. Call again to obtain updated values; modifying the returned table does not alter the house.

### Queries and action calls

| Method | Contract |
| --- | --- |
| `ctx:nearbyUnits(center, radius, filters)` | Center is a unit handle or integer point. Finite radius `0..128` cells. Returns a dense array sorted by unit ID. |
| `ctx:action(name, args)` | Execute one native Starfall action; returns success boolean. |
| `ctx:actions(list)` | Ordered array of tables containing `Action` and action fields; returns true if any succeeds. Shared action context supports consume-then-steal. |
| `ctx:runAction(section, overrides)` | Execute an existing INI single-action section. Overrides are limited to Target, Destination, and ForceMove-specific fields for a ForceMove section. |

Nearby filters use exact Lua keys `houses`, `types`, `includeSelf`. Houses defaults to `All`; types defaults to `*`; both accept a comma-separated string or array of strings. Types accepts research-style `@Category` selectors. A unit center is excluded unless `includeSelf=true`; for a point center there is no center unit to exclude. Relationships are relative to the executing owner. This query is a simulation query, not a general local visibility filter.

Action argument keys are case-insensitive. Prefer `Amount`, `Target`, `Passive`, etc. without the `Action.` prefix. Lua boolean fields map to yes/no. `Target` can be a unit handle, a valid symbolic name for the context, or a point for positional actions. `Destination` can be an integer point for ChronoTeleport. ForceMove accepts shorthand `Offset,Mode,Center,Distance,Height,Duration,Tilt,Easing,IgnoreCollision,BlowWeight,AffectsIronCurtain`.

Do not supply aliases for the same field twice, including capitalization variants or `Duration` together with `ForceMove.Duration`. Such conflicts are rejected to prevent Lua table iteration order from deciding the result. Unknown action keys are errors. `runAction` cannot override arbitrary amounts or switch the action type.

Lua actions cannot recursively invoke `RunScript` or use `ReplaceWeapon`. Damage-interception callbacks can only use `SetDamage`/`BlockDamage` through the native action entry. Sequence/map callbacks reject actions requiring unsupported caster context, including ReplaceWeapon, SetDamage, BlockDamage, SelfDestruct, StealPassive, and SendSignal. Supported live-unit actions in sequences need an explicit unit handle.

**Callback fragment**:

```lua
local targets = ctx:nearbyUnits(ctx.self, 5, {
    houses = {"Self", "Allies"}, types = {"@Infantry"},
})
for _, unit in ipairs(targets) do
    if unit:trainable() then
        ctx:action("Promote", {Target = unit, Levels = 1})
    end
end
```

### Timers, state, and synchronized randomness

| Method / value | Contract |
| --- | --- |
| `ctx:schedule(frames, callbackName, data)` | Positive integer delay, at most INT_MAX and without scenario-frame overflow. Return task ID; callback receives `(ctx,data)`. |
| `ctx:cancel(id)` | Cancel a pending task belonging to this owner/skill context. Returns false for already executed, canceled, or foreign tasks. |
| `ctx.state.key` | Read a saved value copy. Keys are strings of 1..127 bytes. |
| `ctx.state.key = value` | Store a copied value; nil deletes the key. |
| `ctx:randomInt(min,max)` | Inclusive ordered signed 32-bit integer range. |
| `ctx:randomPoint(area)` | `area={center=integerPoint,radius=integer}`; radius `0..128`; uniform eligible cell selection in the map-clipped circle. |

Persistable values: nil, boolean, integer, finite number, string, Starfall unit handle, and plain tables with integer or string keys. No functions, cycles, metatables, or other userdata. A value tree allows at most 256 nodes, depth 12, and an 8 KiB aggregate string/key budget. Unit handles preserve IDs, not a guarantee that the object will still exist.

Reading a table from state returns a copy. This does **not** save an update:

```lua
local progress = ctx.state.progress
progress.count = progress.count + 1
-- Write it back explicitly:
ctx.state.progress = progress
```

That fragment requires `ctx.state.progress` to have been initialized. Ordinary state is scoped by holder and skill. Sequence/map state is scoped by cast; multiple sequence steps in one cast share the cast state. Animation/task ownership additionally uses the creating step/skill where required.

Timers due on the same frame execute in creation order within their store. Ordinary holder tasks/state are removed when the holder is removed. Losing the skill alone does not cancel queued tasks while its definition remains available. Delayed callbacks resolve current objects and current parameters; store original event numbers/aim in `data` if needed. A removed original counterpart becomes nil for the delayed context.

There are separate ordinary-unit and sequence/map timer stores, each limited to 1,024 pending tasks and 2,048 saved state keys. The Lua random stream is synchronized, saved, and separate from native weapon RNG. Do not branch simulation changes on local UI visibility, audio results, external time, or other client-specific observations.

**Complete module** for a delayed heal, called by a compatible RunScript skill:

```lua
local skill = {}

function skill.start(ctx)
    if not ctx.self or not ctx.self:alive() then return end
    local pending = ctx.state.pending
    if pending then ctx:cancel(pending) end
    ctx.state.pending = ctx:schedule(30, "finish", {amount = 25})
end

function skill.finish(ctx, data)
    ctx.state.pending = nil
    if ctx.self and ctx.self:alive() then
        ctx:action("Heal", {Target = ctx.self, Amount = data.amount})
    end
end

return skill
```

### Movable animation API

| Method | Contract |
| --- | --- |
| `ctx:spawnAnim(id, point, options)` | Create registered animation; return integer handle or nil for invalid type/resource/location. Point may have fractional cells. |
| `ctx:moveAnim(handle, point, frames)` | Replace movement with linear travel over integer `1..54000` frames; keep height offset. Return success. |
| `ctx:removeAnim(handle)` | Remove an animation owned by this context; nil/expired/foreign handles return false. |

Options use exact Lua keys: `loop=false`, `duration=150` (integer `1..54000` frames), `height=0` (finite `-64..64` cells above ground). At most 2,048 managed Lua animations exist globally. A unit/skill owns its animations; sequence casts/steps own theirs. Save/load retains lifetime and movement. Holder removal cleans ordinary animations; losing the passive alone does not. Casterless sequence animations can continue until duration or explicit removal. A non-looping native animation may naturally finish before duration. Animation handles are plain integers that can be stored in data/state; they do not apply damage or displacement themselves.

### Runtime limits and unavailable libraries

Each module load/callback has an 8 MiB Lua allocator limit and a hook budget of approximately 100,000 Lua VM instructions. Native action/query work has a 4,096-operation budget per callback; query cost can exceed one operation. These are failure limits, not performance targets.

Available libraries are a restricted base environment plus `table`, `string`, `math`, and `utf8`. `ipairs` is available. The following are unavailable: `load`, `loadfile`, `dofile`, `collectgarbage`, `print`, `warn`, `getmetatable`, `setmetatable`, `pcall`, `xpcall`, `pairs`, `next`, `tostring`, `rawset`, `math.random`, `math.randomseed`, `string.dump`, and `string.format`. The runtime does not open `io`, `os`, `package`, `debug`, or `coroutine`; `require` is not provided.

Syntax/runtime errors include script location in `Starfall.log`. An error ends that callback; already executed actions are not rolled back. Lua return values are ignored by the callback dispatcher. Use supported state/action/task operations to produce effects, not a return value as an assumed success flag.

## ActionSequence superweapons

`Type=ActionSequence` is provided by Starfall and can run without Ares/Phobos. Each native registered superweapon entry retains its own sidebar identity and recharge. Native buildings grant it through `SuperWeapon` or `SuperWeapon2`.

| Field | Contract |
| --- | --- |
| `Type` | `ActionSequence`. |
| `Starfall.Actions` | Required nonempty ordered action-section list; duplicate steps remain steps. |
| `Starfall.Target` | `Point` only; default point behavior. |
| `RechargeTime` | Native game minutes, not Starfall frames. |

Direct INI steps support positional actions targeting `Point` (or omitted target), money/research-point actions targeting `Owner` (or omitted target), and `RunScript` with no explicit target. SpawnUnit owner may use `Owner`/`Self`, `Neutral`, or player-slot aliases. To heal, damage, or move particular units from Lua, first obtain live handles and pass them explicitly.

**Native rules fragment**, assuming the referenced native names exist:

```ini
[SuperWeaponTypes]
SF01=FieldSupply

[GAPILE]
SuperWeapon=FieldSupply

[FieldSupply]
Type=ActionSequence
UIName=Name:MTNK
SidebarImage=GTNKICON
RechargeTime=1
IsPowered=no
Starfall.Actions=SF_DropTank,SF_SupplyCredits

[SF_DropTank]
Action=SpawnUnit
Action.Target=Point
Action.UnitType=MTNK
Action.Owner=Owner

[SF_SupplyCredits]
Action=GiveCredits
Action.Target=Owner
Action.Amount=500
```

Sequence Lua receives `ctx.point`, `ctx.owner`, `ctx.superWeapon`, and `ctx.castId`; initial self/target are nil. Timers and state are associated with the cast, not an imaginary caster unit. Cast state is retired when no pending tasks remain; persistent animations follow their own ownership/lifetime rules. See [Rules.ini](../examples/ActionSequence/Rules.ini) and [GravityWell.lua](../examples/ActionSequence/GravityWell.lua).

## Animation, icons, and audio

### Passive visual fields

| Field | Default | Behavior |
| --- | --- | --- |
| `Anim` | none | Attached looping animation; follows holder and its drawing scale. |
| `Anim.Always` | `no` | `yes` ignores passive state conditions for the attached animation; still subject to visibility/lifetime. Requires Anim. |
| `GainAnim` | none | Play once when newly acquired on-map; transport acquisition waits until on-map. |
| `TriggerAnim` | none | Play after trigger checks, before the action group; for WhileState, on state entry after its wait. |
| `TriggerAnim.Target` | `Self` | `Self` or `Target`; unavailable target/location skips playback. |
| `Tint.Color` | none | Three integers `R,G,B`, each `0..255`. |

Registered animation IDs must exist. A visuals-only passive can omit Trigger when its section contains only supported Anim/GainAnim/Tint/Stack.Max fields. A conditional visuals-only passive can use WhileState with a condition. An event can omit actions when TriggerAnim supplies the visual result. For an action-bearing event, animation playback alone does not replace the action-success cooldown rule.

Attached animations hide while cloaked/off-map and disappear when ownership is lost. Multiple tints combine by component-wise maxima; cloak hides them, and iron curtain/force shield takes visual precedence. Tint does not alter damage or ownership. Passive visuals, state-entry tracking, and supported animation state persist across saves.

### Icons, text, and audio

Research and active icons accept PNG and engine-loadable PCX. PNG transparency is supported; invalid/missing resources use the appropriate built-in fallback. Research rarity colors: Common preserves original colors, Rare uses blue/purple, Epic gold, Insane red. Keep UI resource names separate from animation IDs.

For generated single-passive research details, supported placeholders include `{amount}`, `{percent}`, `{count}`, `{chance}`, `{cooldown}`. A multi-action passive's custom amount/percent placeholders refer to its first action. GiveCredits supports `{credits}`. Add a literal percent sign yourself, such as `{percent}%`. Custom WhileState details and a combined research project's custom Detail are displayed literally rather than expanded per child effect.

Fields in `[Starfall.ResearchSounds]`: `Button`, `Cards`, `Refresh`, `Select`, `Available`. Each defaults to empty. Accepted built-ins are `builtin:cards`, `builtin:select`, and `builtin:available`, or an external WAV path. Empty Button uses native click audio; other empty fields are silent. Failed Button audio falls back to native click audio.

External WAV: uncompressed PCM, 8/16-bit, mono/stereo, `8000..48000` Hz, at most 16 MiB. Paths can be absolute or relative to the game root. Action PlaySound instead uses a native sound ID or `wav:<path>`. Custom action WAV plays only for a local visible/unshrouded position, at the configured effects volume; missing local audio must not control gameplay. Local shake respects the viewport and applicable `HideShakeEffects` setting. Fire/railgun particle systems require distinct source and destination positions.

Cache behavior is internal, not additional INI configuration:

| Cache | Bound |
| --- | --- |
| Decoded icon/card images | 256 entries and 64 MiB of accounted data. |
| WAV samples | 64 entries and 32 MiB of accounted data. |
| Current scaled card surface | One surface, at most 1,048,576 16-bit pixels (2 MiB). |
| Rendered text | 256 entries and 4 MiB. |
| Simultaneously managed audio voices | 8. |

Image/audio caches use bounded LRU behavior; failed loads also occupy bounded entries. Oversized valid resources can be used transiently. Reloading research configuration clears image/card/audio caches; in-progress sounds retain independent playback buffers. Scenario/load reset stops old voices. These limits do not cap total process memory, temporary decoding, or the game's own PCX cache. There is no documented user-facing hot-reload command; do not invent one.

### Native animation references

Use an animation type already registered by the game or mod, with a valid native image. `Anim`, `Action.Anim`, and `ctx:spawnAnim` reference the animation type ID, not an image filename. Register custom animation types through the game's normal rules and art configuration before using them.

The Tornado example uses the native `PIFF` animation as a placeholder. Replace `Script.Param.Anim` with your mod's registered animation ID to supply its artwork; the movement, damage, and Lua animation APIs work independently of that choice.

## Persistence, multiplayer, and compatibility

### Save/load contract

Starfall stores extension data in the `Starfall.Passives` stream. The current marker is `0x66504653`, which adds active cloaking-capability IDs. The previous `0x65504653` layout remains readable. Saves written with the new marker require this implementation or a compatible newer reader. This document is not a full binary save-format specification; use the Writer/Reader/saveState/loadState implementation before editing save code.

The stream covers relevant passive counts/cooldowns/stacks and snapshots, grants and remaining durations, continuous state progress, extended health/stat changes, research ownership/points/offers/refreshes/building rewards, supported motion/animations, sequence/map context, Lua timers/state/RNG, and associated identifiers. Not every cache is serialized; derived presentation and lookup caches are rebuilt or cleared.

Loading validates and stages extension state before committing. An existing malformed extension stream is rejected rather than partially applied. Missing extension data is handled separately for older native saves. Some older Starfall layouts are supported, but no claim is made that every historical build or arbitrary changed configuration can load safely. Keep project/passive IDs stable and keep compatible scripts available for saved callbacks.

### Multiplayer contract

All peers must use the same Starfall DLL, gameplay rules, maps, scripts, and required animation/resources. Local UI selection sends commands that are revalidated in simulation; direct local input or drawing code must not mutate shared gameplay state.

Use `ctx:randomInt` and `ctx:randomPoint` for Lua randomness. Do not use external randomness, unordered iteration, clocks, files, or client-specific conditions for simulation decisions. Script errors retain earlier effects; identical error behavior is not a substitute for valid scripts.

The repository includes real two-client tests with frame-by-frame state comparisons, plus impaired-network and negative-divergence modes. These test specific scenarios and dependency combinations. They do not establish that every Lua example, arbitrary mod combination, long session, or multiplayer save/load path has been covered. Current recorded evidence is linked in the test section.

### Unit conversion and extension scope

Without Phobos, Starfall uses its native conversion path. With Phobos loaded, it requires the exported `ConvertToType_Phobos` interface; absence disables conversion. It does not require Ares merely to transform units.

Conversion requires different source/destination types of the same supported object class and matching Locomotor. It preserves identity, ownership, rank, passive state, proportional health, and extra maximum-health contribution where supported. A research conversion affects only current eligible units, never future production. At least one successful conversion is required for a conversion-only purchase.

The native fallback rejects unsupported spawner/slave/manager combinations and incompatible mind-control/parasite/chrono states. Cargo must fit the destination's passenger capacity and size constraints; occupied transports require compatible open-topped/gunner behavior. Units must be alive, on-map, and valid for their destination terrain/state. Third-party type-attached data is not automatically compatible with every native conversion.

Mind-control immunity includes the supported native controller paths and the adapted Ares 3.0p1 permanent-control path. Unsupported entrypoints/versions are logged; this is not blanket compatibility with every Ares/Phobos build. Consult the current review records and test the exact loader/extensions used by a mod.

## Diagnostics and removed interfaces

Start with game-root `Starfall.log`. Research diagnostics include section, field, value, reason, and a file line number when available. Merged-rule diagnostics cannot always identify the original include/map file. Lua diagnostics include file/function and traceback location. A logged invalid passive can cause dependent grants/projects to become unavailable as well.

| Symptom | Check first |
| --- | --- |
| Passive never executes | Registry, exact ID, definition validity, unit binding/grant, counterpart filters, state condition, count/cooldown, and target viability. |
| WhileState is rejected | Missing condition; event Action/Filter/Chance fields mixed in; unknown Modifier; missing visual/modifier result. |
| Formula fails | Supported destination field, `$()` wrapper, variable context, absent target, division by zero, and result range. |
| No research cards | Enabled, registration, valid effect, rarity weights, country, prerequisites, ownership, and referenced passives. |
| A card cannot be purchased | Balance at least 100, current offered ID, unmet immediate conversion/technology applicability, duplicate command/ownership. |
| Refresh unavailable | Balance threshold, global switch, slot already used, or no unseen same-rarity candidate. |
| Active skill is missing/rejected | Registered/held ID, restricted Active schema, single RunScript action, discovered function, caster state, target/range. |
| Lua state disappears | Module globals used instead of ctx.state; state table modified without writeback; holder/cast retired. |
| Delayed callback lacks original aim/damage | Store it explicitly in task data; scheduled callbacks reconstruct their own context. |
| Icon/audio replacement is not visible | Correct path/format, cache invalidation at configuration/scenario boundary, and native PCX lookup. |
| A new option spelling seems ignored | Check the actual parser; do not infer fields from other mods or from a similar name. |

Removed or invalid authoring forms:

| Old / unsupported form | Replacement / rule |
| --- | --- |
| `Condition.SelfHealthAtLeast`, `Condition.SelfHealthAtMost`, `Condition.SelfHealthBelow` | `Condition.Formula` with health comparisons. |
| `Condition.AmmoAtLeast`, `Condition.AmmoAtMost` | Formula using `Self.Ammo`. |
| `Condition.PassengersAtLeast`, `Condition.PassengersAtMost` | Formula using `Self.Passengers`. |
| `Modifier.PowerOutputMultiplier`, `Modifier.PowerDrainMultiplier` | `Modifier.PowerMultiplier`. |
| `Trigger=OnCast` | `Trigger=Active`. |
| `[Starfall.Research] Options=...` | Register projects in `[Starfall.ResearchTypes]`. |
| Arbitrary `Action.*` names, conflicting Lua aliases | Use only documented fields once. |
| `Action` together with `Actions` | Choose one schema per parent. |
| `math.random`, `pairs`, persistent module globals | Synchronized RNG, ordered arrays, ctx.state/task data. |

The old separate shield-style configuration is not an active documented feature. Use the current supported immunity/iron-curtain/damage-interception interfaces according to the desired behavior; they are not interchangeable shields.

## Build, tests, and source map

Commands run from the repository root on Windows:

```bat
build.cmd
tests\unit\build.cmd
tests\build.cmd research-hud
```

The unified test entry point is `tests/run.ps1`; suites and engine expectations live in `tests/cases.json`. The default `quick` suite checks the test infrastructure and runs all six standalone programs without launching the game:

```powershell
.\tests\run.ps1
.\tests\run.ps1 -List
.\tests\run.ps1 -Suite smoke -Plan
.\tests\run.ps1 -Case cloakable,health-formula
.\tests\run.ps1 -Suite network
```

For game tests, copy `tests/environment.example.json` to the ignored `tests/local/environment.json` and set local paths, or pass them explicitly. Existing direct runners remain supported:

```powershell
.\tests\run-game.ps1 -Case research-hud -GameDirectory 'D:\Games\Yuri'
.\tests\run-multiplayer.ps1 `
    -GameDirectory 'D:\Games\Yuri' `
    -PhobosPackageDirectory 'D:\Packages\Phobos' `
    -StandaloneSpawner 'D:\Packages\CnCNet-Spawner.dll'
```

The automated runners build dedicated probe DLLs and use isolated run directories. Compilation is cached by source content, toolchain signature, and output hashes; tests always execute. Common libraries are shared, and immutable `.mix` archives use hard links when possible. Settings, DLLs, scripts, maps, and saves remain separate. Each suite writes logs and a JSON summary under `tests/local/records/suite-<time>-<id>/`; failures produce a failing exit status. `tests/clean.ps1` previews disposable artifacts, and `-Apply` removes them while retaining diagnostics and the reusable game base.

Do not install a test DLL as a normal gameplay release. Read [tests/README.md](../tests/README.md) for requirements, case names, output records, and network profiles. Six standalone test programs cover Lua runtime, audio, text, generic resources, research model, and action arguments. Actual game tests cover engine-dependent behavior; a standalone pass is not a substitute for an in-game or multiplayer test.

Implementation index:

| Source | Responsibility |
| --- | --- |
| [Starfall.cpp](../src/Starfall.cpp) | Passive parsing, runtime state, events, actions, building research adapters, engine hooks, serialization, and runtime coordination. |
| [ConfigText.cpp](../src/ConfigText.cpp) | Shared text/number/list normalization and diagnostics. |
| [Research.h](../src/Research.h), [Research.cpp](../src/Research.cpp) | Research data types/state, points, offers, refresh, purchase, reset. |
| [ResearchConfig.cpp](../src/ResearchConfig.cpp) | Research file/merged-rule parsing and prerequisite validation. |
| [ResearchEffects.cpp](../src/ResearchEffects.cpp) | Research validation/execution, production permissions and prices. |
| [ResearchInternal.h](../src/ResearchInternal.h) | Narrow internal research/runtime interfaces. |
| [ResearchUI.cpp](../src/ResearchUI.cpp), [ActiveSkillUI.inc](../src/ActiveSkillUI.inc) | UI, input, image rendering. |
| [ActiveCast.inc](../src/ActiveCast.inc) | Simulation-side active-command validation/execution. |
| [HealthFormula.h](../src/HealthFormula.h), [UnitHealth.h](../src/UnitHealth.h) | Formula grammar/evaluation and extended health. |
| [LuaRuntime.h](../src/LuaRuntime.h), [LuaSkillActions.inc](../src/LuaSkillActions.inc) | Restricted Lua environment, contexts, actions, queries, scheduling. |
| [LuaTimers.h](../src/LuaTimers.h), [LuaTaskData.h](../src/LuaTaskData.h) | Saved task/state values and deterministic RNG. |
| [LuaNearby.inc](../src/LuaNearby.inc), [LuaAnim.inc](../src/LuaAnim.inc) | Query support and managed Lua animations. |
| [ActionSequence.inc](../src/ActionSequence.inc) | Superweapon/map scripts, casts, map overlay, sequence persistence. |
| [ForceMove.inc](../src/ForceMove.inc) | Forced displacement, eligibility, collision, and landing. |
| [VisualScale.cpp](../src/VisualScale.cpp) | Native SHP, voxel, projectile, and attached-animation scaling. |
| [ResourceCache.h](../src/ResourceCache.h), [ResearchAudio.h](../src/ResearchAudio.h), [ResearchText.h](../src/ResearchText.h) | Bounded presentation caches and audio/text helpers. |
| [module-sources.cmd](../tools/module-sources.cmd) | Shared separately compiled module list. |

Architecture and verification records: [module boundaries](architecture.md), [initial review](reviews/2026-11-14.md), [fixes](reviews/2026-11-14-fixes.md), [multiplayer](reviews/2026-11-14-multiplayer.md), [cache work](reviews/2026-11-14-cache.md), [module extraction](reviews/2026-11-14-modules.md). These dated records describe measured coverage and remaining limits; they are not promises about untested environments.

Examples: [Lua configuration](../examples/Lua/Starfall.ini), [AreaBarrage.lua](../examples/Lua/AreaBarrage.lua), [Tornado configuration](../examples/Tornado/Starfall.ini), [Tornado.lua](../examples/Tornado/Tornado.lua), [research example](../examples/Starfall.HexBrawl.ini).

## Credits

Creator: 斩断云天. Collaborator: ChatGPT, 贪吃的布丁. Acknowledgments: Westwood, the Ares authors, the Phobos team, and ra2jj.com.
