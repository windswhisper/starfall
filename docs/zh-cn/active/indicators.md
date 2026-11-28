# 主动技能目标指示器

[返回目录](../index.md)

目标指示器显示技能的施法范围、目标位置和方向。贴图只影响本地显示，不改变有效射程或实际命中范围。

```ini
[主动技能 ID]
Active.Indicator.Texture=(PNG 文件路径)
; 设置地面目标预览贴图；Area 使用范围圈，Unit、Point、Direction 使用目标准星。
; 默认值：内置范围圈或准星。

Active.Indicator.Opacity=(整数百分比)
; 按比例调整贴图透明度；0 隐藏贴图预览。
; 默认值：90；范围：0–100。

Active.Indicator.Arrow.Texture=(PNG 文件路径)
; 设置 Direction 技能使用的向上箭头贴图。
; 默认值：内置箭头。

Active.Indicator.Arrow.Width=(小数格数)
; 设置方向箭头的世界坐标宽度。
; 默认值：0.75；范围：0.1–8。

Active.Indicator.Arrow.Head=(整数百分比)
; 设置箭头顶部端帽占裁切后图片高度的比例。
; 默认值：25；范围：1–98；与 Tail 相加必须小于 100。

Active.Indicator.Arrow.Tail=(整数百分比)
; 设置箭头底部端帽占裁切后图片高度的比例。
; 默认值：37；范围：1–98；与 Head 相加必须小于 100。
```

指示器使用透明 PNG 原图；范围圈和准星按地面视角投影到游戏画面。方向箭头分为头、中、尾三段，改变长度时只拉伸中段。有效目标显示亮蓝色，无效目标显示红色；范围圈自转，目标准星循环缩放。

游戏目录中的 `sfindicator_ring.png`、`sfindicator_target.png` 和 `sfindicator_arrow.png` 可覆盖内置默认图；单个技能也可通过上述字段单独指定。自定义图片缺失时回退内置图。
