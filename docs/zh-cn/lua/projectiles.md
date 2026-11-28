# Lua 抛射体运动

[返回目录](../index.md)

抛射体脚本为每个飞行中的弹体独立控制路径。它属于 ActSp 的抛射体运动功能，与普通 `ctx` 回调使用不同的 `p` 上下文。

```ini
[抛射体类型 ID]
ProjectileScript=(Lua 文件名)
; 指定抛射体运动脚本。
; 必填；需要可见抛射体，不能同时使用 Phobos Trajectory 控制器。

ProjectileScript.Life=(整数帧)
; 每发脚本抛射体的最大存续时间。
; 默认值：90；范围：1–1000000。
```

脚本可提供 `onLaunch(p)`、`onTick(p)` 和 `onTravel(p, from, to)` 回调。发射回调在第 0 帧执行；飞行更新从第 1 帧开始。移动路径回调在每个非零移动步长执行，即使飞行更新暂时休眠也会触发。飞行期间需要保存的数据放在 `p.state`。

## 抛射体上下文

| 字段或方法 | 含义 |
| --- | --- |
| `p.age`、`p.life` | 已经过的帧数和最大存续帧数。 |
| `p:weaponSpeed()` | 发射武器的原始 INI `Speed`。 |
| `p:speed()`、`p:launchSpeed()` | 当前速度和发射初速度的大小，单位为格／帧。 |
| `p:position()`、`p:velocity()` | 三维位置和速度；位置的 Z 为世界绝对高度。 |
| `p:heightAboveGround()` | 当前相对地面的高度，单位为格。 |
| `p:target()`、`p:targetPoint()` | 存活地图目标句柄及三维瞄准点；失去目标后保留最后瞄准点。目标句柄支持 `id`、`alive`、`position` 和 `type`。 |
| `p:firer()` | 存活的原始开火者句柄，否则为 `nil`。 |
| `p:groundPoint(point)` | 返回指定位置的地面或桥面坐标；省略时使用当前位置。 |
| `p:nearbyUnits(point, radius, filter)` | 查询附近单位；半径 0–128 格，结果按对象 ID 排序。 |
| `p:setVelocity(x, y, z)` | 从当前帧开始修改持续速度。 |
| `p:moveTo(x, y, z)` | 指定当前帧的移动终点；下一帧恢复持续速度。 |
| `p:steerTo(point, speed, maxTurn)` | 向三维位置转向，单次最大转角为 0–180 度。 |
| `p:detonate()` | 在当前位置结束飞行；具体结束弹头取决于普通或穿透抛射体配置。 |
| `p:action(name, fields)` | 在抛射体上下文执行受支持的结果，返回是否成功。 |

抛射体动作支持 `DetonateWarhead`、`PlayAnim`、`PlaySound`、`SpawnParticleSystem` 和 `FireWeapon`。脚本路径仍会检查支持的墙体、障碍和护盾；启用 `Piercing.Enabled` 时沿实际运动路径判定穿透。
