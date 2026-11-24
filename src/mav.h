/* Single place that pulls in the generated MAVLink C library.
 * We use the ardupilotmega dialect: it is a superset of common.xml and is
 * what both ArduPilot and (for the messages we need) PX4 speak. */
#pragma once

#include <ardupilotmega/mavlink.h>
