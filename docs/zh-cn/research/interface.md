# 科研界面与提示

[返回目录](../index.md)

这些参数控制科研标题、选择公告、稀有度名称和颜色，以及按钮和选卡提示。单个项目的名称、说明、图标和作用对象文字在[科研项目](projects.md)中配置。

## 界面与公告

```ini
[Starfall.Research]
Title=(文本或文本 ID)
; 科研选择面板的标题。
; 默认值：请选择升级。

AnnounceSelection=(布尔值)
; 玩家成功选择普通或免费科研后，向所有玩家公告玩家名和项目名。
; 默认值：yes。

ShowAvailableAnimation=(布尔值)
; 普通科研变为可购买或新获得免费选择时，播放汇向科研按钮的动画。
; 默认值：yes；与提示音可分别关闭。
```

## 稀有度名称

```ini
[Starfall.Research]
RarityName.Common=(文本或文本 ID)
; 普通档显示名称。
; 默认值：普通。

RarityName.Rare=(文本或文本 ID)
; 稀有档显示名称。
; 默认值：稀有。

RarityName.Epic=(文本或文本 ID)
; 史诗档显示名称。
; 默认值：史诗。

RarityName.Insane=(文本或文本 ID)
; 疯狂档显示名称。
; 默认值：疯狂。
```

修改显示名称不改变 `Rarity` 和奖励筛选使用的英文档位 ID；留空时使用该档默认名称。

## 稀有度颜色

```ini
[Starfall.Research]
RarityColor.Common=(红,绿,蓝)
; 普通档界面颜色。
; 默认值：使用该档默认配色；图标保留原图颜色。

RarityColor.Rare=(红,绿,蓝)
; 稀有档界面颜色。
; 默认值：该档默认配色。

RarityColor.Epic=(红,绿,蓝)
; 史诗档界面颜色。
; 默认值：该档默认配色。

RarityColor.Insane=(红,绿,蓝)
; 疯狂档界面颜色。
; 默认值：该档默认配色。
```

颜色分量均为 0–255 的整数。颜色用于图标、卡片强调部分和提示标题，图标原有明暗及透明度会保留。

## 提示音

```ini
[Starfall.ResearchSounds]
Button=(内置音效名或 WAV 路径)
; 点击有效按钮时播放。
; 默认值：空，使用游戏原版点击音。

Cards=(内置音效名或 WAV 路径)
; 展开有候选卡片的面板时播放。
; 默认值：空，不播放。

Refresh=(内置音效名或 WAV 路径)
; 成功刷新卡片时播放。
; 默认值：空，不播放。

Select=(内置音效名或 WAV 路径)
; 成功选择科研时播放。
; 默认值：空，不播放。

Available=(内置音效名或 WAV 路径)
; 普通科研变为可购买或新获得免费选择时播放。
; 普通提示默认静音；免费科研省略此字段时使用内置提示音。
```

内置音效可填 `builtin:cards`、`builtin:select` 或 `builtin:available`。外部 WAV 支持未压缩 PCM、8 或 16 位、单声道或立体声，采样率为 8000–48000 Hz，文件最大 16 MiB。`Starfall.ini` 中明确留空 `Available` 可关闭免费科研提示音。
