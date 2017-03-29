/**
 * POOT
 * The Zebro Project
 * Delft University of Technology
 * 2016
 *
 * Filename: motion.c
 *
 * Description:
 * Motion controller. Keeps track of the current walking
 * mode or assignment, and decides how to actuate the
 * H-bridge.
 * *
 * Authors:
 * Daniel Booms -- d.booms@solcon.nl
 * Piet De Vaere -- Piet@DeVae.re
 *
 * Last edited:
 * 06-03-2017		Floris Rouwen		florisrouwen@outlook.com
 */

#include "stdint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "motion.h"
#include "vregs.h"
#include "h_bridge.h"
#include "address.h"
#include "time.h"
#include "encoder.h"
#include "peak.h"
#include "adc.h"
#include "errors.h"
#include "interrupts.h"

static struct motion_state state = { 0, 0, 0, 0, 0, 0, 0 };
static struct motion_state new_state = { 0, 0, 0, 0, 0, 0, 0 };
uint32_t std_var; /* standard deviation of the hall-sensor with a peak at hall-sensor 3. */
uint16_t position_touch_down = 0;
uint8_t calibrate = 1; /* When Zebro is turned on, calibrate should first be on. */
static uint16_t touch_down_position = 0;
static uint16_t stand_up_position = 0;
static uint16_t lift_off_position = 0;
static uint16_t last_known_position = 0;
static uint8_t side_of_zebro = 0;
//static uint32_t increasing_delay = 0;
/* current setpoint. This is a setpoint between -2^15 and 2^15 */
static int32_t current_setpoint = 0;

/**
 * Process data send to any of the addresses in the motion control range.
 */
int32_t motion_new_zebrobus_data(uint32_t address, uint8_t data) {

	switch (address) {

	/* set mode */
	case VREGS_MOTION_MODE:
		new_state.mode = data;
		break;

	case VREGS_MOTION_LIFT_OFF_TIME_A:
		new_state.lift_off_time_a = data;
		break;

	case VREGS_MOTION_LIFT_OFF_TIME_B:
		new_state.lift_off_time_b = data;
		break;

	case VREGS_MOTION_TOUCH_DOWN_TIME_A:
		new_state.touch_down_time_a = data;
		break;

	case VREGS_MOTION_TOUCH_DOWN_TIME_B:
		new_state.touch_down_time_b = data;
		break;

	case VREGS_MOTION_NEW_DATA_FLAG:
		new_state.new_data_flag = data;
		break;

		/* set crc */
	case VREGS_MOTION_CRC:
		new_state.crc = data;
		break;

		/* check if the new command is sane, and if it is, activate it.
		 * Reset the 'new_state' struct in either case
		 */
	case VREGS_MOTION_UPDATE:
		if (!motion_validate_state(new_state) && ((calibrate == 0)
				|| (new_state.mode == 0) /* idle state should always be reached */
				|| (new_state.mode == 1) /* we should always be able to go to calibration state if necessary */
				|| (new_state.mode == 255))) { /* panic state should of course always be reachable */
			state = new_state;
			motion_write_state_to_vregs(state);
		}
		new_state.mode = 0;
		new_state.lift_off_time_a = 0;
		new_state.lift_off_time_b = 0;
		new_state.touch_down_time_a = 0;
		new_state.touch_down_time_b = 0;
		new_state.new_data_flag = 0;
		new_state.crc = 0;
		break;

	default:
		break;

	}
	return 0;
}

/**
 * Write the values of the given state to the vregs
 */
int32_t motion_write_state_to_vregs(struct motion_state motion_state) {
	vregs_write(VREGS_MOTION_MODE, motion_state.mode);
	vregs_write(VREGS_MOTION_LIFT_OFF_TIME_A,
			(uint8_t) (motion_state.lift_off_time_a));
	vregs_write(VREGS_MOTION_LIFT_OFF_TIME_B, motion_state.lift_off_time_b);
	vregs_write(VREGS_MOTION_TOUCH_DOWN_TIME_A, motion_state.touch_down_time_a);
	vregs_write(VREGS_MOTION_TOUCH_DOWN_TIME_B, motion_state.touch_down_time_b);
	vregs_write(VREGS_MOTION_NEW_DATA_FLAG, motion_state.new_data_flag);
	vregs_write(VREGS_MOTION_CRC, motion_state.crc);
	return 0;
}

/**
 * Stub for the state validator.
 * return zero is state is OK.
 */
int32_t motion_validate_state(struct motion_state motion_state) {
	return 0;
}

/**
 * Look at the drive command, the position data, and the time.
 * Use this data to choose how to actuate the h_bridge.
 *
 * Info:
 * Left leg : get_address_side = 0; walking forward = counterclockwise = h_bridge_drive(x, 0, x) : encoder_dir = 1 : encoder = counting down.
 *
 * The finite state machine toggled by fsm_flag is as follows. For each number of fsm_flag=
 * 		0: Set encoder in the middle of it's range.
 * 		1: Rotate the leg backwards until it touches the ground.
 * 		2: Rotate the leg forward and collect hall-sensor samples. Store these in an array.
 * 		3: Reset the peak_detected array.
 * 		4: Rotate the leg backward until the 3rd hall-sensor is found and set the encoder value to zero.
 * 		5: Calibration completed, calculate the arrival time for the leg to stand position.
 * 		6: Rotate the leg forward until the stand position is reached.
 * 		7:
 */
