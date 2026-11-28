# 弹头引爆脚本

[返回目录](../index.md)

弹头引爆脚本在每次地图上的弹头引爆完成伤害结算后执行，空地引爆也会触发。

```ini
[弹头 ID]
Starfall.DetonateScript=(Lua 文件名)
; 指定弹头引爆后执行的脚本。
; 必填；支持规则、Starfall.ini 和地图覆盖。

Starfall.DetonateFunction=(函数名)
; 指定脚本导出的引爆回调。
; 默认值：onDetonate。

Script.Param.参数名=(数值或文本)
; 向该弹头回调传递自定义参数。
; 通过 ctx.params 的小写参数名读取。
```

回调中的 `ctx.warhead` 是弹头 ID，`ctx.point` 是引爆格，`ctx.damage` 是经过目标装甲计算前的基础伤害；`ctx.owner` 为开火所属方索引，无归属时使用中立方。`ctx.self` 和 `ctx.target` 初始为空。

一次直接命中及其范围伤害共用一次回调；独立引爆各自执行。回调同步引爆自身弹头时会执行爆炸，但阻止同一回调递归进入。
