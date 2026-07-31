# MERIVUS 分阶段编队固件说明

## 构建目标

- Pixhawk 6C Mini：`px4_fmu-v6c_default`
- SITL：`px4_sitl_default`
- 模块：`swarm_node`

`boards/px4/fmu-v6c/default.px4board` 已启用 `CONFIG_MODULES_SWARM_NODE=y`。模块由多旋翼启动脚本启动，不读取或执行各机已有 Mission。

## 与 GroundStation 的协议

配套地面站源码位于 `E:\MERIVUS\GroundStation`，入口是 `custom/src/Swarm/SwarmController.*`。

协议版本为 `2`，同一状态机支持包含 UAV-1 的单机、双机或完整六机验证。四个命令共用以下参数：

- `param1`：协议版本；
- `param2`：UAV-1～UAV-6 成员位图；
- `param3`：主机 system ID，固定为 `1`；
- `param4`：不大于 24 位的非零会话 ID。

事务阶段：

1. `MAV_CMD_USER_1`（PREPARE）：逐机执行身份、本地位置、落地和解锁状态预检，完成后 ACK。
2. `MAV_CMD_USER_2`（COMMIT）：先连续发送 Offboard setpoint，再依次切换 Offboard、解锁和起飞；过程中周期返回 `IN_PROGRESS`，到达 Ready 后最终 ACK。
3. 全部成员 Ready 后，地面站发送 `MAV_CMD_USER_3`（RELEASE），成员才进入实际编队 Control。
4. 任一阶段失败、超时或人工结束时发送 `MAV_CMD_USER_4`（ABORT），机载模块请求 AUTO_LOITER。
5. UAV-1 的标准 `GPS_RAW_INT` 由地面站读取，并转换为发送给本次所有成员的 `FOLLOW_TARGET` 会话租约。消息同时绑定会话 ID 和最初 PREPARE 的 MAVLink source system/component。

## 机载状态与轨迹

- 启动前检查 system ID 必须为 1～6、本地位置有效且飞机仍在地面。
- 所有成员在收到来源、会话和新鲜度均合法的 `FOLLOW_TARGET` 后才进入 Offboard。
- 从机还要求主机目标距离不超过 200 米。
- 六机起飞高度为相对起点 5 米。
- UAV-1 依次经过起点、北向 5 米、东北 5 米、东向 5 米并返回起点上空，之后持续悬停。
- UAV-2～UAV-6 按 system ID 形成 5 米间隔，跟随 UAV-1 的全球位置。

## 安全边界

- `FOLLOW_TARGET` 超过 3 秒未更新、来源不符、会话不符或位置无效时，所有成员（包括主机）请求 AUTO_LOITER。
- 启动、进入 Offboard、解锁或起飞阶段超过 20 秒时退出到 AUTO_LOITER。
- 停止命令不会自动降落。
- PREPARE 的最终 ACK 表示本机预检通过；COMMIT 的最终 ACK 表示本机已经到达 Ready；RELEASE ACK 表示本机已经进入 Control。
- 位置租约丢失后的自主退出由机载端执行，地面站还会在检测到成员链路异常时对整组发送 ABORT。
- 真实飞机验证必须由人员按现场安全流程执行；自动验证只允许 Mock/SITL。
