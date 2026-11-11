# 影之刃零 · 瑟瑞斯（Ceris）MMD 角色替换 Mod

把《影之刃零》（Phantom Blade Zero）的「魂」替换为 MMD 角色「瑟瑞斯」，保留完整的 943 根 MMD 骨骼、785 个刚体、1,306 个关节与 29 项表情，并附带双手武器握持 IK 修正。

当前版本：**资源包 v37 + 运行库 v38.0**（2026-11-11）。本仓库同时是工程存档与安装包发布处。

> ⚠️ **本仓库必须保持私有。** 角色模型「瑟瑞斯-悠游 霄倾君怀」的原始授权明确禁止二次配布，详见 [Credits / 致谢](CREDITS.md)。

## 下载与安装

安装包：[`CerisMMD_PBZ_v37_v38.zip`](https://github.com/UmbrellaZG-2/PBZ-Ceris-MMD/releases)（Releases 页面）。

把压缩包解压后 **合并到游戏根目录**（例如 `E:\SteamLibrary\steamapps\common\PBZ`），即可得到：

```text
ProjectPBZ\Content\Paks\~mods\zzz_CerisMMD_v37_P.pak / .ucas / .utoc
platform\steam\ue4ss\Mods\CerisFullMMD\...
```

前置条件只有一个：游戏目录已安装 [UE4SS v3.0.1](https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/v3.0.1)（`dwmapi.dll` 与 `ue4ss\` 位于 `platform\steam\`）。完整步骤、验证方法与卸载方式见 [安装说明](docs/INSTALL.md)。

## 当前效果与限制

- 943 根 PMX 原骨 + 1 根适配根，共 944 骨；原层级、付与、IK 与 Bullet 物理全部保留，50,560 顶点 / 65,562 三角形。
- 29 项表情可通过 `expressions.json` 混合调节，约 1 秒热生效；未自动映射到游戏对白或口型。
- v38 双手握持 IK：161 组编辑器动作样本，最大掌心误差 0.039 毫米；实测日志多数帧 `grip_max_error_cm < 0.01`。
- 已知限制：实战画面尚未逐帧验收，极端动作下出现过单区间 3.22 cm 偏差；手臂最大伸长 1.3246 倍（上限 1.35 倍）；仅适配 `PBZ.exe` SHA-256 `4415933c…` 的游戏版本，游戏更新后可能失效。
- 更早的 v4–v36 迭代版本已清理，详见 [版本说明](docs/RELEASE_NOTES.md)。

## 工程存档

仓库保存当前制作状态，不含安装包本体（`.pak/.ucas/.utoc`、编译产物）：

| 目录 | 内容 |
| --- | --- |
| `runtime/` | UE4SS C++ 运行库源码：MMDABI 3701 桥接、PMX 解析、物理与双手 IK、构建脚本 |
| `model/` | 最终 PMX 模型与第三方库许可文本 |
| `metadata/` | 骨骼映射计划 `game_plan.json`、表情配置 `expressions.json`、原始骨骼命名、SDK 导出凭据 |
| `tools/` | 握持校验、渲染、安装管理与安装包打包脚本 |
| `validation/` | v38 握持验证数据与 UE 集成后处理验证 |
| `docs/` | 安装、构建、版本说明与实现记录 |

环境：Windows + Visual Studio 2022（MSVC v143）、CMake；依赖 UE4SS `1ceefac8`、Saba、Bullet 2.83.7、GLM、spdlog。构建与打包方法见 [工程说明](docs/BUILD.md)。

工程存档不是一键重建流水线：`runtime/generated/sdk` 中的导出库需要本机用 `prepare_sdk.py` 生成，游戏侧蓝图与资源包需要对应的 UE 工程。

[安装说明](docs/INSTALL.md) · [版本说明](docs/RELEASE_NOTES.md) · [Credits / 致谢](CREDITS.md)
