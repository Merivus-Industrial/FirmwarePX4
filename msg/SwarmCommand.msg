uint64 timestamp # time since system start (microseconds)

uint8 ACTION_NONE = 0
uint8 ACTION_PREPARE = 1
uint8 ACTION_COMMIT = 2
uint8 ACTION_RELEASE = 3
uint8 ACTION_ABORT = 4

uint8 action
uint8 protocol_version
uint8 leader_system_id
uint8 source_system
uint8 source_component
uint8 member_mask
uint32 session_id
uint32 command
