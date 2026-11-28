# Miscellaneous

[Manual home](../manual.md)

## Custom ground pickups

Register each type in `[Starfall.PickupTypes]`. Its section accepts `Image` (optional theater unit-palette SHP, transparent index 0, one animation frame per four game frames), `Lifetime` (positive game frames, default `-1` for permanent), and one of `Action`, `Actions`, or `RandomActions` with the usual action parameters. The default art is used when the custom image is omitted or unavailable. Native crate limits and expiration are independent. Pickups skip native crate overlays. Targeted native crates whose final cell contains a pickup search within three cells for a legal alternative, failing if none exists. Random crate refreshes skip occupied cells through the normal retry path.

To clear all remaining wave tokens, use `Action=RemovePickup`, `Action.Range=-1`, and `Action.PickupTypes=SF_WaveToken`. Range uses the actual landing cell. Unknown IDs invalidate the action. Lua accepts the same fields, for example `ctx:action("RemovePickup", {Target=ctx.point, Range=5, PickupTypes={"SF_MedKit","SF_SupplyBox"}})`. It returns true if at least one pickup was removed.

A ground infantry or vehicle on the cell collects it once, regardless of owner. Self and Target resolve to the collector, VictimLocation to the pickup cell. If several units occupy the same cell, the lowest object ID wins. A full-health collector still consumes a healing pickup. Ground pickups persist in saves.

```ini
[Starfall.PickupTypes]
0=SF_MedKit

[SF_MedKit]
Image=sfmedkit.shp
Lifetime=900
Actions=SF_KitHeal,SF_KitMoney

[SF_KitHeal]
Action=Heal
Action.Target=Self
Action.Amount=50

[SF_KitMoney]
Action=GiveCredits
Action.Amount=200

[Starfall.PassiveTypes]
100=SF_DropKit

[SF_DropKit]
Trigger=OnKill
Filter.TargetHouse=Enemies
Action=SpawnPickup
Action.Target=VictimLocation
Action.PickupType=SF_MedKit
```

`Action.CrateType` accepts `Random,Money,Unit,HealBase,Cloak,Explosion,Napalm,Squad,Darkness,Reveal,Armor,Speed,Firepower,ICBM,Invulnerability,Veteran,IonStorm,Gas,Tiberium,Pod`. `Random` uses the current native `[Powerups]` weights.

## ActionSequence superweapons

`Type=ActionSequence` is provided by Starfall and can run without Ares/Phobos. Each native registered superweapon entry retains its own sidebar identity and recharge. Native buildings grant it through `SuperWeapon` or `SuperWeapon2`.

```ini
[SuperWeaponID]
Type=(ActionSequence)
; ActionSequence.

Starfall.Actions=(list)
; Required nonempty ordered action-section list; duplicate steps remain steps.

Starfall.Target=(Point)
; Point only; default point behavior.

RechargeTime=(number)
; Native game minutes, not Starfall frames.

```

Direct INI steps support positional actions targeting `Point` (or omitted target), money/research-point actions targeting `Owner` (or omitted target), and `RunScript` with no explicit target. SpawnUnit and SpawnBuilding owner may use `Owner`/`Self`, `Neutral`, or player-slot aliases. To heal, damage, or move particular units from Lua, first obtain live handles and pass them explicitly.

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

Sequence Lua receives `ctx.point`, `ctx.owner`, `ctx.superWeapon`, and `ctx.castId`; initial self/target are nil. Timers and state are associated with the cast, not an imaginary caster unit. Cast state is retired when no pending tasks remain; persistent animations follow their own ownership/lifetime rules.
