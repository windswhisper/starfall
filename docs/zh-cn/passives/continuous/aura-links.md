# 关联被动与光环

[返回目录](../../index.md)

关联被动和光环可在状态生效时，将其他被动提供给持有者、附近单位或关联单位。

```ini
[被动 ID]
Modifier.SpawnPassives=(被动 ID 列表)
; 授予原版 Spawns= 创建的直接子机。

Modifier.LinkedPassives=(被动 ID 列表)
; 状态有效时使持有者拥有指定被动，不额外提供层数。

Modifier.TransportPassives=(被动 ID 列表)
; 乘客将被动授予其直接搭乘的地面载具。

Modifier.TransportPassives.Stack=(布尔值)
; 每个提供者是否贡献一层；需配置 Modifier.TransportPassives。
; 默认值：no。

Modifier.Aura.Passives=(被动 ID 列表)
; 光环授予的已注册被动。
; 必填；不能直接授予另一个光环被动。

Modifier.Aura.Range=(整数格数)
; 光环半径；-1 表示全地图。
; 必填；局部范围：1–32。

Modifier.Aura.Houses=(所属关系列表)
; 可填 Self、Allies、Enemies、Neutral、All。
; 默认值：Self。

Modifier.Aura.TargetTypes=(类型或科研分类列表)
; 可填具体类型、*、@Infantry、@Vehicle、@Aircraft、@Building。
; 默认值：*。

Modifier.Aura.IncludeSelf=(布尔值)
; 是否包含通过其他筛选条件的持有者自身。
; 默认值：no。
```

光环每 15 帧刷新一次，来源消失或状态失效时撤销该来源。乘客对载具的支持以直接搭乘关系为准，不能使用附近空间条件。
