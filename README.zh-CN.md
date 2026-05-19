# agent-debugboard

[English](README.md)

`agent-debugboard` 是 **Agent DebugBoard** 的 RP2040 固件。它把一块硬件
调试板变成 PC 侧 Agent/AI 可以直接操作的 USB 控制接口，用于控制目标开发板
或 SBC 的供电、刷机模式、TF/SD 路由、电流监测 ADC 和一组安全 GPIO。

![Agent Debugger 宣传图](doc/marketing/agent-debugger-promo.png)

## 项目简介

Agent DebugBoard 面向自动化 bring-up、远程恢复、产测和 AI agent 调试链路。
固件会枚举为名为 `Agent DebugBoard` 的 USB CDC ACM 串口设备，并提供
`debugboard` shell 命令；仓库里同时提供了主机侧 Go native CLI，方便脚本或
Agent 直接调用。

本仓库包含 Zephyr 应用、主机侧辅助工具、单元测试、原理图副本和项目文档。

## 功能范围

| 模块 | 当前固件支持 |
| --- | --- |
| USB 控制 | CDC ACM shell，提供 `debugboard` 命令 |
| 主机自动化 | 支持 JSON 输出和 `doctor` 诊断的 `agent-debugboardctl` CLI |
| 电源轨 | `12v_out`、`5v_out`、`5v_ws`、`20v_out` |
| ADC 监测 | 读取 `5v_out`、`12v_out`、`20v_out` 的电流监测通道 |
| TF/SD 路由 | 在 `target` 和 `usb-reader` 之间切换 |
| GPIO | 安全白名单：`GP13`、`GP14`、`GP15`、`GP22`、`GP23`、`GP24` |
| 固件更新 | 通过 USB 命令让 RP2040 进入 BOOTSEL |

`5V_FIN` 会被当作独立的输入/来源电源轨处理，不作为可控输出电源轨暴露给主机。

## 给 AI Agent 的使用入口

AI Agent 在操作硬件前，应先读取
[skills/agent-debugboard/SKILL.md](skills/agent-debugboard/SKILL.md)。这份 skill
是仓库内面向 Agent 的权威操作规程，包含 `agent-debugboardctl` 的安装、连接诊
断、JSON 命令使用和有副作用操作的安全规则。

推荐 Agent 最小流程：

```sh
agent-debugboardctl --version
agent-debugboardctl --json doctor
agent-debugboardctl --json status
```

如果 `agent-debugboardctl` 未安装，先按 skill 中的安装命令处理。自动化场景
优先使用 `agent-debugboardctl --json ...`，解析 `schema`、`ok`、`command` 和
`error.code`，不要解析面向人看的文本输出。

## 安装主机侧 CLI

`agent-debugboardctl` 是 Go native binary。用户不需要 Python、pip 或虚拟环境。

公开仓库可以在 macOS / Linux 上通过 `curl` 或 `wget` 一行安装最新版：

```sh
curl -fsSL https://raw.githubusercontent.com/xzl01/agent-debugboard/main/scripts/install.sh | sh
wget -qO- https://raw.githubusercontent.com/xzl01/agent-debugboard/main/scripts/install.sh | sh
```

也可以指定版本或安装目录：

```sh
curl -fsSL https://raw.githubusercontent.com/xzl01/agent-debugboard/main/scripts/install.sh | VERSION=v0.0.2 sh
curl -fsSL https://raw.githubusercontent.com/xzl01/agent-debugboard/main/scripts/install.sh | INSTALL_DIR=/usr/local/bin sh
```

当前仓库是私有仓库时，需要先提供 GitHub token。已经登录 GitHub CLI 的机器可
以直接使用 `gh auth token`：

