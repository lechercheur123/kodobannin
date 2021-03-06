// vi:noet:sw=4 ts=4

#pragma once

#include "hlo_ble_time.h"
#include "morpheus_gatt.h"

#include "pb_decode.h"
#include "pb_encode.h"
#include "morpheus_ble.pb.h"

#define MSG_NO_MEMORY   "No memory\r\n"

enum {
    BLE_UUID_MORPHEUS_SVC = 0xFEE1,
};

enum morpheus_command_type {
    MORPHEUS_COMMAND_SET_TIME = 0,
    MORPHEUS_COMMAND_GET_TIME,
    MORPHEUS_COMMAND_SET_WIFI_ENDPOINT,
    MORPHEUS_COMMAND_GET_WIFI_ENDPOINT,
    MORPHEUS_COMMAND_SET_ALARMS,
    MORPHEUS_COMMAND_GET_ALARMS,
    MORPHEUS_COMMAND_SWITCH_TO_PAIRING_MODE,
    MORPHEUS_COMMAND_SWITCH_TO_NORMAL_MODE,
    MORPHEUS_COMMAND_START_WIFISCAN,
    MORPHEUS_COMMAND_STOP_WIFISCAN,
    MORPHEUS_COMMAND_GET_DEVICE_ID,
    MORPHEUS_COMMAND_SET_LED,
    
} __attribute__((packed));

struct wifi_endpoint {
    uint8_t name_len;
    uint8_t name[1];  // char pointer
    uint8_t mac[6];
} __attribute__((packed));

struct selected_endpoint {
    uint16_t password_len;
    uint8_t mac[6];
    uint8_t password[1]; // char pointer
} __attribute__((packed));


struct morpheus_command
{
    enum morpheus_command_type command;
    
    union {
        struct hlo_ble_time set_time;
        struct wifi_endpoint scan_result;
        uint8_t device_id[6]; // this is the mac address
    } __attribute__((packed));
} __attribute__((packed));


enum morpheus_data_response_type {
    PILL_DATA_RESPONSE_ACK,
    PILL_DATA_RESPONSE_MISSING,
} __attribute__((packed));


struct morpheus_data_response
{
    enum morpheus_data_response_type type;
    union {
        uint8_t acked_sequence_number;
        struct {
            uint8_t missing_packet_count;
            uint8_t missing_packet_sequence_numbers[18];
        };
    };
} __attribute__((packed));


void morpheus_ble_services_init(void);
void morpheus_ble_evt_handler(ble_evt_t* ble_evt);
void morpheus_ble_write_handler(ble_gatts_evt_write_t* event);
void morpheus_load_modules(void);
void morpheus_ble_transmission_layer_init();
void morpheus_ble_transmission_layer_reset();
bool morpheus_ble_reply_protobuf(MorpheusCommand* morpheus_command);
bool morpheus_ble_reply_protobuf_error(uint32_t error_type);

bool morpheus_ble_decode_protobuf(MorpheusCommand* command, const char* raw, size_t len);
bool morpheus_ble_encode_protobuf(MorpheusCommand* command, char* raw, size_t* len);
void morpheus_ble_remove_decode_funcs(MorpheusCommand* command);
void morpheus_ble_assign_decode_funcs(MorpheusCommand* command);
void morpheus_ble_assign_encode_funcs(MorpheusCommand* command);
void morpheus_ble_free_protobuf(MorpheusCommand* command);
