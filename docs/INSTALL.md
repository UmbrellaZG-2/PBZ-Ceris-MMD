# 安装说明

适用于 `CerisMMD_PBZ_v37_v42.zip`（资源包 v37 + 运行库 v42.0）。

> 已有旧安装：只需替换 `platform\steam\ue4ss\Mods\CerisFullMMD\dlls\main.dll` 即可，`game_plan.json`、`model.pmx` 与资源包不变。

## 0. 前置条件

1. **正版游戏**：Steam 版《影之刃零》。下文用 `PBZ` 指游戏根目录，例如 `E:\SteamLibrary\steamapps\common\PBZ`。
2. **UE4SS v3.0.1**：从 [RE-UE4SS Releases](https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/v3.0.1) 下载 `UE4SS_v3.0.1.zip`，把其中的 `dwmapi.dll` 与整个 `ue4ss\` 文件夹放到：

   ```text
   PBZ\platform\steam\
   ├─ PBZ.exe
   ├─ dwmapi.dll      ← UE4SS 注入器
   └─ ue4ss\          ← UE4SS 主体与 Mods 目录
   ```

   已经装过 UE4SS 的跳过本节，**不要覆盖现有的 `mods.txt` 与 `UE4SS-settings.ini`**。
3. 确认游戏已关闭。

## 1. 安装

把压缩包解压，将里面的 `ProjectPBZ` 与 `platform` 两个文件夹**合并**到游戏根目录（覆盖同名目录，不要另建子目录）。

合并后应得到：

```text
PBZ\ProjectPBZ\Content\Paks\~mods\zzz_CerisMMD_v37_P.pak
PBZ\ProjectPBZ\Content\Paks\~mods\zzz_CerisMMD_v37_P.ucas
PBZ\ProjectPBZ\Content\Paks\~mods\zzz_CerisMMD_v37_P.utoc
PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\dlls\main.dll
PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\enabled.txt
PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\game_plan.json
PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\expressions.json
PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\model.pmx
PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\original_names.json
PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\licenses\
```

要点：

- `~mods` 不存在就手工新建；三个 `.pak/.ucas/.utoc` 必须**同名同目录**，缺一不可。
- `enabled.txt` 是空文件但**必须存在**，UE4SS 靠它识别 Mod。
- 不要手动改名 `CerisFullMMD` 文件夹或 `main.dll`。

## 2. 关闭旧调试 Mod

打开 `PBZ\platform\steam\ue4ss\Mods\mods.txt`，确认：

```ini
CerisFix : 0
CerisProbe : 0
```

这两个旧调试 Mod 会导致崩溃，不要启用；也可以直接把对应文件夹移走。

## 3. 启动与验证

启动游戏进入存档，首次加载会稍慢。

检查 `PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\runtime.log`：

```text
bridge_loaded; runtime=42.0; pelvis_calibration=1; hair_calibration=1; cloth_diagnostics=1; grip_constraints=2; waiting_for_MMDABI_3701
instance_created; original_bones=943; rigids=785; joints=1306; morphs=29; pelvis_calibrations=2 hair_calibrations=246
pose_applied frames=1 step_ms=... grip_max_error_cm=... physics_rescues=0
```

- 出现 `bridge_loaded` 表示运行库已被 UE4SS 加载；
- 出现 `instance_created` 表示角色实例已创建；
- `pose_applied` 持续刷新表示每帧姿态正常提交；`physics_rescues` 应恒为 0。
- `pose_applied` 行里的 `cloth_dev` / `cloth_radius` 是布片漂移诊断（v41）：正常游玩 `cloth_dev` 应在 2 单位以内、`cloth_rescues` 增长很慢；偶尔出现 `cloth_snap` 行属于急转身/墙跑时的正常回弹，若每帧都在 `cloth_snap` 说明阈值对该动作太严。
- `pelvis_calibrations=2` 表示裙摆/臀部穿模修复（v40）已生效：两只臀部胶囊被校准为 group 0、mask 0xfffe、半径 1.25。若为 0，说明 `model.pmx` 结构不符（换过模型），需按 `docs/v40_裙摆穿模修复记录.md` 重新确认刚体名。
- `hair_calibrations=246` 表示头发穿模修复（v42）已生效：5 只躯干胶囊被放大到皮肤表面且只撞头发，241 个发刚体的碰撞盒被搬到对应发片上。若为 0，说明躯干/发束刚体名不符，按 `docs/v42_头发穿模修复记录.md` 重新核对。

若没有该文件，检查 `PBZ\platform\steam\ue4ss\UE4SS.log` 是否有 `Mod 'CerisFullMMD' has enabled.txt, starting mod.`。

## 4. 表情配置（可选）

编辑 `PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\expressions.json`：

```json
{
  "enabled": true,
  "names": ["真面目", "困る", "にこり", "..."],
  "weights": [0.0, 0.0, 0.0, "..."]
}
```

- `weights` 与 `names` 一一对应，范围 0.0 – 1.0，可多项混合；
- 保存后约 1 秒生效，无需重启游戏；
- 全部设回 0 即恢复基础表情。常用索引：6「あ」、13「まばたき」（眨眼）、14「笑い」。

## 5. 卸载

1. 删除 `PBZ\ProjectPBZ\Content\Paks\~mods\zzz_CerisMMD_v37_P.pak/.ucas/.utoc`；
2. 删除 `PBZ\platform\steam\ue4ss\Mods\CerisFullMMD\` 整个文件夹（想保留表情配置就先备份 `expressions.json`）；
3. 重启游戏。

UE4SS 本体可以保留，不影响原版游戏。

## 6. 常见问题

| 现象 | 排查 |
| --- | --- |
| 启动即崩溃 | 确认 `CerisFix`、`CerisProbe` 在 `mods.txt` 中为 `0`，或移除这两个文件夹 |
| 角色没变 | 确认 `~mods` 三个文件齐全同名；确认没有其他角色 Mod 覆盖同名资源 |
| 角色透明 / 贴图丢失 | 核对 `.ucas` 为 34,248,750 字节，用 `SHA256SUMS.txt` 校验 |
| 动作卡住 / 物理异常 | 查看 `runtime.log` 的 `pose_applied`；确认 `game_plan.json` 未被旧版本覆盖 |
| 武器握持偏移 | 查看 `grip_max_error_cm`，极端动作下的已知限制见 `docs/RELEASE_NOTES.md` |
| 游戏更新后失效 | 运行库绑定当前 `PBZ.exe` 版本，需重新适配 |

## 7. 文件校验

```powershell
Get-FileHash .\ProjectPBZ\Content\Paks\~mods\zzz_CerisMMD_v37_P.ucas -Algorithm SHA256
```

期望值见仓库根目录 `SHA256SUMS.txt`。