int32_t motion_drive_h_bridge() {
	int32_t return_value = 0;
	static uint16_t adc_data[ARRAY_SIZE];
	static uint8_t fsm_flag = 0;
	static uint16_t position[2] = { 0, 0 };
	uint8_t side_of_zebro = address_get_side();
//	static uint32_t stand_up_time = 0;
//	static uint32_t touch_down_time = 0;
//	static uint32_t lift_off_time = 0;
//	static uint8_t position_reached = 0;
//	static uint8_t stabilize = 0;
//	static uint8_t flag = 0;
	static uint8_t stabilizing_direction = 0;
//	static uint16_t difference_position = 0;
//	static uint16_t difference_position_complement = 0;
//	static uint16_t current_position = 0;
//	static int16_t error, error_d, error_old;
//	static int32_t error_i;
//	static uint16_t Kp = 5000, Ki = 0, Kd = 10;
//	static int32_t pid_output;
//	static uint32_t dt, time_old;
//	static uint16_t duty_cycle;

#ifdef DEBUG_VREGS
	vregs_write(VREGS_MOTION_STD_DEV_A, (uint8_t) get_std_var());
	vregs_write(VREGS_MOTION_STD_DEV_B, (uint8_t) (get_std_var() >> 8));
	vregs_write(VREGS_FSM_FLAG, (uint8_t) fsm_flag);
	vregs_write(VREGS_DEBUG_FLAG_1, (uint8_t) calibrate);
	vregs_write(VREGS_MOTION_LAST_KNOWN_POSITION_A,
			(uint8_t) last_known_position);
	vregs_write(VREGS_MOTION_LAST_KNOWN_POSITION_B,
			(uint8_t) (last_known_position >> 8));
	vregs_write(VREGS_MOTION_STABILIZING_DIRECTION,
			(uint8_t) (stabilizing_direction));
	vregs_write(VREGS_CURRENT_SETPOINT, (uint8_t) (current_setpoint >> 3));
#endif

	switch (state.mode) {

	case MOTION_MODE_IDLE:
		/* Function stabilizes the leg on the last known position
		 * But: not more than a quarter of an entire circle.
		 */
		fsm_flag = 0;
		current_setpoint = 0;
		h_bridge_disable();
		return 0;

	case MOTION_MODE_CALIBRATE:
		if (fsm_flag == 0) {
			/* Set calibrate to 1 to enable reading adc-data of the hall-sensors */
			set_calibrate(1);
			/* Set the encoder value somewhere in the middle of its range to prevent wrapping around immediately. */
//			encoder_set_position_mid();
			fsm_flag = 1;
		}
		if (fsm_flag == 1) {
//			h_bridge_drive_motor(MOTION_PROBE_SPEED, !side_of_zebro,
//			H_BRIDGE_MODE_SIGN_MAGNITUDE);

			if (side_of_zebro == 0) {
				current_setpoint = MOTION_PROBE_CURRENT_SETPOINT;
			}else {
				current_setpoint = -MOTION_PROBE_CURRENT_SETPOINT;
			}
			/* If the legs current position is smaller of equal to the last position, we are touching the ground.
			 * What if the leg keeps spinning? How to calibrate then, because of overflow encoder etc? */
			if ((position[!side_of_zebro] <= position[side_of_zebro])) { /* We will always enter this loop the first time the calibrate-loop runs because positions are initialized equal.*/
				start_timing_measurement();
				/* wait for position to hold */
				if (stop_and_return_timing_measurement(400)) {
					h_bridge_disable();
					current_setpoint = 0;
					fsm_flag = 2;
				}
			} else {
				/* If the position condition was not satisfied, reset timer. */
				reset_timing_measurement();
			}
		}
		if (fsm_flag == 2) {
			static uint16_t n = 0;
//			h_bridge_drive_motor(MOTION_PROBE_SPEED, side_of_zebro,
//			H_BRIDGE_MODE_SIGN_MAGNITUDE);

			if (side_of_zebro == 0) {
				current_setpoint = -MOTION_PROBE_CURRENT_SETPOINT;
			}else {
				current_setpoint = MOTION_PROBE_CURRENT_SETPOINT;
			}
			if (time_get_time_ms() % 10 == 0) { /* We don't want to sample too often */
				adc_data[n] = adc_get_value(2); /* We're only interested in the value of the 3rd hall sensor */
				n = n + 1;
			}
			if (n >= ARRAY_SIZE) {
				n = ARRAY_SIZE - 1;
			}
			/* If the legs current position is larger or equal to the last position, we are touching the ground.
			 * There is a problem when the encoder goes from 910 --> 0 or the other way around.
			 * What if the leg keeps spinning? How to calibrate then, because of overflow encoder etc? */
			if ((position[!side_of_zebro] >= position[side_of_zebro])) { /* Condition is different since we turn the other way */
				start_timing_measurement();
				/* wait for position data */
				if (stop_and_return_timing_measurement(400)) {
					h_bridge_disable();
					current_setpoint = 0;
					/* Calculate the standard deviation of the collected samples. */
					std_var = std_var_stable(adc_data, n);
					fsm_flag = 3;
				}
			} else {
				/* If the position condition was not satisfied, reset timer. */
				reset_timing_measurement();
			}
		}
		if (fsm_flag == 3) {
			reset_peak_detected();
			fsm_flag = 4;
		}
		if (fsm_flag == 4) {
//			h_bridge_drive_motor(MOTION_PROBE_SPEED, !side_of_zebro,
//			H_BRIDGE_MODE_SIGN_MAGNITUDE);

			if (side_of_zebro == 0) {
				current_setpoint = MOTION_PROBE_CURRENT_SETPOINT;
			}else {
				current_setpoint = -MOTION_PROBE_CURRENT_SETPOINT;
			}
			if (get_peak_detected(2) == 1) {
				h_bridge_disable();
				current_setpoint = 0;
				encoder_reset_position();
				last_known_position = encoder_get_position();
				set_calibrate(0);
				motion_stop();
				/* Do we need to free the adc_data memory? We don't need this array after this. */
//				for (i = 0; i < (ARRAY_SIZE - 1); i++) {
//					free(&adc_data[i]);
//					adc_data[i] = 0;
//				}
			}
		}

		/* Last thing that should happen in this loop, get new encoder data. Slower is better since there is a higher chance of spotting the stopping of rotation. */
		if (time_get_time_ms() % 200 == 0) {
			position[0] = position[1];
			position[1] = encoder_get_position();
		}
		break;

	case MOTION_MODE_STAND_UP:
//		if (fsm_flag == 0) {
//			fsm_flag = 5;
//		}
//		stand_up_time = (state.lift_off_time_a * 4) /* We use lift off time because the state-machine is either at 0 or at 9 and thus it is allowed to touch the lift_off_time */
//		+ (state.lift_off_time_b * 1000);
//		if (fsm_flag == 5) {
//			fsm_flag = fsm_flag + motion_move_to_point(stand_up_position,
//			MOTION_DIRECTION_FORWARD, stand_up_time);
//		}
//		if (fsm_flag == 6) {
//			stand_up_time = 0;
//			motion_stop();
//		}
		break;

		/* it is assumed that if we move to lift off, we will also always move to touch down. After that, we'll see what we'll do. */
	case MOTION_MODE_WALK_FORWARD:
		/* current setpoint should be between -1000 and 1000 for an amp */
		start_timing_measurement();
		if (stop_and_return_timing_measurement(2) == 1) {
			if ((current_setpoint == -1000) | (current_setpoint == 0)) {
				interrupts_disable();
				current_setpoint = 1000;
				interrupts_enable();
			}else if (current_setpoint == 1000) {
				interrupts_disable();
				current_setpoint = -1000;
				interrupts_enable();
			}
		}
//		lift_off_time = (state.lift_off_time_a * 4)
//				+ (state.lift_off_time_b * 1000);
//		touch_down_time = (state.touch_down_time_a * 4)
//				+ (state.touch_down_time_b * 1000);
//
//		if (fsm_flag == 0) {
//			fsm_flag = 8;
//		}
//
//		if (fsm_flag == 8) {
//			position_reached = motion_move_to_point(lift_off_position,
//			MOTION_DIRECTION_FORWARD, lift_off_time);
//			if ((position_reached == 1)) {
//				// state.new_data_flag = 0;
//				position_reached = 0;
//				fsm_flag = fsm_flag + 1;
//				/* We could insert a motion_stop to break out of the step, keep the fsm_flag at 9 and return to the stand-position. Just a thought. */
//			}
//		}
//		if (fsm_flag == 9) {
//			position_reached = motion_move_to_point(touch_down_position,
//			MOTION_DIRECTION_FORWARD, touch_down_time);
//			if ((position_reached == 1) && (state.new_data_flag == 1)) {
//				state.new_data_flag = 0;
//				position_reached = 0;
//				fsm_flag = fsm_flag + 1;
//			} else if ((position_reached == 1) && (state.new_data_flag == 0)) {
//				motion_stop(); /* now the fsm_flag is still 9 */
//			} /* else? assume leg didn't make it? */
//		}
//		if (fsm_flag == 10) {
//			fsm_flag = 0;
//			/* if we use motion_stop(), we only get into this loop when there actually is data.
//			 * But then the leg stays at the touch down position instead of stand-position.
//			 */
//			motion_stop();
//		}
		break;

	case MOTION_MODE_WALK_BACKWARD:
		current_setpoint = 1000;
		break;

	case MOTION_MODE_CONTINUOUS_ROTATION:
		/* This mode is more a debug thing. */
		/* Continuously rotate the leg forward at a 10th of the dutycycle */
//		start_timing_measurement();
//		if (stop_and_return_timing_measurement(1) == 1) {
//			interrupts_disable();
//			current_setpoint = ((~current_setpoint) + 1);
//			interrupts_enable();
//		}
//				&& (duty_cycle <= 895)) {
//			duty_cycle = duty_cycle + 64;
//			h_bridge_drive_motor(duty_cycle, !side_of_zebro,
//					H_BRIDGE_MODE_SIGN_MAGNITUDE);
//		}
//		if (duty_cycle > 895) {
//			motion_stop();
//		}
		break;

	case MOTION_MODE_MOVE_TO_LIFT_OFF:
//		lift_off_time = (state.lift_off_time_a * 4)
//				+ (state.lift_off_time_b * 1000);
//		if (motion_move_to_point(lift_off_position,
//		MOTION_DIRECTION_FORWARD, lift_off_time)) {
//			motion_stop();
//		}
		break;

	default:
		motion_stop();
		return_value = 255;
	}
	motion_write_state_to_vregs(state);
	return return_value;
}

