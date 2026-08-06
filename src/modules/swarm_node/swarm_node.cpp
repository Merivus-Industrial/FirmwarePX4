#include "swarm_node.h"

#include <commander/px4_custom_mode.h>
#include <mathlib/mathlib.h>

#include <cmath>
#include <inttypes.h>

SwarmNode::SwarmNode() :
	ScheduledWorkItem(MODULE_NAME, px4::wq_configurations::swarm_node)
{
}

SwarmNode::~SwarmNode()
{
	perf_free(_loop_perf);
	perf_free(_loop_interval_perf);
}

bool SwarmNode::init()
{
	ScheduleOnInterval(20'000); // 50 Hz: comfortably above the PX4 offboard minimum update rate.
	return true;
}

void SwarmNode::handleSwarmCommand()
{
	swarm_command_s command{};

	if (!_swarm_command_sub.update(&command) || command.protocol_version != kProtocolVersion) {
		return;
	}

	switch (command.action) {
	case swarm_command_s::ACTION_PREPARE:
		handlePrepareCommand(command);
		break;

	case swarm_command_s::ACTION_COMMIT:
		handleCommitCommand(command);
		break;

	case swarm_command_s::ACTION_RELEASE:
		handleReleaseCommand(command);
		break;

	case swarm_command_s::ACTION_ABORT:
		handleAbortCommand(command);
		break;

	default:
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_UNSUPPORTED, 0, 0,
				  command.source_system, command.source_component);
		break;
	}
}

void SwarmNode::handlePrepareCommand(const swarm_command_s &command)
{
	if (_state != State::Idle) {
		if (commandMatchesSession(command) && _state == State::Prepared) {
			publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED);

		} else {
			publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_TEMPORARILY_REJECTED, 0, 0,
					  command.source_system, command.source_component);
		}

		return;
	}

	_vehicle_status_sub.copy(&_vehicle_status);
	const uint8_t vehicle_id = _vehicle_status.system_id;

	if (!vehicleIsMember(vehicle_id, command.member_mask)
	    || command.leader_system_id != kLeaderSystemId) {
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_DENIED, 0, 0,
				  command.source_system, command.source_component);
		return;
	}

	_vehicle_id = vehicle_id;
	_member_mask = command.member_mask;
	_session_id = command.session_id;
	_command_source_system = command.source_system;
	_command_source_component = command.source_component;

	if (!prepareFormation()) {
		PX4_ERR("formation preflight rejected");
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_DENIED);
		resetFormation();
		return;
	}

	_state = State::Prepared;
	_phase_started_at = hrt_absolute_time();
	publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED);
	PX4_INFO("formation prepared for system %u, session %" PRIu32, _vehicle_id, _session_id);
}

void SwarmNode::handleCommitCommand(const swarm_command_s &command)
{
	if (!commandMatchesSession(command)) {
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_DENIED, 0, 0,
				  command.source_system, command.source_component);
		return;
	}

	if (_state == State::Ready || _state == State::Control) {
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED, 100);
		return;
	}

	if (_state != State::Prepared) {
		if (_commit_command_pending) {
			publishCommitProgress(10);

		} else {
			publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_TEMPORARILY_REJECTED);
		}

		return;
	}

	_phase_started_at = hrt_absolute_time();
	_last_progress_ack_at = 0;
	_commit_command_pending = true;
	_state = State::WaitForTarget;
	publishCommitProgress(5);
	PX4_INFO("formation commit accepted for system %u, session %" PRIu32, _vehicle_id, _session_id);
}

void SwarmNode::handleAbortCommand(const swarm_command_s &command)
{
	if (_state == State::Idle) {
		_command_source_system = command.source_system;
		_command_source_component = command.source_component;
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED);
		resetFormation();
		return;
	}

	if (!commandMatchesSession(command)) {
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_DENIED, 0, 0,
				  command.source_system, command.source_component);
		return;
	}

	if (_commit_command_pending) {
		finishCommit(vehicle_command_ack_s::VEHICLE_CMD_RESULT_CANCELLED);
	}

	_abort_command_pending = true;
	publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_IN_PROGRESS, 10);

	if (_state == State::Prepared) {
		finishAbort(vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED);
		resetFormation();

	} else {
		beginExitToHold("formation abort requested");
	}
}

