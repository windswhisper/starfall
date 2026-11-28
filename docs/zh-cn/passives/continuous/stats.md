# 属性倍率

[返回目录](../../index.md)

持续属性倍率在状态满足时调整单位或建筑的属性，状态失效后撤销。

以下参数用于 `Trigger=WhileState` 的被动；未填写的倍率默认为 1，开关默认不产生效果。

```ini
[被动 ID]
Modifier.SpeedMultiplier=(数字或公式)
; 移动速度倍率；范围：大于 0 且不超过 100。

Modifier.FirepowerMultiplier=(数字或公式)
; 火力倍率；范围：大于 0 且不超过 100。

Modifier.ArmorMultiplier=(数字或公式)
; 装甲强度倍率，数值越高越耐伤害；范围：大于 0 且不超过 100。

Modifier.WeaponRangeMultiplier=(数字或公式)
; 标准武器最大射程倍率；范围：大于 0 且不超过 100。

Modifier.FireRateMultiplier=(数字或公式)
; 射速倍率；标准重新装填延迟除以此值，不改变连发内部间隔。
; 范围：大于 0 且不超过 100。

Modifier.ExperienceMultiplier=(数字或公式)
; 持有者击杀敌人所得经验的倍率。
; 默认值：1；范围：0–100。

Modifier.PowerMultiplier=(数字或公式)
; 建筑发电量或耗电量的倍率。
; 默认值：1；范围：0–100。

Modifier.VisualScale=(固定小数倍率)
; 单位或建筑绘制体型，不改变建筑占地。
; 范围：0.01–2.00。

Modifier.ProjectileScale=(固定小数倍率)
; 发射时的弹体、命中特效及已有 CellSpread 范围倍率。
; 范围：0.50–2.00。

Modifier.TooltipText=(文本，可包含公式)
; 持续状态生效时，在单位悬停名称下显示一行文字。
; 默认值：不显示。

Modifier.TooltipDecimals=(整数)
; 提示文字中所有公式数值的固定小数位数。
; 范围：0–6；需配置 Modifier.TooltipText。
```
