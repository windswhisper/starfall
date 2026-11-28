# 自定义信号

[返回目录](../../index.md)

信号不需要注册。发送后排队交付，接收被动使用 `Trigger=OnSignal` 和匹配的 `Signal`。

## 发送方

```ini
[发送方被动 ID 或结果节 ID]
Action=SendSignal
; 发送信号。

Action.Signal=(信号 ID)
; 非空信号名称，不区分大小写。
; 必填；最多 127 字节。

Action.Target=(目标名称)
; 指定接收者或广播中心单位。
; 默认值：Self。

Action.Signal.Range=(整数格数)
; 0 直接发送；1–128 为局部广播；-1 为全地图。
; 默认值：0。

Action.Signal.Houses=(所属关系列表)
; 相对发送者，可填 Self、Allies、Enemies、Neutral、All。
; 默认值：Self。

Action.Signal.IncludeSelf=(布尔值)
; 广播时是否包含发送者；直接发送给自身不受此项限制。
; 默认值：no。

Action.Signal.Target=(单位目标名称)
; 附带单位，可填 Self、Target、Attacker、Victim；OnSignal 中还可填 Sender。
; 默认值：无。

Action.Signal.Value=(数字或公式)
; 发送时计算的数值。
; 默认值：0。
```

## 接收方

```ini
[接收方被动 ID]
Trigger=OnSignal
; 收到信号时判定。

Signal=(信号 ID)
; 要匹配的信号名称。
; 必填。

Filter.SenderHouse=(所属关系列表)
; 按接收者视角筛选发送方所属关系。
; 默认值：All。

Filter.SenderKind=(单位类别列表)
; 筛选发送者类别，取值与 Filter.TargetKind 相同。
; 默认值：All。
```

接收时 `Self` 是接收者，`Target` 是附带单位，`Sender` 是发送者；`Filter.Target*` 筛选附带单位。信号链接收深度最多 16 层，最多交付 1024 次，超限会停止并记录日志。