void SwarmNode::handleReleaseCommand(const swarm_command_s &command)
{
	if (!commandMatchesSession(command)) {
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_DENIED, 0, 0,
				  command.source_system, command.source_component);
		return;
	}

	if (_state == State::Control) {
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED);
		return;
	}

	if (_state != State::Ready) {
		publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_TEMPORARILY_REJECTED);
		return;
	}

	_state = State::Control;
	_square_started_at = hrt_absolute_time();
	_phase_started_at = _square_started_at;
	publishCommandAck(command.command, vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED);
}

bool SwarmNode::prepareFormation()
{
	if (!_vehicle_status_sub.copy(&_vehicle_status)
	    || !_vehicle_local_position_sub.copy(&_local_position)
	    || !_vehicle_land_detected_sub.copy(&_land_detected)) {
		return false;
	}

	_vehicle_id = _vehicle_status.system_id;

	if (!vehicleIsMember(_vehicle_id, _member_mask)
	    || !_local_position.xy_valid || !_local_position.z_valid
	    || !_land_detected.landed
	    || _vehicle_status.arming_state != vehicle_status_s::ARMING_STATE_STANDBY) {
		return false;
	}

	_begin_x = _local_position.x;
	_begin_y = _local_position.y;
	_begin_z = _local_position.z;
	_hold_x = _begin_x;
	_hold_y = _begin_y;
	_hold_z = _begin_z;
	_target_x = NAN;
	_target_y = NAN;
	_last_target_received_at = 0;
	_last_command_sent_at = 0;

	if (_vehicle_id != kLeaderSystemId) {
		if (!_local_position.xy_global
		    || !PX4_ISFINITE(_local_position.ref_lat)
		    || !PX4_ISFINITE(_local_position.ref_lon)) {
			return false;
		}

		_global_local_projection.initReference(_local_position.ref_lat, _local_position.ref_lon,
						       _local_position.ref_timestamp);
	}

	return true;
}

bool SwarmNode::updateFollowTarget()
{
	follow_target_s target{};

	if (!_follow_target_sub.update(&target)
	    || (target.custom_state & kFollowTargetMagicMask) != kFollowTargetMagicPrefix
	    || (target.custom_state & kFollowTargetSessionMask) != _session_id
	    || target.source_system != _command_source_system
	    || target.source_component != _command_source_component
	    || hrt_elapsed_time(&target.timestamp) > kTargetTimeoutUs
	    || !PX4_ISFINITE(target.lat)
	    || !PX4_ISFINITE(target.lon)) {
		return false;
	}

	_follow_target = target;
	_last_target_received_at = target.timestamp;

	return _vehicle_id == kLeaderSystemId || projectFollowerTarget();
}

bool SwarmNode::projectFollowerTarget()
{
	_global_local_projection.project(_follow_target.lat, _follow_target.lon, _target_x, _target_y);

	if (!PX4_ISFINITE(_target_x) || !PX4_ISFINITE(_target_y)) {
		return false;
	}

	const float distance = hypotf(_local_position.x - _target_x, _local_position.y - _target_y);
	return PX4_ISFINITE(distance) && distance <= kMaximumTargetDistanceMeters;
}

bool SwarmNode::targetIsFresh() const
{
	return _last_target_received_at > 0
	       && hrt_elapsed_time(&_last_target_received_at) <= kTargetTimeoutUs;
}

bool SwarmNode::commandMatchesSession(const swarm_command_s &command) const
{
	return command.session_id == _session_id
	       && command.member_mask == _member_mask
	       && command.leader_system_id == kLeaderSystemId
	       && command.source_system == _command_source_system
	       && command.source_component == _command_source_component;
}

bool SwarmNode::vehicleIsMember(uint8_t vehicle_id, uint8_t member_mask) const
{
	return vehicle_id >= kLeaderSystemId
	       && vehicle_id <= kVehicleCount
	       && (member_mask & (1u << (vehicle_id - 1u))) != 0;
}

void SwarmNode::beginExitToHold(const char *reason, bool command_failure)
{
	if (_state == State::Idle) {
		return;
	}

	if (command_failure && _commit_command_pending) {
		finishCommit(vehicle_command_ack_s::VEHICLE_CMD_RESULT_FAILED);
	}

	_vehicle_local_position_sub.copy(&_local_position);
	_hold_x = _local_position.x;
	_hold_y = _local_position.y;
	_hold_z = _local_position.z;
	_phase_started_at = hrt_absolute_time();
	_last_command_sent_at = 0;
	_state = State::ExitToHold;
	PX4_WARN("%s", reason);
}

