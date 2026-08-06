# MERIVUS FirmwarePX4

MERIVUS 多无人机编队飞行固件，基于 **PX4 v1.14** 源码开发，当前主要面向 **Holybro Pixhawk 6C Mini（FMUv6C）** 和 PX4 SITL。

> [!IMPORTANT]
> 这是 `Merivus-Industrial` 维护的个人研发项目，不是 PX4 官方仓库，也不代表 PX4 官方发行版本。仓库以当前可开发源码快照重新初始化，未保留 PX4 上游 Git 提交历史；上游版权、第三方组件及许可证信息仍按原文件保留。

## 当前状态

- 阶段：研发与 Mock/SITL 验证；
- 固件目标：`px4_fmu-v6c_default`；
- 仿真目标：`px4_sitl_default`；
- 编队模块：`swarm_node`；
- 配套地面站：[Merivus-Industrial/MERIVUS](https://github.com/Merivus-Industrial/MERIVUS)；
- 推荐构建环境：Ubuntu 22.04 或已有 PX4 v1.14 工具链的 Ubuntu 虚拟机；
- Windows 用于源码编辑、版本管理和 QGroundControl 刷写，不作为本项目的原生 PX4 编译环境。

当前源码尚未在本次仓库整理后完成真实飞机验证，不应直接视为可投入生产飞行的发布固件。

## MERIVUS 定制内容

### 分阶段编队协议

`swarm_node` 与 MERIVUS 地面站实现版本化编队事务：

1. `PREPARE`：检查成员身份、位置、落地和解锁状态；
2. `COMMIT`：进入 Offboard 准备、解锁并起飞到 Ready；
3. `RELEASE`：所有成员 Ready 后开始实际编队轨迹；
4. `ABORT`：任一阶段失败、超时或人工结束时退出到 Hold/Loiter。

协议支持包含 UAV-1 的单机、双机和完整六机验证。详细参数、状态机、位置租约和失效保护见：

- [机载编队模块说明](src/modules/swarm_node/README.md)
- [SwarmCommand 消息定义](msg/SwarmCommand.msg)

### 目标与板级配置

- `boards/px4/fmu-v6c/default.px4board`：Pixhawk 6C Mini 编队固件；
- `boards/px4/sitl/default.px4board`：SITL 编队验证；
- `boards/hkust/`：HKUST NXT 系列板级配置；
- `boards/micoair/`：MicoAir H743 系列板级配置；
- `boards/holybro/durandal-v1/`：Durandal 定制配置。

## 快速开始

### 1. 在 Ubuntu 中获取源码

```bash
git clone --recursive https://github.com/Merivus-Industrial/FirmwarePX4.git
cd FirmwarePX4
```

如果已经克隆但缺少子模块：

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

### 2. 安装固件工具链

已有可编译 PX4 v1.14 的 Ubuntu 虚拟机可以跳过此步。新环境建议使用 Ubuntu 22.04：

```bash
bash Tools/setup/ubuntu.sh --no-sim-tools
source ~/.profile
```

### 3. 编译 Pixhawk 6C Mini

```bash
make px4_fmu-v6c_default
```

成功后用于 QGroundControl 刷写的文件为：

```text
build/px4_fmu-v6c_default/px4_fmu-v6c_default.px4
```

完整的 Windows、Ubuntu 虚拟机、源码同步和刷写流程见：

- [构建与刷写指南](Documentation/merivus/BUILD_AND_FLASH.md)
- [RTK 与 4G 数传配置契约](Documentation/merivus/RTK_AND_4G_CONFIGURATION.md)

## 推荐开发流程

```text
Windows 修改与提交源码
        ↓ push
Merivus-Industrial/FirmwarePX4
        ↓ pull
Ubuntu 虚拟机编译
        ↓ 复制 .px4
Windows QGroundControl 刷写
```

优先使用 Git 同步 Windows 与 Ubuntu 虚拟机，不要反复覆盖整个源码目录。编译应在 Ubuntu 的 Linux 文件系统内进行，不建议直接在 VMware 共享目录中构建。

## 安全边界

- 默认只进行代码检查、Mock 和 SITL 验证；
- 不自动连接或控制真实飞机；
- `ABORT`/失效保护只请求 Hold/Loiter，不代表已经安全降落；
- 首次实机测试必须拆桨或采取等效防护，并由现场人员完成检查、授权和急停准备；
- 自定义固件刷写前应保存参数并确认飞控型号、bootloader 和固件目标一致。

## 许可证与上游

本项目继承 PX4 源码中的 BSD 3-Clause License，详见 [LICENSE](LICENSE)。各子模块和第三方组件可能采用各自许可证，请同时遵守对应目录或上游仓库中的许可要求。

PX4 官方项目：

- <https://github.com/PX4/PX4-Autopilot>
- <https://px4.io/>
