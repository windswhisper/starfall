# 持续单位指令

[返回目录](../index.md)

持续单位指令使单位按照选定区域持续执行攻击或自动进入行为，可由命令栏、主动技能或 Lua 启动。

## 区域打击

区域打击让单位留在当前位置，持续搜索并攻击指定圆形区域内的合格目标。目标还须位于单位当前武器射程内；每个单位独立选择目标，一轮连发期间会保持同一合格目标。区域内暂时没有目标时，指令仍会等待并继续搜索。

选择新区域会替换之前的打击区域；对单位下达移动、攻击、停止或部署等手动指令可接管单位。

## 自动进驻

自动进驻让步兵或载具在指定圆形区域内寻找最近的合适容器，并为单位分配可用位置。容器可配置为运输工具、驻军建筑或坦克碉堡；实际能否进入仍取决于单位类型、所属关系和剩余空间。

一次指令只分配当时可用的位置，没有分到位置的单位会原地转入警戒。前往途中如果目标失效，或单位连续 180 帧没有移动进展，也会返回警戒；手动移动或停止可取消前往。之后出现空位时，需要重新下达指令。

## 注册与参数

```ini
[Starfall.UnitOrders]
0=(指令 ID)
; 注册持续单位指令。

[指令 ID]
Type=(AreaAttack 或 AutoEnter)
; AreaAttack 为区域攻击；AutoEnter 为自动进入运输工具、驻军建筑或坦克碉堡。
; 必填。

CommandBar.Button=(命令按钮名)
; 为指令指定命令栏按钮名称。
; 默认值：无；需在 uimd.ini 对应 ButtonList 中加入该名称才能显示。

CommandBar.Image=(SHP 文件名)
; 指定按钮图像。
; 默认值：按钮名称加 .shp；文件缺失时使用内置准星。

CommandBar.Tooltip=(文本或 TIP: 标签)
; 设置命令栏提示文字。
; 默认值：显示指令名称及半径；无效标签也使用默认值。

Radius=(整数格数)
; 设置命令栏按钮选择的区域半径。
; 默认值：6；范围：1–128。

ScanInterval=(正整数帧)
; 设置区域索敌或自动进入接近检查间隔。
; 默认值：15。

AreaAttack.Houses=(所属关系列表)
; 区域攻击可选择 Enemies、Neutral。
; 默认值：Enemies。

AreaAttack.Kinds=(单位类别列表)
; 区域攻击目标类别，可填 Infantry、Vehicle、Aircraft、Building、All。
; 默认值：All。

AreaAttack.Select=(Random)
; 设置区域攻击的目标选择方式。
; 默认值：Random。

AutoEnter.Kinds=(容器类别列表)
; 设置自动进入的容器，可填 Transport、Garrison、TankBunker。
; 默认值：三种均允许。
```

```ini
[主动技能 ID]
Action=StartUnitOrder
; 启动指定指令；需要 Trigger=Active 和 Active.Target=Area。

Action=CancelUnitOrder
; 取消当前指令；作为主动技能时需要 Active.Target=None。

Action.Order=(指令 ID)
; 指定要启动或限制取消的已注册指令。
; StartUnitOrder 必填；CancelUnitOrder 可省略。
```

移动、攻击、停止或部署等手动指令可以接管单位。指令随存档保留，单位死亡或所属方改变时结束。Lua 方法见[Lua 单位指令](../lua/orders.md)。
