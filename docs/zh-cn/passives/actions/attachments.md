# 挂载子单位

[返回目录](../../index.md) · [一次性结果](index.md)

`AttachUnit` 在存活且位于地图上的步兵、载具、飞机或建筑上创建一个真实的步兵或载具子单位。每次成功执行创建一个，子单位不能继续挂载子单位。

```ini
[挂载结果节 ID]
Action=AttachUnit
; 创建一个挂载子单位。

Action.UnitType=(步兵或载具类型 ID)
; 指定子单位类型；必填。

Action.Target=(目标名称)
; 作为母体的单位或建筑；默认值：Self。

Action.Slot=(槽位名或 Auto)
; Auto 占用母体 AttachSlots 中第一个空槽；指定名称则保留该槽位。
; 默认值：不占用命名槽位；与 Action.Node 互斥。

Action.Node=(节点名)
; 占用母体的指定节点；与 Action.Slot 互斥。

Action.Anchor=(Body 或 Turret)
; 自由位置相对母体本体或炮塔；默认值：Body。

Action.FLH=(前,左,高整数偏移)
; 相对锚点的偏移，命名节点时叠加到节点偏移上。
; 默认值：0,0,0；分量范围：-8192–8192；横向一格为 256 leptons。

Action.TransportFire=(FollowTransport 或 Disabled)
; 母体在开放式运输工具中时是否允许挂载单位开火。
; 默认值：FollowTransport。

Action.ExcludeParentPassives=(被动 ID 列表)
; 指定不从母体继承的普通状态被动；默认值：空。

[母体类型 ID]
AttachSlots=(槽位名列表)
; 定义 Auto 安装时依次使用的命名槽位。

AttachNode.节点名.FLH=(前,左,高整数偏移)
; 设置命名节点相对母体的偏移。

AttachNode.节点名.Anchor=(Body 或 Turret)
; 设置命名节点相对本体或炮塔。
```

子单位跟随母体位置、朝向和所属方，保留自己的原生类型被动。母体销毁时子单位移除；子单位销毁后槽位释放。同占地建筑变形保留挂载单位及偏移。

## 移除挂载单位

```ini
[移除结果节 ID]
Action=RemoveAttachedUnit
; 静默移除目标母体上的挂载单位，不触发死亡或击杀事件，并立即释放槽位。

Action.Target=(目标名称)
; 作为母体的单位或建筑；默认值：Self。

Action.Slot=(实际槽位名)
; 只移除指定槽位中的单位；省略时不限制，Auto 不用于移除。

Action.UnitType=(步兵或载具类型 ID)
; 只移除指定类型的单位；省略时不限制。

Action.Count=(正整数或 All)
; 最多移除的数量；All 移除所有匹配单位。
; 默认值：1；按安装时间从新到旧移除。
```

没有匹配项时结果失败，并停止当前有序 `Actions` 或 Lua `ctx:actions` 列表；此前已执行的结果不会回滚。
