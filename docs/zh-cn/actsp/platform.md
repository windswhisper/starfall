# 建筑高台

[返回目录](../index.md)

建筑高台为高于周围地面的建筑平台，可承载地面单位和普通建筑。新配置使用完整矩形外围地基，中间留空作为台面。

```ini
[高台建筑类型 ID]
HighPlatform.Enabled=(布尔值)
; 为使用受支持地基的建筑启用高台行为。
; 默认值：no。

HighPlatform.AllowLand=(布尔值)
; 允许在完全由可建造陆地组成的区域放置。
; 默认值：yes。

HighPlatform.AllowWater=(布尔值)
; 允许在完全由开阔水面组成的区域放置。
; 默认值：no；不支持陆地与水面混合地基。

HighPlatform.Enter=(布尔值)
; 允许符合条件的友方步兵和载具进入该高台。
; 默认值：yes。

[步兵或载具类型 ID]
HighPlatform.Enter=(布尔值)
; 是否允许此类型进入启用入口的友方高台。
; 默认值：yes。
```

高台的地基参数写在 `artmd.ini` 对应的建筑图像节中：`Foundation=Custom`，`Foundation.X` 和 `Foundation.Y` 指定外部宽度与长度，两个尺寸均至少为 3；`Foundation.n` 列出完整的一格宽外围，内部留空。台面抬高四级地形高度；外围与台面必须平坦、同高且满足地形和占用检查。

单位可从高台外围进入空闲台面；选择高台并部署可让台面单位下来。出售或摧毁高台会恢复原地形，同时摧毁台面上的建筑并使地面单位坠落死亡。高台图像可在 `artmd.ini` 配置 `CanHideThings=no`，以避免遮住台面单位。
