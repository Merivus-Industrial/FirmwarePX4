# MERIVUS FirmwarePX4 构建与刷写指南

本文说明如何在 Windows 编辑 MERIVUS PX4 源码，使用已有 Ubuntu 虚拟机完成编译，再通过 Windows 版 QGroundControl 刷写 Pixhawk 6C Mini。

## 1. 环境分工

| 环境 | 用途 |
| --- | --- |
| Windows | 编辑源码、Git 提交与推送、运行 QGroundControl、选择自定义固件刷写 |
| Ubuntu 虚拟机 | 拉取源码、初始化子模块、安装 ARM 工具链、编译 PX4 |
| GitHub | 在 Windows 与 Ubuntu 之间同步版本，并保存可追踪的个人项目历史 |

不建议使用普通 Git Bash 直接编译本项目。Git Bash 本身不包含完整的 PX4 NuttX/ARM 工具链。

## 2. 首次准备 Ubuntu 虚拟机

推荐 Ubuntu 22.04。确认网络、磁盘空间和 Git 可用：

```bash
lsb_release -a
git --version
df -h
```

将源码克隆到 Ubuntu 用户目录，而不是 `/mnt` 或 VMware 共享目录：

```bash
mkdir -p ~/src
cd ~/src
git clone --recursive https://github.com/Merivus-Industrial/FirmwarePX4.git
cd FirmwarePX4
```

如果虚拟机已有旧副本，先识别并提交仍需保留的变更；存在来源不明的修改时停止构建并查明归属。不要复制旧目录作为长期备份，也不要把新仓库强行覆盖到未知工作区。

## 3. 安装或检查 PX4 工具链

已有可编译 PX4 v1.14 的虚拟机先检查：

```bash
arm-none-eabi-gcc --version
cmake --version
ninja --version
python3 --version
```

缺少依赖时，在仓库根目录执行：

```bash
bash Tools/setup/ubuntu.sh --no-sim-tools
source ~/.profile
```

该脚本会安装 NuttX/Pixhawk 固件构建所需依赖。只在需要 SITL 图形仿真时安装仿真工具。

## 4. 日常同步源码

推荐流程是 Windows 提交并推送，Ubuntu 再拉取：

```bash
cd ~/src/FirmwarePX4
git switch main
git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive
git status --short
```

开始编译前，`git status --short` 应没有非预期输出。若子模块出现本地修改，先确认它是实际开发改动，还是 Windows 文件权限、换行或符号链接造成的假改动。

记录本次构建的源码身份：

```bash
git rev-parse HEAD
git status --porcelain=v1
git submodule status --recursive
arm-none-eabi-gcc --version | head -n 1
```

只有工作区为空、子模块与提交记录一致时才允许生成部署固件。构建记录必须保存主仓提交、子模块提交、工具链版本和后续产物 SHA-256；不得用目录名称或人工备注代替源码身份。

## 5. 编译 Pixhawk 6C Mini

在仓库根目录执行：

```bash
make px4_fmu-v6c_default
```

预期产物：

```text
build/px4_fmu-v6c_default/px4_fmu-v6c_default.px4
build/px4_fmu-v6c_default/px4_fmu-v6c_default.elf
```

确认 `.px4` 文件存在并记录校验值：

```bash
ls -lh build/px4_fmu-v6c_default/px4_fmu-v6c_default.px4
sha256sum build/px4_fmu-v6c_default/px4_fmu-v6c_default.px4
```

将输出的 SHA-256 与源码提交一起写入本次发布记录。MD5 只可兼容旧流程，不作为唯一完整性校验。

日常通过 QGroundControl 刷写应使用 `.px4` 文件，不要把 `.bin`、`.elf` 或 bootloader 文件当作普通自定义固件。

## 6. 编译 SITL

需要验证编队模块是否进入 SITL 固件时：

```bash
make px4_sitl_default
```

若虚拟机没有图形环境，应按实际仿真方式使用 headless 模式。真实飞机验证前，应先完成 Mock/SITL 流程和失效保护检查。

## 7. 将固件复制回 Windows

### VMware 共享文件夹

将 Windows 输出目录配置为 VMware 共享文件夹，例如：

```text
E:\MERIVUS\FirmwareOutput
```

Ubuntu 中通常挂载为 `/mnt/hgfs/MERIVUS/FirmwareOutput`。复制固件：

```bash
cp build/px4_fmu-v6c_default/px4_fmu-v6c_default.px4 \
  /mnt/hgfs/MERIVUS/FirmwareOutput/
```

共享目录只用于传输源码或产物，不建议直接作为 Linux 编译目录。

复制后在 Ubuntu 再次计算共享目录中的校验值，并确认与构建目录完全相同：

```bash
sha256sum \
  build/px4_fmu-v6c_default/px4_fmu-v6c_default.px4 \
  /mnt/hgfs/MERIVUS/FirmwareOutput/px4_fmu-v6c_default.px4
```

Windows 侧使用 `Get-FileHash -Algorithm SHA256` 复核。任何一处不一致都应停止刷写并重新执行受控复制，不得在输出目录或刷写机上直接修改固件。

### SCP

如果 Windows 已启用 OpenSSH Server，也可以从 Ubuntu 执行：

```bash
scp build/px4_fmu-v6c_default/px4_fmu-v6c_default.px4 \
  <windows-user>@<windows-ip>:/目标目录/
```

## 8. 使用 QGroundControl 刷写

1. 关闭其他可能占用飞控串口的软件；
2. 断开飞控电池和不必要的外设供电；
3. 打开 Windows 版 QGroundControl；
4. 进入“车辆设置 → 固件”；
5. 按提示连接 Pixhawk 6C Mini USB；
6. 打开高级设置并选择“Custom firmware file”；
7. 选择 `px4_fmu-v6c_default.px4`；
8. 等待擦除、写入、校验和重启全部完成；
9. 重新检查参数、传感器、遥控、输出和失效保护。

WSL/虚拟机内构建与 Windows QGroundControl 刷写应分开处理，不需要在 Ubuntu 中执行 `make ... upload`。

## 9. 实机前检查

- 确认目标确实是 Pixhawk 6C Mini/FMUv6C；
- 确认所有成员 system ID 唯一且位于 1～6；
- 确认 UAV-1 为编队主机；
- 检查 GPS、本地位置、遥控、模式切换和急停；
- 拆桨或采取等效防护进行首次上电检查；
- 先完成单机，再完成双机，最后才进行六机验证；
- 验证 `PREPARE`、`COMMIT`、`RELEASE`、`ABORT` 及超时退出；
- 确认链路或 `FOLLOW_TARGET` 超时后所有成员进入预期 Hold/Loiter；
- 不把 Hold/Loiter 当作自动降落完成。

## 10. 常见问题

### 子模块缺失

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

### 找不到 ARM 编译器

```bash
source ~/.profile
arm-none-eabi-gcc --version
```

仍找不到时重新执行：

```bash
bash Tools/setup/ubuntu.sh --no-sim-tools
```

### Windows 中出现大量子模块修改

PX4 包含 Unix 可执行位和符号链接。从 Linux 通过不保留元数据的方式复制到 Windows 后，Git 可能显示大量假改动。不要直接提交这些变化；应在 Ubuntu 中克隆仓库和编译，Windows 侧只提交明确的业务源码改动。

### QGroundControl 无法选择固件

确认选择的是：

```text
px4_fmu-v6c_default.px4
```

而不是 `.elf`、bootloader `.bin` 或其他板型产物。