```sh
export GH_TOKEN="$(gh auth token)"
curl -fsSL \
  -H "Authorization: Bearer $GH_TOKEN" \
  -H "Accept: application/vnd.github.raw" \
  "https://api.github.com/repos/xzl01/agent-debugboard/contents/scripts/install.sh?ref=main" | sh
wget -qO- \
  --header="Authorization: Bearer $GH_TOKEN" \
  --header="Accept: application/vnd.github.raw" \
  "https://api.github.com/repos/xzl01/agent-debugboard/contents/scripts/install.sh?ref=main" | sh
```

Windows PowerShell：

```powershell
irm https://raw.githubusercontent.com/xzl01/agent-debugboard/main/scripts/install.ps1 | iex
```

私有仓库 PowerShell：

```powershell
$env:GH_TOKEN = gh auth token
irm `
  -Headers @{Authorization = "Bearer $env:GH_TOKEN"; Accept = "application/vnd.github.raw"} `
  "https://api.github.com/repos/xzl01/agent-debugboard/contents/scripts/install.ps1?ref=main" | iex
```

也可以从 GitHub Release 手动下载匹配 OS 和 CPU 的产物：

| 系统 / CPU | 产物 |
| --- | --- |
| Windows x64 | `agent-debugboardctl_windows_amd64.zip` |
| Windows arm64 | `agent-debugboardctl_windows_arm64.zip` |
| Linux x64 | `agent-debugboardctl_linux_amd64.tar.gz` |
| Linux arm64 | `agent-debugboardctl_linux_arm64.tar.gz` |
| macOS Intel | `agent-debugboardctl_darwin_amd64.tar.gz` |
| macOS Apple Silicon | `agent-debugboardctl_darwin_arm64.tar.gz` |

macOS 上未签名的 release 二进制可能触发 Gatekeeper，提示 Apple 无法验证软件。
安装脚本会先校验 `SHA256SUMS.txt`，再移除安装后二进制的 quarantine 标记。
如果手动安装，请先校验 SHA256，再执行：

```sh
xattr -dr com.apple.quarantine ./agent-debugboardctl
```

安装后验证：

```sh
agent-debugboardctl --help
agent-debugboardctl --version
agent-debugboardctl doctor
```

## 构建固件

创建 Python 环境并拉取 Zephyr：

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -U pip west

west init -l .
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

如果还没有安装 Zephyr SDK，需要先安装。当前本地构建已用 Zephyr SDK
`1.0.1` 验证过。

构建 RP2040 固件：

```sh
source .venv/bin/activate
west build -p always -b rpi_pico/rp2040 apps/agent_debugboard -d build/agent_debugboard
```

生成的 UF2 文件位置：

```text
build/agent_debugboard/zephyr/zephyr.uf2
```

## 刷写

如果板子当前已经运行本固件，可以先让它进入 BOOTSEL，再刷写新的 UF2：

```sh
agent-debugboardctl bootloader
picotool load -v -x build/agent_debugboard/zephyr/zephyr.uf2
```

如果板子已经以 `RPI-RP2` 磁盘方式挂载，只需要执行：

```sh
picotool load -v -x build/agent_debugboard/zephyr/zephyr.uf2
```

## GitHub Actions 产物

`Build` workflow 会检查每次 push 和 pull request。推送 `v*` tag 会触发
`Release` workflow，自动构建固件、打包主机 CLI、创建 GitHub Release，并上传
固定命名的 release assets。

- `agent-debugboard-rp2040.uf2`：用于拖拽刷写或 `picotool` 的 RP2040 固件。
- `agent-debugboard-rp2040.elf`：用于调试的 RP2040 ELF。
- `agent-debugboard-rp2040.map`：RP2040 链接 map。
- `agent-debugboardctl_windows_amd64.zip`：Windows x64 native CLI。
- `agent-debugboardctl_windows_arm64.zip`：Windows arm64 native CLI。
- `agent-debugboardctl_linux_amd64.tar.gz`：Linux x64 native CLI。
- `agent-debugboardctl_linux_arm64.tar.gz`：Linux arm64 native CLI。
- `agent-debugboardctl_darwin_amd64.tar.gz`：macOS Intel native CLI。
- `agent-debugboardctl_darwin_arm64.tar.gz`：macOS Apple Silicon native CLI。
- `SHA256SUMS.txt`：所有 release assets 的 SHA256 校验文件。

