# FPSMap_Project / Helios Outpost

Unreal Engine 5.8 的第一人称游戏工程。保留现有地图、角色、武器、宠物、菜单，以及仍与游戏耦合的编辑器、QA 和 MCP 代码。

**直接游玩请下载下方的 Windows 试玩版。** Git 仓库提供源码、配置和必要脚本；从源码运行完整游戏仍需自行合法取得并恢复本地素材。资源清单只描述文件，不提供原始素材下载，也不授予第三方素材的使用或再分发权。

## 下载游戏与演示视频

- [下载 Windows 64 位试玩版](https://github.com/Zkyyyds-Star/FPSMap_Project/releases/download/v0.1.0-preview.1/FPSMap_Project-Windows-x64-v0.1.0-preview.1.zip)：完整解压后运行 `Windows/HeliosOutpost.exe`，无需安装 Unreal Engine 或开发工具。
- [演示视频（1080p / 30 fps，约 2 分 8 秒）](https://github.com/Zkyyyds-Star/FPSMap_Project/releases/download/v0.1.0-preview.1/FPSMap_Project-demo.mp4)：上传原视频，保留原画质。
- [试玩版发布说明与已知问题](https://github.com/Zkyyyds-Star/FPSMap_Project/releases/tag/v0.1.0-preview.1) · [SHA256 校验文件](https://github.com/Zkyyyds-Star/FPSMap_Project/releases/download/v0.1.0-preview.1/SHA256SUMS.txt)

游戏包包含运行资源、第三方运行库和中文游玩说明。系统需为 Windows 64 位，并具备游戏要求的图形驱动和 DirectX 功能；如提示缺少 Visual C++ 运行库，可运行包内的 `Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe`。请保留整个目录结构，不要只复制 EXE。

当前为首个试玩版，两张生化地图仍有下文记录的原有 QA 失败。第三方素材的授权状态尚未逐项核实，发布成品不代表授予其中素材的提取、改作或再分发权。

## 环境与日常入口

- Windows 64 位；Unreal Engine **5.8.2**。
- Visual Studio 2022 Build Tools，安装 C++ 游戏开发所需的 MSVC 和 Windows SDK；构建使用引擎自带的工具链。
- PowerShell 5.1 或更新版本。以下命令从项目根目录执行。
- 双击 **开始游戏.cmd** 进入默认的探险生存；双击 **打开编辑器.cmd** 继续开发。
- 首次使用前先恢复本地资源并完成下方校验，再编译工程。两个入口使用编译后的 Unreal Editor 运行游戏或打开工程。

引擎路径按显式 `-EngineRoot`、`UE_ENGINE_ROOT` 环境变量、已注册安装的顺序查找。项目路径可以改变，不要求固定盘符。启动和打包默认使用项目内 `unreal/DerivedDataCache`，无需本机 Zen 服务；该缓存可重新生成。新增启动、构建和打包日志写入 `unreal/Saved`，不提交到 Git。保留的历史 QA 代码仍可能向根目录 `qa` 写报告，该目录同样忽略。

```powershell
# 显式指定引擎路径；替换为自己的安装目录
.\scripts\build_unreal.ps1 -Target Editor -Configuration Development -EngineRoot '<UE_5.8.2目录>'
.\scripts\build_unreal.ps1 -Target Game -Configuration Shipping -EngineRoot '<UE_5.8.2目录>'

# 日常启动；Game 为独立游戏窗口，Editor 为编辑器
.\scripts\launchers\launch.ps1 -Mode Game
.\scripts\launchers\launch.ps1 -Mode Editor

# 选择地图和原有武器预设
.\scripts\launchers\launch.ps1 -Mode Game -MapId HeliosAdventure -Preset Calamity

# 打包到项目外的独立目录（路径请自行替换）
.\scripts\package_unreal.ps1 -EngineRoot '<UE_5.8.2目录>' -ArchiveDirectory '<独立打包目录>'
```

不传 `-ArchiveDirectory` 时，打包默认写到项目同级的 `FPSMap_Project_packaged` 目录，Windows 游戏入口为包内的 `Windows/HeliosOutpost.exe`。

打包默认使用较低的 Cooker 并发，适合内存有限的机器；`-CookerCoreLimit 3` 只限制烘焙进程，不限制游戏运行或降低成品画质。打包进行完整烘焙，复用项目内派生数据缓存。

启动参数还支持 `-EngineRoot`、`-Wait`（等待本次启动结束）、`-Offscreen`（离屏渲染）、`-Headless`（无图形验收）和 `-DryRun`（检查并展示启动参数）。预设为 `Default`、`Calamity`、`Ultimatum`、`YunSummerMegatron`、`Fighter`、`SwordDog`。旧启动预设保存在 `scripts/launchers`；默认入口保持探险生存。

## 地图

| 正式地图 | MapId |
| --- | --- |
| 探险生存（默认） | `HeliosAdventure` |
| 鬼村 | `IkunGhostVillage` |
| 城镇 | `IkunTown` |
| 城市挑战 | `UrbanChallenge` |
| 生化金字塔 | `CFMBioPyramid` |
| 生化金字塔浮空版 | `CFMBioPyramidFloating` |

这六张地图列入正式打包名单。`HeliosEnemyTestbed` 和 `HeliosOutpost` 仍可从本地启动器打开，作为现有测试场景保留。

## 本地素材恢复与校验

`resources.manifest.json` 以稳定排序列出本地资源相对路径、字节数、SHA256、`license_status: unconfirmed` 和 `distribution: local_only`。清单覆盖 `unreal/Content`（不含 Python 编译缓存）以及下面三份仍被代码读取的本地输入：

- `exports/layout.json`
- `exports/cfm_biopyramid_floating/water/water_visual_fixture.json`
- `qa/cfm_bio_box_weapons/stun_controller_source_audit.json`

从自己的合规备份或其他合法来源取得对应资源后，按清单中的相对路径恢复到项目根目录。原始资源文件、存档、个人设置、原始制作素材和游戏成品均不进入 Git 源码提交；Release 单独提供已打包的可玩成品。工具不会自动提取或下载素材。

```powershell
# 快速检查清单中的全部文件是否存在、大小是否相符
.\scripts\check_resources.ps1

# 恢复备份或更新素材后，逐文件核对 SHA256
.\scripts\check_resources.ps1 -VerifyHash

# 仅在确认本地资源完整、且有意更改资源基线后更新清单
.\scripts\update_resource_manifest.ps1
```

校验失败会列出缺失或不符的文件并报错退出；启动和打包都会先校验。默认最多展示 30 条错误，完整列表可用 `try { .\scripts\check_resources.ps1 } catch { $_.TargetObject.failures }` 读取。脚本支持 `-ProjectRoot` 和 `-ManifestPath`，便于对恢复目录或干净副本校验。不要通过重写清单来掩盖缺失素材。

构建 C++ 源码与完整游戏验收是两个步骤：没有本地素材时仍可编译源码；运行、烘焙和完整打包需要恢复资源。游戏存档和用户设置位于 `unreal/Saved`，请在清理该目录前单独备份。

## GitHub 提交边界

`.gitignore` 使用公开文件白名单，只接纳说明、资源清单、两个入口、有效的启动/构建/维护脚本、`.uproject`、`unreal/Source` 和 `unreal/Config` 内的文本源码配置。`Content`、`exports`、`qa`、制作素材、缓存、存档、日志和打包输出均留在本地；`.gitattributes` 只定义文本规则，不要求素材使用 Git LFS。

仓库为 `Zkyyyds-Star/FPSMap_Project`；Windows 成品和演示视频通过 Release 分发，不进入 Git 历史，也不使用 Git LFS。每次上传前应检查实际暂存列表、凭据、本机路径和大文件；GitHub 普通 Git 单文件有 [100 MiB 上限](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)。

源码暂未添加开源许可证。第三方角色、武器、地图、模型、动画、音频、字体等素材的权利归各自权利人，授权状态尚未逐项核实；资源清单中的本地保留状态不能作为公开素材的许可。

## 验收记录

2026-10-21 使用 UE 5.8.2 和 Visual Studio 2022 Build Tools 整理与验收。完整本地资源逐文件 SHA256 校验通过；原始制作素材、旧 Git 数据、原有存档和个人设置已在项目外备份。源码只修复一处 Shipping 编译兼容问题：为引擎在 Shipping 中不提供的 AI 调试描述日志添加编译条件，AI 行为逻辑未改。

| 实测项目 | 结果与范围 |
| --- | --- |
| Editor Development | 干净构建成功；最终修订增量构建成功 |
| Game Shipping | 构建成功；首次遇到的诊断 API 编译问题已修复 |
| 公开源码副本 | 无素材完整编译成功；最终修订在干净 Git 下重新生成规则并实际编译成功 |
| 副本资源恢复 | 11,572 个文件 SHA256 通过；恢复后单图核心玩法 13 项通过、0 失败 |
| 四张探险地图 | 81 项通过、0 失败；覆盖选装、移动、跳跃、开火、换弹、死亡／复活重开和抽样切图 |
| 两张生化地图内置 QA | 213 项通过、16 条失败记录；同名失败在整理前 DLL 中复现，未发现新增失败，仍保留为未通过 |
| 渲染与音频 | 实际菜单截图检查；音频输出、静音恢复、18 项设置重启恢复及两首音乐续播，共 152 项通过、0 失败 |
| 六地图独立 Shipping 包 | 完整烘焙、打包成功；项目外运行 52 项通过、0 失败，覆盖六图加载／存活、四张探险地图核心输入及抽样敌人活动 |
| 统一启动入口 | 清理生成物后，系统 PowerShell 5.1 实际启动、菜单／渲染／项目内缓存通过，定时正常退出；14 项通过、0 失败 |
| 试玩版下载 ZIP | 最终压缩包 CRC 与 116 个解压文件 SHA256 通过；从解压副本启动，默认地图、菜单、开始游戏、存活及实际渲染共 9 项通过、0 失败；测试结束后主动关闭验收进程 |

这不是全组合验收。独立包的两张生化地图只验证加载和存活；其余输入、音频及完整流程未在该次包验收中覆盖。生化 QA 中的模型身份、补给与猎手生命周期仍有原有失败；部分敌人活动、全部角色／武器／宠物组合、每个旧预设的完整流程、启动器可选 `-Wait` 分支以及物理扬声器听感未全部覆盖。源码副本编译与素材恢复成功也不等于全部游戏流程通过。

本轮工程由约 **37.81 GiB** 整理到 **4.85 GiB**，其中本体约 **3.32 GiB**、本轮重新生成并保留的启动资源缓存约 **1.54 GiB**。项目外独立游戏包约 **1.79 GiB**；原始素材、旧 Git 和验收备份另计，工程缩小不代表同等磁盘空间已释放。公开 Git 文件约 8.2 MiB，素材和缓存不进入提交。
