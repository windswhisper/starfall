# 流火示例

下面的入门示例都使用《尤里的复仇》原版单位和流火内置界面。任选一项，把该目录的 Starfall.ini 放在已加载 Starfall.dll 的游戏根目录，然后开始新对局。已有同名配置时，请合并对应段，给注册表分配空闲编号，并保留单位已有的被动列表。

| 示例 | 安装文件 | 在游戏里观察 |
| --- | --- | --- |
| 步兵弹射投送 | [PassengerEjection/Starfall.ini](PassengerEjection/Starfall.ini) | 给战斗要塞装入步兵，点「弹射投送」后指定落点。车上步兵每隔 9 帧弹出一人，各自开伞着陆。 |
| 地面载具跳跃 | [VehicleJump/Starfall.ini](VehicleJump/Starfall.ini) | 多选灰熊，点「载具跳跃」后点击河对岸的地面。每辆车单独找落点。点击可碾压的敌兵可尝试落地碾压。 |
| 击杀后维修 | [Passive/Starfall.ini](Passive/Starfall.ini) | 用一辆受损的盟军灰熊坦克击杀敌人，坦克恢复 50 点生命，最多到满血。 |
| 一次科研三选一 | [Research/Starfall.ini](Research/Starfall.ini) | 开局获得 100 科研点。打开科研面板，可选 500 资金、灰熊降价 20% 或步兵降价 10%。选中一项后核对资金或生产栏报价。 |
| 武器脚本双发 | [WeaponScript/Starfall.ini](WeaponScript/Starfall.ini) 和 [DemoDoubleShot.lua](WeaponScript/DemoDoubleShot.lua) | 两个文件都放在游戏根目录。命令普通灰熊攻击移动中的目标，第一炮瞄准单位，6 帧后第二炮打向原格子。按 Ctrl 对地攻击时，两炮都打向该格。 |

## ActSp 示例

[实体围墙](EntityWalls/EntityWalls.rules.ini)包含可独立建造的苏军节点和墙段。通过 Ares 引入这一个规则文件即可使用原版美术。

[灰熊警戒哨站](GrizzlyOutpost/README.md)使用机枪碉堡外观，设置集合点后派出一辆低空灰熊。目录中包含规则片段、美术配置和完整的灰熊子机模型。

[自动进入](AutoEnter/Starfall.ini)让步兵进入有剩余容量的运输载具或驻军建筑，或让坦克进入空碉堡。在对应模式的高级指令栏 `ButtonList` 中加入 `AutoBoard` 即可使用。

[灰熊地面哨站](GrizzlyGroundOutpost/README.md)派出一辆原版灰熊坦克，沿地面绕障前往集合点。坦克阵亡后，哨站会在补员时间到达后重新派车。

[混凝土高台](HighPlatform/README.md)是可以建造、出售和摧毁的苏军高台。它将 4×4 地形升高四级，上方可以放置建筑和单位。

[苏军小型高台](CompactHighPlatform/README.md)使用 3×3 外圈和一格台面，墙顶标识带随阵营色变化。

## 建筑球形护盾靶场

将 [Starfall_BuildingShield.map](BuildingShield/Starfall_BuildingShield.map) 放入游戏的 `Maps/Custom`，将 [BuildingShieldDemo.rules.ini](BuildingShield/BuildingShieldDemo.rules.ini) 放入游戏根目录，并从 `rules_include.ini` 的 `[#include]` 引入。建筑的护盾绑定写在地图里。

在标准遭遇战选择 **Starfall - Building Shield Range**，玩家选 1 号出生点，敌对电脑选 2 号。每条靶道各有一辆灰熊。用灰熊攻击同一靶道里塔后的磁能反应炉。磁暴线圈每拦下一炮损失 30 生命。炮弹被光棱塔完全吸收时，仍能看到弹头在护盾边缘的命中动画。两座反应炉都保持满血。

## ArtSp 示例

**哨戒炮曳光**。从规则配置引入 [SentryTracer.rules.ini](SentryTracer/SentryTracer.rules.ini)，从美术配置引入 [SentryTracer.art.ini](SentryTracer/SentryTracer.art.ini)。开一局苏军遭遇战，建造哨戒炮，在射程内按住 Ctrl 对地开火。每发机枪弹都会显示一道金黄色曳光。将 `Tracer.Every` 改为 `3`，可对比第 1、4、7……发显示的节奏。

[中文规则手册](../docs/manual.html) · [英文参数参考](../docs/manual.md)
