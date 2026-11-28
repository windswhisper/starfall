# 自定义拾取物

[返回目录](../index.md)

自定义拾取物放置在地图格上，地面步兵或载具进入后触发配置结果。它与原版箱子的数量和有效期相互独立。

## 注册与配置

```ini
[Starfall.PickupTypes]
0=(拾取物 ID)
; 注册拾取物配置节。
; 合并时使用未占用的键。

[拾取物 ID]
Image=(SHP 文件名)
; 指定拾取物图像，使用当前地形的单位调色板。
; 默认值：内置图像；透明色索引为 0，每四帧播放一帧。

Lifetime=(整数帧)
; 指定拾取物在地图上的存在时间。
; 默认值：-1，永久；临时存在时填写正整数帧。

Action=(结果名称)
; 拾取时执行单个结果，参数使用 Action. 前缀。

Actions=(结果节 ID 列表)
; 拾取时按顺序执行多个结果节。

RandomActions=(结果节 ID 列表)
; 拾取时按权重随机执行结果节。

RandomActions.Weights=(非负整数列表)
; 与随机结果逐项对应的权重。
; 默认值：等权；不得全部为 0。

RandomActions.Count=(整数)
; 随机抽取次数，允许重复。
; 默认值：1；范围：1–256。
```

`Action`、`Actions` 和 `RandomActions` 是互斥的结果配置方式。拾取时 `Self` 和 `Target` 指拾取单位，`VictimLocation` 指拾取格；同格有多个单位时由对象 ID 最小者拾取。

## 生成与移除

```ini
[被动 ID 或结果节 ID]
Action=SpawnPickup
; 在目标位置生成自定义拾取物。

Action.PickupType=(拾取物 ID)
; 指定已注册的拾取物类型。
; 必填。
```

```ini
[被动 ID 或结果节 ID]
Action=RemovePickup
; 移除匹配的自定义拾取物，不发放拾取奖励。

Action.Range=(整数格数)
; 0 表示目标格，正数表示圆形半径，-1 表示全地图。
; 默认值：0。

Action.PickupTypes=(拾取物 ID 列表或 *)
; 限制要移除的拾取物类型。
; 默认值：*；不能将 * 与其他 ID 混用。
```

生成位置不可用时会在附近搜索；移除结果不影响原版箱子。拾取物随存档保留。
