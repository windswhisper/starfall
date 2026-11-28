# 建筑定时产出科研点

[返回目录](../index.md)

建筑可在正常工作期间按独立计时周期向所属玩家发放科研点。

```ini
[建筑类型 ID]
Starfall.ResearchPoints=(正整数)
; 每个周期完成时发放的科研点。
; 默认值：0；与 Starfall.ResearchInterval 均为正数时启用。

Starfall.ResearchInterval=(正整数帧)
; 建筑发放科研点的周期。
; 默认值：150 帧。
```

EMP、离线或超时空移除期间暂停计时；建造、出售或离开地图会清除当前进度。所属方改变时为新所属方重新计时。
