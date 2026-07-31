uint64 timestamp  # time since system start (microseconds)
uint64 custom_state # opaque MAVLink FOLLOW_TARGET state used to identify the MERIVUS formation stream
uint8 source_system
uint8 source_component

float64 lat       # target latitude in degrees
float64 lon       # target longitude in degrees
float32 alt       # target position

float32 vy        # target vel in y
float32 vx        # target vel in x
float32 vz        # target vel in z

uint8 est_cap     # target reporting capabilities
