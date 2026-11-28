# Starfall Lab Lua Guide

English reference for mod authors, AI assistants, and maintainers. Game time uses 15 simulation frames per second.

[Manual home](../manual.md) · [Configuration reference](reference.md)

## Contents

1. [Quick start](#quick-start)
2. [Script entry points](#script-entry-points)
   - [Passive event](#passive-event-scripts)
   - [Active skills](#active-skills)
   - [Weapon firing scripts](#weapon-firing-scripts)
   - [Warhead detonation](#run-lua-on-warhead-detonation)
   - [Map Lua preset](#map-lua-preset)
3. [Module lifecycle and configuration](#module-lifecycle-and-configuration)
4. [Context values](#context-values)
5. [Unit handles and house snapshots](#unit-handles-and-house-snapshots)
6. [Unit orders](#unit-orders)
7. [Queries and action calls](#queries-and-action-calls)
8. [Timers, state, and synchronized randomness](#timers-state-and-synchronized-randomness)
9. [Movable animation API](#movable-animation-api)
10. [Runtime limits and unavailable libraries](#runtime-limits-and-unavailable-libraries)
11. [Interface index](#interface-index)

## Quick start

This active skill teleports a selected Grizzly Tank to a clicked cell. Merge the registration into an unused entry and add the skill to the unit's existing passive list. Place the Lua file in the game root. In game, select the tank, click its Recall skill, and choose a valid cell within 12 cells. Successful execution starts a 300-frame cooldown. Check game-root `Starfall.log` for configuration and script errors.

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

`Recall.lua`

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

## Script entry points

| Entry point | Configuration | Context and execution |
| --- | --- | --- |
| Passive event | Registered passive with `Action=RunScript`, `Action.Script`, and `Action.Function`. | Uses the passive trigger and event context. See [trigger catalog](passives.md#trigger-catalog) and [damage interception](passives.md#synchronous-damage-interception). |
| Active skill | `Trigger=Active` with one `RunScript` action. | Player commands supply validated targeting data. See [active skills](#active-skills). |
| Weapon firing | Weapon `Starfall.FireScript` and optional `Starfall.FireFunction`. | One callback per accepted volley. See [weapon firing scripts](#weapon-firing-scripts). |
| Warhead detonation | Warhead `Starfall.DetonateScript` and optional `Starfall.DetonateFunction`. | Runs after each on-map detonation. See [warhead detonation](#run-lua-on-warhead-detonation). |
| Map trigger | Register a map Lua preset and invoke action `9602`. | Uses map/sequence context. See [map Lua preset](#map-lua-preset) and [map action numbers](map-integration.md#map-action-numbers). |
| ActionSequence | A `RunScript` sequence step. | Uses sequence context. See [ActionSequence superweapons](miscellaneous.md#actionsequence-superweapons). |

### Passive event scripts

Register the passive in `[Starfall.PassiveTypes]`, bind or grant it to the holder, and select an event with `Trigger`. Configure one `RunScript` action with `Action.Script` and `Action.Function`. The callback receives a context whose holder and counterpart follow that event's rules. Queued event callbacks and synchronous damage-interception callbacks have different action restrictions. See [trigger catalog](passives.md#trigger-catalog) and [synchronous damage interception](passives.md#synchronous-damage-interception).

```ini
[Starfall.PassiveTypes]
1=SF_LuaKillHeal

[MTNK]
Starfall.Passives=SF_LuaKillHeal

[SF_LuaKillHeal]
Trigger=OnKill
Action=RunScript
Action.Script=KillHeal.lua
Action.Function=onKill
```

`KillHeal.lua`

```lua
return {
    onKill = function(ctx)
        if ctx.self and ctx.self:alive() then
            ctx:action("Heal", {Target = ctx.self, Amount = 25})
        end
    end,
}
```

Merge the registry entry and the unit's passive list into the existing configuration. A qualifying kill queues the callback. The heal is applied when the callback runs and the holder is still alive.

### Weapon firing scripts

Set `Starfall.FireScript` on the weapon section to a Lua module in the game directory. The optional `Starfall.FireFunction` names an exported function and defaults to `onFire`. A map may override either field. The binding follows the weapon selected for the volley, including a weapon chosen by `OnBeforeFire`. No passive registration is required.

```ini
[105mm]
Starfall.FireScript=DoubleShot.lua
Starfall.FireFunction=onFire
```

In `DoubleShot.lua`, the first shot follows the target if it still exists. The second fires at the original cell six simulation frames later.

```lua
return {
    onFire = function(ctx)
        ctx:action("FireWeapon", {Weapon="120mm", Target=ctx.target or ctx.point})
        ctx:schedule(6, "followUp", ctx.point)
    end,

    followUp = function(ctx, savedPoint)
        ctx:action("FireWeapon", {Weapon="120mm", Target=savedPoint})
    end
}
```

- One accepted volley queues one callback even with `Burst>1`. The weapon supplies targeting rules and full-volley rearm time and consumes ammunition once. All projectiles in the volley come from the script. An ordinary `OnFire` passive receives one event for the volley.
- `ctx.self` is the shooter. `ctx.target` is the live targeted unit or building, or `nil` for ground fire or a target lost before dispatch. `ctx.point` and `ctx.event.targetPoint` both hold the target cell at firing time. Pass the point as timer data to reuse it later.
- Callbacks run at the simulation event dispatch. The shooter must still exist. Pending callbacks and timers survive save/load. An invalid module or function logs a configuration error and leaves ordinary firing enabled. A runtime error in an accepted volley logs the error and still consumes the volley.
- This entry covers the standard firing path. `Suicide`, `Spawner`, `LimboLaunch`, and `DiskLaser` weapons are rejected at registration. The Lua `FireWeapon` action emits its named projectile directly and can use the same weapon ID without recursively starting a volley.

A Lua `FireWeapon` call can set its own launch point for one shot:

```lua
local p = ctx.point
ctx:action("FireWeapon", {
    Weapon = "PrismShot",
    Target = ctx.target or p,
    Origin = {x = p.x - 1.5, y = p.y, height = 6},
})
```

`Origin` accepts a playable map point or a live on-map unit handle. Point coordinates use cells with integers at cell centers. Fractional X/Y allow subcell placement. Optional `height` uses cells above the terrain or bridge surface directly under the point (default 0, one cell = 256 leptons). A handle uses the unit's actual position, including flight height. Invalid points and unavailable handles raise a Lua error before firing. The origin applies only to this shot and takes precedence over the weapon's `FireOrigin` and offset. Omitting it uses the normal weapon settings. The target, projectile tracking and damage owner retain their usual meanings. This option covers the same conventional weapons as [target-relative weapon origins](actsp.md#target-relative-weapon-origin); unsupported weapons return false with an explicit origin.


### Run Lua on warhead detonation

Place these keys on a registered warhead. The module must export the named Lua function. The default function is `onDetonate`. Rules, Starfall.ini and map overrides are supported, with the map taking precedence.

```ini
[MyBlastWH]
Starfall.DetonateScript=Blast.lua
Starfall.DetonateFunction=onDetonate
Script.Param.Radius=3
```

```lua
local blast = {}
function blast.onDetonate(ctx)
    local units = ctx:nearbyUnits(ctx.point, ctx.params.radius, {houses={"Enemies"}})
    for _, unit in ipairs(units) do
        ctx:action("Damage", {Target=unit, Amount=5})
    end
end
return blast
```

Each on-map explosion invokes the function once after damage resolution, including explosions on empty ground. A direct hit and its own area damage share a callback. Two independent explosions in the same cell and frame each get a distinct `ctx.castId`. Synchronously detonating the same warhead from its own callback applies the explosion while suppressing re-entry into that callback.

| Lua field | Value |
| --- | --- |
| `ctx.warhead` | Native warhead ID. |
| `ctx.point` | Map cell of the explosion. |
| `ctx.owner` | Firing house index, or the Neutral house for an unattributed explosion. |
| `ctx.damage` | Base damage of this detonation before victim armor. Save it in task data if a scheduled callback needs it. |
| `ctx.self`, `ctx.target` | Both nil. Use an explicit point or query nearby units for actions. |
| `ctx.params` | Values from `Script.Param.*` on the warhead, keyed by lowercase suffix. |

Scheduled tasks survive save/load independently of the firer. For Ares Ivan weapons, set `IvanBomb.Warhead` on the planting weapon and bind Lua on the chosen detonation warhead. Disarming the attached bomb cancels its explosion.

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

### Active skills

See the [active skills guide](active-skills.md#active-skills).

## Lua API

### Module lifecycle and configuration

The embedded runtime is Lua 5.4.8. Each UTF-8 `.lua` file must return a table of callback functions. A UTF-8 BOM is accepted. Files are discovered from the game root at setup/load, with a 1 MiB per-file limit. `Action.Script` can omit `.lua`. INI function lookup is case-insensitive but rejects ambiguous names differing only by case; scheduled callback names must exactly match a returned function name.

The runtime pools Lua VMs and caches compiled module code for skill, timer and projectile callbacks. Each invocation runs the module again with fresh globals, writable standard-library tables and module locals. Nested callbacks borrow a separate VM. Reloading scripts clears the idle pool. Do not use module initialization for game mutations or assume an initialized `ctx` exists at top level. Persistent data belongs in the interfaces below.

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
| `ctx.event.selfPoint` | Recorded death cell on `OnDeath`; usable after the holder is removed. |
| `ctx.event.targetPoint` | Recorded target cell for supported OnFire events and weapon firing scripts, including ground fire. |
| `ctx.params` | Read parameters described above. |
| `ctx.frame` | Current simulation frame. |
| `ctx.taskId` | Present during a scheduled callback. |
| `ctx.point`, `ctx.area`, `ctx.direction` | Active targeting data where applicable. Weapon firing scripts receive the original target cell in `ctx.point`. To use it in a scheduled callback, pass it as task data. |
| `ctx.owner` | Originating house index for sequence, map and warhead callbacks. For ordinary unit callbacks use `ctx:house()` or `ctx.self:owner()`. |
| `ctx.warhead` | Warhead ID during a detonation callback and its scheduled tasks. See [Run Lua on warhead detonation](#run-lua-on-warhead-detonation). |
| `ctx.superWeapon`, `ctx.castId` | Superweapon ID and independent invocation number. Map and warhead callbacks expose the invocation number. |
| `ctx.state` | Persistent state proxy; see below. |

There is no general `ctx.sender`, `ctx.trigger`, `ctx.attacker`, `ctx.victim`, or arbitrary engine object API. Formula `Sender` does not imply an identically named Lua property.

### Projectile callbacks

Use `ProjectileScript` on the projectile type to control individual rounds from `onLaunch(p)` and `onTick(p)`. Use `onTravel(p, from, to)` for actions on a clipped movement segment, `p:groundPoint()` for the ground below it and `p:nearbyUnits()` for target acquisition. The projectile context exposes movement, its aim and per-flight state. Enable `Piercing.Enabled=yes` on the same projectile to apply piercing hits along its scripted path. See [Lua projectile motion](actsp.md#lua-projectile-motion) for units, lifecycle, collision and the complete API.

### Unit handles and house snapshots

A unit handle can come from `ctx.self` (the skill holder), `ctx.target` (the event counterpart), `ctx:nearbyUnits(...)`, or `container:occupants()`. Read context fields with a dot and call unit methods with a colon. A callback without a counterpart can have `ctx.target == nil`. Use `if unit and unit:alive() then` before reading live properties. On a handle that no longer resolves, `id()` still returns its original ID, `alive()` returns `false`, `occupants()` returns an empty array, and the remaining property queries return `nil`.

Use `unit:type() == "MTNK"` to match any Grizzly Tank. To match the same individual tank across callbacks, store the handle in task data or `ctx.state`, check `:alive()` when you retrieve it, and compare handles with `a == b`.

| Method | Live return type | Meaning |
| --- | --- | --- |
| `unit:id()` | integer | Identifies this individual unit. Two Grizzlies have different IDs. An existing handle keeps its ID after removal. |
| `unit:type()` | string | Current rules type ID, such as `E1` (GI), `MTNK` (Grizzly Tank), or `GAPOWR` (Allied Power Plant). Lua string comparisons are case-sensitive. |
| `unit:kind()` | string | Engine class in lowercase: `infantry`, `vehicle`, `aircraft`, `building`, or `other`. A Grizzly has type `"MTNK"` and kind `"vehicle"`. |
| `unit:alive()` | boolean | True while the handle resolves to a live unit in the current callback. A handle to a destroyed unit returns `false`. |
| `unit:position()` | table or nil | Returns integer map-cell coordinates `{x=…, y=…}` while the unit is on the map. A unit off the map or in limbo returns `nil`. The point can be passed to `ctx:nearbyUnits`. |
| `unit:owner()` | integer | Current owning house’s array index, or `-1` for no owner. Pass it to `ctx:house(unit:owner())` to read the house’s credits, power, and research points. |
| `unit:health()` | integer | Current live hit points from Starfall’s health system. |
| `unit:maxHealth()` | integer | Current maximum hit points including active maximum-health changes. `unit:health() * 2 <= unit:maxHealth()` tests for half health or less. |
| `unit:ammo()` | integer | Remaining ammunition. `0` means empty and `-1` means unlimited ammunition. |
| `unit:passengers()` | integer | Number of passengers directly aboard this unit. An empty transport returns `0`. |
| `unit:occupants()` | array of unit handles | Lists direct cargo (including carried vehicles), infantry in a building garrison, and the tank fully parked inside a tank bunker. Works with both native and BetterGarrison firing rules. Returns an empty array if there are no live members. |
| `unit:rank()` | integer | Current veterancy rank: `0` rookie, `1` veteran, `2` elite. |
| `unit:trainable()` | boolean | The current type’s `Trainable` flag. Use `unit:trainable() and unit:rank() < 2` to check for a trainable unit below elite rank. |
| `unit:cost()` | integer | Base `Cost` of the current rules type. Use it for rewards based on a unit’s base price. |
| `unit:hasPassive(passiveId)` | boolean | Whether the unit currently owns the named passive. Pass its registered ID as a string, e.g. `unit:hasPassive("SF_FieldCommander")`. The lookup is case-insensitive. |
| `unit:stack(passiveId)` | integer | Effective layer count for the named passive, including stackable passenger contributions. Returns `0` if absent. An owned passive has at least one layer. Use `hasPassive(passiveId)` to test ownership. |

For event-time values, use snapshots such as `ctx.event.selfHealth` and `ctx.event.targetHealth` when available. During `OnDeath`, `ctx.event.selfPoint` retains the death cell even if the holder’s `position()` now returns `nil`.

Each call to `occupants()` takes a fresh snapshot of the direct members in engine order. Use `ipairs` to visit them. To inspect a transport carried inside another transport, call `occupants()` on that member. Use the container’s position for an embarked member whose `position()` returns `nil`.

```lua
for _, member in ipairs(ctx.self:occupants()) do
    if member:type() == "E1" and member:health() < member:maxHealth() then
        ctx:action("Heal", {Target=member, Amount=10})
    end
end
```

**Example. Measure a target Grizzly’s health change over 30 frames.** Set the unit-targeted skill’s `Action.Function=mark`. The scheduled callback receives the original handle and starting health. If the tank survives, `ctx.state.lastSample` contains its numeric instance ID and the signed change in hit points.

```lua
return {
    mark = function(ctx)
        local unit = ctx.target
        if not unit or not unit:alive() or unit:type() ~= "MTNK" then
            return
        end
        ctx:schedule(30, "measure", {unit = unit, hp = unit:health()})
    end,

    measure = function(ctx, data)
        local unit = data.unit
        if unit:alive() then
            ctx.state.lastSample = {
                id = unit:id(),
                hpChange = unit:health() - data.hp,
            }
        end
    end,
}
```

`ctx:house(index)` returns a new snapshot with `index`, `powerOutput`, `powerDrain`, `powerBalance`, `credits`, and `researchPoints`. Omit index for the holder/launching owner. Invalid index returns nil. Call again to obtain updated values; modifying the returned table does not alter the house.

### Unit orders

| Lua call | Game behavior |
| --- | --- |
| `unit:moveTo({x=56, y=69})` | Order a mobile unit to travel to a playable map cell using normal pathfinding. |
| `unit:startOrder(id, area)` | Start a registered continuous order and return whether it was accepted. Pass `ctx.area` or a table with a cell `center` and a `radius` in cells. [Continuous unit orders](actsp.md#continuous-unit-orders) |
| `unit:currentOrder()` | Return the registered ID of the running order, or nil. |
| `unit:cancelOrder([id])` | Cancel the running order and return whether it ended. An optional ID must match. |
| `unit:setDroneGuardCell({x=84, y=71})` | Assign a guard cell to an eligible ActSp carrier or outpost. See [guard cell orders](actsp.md#guard-cell-orders) for argument and return-value rules. |
| `unit:attack(targetUnit)` | Order an armed unit to attack a live on-map target. Normal weapon selection, movement into range and reloading apply. |
| `unit:attack({x=55, y=70})` | Order the unit to attack a ground cell with a suitable weapon. |
| `unit:deploy()` | Issue the game's Deploy command. Its effect depends on the unit. A GI toggles its deployed stance. |

Each method returns a boolean. `true` means the order reached the game's command handler. Travel, firing and deployment take simulation time and can be interrupted. A later order can replace the current one. To attack after reaching a destination, check `unit:position()` in a later callback. An accepted order counts as a successful Lua skill action.

An expired handle, a passenger, an attached child, or a unit locked by EMP or forced movement returns `false`. A building cannot move, and a unit without a suitable weapon or deploy ability rejects the corresponding order. Pass integer map-cell coordinates within the playable area. A malformed point or argument type raises a Lua error. During `OnBeforeDealDamage` and `OnBeforeTakeDamage`, schedule orders for a later frame. Simulation callbacks can issue orders for a unit of any house.

### Queries and action calls

| Method | Contract |
| --- | --- |
| `ctx:nearbyUnits(center, radius, filters)` | Center is a unit handle or integer point. Finite radius `0..128` cells. Returns a dense array sorted by unit ID. |
| `ctx:action(name, args)` | Execute one native Starfall action; returns success boolean. |
| `TauntTarget` in `GrantPassive` | Optional unit handle for a taunt passive. `Target` receives the control effect and attacks `TauntTarget`. Omit `TauntTarget` to attack the caster. Example: `ctx:action("GrantPassive", {Target=enemy, Passive="SF_Taunted", Duration=75, TauntTarget=decoy})`. |
| `ctx:actions(list)` | Ordered array of tables containing `Action` and action fields; returns true if any succeeds. Shared action context supports consume-then-steal. |
| `ctx:runAction(section, overrides)` | Execute an existing INI single-action section. Overrides include Target, Destination, Origin for a FireWeapon section, and ForceMove-specific fields for a ForceMove section. |

Nearby filters use exact Lua keys `houses`, `types`, `includeSelf`, `visibleOnly`. Houses defaults to `All`; types defaults to `*`; both accept a comma-separated string or array of strings. Types accepts research-style `@Category` selectors. A unit center is excluded unless `includeSelf=true`; for a point center there is no center unit to exclude. Relationships are relative to the executing owner. `visibleOnly=true` excludes hostile undetected cloak/disguise using synchronized owner detection; default false preserves simulation queries. It does not test local UI visibility or fog of war.

Action argument keys are case-insensitive. Prefer `Amount`, `Target`, `Passive`, etc. without the `Action.` prefix. Lua boolean fields map to yes/no. `Target` can be a unit handle, a valid symbolic name for the context, or a point for positional actions. `Destination` can be an integer point for ChronoTeleport or JumpTo. A vehicle jump action returns true when the vehicle takes off. A scripted active skill records a successful cast when a callback action changes game state.

```lua
return ctx:action("JumpTo", {Target=ctx.self, Destination=ctx.point, Range=6, Height=3, Duration=15})
```

To launch the infantry currently aboard a transport in sequence, call `ctx:action("EjectPassengers", {Target=ctx.self, Destination=ctx.point, Range=8, Interval=9, HorizontalSpeed=1, OpenHeight=3})`. It returns true when the first passenger launches. Each rider opens its own parachute. Interval uses simulation frames. HorizontalSpeed is measured in cells per game second and defaults to 1.

To launch one unit already on the map, pass its handle to `EjectUnit`. A grounded infantryman or ground vehicle of any house launches from its own current position. The destination is a map cell, and range is measured from the launched unit. The optional `Range`, `HorizontalSpeed` and `OpenHeight` fields have the same units and defaults as for `EjectPassengers` (8 cells, 1 cell per game second and 3 cells). The call returns true on takeoff and false for an unavailable unit, an out-of-range destination or no legal landing site. Each call launches one unit; call it once per handle to launch a group. Buildings, aircraft, passengers inside transports and units already in forced motion or parachuting are ineligible.

```lua
local units = ctx:nearbyUnits(ctx.point, 3, {houses={"Enemies"}})
for _, unit in ipairs(units) do
    if unit:kind() == "vehicle" then
        ctx:action("EjectUnit", {Target=unit, Destination=ctx.point, Range=8})
    end
end
```

JumpTo also accepts `TakeoffAnim`, `TakeoffSound`, `LandAnim` and `LandSound` in the action table. The engine plays them at the vehicle's actual launch and landing positions.

ForceMove accepts shorthand `Offset,Mode,Center,Distance,Height,AirborneHeight,Duration,Tilt,Easing,IgnoreCollision,BlowWeight,AffectsIronCurtain`.

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
| `ctx:spawnDeathCopy()` | In an OnDeath callback, create a fresh unit of the dead holder’s type for its owner near the death cell. Supports infantry, vehicles and aircraft. If the holder belonged to a map team, each copy rejoins that team at its current script step, even when the holder was its last member. Multiple copies can share the original task force slot. A copy of a unit without a team starts on Guard. Returns the new unit’s handle, or nil if placement fails. |
| `ctx:spawnAnim(id, point, options)` | Create registered animation; return integer handle or nil for invalid type/resource/location. Point may have fractional cells. |
| `ctx:moveAnim(handle, point, frames)` | Replace movement with linear travel over integer `1..54000` frames; keep height offset. Return success. |
| `ctx:removeAnim(handle)` | Remove an animation owned by this context; nil/expired/foreign handles return false. |

Options use exact Lua keys: `loop=false`, `duration=150` (integer `1..54000` frames), `height=0` (finite `-64..64` cells above ground). At most 2,048 managed Lua animations exist globally. A unit/skill owns its animations; sequence casts/steps own theirs. Save/load retains lifetime and movement. Holder removal cleans ordinary animations; losing the passive alone does not. Casterless sequence animations can continue until duration or explicit removal. A non-looping native animation may naturally finish before duration. Animation handles are plain integers that can be stored in data/state; they do not apply damage or displacement themselves.

### Runtime limits and unavailable libraries

Each module load/callback has an 8 MiB Lua allocator limit and a hook budget of approximately 100,000 Lua VM instructions. Native action/query work has a 4,096-operation budget per callback; query cost can exceed one operation. These are failure limits, not performance targets.

Available libraries are a restricted base environment plus `table`, `string`, `math`, and `utf8`. `ipairs` is available. The following are unavailable: `load`, `loadfile`, `dofile`, `collectgarbage`, `print`, `warn`, `getmetatable`, `setmetatable`, `pcall`, `xpcall`, `pairs`, `next`, `tostring`, `rawset`, `math.random`, `math.randomseed`, `string.dump`, and `string.format`. The runtime does not open `io`, `os`, `package`, `debug`, or `coroutine`; `require` is not provided.

Syntax/runtime errors include script location in `Starfall.log`. An error ends that callback; already executed actions are not rolled back. Lua return values are ignored by the callback dispatcher. Use supported state/action/task operations to produce effects, not a return value as an assumed success flag.

### Troubleshooting

| Symptom | Check |
| --- | --- |
| The callback does not run | Confirm passive registration or the weapon, warhead, or map binding. Verify the Lua file is in the game root and returns a table containing the requested function. Check `Starfall.log`. |
| An active button is missing | Confirm the unit owns the registered `Trigger=Active` skill and that its active configuration and script function are valid. |
| A saved counter resets | Write persistent values to `ctx.state` or scheduled task data. Each callback starts with fresh script variables. |
| A delayed callback loses its original target or values | Store the required handle, event values, or point in task data. Check `:alive()` before accessing a stored unit. |
| A script reports an unavailable function or library | Check the available libraries and native callback restrictions above. Simulation random choices use `ctx:randomInt` or `ctx:randomPoint`. |

## Interface index

| Find | Reference |
| --- | --- |
| Script loading, parameters, callbacks | [Module lifecycle and configuration](#module-lifecycle-and-configuration) |
| `ctx.self`, `ctx.target`, `ctx.point`, snapshots | [Context values](#context-values) |
| `unit:alive()`, `unit:health()`, `ctx:house()` | [Unit handles and house snapshots](#unit-handles-and-house-snapshots) |
| `unit:moveTo()`, `unit:attack()`, `unit:setDroneGuardCell()` | [Unit orders](#unit-orders) |
| `ctx:nearbyUnits()`, `ctx:action()`, `ctx:actions()`, `ctx:runAction()` | [Queries and action calls](#queries-and-action-calls) |
| `ctx:schedule()`, `ctx:cancel()`, `ctx.state`, random functions | [Timers, state, and synchronized randomness](#timers-state-and-synchronized-randomness) |
| `ctx:spawnAnim()`, `ctx:moveAnim()`, `ctx:removeAnim()` | [Movable animation API](#movable-animation-api) |
