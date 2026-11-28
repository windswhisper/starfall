# ActSp 特殊行为子模块

ActSp 为《尤里的复仇》提供飞行子机巡逻、建筑地面守卫、高级驻军、可建造高台和实体围墙。运行项目根目录的 `build.cmd` 会将这些功能编译进 `build/Starfall.dll`。按[流火安装说明](../../README.md#运行与测试)加载 DLL，并在规则中启用所需功能。

可建造高台的配置和所需美术见[混凝土高台示例](../../examples/HighPlatform/README.md)。

[苏联实体围墙示例](../../examples/EntityWalls/EntityWalls.rules.ini)使用原版美术，提供可以相互续接的节点和墙段。围墙的连接距离、挡弹和高墙规则见[实体围墙参数](../../docs/manual/actsp.md#entity-walls)。

在 `rulesmd.ini` 的**母体类型节**中追加 `Drone.*` 字段。例如，给原版盟军航空母舰的 `[CARRIER]` 节加入以下三行即可启用环绕巡航：

```ini
Drone.Mode=Orbit
Drone.Radius=3
Drone.Period=180
```

ActSp 按字段读取游戏已加载的规则，省略的字段使用下表所列默认值。已有独立配置的整合包，可将原有 `Drone.*` 字段移到 `rulesmd.ini` 对应单位节。

飞行模式的母体需要已经配置原版子机和槽位。无目标放飞要求当前主武器带有 `Spawner=yes`，有精英武器的母体也要配置对应的精英发射器。

带机场机位的建筑母体可以设置 `Drone.UseBuildingDocks=yes`，让子机在占用的机位上补弹、待命并起飞。子机的 `Dock=` 列表须包含母体建筑。普通飞机可以使用其余空位。详见[建筑机位配置](../../docs/manual/actsp.md#visible-parking-at-an-aircraft-dock)。

| 模式 | 配置 | 行为 |
| --- | --- | --- |
| `Native` 或省略模式 | 无 | 沿用原版放飞条件 |
| `Guard` | 可选 `Drone.GuardCell=84,71` | 默认警戒这个绝对地图格。省略时，子机等待集合点、Lua 命令或母体的攻击命令 |
| `Follow` | `Drone.Offset=2,0` | 警戒母体附近的相对地图格。默认偏移为 (2,0)，每轴可取 −32 到 32，不随母体朝向旋转 |
| `Orbit` | `Drone.Radius=3`、`Drone.Period=180` | 围绕母体更新航点，各槽位错开相位。半径为 1–32 格，周期为 30–3600 个游戏帧，默认值为 3 和 180 |

可用 `Drone.GuardRange=5` 设置警戒点周围的索敌半径（1–32 格，默认 5 格）。子机选择范围内最近且能攻击的敌人，母体发射器指定的目标优先。警戒点控制移动目的地，实际转弯、速度和抵达后的着陆行为取决于飞行器运动方式。弹药耗尽后子机按原版流程返航、补给，再次出动。导弹型子机使用原版逻辑。

### 在对局中指定警戒格

可直接安装的建筑示例见[灰熊警戒哨站](../../examples/GrizzlyOutpost/README.md)，包含母体、子机、武器及模型。

在已有子机的哨站建筑类型节写入 `Drone.Mode=Guard`。选中己方哨站，用平时设置工厂集合点的操作点击空地，该建筑的所有槽位共用这个警戒格。有生产功能的母体沿用工厂最终选定的集合点。每个母体独立保存警戒格，同类型的其他母体可以守在不同位置。

Lua 技能可以对建筑或移动母体调用以下方法。地面点选技能也可直接传入 `ctx.point`。

```lua
local accepted = ctx.self:setDroneGuardCell({x=84, y=71})
```

成功返回 `true`。单位无法接受指令、母体未配置为 `Guard` 或使用导弹子机时返回 `false`。传入地图可用范围内的整数格坐标。无效坐标会引发 Lua 参数错误。集合点命令和 Lua 以最后生效的一次为准。Lua 只修改子机警戒格，工厂生产单位仍前往原有的出厂集合点。警戒格随对局存档保存。

修改规则后重启游戏，让引擎重新加载 `rulesmd.ini`。ActSp 在新局和读档时清空行为缓存。环绕相位由游戏帧和原版槽位序号计算。联机时所有玩家应使用同一份 DLL 和游戏规则。单机和读档回归入口为 `tests/run.ps1 -Case actsp`。双客户端测试使用 `tests/run-multiplayer.ps1` 的 `-ActSp` 参数，会比较双方每帧状态并检查子机实际飞过的坐标。

### 给建筑配置地面守卫

在建筑类型节中写入以下字段。选择一个已注册、可在地面移动的 `VehicleTypes` 或 `InfantryTypes` 类型。每栋建筑管理一个地面守卫，已有原版飞行子机管理器的建筑优先使用飞行槽位。

```ini
Drone.Mode=Guard
Drone.GroundUnit=MTNK
Drone.GroundRespawnDelay=300
Drone.GuardRange=5
```

使用 `Drone.GroundUnit=E1` 可派出美国大兵。获取警戒格后，建筑从紧邻占地的可通行空格派出守卫。所有出口被堵时等待。地面守卫使用自身的寻路和武器。集合点、Lua 和可选的 `Drone.GuardCell` 默认格均可使用。补员延迟按游戏帧计算，默认 300，取值 0–1000000。模块每 8 帧检查守卫和可用出口。

失去守卫后哨站会补派一个同类型单位。哨站易主时，原守卫归原主人，新主人按延迟补员。哨站拆除或被摧毁时，存活的守卫成为普通单位。存档保存哨站与守卫的对应关系、警戒格及下次补员时间。[完整地面哨站示例](../../examples/GrizzlyGroundOutpost/README.md)

### 高级驻扎开火

在 `rulesmd.ini` 的 `[General]` 节添加以下配置即可全局启用。设置默认关闭。启用后，伤害默认乘以 `1.20`，射程默认增加 `1` 格，射速默认乘以 `1.0`。

```ini
[General]
BetterGarrison.Enabled=yes
BetterGarrison.DamageMultiplier=1.20
BetterGarrison.RangeBonus=1
BetterGarrison.FireRateMultiplier=1.25
```

启用后，原本可驻扎开火的建筑保留原版入住、撤出和容量。驻军各自使用乘员武器、炮口和冷却，尊重 `OpenTransportWeapon` 及武器的 `FireInTransport`。建筑根据最早入驻且仍在场的步兵的武器判断能否手动点选目标。能攻击指定目标的驻军优先集火，其他驻军可以独立索敌。这个模式的加成适用于驻军射击，沿用普通武器流程结算经验与击杀。建筑仍可使用自身配置的武器。

驻军使用引擎的武器与威胁评估来选择敌方目标，遵循弹头的自动索敌限制和武器的目标筛选。盟友、中立单位和建筑以及标记为 `Insignificant=yes` 的对象会从自动索敌中排除。可直接点选武器允许攻击的中立目标，也可按住 Ctrl 左键强制攻击武器允许的空地。选中驻扎建筑下达攻击指令时显示原地攻击指针。

`DamageMultiplier` 乘以该次射击的伤害。`RangeBonus` 以格为单位加到最大射程。鼠标点选和自动索敌都从建筑中心判定射程，弹体从各自的驻扎炮口发出。`FireRateMultiplier` 表示射速倍率，完整装填间隔除以该值，向上取整且至少 1 帧。示例中的 40 帧间隔会缩短到 32 帧，同一轮连射的弹间间隔沿用武器设置。

全局启用后，可在建筑类型节用同名字段逐项覆盖这三项加成。省略的字段继承 `[General]` 的值。配置示例见[高级驻军参数](../../docs/manual/actsp.md#advanced-garrison)。

依赖专用 `OccupyWeapon` 的建筑可在其建筑类型节填写 `BetterGarrison.Enabled=no`，保留原版驻扎开火。所有联机玩家应加载同一版流火和同一份规则。
