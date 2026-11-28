# 弹头授予被动

[返回目录](../../index.md)

弹头可以在命中符合条件的单位时授予被动，并按所属关系和单位类型筛选。

```ini
[弹头 ID]
Starfall.GrantPassives=(被动 ID 列表)
; 命中符合条件的单位时授予已注册的被动。
; 默认值：无；列表中存在无效 ID 时整条规则不生效。

Starfall.GrantPassives.Duration=(整数帧)
; 每次授予的持续时间，正数为临时授予。
; 默认值：-1，永久。

Starfall.GrantPassives.StackOnReapply=(布尔值)
; 目标已拥有该被动时，再次授予是否增加一层手动层数。
; 默认值：no

Starfall.GrantPassives.Houses=(所属关系列表)
; 按开火方筛选：All、Self、Allies、Enemies、Neutral。
; 默认值：All；弹头自身的阵营限制也会生效。

Starfall.GrantPassives.TargetTypes=(类型或科研分类列表)
; 筛选接收被动的单位类型。
; 默认值：*，不限制类型。
```

`CellSpread=0` 仅向直接命中的单位授予；正范围按爆炸位置筛选。零伤害命中也可授予，但死亡、离场、铁幕保护和弹头对应装甲 `Verses` 为零的目标会被排除。同一次引爆对同一单位的每项被动最多授予一次。