/* Function to move the leg to a certain position (touch down, stand, or lift) with a certain direction (forward or backward).
 * dir should be 0 for forward and 1 for backward. Arrival time should be in absolute milliseconds.
 *
 * For fluent movement: no h_bridge_disable() in this function
 *
 * Don't put motion_stop() in this function. Do this after using this function.
 *
 * Info:
 * Left leg : get_address_side = 0; walking forward = counterclockwise = h_bridge_drive(x, 0, x) : encoder_dir = 1 : encoder = counting down.
 */
//uint8_t motion_move_to_point(uint16_t position, uint8_t dir,
//		uint32_t arrival_time) {
//	uint8_t status = 0; /* Turns 1 when the point was reached. */
//	uint8_t direction = side_of_zebro ^ dir; /* Bitwise xor makes direction correct for any dir on any leg. */
//	int16_t dutycycle = 0;
//	uint16_t difference_position = 0;
//	uint32_t difference_time = 0;
//	uint16_t current_position = encoder_get_position();
//	static int32_t time_old_error_d_loop, time_old_desired_speed_loop,
//			time_old_position, average_current_speed, time_old_error_i_loop,
//			pid_output;
//	static int16_t error_old, error_d, error, error_i, current_speed[4]; /* doesn't need to be static. For debug it is */
//	int32_t delta_time;
//	static uint32_t desired_speed;
////	static uint16_t Kp = 128, Ki = 32, Kd = 4, position_old;
//	static uint16_t Kp = 200, Ki = 40, Kd = 1, position_old;
//
//	difference_position = abs(position - current_position);
//
//	if (dir == 1) {
//		/* Calculate the number of pulses before we reach the correct position. */
//		if (!side_of_zebro) {
//			if (position < current_position) {
//				difference_position = ENCODER_PULSES_PER_ROTATION
//						- difference_position;
//			}
//		} else if (side_of_zebro) {
//			if (position > current_position) {
//				difference_position = ENCODER_PULSES_PER_ROTATION
//						- difference_position;
//			}
//		} else {
//			/* should never happen */
//			difference_position = 0; /* This is safe since dutycycle will be zero because of this. */
//		}
//
//		/* Forward */
//	} else if (dir == 0) {
//		/* Calculate the number of pulses before we reach the correct position. */
//		if (!side_of_zebro) {
//			if (position > current_position) {
//				difference_position = ENCODER_PULSES_PER_ROTATION
//						- difference_position;
//			}
//		} else if (side_of_zebro) {
//			if (position < current_position) {
//				difference_position = ENCODER_PULSES_PER_ROTATION
//						- difference_position;
//			}
//		} else {
//			/* should never happen */
//			difference_position = 0; /* This is safe since dutycycle will be zero because of this. */
//		}
//	} else {
//		// do nothing
//	}
//
//	/* Check if the leg arrived. OLD STATEMENT FOR DIR=0 */
////		if (((current_position <= position + MOTION_POSITION_HYSTERESIS)
////				&& (current_position >= (position - MOTION_POSITION_HYSTERESIS))
////				&& (!side_of_zebro))
////				|| ((current_position >= position - MOTION_POSITION_HYSTERESIS)
////						&& (current_position
////								<= (position + MOTION_POSITION_HYSTERESIS))
////						&& (side_of_zebro))) {
//	/* Later on we will check if the time difference is large than zero. */
//	difference_time = time_calculate_delta(arrival_time, time_get_time_ms());
//
//	/* Calculate the dutycycle necessary to get the leg at the right position at the right time. */
//	if ((difference_time > 0) && (difference_position > 0)) {
//		if (time_calculate_delta(time_get_time_ms(),
//				time_old_desired_speed_loop) >= 10) {
//			time_old_desired_speed_loop = time_get_time_ms();
//			desired_speed = ((((difference_position) * 1000) << 12)
//					/ difference_time) >> 12;
//		}
//		/* prevent wrap around? */
//		if ((abs(position_old - current_position)) >= 3) {
////			current_speed[3] = (((abs(position_old - current_position)) * 1000)
////					/ (time_calculate_delta(time_get_time_ms(),
////							time_old_position)));
////			time_old_position = time_get_time_ms();
////			uint8_t i = 0;
////			int16_t sum = 0;
////			for (i = 0; i < 4; i++) {
////				sum += current_speed[i];
////			}
////			average_current_speed = sum >> 2; //Divide sum to get average. We lose some accuracy, but this is no problem.
////
////			uint8_t j;
////			for (j = 0; i < 3; j++) {
////				current_speed[j] = current_speed[j + 1];
////			}
//			average_current_speed = (((abs(position_old - current_position)) * 1000)
//										/ (time_calculate_delta(time_get_time_ms(),
//												time_old_position)));
//			/* number between -1820 and +1820 */
//			error = desired_speed - average_current_speed;
//			position_old = current_position;
//		} else {
//			/* current speed & errors stays as it is */
//		}
//		if (time_calculate_delta(time_get_time_ms(), time_old_error_d_loop)
//				>= 5) {
//			delta_time = time_calculate_delta(time_get_time_ms(),
//					time_old_error_d_loop);
//			time_old_error_d_loop = time_get_time_ms();
//			/* number between -5000 and +5000 */
//			error_d = ((error - error_old) * 1000) / delta_time;
//			error_old = error;
//		} else {
//			/* do nothing */
//		}
//		/* error is a number between -1820 and +1820. error_d is a number between -4000 and +4000; error_i is a number between -100 and +100
//		 * Say we want all K-values between 0 and 100. Then pid_output has a max of: 100*1820 + 25*4000 + 1820*100 = 464,000
//		 */
//		pid_output = ((Kp * error) + (Ki * error_i) + (Kd * error_d));
//
//		/* The error_i needs a lot of time because the error values are between 0 and 255 and that is not high enough to allow multiplying by at dt of 0.001. We need at least a 100 ms. */
//		if (time_calculate_delta(time_get_time_ms(), time_old_error_i_loop)
//				>= 100) {
//			delta_time = time_calculate_delta(time_get_time_ms(),
//					time_old_error_i_loop);
//			time_old_error_i_loop = time_get_time_ms();
//			/* If motor is at max torque we want to stop the integrating error. Max torque is defined as: 255 << 14. Same order of magnitude as the other errors.*/
//			if (abs(pid_output) >= 464000
//					&& (((error >= 0) && (error_i >= 0))
//							|| ((error < 0) && (error_i < 0)))) {
//				/* Leave error_i as it was. */
//			} else {
//				/* number between -100 and +100 */
//				error_i = error_i + ((((error << 8) * delta_time) / 1000) >> 8);
//			}
//		}
//
//		/* this should be between -250 and 250! */
//		dutycycle = (desired_speed >> 3) + (pid_output >> 11);
//
//		if (dutycycle > 250 || dutycycle < -250) {
//			if (dutycycle > 0) {
//				dutycycle = 250;
//			}
//			if (dutycycle < 0) {
//				dutycycle = -250;
//			}
//		}
//		if (dutycycle < 0) {
//			direction = !direction;
//			dutycycle = (uint8_t) (-dutycycle);
//		} else {
//			dutycycle = (uint8_t) dutycycle;
//		}
//	} else {
//		difference_time = 0;
//		dutycycle = 0; /* We are not actually sending this value to the h-bridge, because that would be bad. */
//	}
//	/* Check if the leg arrived. OLD STATEMENT FOR DIR=1 */
////		if (((current_position >= position - MOTION_POSITION_HYSTERESIS)
////				&& (current_position <= (position + MOTION_POSITION_HYSTERESIS))
////				&& (!side_of_zebro))
////				|| ((current_position <= position + MOTION_POSITION_HYSTERESIS)
////						&& (current_position
////								>= (position - MOTION_POSITION_HYSTERESIS))
////						&& (side_of_zebro))) {
//	if (current_position == position) { /* This works when checking at at least 1820 Hz since we want to guarantee functionality. */
//		if (increasing_delay > 2500) {
//			increasing_delay = increasing_delay - 2500;
//		}
//		last_known_position = position;
//		arrival_time = 0;
//		/* Null things when step is done, otherwise it influences the next step. */
//		error = 0;
//		error_old = 0;
//		error_d = 0;
//		error_i = 0;
//		desired_speed = 0;
//		current_speed[0] = 0;
//		current_speed[1] = 0;
//		current_speed[2] = 0;
//		current_speed[3] = 0;
//		average_current_speed = 0;
//		position_old = 0;
//		status = 1;
//	} else {
//		if (dutycycle > 0) {
//			h_bridge_drive_motor(dutycycle, direction,
//			H_BRIDGE_MODE_SIGN_MAGNITUDE);
//		} else {
//			/* just keep turning. Don't really know if this is nice. We'll see. */
//			h_bridge_drive_motor(25, direction,
//			H_BRIDGE_MODE_SIGN_MAGNITUDE);
//		}
//	}
//
//#ifdef DEBUG_VREGS
//	vregs_write(VREGS_MOTION_ARRIVAL_TIME, (arrival_time >> 10));
//	vregs_write(VREGS_MOTION_DUTYCYCLE, dutycycle);
//	vregs_write(VREGS_MOTION_DELTA_T, difference_time >> 10);
//	vregs_write(VREGS_MOTION_DELTA_S_A, difference_position);
//	vregs_write(VREGS_MOTION_DELTA_S_B, difference_position >> 8);
//
//	vregs_write(VREGS_MOTION_DESIRED_SPEED, (uint8_t) (desired_speed >> 3));
//	vregs_write(VREGS_MOTION_CURRENT_SPEED,
//			(uint8_t) (average_current_speed >> 3));
//	if (error >= 0) {
//		vregs_write(VREGS_MOTION_PID_ERROR_A, (uint8_t) error);
//		vregs_write(VREGS_MOTION_PID_ERROR_B, (uint8_t) (error >> 8));
//	}
//	if (error_i >= 0) {
//		vregs_write(VREGS_MOTION_PID_ERROR_I_A, (uint8_t) error_i);
//		vregs_write(VREGS_MOTION_PID_ERROR_I_B, (uint8_t) (error_i >> 8));
//	}
//	if (error_d >= 0) {
//		vregs_write(VREGS_MOTION_PID_ERROR_D_A, (uint8_t) error_d);
//		vregs_write(VREGS_MOTION_PID_ERROR_D_B, (uint8_t) (error_d >> 8));
//	}
//	if (pid_output >= 0) {
//		vregs_write(VREGS_MOTION_PID_OUTPUT_P, (uint8_t) (pid_output >> 11));
//	}
//	if (pid_output < 0) {
//		vregs_write(VREGS_MOTION_PID_OUTPUT_M, (uint8_t) ((-pid_output) >> 11));
//	}
//#endif
//	return status;
//}

