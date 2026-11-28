# 素材引用

[返回目录](../index.md)

本节汇总图标、界面文字、音效和动画的引用规则。各功能的具体参数见对应小节。

## 图标与贴图

科研和主动技能支持 PNG 或游戏可加载的 PCX 图标，目标指示器使用透明 PNG。PNG 的透明区域由 alpha 通道决定，不透明的黑色像素不会自动透明。主动技能图标会裁切到按钮开口，重要内容应放在中央。

配置字段见[科研项目](../research/projects.md)、[主动技能图标](../active/icons.md)、[目标指示器](../active/indicators.md)和[武器曳光](../artsp/tracers.md)。

## 文本与名称

科研、主动技能和单位头顶注释可配置文本或文本 ID。科研项目的 `Name`、`Summary`、`Detail` 和 `TargetLabel` 分别用于名称、简述、详情和作用对象显示。

部分单被动科研详情支持 `{amount}`、`{count}`、`{chance}`、`{cooldown}` 等占位符，资金奖励支持 `{credits}`。多结果被动的金额占位符取首个结果；持续状态和组合科研的自定义详情不按每个子结果展开。

配置字段见[科研项目](../research/projects.md)、[主动技能名称](../active/icons.md)和[单位头顶注释](../artsp/labels.md)。

## 音效

科研界面可使用内置提示音或外部 WAV；`PlaySound` 结果使用原版音效 ID，或以 `wav:` 开头的文件路径。外部 WAV 支持未压缩 PCM、8 或 16 位、单声道或立体声，采样率为 8000–48000 Hz，文件最大 16 MiB。

配置字段见[科研提示音](../research/interface.md)和[一次性结果参数](../passives/actions/catalog.md)。

## 动画与染色

流火可引用游戏或模组已注册的动画类型，也可通过 Lua 创建和移动动画。动画类型 ID 与图像文件名是不同概念。

配置字段见[被动动画与染色](../passives/continuous/visuals.md)、[一次性结果参数](../passives/actions/catalog.md)和 [Lua 可移动动画](../lua/animations.md)。
