#pragma once

#include <px4_platform_common/module.h>
#include <px4_platform_common/px4_work_queue/ScheduledWorkItem.hpp>

#include <drivers/drv_hrt.h>
#include <geo/geo.h>
#include <lib/perf/perf_counter.h>

#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/follow_target.h>
#include <uORB/topics/offboard_control_mode.h>
#include <uORB/topics/swarm_command.h>
#include <uORB/topics/trajectory_setpoint.h>
#include <uORB/topics/vehicle_command.h>
#include <uORB/topics/vehicle_command_ack.h>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/vehicle_local_position.h>
#include <uORB/topics/vehicle_status.h>

class SwarmNode final : public ModuleBase<SwarmNode>, public px4::ScheduledWorkItem
{
public:
	SwarmNode();
	~SwarmNode() override;

	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]);
	static int print_usage(const char *reason = nullptr);

	bool init();
	int print_status() override;

private:
	enum class State : uint8_t {
		Idle = 0,
		Prepared,
		WaitForTarget,
		Prestream,
		EnterOffboard,
		Arm,
		Takeoff,
		Ready,
		Control,
		ExitToHold,
	};

	void Run() override;
	void handleSwarmCommand();
	void handlePrepareCommand(const swarm_command_s &command);
	void handleCommitCommand(const swarm_command_s &command);
	void handleReleaseCommand(const swarm_command_s &command);
	void handleAbortCommand(const swarm_command_s &command);
	bool prepareFormation();
	bool updateFollowTarget();
	bool projectFollowerTarget();
	bool targetIsFresh() const;
	bool commandMatchesSession(const swarm_command_s &command) const;
	bool vehicleIsMember(uint8_t vehicle_id, uint8_t member_mask) const;
	void beginExitToHold(const char *reason, bool command_failure = false);
	void resetFormation();

	bool requestOffboard();
	bool requestArm();
	bool requestAutoHold();
	bool controlPosition(float x, float y, float z);
	void publishPositionSetpoint(float x, float y, float z);
	void publishVehicleCommand(uint32_t command, float param1 = 0.f, float param2 = 0.f, float param3 = 0.f);
	void publishCommandAck(uint32_t command, uint8_t result, uint8_t progress = 0, int32_t result_param2 = 0,
			       uint8_t target_system = 0, uint8_t target_component = 0);
	void publishCommitProgress(uint8_t progress);
	void finishCommit(uint8_t result);
	void finishAbort(uint8_t result);

	static constexpr uint8_t kProtocolVersion = 2;
	static constexpr uint8_t kLeaderSystemId = 1;
	static constexpr uint8_t kVehicleCount = 6;
	static constexpr uint32_t kCommitMavlinkCommand = 31011; // MAV_CMD_USER_2
	static constexpr uint32_t kAbortMavlinkCommand = 31013; // MAV_CMD_USER_4
	static constexpr float kTakeoffHeightMeters = 5.f;
	static constexpr float kFollowerSpacingMeters = 5.f;
	static constexpr float kMaximumTargetDistanceMeters = 200.f;
	static constexpr float kPositionAcceptanceMeters = 1.f;
	static constexpr hrt_abstime kTargetTimeoutUs = 3'000'000;
	static constexpr hrt_abstime kPhaseTimeoutUs = 20'000'000;
	static constexpr hrt_abstime kPrestreamDurationUs = 1'500'000;
	static constexpr hrt_abstime kCommandIntervalUs = 200'000;
	static constexpr hrt_abstime kProgressIntervalUs = 1'000'000;
	static constexpr uint64_t kFollowTargetMagicPrefix = 0x4d45524900000000ULL; // "MERI" + session
	static constexpr uint64_t kFollowTargetMagicMask = 0xffffffff00000000ULL;
	static constexpr uint64_t kFollowTargetSessionMask = 0x00000000ffffffffULL;

	State _state{State::Idle};
	uint8_t _vehicle_id{0};
	uint8_t _member_mask{0};
	uint8_t _command_source_system{0};
	uint8_t _command_source_component{0};
	uint32_t _session_id{0};
	bool _commit_command_pending{false};
	bool _abort_command_pending{false};

	float _begin_x{NAN};
	float _begin_y{NAN};
	float _begin_z{NAN};
	float _target_x{NAN};
	float _target_y{NAN};
	float _hold_x{NAN};
	float _hold_y{NAN};
	float _hold_z{NAN};

	hrt_abstime _phase_started_at{0};
	hrt_abstime _square_started_at{0};
	hrt_abstime _last_target_received_at{0};
	hrt_abstime _last_command_sent_at{0};
	hrt_abstime _last_progress_ack_at{0};

	MapProjection _global_local_projection{};
	follow_target_s _follow_target{};
	vehicle_land_detected_s _land_detected{};
	vehicle_local_position_s _local_position{};
	vehicle_status_s _vehicle_status{};

	uORB::Subscription _follow_target_sub{ORB_ID(follow_target)};
	uORB::Subscription _swarm_command_sub{ORB_ID(swarm_command)};
	uORB::Subscription _vehicle_land_detected_sub{ORB_ID(vehicle_land_detected)};
	uORB::Subscription _vehicle_local_position_sub{ORB_ID(vehicle_local_position)};
	uORB::Subscription _vehicle_status_sub{ORB_ID(vehicle_status)};

	uORB::Publication<offboard_control_mode_s> _offboard_control_mode_pub{ORB_ID(offboard_control_mode)};
	uORB::Publication<trajectory_setpoint_s> _trajectory_setpoint_pub{ORB_ID(trajectory_setpoint)};
	uORB::Publication<vehicle_command_s> _vehicle_command_pub{ORB_ID(vehicle_command)};
	uORB::Publication<vehicle_command_ack_s> _vehicle_command_ack_pub{ORB_ID(vehicle_command_ack)};

	perf_counter_t _loop_perf{perf_alloc(PC_ELAPSED, MODULE_NAME": cycle")};
	perf_counter_t _loop_interval_perf{perf_alloc(PC_INTERVAL, MODULE_NAME": interval")};
};
