# agent-debugboard

[English](README.md)

`agent-debugboard` 是 **Agent DebugBoard** 的 RP2040 固件。它把一块硬件
调试板变成 PC 侧 Agent/AI 可以直接操作的 USB 控制接口，用于控制目标开发板
或 SBC 的供电、刷机模式、TF/SD 路由、电流监测 ADC 和一组安全 GPIO。

![Agent Debugger 宣传图](doc/marketing/agent-debugger-promo.png)

## 项目简介

Agent DebugBoard 面向自动化 bring-up、远程恢复、产测和 AI agent 调试链路。
固件会枚举为名为 `Agent DebugBoard` 的 USB CDC ACM 串口设备，并提供
`debugboard` shell 命令；仓库里同时提供了主机侧 Python CLI，方便脚本或
Agent 直接调用。

本仓库包含 Zephyr 应用、主机侧辅助工具、单元测试、原理图副本和项目文档。

## 功能范围

| 模块 | 当前固件支持 |
| --- | --- |
| USB 控制 | CDC ACM shell，提供 `debugboard` 命令 |
| 主机自动化 | `agent-debugboardctl` CLI |
| 电源轨 | `12v_out`、`5v_out`、`5v_ws`、`20v_out` |
| ADC 监测 | 读取 `5v_out`、`12v_out`、`20v_out` 的电流监测通道 |
| TF/SD 路由 | 在 `target` 和 `usb-reader` 之间切换 |
| GPIO | 安全白名单：`GP13`、`GP14`、`GP15`、`GP22`、`GP23`、`GP24` |
| 固件更新 | 通过 USB 命令让 RP2040 进入 BOOTSEL |

`5V_FIN` 会被当作独立的输入/来源电源轨处理，不作为可控输出电源轨暴露给主机。

## 安装主机侧 CLI

`agent-debugboardctl` 是 Go native binary。用户不需要 Python、pip 或虚拟环境。
从 `Build` workflow run 下载匹配 OS 和 CPU 的产物：

| 系统 / CPU | 产物 |
| --- | --- |
| Windows x64 | `agent-debugboardctl_windows_amd64.zip` |
| Windows arm64 | `agent-debugboardctl_windows_arm64.zip` |
| Linux x64 | `agent-debugboardctl_linux_amd64.tar.gz` |
| Linux arm64 | `agent-debugboardctl_linux_arm64.tar.gz` |
| macOS Intel | `agent-debugboardctl_darwin_amd64.tar.gz` |
| macOS Apple Silicon | `agent-debugboardctl_darwin_arm64.tar.gz` |

Windows PowerShell：

```powershell
.\agent-debugboardctl.exe --help
```

Linux / macOS：

```sh
chmod +x ./agent-debugboardctl
sudo install -m 755 ./agent-debugboardctl /usr/local/bin/agent-debugboardctl
agent-debugboardctl --help
```

开发者可以从源码构建：

```sh
go build -o agent-debugboardctl ./cmd/agent-debugboardctl
./agent-debugboardctl --help
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

`Build` workflow 会把固件发布为 `agent-debugboard-rp2040-firmware`，把主机侧
CLI 归档发布为 `agent-debugboardctl-native-packages`。

- `agent-debugboard-rp2040-firmware`：RP2040 的 UF2、ELF 和 map 文件。
- `agent-debugboardctl_windows_amd64.zip`：Windows x64 native CLI。
- `agent-debugboardctl_windows_arm64.zip`：Windows arm64 native CLI。
- `agent-debugboardctl_linux_amd64.tar.gz`：Linux x64 native CLI。
- `agent-debugboardctl_linux_arm64.tar.gz`：Linux arm64 native CLI。
- `agent-debugboardctl_darwin_amd64.tar.gz`：macOS Intel native CLI。
- `agent-debugboardctl_darwin_arm64.tar.gz`：macOS Apple Silicon native CLI。
- `checksums.txt`：host CLI 压缩包的 SHA256 校验文件。

下载 host CLI 产物并解压后，可以直接运行：

```sh
agent-debugboardctl --help
```

## 主机侧使用

查询调试板状态：

```sh
agent-debugboardctl status
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
debugboard rail list
debugboard adc read
debugboard sd get
debugboard gpio list
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
.goreleaser.yaml              GoReleaser 主机侧 CLI 打包配置
go.mod, go.sum                主机侧 CLI Go module
west.yml                      Zephyr workspace manifest
```
