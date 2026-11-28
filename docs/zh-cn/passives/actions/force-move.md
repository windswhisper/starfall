# 强制位移

[返回目录](../../index.md)

强制位移可推开、拉近或击飞目标，并可设置移动距离、高度及碰撞规则。

```ini
[被动 ID 或结果节 ID]
Action=ForceMove
; 使支持的地面步兵或载具产生强制位移或击飞。

Action.ForceMove.Offset=(横向格数,纵向格数)
; 相对位移，支持小数。
; 各分量范围：-512–512；不能与 Mode、Center、Distance 同时使用。

Action.ForceMove.Mode=(Push、Pull 或 Launch)
; 分别为推开、拉近、击飞。
; 不填 Offset 时必填；不要填写 Offset 作为 Mode。

Action.ForceMove.Center=(Self 或 Impact)
; 推拉中心为持有者或事件命中位置。
; 默认值：Self；不用于 Offset。

Action.ForceMove.Distance=(小数格数)
; 推拉的水平距离。
; 默认值：0；范围：0–512；Push 和 Pull 要求有效距离大于 0。

Action.ForceMove.Height=(小数格数)
; 抬升高度。
; 默认值：Launch 为 1 格，其他为 0；范围：0–512。

Action.ForceMove.AirborneHeight=(小数格数)
; 当前离地高度达到此值时按空中目标索敌；0 禁用该判定。
; 默认值：1；范围：0–512。

Action.ForceMove.Duration=(正整数帧)
; 位移持续时间。
; 必填。

Action.ForceMove.Easing=(缓动方式)
; 水平移动进度：Linear、EaseIn、EaseOut、EaseInOut。
; 默认值：Linear。

Action.ForceMove.Tilt=(布尔值)
; 支持的 voxel 载具在位移时倾斜并在结束后恢复。
; 默认值：yes。

Action.ForceMove.IgnoreCollision=(布尔值)
; yes 穿过障碍并在落地时结算碰撞；no 在障碍前停止水平移动。
; 默认值：yes。

Action.ForceMove.BlowWeight=(小数)
; 只影响原版 Weight 严格小于此值的目标。
; 默认值：不限制重量。

Action.ForceMove.AffectsIronCurtain=(布尔值)
; 是否允许影响真正的铁幕保护目标。
; 默认值：no。
```

水平位移或高度至少有一项有效且非零。位移过程更新实际水平坐标；高度达到阈值后使用防空目标规则，下降到阈值以下恢复地面目标。忽略碰撞时落地可能碾压单位，撞到建筑或落在不可通行地形也可能导致死亡。
