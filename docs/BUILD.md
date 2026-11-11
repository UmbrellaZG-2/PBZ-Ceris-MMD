# 工程说明

本仓库是 v37 资源包 + v38.0 运行库的制作存档，不是一键重建流水线。游戏侧蓝图、资源包打包工程与 UE4SS 导出库需要本机准备。

## 目录约定

| 路径 | 说明 |
| --- | --- |
| `runtime/` | 运行库 C++ 源码，产出 `main.dll` |
| `runtime/generated/sdk/` | 由 `prepare_sdk.py` 在本机生成，**不随仓库提供** |
| `metadata/game_plan.json` | 骨骼映射与姿态计划，运行库启动时读取 |
| `metadata/expressions.json` | 29 项表情权重，运行库每秒热读 |
| `model/` | 最终 PMX 与第三方许可文本 |
| `release/` | 安装包暂存目录（`.gitignore` 忽略），由本机安装文件汇总 |
| `output/` | 打包产物（`.gitignore` 忽略） |

## 运行库构建

环境：Windows 10/11 x64、Visual Studio 2022（MSVC v143）、CMake ≥ 3.20、Ninja。

1. 准备依赖到 `runtime/deps/`：

   ```text
   runtime/deps/
   ├─ RE-UE4SS-1ceefac8b8c286f0fb3eb62b4aae075ff81df0bd/
   ├─ saba-master/
   ├─ bullet3-2.83.7/
   ├─ ue4ss-exports.txt          # 从本机 UE4SS.dll 导出的符号表（dumpbin /exports）
   └─ build_tools/               # 可选：本地 cmake / ninja
   ```

2. 生成 ABI 适配层：`python runtime/prepare_sdk.py`
   - 从本机 `PBZ\platform\steam\ue4ss\UE4SS.dll` 读出导出表，生成 `generated/sdk/UE4SS.def` 与 `UE4SS.lib`；
   - 同时生成本机 `CppUserModBase` 头文件，并写入 `generated/sdk_provenance.json`。
   - 不猜测任何 UObject 成员偏移，只调用公开导出的反射函数。

3. 构建：

   ```bat
   cd runtime
   build_core.cmd
   ```

   或手工：

   ```bat
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j 4
   ```

   产物：`build/main.dll`（静态包含 Saba 与 Bullet 2.83.7）。

## 安装包打包

`tools/package_release.ps1` 从 `release/`、`metadata/`、`model/` 汇总安装包：

```powershell
powershell -NoProfile -File tools\package_release.ps1
```

输出 `output/CerisMMD_PBZ_v37_v38.zip`，结构即「合并到游戏根目录」的形式：

```text
安装说明.txt
ProjectPBZ\Content\Paks\~mods\zzz_CerisMMD_v37_P.pak / .ucas / .utoc
platform\steam\ue4ss\Mods\CerisFullMMD\dlls\main.dll
platform\steam\ue4ss\Mods\CerisFullMMD\enabled.txt
platform\steam\ue4ss\Mods\CerisFullMMD\game_plan.json
platform\steam\ue4ss\Mods\CerisFullMMD\expressions.json
platform\steam\ue4ss\Mods\CerisFullMMD\model.pmx
platform\steam\ue4ss\Mods\CerisFullMMD\original_names.json
platform\steam\ue4ss\Mods\CerisFullMMD\licenses\
```

## 安装 / 回滚与校验

`tools/manage_v38.ps1` 提供 `-Action Validate|Install|Rollback`，会校验：

- 本机 UE4SS `UE4SS.dll` 的 SHA-256 是否仍为适配版本；
- v37 资源包三件套哈希；
- v38 目标文件的 SHA-256，写入 `install_receipt.json`；
- 安装前确认游戏进程已退出，并先把旧文件备份到 `modwork/quarantine/`。

脚本中的路径是本机硬编码（游戏库 `E:\`），换机使用需自行修改。

## 校验脚本

| 脚本 | 作用 |
| --- | --- |
| `tools/validate_grip.py` | 离线握持约束求解与误差统计 |
| `tools/prepare_grip.py` | 从 `game_plan.json` 生成握持约束参数 |
| `tools/render_grip.py` | 握持姿态渲染出图 |
| `tools/ue_validate_grip.py` | 对比 UE 后处理输出与离线结果 |

## 不应覆盖的游戏资源

- 不要替换游戏原有 `pak`，资源只放在 `ProjectPBZ\Content\Paks\~mods\`。
- 不要覆盖 UE4SS 的 `mods.txt`、`UE4SS-settings.ini`。
- 不要启用旧 `CerisProbe`、`CerisFix`。