void SwarmNode::resetFormation()
{
	_state = State::Idle;
	_vehicle_id = 0;
	_phase_started_at = 0;
	_square_started_at = 0;
	_last_target_received_at = 0;
	_last_command_sent_at = 0;
	_last_progress_ack_at = 0;
	_member_mask = 0;
	_session_id = 0;
	_command_source_system = 0;
	_command_source_component = 0;
	_commit_command_pending = false;
	_abort_command_pending = false;
}

void SwarmNode::publishPositionSetpoint(float x, float y, float z)
{
	offboard_control_mode_s control_mode{};
	control_mode.timestamp = hrt_absolute_time();
	control_mode.position = true;
	_offboard_control_mode_pub.publish(control_mode);

	trajectory_setpoint_s setpoint{};
	setpoint.timestamp = control_mode.timestamp;
	setpoint.position[0] = x;
	setpoint.position[1] = y;
	setpoint.position[2] = z;

	for (int i = 0; i < 3; ++i) {
		setpoint.velocity[i] = NAN;
		setpoint.acceleration[i] = NAN;
		setpoint.jerk[i] = NAN;
	}

	setpoint.yaw = NAN;
	setpoint.yawspeed = NAN;
	_trajectory_setpoint_pub.publish(setpoint);
}

void SwarmNode::publishVehicleCommand(uint32_t command, float param1, float param2, float param3)
{
	vehicle_command_s vehicle_command{};
	vehicle_command.timestamp = hrt_absolute_time();
	vehicle_command.command = command;
	vehicle_command.param1 = param1;
	vehicle_command.param2 = param2;
	vehicle_command.param3 = param3;
	vehicle_command.target_system = _vehicle_status.system_id;
	vehicle_command.target_component = _vehicle_status.component_id;
	vehicle_command.source_system = _vehicle_status.system_id;
	vehicle_command.source_component = _vehicle_status.component_id;
	vehicle_command.from_external = false;
	_vehicle_command_pub.publish(vehicle_command);
}

bool SwarmNode::requestOffboard()
{
	const float takeoff_z = _begin_z - kTakeoffHeightMeters;
	publishPositionSetpoint(_begin_x, _begin_y, takeoff_z);
	_vehicle_status_sub.copy(&_vehicle_status);

	const bool offboard = _vehicle_status.nav_state == vehicle_status_s::NAVIGATION_STATE_OFFBOARD;
	const hrt_abstime now = hrt_absolute_time();

	if (!offboard && now - _last_command_sent_at >= kCommandIntervalUs) {
		publishVehicleCommand(vehicle_command_s::VEHICLE_CMD_DO_SET_MODE, 1.f, PX4_CUSTOM_MAIN_MODE_OFFBOARD);
		_last_command_sent_at = now;
	}

	return offboard;
}

bool SwarmNode::requestArm()
{
	const float takeoff_z = _begin_z - kTakeoffHeightMeters;
	publishPositionSetpoint(_begin_x, _begin_y, takeoff_z);
	_vehicle_status_sub.copy(&_vehicle_status);

	const bool armed = _vehicle_status.arming_state == vehicle_status_s::ARMING_STATE_ARMED;
	const hrt_abstime now = hrt_absolute_time();

	if (!armed && now - _last_command_sent_at >= kCommandIntervalUs) {
		publishVehicleCommand(vehicle_command_s::VEHICLE_CMD_COMPONENT_ARM_DISARM, 1.f);
		_last_command_sent_at = now;
	}

	return armed;
}

bool SwarmNode::requestAutoHold()
{
	_vehicle_status_sub.copy(&_vehicle_status);

	if (_vehicle_status.arming_state != vehicle_status_s::ARMING_STATE_ARMED) {
		return true;
	}

	if (_vehicle_status.nav_state == vehicle_status_s::NAVIGATION_STATE_AUTO_LOITER) {
		return true;
	}

	publishPositionSetpoint(_hold_x, _hold_y, _hold_z);
	const hrt_abstime now = hrt_absolute_time();

	if (now - _last_command_sent_at >= kCommandIntervalUs) {
		publishVehicleCommand(vehicle_command_s::VEHICLE_CMD_DO_SET_MODE, 1.f, PX4_CUSTOM_MAIN_MODE_AUTO,
				      PX4_CUSTOM_SUB_MODE_AUTO_LOITER);
		_last_command_sent_at = now;
	}

	return false;
}

