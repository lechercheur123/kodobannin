/* Copyright (c) 2013 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */
 
#include <stdint.h>
#include <stddef.h> 
#include "dfu.h"
#include "dfu_types.h"
#include "nrf.h"
#include "nrf51.h"
#include "nrf51_bitfields.h"
#include "app_util.h"
#include "nrf_sdm.h"
#include "app_error.h"
#include "nrf_error.h"
#include "ble_flash.h"
#include "app_timer.h"
#include "app_error.h"
#include "nordic_common.h"
#include "bootloader.h"
#include "bootloader_types.h"
#include "crc16.h"
#include "pstorage.h"
#include "nrf_gpio.h"

/**@brief States of the DFU state machine. */
typedef enum
{
    DFU_STATE_INIT_ERROR,        /**< State for: dfu_init(...) error. */
    DFU_STATE_IDLE,              /**< State for: idle. */
    DFU_STATE_RDY,               /**< State for: ready. */
    DFU_STATE_RX_INIT_PKT,       /**< State for: receiving initialization packet. */
    DFU_STATE_RX_DATA_PKT,       /**< State for: receiving data packet. */
    DFU_STATE_VALIDATE,          /**< State for: validate. */
    DFU_STATE_WAIT_4_ACTIVATE    /**< State for: waiting for dfu_image_activate(). */
} dfu_state_t;

static dfu_state_t             m_dfu_state;                /**< Current DFU state. */
static uint32_t                m_image_size;               /**< Size of the image that will be transmitted. */

static uint32_t                m_init_packet[16];          /**< Init packet, can hold CRC, Hash, Signed Hash and similar, for image validation, integrety check and authorization checking. */ 
static uint8_t                 m_init_packet_length;       /**< Length of init packet received. */
static uint16_t                m_image_crc;                /**< Calculated CRC of the image received. */
static uint32_t                m_new_app_max_size;         /**< Maximum size allowed for new application image. */
static uint32_t                m_app_data_received;        /**< Amount of received data. */
static app_timer_id_t          m_dfu_timer_id;             /**< Application timer id. */
static bool                    m_dfu_timed_out = false;    /**< Boolean flag value for tracking DFU timer timeout state. */
static pstorage_handle_t       m_storage_handle_swap;
static pstorage_handle_t       m_storage_handle_app;
static pstorage_module_param_t m_storage_module_param;
static dfu_callback_t          m_data_pkt_cb;

#define IMAGE_WRITE_IN_PROGRESS() (m_app_data_received > 0)      /**< Macro for determining is image write in progress. */
#define APP_TIMER_PRESCALER       0                                                                 /**< Value of the RTC1 PRESCALER register. */
#define DFU_TIMEOUT_INTERVAL      APP_TIMER_TICKS(60000, APP_TIMER_PRESCALER)                       /**< DFU timeout interval in units of timer ticks. */             


static void pstorage_callback_handler(pstorage_handle_t * handle, uint8_t op_code, uint32_t result, uint8_t * p_data, uint32_t data_len)
{
    if ((m_dfu_state == DFU_STATE_RX_DATA_PKT) && 
        (op_code == PSTORAGE_STORE_OP_CODE)    && 
        (result == NRF_SUCCESS))
    {
        if (m_data_pkt_cb != NULL)
        {
            m_data_pkt_cb(result, p_data);
        }
    }
    APP_ERROR_CHECK(result);
}


/**@brief Function for handling the DFU timeout.
 *
 * @param[in] p_context The timeout context.
 */
static void dfu_timeout_handler(void * p_context)
{
    UNUSED_PARAMETER(p_context);
    dfu_update_status_t update_status;
    
    m_dfu_timed_out           = true;
    update_status.status_code = DFU_TIMEOUT;

    bootloader_dfu_update_process(update_status);
}


/**@brief   Function for restarting the DFU Timer.
*
 * @details This function will stop and restart the DFU timer. This function will be called by the 
 *          functions handling any DFU packet received from the peer that is transferring a firmware 
 *          image.
 */
static uint32_t dfu_timer_restart(void)
{
    if (m_dfu_timed_out)
    {
        // The DFU timer had already timed out.
        return NRF_ERROR_INVALID_STATE;
    }

    uint32_t err_code = app_timer_stop(m_dfu_timer_id);
    APP_ERROR_CHECK(err_code);

    err_code = app_timer_start(m_dfu_timer_id, DFU_TIMEOUT_INTERVAL, NULL);
    APP_ERROR_CHECK(err_code);    
    
    return err_code;
}


