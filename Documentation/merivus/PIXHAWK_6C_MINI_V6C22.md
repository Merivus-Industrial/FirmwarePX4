# Pixhawk 6C Mini V6C22 硬件契约与验收

## 适用硬件

本产品的 Pixhawk 6C Mini `ver all` 标识为：

```text
HW arch: PX4_FMU_V6C
HW type: V6C002002
HW version: 0x002
HW revision: 0x002
```

`V6C002002` 对应 V6C22（Mini Rev 2）。板载传感器契约如下：

| 总线 | 器件 | 用途 |
| --- | --- | --- |
| SPI1 | BMI088 | 加速度计、陀螺仪 |
| SPI1 | ICM-42688-P | 加速度计、陀螺仪 |
| I2C4 | IST8310，地址 `0x0c` | 磁力计 |
| I2C4 | MS5611，地址 `0x77` | 气压计 |

V6C00、V6C01 和 V6C21 使用 BMI055；V6C02、V6C22 必须按硬件版本改用 BMI088。不得通过同时盲启两个 Bosch 驱动来掩盖版本识别错误。

硬件资料：

- [Holybro Pixhawk 6C Mini Technical Specification](https://docs.holybro.com/autopilot/pixhawk-6c-mini/technical-specification)
- [PX4 上游 FMUv6C 板级配置](https://github.com/PX4/PX4-Autopilot/tree/main/boards/px4/fmu-v6c)

## 构建与刷写同源检查

当前工作区产品基线为 `v1.14.0-1.0.0`。验收截图中的飞控运行官方 PX4 `v1.17.0`、提交 `d6f12ad1c4f70ad3230afd7d86e971421e02fef4`，不是当前产品工作区的构建产物。

按 `BUILD_AND_FLASH.md` 从本工作区构建并刷写后，先执行：

```sh
ver all
```

验收条件：

- `HW type` 仍为 `V6C002002`；
- `PX4 git-hash` 与本次构建所记录的 Git 提交一致；
- 构建和刷写记录同时保存源码提交、工具链版本与固件 SHA-256。

## 传感器验收

断桨并保持飞控静止，在 MAVLink 控制台执行：

```sh
bmi088 -A status
bmi088 -G status
icm42688p status
ms5611 status
ist8310 status
sensors status
```

验收条件：

- BMI088 加速度计和陀螺仪均为运行状态，且没有持续通信错误；
- ICM-42688-P 正常运行，形成第二套惯性测量冗余；
- MS5611 与 IST8310 正常运行；
- 不应出现 BMI055 启动或探测失败日志；
- `sensors status` 中两套 IMU 数据持续更新，故障计数不增长。

V6C22 的官方器件表不包含 LSM6DSV 或 RM3100，因此本产品基线不启用这两个后来用于其他 FMUv6C 变体的回退驱动。
