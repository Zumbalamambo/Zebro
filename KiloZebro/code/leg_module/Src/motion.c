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

#include "stm32f0xx_hal.h"
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
#include "globals.h"

static struct motion_state state = { 0, 0, 0, 0, 0, 0, 0 };
static struct motion_state new_state = { 0, 0, 0, 0, 0, 0, 0 };
uint32_t std_var; /* standard deviation of the hall-sensor with a peak at hall-sensor 3. */
uint16_t position_touch_down = 0;
uint8_t calibrate = 1; /* When Zebro is turned on, calibrate should first be on. */
//static uint16_t touch_down_position = 0;
//static uint16_t stand_up_position = 0;
//static uint16_t lift_off_position = 0;
static int16_t last_known_position = 0;
static uint8_t side_of_zebro = 0;
//static uint32_t increasing_delay = 0;
static uint8_t kp = 120, ki = 150, kd = 30;
/* current setpoint. This is a setpoint between -2^15 and 2^15 */
static int32_t current_setpoint;
static int16_t position_setpoint = 0;
static int16_t absolute_position;

/**
 * Process data send to any of the addresses in the motion control range.
 */
int32_t motion_new_zebrobus_data(uint32_t address, uint8_t data) {

	switch (address) {

	/* set mode */
	case VREGS_MOTION_MODE:
		new_state.mode = data;
		break;

	case VREGS_MOTION_POSITION_A:
		new_state.position_a = data;
		break;

	case VREGS_MOTION_POSITION_B:
		new_state.position_b = data;
		break;

	case VREGS_MOTION_TIME_A:
		new_state.time_a = data;
		break;

	case VREGS_MOTION_TIME_B:
		new_state.time_b = data;
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
		if (!motion_validate_state(new_state)
				&& ((calibrate == 0) || (new_state.mode == 0) /* idle state should always be reached */
				|| (new_state.mode == 1) /* we should always be able to go to calibration state if necessary */
				|| (new_state.mode == 255))) { /* panic state should of course always be reachable */
			state = new_state;
			motion_write_state_to_vregs(state);
		}
		new_state.mode = 0;
		new_state.position_a = 0;
		new_state.position_b = 0;
		new_state.time_a = 0;
		new_state.time_b = 0;
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
	vregs_write(VREGS_MOTION_POSITION_A, (uint8_t) (motion_state.position_a));
	vregs_write(VREGS_MOTION_POSITION_B, motion_state.position_b);
	vregs_write(VREGS_MOTION_TIME_A, motion_state.time_a);
	vregs_write(VREGS_MOTION_TIME_B, motion_state.time_b);
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

void motion_control_position(void) {
	static uint8_t counter = 0;
	static uint16_t dt, time_prev;
	static int16_t error_position, error_position_prev;
	static int32_t error_integral, error_derivative;

	dt = time17_get_time() - time_prev; /* overflow is taken care of by uint16_t and ARR = 0xFFFF. dt is typically */
	time_prev = time17_get_time();
	error_position = position_setpoint - encoder_get_position(); /* there are 910 pulses in one circle, the error will typically be 1/14th of that is our guess = 65 ticks*/
	error_integral += (dt * error_position) / (48000000 >> 15);
	if (counter == 10) {
		error_derivative = ((error_position - error_position_prev)); /* to little accuracy on the encoder for real d. The max value is 2 or 3. */
		error_position_prev = error_position;
		counter = 0;
	} else {
		counter++;
	}
	/* current setpoint. This is a setpoint between -2^15 and 2^15 */
	current_setpoint = ((kp * error_position) + (ki * (error_integral >> 13))
			+ (kd * 40 * error_derivative));

	/* max current_setpoint is INT16_MAX*/
	if (current_setpoint > MOTION_CURRENT_SETPOINT_MAX) {
		current_setpoint = MOTION_CURRENT_SETPOINT_MAX;
		if (((kp * error_position)) < MOTION_CURRENT_SETPOINT_MAX) {
			error_integral = ((((MOTION_CURRENT_SETPOINT_MAX)
					- (kp * error_position)) / ki) << 13);
		} else {
			error_integral = 0;
		}
	} else if (current_setpoint < MOTION_CURRENT_SETPOINT_MIN) {
		current_setpoint = MOTION_CURRENT_SETPOINT_MIN;
		if (((kp * error_position)) > (MOTION_CURRENT_SETPOINT_MIN)) {
			error_integral = ((((MOTION_CURRENT_SETPOINT_MIN)
					- (kp * error_position)) / ki) << 13);
		} else {
			error_integral = 0;
		}
	}

#ifdef DEBUG_VREGS
	vregs_write(VREGS_POSITION_CONTROL_KP, motion_position_control_get_kp());
	vregs_write(VREGS_POSITION_CONTROL_KI, motion_position_control_get_ki());
	vregs_write(VREGS_POSITION_CONTROL_KD, motion_position_control_get_kd());
	if (error_position > 0) {
		vregs_write(VREGS_POSITION_CONTROL_E_POS, (uint8_t) (error_position));
		vregs_write(VREGS_POSITION_CONTROL_E_NEG, (uint8_t) (0));
	} else {
		vregs_write(VREGS_POSITION_CONTROL_E_NEG,
				(uint8_t) (abs(error_position)));
		vregs_write(VREGS_POSITION_CONTROL_E_POS, (uint8_t) (0));
	}
	if (error_integral > 0) {
		vregs_write(VREGS_POSITION_CONTROL_EI_POS,
				(uint8_t) (error_integral >> 15));
		vregs_write(VREGS_POSITION_CONTROL_EI_NEG, (uint8_t) (0));
	} else {
		vregs_write(VREGS_POSITION_CONTROL_EI_NEG,
				(uint8_t) ((abs(error_integral)) >> 15));
		vregs_write(VREGS_POSITION_CONTROL_EI_POS, (uint8_t) (0));
	}
	if (error_derivative > 0) {
		vregs_write(VREGS_POSITION_CONTROL_ED_POS,
				(uint8_t) (error_derivative));
		vregs_write(VREGS_POSITION_CONTROL_ED_NEG, (uint8_t) (0));
	} else {
		vregs_write(VREGS_POSITION_CONTROL_ED_NEG,
				(uint8_t) (abs(error_derivative)));
		vregs_write(VREGS_POSITION_CONTROL_ED_POS, (uint8_t) (0));
	}
	vregs_write(VREGS_POSITION_CONTROL_DT, (uint8_t) (dt / 48));
	vregs_write(VREGS_POSITION_MEASURED, (uint8_t) (encoder_get_position()));
#endif
}

void motion_position_control_set_kp(uint8_t value) {
	kp = value;
	return;
}

void motion_position_control_set_ki(uint8_t value) {
	ki = value;
	return;
}

void motion_position_control_set_kd(uint8_t value) {
	kd = value;
	return;
}

uint8_t motion_position_control_get_kp(void) {
	return kp;
}

uint8_t motion_position_control_get_ki(void) {
	return ki;
}

uint8_t motion_position_control_get_kd(void) {
	return kd;
}

void motion_absolute_position_calculator(void) {
	static int16_t previous_encoder_position;
	int16_t current_position = encoder_get_position();
	int16_t delta = current_position - previous_encoder_position;
	absolute_position = absolute_position + delta;
	if (absolute_position < 0) {
		absolute_position = ENCODER_PULSES_PER_ROTATION + absolute_position;
	}
	absolute_position = absolute_position % ENCODER_PULSES_PER_ROTATION;
	previous_encoder_position = current_position;

#ifdef DEBUG_VREGS
	vregs_write(VREGS_POSITION_ABSOLUTE_A, (uint8_t) (absolute_position >> 8));
	vregs_write(VREGS_POSITION_ABSOLUTE_B, (uint8_t) absolute_position);
#endif
}

void motion_reset_absolute_position_calculator(void) {
	absolute_position = 0;
	return;
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
 * 		5: Calibration completed
 */
int32_t motion_drive_h_bridge() {
	int32_t return_value = 0;
	static uint16_t adc_data[ARRAY_SIZE];
	static uint8_t fsm_flag = 0;
	static uint16_t position[2] = { 0, 0 };
	uint8_t side_of_zebro = address_get_side();

	static uint8_t kp_old;
	static uint8_t ki_old;
	static uint8_t kd_old;

	static uint16_t distance_move = 0;

	static int16_t starting_setpoint = 0;
	static int16_t absolute_starting_setpoint = 0;
	static int16_t absolute_ending_setpoint = 0;
//	static int16_t ending_setpoint = 0;
//	static uint32_t position_ramp_length = 0;
//	uint16_t delta_s = 0;
//	static uint32_t constant_speed = 0;
//	static uint32_t n = 1;
//	uint8_t rounds = 0;

//	static uint32_t touch_down_time = 0;
//	static uint32_t lift_off_time = 0;
	static uint32_t t_move_total = 0;
	static uint32_t t_ad = 0;
	static uint32_t starting_time = 0;
	uint32_t t = 0;

//	uint16_t ending_position_setpoint, delta_s;
//	static uint16_t setpoint = 0;
//	static uint32_t previous_time;
//	int32_t delta_t;

#ifdef DEBUG_VREGS
	vregs_write(VREGS_MOTION_STD_DEV_A, (uint8_t) get_std_var());
	vregs_write(VREGS_MOTION_STD_DEV_B, (uint8_t) (get_std_var() >> 8));
	vregs_write(VREGS_MOTION_FSM_FLAG, (uint8_t) fsm_flag);
	vregs_write(VREGS_DEBUG_FLAG_1, (uint8_t) calibrate);
	vregs_write(VREGS_DEBUG_FLAG_2, (uint8_t) (ADC1->CR & ADC_CR_ADSTART));
	vregs_write(VREGS_MOTION_LAST_KNOWN_POSITION_A,
			(uint8_t) last_known_position >> 8);
	vregs_write(VREGS_MOTION_LAST_KNOWN_POSITION_B,
			(uint8_t) (last_known_position));
//	vregs_write(VREGS_MOTION_CONSTANT_SPEED, (uint8_t) (constant_speed >> 3));
//	vregs_write(VREGS_MOTION_POSITION_RAMP_LENGTH,
//			(uint8_t) (position_ramp_length));
	vregs_write(VREGS_MOTION_POSITION_SETPOINT_A,
			(uint8_t) (position_setpoint >> 8));
	vregs_write(VREGS_MOTION_POSITION_SETPOINT_B,
			(uint8_t) (position_setpoint));
	vregs_write(VREGS_POSITION_DISTANCE_MOVE_A, (uint8_t) (distance_move >> 8));
	vregs_write(VREGS_POSITION_DISTANCE_MOVE_B, (uint8_t) (distance_move));
#endif

	switch (state.mode) {

	case MOTION_MODE_IDLE:
		/* Function stabilizes the leg on the last known position
		 * But: not more than a quarter of an entire circle.
		 */
		fsm_flag = 0;
		position_setpoint = last_known_position;
		return 0;

	case MOTION_MODE_CALIBRATE:
		if (fsm_flag == 0) {
			kp_old = kp;
			ki_old = ki;
			kd_old = kd;
			kp = 30;
			ki = 38;
			kd = 7;
			/* Set calibrate to 1 to enable reading adc-data of the hall-sensors */
			set_calibrate(1);
			/* Set AD_STOP to 1 to stop measuring on the ADC. */
			ADC1->CR |= ADC_CR_ADSTP;
			/* wait for adstart to become 0 */
			if ((ADC1->CR & ADC_CR_ADSTART) == 0) {
				interrupts_disable();
				DMA1_Channel1->CCR &= ~DMA_CCR_EN;
				/* Select the channels to convert. */
				ADC1->CHSELR = (1 << ADC_HAL_1_CH) | (1 << ADC_HAL_2_CH)
						| (1 << ADC_HAL_3_CH) | (1 << ADC_ID_RESISTOR_CH)
						| (1 << ADC_BATTERY_CH) | (1 << ADC_MOTOR_CURRENT_CH)
						| (1 << ADC_TEMP_CH);
				/* set size of transfer */
				DMA1_Channel1->CNDTR = ADC_NUM_OF_CHANNELS;
				/* enable the DMA channel */
				DMA1_Channel1->CCR |= DMA_CCR_EN;
				/* Write AD_START to 1 to start conversions. */
				ADC1->CR |= ADC_CR_ADSTART;
				fsm_flag = 1;
				interrupts_enable();
			}
		}
		if (fsm_flag == 1) {
			vregs_write(VREGS_DEBUG_FLAG_3,
					(adc_get_absolute_current_measured_mA() >> 8));
			if (adc_get_absolute_current_measured_mA() > MOTION_CALIB_MAX_CURRENT_MA) {
				start_timing_measurement();
				/* wait for position to hold */
				if (stop_and_return_timing_measurement(500)) {
					encoder_reset_position();
					position_setpoint = encoder_get_position();
					fsm_flag = 2;
				}
			} else {
				reset_timing_measurement();
				if (time_get_time_ms() % 10 == 0) {
					position_setpoint -= 1;
				}
			}
		}
		if (fsm_flag == 2) {
			static uint16_t n = 0;
			if (time_get_time_ms() % 10 == 0) { /* We don't want to sample too often */
				adc_data[n] = adc_get_value(ADC_HAL_2_INDEX); /* We're only interested in the value of the 3rd hall sensor */
				n = n + 1;
			}
			if (n >= ARRAY_SIZE) {
				n = ARRAY_SIZE - 1;
			}
			if (position_setpoint >= 400) {
				fsm_flag = 3;
			} else {
				if (time_get_time_ms() % 10 == 0) {
					position_setpoint += 1;
				}
			}
//			vregs_write(VREGS_DEBUG_FLAG_3,
//					(adc_get_absolute_current_measured_mA() >> 8));
//			if (adc_get_absolute_current_measured_mA() > MOTION_CALIB_MAX_CURRENT_MA) {
//				start_timing_measurement();
//				/* wait for position to hold */
//				if (stop_and_return_timing_measurement(500)) {
//					/* Calculate the standard deviation of the collected samples. */
//					std_var = std_var_stable(adc_data, n);
//					position_setpoint = encoder_get_position();
//					fsm_flag = 3;
//				}
//			} else {
//				reset_timing_measurement();
//				if (time_get_time_ms() % 10 == 0) {
//					position_setpoint += 1;
//				}
//			}
		}
		if (fsm_flag == 3) {
			reset_peak_detected();
			fsm_flag = 4;
		}
		if (fsm_flag == 4) {
			if (get_peak_detected(MOTION_CALIBRATION_HALL_SENSOR) == 1) {
				encoder_reset_position();
				last_known_position = encoder_get_position();
				fsm_flag = 5;
			} else {
				if (time_get_time_ms() % 10 == 0) {
					position_setpoint -= 1;
				}
			}
		}
		/* wait for adstart to become 0 */
		if (fsm_flag == 5) {
			kp = kp_old;
			ki = ki_old;
			kd = kd_old;
			if ((ADC1->CR & ADC_CR_ADSTART) != 0) {
				/* Set AD_STOP to 1 to stop measuring on the ADC. */
				ADC1->CR |= ADC_CR_ADSTP;
			}
			if ((ADC1->CR & ADC_CR_ADSTART) == 0) {
				interrupts_disable();
				DMA1_Channel1->CCR &= ~DMA_CCR_EN;
				/* Select the channels to convert. */
				ADC1->CHSELR = (1 << ADC_BATTERY_CH)
						| (1 << ADC_MOTOR_CURRENT_CH) | (1 << ADC_TEMP_CH);
				/* set size of transfer */
				DMA1_Channel1->CNDTR = ADC_NUM_OF_CHANNELS - 4;
				/* enable the DMA channel */
				DMA1_Channel1->CCR |= DMA_CCR_EN;
				/* Write AD_START to 1 to start conversions. */
				ADC1->CR |= ADC_CR_ADSTART;
				set_calibrate(0);
				motion_stop();
				interrupts_enable();
			}
		}
		break;

		/* it is assumed that if we move to lift off, we will also always move to touch down. After that, we'll see what we'll do. */
	case MOTION_MODE_WALK_FORWARD:
		if (fsm_flag == 0) {
			starting_setpoint = encoder_get_position();
			absolute_starting_setpoint = absolute_position;
			absolute_ending_setpoint = ((state.position_a << 8)
					+ state.position_b);
			starting_time = time_get_time_ms();
			if (absolute_starting_setpoint >= absolute_ending_setpoint) {
				distance_move =
						ENCODER_PULSES_PER_ROTATION
								- (absolute_starting_setpoint
										- absolute_ending_setpoint);
			} else {
				distance_move = absolute_ending_setpoint
						- absolute_starting_setpoint;
			}
			t_move_total = ((state.time_a * 1000) + (state.time_b * 4))
					- starting_time;
			t_ad = MOTION_ACC_DEC_TIME_MS;
			fsm_flag = 6;
		}
		if (fsm_flag == 6) {
			t = time_calculate_delta(time_get_time_ms(), starting_time);
			if (t <= t_ad) {
				position_setpoint = (starting_setpoint
						+ ((distance_move * t * t)
								/ (2 * t_ad * (t_move_total - t_ad))));
			}

			if ((t > t_ad) && (t <= (t_move_total - t_ad))) {
				position_setpoint = (starting_setpoint
						+ (((distance_move * t) / (t_move_total - t_ad))
								- ((distance_move * t_ad)
										/ (2 * (t_move_total - t_ad)))));
			}

			if ((t > (t_move_total - t_ad)) && (t <= t_move_total)) {
				position_setpoint = (starting_setpoint
						+ (distance_move
								- ((distance_move
										* ((t_move_total - t)
												* (t_move_total - t)))
										/ (2 * t_ad * (t_move_total - t_ad)))));
			}
			if (t >= t_move_total) {
				last_known_position = encoder_get_position();
				motion_stop();
			}
		}
		break;

	case MOTION_MODE_WALK_BACKWARD:

		break;

	default:
		motion_stop();
		return_value = 255;
	}
	motion_write_state_to_vregs(state);
	return return_value;
}

/* Set the setpoint for current */
void set_current_setpoint(int32_t value) {
	current_setpoint = value;
}

/* Get the setpoint for current */
int32_t get_current_setpoint(void) {
	if (current_setpoint >= INT16_MAX) {
		current_setpoint = INT16_MAX;
	}
	if (current_setpoint <= INT16_MIN) {
		current_setpoint = INT16_MIN;
	}
	return current_setpoint;
}

uint8_t get_state_mode(void) {
	return state.mode;
}

void set_state_mode(uint8_t mode) {
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
	state.position_a = lift_off_time_a;
	state.position_b = lift_off_time_b;
	state.time_a = touch_down_time_a;
	state.time_b = touch_down_time_b;
	state.new_data_flag = new_data_flag;
	state.crc = crc;
	motion_write_state_to_vregs(state);
}

/**
 * Clear the current motion command, and stop
 */
int32_t motion_stop(void) {
	state.mode = 0;
	state.position_a = 0;
	state.position_b = 0;
	state.time_a = 0;
	state.time_b = 0;
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
//	touch_down_position = (!(side_of_zebro) * TOUCH_DOWN_POSITION_L)
//			+ (side_of_zebro * TOUCH_DOWN_POSITION_R);
//	stand_up_position = (!(side_of_zebro) * STAND_UP_POSITION_L)
//			+ (side_of_zebro * STAND_UP_POSITION_R);
//	lift_off_position = (!(side_of_zebro) * LIFT_OFF_POSITION_L)
//			+ (side_of_zebro * LIFT_OFF_POSITION_R);
//#ifdef DEBUG_VREGS
//	vregs_write(VREGS_MOTION_TOUCH_DOWN_POSITION_A,
//			(uint8_t) (touch_down_position));
//	vregs_write(VREGS_MOTION_TOUCH_DOWN_POSITION_B,
//			(uint8_t) (touch_down_position >> 8));
//	vregs_write(VREGS_STAND_UP_POSITION_A, (uint8_t) (stand_up_position));
//	vregs_write(VREGS_STAND_UP_POSITION_B, (uint8_t) (stand_up_position >> 8));
//	vregs_write(VREGS_MOTION_LIFT_OFF_POSITION_A,
//			(uint8_t) (lift_off_position));
//	vregs_write(VREGS_MOTION_LIFT_OFF_POSITION_B,
//			(uint8_t) (lift_off_position >> 8));
//#endif

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
