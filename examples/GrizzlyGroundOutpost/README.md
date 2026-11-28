# 灰熊地面哨站

盟军建造厂和兵营就绪后，可在防御栏以 1000 资金建造 **Grizzly Ground Outpost**。哨站使用原版机枪碉堡外观。选中哨站，按设置工厂集合点的方式点击空地，它就会派一辆原版灰熊坦克（`MTNK`）沿地面前往。坦克会绕开建筑，用自己的炮塔和武器警戒目的地周围 5 格。再次设置集合点可以改派同一辆坦克。

## 安装

1. 按[流火安装说明](../../README.md#运行与测试)加载包含 ActSp 的 `Starfall.dll`。
2. 将 `GrizzlyGround.rules.ini` 放入游戏目录。在启用了 Ares 的 `rulesmd.ini` 的 `[#include]` 节，用空闲编号加入一行，例如 `50=GrizzlyGround.rules.ini`。也可把片段里的建筑注册项与建筑节合入现有规则。建筑和坦克复用游戏已有美术资源。
3. 重启游戏，开始新对局，建造哨站并指定可通行的地面格。哨站周围需要留出至少一个可放置坦克的相邻格。出口全部堵住时，哨站会等到有空位再出车。

## 规则

```ini
[ASGNDPOST]
Drone.Mode=Guard
Drone.GroundUnit=MTNK
Drone.GuardRange=5
Drone.GroundRespawnDelay=300
```

每座哨站管理一辆坦克。坦克阵亡后，哨站等待 300 个游戏帧，再派车去当前警戒格。哨站被摧毁或出售时，现存坦克归原玩家继续使用。哨站被占领时，原坦克也归原玩家，新主人按补员时间获得自己的坦克。存档会记录警戒格、坦克身份和补员进度。

如果希望哨站建成后自动派车，可以在建筑节加上 `Drone.GuardCell=84,71`，坐标为该地图的绝对格。安装支持子机指令的流火后，也可以从哨站的 Lua 技能调用 `ctx.self:setDroneGuardCell(ctx.point)`。

### 可选车位

在 `[ASGNDPOST]` 增加以下两行，灰熊就会停在碉堡的中心格待命。集合点设在远处时出动，设在碉堡旁边时回位。受损灰熊也能停车，停靠不会回血。

```ini
Drone.UseBuildingDocks=yes
NumberOfDocks=1
```
