// vi:noet:sw=4 ts=4

/* Copyright (c) 2013 Hello Inc. All Rights Reserved. */

#pragma once

#include <stdint.h>
#include <ble_srv_common.h>
#include <ble.h>

#include "hlo_ble.h"

#define BLE_UUID_HELLO_DEMO_SVC 0x1337
#define BLE_UUID_DATA_CHAR      0xDA1A
#define BLE_UUID_CONF_CHAR      0xC0FF
#define BLE_UUID_CMD_CHAR       0xBEEF
#define BLE_UUID_GIT_DESCRIPTION_CHAR 0x617D

typedef enum {
	Demo_Config_Standby = 0,
	Demo_Config_Sampling,
	Demo_Config_Calibrating,
	Demo_Config_Enter_DFU,
	Demo_Config_ID_Band,
} Demo_Config;

/**@brief Hello Demo init structure. This contains all possible characteristics
 *        needed for initialization of the service.
 */
typedef struct
{
    hlo_ble_write_handler   data_write_handler;
	hlo_ble_write_handler   mode_write_handler;
	hlo_ble_write_handler   cmd_write_handler;
	hlo_ble_connect_handler conn_handler;
	hlo_ble_connect_handler disconn_handler;
} hlo_ble_demo_init_t;

/**@brief Function for initializing the Hello Demo Service.
 *
 * @details This call allows the application to initialize the Hello Demo service.
 *          It adds the service and characteristics to the database, using the initial
 *          values supplied through the p_init parameter. Characteristics which are not
 *          to be added, shall be set to NULL in p_init.
 *
 * @param[in]   p_init   The structure containing the values of characteristics needed by the
 *                       service.
 *
 * @return      NRF_SUCCESS on successful initialization of service.
 */
void hlo_ble_demo_init(const hlo_ble_demo_init_t * p_init);

void hlo_ble_demo_on_ble_evt(ble_evt_t *event);
uint16_t hlo_ble_demo_data_send(const uint8_t * data, const uint16_t data_len);
uint32_t hlo_ble_demo_data_send_blocking(const uint8_t *data, const uint16_t len);

uint16_t hlo_ble_demo_get_handle();


void hlo_ble_alpha0_init();
