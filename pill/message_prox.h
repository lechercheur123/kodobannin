#ifndef MESSAGE_PROX_H
#define MESSAGE_PROX_H
#include "message_base.h"

typedef enum{
	PROX_PING = 0,
	PROX_READ,
	PROX_CALIBRATE,
}MSG_ProxAddress;

MSG_Base_t * MSG_Prox_Init(const MSG_Central_t * central);

#endif
