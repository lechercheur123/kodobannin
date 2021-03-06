// vi:noet:sw=4 ts=4

#pragma once

#include <app_error.h>
#include <nrf_soc.h>
#include "message_uart.h"
#include "hello_dfu.h"
#include "app.h"

extern const uint8_t hex[16];

#undef PACKED
#undef UNUSED
#define PACKED __attribute__((packed))
#define UNUSED __attribute__((unused))

#define APP_ASSERT(condition) APP_ERROR_CHECK(!(condition))

#define APP_OK(expr) APP_ERROR_CHECK(expr);
#define BOOL_OK(expr) APP_ASSERT(expr);

#include <simple_uart.h>
#define PRINT_BYTE(a,b) MSG_Uart_PrintByte((const uint8_t*)a,b)
#define PRINT_HEX(a,b) MSG_Uart_PrintHex((const uint8_t*)a,b)
#define PRINT_DEC(a) MSG_Uart_PrintDec((const int*)a)
#define PRINTF(...) MSG_Uart_Printf(__VA_ARGS__)
#define PRINTS(a) MSG_Uart_Prints(a)
#define PRINTC(a) MSG_Uart_Printc(a)
#define SIMPRINT_HEX(a,b) serial_print_hex((uint8_t *)a,b)
#define SIMPRINTS(a) simple_uart_putstring((const uint8_t *)a)
#define SIMPRINTC(a) simple_uart_put(a)

#ifdef VERBOSE_DEBUG
#define DEBUG_HEX(a,b) MSG_Uart_PrintHex((uint8_t*)a,b)
#define DEBUGS(a) MSG_Uart_Prints(a)
#define DEBUGC(a) MSG_Uart_Printc(a)
#else
#define DEBUG_HEX(a,b) {}
#define DEBUGS(a) {}
#define DEBUGC(a) {}
#endif

void debug_print_ticks(const char* const message, uint32_t start_ticks, uint32_t stop_ticks);

#define DEBUG(a,b) {PRINTS(a); PRINT_HEX(&b, sizeof(b)); PRINTC('\r'); PRINTC('\n');}

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define TRACE(...) do { PRINTS(__FILE__ ":" STR(__LINE__) " ("); PRINTS(__func__); PRINTS(")\r\n"); } while (0)

#define GET_UUID_64() (*(uint64_t*)NRF_FICR->DEVICEID)
#define GET_UUID_32() (NRF_FICR->DEVICEID[0] ^ NRF_FICR->DEVICEID[1])
#define GET_UUID_16() ((uint16_t) (GET_UUID_32() & 0xFFFF)^((GET_UUID_32() >> 16) & 0xFFFF))

#define REBOOT_TO_DFU() do{\
								if(NRF_SUCCESS == sd_power_gpregret_set((uint32_t)GPREGRET_FORCE_DFU_ON_BOOT_MASK)){\
									sd_nvic_SystemReset();\
								}\
							}while(0)
#define REBOOT() sd_nvic_SystemReset()
#define REBOOT_WITH_ERROR(err_mask) do{\
								if(NRF_SUCCESS == sd_power_gpregret_set((uint32_t)err_mask)){\
									sd_nvic_SystemReset();\
								}\
							}while(0)

#define AES128_BLOCK_SIZE 16

void serial_print_hex(uint8_t *ptr, uint32_t len);
void serial_print_byte(uint8_t *ptr, uint32_t len);
void binary_to_hex(uint8_t *ptr, uint32_t len, uint8_t* out);
void binary_to_dec(uint8_t *ptr, uint32_t len, uint8_t* out);

/// Sums all the bytes from the start pointer for len bytes modulo 256, and returns 256 minus the result.
uint8_t memsum(void *start, unsigned len);

//nounce is 64bits
uint32_t aes128_ctr_encrypt_inplace(uint8_t * message, uint32_t message_size, const uint8_t * key, const uint8_t * nonce);
uint32_t aes128_ctr_decrypt_inplace(uint8_t * message, uint32_t message_size, const uint8_t * key, const uint8_t * nonce);
const uint8_t * get_aes128_key(void);
int nrf_atoi(char *p);

union int16_bits {
	int16_t value;
	uint8_t bytes[sizeof(int16_t)];
};

union uint16_bits {
	uint16_t value;
	uint8_t bytes[sizeof(uint16_t)];
};

union int32_bits {
	int32_t value;
	uint8_t bytes[sizeof(int32_t)];
};

union uint32_bits {
	uint32_t value;
	uint8_t bytes[sizeof(uint32_t)];
};

union generic_pointer {
	uint8_t* p8;
	uint16_t* p16;
	uint32_t* p32;

	int8_t* pi8;
	int16_t* pi16;
	int32_t* pi32;
};

static inline uint16_t __attribute__((const)) bswap16(uint16_t x)
{
	__asm__ ("rev16 %0, %1" : "=r" (x) : "r" (x));

	return x;
}

static inline uint32_t __attribute__((const)) bswap32(uint32_t x)
{
	__asm__ ("rev %0, %1" : "=r" (x) : "r" (x));

	return x;
}

#undef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#undef MAX
#define MAX(a, b) ((a) < (b) ? (b) : (a))

#undef ABS
#define ABS(a) (a>0?a:-a)