uint32_t dfu_init(void)
{
    uint32_t              err_code;
    bootloader_settings_t bootloader_settings;
    dfu_update_status_t   update_status;
    
    m_storage_module_param.cb          = pstorage_callback_handler;
    
    // Clear swap area.
    uint32_t * p_bank_start_address = (uint32_t *)DFU_BANK_1_REGION_START; 
    
    m_init_packet_length  = 0;
    m_image_crc           = 0;    
           
    err_code = pstorage_raw_register(&m_storage_module_param, &m_storage_handle_app);
    if (err_code != NRF_SUCCESS)
    {
        m_dfu_state = DFU_STATE_INIT_ERROR;
        return err_code;
    }
    
    m_storage_handle_app.block_id   = CODE_REGION_1_START;
    m_storage_handle_swap           = m_storage_handle_app;
    
    bootloader_settings_get(&bootloader_settings);
    if ((bootloader_settings.bank_1 != BANK_ERASED) || (*p_bank_start_address != EMPTY_FLASH_MASK))
    {
        err_code = pstorage_raw_clear(&m_storage_handle_swap, DFU_IMAGE_MAX_SIZE_FULL);
        if (err_code != NRF_SUCCESS)
        {
            m_dfu_state = DFU_STATE_INIT_ERROR;
            return err_code;
        }
        
        update_status.status_code = DFU_BANK_1_ERASED;
        bootloader_dfu_update_process(update_status);
    }
    
    // Create the timer to monitor the activity by the peer doing the firmware update.
    err_code = app_timer_create(&m_dfu_timer_id,
                                APP_TIMER_MODE_SINGLE_SHOT,
                                dfu_timeout_handler);
    APP_ERROR_CHECK(err_code);

    // Start the DFU timer.
    err_code = app_timer_start(m_dfu_timer_id, DFU_TIMEOUT_INTERVAL, NULL);
    APP_ERROR_CHECK(err_code);

    // Size which indicates how large application DFU are able to handle.
    // The area is not erased but has been locked by the running application, and is considered
    // to be application data save space.
    m_new_app_max_size = DFU_IMAGE_MAX_SIZE_FULL;
    
    m_app_data_received = 0;
    m_dfu_state         = DFU_STATE_IDLE;        

    return NRF_SUCCESS;
}


void dfu_register_callback(dfu_callback_t callback_handler)
{
    m_data_pkt_cb = callback_handler;
}


uint32_t dfu_image_size_set(uint32_t image_size)
{
    uint32_t err_code;
    
    if (image_size > m_new_app_max_size)
    {
        return NRF_ERROR_DATA_SIZE;
    }

    if ((image_size & (sizeof(uint32_t) - 1)) != 0)
    {
        // Image_size is not a multiple of 4 (word size).
        return NRF_ERROR_NOT_SUPPORTED;
    }
    
    switch (m_dfu_state)
    {
        case DFU_STATE_IDLE:    
            // Valid peer activity detected. Hence restart the DFU timer.
            err_code = dfu_timer_restart();
            if (err_code != NRF_SUCCESS)
            {
                return err_code;
            }        
            
            m_image_size = image_size;
            m_dfu_state  = DFU_STATE_RDY;    
            break;
            
        default:
            err_code = NRF_ERROR_INVALID_STATE;
            break;
    }
    
    return err_code;    
}


uint32_t dfu_data_pkt_handle(dfu_update_packet_t * p_packet)
{
    uint32_t   data_length;
    uint32_t   err_code;
    uint32_t * p_data;

    if (p_packet == NULL)
    {
        return NRF_ERROR_NULL;
    }

    // Check pointer alignment.
    if(((uint32_t) (p_packet->p_data_packet)) & (sizeof(uint32_t) - 1))
    {
        // The p_data_packet is not word aligned address.
        return NRF_ERROR_INVALID_ADDR;
    }

    switch (m_dfu_state)
    {
        case DFU_STATE_RDY:
        case DFU_STATE_RX_INIT_PKT:
            m_dfu_state = DFU_STATE_RX_DATA_PKT;
            // fall-through.

        case DFU_STATE_RX_DATA_PKT:
            data_length = p_packet->packet_length * sizeof(uint32_t);

            if ((m_app_data_received + data_length) > m_image_size)
            {
                // The caller is trying to write more bytes into the flash than the size provided to 
                // the dfu_image_size_set function. This is treated as a serious error condition and 
                // an unrecoverable one. Hence point the variable mp_app_write_address to the top of 
                // the flash area. This will ensure that all future application data packet writes 
                // will be blocked because of the above check.
                m_app_data_received = 0xFFFFFFFF;;

                return NRF_ERROR_DATA_SIZE;
            }
            
            // Valid peer activity detected. Hence restart the DFU timer.
            err_code = dfu_timer_restart();
            if (err_code != NRF_SUCCESS)
            {
                return err_code;
            }

            p_data = (uint32_t *)p_packet->p_data_packet;

            err_code = pstorage_raw_store(&m_storage_handle_swap, (uint8_t*) p_data, data_length, m_app_data_received);
            if (err_code != NRF_SUCCESS)
            {
                return err_code;
            }
            
            m_app_data_received += data_length;

            if (m_app_data_received != m_image_size)
            {
                // The entire image is not received yet. More data is expected.
                err_code = NRF_ERROR_INVALID_LENGTH;
            }
            else
            {
                // The entire image has been received. Return NRF_SUCCESS.
                err_code = NRF_SUCCESS;
            }
            break;

        default:
            err_code = NRF_ERROR_INVALID_STATE;
            break;            
    }     

    return err_code;    
}


