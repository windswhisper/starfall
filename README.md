# Starfall（流火）

《红色警戒 2：尤里的复仇》的 32 位 DLL 扩展。通过 INI 配置单位被动、科研选项、国家限制和前置项目。玩家可通过地图触发器或建筑获取科研点，每 100 点购买一项研究。

## 在 Windows 上编译

安装 Git 和 Visual Studio 2022（Community 或 Build Tools）。勾选“使用 C++ 的桌面开发”，包含 MSVC v143 x86/x64 工具和 Windows SDK。编译本身无需安装游戏。

```bat
git clone --recurse-submodules https://github.com/windswhisper/starfall.git
cd starfall
.\build.cmd
```

产物为 `build\Starfall.dll`。如果已普通克隆仓库，可直接运行脚本补齐 YRpp。更新源码后用 `git submodule update --init` 同步依赖到项目记录的提交。GitHub 的 Download ZIP 不含子模块，请使用 Git 克隆。

## 运行与测试

当前版本针对《远征》测试环境的 `gamemd.exe`（Syringe 报告的 CRC 为 `0x54CC0A13`）。流火会在加载时检查此值。步兵原地转换使用 Ares 3.0p1 的接口，其他游戏版本和扩展组合需另行验证。

1. 把编译出的 `Starfall.dll`、`Starfall.ini` 及 `assets/research/` 下的六个 **PCX 文件**放入游戏根目录，与 `gamemd.exe` 同级。
2. 使用游戏原有的 Syringe 启动方式进入对局。战场右上角的图标打开科研面板。
3. 可选：将 `maps/` 中的地图放入客户端的 `Maps/Custom/`。在 `Starfall - Research Points Test` 选择 1 号出生位，地图触发器先给 90 点，科技医院运行 150 帧后补齐到 100 点。

完整可用示例见 [Starfall.ini](Starfall.ini)。保持科研项目 ID 稳定可以让后续版本继续识别存档中的研究记录。

## 源码与依赖

- `src/`：游戏事件、被动、科研、界面和存读档。
- [YRpp](https://github.com/Phobos-developers/YRpp) 通过 Git 子模块固定版本，提供编译所需的游戏接口头文件。项目开发参考了 [Phobos](https://github.com/Phobos-developers/Phobos)。
- Starfall 的许可证见 [LICENSE](LICENSE)。外部依赖保留其上游声明。游戏本体、第三方 DLL 和加载器需自行准备。