bool SwarmNode::controlPosition(float x, float y, float z)
{
	publishPositionSetpoint(x, y, z);

	if (!_vehicle_local_position_sub.copy(&_local_position)
	    || !_local_position.xy_valid || !_local_position.z_valid) {
		return false;
	}

	return fabsf(_local_position.x - x) < kPositionAcceptanceMeters
	       && fabsf(_local_position.y - y) < kPositionAcceptanceMeters
	       && fabsf(_local_position.z - z) < kPositionAcceptanceMeters;
}

void SwarmNode::publishCommandAck(uint32_t command, uint8_t result, uint8_t progress, int32_t result_param2,
				  uint8_t target_system, uint8_t target_component)
{
	vehicle_command_ack_s ack{};
	ack.timestamp = hrt_absolute_time();
	ack.command = command;
	ack.result = result;
	ack.result_param1 = progress;
	ack.result_param2 = result_param2;
	ack.target_system = target_system != 0 ? target_system : _command_source_system;
	ack.target_component = target_component != 0 ? target_component : _command_source_component;
	ack.from_external = false;
	_vehicle_command_ack_pub.publish(ack);
}

void SwarmNode::publishCommitProgress(uint8_t progress)
{
	const hrt_abstime now = hrt_absolute_time();

	if (!_commit_command_pending
	    || (_last_progress_ack_at != 0 && now - _last_progress_ack_at < kProgressIntervalUs)) {
		return;
	}

	publishCommandAck(kCommitMavlinkCommand, vehicle_command_ack_s::VEHICLE_CMD_RESULT_IN_PROGRESS, progress);
	_last_progress_ack_at = now;
}

void SwarmNode::finishCommit(uint8_t result)
{
	if (_commit_command_pending) {
		publishCommandAck(kCommitMavlinkCommand, result,
				  result == vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED ? 100 : 0);
		_commit_command_pending = false;
	}
}

void SwarmNode::finishAbort(uint8_t result)
{
	if (_abort_command_pending) {
		publishCommandAck(kAbortMavlinkCommand, result,
				  result == vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED ? 100 : 0);
		_abort_command_pending = false;
	}
}

