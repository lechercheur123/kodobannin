#ifndef MESSAGE_PROX_H
#define MESSAGE_PROX_H
#include "message_base.h"

typedef enum{
	PROX_PING = 0,
	PROX_READ,
	PROX_START_CALIBRATE,
	PROX_ERASE_CALIBRATE,
	PROX_READ_REPLY_BLE,
}MSG_ProxAddress;

MSG_Base_t * MSG_Prox_Init(const MSG_Central_t * central);

#endif
