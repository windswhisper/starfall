# 流火更新日志

## 0.40（2026-11-28）

- 科研新增项目计数器、条件阶段奖励和多档羁绊；支持重复研究、混池抽取和自定义稀有度颜色。
- Lua 脚本运行环境支持缓存复用。
- 新增区域锁定打击和自动进入指令，支持在高级指令栏配置。自动进入可分别分配载员、驻军和坦克碉堡的位置。
- ActSp 增加实体围墙及投射物阻挡、乘客与单位弹射、载具跳跃等功能。实体围墙的默认最大间隔为 4 格。
- ArtSp 新增曳光弹、持续光束和单位头顶注释。资金与科研点结果支持飘字提示。

- 一次性结果累计的火力和装甲默认上限从 300% 提高到 1000%，可在 `[Starfall.PassiveLimits]` 中分别设置 `Firepower` 和 `Armor`。

- 超时空传送、载具跳跃、乘客弹射和单位弹射支持在 INI 中填写 `Action.Destination=Target`，以执行时事件目标所在格为目的地。例如受伤后可传送到攻击者附近。

- **不兼容变更**：移除 `Action.PercentOfDamage`。治疗、伤害、获得资金和获得科研点统一使用 `Action.Amount`。旧配置中的 `Action.PercentOfDamage=25%` 应改为 `Action.Amount=$(Event.Damage * 25%)`，并删除原字段。`Event.Damage` 仅适用于伤害触发时机。含旧字段的结果会被判为无效，即使同时填写了 `Action.Amount`。科研详情中的 `{percent}` 占位符也已移除，可直接写明比例。

- `Action=Damage` 的默认弹头由 `C4Warhead` 改为 `Super`，INI 和 Lua 的伤害结果均生效。可用 `Action.Warhead` 或 Lua 的 `Warhead` 指定其他弹头。

- 被动首次获得时，`This.Stack` 从原来的 0 层改为 1 层。获得事件和持续效果均按 1 层起算，`Stack.Max` 包含初始层。`ChangePassiveStack` 将普通层数扣至 0（含超额扣减）时自动移除被动。乘客、科研提供的层数及仍由父被动授予的子被动保留。
- 旧 INI 如果用 `This.Stack + 1` 补上初始层，请改为 `This.Stack`，避免多算一层。
