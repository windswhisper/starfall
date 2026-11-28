# 科研结果

[返回目录](../index.md)

科研结果可以直接写在科研项目节中，也可以写在 `Effects` 指向的独立结果节中。以下每张参数表对应一种 `Effect`；结果节必须明确填写 `Effect`。

## 授予被动

```ini
[科研项目 ID 或结果节 ID]
Effect=GrantPassive
; 向符合条件的现有及后续己方单位授予被动。

Passive=(被动 ID)
; 指定已注册的被动。
; 必填。

TargetTypes=(类型 ID 或分类列表)
; 可填具体类型、*、@Infantry、@Vehicle、@GroundVehicle、@Aircraft、@Building。
; 必填；允许混合填写。
```

`@GroundVehicle` 包含符合地面载具条件的矿车、运输车和基地车；不包含舰船及按配置归类为空中单位的载具。

这里的具体类型 ID 不会因为规则中不存在而自动使项目失效；不存在的类型不会匹配到受益单位。被动 ID 无效、目标列表为空或使用未知分类则会使结果无效。

## 获得资金

```ini
[科研项目 ID 或结果节 ID]
Effect=GiveCredits
; 一次性向获得科研的玩家发放资金。

Credits=(正整数)
; 指定资金数量。
; 默认值：0；有效奖励需要正数。
```

## 解锁建造

```ini
[科研项目 ID 或结果节 ID]
Effect=UnlockTech
; 授予已有单位或建筑类型的生产许可。

Technology=(类型 ID)
; 指定要解锁的单位或建筑。
; 必填；该类型须已在游戏规则中定义。
```

## 免费科研选择

```ini
[科研项目 ID 或结果节 ID]
Effect=GrantFreeResearch
; 向玩家提供一次免费选择科研的机会。

Reward.ResearchOptions=(科研项目 ID 列表)
; 限制候选项目范围。
; 默认值：省略时从已注册项目中筛选。

Reward.Rarity=(Any、Common、Rare、Epic 或 Insane)
; 限制候选项目的稀有度。
; 默认值：Any，不限制。

Reward.Count=(整数)
; 指定发放的免费科研选择次数。
; 默认值：1；此结果仅接受 1。
```

## 获得随机科研

```ini
[科研项目 ID 或结果节 ID]
Effect=GrantRandomResearch
; 立即随机获得科研项目。

Reward.Rarity=(Any、Common、Rare、Epic 或 Insane)
; 限制随机奖励的稀有度。
; 默认值：Any，不限制。

Reward.Count=(整数)
; 直接抽取的次数。
; 默认值：1；范围：1–256。
```

随机奖励链会排除本链中已经获得的项目，避免循环反复触发。指定稀有度没有合格项目时不会回退到其他稀有度。

## 替换生产

```ini
[科研项目 ID 或结果节 ID]
Effect=ReplaceProduction
; 将获得者生产列表中的一种类型替换为另一种类型。

FromType=(类型 ID)
; 原类型。
; 必填。

ToType=(类型 ID)
; 新类型。
; 必填；必须与原类型同类别；建筑还要求 BuildCat 相同。
```

该结果只替换生产，不直接变形已有单位；替换链不受支持。建筑生产替换不要求两个建筑占地一致。

## 步兵转换

```ini
[科研项目 ID 或结果节 ID]
Effect=ConvertInfantry
; 转换获得者当前符合条件的步兵。

FromType=(步兵类型 ID)
; 原步兵类型。
; 必填。

ToType=(步兵类型 ID)
; 目标步兵类型。
; 必填。
```

## 载具转换

```ini
[科研项目 ID 或结果节 ID]
Effect=ConvertVehicle
; 转换获得者当前符合条件的载具。

FromType=(载具类型 ID)
; 原载具类型。
; 必填。

ToType=(载具类型 ID)
; 目标载具类型。
; 必填。
```

Drive 和可降落的 Jumpjet 载具可通过 Phobos 的转换接口相互转换；载具须静止，位于可通行地面且不在桥上。

## 修改指定类型价格

```ini
[科研项目 ID 或结果节 ID]
Effect=ModifyTypeCost
; 修改获得者生产指定类型时的价格。

TargetTypes=(类型 ID 列表)
; 指定受影响的具体单位或建筑类型。
; 必填；不支持分类选择器。

CostModifier=(非零有符号整数或百分比)
; 增加或减少固定金额，或调整价格百分比。
; 必填。
```

这里列出的具体类型 ID 必须都已在游戏规则中注册；存在未注册类型时，此结果无效。

## 修改分类价格

```ini
[科研项目 ID 或结果节 ID]
Effect=ModifyCategoryCost
; 修改获得者生产某个侧栏分类的价格。

Category=(Building、Defense、Infantry 或 Vehicle)
; 分别代表普通建筑、防御建筑、步兵和载具分类。
; 必填；Vehicle 包含舰船与飞机。

CostModifier=(非零有符号百分比)
; 增加或减少该分类的价格百分比。
; 必填。
```

生产订单开始时锁定价格，取消时退还实际支付金额。多个科研百分比调整相加，固定金额调整随后计算。

## 变体继承科研目标

```ini
[变体类型 ID]
Starfall.ResearchAs=(基础类型 ID)
; 使以基础类型为目标的 GrantPassive 和 ModifyTypeCost 也作用于此变体。
; 只接受一个直接基础类型 ID。
```

只针对变体的效果仍只影响变体。同一结果同时列出基础类型和变体时只应用一次，价格按各类型自身的基础价格计算。

## 结果执行规则

普通购买时，暂时无法生效的结果会跳过；至少有一个结果成功才会扣除点数并记录科研。已经执行的结果不会因后续结果失败而回滚，跳过的一次性结果也不会在之后补发。