/* Set the setpoint for current */
void set_current_setpoint (int32_t value) {
	current_setpoint = value;
}

/* Get the setpoint for current */
int32_t get_current_setpoint (void) {
	return current_setpoint;
}

uint8_t get_state_mode (void) {
	return state.mode;
}

void set_state_mode (uint8_t mode) {
	state.mode = mode;
	return;
}

/* Return standard deviation */
uint32_t get_std_var(void) {
	return std_var;
}

/* Calculate the standard deviation from a vector with length n. */
uint32_t std_var_stable(uint16_t *a, uint16_t n) {
	if (n == 0)
		return 0;
	uint64_t sum = 0;
	uint64_t sq_sum = 0;
	unsigned i = 0;
	for (i = 0; i < n; ++i) {
		uint32_t ai = a[i];
		sum += ai;
		sq_sum += ai * ai; /*We lose accuracy, but it's okay*/
	}
	uint64_t N = n;
	uint32_t std_var = (N * sq_sum - sum * sum) / (N * N);
	std_var = isqrt(std_var);
	return std_var;
}

/* Take the square root of x and round to the nearest integer. Copied from somewhere. */
uint32_t isqrt(uint32_t x) {
	register unsigned long op, res, one;

	op = x;
	res = 0;

	/* "one" starts at the highest power of four <= than the argument. */
	one = 1 << 30; /* second-to-top bit set */
	while (one > op)
		one >>= 2;

	while (one != 0) {
		if (op >= res + one) {
			op -= res + one;
			res += one << 1; /* <-- faster than 2 * one */
		}
		res >>= 1;
		one >>= 2;
	}

	/* Do arithmetic rounding to nearest integer */
	if (op > res) {
		res++;
	}

	return res;
}

