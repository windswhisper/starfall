# Lua 运行环境与限制

[返回目录](../index.md)

流火使用受限制的 Lua 运行环境，脚本只能访问提供的标准库和流火接口。运行错误会记录在 `Starfall.log` 并结束当前回调，已执行的结果不会回滚。

## 标准库

可用环境包含受限基础函数及 `table`、`string`、`math`、`utf8` 库；可以使用 `ipairs`。

以下功能不可用：`io`、`os`、`package`、`debug`、`coroutine`、`require`、`load`、`loadfile`、`dofile`、`collectgarbage`、`print`、`warn`、`getmetatable`、`setmetatable`、`pcall`、`xpcall`、`pairs`、`next`、`tostring`、`rawset`、`math.random`、`math.randomseed`、`string.dump` 和 `string.format`。同步随机数使用[同步随机接口](timers.md)。

## 运行上限

| 项目 | 上限 |
| --- | --- |
| 单个 Lua 文件 | 1 MiB。 |
| 单次模块加载或回调的 Lua 分配内存 | 8 MiB。 |
| 单次模块加载或回调的 Lua 虚拟机指令 | 约 100000 条。 |
| 单次回调的原生动作与查询预算 | 4096；某些查询消耗多于一次。 |
| 待执行任务 | 普通单位和序列／地图各自最多 1024 个。 |
| 持久状态键 | 普通单位和序列／地图各自最多 2048 个。 |
| Lua 管理的动画 | 全局最多 2048 个。 |

这些数值是失败边界，不是性能目标。联机时，脚本不应根据本地界面显示、音效结果、系统时间或其他不同步的数据改变游戏状态。
