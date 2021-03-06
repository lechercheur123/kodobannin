// vi:noet:sw=4 ts=4

#pragma once

#include "spi_nor.h"

#include "imu_data.h"
#include "message_base.h"

struct sensor_data_header;

typedef void(*imu_data_ready_callback_t)(uint16_t fifo_bytes_available);

struct imu_settings {
	uint16_t active_wom_threshold; // in microgravities
    uint16_t inactive_wom_threshold; // in microgravities
    enum imu_hz inactive_sampling_rate;
    enum imu_sensor_set active_sensors;
	enum imu_hz active_sampling_rate;
	enum imu_accel_range accel_range;
    
    imu_data_ready_callback_t data_ready_callback;
    bool is_active;
};

typedef enum{
	IMU_PING = 0,
	IMU_READ_XYZ,
	IMU_SELF_TEST,
	IMU_FORCE_SHAKE,
}MSG_IMUAddress;

/* See README_IMU.md for an introduction to the IMU, and vocabulary
   that you may need to understand the rest of this. */

MSG_Base_t * MSG_IMU_Init(const MSG_Central_t * central);
void imu_get_settings(struct imu_settings* settings);

bool imu_is_active();

void imu_set_data_ready_callback(imu_data_ready_callback_t callback);
uint8_t clear_stuck_count(void);
uint8_t fix_imu_interrupt(void);

void top_of_meas_minute(void);

MSG_Base_t * MSG_IMU_GetBase(void);