开发者可以从源码构建 host CLI：

```sh
go build -o agent-debugboardctl ./cmd/agent-debugboardctl
./agent-debugboardctl --help
```

## 主机侧使用

查询调试板状态：

```sh
agent-debugboardctl status
agent-debugboardctl doctor
```

Agent 或自动化程序推荐优先使用 JSON 输出。JSON 响应固定包含
`schema: "agent-debugboard.v1"`、`ok`、`command`，成功时返回命令相关字段，
失败时返回 `error: {code, message}`：

```sh
agent-debugboardctl --json doctor
agent-debugboardctl --json status
agent-debugboardctl --json rail list
agent-debugboardctl --json adc read
agent-debugboardctl --json gpio list
```

控制电源轨：

```sh
agent-debugboardctl rail set 12v_out on
agent-debugboardctl rail set 12v_out off
agent-debugboardctl rail set 5v_out on
agent-debugboardctl rail set 5v_out off
agent-debugboardctl rail set 5v_ws on
agent-debugboardctl rail set 20v_out on
```

读取电流监测 ADC 通道：

```sh
agent-debugboardctl adc read
agent-debugboardctl adc read 5v_out
agent-debugboardctl adc read 12v_out
agent-debugboardctl adc read 20v_out
```

切换 TF/SD 路由：

```sh
agent-debugboardctl sd route target
agent-debugboardctl sd route usb-reader
```

使用安全 GPIO：

```sh
agent-debugboardctl gpio list
agent-debugboardctl gpio set GP13 1
agent-debugboardctl gpio input GP13
```

## 直接使用 Shell

打开 CDC 串口设备后，可以直接使用 `debugboard` 命令：

```text
debugboard status
debugboard status --json
debugboard rail list
debugboard rail list --json
debugboard adc read
debugboard adc read --json
debugboard sd get
debugboard sd get --json
debugboard gpio list
debugboard gpio list --json
debugboard bootloader
```

## 硬件映射

| 功能 | 固件名称 | 原理图信号 |
| --- | --- | --- |
| 12 V 输出使能 | `12v_out` | `GP02_12V_EN` |
| 5 V 输出使能 | `5v_out` | `GP05_5V_EN` |
| 5 V WS 使能 | `5v_ws` | `GP09_5V_WS_EN` |
| 20 V 输出使能 | `20v_out` | `GP10_20V_EN` |
| TF/SD 路由切换 | `sd route` | `GP06_TF_SW` |
| 5 V 电流监测 | `adc read 5v_out` | `S_C_5V` |
| 12 V 电流监测 | `adc read 12v_out` | `S_C_12V` |
| 20 V 电流监测 | `adc read 20v_out` | `S_C_20V` |

当前原理图副本放在
[doc/agent-debugboard-schematic.pdf](doc/agent-debugboard-schematic.pdf)。

## 开发

运行单元测试：

```sh
./apps/agent_debugboard/tests/run_unit_tests.sh
```

测试脚本覆盖：

- 共享板级模型的 host C 单元测试。
- 主机侧 CLI 辅助工具的 Go 测试。

## 仓库结构

```text
apps/agent_debugboard/        Zephyr 应用
apps/agent_debugboard/src/    固件源码和共享板级模型
apps/agent_debugboard/tests/  单元测试
cmd/agent-debugboardctl/      Go 主机侧 CLI 入口
internal/hostcli/             Go 主机侧 CLI 实现
doc/                          硬件文档和宣传素材
skills/agent-debugboard/      面向 Agent 的 skill 和操作规程
.goreleaser.yaml              GoReleaser 主机侧 CLI 打包配置
go.mod, go.sum                主机侧 CLI Go module
west.yml                      Zephyr workspace manifest
```
