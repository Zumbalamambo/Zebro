/**
 * POOT
 * The Zebro Project
 * Delft University of Technology
 * 2016
 *
 * Filename: h_bridge.h
 *
 * Description:
 * Code to drive the H-bridge
 *
 * Authors:
 * Piet De Vaere -- Piet@DeVae.re
 */

#ifndef H_BRIDGE_H
#define H_BRIDGE_H

#include "stdint.h"

#define H_BRIDGE_PRESCALER 0
#define H_BRDIGE_ARR 1023
#define H_BRIDGE_MAX_DUTYCYCLE (H_BRDIGE_ARR)
#define H_BRIDGE_MAX_MOTOR_SPEED (H_BRDIGE_ARR)
#define H_BRIDGE_FORWARD 1
#define H_BRIDGE_BACKWARD 0
#define H_BRIDGE_DEMO_DELAY 5000
#define H_BRIDGE_ADC_TRIGGER (H_BRDIGE_ARR)
//#define H_BRIDGE_EARLY_TRIGGER 240
//#define H_BRIDGE_LATE_TRIGGER 1440

#define H_BRIDGE_MODE_IDLE 0
#define H_BRIDGE_MODE_LOCKED_ANTI_PHASE 1
#define H_BRIDGE_MODE_SIGN_MAGNITUDE 2

#define H_BRIDGE_NOMINAL_MOTOR_VOLTAGE 24


void h_bridge_init(void);
uint8_t h_bridge_drive_motor(uint16_t, uint8_t, uint8_t);
void h_bridge_disable(void);
void h_bridge_lock_anti_phase(uint16_t);
void h_bridge_sign_magnitude(uint8_t, uint16_t);
void h_bridge_set_ch4(uint16_t);
int16_t h_bridge_remove_extreme_dutycycles(uint16_t);
//void h_bridge_demo(void);
//int32_t h_bridge_calc_and_set_ch4(int32_t dutycycle);
#endif /* H_BRIDGE_H */