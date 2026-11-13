# MEMORY.md — 长期事实与约定

## 仓库与环境

- 本工作区 `51_PWMCtrol/` 是 **多项目仓库 `KeilMiscProject` 的子目录**，git 根在
  `F:\SYSFILE\DeskTop\Misc.Git\KeilMiscProject`，远端 `origin = https://github.com/in-serinder/KeilMiscProject.git`。
  → **提交时必须限定路径**：`git add -- 51_PWMCtrol`，否则会把同级的 `51_Capsa`、`51_24Cxx_Copier`
  里别人的改动一起带上（这些目录长期有脏文件）。
- 主分支是 `master`（`main` 也存在但只有 Initial commit，别推错）。
- 作者身份：`Serinder <serinder@163.com>`；提交信息用英文（与既有历史一致）。

### ⚠ 推送网络限制（2026-11-13 实测）
- `https://github.com` **443 端口连不上**（Connection reset / 超时），`api.github.com` 反而正常。
  → `git push` 走 HTTPS 会失败，重试与 HTTP/1.1 均无效。
- SSH **22 与 443 都能握手**（`ssh -T git@github.com` 回 `Permission denied (publickey)`），
  但 `~/.ssh/` 里**没有密钥**，只有 known_hosts。
- 结论：要推送必须先「挂代理」或「配 SSH 密钥」二选一，见 README 式的三条命令。

### ⚠ .NET 构建（2026-11-13 实测）
- `dotnet restore` 在本机**必失败**：`NuGet.targets Value cannot be null (Parameter 'path1')`
  （`XPlatMachineWideSetting` 解析 machine-wide 配置时返回 null）。设 ProgramData/TEMP、
  换 PowerShell、关沙箱都无效；连已有的参考项目也失败。
- **可用绕过**：`D:\VSTudio\MSBuild\Current\Bin\MSBuild.exe <proj> -t:Restore`（注意用 `-t:` 不要用 `/t:`，
  MSYS 会把 `/t:` 当路径），成功后 `dotnet build --no-restore` 一切正常。
- Avalonia 工程**必须声明 DPI 感知**（`app.manifest` + `SetProcessDpiAwarenessContext`），否则窗口被位图拉伸、
  布局宽度算少 20%，右侧内容会被裁掉。

## 项目约定

- 文档放 `docs/`，编号 `01-`/`02-`/`03-`，中文标题；模拟器放 `sim/`。
- 协议帧：上位机 `>` … `<`，下位机 `[` … `]`，字段分隔 `_`；通道号 **0/1/2**。
- `.gitignore` 已忽略 `.vs/ obj/ bin/ __pycache__/ sim_store.json`。
- 文档写作风格：先给结论表，再给理由；有争议处列「待确认」而不是含糊带过。

## 用户偏好（观察所得）

- 喜欢先「画靶」（模拟器/文档）再写固件，重视能在 PC 上跑起来看得见的东西。
- 对细节敏感（会指出数码管 50 显示成 500 这类问题），希望我直接给方案并说明取舍，而不是让他做选择题。
- 交流用中文；技术术语可中英混用。
