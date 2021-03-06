/**
 * POOT
 * The Zebro Project
 * Delft University of Technology
 * 2016
 *
 * Filename: vregs.h
 *
 * Description:
 * Implementation of the virtual registers used for communication
 * using ZebroBus (I2C1) and UART1
 *
 * !! IMPORTANT !!
 * In order for the host debug tools to work, it is important that
 * the structure of this file remains the same! Be careful when making
 * changes.
 *
 * The debug tool reads out the values between the BEGIN FIELD NAME DEFINITIONS
 * and END FIELD NAME DEFININTIONS statements. Leave these lines intact!
 * It than generates labels for the registers by remove the '#define VREGS_'
 * that every label has. Thus, in order for proper labels to be generated, you
 * should start all vregs name definitions with 'VREGS_' and put them between
 * the BEGIN and END statements.
 *
 * Authors:
 * Piet De Vaere -- Piet@DeVae.re
 * Daniel Booms -- d.booms@solcon.nl
 */


#ifndef __VREGS_H__
#define __VREGS_H__

#include "stdint.h"
#include "globals.h"

#define VREGS_NUM_OF_BUFFERS 2
#define VREGS_FILE_SIZE 256
#define VREGS_FILE_APPEND_SIZE 5
#define VREGS_FILE_TOTAL_SIZE (VREGS_FILE_APPEND_SIZE + VREGS_FILE_SIZE)
#define VREGS_SYNC_0 0xFF
#define VREGS_SYNC_1 0x45
#define VREGS_SYNC_2 0x12
#define VREGS_SYNC_3 0xEA
#define VREGS_SYNC_4 0x4B

/* Important: do not remove the line below, it is used by the debug tools */
/* BEGIN FIELD NAME DEFINITIONS */
#define VREGS_ZEBROBUS_VERSION 0
#define VREGS_PRODUCT_ID 1
#define VREGS_PRODUCT_VERSION 2
#define VREGS_SERIAL_ID 3
#define VREGS_SOFTWARE_VERSION 4
#define VREGS_ZEBROBUS_ADDRESS 5
#define VREGS_LEG_SIDE 6
#define VREGS_LEG_ADDRESS 7

#define VREGS_QUICK_STATUS 10
#define VREGS_SYNC_COUNTER 11
#define VREGS_CLOCK_A 12
#define VREGS_CLOCK_B 13
#define VREGS_LOOP_COUNTER 16
#define VREGS_LOOP_TIME 17

#define VREGS_ERROR_COUNTER 20
#define VREGS_LAST_ERROR 21
#define VREGS_EMERGENCY_STOP 22

#define VREGS_BATTERY_VOLTAGE 25
#define VREGS_ADC_INTERNAL_TEMP_A 26
#define VREGS_POOTBUS_TEMP111 27
#define VREGS_POOTBUS_TEMP100 28

#define VREGS_MOTION_MODE 30
#define VREGS_MOTION_POSITION_A 31
#define VREGS_MOTION_POSITION_B 32
#define VREGS_MOTION_TIME_A 33
#define VREGS_MOTION_TIME_B 34
#define VREGS_MOTION_NEW_DATA_FLAG 35
#define VREGS_MOTION_CRC 36
#define VREGS_MOTION_UPDATE 37

#define VREGS_MOTOR_VOLTAGE 40
#define VREGS_MOTOR_CURRENT 41
#define VREGS_H_BRIDGE_MODE 42
#define VREGS_H_BRIDGE_DIRECTION 43
#define VREGS_H_BRIDGE_DUTY_A 44
#define VREGS_H_BRIDGE_DUTY_B 45

#define VREGS_STD_DEVIATION_A 46
#define VREGS_STD_DEVIATION_B 47
#define VREGS_STD_DEVIATION_C 48
#define VREGS_STD_DEVIATION_D 49

#ifdef DEBUG_VREGS
#define VREGS_CURRENT_CONTROL_KP 50
#define VREGS_CURRENT_CONTROL_KI 51
#define VREGS_CURRENT_CONTROL_E_POS 52
#define VREGS_CURRENT_CONTROL_E_NEG 53
#define VREGS_CURRENT_CONTROL_EI_POS 54
#define VREGS_CURRENT_CONTROL_EI_NEG 55
#define VREGS_CURRENT_CONTROL_DT 56
#define VREGS_CURRENT_MEASURED 57
#define VREGS_CURRENT_SETPOINT_A 58
#define VREGS_CURRENT_SETPOINT_B 59

#define VREGS_ADC_DATA0A 65 // Hall sensor 1a
#define VREGS_ADC_DATA0B 66 // Hall sensor 1b
#define VREGS_ADC_DATA1A 67 // Hall sensor 2a
#define VREGS_ADC_DATA1B 68 // Hall sensor 2b
#define VREGS_ADC_DATA2A 69 // Hall sensor 3a
#define VREGS_ADC_DATA2B 70 // Hall sensor 3b
#define VREGS_ADC_DATA3A 71
#define VREGS_ADC_DATA3B 72
#define VREGS_ADC_DATA4A 73
#define VREGS_ADC_DATA4B 74
#define VREGS_ADC_DATA5A 75
#define VREGS_ADC_DATA5B 76
#define VREGS_ADC_DATA6A 77
#define VREGS_ADC_DATA6B 78