uint32_t dfu_init_pkt_handle(dfu_update_packet_t * p_packet)
{
    uint32_t err_code;
    uint32_t i;

    switch (m_dfu_state)
    {
        case DFU_STATE_RDY:
            m_dfu_state = DFU_STATE_RX_INIT_PKT;
            /* fall-through */         
        
        case DFU_STATE_RX_INIT_PKT:
            // DFU initialization has been done and a start packet has been received.
            if (IMAGE_WRITE_IN_PROGRESS())
            {
                // Image write is already in progress. Cannot handle an init packet now.
                return NRF_ERROR_INVALID_STATE;
            }
                    
            // Valid peer activity detected. Hence restart the DFU timer.
            err_code = dfu_timer_restart();
            if (err_code != NRF_SUCCESS)
            {
                return err_code;
            }
            
            for (i = 0; i < p_packet->packet_length; i++)
            {
                m_init_packet[m_init_packet_length++] = p_packet->p_data_packet[i];
            }            
            break;
            
        default:
            // Either the start packet was not received or dfu_init function was not called before.
            err_code = NRF_ERROR_INVALID_STATE;        
            break;            
    }
    
    return err_code;       
}


uint32_t dfu_image_validate()
{
    uint32_t err_code;
    uint16_t received_crc;    
    
    switch (m_dfu_state)
    {
        case DFU_STATE_RX_DATA_PKT:
            m_dfu_state = DFU_STATE_VALIDATE;
            
            // Check if the application image write has finished.
            if (m_app_data_received != m_image_size)
            {
                // Image not yet fully transfered by the peer or the peer has attempted to write
                // too much data. Hence the validation should fail.
                err_code = NRF_ERROR_INVALID_STATE;
            }
            else
            {                        
                // Valid peer activity detected. Hence restart the DFU timer.
                err_code = dfu_timer_restart();
                if (err_code == NRF_SUCCESS)
                {                    
                    // calculate CRC from DFU_BANK_1_REGION_START to mp_app_write_address.
                    m_image_crc  = crc16_compute((uint8_t*)DFU_BANK_0_REGION_START,
                                                 m_image_size, 
                                                 NULL);
                    received_crc = uint16_decode((uint8_t*)&m_init_packet[0]);
                    
                    if ((m_init_packet_length != 0) && (m_image_crc != received_crc))
                    {
                        return NRF_ERROR_INVALID_DATA;
                    }                    
                    
                    m_dfu_state = DFU_STATE_WAIT_4_ACTIVATE;                                                                                
                }
            }
            break;
            
        default:
            err_code = NRF_ERROR_INVALID_STATE;
            break;              
    }
    
    return err_code;        
}


uint32_t dfu_image_activate()
{
    uint32_t            err_code;
    
    switch (m_dfu_state)
    {    
        case DFU_STATE_WAIT_4_ACTIVATE:
            
            // Stop the DFU Timer because the peer activity need not be monitored any longer.
            err_code = app_timer_stop(m_dfu_timer_id);
            APP_ERROR_CHECK(err_code);
            
            // Erase BANK 0.
            /*
             *err_code = pstorage_raw_clear(&m_storage_handle_app, m_image_size);
             *APP_ERROR_CHECK(err_code);
             */

            //err_code = pstorage_raw_store(&m_storage_handle_app, (uint8_t*) m_storage_handle_swap.block_id, m_image_size, 0);

            if (err_code == NRF_SUCCESS)
            {
                dfu_update_status_t update_status;

                update_status.status_code = DFU_UPDATE_COMPLETE;
                update_status.app_crc     = m_image_crc;                
                update_status.app_size    = m_image_size;

                bootloader_dfu_update_process(update_status);        
            }
            break;
            
        default:
            err_code = NRF_ERROR_INVALID_STATE;
            break;
    }
    
    return err_code;    
}


void dfu_reset(void)
{
    dfu_update_status_t update_status;
    
    update_status.status_code = DFU_RESET;

    bootloader_dfu_update_process(update_status);
}