void SwarmNode::Run()
{
	if (should_exit()) {
		ScheduleClear();
		exit_and_cleanup();
		return;
	}

	perf_begin(_loop_perf);
	perf_count(_loop_interval_perf);
	handleSwarmCommand();

	const hrt_abstime now = hrt_absolute_time();

	switch (_state) {
	case State::Idle:
		break;

	case State::Prepared:
		if (now - _phase_started_at > kPhaseTimeoutUs) {
			PX4_WARN("prepared formation session expired before commit");
			resetFormation();
		}

		break;

	case State::WaitForTarget:
		_vehicle_local_position_sub.copy(&_local_position);
		publishCommitProgress(10);

		if (updateFollowTarget()) {
			_state = State::Prestream;
			_phase_started_at = now;

		} else if (now - _phase_started_at > kTargetTimeoutUs) {
			beginExitToHold("formation lease was not received before timeout", true);
		}

		break;

	case State::Prestream:
		updateFollowTarget();
		publishCommitProgress(20);
		publishPositionSetpoint(_begin_x, _begin_y, _begin_z - kTakeoffHeightMeters);

		if (!targetIsFresh()) {
			beginExitToHold("formation lease expired during offboard prestream", true);

		} else if (now - _phase_started_at >= kPrestreamDurationUs) {
			_state = State::EnterOffboard;
			_phase_started_at = now;
			_last_command_sent_at = 0;
		}

		break;

	case State::EnterOffboard:
		updateFollowTarget();
		publishCommitProgress(35);

		if (!targetIsFresh()) {
			beginExitToHold("formation lease expired before offboard transition", true);

		} else if (requestOffboard()) {
			_state = State::Arm;
			_phase_started_at = now;
			_last_command_sent_at = 0;

		} else if (now - _phase_started_at > kPhaseTimeoutUs) {
			beginExitToHold("offboard transition timed out", true);
		}

		break;

	case State::Arm:
		updateFollowTarget();
		publishCommitProgress(50);

		if (!targetIsFresh()) {
			beginExitToHold("formation lease expired before arming", true);

		} else if (requestArm()) {
			_state = State::Takeoff;
			_phase_started_at = now;

		} else if (now - _phase_started_at > kPhaseTimeoutUs) {
			beginExitToHold("formation arming timed out", true);
		}

		break;

	case State::Takeoff:
		updateFollowTarget();
		publishCommitProgress(70);

		if (!targetIsFresh()) {
			beginExitToHold("formation lease expired during takeoff", true);

		} else if (controlPosition(_begin_x, _begin_y, _begin_z - kTakeoffHeightMeters)) {
			_state = State::Ready;
			_phase_started_at = now;
			finishCommit(vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED);

		} else if (now - _phase_started_at > kPhaseTimeoutUs) {
			beginExitToHold("formation takeoff timed out", true);
		}

		break;

	case State::Ready:
		updateFollowTarget();

		if (!targetIsFresh()) {
			beginExitToHold("formation lease timed out while waiting for release");

		} else {
			controlPosition(_begin_x, _begin_y, _begin_z - kTakeoffHeightMeters);
		}

		break;

	case State::Control:
		updateFollowTarget();

		if (!targetIsFresh()) {
			beginExitToHold("formation lease timed out");
			break;
		}

		if (_vehicle_id == kLeaderSystemId) {
			const hrt_abstime elapsed = now - _square_started_at;
			float x = _begin_x;
			float y = _begin_y;

			if (elapsed < 10'000'000) {
				// Hold the first corner while followers settle.
			} else if (elapsed < 20'000'000) {
				x += 5.f;

			} else if (elapsed < 30'000'000) {
				x += 5.f;
				y += 5.f;

			} else if (elapsed < 40'000'000) {
				y += 5.f;
			}

			controlPosition(x, y, _begin_z - kTakeoffHeightMeters);

		} else {
			_vehicle_local_position_sub.copy(&_local_position);

			if (!projectFollowerTarget()) {
				beginExitToHold("leader target stream timed out or became invalid");
				break;
			}

			controlPosition(_target_x + (_vehicle_id - kLeaderSystemId) * kFollowerSpacingMeters,
					_target_y, _begin_z - kTakeoffHeightMeters);
		}

		break;

	case State::ExitToHold:
		if (_abort_command_pending
		    && (_last_progress_ack_at == 0 || now - _last_progress_ack_at >= kProgressIntervalUs)) {
			publishCommandAck(kAbortMavlinkCommand, vehicle_command_ack_s::VEHICLE_CMD_RESULT_IN_PROGRESS, 70);
			_last_progress_ack_at = now;
		}

		if (requestAutoHold()) {
			finishAbort(vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED);
			resetFormation();

		} else if (now - _phase_started_at > kPhaseTimeoutUs) {
			PX4_ERR("failed to enter auto hold after formation exit");
			finishAbort(vehicle_command_ack_s::VEHICLE_CMD_RESULT_FAILED);
			resetFormation();
		}

		break;
	}

	perf_end(_loop_perf);
}

int SwarmNode::task_spawn(int argc, char *argv[])
{
	SwarmNode *instance = new SwarmNode();

	if (instance) {
		_object.store(instance);
		_task_id = task_id_is_work_queue;

		if (instance->init()) {
			return PX4_OK;
		}
	}

	delete instance;
	_object.store(nullptr);
	_task_id = -1;
	return PX4_ERROR;
}

int SwarmNode::print_status()
{
	PX4_INFO("state: %u, system id: %u", static_cast<unsigned>(_state), _vehicle_id);
	perf_print_counter(_loop_perf);
	perf_print_counter(_loop_interval_perf);
	return 0;
}

int SwarmNode::custom_command(int argc, char *argv[])
{
	return print_usage("unknown command");
}

int SwarmNode::print_usage(const char *reason)
{
	if (reason) {
		PX4_WARN("%s\n", reason);
	}

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
Versioned MERIVUS formation controller for one, two, or six members.

The module accepts MAV_CMD_USER_1/2/3/4 as PREPARE, COMMIT, RELEASE, and
ABORT. FOLLOW_TARGET carries a source-bound session lease. Every aircraft
exits to AUTO_LOITER if that lease becomes stale.
)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("swarm_node", "controller");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return 0;
}

extern "C" __EXPORT int swarm_node_main(int argc, char *argv[])
{
	return SwarmNode::main(argc, argv);
}
