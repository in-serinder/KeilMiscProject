# MEMORY.md — 51_24Cxx_Copier

## 仓库 / 版本控制

- **git 根目录是父目录** `F:\SYSFILE\DeskTop\Misc.Git\KeilMiscProject`（monorepo，
  里面还有 51_Capsa、51_NFG、STM32_Weather_Monitoring_Node 等），
  `51_24Cxx_Copier` 只是子目录。不要在子目录里假设 `git rev-parse --show-toplevel`
  等于子目录本身。
- 远端：`origin https://github.com/in-serinder/KeilMiscProject.git`，主分支 `master`，
  提交身份 `Serinder <serinder@163.com>`。
- `.workbuddy/`（含 memory）属于本地工作数据，**不要提交**。
- 历史遗留问题：`.vs/`、`bin/`、`obj/` 已被仓库跟踪，提交时会大量变动；
  提交时只 stage 源码路径，不要 `git add -A`。若要彻底解决需要
  `git rm -r --cached` + 根 `.gitignore`，属于独立改动，未经用户同意不要做。
- `firmware/` 已有独立 `.gitignore`（Objects/、Listings/、*.OBJ、*.LST、*.ABS、*.M51、*.uvgui.*）。

## 项目结构

- `firmware/` — STC8G1K08A 下位机（Keil C51，`UV4 -b` 构建，产物 `Objects/copier.hex`）
- `avalonia_upper/` — 上位机（.NET 8 / Avalonia，`dotnet build --no-restore` 验证）
- `lower-level_simulation/` — Python 协议模拟器（作为协议行为基准）

## 工具链注意事项（本机）

- 上位机运行时会锁住 `bin\Debug\net8.0\24Cxx_Copier_Porg.exe`，构建报 MSB3027；
  用 `-p:BaseOutputPath=bin_verify\` 另开目录验证编译。
- 本机 `dotnet new`/带 restore 的构建会报 NuGet `Value cannot be null (path1)`；
  新建临时测试工程时改用 SDK 自带 `csc.dll` + `packs\Microsoft.NETCore.App.Ref\...\ref\net8.0\*.dll`
  直接编译，再写 `*.runtimeconfig.json` 用 `dotnet x.dll` 运行。
