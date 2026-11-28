# 持有者状态条件

[返回目录](../../index.md)

持有者状态条件检查单位自身的电力、移动和隐形状态。

```ini
[被动 ID]
Condition.OwnerPower=(Sufficient 或 Low)
; 检查所属玩家的电力是否充足。
; 默认值：不限制。

Condition.SelfPowered=(布尔值)
; 检查持有者自身是否通电且在线。
; 默认值：不限制。

Condition.SelfMoving=(布尔值)
; 检查移动器是否实际移动；建筑视为静止。
; 默认值：不限制。

Condition.SelfCloak=(隐形状态)
; 可填 Uncloaked、Cloaking、Cloaked、Uncloaking。
; 默认值：不限制。
```
