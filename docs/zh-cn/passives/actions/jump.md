# 载具跳跃

[返回目录](../../index.md) · [一次性结果概述](index.md)

类似镰刀机甲的跳跃移动，建议配合主动技能发动。
```ini
[被动 ID 或结果节 ID]
Action=JumpTo
; 使支持的地面载具跳向目标格。

Action.Destination=(X,Y 或 Target)
; Target 取执行动作时事件目标所在的地图格。没有有效的在场目标时，本次动作失败。
; 指定目标地图格；省略时使用事件命中点。
; 坐标范围：0–511，且须位于可游玩地图内。

Action.Jump.Range=(整数格数)
; 最大跳跃距离。
; 默认值：6；范围：1–128。

Action.Jump.Height=(整数格数)
; 起点与终点连线上方的跳跃弧线高度。
; 默认值：3；范围：1–128。

Action.Jump.Duration=(正整数帧)
; 飞行持续时间。
; 默认值：15；范围：1–3600。

Action.Jump.TakeoffAnim=(动画 ID)
; 成功起跳时播放的已注册动画。
; 默认值：不播放。

Action.Jump.TakeoffSound=(音效 ID 或 WAV 路径)
; 成功起跳时播放的音效；WAV 路径使用 wav: 前缀。
; 默认值：不播放。

Action.Jump.LandAnim=(动画 ID)
; 落地时播放的已注册动画。
; 默认值：不播放。

Action.Jump.LandSound=(音效 ID 或 WAV 路径)
; 落地时播放的音效；WAV 路径使用 wav: 前缀。
; 默认值：不播放。
```
载具跳跃落地时会发动单位原生的碾压能力，可以摧毁目标点的步兵或载具，若目标格被占据又无法碾压时会改为寻找附近空地着陆。

作为主动技能使用时，目标类型须为 Point，跳跃距离由 `Active.Range` 配置；不应在主动技能节中填写 `Action.Destination` 或 `Action.Jump.Range`。
