# MERIVUS RTK 与 4G 数传配置契约

本文定义 Pixhawk 6C Mini、Hyper982 RTK 模块和 HyperLte 4G 模块的唯一受支持配置。TCP 是 4G 模块到地面站之间的网络传输；飞控侧仍是 TELEM1 串口 `/dev/ttyS5`，两侧配置必须同时成立。

## 硬件连接

- Hyper982 UART1 连接飞控 GPS1，串口参数为 230400 8N1。
- HyperLte UART1 连接飞控 TELEM1，串口参数为 57600 8N1，关闭 RTS/CTS 流控。
- RTK、4G 和摄像头按硬件设计独立稳压供电并共地。

## 固件默认参数

`px4_fmu-v6c_default` 将以下配置作为产品默认值：

```text
GPS_1_CONFIG     201      # GPS1
GPS_1_PROTOCOL   6        # NMEA (generic)
SER_GPS1_BAUD    230400
EKF2_HGT_REF     1        # GPS
EKF2_GPS_CTRL    15       # 位置、高度、速度和双天线航向
GPS_YAW_OFFSET   90       # 主天线在右、从天线在左

MAV_0_CONFIG     101      # TELEM1
MAV_0_MODE       0        # Normal
MAV_0_RATE       0        # 自动使用串口物理带宽的一半
MAV_0_FLOW_CTRL  0        # Force off
MAV_0_FORWARD    0
MAV_0_RADIO_CTL  0
SER_TEL1_BAUD    57600
```

`GPS_YAW_OFFSET=90` 只适用于上述天线布局。安装方向改变时，应按机体坐标系重新确定该值，不得沿用默认角度。

## 已刷写飞机的参数迁移

PX4 会保留已经保存的参数，刷新固件不会覆盖旧值。曾按旧文档设置 `MAV_0_MODE=1` 或 `MAV_0_RATE=80000` 的飞机，需要在 MAVLink 控制台执行：

```sh
param set MAV_0_CONFIG 101
param set MAV_0_MODE 0
param set MAV_0_RATE 0
param set MAV_0_FLOW_CTRL 0
param set MAV_0_FORWARD 0
param set MAV_0_RADIO_CTL 0
param set SER_TEL1_BAUD 57600

param set GPS_1_CONFIG 201
param set GPS_1_PROTOCOL 6
param set SER_GPS1_BAUD 230400
param set EKF2_HGT_REF 1
param set EKF2_GPS_CTRL 15
param set GPS_YAW_OFFSET 90

param save
reboot
```

`Normal` 是面向地面站的完整消息模式。`Custom` 和 `Magic` 只保留协议管理消息，除非另行逐条配置消息流；因此不得把它们用于 MERIVUS 的 4G 地面站链路。

| 模式 | 适用链路 | 默认业务流 | MERIVUS 使用约束 |
| --- | --- | --- | --- |
| Normal | TELEM1、数传电台、4G 串口透传 | 完整 GCS 状态、位置、GPS 与控制消息 | 4G 地面站链路唯一允许模式 |
| Config | USB | 高速配置、诊断与完整遥测 | 由 USB 自动选择，不用于 57600 串口 |
| Custom / Magic | 手工逐条配置消息的专用链路 | 无默认业务流 | 产品部署禁用 |
| Onboard | 伴随计算机 | 高频传感器、位置与控制消息 | 不用于地面站连接 |

57600 8N1 的物理上限是 5760 B/s。`MAV_0_RATE=0` 会选择 2880 B/s 并按带宽自动缩放各消息流。显式配置超过 5760 B/s 的值没有物理意义，固件会将其限制到串口上限并输出警告。

## 重启后的验收

执行：

```sh
mavlink status
mavlink status streams
gps status
listener sensor_gps -n 1
```

验收条件：

- MAVLink 实例为 `Normal`，设备为 `/dev/ttyS5 @ 57600`，`tx rate max` 为 2880 B/s。
- 默认流至少包含 `GPS_RAW_INT`、`GLOBAL_POSITION_INT`、`GPS_STATUS`、`SYS_STATUS` 和 `EXTENDED_SYS_STATE`。
- `gps status` 显示 NMEA、`/dev/ttyS0 @ 230400`，读取速率持续非零。
- `sensor_gps` 的 `timestamp` 持续更新，`fix_type >= 3` 且 `satellites_used > 0`。
- USB 与 TCP 连接看到的卫星数和定位状态一致；允许刷新周期不同，不允许只有 USB 有数据。

## 故障定位边界

- UPrecise 无卫星且 GGA 质量为 0：检查天线、供电、射频环境和接收机配置。
- UPrecise 有定位、`sensor_gps` 无更新：检查 GPS1 接线、NMEA 协议和 230400 波特率。
- `sensor_gps` 有定位、USB 正常而 TCP 无 GPS：检查 TELEM1 是否为 `Normal`，并用 `mavlink status streams` 确认 `GPS_RAW_INT`。
- MAVLink 接收正常但丢包或延迟高：先核对 `SER_TEL1_BAUD` 与模块 TTL 波特率，再检查供电、运营商链路和 TCP 中转；不得用超出串口物理容量的 `MAV_0_RATE` 掩盖问题。
