/**
 * POOT
 * The Zebro Project
 * Delft University of Technology
 * 2016
 *
 * Filename: motion.h
 *
 * Description:
 * Motion controller. Keeps track of the current walking
 * mode or assignment, and decides how to actuate the
 * H-bridge.
 * *
 * Authors:
 * Daniel Booms --d.booms@solcon.nl
 * Piet De Vaere -- Piet@DeVae.re
 *
 * Last edited:
 * 06-03-2017		Floris Rouwen		florisrouwen@outlook.com
 */

#ifndef __MOTION_H__
#define __MOTION_H__

#define MOTION_MODE_IDLE 0
#define MOTION_MODE_CALIBRATE 1
#define MOTION_MODE_STAND_UP 2
#define MOTION_MODE_WALK_FORWARD 3
#define MOTION_MODE_CONTINUOUS_ROTATION 10
#define MOTION_MODE_MOVE_TO_LIFT_OFF 11
#define MOTION_MODE_PANIC_STOP 255


//#define MOTION_DEBUG_COMMAND 5
#define MOTION_DIRECTION_FORWARD 0
#define MOTION_DIRECTION_BACKWARD 1

/* At a very maximum a leg should be able to spin 2 rounds in 1 second (120 RPM). This is 1820 pulses per second.
 * The motor is measured to do 140 RPM at full dutycycle. So our max dutycycle is 223 for 120 RPM. Now we have (1820/1)/223 ~= 8.
 */
/* This is not 8 because it is also a measure of how aggressive the speed up is when the leg is delayed. Lower is more aggressive. */
#define DUTYCYCLE_PER_PULSE_PER_SECOND 5
/* This determines how many pulses the encoder can pass a certain position and still say it reached the position. This is necessary especially when the loop is not fast enough. */
#define MOTION_POSITION_HYSTERESIS 5
/* Time in ms the leg gets to turn back the leg to the last known position. */
#define STABALIZING_TIME 250

#define ARRAY_SIZE 255
#define TOUCH_DOWN_POSITION_L 350
#define TOUCH_DOWN_POSITION_R 560
#define STAND_UP_POSITION_L 310
#define STAND_UP_POSITION_R 610 // 910-585
#define LIFT_OFF_POSITION_L 270
#define LIFT_OFF_POSITION_R 650

#include "stdint.h"

struct motion_state{
	uint8_t mode;
	uint8_t lift_off_time_a;
	uint8_t lift_off_time_b;
	uint8_t touch_down_time_a;
	uint8_t touch_down_time_b;
	uint8_t new_data_flag;
	uint8_t crc;
};

int32_t motion_new_zebrobus_data(uint32_t address, uint8_t data);
int32_t motion_write_state_to_vregs(struct motion_state motion_state);
int32_t motion_validate_state(struct motion_state motion_state);
int32_t motion_drive_h_bridge();
uint8_t motion_move_to_point(uint16_t point, uint8_t dir, uint32_t arrival_time);
//void set_next_walk_instruction(uint8_t value);
uint32_t get_std_var (void);
uint16_t std_var_stable(uint16_t *a, uint16_t n);
uint32_t isqrt(uint32_t x);
void motion_set_state(uint8_t mode, uint8_t lift_off_time_a, uint8_t lift_off_time_b, uint8_t touch_down_time_a,
		uint8_t touch_down_time_b, uint8_t new_data_flag, uint8_t crc);
int32_t motion_stop(void);
uint8_t get_calibrate(void);
void set_calibrate(uint8_t value);
void motion_init(void);

/* ------- OLD CODE ---------*/

//#define MOTION_SEGMENTS_PER_FULL_CIRCLE 240 /*number of angle segments in a full circle*/
//#define MOTION_SEGMENTS_PER_HALF_CIRCLE (MOTION_SEGMENTS_PER_FULL_CIRCLE>>1)
/*Conversion factor between the two basic units 10RPM and angle segments per second*/
//#define MOTION_SEGMENTS_PER_SECOND_PER_RPM (MOTION_SEGMENTS_PER_FULL_CIRCLE*10/60)
//#define MOTION_MILLISECOND_PER_SECOND 1000
//#define MOTION_SEGMENTS_PER_INDEX 10 /*index is 10 angle segments*/
//#define MOTION_CONTROLLABLE_LIMIT 200 /*angle pieces*/
//#define MOTION_NEUTRAL_DUTYCYCLE 1 /*Motor is controlled locked anti-phase, with dutycycle 50 the motor stops*/
//#define MOTION_CONTROLLER_P 1 /*Proportional control value*/
//#define MOTION_FLAG_SET 1
//#define MOTION_FLAG_CLEAR 0

/*For Debug*/
/*#define VREGS_MOTION_MODE 32
#define VREGS_MOTION_SPEED 33
#define VREGS_MOTION_PHASE 34
#define VREGS_MOTION_EXTRA 35
#define VREGS_MOTION_CRC 36
#define VREGS_MOTION_UPDATE 37
#define ADDRESS_LEFT 0
#define ADDRESS_RIGHT 1
#define TIME_MAX_TIME_MS 256000
*/

//struct motion_timestamped_angle{
//    int32_t angle;
//    int32_t timestamp;
//};

//int32_t motion_virtual_angle(int32_t motion_mode, int32_t time);
//int32_t motion_estimated_currect_angle(int32_t time);
//int32_t motion_angle_error(int32_t time);
//int32_t motion_drive_h_bridge_simple(void);
//int32_t motion_update_data(void);
//int32_t motion_go_to_position_dumb(void);
//void motion_init(void);
//void motion_turn(void);
#endif /* __MOTION_H__ */
