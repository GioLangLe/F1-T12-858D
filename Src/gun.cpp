/*
 * gun.cpp
 *
 *  Created on: 14 ���. 2019 �.
 *      Author: Alex
 */

#include "gun.h"

void HOTGUN_HW::init(void) {
	c_fan.init(sw_avg_len,		fan_off_value,	fan_on_value);
	sw_gun.init(sw_avg_len,		sw_off_value, 	sw_on_value);
	sw_mode.init(sw_avg_len,	sw_off_value, 	sw_on_value);
}

void HOTGUN_HW::checkSWStatus(void) {
	if (HAL_GetTick() > check_sw) {
		uint16_t on = 0;
		if (GPIO_PIN_RESET == HAL_GPIO_ReadPin(GUN_REED_GPIO_Port, GUN_REED_Pin))	on = 100;
		sw_gun.update(on);
		on = 0;
		if (GPIO_PIN_RESET == HAL_GPIO_ReadPin(MODE_SW_GPIO_Port, MODE_SW_Pin))		on = 100;
		sw_mode.update(on);
	}
}

void HOTGUN::init(void) {
	mode		= POWER_OFF;								// Completely stopped, no power on fan also
	fan_speed	= 0;
	fix_power	= 0;
	chill		= false;
	HOTGUN_HW::init();
    h_power.reset();
	h_temp.reset();
	d_power.length(ec);
	PID::init(13);											// Initialize PID for Hot Air Gun
    resetPID();
}

uint8_t HOTGUN::avgPowerPcnt(void) {
	uint8_t pcnt = 0;
	if (mode == POWER_FIXED) {
//		pcnt = map(h_power.read(), 0, max_fix_power, 0, 100);
		pcnt = map(fix_power, 0, max_fix_power, 0, 100);
	} else {
		pcnt = map(h_power.read(), 0, max_power, 0, 100);
	}
	if (pcnt > 100) pcnt = 100;
	return pcnt;
}

uint16_t HOTGUN::appliedPower(void) {
	return TIM1->CCR4;
}

uint16_t HOTGUN::fanSpeed(void) {
	return constrain(TIM2->CCR2, 0, 1999);
}

void HOTGUN::switchPower(bool On) {
	switch (mode) {
		case POWER_OFF:
			if (fanSpeed() == 0) {
				if (On)										// !FAN && On
					mode = POWER_ON;
			} else {
				if (On) {
					if (isGunConnected()) {					// FAN && On && connected
						mode = POWER_ON;
					} else {								// FAN && On && !connected
						shutdown();
					}
				} else {
					if (isGunConnected()) {					// FAN && !On && connected
						if (isCold()) {						// FAN && !On && connected && cold
							shutdown();
						} else {							// FAN && !On && connected && !cold
							mode = POWER_COOLING;
						}
					}
				}
			}
			break;
		case POWER_ON:
			if (!On) {
				mode = POWER_COOLING;
			}
			break;
		case POWER_FIXED:
			if (fanSpeed()) {
				if (On) {									// FAN && On
					mode = POWER_ON;
				} else {									// FAN && !On
					if (isGunConnected()) {					// FAN && !On && connected
						if (isCold()) {						// FAN && !On && connected && cold
							shutdown();
						} else {							// FAN && !On && connected && !cold
							mode = POWER_COOLING;
						}
					}
				}
			} else {										// !FAN
				if (!On) {									// !FAN && !On
					shutdown();
				}
			}
			break;
		case POWER_COOLING:
			if (fanSpeed()) {
				if (On) {									// FAN && On
					if (isGunConnected()) {					// FAN && On && connected
						mode = POWER_ON;
					} else {								// FAN && On && !connected
						shutdown();
					}
				} else {									// FAN && !On
					if (isGunConnected()) {
						if (isCold()) {						// FAN && !On && connected && cold
							shutdown();
						}
					} else {								// FAN && !On && !connected
						shutdown();
					}
				}
			} else {
				if (On) {									// !FAN && On
					mode = POWER_ON;
				}
			}
	}
	h_power.reset();
	d_power.reset();
}

void HOTGUN::fixPower(uint8_t Power) {
    if (Power == 0) {										// To switch off the hot gun, set the Power to 0
        switchPower(false);
        return;
    }

    if (Power > max_power) Power = max_power;
    mode = POWER_FIXED;
    fix_power	= Power;
}

uint16_t HOTGUN::power(void) {
	uint16_t t = h_temp.read();								// Actual Hot Air Gun temperature

	if ((t >= int_temp_max + 100) || (t > (temp_set + 400))) {	// Prevent global over heating
		if (mode == POWER_ON) chill = true;					// Turn off the power in main working mode only;
	}

	int32_t		p = 0;
	switch (mode) {
		case POWER_OFF:
			break;
		case POWER_ON:
			TIM2->CCR2	= fan_speed;
			if (chill) {
				if (t < (temp_set - 2)) {
					chill = false;
					resetPID();
				} else {
					break;
				}
			}
			p = PID::reqPower(temp_set, t);
			p = constrain(p, 0, max_power);
			break;
		case POWER_FIXED:
			p			= fix_power;
			TIM2->CCR2	= fan_speed;
			break;
		case POWER_COOLING:
			if (TIM2->CCR2 < min_fan_speed) {
				shutdown();
			} else {
				if (isGunConnected()) {
					if (isCold()) {							// FAN && connected && cold
						shutdown();
					} else {								// FAN && connected && !cold
						uint16_t fan = map(h_temp.read(), temp_gun_cold, temp_set, max_cool_fan, min_fan_speed);
						fan = constrain(fan, min_fan_speed, max_fan_speed);
						TIM2->CCR2 = fan;
					}
				} else {									// FAN && !connected
					shutdown();
				}
			}
			break;
		default:
			break;
	}

	// Only supply the power if the Hot Air Gun is connected
	if (p > 0 && (TIM2->CCR2 < min_fan_speed || !isGunConnected())) p = 0;
	h_power.update(p);
	int32_t	ap	= h_power.average(p);
	int32_t	diff 	= ap - p;
	d_power.update(diff*diff);
	return p;
}

uint8_t	HOTGUN::presetFanPcnt(void) {
	uint16_t pcnt = map(fan_speed, 0, max_fan_speed, 0, 100);
	if (pcnt > 100) pcnt = 100;
	return pcnt;

}
