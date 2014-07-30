#pragma once
/*
 * Here define the modules used
 */

#define MSG_BASE_SHARED_POOL_SIZE 12
#define MSG_BASE_DATA_BUFFER_SIZE 16

#define MSG_BASE_USE_BIG_POOL
#define MSG_BASE_SHARED_POOL_SIZE_BIG 6
#define MSG_BASE_DATA_BUFFER_SIZE_BIG 256

#define MSG_SSPI_TXRX_BUFFER_SIZE 1

typedef enum{
    CENTRAL = 0,
    UART,
    BLE,
    ANT,
    RTC,
    CLI,
    SSPI,
    TIME,
    MOD_END
}MSG_ModuleType;

#define MSG_CENTRAL_MODULE_NUM  (MOD_END)

