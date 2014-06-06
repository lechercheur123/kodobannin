// vi:noet:sw=4 ts=4

#include <stdlib.h>
#include <string.h>

#include <app_timer.h>
#include <ble.h>
#include <ble_advdata.h>
#include <ble_conn_params.h>
#include <ble_hci.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>
#include <pstorage.h>
#include <softdevice_handler.h>

#include "app.h"
#include "hble.h"
#include "util.h"

static hble_evt_handler_t _user_ble_evt_handler;
static uint16_t _connection_handle = BLE_CONN_HANDLE_INVALID;
static ble_gap_sec_params_t _sec_params;

static void
_on_ble_evt(ble_evt_t* ble_evt)
{
    static ble_gap_evt_auth_status_t _auth_status;

    switch(ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        _connection_handle = ble_evt->evt.gap_evt.conn_handle;
        break;
    case BLE_GAP_EVT_DISCONNECTED:
        _connection_handle = BLE_CONN_HANDLE_INVALID;
        break;
    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
        APP_OK(sd_ble_gap_sec_params_reply(_connection_handle,
                                       BLE_GAP_SEC_STATUS_SUCCESS,
                                       &_sec_params));
        break;
    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
        APP_OK(sd_ble_gatts_sys_attr_set(_connection_handle, NULL, 0));
        break;
    case BLE_GAP_EVT_AUTH_STATUS:
        _auth_status = ble_evt->evt.gap_evt.params.auth_status;
        break;
    case BLE_GAP_EVT_SEC_INFO_REQUEST:
        {
            ble_gap_enc_info_t* p_enc_info = &_auth_status.periph_keys.enc_info;
            if(p_enc_info->div == ble_evt->evt.gap_evt.params.sec_info_request.div) {
                APP_OK(sd_ble_gap_sec_info_reply(_connection_handle, p_enc_info, NULL));
            } else {
                // No keys found for this device
                APP_OK(sd_ble_gap_sec_info_reply(_connection_handle, NULL, NULL));
            }
        }
        break;
    case BLE_GAP_EVT_TIMEOUT:
        if (ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_ADVERTISEMENT) {
            // Go to system-off mode (this function will not return; wakeup will cause a reset)
            APP_OK(sd_power_system_off());
        }
        break;
    default:
        // No implementation needed.
        break;
    }

    if(_user_ble_evt_handler) {
        _user_ble_evt_handler(ble_evt);
    }
}

static void
_on_sys_evt(uint32_t sys_evt)
{
    pstorage_sys_event_handler(sys_evt);
}

static void
_on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    switch(p_evt->evt_type) {
    case BLE_CONN_PARAMS_EVT_FAILED:
        APP_OK(sd_ble_gap_disconnect(_connection_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE));
        break;
    default:
        break;
    }
}

static void
_on_conn_params_error(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

void hble_advertising_start()
{
    ble_gap_adv_params_t adv_params = {};
    adv_params.type = BLE_GAP_ADV_TYPE_ADV_IND;
    adv_params.p_peer_addr = NULL;
    adv_params.fp = BLE_GAP_ADV_FP_ANY;
    adv_params.interval = APP_ADV_INTERVAL;
    adv_params.timeout = APP_ADV_TIMEOUT_IN_SECONDS;

    APP_OK(sd_ble_gap_adv_start(&adv_params));
}

void
hble_init(nrf_clock_lfclksrc_t clock_source, bool use_scheduler, char* device_name, hble_evt_handler_t ble_evt_handler)
{
    _user_ble_evt_handler = ble_evt_handler;

    SOFTDEVICE_HANDLER_INIT(clock_source, use_scheduler);

    APP_OK(softdevice_ble_evt_handler_set(_on_ble_evt));
    APP_OK(softdevice_sys_evt_handler_set(_on_sys_evt));

    // initialize GAP parameters
    {
        ble_gap_conn_sec_mode_t sec_mode;
        BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

        APP_OK(sd_ble_gap_device_name_set(&sec_mode, (char*)device_name, strlen(device_name)));

        ble_gap_conn_params_t gap_conn_params = {};
        gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
        gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
        gap_conn_params.slave_latency     = SLAVE_LATENCY;
        gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

        APP_OK(sd_ble_gap_ppcp_set(&gap_conn_params));
    }

    // initialize advertising parameters
    {
        ble_uuid_t adv_uuids[] = {{BLE_UUID_BATTERY_SERVICE, BLE_UUID_TYPE_BLE}};
        uint8_t flags = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

        ble_advdata_t advdata = {};
        advdata.name_type = BLE_ADVDATA_FULL_NAME;
        advdata.include_appearance = true;
        advdata.flags.size = sizeof(flags);
        advdata.flags.p_data = &flags;
        advdata.uuids_complete.uuid_cnt = sizeof(adv_uuids) / sizeof(adv_uuids[0]);
        advdata.uuids_complete.p_uuids = adv_uuids;

        APP_OK(ble_advdata_set(&advdata, NULL));
    }

    // Connection parameters.
    {
        ble_conn_params_init_t cp_init = {};
        cp_init.p_conn_params                  = NULL;
        cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
        cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
        cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
        cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
        cp_init.disconnect_on_fail             = false;
        cp_init.evt_handler                    = _on_conn_params_evt;
        cp_init.error_handler                  = _on_conn_params_error;

        APP_OK(ble_conn_params_init(&cp_init));
    }

    // Sec params.
    {
        _sec_params.timeout      = SEC_PARAM_TIMEOUT;
        _sec_params.bond         = SEC_PARAM_BOND;
        _sec_params.mitm         = SEC_PARAM_MITM;
        _sec_params.io_caps      = SEC_PARAM_IO_CAPABILITIES;
        _sec_params.oob          = SEC_PARAM_OOB;
        _sec_params.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
        _sec_params.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
    }

}