/**
 * This is hard set of the motion state
 */
void motion_set_state(uint8_t mode, uint8_t lift_off_time_a,
		uint8_t lift_off_time_b, uint8_t touch_down_time_a,
		uint8_t touch_down_time_b, uint8_t new_data_flag, uint8_t crc) {
	state.mode = mode;
	state.lift_off_time_a = lift_off_time_a;
	state.lift_off_time_b = lift_off_time_b;
	state.touch_down_time_a = touch_down_time_a;
	state.touch_down_time_b = touch_down_time_b;
	state.new_data_flag = new_data_flag;
	state.crc = crc;
	motion_write_state_to_vregs(state);
}

/**
 * Clear the current motion command, and stop
 */
int32_t motion_stop(void) {
	state.mode = 0;
	state.lift_off_time_a = 0;
	state.lift_off_time_b = 0;
	state.touch_down_time_a = 0;
	state.touch_down_time_b = 0;
	state.new_data_flag = 0;
	state.crc = 0;
	motion_write_state_to_vregs(state);

	return 0;
}

/* Return the Calibrate flag to show the encoder can be calibrated with the hall-sensors. */
uint8_t get_calibrate(void) {
	return calibrate;
}

/* Set calibrate flag to 0 or 1 to be able to calibrate the encoder with the hall-sensors. */
void set_calibrate(uint8_t value) {
	calibrate = value;
	return;
}

