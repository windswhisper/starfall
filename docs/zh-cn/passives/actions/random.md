# 随机结果

[返回目录](../../index.md) · [一次性结果](index.md)

随机结果从指定结果列表中按权重抽取并执行，适合制作随机奖励。

同一被动中，`RandomActions` 不能与 `Action` 或 `Actions` 同时使用。

```ini
[被动 ID]
RandomActions=(结果节 ID 列表)
; 按权重随机抽取并执行结果节。
; 与 Action、Actions 互斥。

RandomActions.Weights=(非负整数列表)
; 与结果列表逐项对应的权重，不得全部为 0。
; 默认值：等权。

RandomActions.Count=(整数)
; 独立抽取的次数，允许重复抽中同一结果。
; 默认值：1；范围：1–256。
```

抽中的结果执行失败时不重新抽取。至少一个结果成功即可触发冷却。