#define VREGS_FAN_1_SPEED 84
#define VREGS_FAN_2_SPEED 85
#define VREGS_TEST_FIELD 86
#define VREGS_RCC_CSR31_24 87
#define VREGS_RCC_CSR23_16 88

#define VREGS_DEBUG_FLAG_1 90 // Calibrate
#define VREGS_DEBUG_FLAG_2 91
#define VREGS_DEBUG_FLAG_3 92
#define VREGS_DEBUG_FLAG_4 93
#define VREGS_DEBUG_FLAG_5 94
#define VREGS_DEBUG_FLAG_6 95
#define VREGS_DEBUG_FLAG_7 96

#define VREGS_POOTBUS_STATE 100
#define VREGS_POOTBUS_BUSY	101

#define VREGS_ENCODER_POSITION_A 110
#define VREGS_ENCODER_POSITION_B 111
#define VREGS_ENCODER_DIR		 112
#define VREGS_MOTION_FSM_FLAG			 113
#define VREGS_MOTION_CONSTANT_SPEED     114
#define VREGS_MOTION_POSITION_RAMP_LENGTH 115
#define VREGS_MOTION_POSITION_SETPOINT_A  116
#define VREGS_MOTION_POSITION_SETPOINT_B  117

//#define VREGS_MOTION_DELTA_T 119
//#define VREGS_MOTION_DELTA_S_A 120
//#define VREGS_MOTION_DELTA_S_B 121
//#define VREGS_MOTION_ARRIVAL_TIME 122
//#define VREGS_MOTION_ARRIVAL_TIME_2 123
#define VREGS_MOTION_LAST_KNOWN_POSITION_A 119
#define VREGS_MOTION_LAST_KNOWN_POSITION_B 120


#define VREGS_PEAK_1_ADC_AVERAGE 130 //[Debug] Average of adc-samples hall sensor 1
#define VREGS_PEAK_2_ADC_AVERAGE 131 //[Debug] Average of adc-samples hall sensor 2
#define VREGS_PEAK_3_ADC_AVERAGE 132 //[Debug] Average of adc-samples hall sensor 3
#define VREGS_PEAK_ADC_SD_A 133 //[Debug] standard deviation of adc-samples sensor 1
#define VREGS_PEAK_ADC_SD_B 134 //[Debug] standard deviation of adc-samples sensor 2
#define VREGS_PEAK_ADC_SD_C 135 //[Debug] standard deviation of adc-samples sensor 3
#define VREGS_PEAK_1_DETECTED 136 //[Debug] Variable showing if currently a peak is being detected or not at sensor 1
#define VREGS_PEAK_2_DETECTED 137 //[Debug] Variable showing if currently a peak is being detected or not at sensor 2
#define VREGS_PEAK_3_DETECTED 138 //[Debug] Variable showing if currently a peak is being detected or not at sensor 3
#define VREGS_PEAK_MAX_AVG_DELTA_A 139 // [Debug] maximum value of average between two successive hall-sensor samples.
#define VREGS_PEAK_MAX_AVG_DELTA_B 140 // [Debug] maximum value of average between two successive hall-sensor samples.
#define VREGS_PEAK_AVG_DELTA 141 // [Debug] maximum value between two successive hall-sensor samples.
#define VREGS_PEAK_THRESHOLD 142
//#define VREGS_PEAK_1_HISTORY_1 141
//#define VREGS_PEAK_1_HISTORY_2 142
//#define VREGS_PEAK_1_HISTORY_3 143
//#define VREGS_PEAK_1_HISTORY_4 144
//#define VREGS_PEAK_FILTERED_DATA_0 145
//#define VREGS_PEAK_FILTERED_DATA_1 146
//#define VREGS_PEAK_FILTERED_DATA_2 147

#define VREGS_POSITION_CONTROL_KP 145
#define VREGS_POSITION_CONTROL_KI 146
#define VREGS_POSITION_CONTROL_KD 147
#define VREGS_POSITION_CONTROL_E_POS 148
#define VREGS_POSITION_CONTROL_E_NEG 149
#define VREGS_POSITION_CONTROL_EI_POS 150
#define VREGS_POSITION_CONTROL_EI_NEG 151
#define VREGS_POSITION_CONTROL_ED_POS 152
#define VREGS_POSITION_CONTROL_ED_NEG 153
#define VREGS_POSITION_CONTROL_DT 154
#define VREGS_POSITION_DELTA_S 155
#define VREGS_POSITION_MEASURED 156
#define VREGS_POSITION_ABSOLUTE_A 157
#define VREGS_POSITION_ABSOLUTE_B 158
#define VREGS_POSITION_DISTANCE_MOVE_A 159
#define VREGS_POSITION_DISTANCE_MOVE_B 160
#define VREGS_POSITION_DELTA_T_A 161
#define VREGS_POSITION_DELTA_T_B 162
#endif

/* END FIELD NAME DEFINITIONS */
/* Important: do not remove the line above, it is used by the debug tools */

void vregs_init();
int32_t vregs_write(uint32_t address, uint8_t data);
uint8_t vregs_read(uint32_t address);
uint8_t vregs_read_buffer(uint32_t address);
int32_t vregs_writeout();
int32_t vregs_writeout_specific();
uint8_t *vregs_get_buffer_address();


#endif /* __VREGS_H__ */
