#pragma once

/*
 * Extra SAE J1979 Mode $01 PIDs (hex) for 2009 Chevy Avalanche LTZ and similar US OBD-II.
 * Freematics COBD::isValidPID() + readPID() skip what the ECU does not support.
 * GM Mode $22 / enhanced PIDs are not included (different protocol).
 */
#ifndef PID_SUPPORTED_PIDS_01_20
#define PID_SUPPORTED_PIDS_01_20 0x00
#endif
#ifndef PID_MONITOR_STATUS
#define PID_MONITOR_STATUS 0x01
#endif
#ifndef PID_FREEZE_DTC
#define PID_FREEZE_DTC 0x02
#endif
#ifndef PID_FUEL_SYSTEM_STATUS
#define PID_FUEL_SYSTEM_STATUS 0x03
#endif
#ifndef PID_CMD_SECONDARY_AIR
#define PID_CMD_SECONDARY_AIR 0x12
#endif
#ifndef PID_O2_SENSORS_PRESENT_13
#define PID_O2_SENSORS_PRESENT_13 0x13
#endif
#ifndef PID_O2_B1S1
#define PID_O2_B1S1 0x14
#endif
#ifndef PID_O2_B1S2
#define PID_O2_B1S2 0x15
#endif
#ifndef PID_O2_B1S3
#define PID_O2_B1S3 0x16
#endif
#ifndef PID_O2_B1S4
#define PID_O2_B1S4 0x17
#endif
#ifndef PID_O2_B2S1
#define PID_O2_B2S1 0x18
#endif
#ifndef PID_O2_B2S2
#define PID_O2_B2S2 0x19
#endif
#ifndef PID_O2_B2S3
#define PID_O2_B2S3 0x1A
#endif
#ifndef PID_O2_B2S4
#define PID_O2_B2S4 0x1B
#endif
#ifndef PID_OBD_STANDARDS
#define PID_OBD_STANDARDS 0x1C
#endif
#ifndef PID_O2_SENSORS_PRESENT_1D
#define PID_O2_SENSORS_PRESENT_1D 0x1D
#endif
#ifndef PID_SUPPORTED_PIDS_21_40
#define PID_SUPPORTED_PIDS_21_40 0x20
#endif
#ifndef PID_SUPPORTED_PIDS_41_60
#define PID_SUPPORTED_PIDS_41_60 0x40
#endif
#ifndef PID_SUPPORTED_PIDS_61_80
#define PID_SUPPORTED_PIDS_61_80 0x60
#endif
#ifndef PID_SUPPORTED_PIDS_81_A0
#define PID_SUPPORTED_PIDS_81_A0 0x80
#endif
#ifndef PID_SUPPORTED_PIDS_A1_C0
#define PID_SUPPORTED_PIDS_A1_C0 0xA0
#endif
#ifndef PID_ENGINE_OIL_LIFE_PCT
#define PID_ENGINE_OIL_LIFE_PCT 0x5F
#endif
