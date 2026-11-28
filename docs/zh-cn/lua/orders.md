# Lua 单位指令

[返回目录](../index.md)

单位指令通过单位句柄下达，使用正常游戏命令路径。指令被接受不代表移动、开火或部署已经完成。

| 方法 | 含义 |
| --- | --- |
| `unit:moveTo(point)` | 命令可移动单位前往有效地图格，使用正常寻路。 |
| `unit:attack(target)` | 命令单位攻击存活地图单位，或攻击可使用武器瞄准的地图格。 |
| `unit:deploy()` | 下达原版部署命令，具体行为由单位类型决定。 |
| `unit:startOrder(id, area)` | 开始已注册的 ActSp 持续单位指令；区域包含中心格和半径。 |
| `unit:currentOrder()` | 返回当前持续单位指令 ID；没有时返回 `nil`。 |
| `unit:cancelOrder(id)` | 取消当前持续单位指令；ID 可省略，填写时须与当前指令相同。 |
| `unit:setDroneGuardCell(point)` | 为符合条件的 ActSp 母体或哨站设置子机警戒格。 |

除 `currentOrder()` 外，上述方法返回布尔值，表示指令是否被接受或成功取消。地图点使用可游玩范围内的整数格坐标；单位失效、在载具内、受到 EMP 或处于强制位移等不兼容状态时，指令可能失败。

在 `OnBeforeDealDamage` 或 `OnBeforeTakeDamage` 的同步伤害拦截回调中，应将单位指令安排到后续帧执行。