/*
 * This is initializer of the motion data
 * This function should only be called when the adc
 * is correctly initialized
 */
void motion_init(void) {
	motion_stop();
	side_of_zebro = address_get_side();
	touch_down_position = (!(side_of_zebro) * TOUCH_DOWN_POSITION_L)
			+ (side_of_zebro * TOUCH_DOWN_POSITION_R);
	stand_up_position = (!(side_of_zebro) * STAND_UP_POSITION_L)
			+ (side_of_zebro * STAND_UP_POSITION_R);
	lift_off_position = (!(side_of_zebro) * LIFT_OFF_POSITION_L)
			+ (side_of_zebro * LIFT_OFF_POSITION_R);

#ifdef DEBUG_VREGS
	vregs_write(VREGS_STAND_UP_POSITION_A, (uint8_t) (stand_up_position));
	vregs_write(VREGS_STAND_UP_POSITION_B, (uint8_t) (stand_up_position >> 8));
#endif
	return;
}

/* ------- OLD CODE ---------*/

//#include "position.h"
//#include "sequencer.h"
//#include "walk.h"
//#include "step.h"
//static int32_t time; /* The time what the function is called, kept constant during the function call*/
//static int8_t angle_error; /*The error that that should be controlled*/
//static int32_t new_dutycycle;
//static int32_t side_of_zebro;
//static int32_t angular_velocity = 0;
//static struct motion_timestamped_angle last_known_angle = { 0, 0 };
//int32_t motion_update_data(void) {
////TODO we should only do this in certain modes
//
//	uint8_t temp_angle;
//	uint8_t diff_angle;
//	int32_t temp_time;
//	int32_t diff_time;
//
//	temp_angle = position_get_current_index() * MOTION_SEGMENTS_PER_INDEX;
//	temp_time = time_get_time_ms();
//	if (temp_angle != last_known_angle.angle) {
//
//		diff_angle = temp_angle - last_known_angle.angle;
//		if (temp_angle < last_known_angle.angle) {
//			diff_angle += MOTION_SEGMENTS_PER_FULL_CIRCLE;
//		}
//		diff_time = temp_time - last_known_angle.timestamp;
//		if (temp_time < last_known_angle.timestamp) {
//			diff_time += TIME_MAX_TIME_MS;
//			/*
//			 * Time wrap-around
//			 * The virtual fase calculation still needs to be correct
//			 * Therefore the phase state should be updated accordingly state.
//			 */
//			phase = motion_virtual_angle(state.mode, TIME_MAX_TIME_MS);
//			motion_write_state_to_vregs(state);
//		}
//
//		angular_velocity = (diff_angle * MOTION_MILLISECOND_PER_SECOND)
//				/ diff_time;
//
//		/*
//		 * Update the data last_known_angle.
//		 */
//		angle = temp_angle;
//		last_known_angle.timestamp = temp_time;
//
//		data_update_flag = MOTION_FLAG_SET;
//
//	}
//	return temp_time;
//}
/**
 * Look at the drive command, the position data, and the time.
 * Use this data to choose how to actuate the h_bridge.

 int32_t motion_drive_h_bridge_simple() {
 // todo: implement proper function here.

 if (state.mode == 0) {
 h_bridge_disable();
 return 0;
 }

 vregs_write(VREGS_DEBUG_FLAG_6, state.speed);

 int32_t dutycycle;

 dutycycle = (((int32_t) state.speed) * 100) / 255;
 vregs_write(VREGS_DEBUG_FLAG_5, (uint8_t) dutycycle);

 h_bridge_lock_anti_phase(dutycycle);

 return 0;
 }


 *
 * Keeps track of where the motor should be.
 * For now this makes the motor turn at a constant speed

 int32_t motion_virtual_angle(int32_t motion_mode, int32_t time) {
 int32_t virtual_angle;
 int32_t side;
 side = address_get_side();
 Check on what side of the Zebro the motor is.
 if (side == ADDRESS_RIGHT) {
 Implement kinmatic equation:
 * Thetha(t)=Omega*t+Thetha(0)
 virtual_angle = ((state.speed * MOTION_SEGMENTS_PER_SECOND_PER_RPM)
 * time / MOTION_MILLISECOND_PER_SECOND + state.phase)
 % MOTION_SEGMENTS_PER_FULL_CIRCLE;
 vregs_write(VREGS_MOTION_VIRTUAL_ANGLE, virtual_angle);
 return (virtual_angle);
 } else {
 Implement kinmatic equation but motion is in reversed direction
 * Thetha(t)=Omega*t+Thetha(0)
 virtual_angle = ((state.speed * -1 * MOTION_SEGMENTS_PER_SECOND_PER_RPM)
 * time / MOTION_MILLISECOND_PER_SECOND
 + (MOTION_SEGMENTS_PER_FULL_CIRCLE - state.phase))
 % MOTION_SEGMENTS_PER_FULL_CIRCLE;
 return (virtual_angle);
 }
 }

 *
 * This function estimates where the leg is now
 * By again calculation the kinematic equations

 int32_t motion_estimated_currect_angle(int32_t time) {
 int32_t diff_time;
 diff_time = time - last_known_angle.timestamp;
 Temporal wrap-around
 if (time < last_known_angle.timestamp) {
 diff_time += TIME_MAX_TIME_MS;
 }
 //return (last_known_angle.angle+((diff_time*angular_velocity)/MOTION_SECOND_PER_MILLISECOND))%MOTION_FULL_CIRCLE;
 return (last_known_angle.angle);
 }
 *
 * Determine the error from the angle where we want to be.
 * Positive angle implies the motor should run slower
 * Negative angle implies the angle should run faster
 * The leg is circular therefore:
 *  both positive and negative angles exist, the smallest is chosen
 *  the function has to deal with wrap-around

 int32_t motion_angle_error(int32_t time) {
 int32_t virtual_angle, estimated_angle;
 int32_t angle_error, angle_error_positive, angle_error_negative;

 virtual_angle = motion_virtual_angle(state.mode, time);
 estimated_angle = motion_estimated_currect_angle(time);

 if (virtual_angle == estimated_angle) {
 angle_error = 0;
 } else if (virtual_angle < estimated_angle) {
 angle_error_positive = estimated_angle - virtual_angle;
 angle_error_negative = angle_error_positive
 - MOTION_SEGMENTS_PER_FULL_CIRCLE;
 if (angle_error_positive < MOTION_SEGMENTS_PER_HALF_CIRCLE) {
 angle_error = angle_error_positive;
 } else {
 angle_error = angle_error_negative;
 }
 } else { virtual_angle>estimated_angle
 angle_error_negative = estimated_angle - virtual_angle;
 angle_error_positive = angle_error_negative
 + MOTION_SEGMENTS_PER_FULL_CIRCLE;
 if (angle_error_positive < MOTION_SEGMENTS_PER_HALF_CIRCLE) {
 angle_error = angle_error_positive;
 } else {
 angle_error = angle_error_negative;
 }
 }
 return angle_error;
 }

 *
 * Go to the hall sensor that is closest to the given angle.
 * Uses round down of the angle to find the hall sensor.
 * Just rotates until the hall sensor is passed, then abruptly stops.

 int32_t motion_go_to_position_dumb(void) {
 int32_t position_index;

 position_index = state.phase / 10;
 if (position_index >= POSITION_NUM_OF_POSITIONS)
 return -1;
 if (position_index < 0)
 return -1;

 vregs_write(VREGS_DEBUG_FLAG_1, position_index);

 if (position_get_current_index() == position_index) {
 h_bridge_disable();
 }

 else {
 int32_t dutycycle;

 dutycycle = (((int32_t) state.speed) * 100) / 255;

 h_bridge_lock_anti_phase(dutycycle);
 }

 return 0;
 }

 void motion_turn(void) {
 angle_error = motion_angle_error(time);
 vregs_write(VREGS_MOTION_ANGLE_ERROR, angle_error);
 If the the motor is within the controllable limit use a P-controller to control it
 if (angle_error > -1 * MOTION_CONTROLLABLE_LIMIT
 && angle_error < MOTION_CONTROLLABLE_LIMIT) {
 The motor is only controlled when a new reliable data point is known
 if (data_update_flag == MOTION_FLAG_SET) {
 This is the implementation of the controller
 * For now this is a P controller
 new_dutycycle = MOTION_NEUTRAL_DUTYCYCLE
 - (MOTION_CONTROLLER_P * angle_error >> 2);

 Motor is not allowed to turn in the wrong direction
 if (((side_of_zebro == ADDRESS_RIGHT) && (state.speed > 0)
 && (new_dutycycle < MOTION_NEUTRAL_DUTYCYCLE))
 || ((side_of_zebro == ADDRESS_RIGHT) && (state.speed < 0)
 && (new_dutycycle > MOTION_NEUTRAL_DUTYCYCLE))
 || ((side_of_zebro == ADDRESS_LEFT) && (state.speed < 0)
 && (new_dutycycle < MOTION_NEUTRAL_DUTYCYCLE))
 || ((side_of_zebro == ADDRESS_LEFT) && (state.speed > 0)
 && (new_dutycycle > MOTION_NEUTRAL_DUTYCYCLE))) {
 new_dutycycle = MOTION_NEUTRAL_DUTYCYCLE;

 }
 Dutycycle has limits
 if (new_dutycycle < 2) {
 new_dutycycle = 2;
 }

 else if (new_dutycycle > 98) {
 new_dutycycle = 98;
 }
 For these dutycycles the motor has to little torque to move
 * therefore
 else if (new_dutycycle > 47 && new_dutycycle < 53) {
 new_dutycycle = 50;
 }

 Update dutycycle and actuate the motor
 dutycycle = new_dutycycle;
 h_bridge_lock_anti_phase(dutycycle);

 When the motor is still waiting to get started in the right direction
 * the flag should not be cleared
 if ((state.speed == 0)
 || (new_dutycycle != MOTION_NEUTRAL_DUTYCYCLE)) {
 data_update_flag = MOTION_FLAG_CLEAR;
 }

 }
 }
 If the motor is outside the controllable limit it idles until it is
 else {
 h_bridge_lock_anti_phase(MOTION_NEUTRAL_DUTYCYCLE);
 dutycycle = MOTION_NEUTRAL_DUTYCYCLE;
 data_update_flag = MOTION_FLAG_SET;
 }
 }*/
