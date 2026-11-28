# 超时空传送

[返回目录](../../index.md)

超时空传送将符合条件的地面移动单位送到目标附近的可用格子。

```ini
[被动 ID 或结果节 ID]
Action=ChronoTeleport
; 将支持的地面移动单位传送到可用格子。

Action.Destination=(X,Y 或 Target)
; Target 取执行动作时事件目标所在的地图格。没有有效的在场目标时，本次动作失败。
; 填固定坐标时，每个坐标范围：0–511，且必须在可游玩地图内。

Action.SearchRadius=(整数格数)
; 目标格不可用时的圆形搜索半径。
; 默认值：3；范围：0–32；0 只尝试目标格。

Action.Instant=(布尔值)
; yes 立即移动；no 使用原版超时空传送行为。
; 默认值：no。
```

例如，载具受伤后传送到攻击者附近。

```ini
[WarpToAttacker]
Trigger=OnTakeDamage
Action=ChronoTeleport
Action.Target=Self
Action.Destination=Target
Action.SearchRadius=3
```

即时传送保留单位身份、生命和效果，但会清除现有移动和命令。不要在同步伤害拦截内直接传送，应延后执行。
