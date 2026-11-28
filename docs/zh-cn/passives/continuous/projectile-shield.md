# 抛射体护盾

[返回目录](../../index.md)

抛射体护盾在持有者周围形成球形拦截区域，可调整护盾范围和拦截伤害。

```ini
[被动 ID]
Modifier.ProjectileShield.Radius=(小数格数)
; 球形护盾半径。
; 必填；大于 0 且不超过 512。

Modifier.ProjectileShield.HeightOffset=(小数格数)
; 球心相对持有者的高度偏移。
; 默认值：0；范围：-512–512。

Modifier.ProjectileShield.BlockInviso=(布尔值)
; 是否拦截瞬时 Inviso 抛射体。
; 默认值：no。

Modifier.ProjectileShield.DamageMultiplier=(非负小数)
; 拦截伤害倍率；0 表示完全吸收。
; 默认值：1。
```
