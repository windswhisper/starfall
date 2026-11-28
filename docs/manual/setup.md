# Installation and Configuration

[Manual home](../manual.md) · [Configuration reference](reference.md)

## Contents

- [Authoring contract](#authoring-contract)
- [Installation and configuration sources](#installation-and-configuration-sources)
- [Registries and a complete passive example](#registries-and-a-complete-passive-example)
- [Diagnostics and removed interfaces](#diagnostics-and-removed-interfaces)
- [Build, tests, and source map](#build-tests-and-source-map)

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
| Owned passive | A passive available through at least one source. Its effective stack count is at least one while it is held. |

## Installation and configuration sources

Build with Visual Studio 2022 / MSVC v143, x86 C++ tools, and a Windows SDK. Run `build.cmd` at the repository root. The output is `build/Starfall.dll`.

Place the DLL beside `gamemd.exe` and load it with a compatible Syringe loader. The handshake requires game executable CRC `0x54CC0A13`. The game and loader are not supplied by this project. With Phobos, load Starfall before Phobos; the automated tests use explicit extension order. See [compatibility](compatibility.md#persistence-multiplayer-and-compatibility) for conversion and mind-control restrictions.

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

```ini
[Starfall.PassiveTypes]
0=(passive or active skill section ID)
; Registers a passive or active skill definition.

[UnitTypeID]
Starfall.Passives=(comma-separated passive or active skill IDs)
; Gives the registered abilities to units of this type, regardless of owner.

[Starfall.ResearchTypes]
0=(research project section ID)
; Registers a research project.

[Starfall.PassiveGrants]
1=(grant preset section ID)
; Registers a grant preset. Map and team action 9601 use the numeric key.

[Starfall.MapScripts]
1=(map Lua preset section ID)
; Registers a map Lua preset. Map action 9602 uses the numeric key.

[SuperWeaponTypes]
SF01=(superweapon section ID)
; Registers a native superweapon that can use Type=ActionSequence.
```

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

## Diagnostics and removed interfaces

Start with game-root `Starfall.log`. Research diagnostics include section, field, value, reason, and a file line number when available. Merged-rule diagnostics cannot always identify the original include/map file. Lua diagnostics include file/function and traceback location. A logged invalid passive can cause dependent grants/projects to become unavailable as well.

| Symptom | Check first |
| --- | --- |
| Passive never executes | Registry, exact ID, definition validity, unit binding/grant, counterpart filters, state condition, count/cooldown, and target viability. |
| WhileState is rejected | Event Action/Filter/Chance fields mixed in; unknown Modifier; missing visual/modifier result. |
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

Do not install a test DLL as a normal gameplay release. Read [tests/README.md](../../tests/README.md) for requirements, case names, output records, and network profiles. Six standalone test programs cover Lua runtime, audio, text, generic resources, research model, and action arguments. Actual game tests cover engine-dependent behavior; a standalone pass is not a substitute for an in-game or multiplayer test.

Implementation index:

| Source | Responsibility |
| --- | --- |
| [Starfall.cpp](../../src/Starfall.cpp) | Scenario lifecycle, per-frame execution order, passive event dispatch, and runtime coordination. |
| [PassiveRules.cpp](../../src/PassiveRules.cpp) and [PassiveTypes.h](../../src/PassiveTypes.h) | Passive definitions, field parsing, and action validation. |
| [Runtime fragments](../../src/runtime/) | Passive registration, state updates, grant propagation, action execution, combat hooks, and save/load transactions. |
| [BuildingResearch.cpp](../../src/BuildingResearch.cpp) | Building and global research income, capture/infiltration rewards, and free research claims. |
| [ResearchDescription.cpp](../../src/ResearchDescription.cpp) | Research card conditions and effect descriptions. |
| [ConfigText.cpp](../../src/ConfigText.cpp) | Shared text/number/list normalization and diagnostics. |
| [Research.h](../../src/Research.h), [Research.cpp](../../src/Research.cpp) | Research data types/state, points, offers, refresh, purchase, reset. |
| [ResearchConfig.cpp](../../src/ResearchConfig.cpp) | Research file/merged-rule parsing and prerequisite validation. |
| [ResearchBonds.cpp](../../src/ResearchBonds.cpp) | Research bond membership, threshold transitions, and per-house reward history. |
| [ResearchStages.cpp](../../src/ResearchStages.cpp) | Counter-indexed stage evaluation, ordered rewards and persistent grants per house. |
| [ResearchEffects.cpp](../../src/ResearchEffects.cpp) | Research validation/execution, production permissions and prices. |
| [ResearchInternal.h](../../src/ResearchInternal.h) | Narrow internal research/runtime interfaces. |
| [ResearchUI.cpp](../../src/ResearchUI.cpp), [ActiveSkillUI.inc](../../src/ActiveSkillUI.inc) | UI, input, image rendering. |
| [ActiveCast.inc](../../src/ActiveCast.inc) | Simulation-side active-command validation/execution. |
| [HealthFormula.h](../../src/HealthFormula.h), [UnitHealth.h](../../src/UnitHealth.h) | Formula grammar/evaluation and extended health. |
| [LuaRuntime.h](../../src/LuaRuntime.h), [LuaSkillActions.inc](../../src/LuaSkillActions.inc) | Restricted Lua environment, contexts, actions, queries, scheduling. |
| [LuaTimers.h](../../src/LuaTimers.h), [LuaTaskData.h](../../src/LuaTaskData.h) | Saved task/state values and deterministic RNG. |
| [LuaNearby.inc](../../src/LuaNearby.inc), [LuaAnim.inc](../../src/LuaAnim.inc) | Query support and managed Lua animations. |
| [ActionSequence.cpp](../../src/ActionSequence.cpp) | Superweapon/map scripts, casts, map overlay, sequence persistence. |
| [ForceMove.cpp](../../src/ForceMove.cpp) | Forced displacement, eligibility, collision, and landing. |
| [BuildingSpawn.h](../../src/BuildingSpawn.h), [BuildingSpawn.cpp](../../src/BuildingSpawn.cpp) | Complete building-foundation checks and deterministic nearby placement for SpawnBuilding. |
| [BuildingTransform.h](../../src/BuildingTransform.h), [BuildingTransform.cpp](../../src/BuildingTransform.cpp) | Same-foundation validation, guarded native replacement and identity handoff for building Transform. |
| [VisualScale.cpp](../../src/VisualScale.cpp) | Native SHP, voxel, projectile, and attached-animation scaling. |
| [ResourceCache.h](../../src/ResourceCache.h), [ResearchAudio.h](../../src/ResearchAudio.h), [ResearchText.h](../../src/ResearchText.h) | Bounded presentation caches and audio/text helpers. |
| [module-sources.cmd](../../tools/module-sources.cmd) | Shared separately compiled module list. |

See [module boundaries](../architecture.md) for implementation responsibilities and the [test guide](../../tests/README.md) for reproducible verification.

The [example index](../../examples/README.md) covers passive repair, research choices, and scripted weapon volleys.
