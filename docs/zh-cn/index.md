# StarfallLab 流火实验室

面向模组制作者的配置参数与接口参考。配置字段按所属功能组织；各功能的注册方式写在对应章节中。

本词典按功能提供参数和接口查询。安装、使用与兼容性说明见[项目 README](../../README.md)。

## 目录

### 基础配置

- [通用类型、单位与数值约定](basics/conventions.md)

### 被动能力

- [被动能力简介](passives/overview.md)
- 注册与来源
  - [注册、定义与授予](passives/foundation/registration.md)
  - [弹头授予被动](passives/foundation/warhead-grants.md)
  - [被动来源](passives/foundation/sources.md)
  - [被动层数](passives/foundation/stacks.md)
- 触发、条件与目标
  - [触发时机](passives/events/triggers.md)
  - [事件条件](passives/events/event-filters.md)
  - [持有者状态条件](passives/events/holder-state.md)
  - [持续状态专用条件](passives/events/continuous-state.md)
- 一次性结果
  - [结果概述](passives/actions/index.md)
  - [结果目标](passives/events/targets.md)
  - [一次性结果参数](passives/actions/catalog.md)
  - [多结果](passives/actions/multiple.md)
  - [随机结果](passives/actions/random.md)
  - [强制位移](passives/actions/force-move.md)
  - [超时空传送](passives/actions/teleport.md)
  - [载具跳跃](passives/actions/jump.md)
  - [弹射乘客与单位](passives/actions/ejection.md)
  - [挂载子单位](passives/actions/attachments.md)
  - [自定义信号](passives/events/signals.md)
- 持续性结果
  - [持续效果概述](passives/continuous/index.md)
  - [属性倍率](passives/continuous/stats.md)
  - [被动属性上下限](passives/continuous/limits.md)
  - [武器与特殊能力](passives/continuous/weapons-abilities.md)
  - [抛射体护盾](passives/continuous/projectile-shield.md)
  - [关联被动与光环](passives/continuous/aura-links.md)
  - [被动动画与染色](passives/continuous/visuals.md)
- 数值公式
  - [公式语法与规则](passives/formulas/index.md)
  - [常用变量](passives/formulas/variables.md)

### 主动技能

- [主动技能简介](active/overview.md)
- 基础配置
  - [主动技能注册与授予](active/registration.md)
  - [目标选择与释放方式](active/targeting.md)
  - [主动技能冷却](active/cooldown.md)
- 界面与效果
  - [主动技能名称与图标](active/icons.md)
  - [主动技能目标指示器](active/indicators.md)
  - [主动技能结果](active/results.md)

### 科研三选一

- [科研三选一简介](research/overview.md)
- 项目与结果
  - [科研项目](research/projects.md)
  - [科研结果](research/effects.md)
- 抽取与奖励
  - [科研抽取与全局设置](research/selection.md)
  - [免费与随机科研奖励](research/free-choices.md)
- 进度与组合
  - [科研计数器](research/counters.md)
  - [科研阶段](research/stages.md)
  - [科研羁绊](research/bonds.md)
- 建筑科研
  - [建筑定时产出科研点](research/building-points.md)
  - [建筑免费科研](research/building-free.md)
- 界面与资源
  - [科研界面与提示](research/interface.md)

### Lua 脚本

- [Lua 脚本简介](lua/overview.md)
- 脚本入口
  - [Lua 脚本入口](lua/entries.md)
  - [武器开火脚本](lua/weapon.md)
  - [弹头引爆脚本](lua/warhead.md)
- 上下文与查询
  - [Lua 上下文与事件数据](lua/context.md)
  - [单位句柄与属性](lua/units.md)
  - [玩家与附近单位查询](lua/queries.md)
- 指令与结果
  - [Lua 单位指令](lua/orders.md)
  - [Lua 结果调用](lua/actions.md)
- 状态与表现
  - [状态、定时任务与同步随机](lua/timers.md)
  - [Lua 可移动动画](lua/animations.md)
  - [Lua 抛射体运动](lua/projectiles.md)
- [Lua 运行环境与限制](lua/limits.md)

### 地图与小队

- 地图触发与小队脚本
  - [地图触发动作](map/trigger-actions.md)
  - [小队脚本动作](map/team-actions.md)
- 预设与单位绑定
  - [被动授予预设](map/grant-presets.md)
  - [地图 Lua 预设](map/lua-presets.md)
  - [地图单位被动绑定](map/unit-bindings.md)

### 特殊行为

- 单位与建筑
  - [子机与地面守卫](actsp/drones.md)
  - [高级驻军](actsp/garrison.md)
  - [建筑高台](actsp/platform.md)
  - [移动建造节点](actsp/build-nodes.md)
  - [实体围墙](actsp/walls.md)
  - [持续单位指令](actsp/orders.md)
- 武器与抛射体
  - [建筑拦截抛射体](actsp/projectile-blocker.md)
  - [Lua 抛射体路径](actsp/projectile-script.md)
  - [穿透抛射体](actsp/piercing.md)
  - [相对目标的武器发射位置](actsp/fire-origin.md)

### 视觉效果

- 武器与单位表现
  - [武器曳光](artsp/tracers.md)
  - [持续光束](artsp/beams.md)
  - [单位头顶注释](artsp/labels.md)

### 杂项

- 独立规则
  - [自定义拾取物](misc/pickups.md)
  - [自定义超武](misc/superweapons.md)

### 额外说明

- [素材引用](resources/reference.md)
- [Starfall.log 日志](resources/log.md)
