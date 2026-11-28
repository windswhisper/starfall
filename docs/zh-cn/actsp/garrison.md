# 高级驻军

[返回目录](../index.md)

高级驻军让建筑内的步兵使用各自可在运输中使用的武器向外开火，并分别装填。全局启用后，可以按建筑类型覆盖开关及倍率。

```ini
[General]
BetterGarrison.Enabled=(布尔值)
; 为支持驻军开火的建筑启用高级驻军。
; 默认值：no。

BetterGarrison.DamageMultiplier=(小数倍率)
; 驻军武器伤害倍率。
; 默认值：1.20；范围：0–100。

BetterGarrison.RangeBonus=(整数格数)
; 索敌和开火射程增加的格数。
; 默认值：1；限制在 -128–128。

BetterGarrison.FireRateMultiplier=(小数倍率)
; 射速倍率，整轮装填时间除以该值。
; 默认值：1.0；范围：0.01–100。

[建筑类型 ID]
BetterGarrison.Enabled=(布尔值)
; 针对该建筑类型覆盖启用状态。
; 默认值：继承全局；全局开关关闭时不能单独启用。

BetterGarrison.DamageMultiplier=(小数倍率)
; 覆盖该建筑类型的驻军伤害倍率。
; 默认值：继承全局。

BetterGarrison.RangeBonus=(整数格数)
; 覆盖该建筑类型的射程加成。
; 默认值：继承全局。

BetterGarrison.FireRateMultiplier=(小数倍率)
; 覆盖该建筑类型的驻军射速倍率。
; 默认值：继承全局。
```

驻军武器遵循 `OpenTransportWeapon`、`FireInTransport` 等原有配置。手动攻击的可选目标由当前最早进入的驻军武器决定；各驻军优先攻击可攻击的指定目标，否则按自身武器规则索敌。可手动攻击符合武器条件的中立对象，按住 Ctrl 可对可攻击的空地强制开火。
