/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
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

/** @file
 *
 * @defgroup ble_sdk_app_hrs_eval_main main.c
 * @{
 * @ingroup ble_sdk_app_hrs_eval
 * @brief Main file for Heart Rate Service Sample Application for nRF51822 evaluation board
 *
 * This file contains the source code for a sample application using the Heart Rate service
 * (and also Battery and Device Information services) for the nRF51822 evaluation board (PCA10001).
 * This application uses the @ref ble_sdk_lib_conn_params module.
 */

#include <stdint.h>
#include <string.h>

#include <util.h>

#include "platform.h"
#include "battery_config.h"
#include "nordic_common.h"
#include "nrf.h"
#include "app_error.h"
#include "nrf_gpio.h"
#include "nrf51_bitfields.h"
#include "softdevice_handler.h"
#include "battery.h"
#include "gpio_nor.h"
#include "imu.h"

//         Vbat          80% 3.0 2.8 20%
//         Vrgb     4.2  3.6    2.2 1.6       0.0
//  6.30  6.00  5.00  4.00  3.00  2.00  1.00  0.00 -1.00 -2.00
//  1.80  1.64  1.45  1.16  0.87  0.58  0.29  0.00 -0.29 -0.58
//  03FE  03D0  355A  02E4  026F  01F9  0186  0108  0080  0006
//  1.20  1.15  1.00  0.85  0.70  0.55  0.40  0.25  0.10  0.05

// adc 0x02E4  ( 740 x 3950 x 1200 ) / 1023  ==>  3.429  Volts
// adc 0x02E4  ( 740 x 3430 x 1200 ) / 1023  ==>  3.064  Volts
//     0x01DC  ( 476 x 7125 x 1200 ) / 1023  ==>  3.98   Volts  <=== correct

// adc 0x026F  ( 623 x 3950 x 1200 ) / 1023  ==>  2.886  Volts
// adc 0x026F  ( 623 x 3430 x 1200 ) / 1023  ==>  2.506  Volts
//     0x0167  ( 359 x 7125 x 1200 ) / 1023  ==>  3.00   Volts  <=== correct

// adc 0x0235  ( 565 x 3950 x 1200 ) / 1023  ==>  2.617  Volts
// adc 0x0235  ( 565 x 3430 x 1200 ) / 1023  ==>  2.273  Volts
//     0x012C  ( 300 x 7125 x 1200 ) / 1023  ==>  2.50   Volts  <=== correct

// adc 0x01F9  ( 505 x 3950 x 1200 ) / 1023  ==>  2.339  Volts
// adc 0x01F9  ( 505 x 3430 x 1200 ) / 1023  ==>  2.091  Volts
//     0x00F1  ( 241 x 7125 x 1200 ) / 1023  ==>  2.01   Volts  <=== correct

/**@brief Macro to convert the result of ADC conversion in millivolts.
 *
 * @param[in]  ADC_VALUE   ADC result.
 * @retval     Result converted to millivolts.
 */

static adc_t _adc_config_result; // Vbat adc reading (battery reference)
static adc_t _adc_config_offset; // Vrgb adc reading (ground reference)
static adc_t _adc_config_droop; // Vbat adc reading (battery minimum)

// original scaling computation using battery.h #defings for scaling constants w/o offset compensation
//#define ADC_BATTERY_IN_MILLI_VOLTS(ADC_VALUE)   ((ADC_VALUE) * ADC_REF_VOLTAGE_IN_MILLIVOLTS / 1023 * ADC_PRE_SCALING_COMPENSATION)

// first attempt scaling computation suffers from rounding error issue
//#define ADC_BATTERY_IN_MILLI_VOLTS(ADC_VALUE)  (((((ADC_VALUE - 0x010A) * 1200 ) / 1023 ) * 7125 ) / 1000 )

// presently using fixed offset 0x010A emperically derived by observing nRF51422 w/Vrgb at ground
//#define ADC_BATTERY_IN_MILLI_VOLTS(ADC_VALUE)  (((((ADC_VALUE - 0x010A) * 7125 ) / 1023 ) * 12 ) / 10 )

#define ADC_OFFSET 0x010A /* range 0x0108 thru 0x010E have been observed */
#define ADC_BATTERY_IN_MILLI_VOLTS(ADC_VALUE, ADC_OFF)  (((((ADC_VALUE - ADC_OFF) * 7125 ) / 1023 ) * 12 ) / 10 )

// for use with low source resistance (511) versus above for high source resistance (523K||215K)
#define ADC_RESULT_IN_MILLI_VOLTS(ADC_VALUE)  (((ADC_VALUE) * 1200 ) / 1023 )

static uint8_t _adc_config_psel;

uint8_t clear_stuck_count(void);

inline void battery_module_power_off()
{
    if(ADC_ENABLE_ENABLE_Disabled != NRF_ADC->ENABLE)
    {
        NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Disabled;
    }
#ifdef PLATFORM_HAS_VERSION
    _adc_config_psel = 0; // release clearing adc port select
    nrf_gpio_pin_set(VBAT_VER_EN); // negate to deactive Vbat resistor divider

 // gpio_cfg_s0s1_output_connect(VBAT_VER_EN, 1);
 // gpio_cfg_d0s1_output_disconnect(VBAT_VER_EN);  // on: 0

 // gpio_input_disconnect(VMCU_SENSE);
 // gpio_input_disconnect(VBAT_SENSE);
#endif
}

static uint16_t _battery_level_initial; // measured battery voltage reference 
static uint16_t _battery_level_voltage; // measured battery voltage in sequence
static uint8_t _battery_level_percent; // computed estimated remainig capacity

static inline uint8_t _battery_level_in_percent(const uint16_t milli_volts)
{

    if (_battery_level_voltage > 3000) // from 3.0 to 4.0 (6.0) volts is 100 to 120 (160) percent
    { // there are 360 adc counts at 3.0 volts (8.333 mV per adc count)
        _battery_level_percent = 100 + (((_battery_level_voltage - 3000) * 20) / 1000);
    }
    else if (_battery_level_voltage > 2750) // 3.0 to 2.75 volts is 100 to 10 percent
    { // 100  98 95 92 89 86 83 80 77 74 71  3.00 thru 2.91  +(1 2 3)-   initial
      // (70) 68 65 62 59 56 53 50 47 44 41  2.92 thru 2.83  +(2 3 4)-     less
      // (40) 38 35  30adc = 250mv 17 14 11  2.84 thru 2.75  +(3 4 5)-  subsequent
        _battery_level_percent = 10 + (((_battery_level_voltage - 2750) * 90) / 250); // 100 to 10 %
        _battery_level_percent -= 1 + (_battery_level_percent % 3); // 98 95 .. 14 11

        if (_battery_level_percent > 98) _battery_level_percent = 98; else
        if (_battery_level_percent < 10) _battery_level_percent = 10;

        uint8_t r=1; // 2/3/4 if between 70 and 40 percent
        if (_battery_level_percent > 70 ) r--; // 1/2/3
        if (_battery_level_percent < 40 ) r++; // 3/4/5

        if (_adc_config_result > (_adc_config_droop - r)) _battery_level_percent++; // lo IR
        if (_adc_config_result < (_adc_config_droop - r)) _battery_level_percent--; // hi IR
    }
    else // less than 10 %  ( Vbat(initial) > Vdd(1.8V) + 0.5V ) 2.6 V to 2.3 V is 10 to 0 percent
    { // Vbat well above Vdd (1.8 + margin) operating threshold 
        if (_battery_level_initial > _battery_level_voltage) // look for battery voltage continuing to
            _battery_level_initial = _battery_level_voltage - 200; // fall after restart with pessimism

        if (_battery_level_initial > 2600) _battery_level_percent = 9; else
        if (_battery_level_initial < 2200) _battery_level_percent = 0; else
            _battery_level_percent = (((_battery_level_initial - 2200) * 9) / 400);
    } // ensure there is sufficient Vbat in reserve to survive next restart

    return _battery_level_percent;
}

void battery_set_result_cached(adc_t adc_result) // initial battery reading
{
    if (_adc_config_droop == 0) { // intial battery reading after heartbeat packet
        _battery_level_initial = ADC_BATTERY_IN_MILLI_VOLTS((uint32_t) adc_result, _adc_config_offset);
        _adc_config_droop = adc_result; // higher load (first battery reading)
    } else {
        if (adc_result < _adc_config_droop) { // save new lowest droop observed
            _battery_level_initial = ADC_BATTERY_IN_MILLI_VOLTS((uint32_t) adc_result, _adc_config_offset);
            _adc_config_droop = adc_result; // higher load (first battery reading)
        }
    }
}

void battery_set_offset_cached(adc_t adc_result) // adc offset reading
{
    if (_adc_config_droop == 0) { // intial battery reading after heartbeat packet
        if (adc_result > (ADC_OFFSET + 8)) { // 0x010E max observed
            _battery_level_percent = 254; // indicate exception (high offset)
            _adc_config_offset = ADC_OFFSET; // nominal default value
        } else if (adc_result < ADC_OFFSET - 8) { // 0x0108 min observed
            _battery_level_percent = 253; // indicate exception (low offset)
            _adc_config_offset = ADC_OFFSET; // nominal default value
        } else {
            _adc_config_offset = adc_result;
        }
    }
}

uint16_t battery_set_voltage_cached(adc_t adc_result)
{
    uint32_t value;

    _adc_config_result = adc_result; // nominal load (subsequent battery reading)

    if (_adc_config_droop > _adc_config_result) {
        value = _adc_config_droop;
    } else {
        value = _adc_config_result;
    }

    _battery_level_voltage = ADC_BATTERY_IN_MILLI_VOLTS(value, _adc_config_offset);
    if(_battery_level_percent <= 160){ // preserve exception indicator
        _battery_level_in_percent(_battery_level_voltage);
    }
    return _battery_level_voltage;
}

void battery_set_percent_cached(int8_t value)
{
    _battery_level_percent = value; // used to hijack percent to indicate exception
}

uint8_t battery_level_measured(adc_t adc_result, uint16_t adc_count)
{
#ifdef PLATFORM_HAS_BATTERY
    switch (adc_count) { // for each adc reading

            case 1: battery_set_result_cached(adc_result); // save initial Vbat
                    return LDO_VRGB_ADC_INPUT; break; // setup for adc offset

            case 2: battery_set_offset_cached(adc_result); // save input ground reference
                    return LDO_VBAT_ADC_INPUT; break; // setup for internal resistance

            case 3: battery_set_voltage_cached(adc_result); // save subsequent Vbat
                    break; // fall thru to end adc reading sequence
    }
#endif
    battery_module_power_off(); // disable Vbat resistor (523K||215K) divider
    return 0; // indicate no more adc conversions required
}

void battery_update_level() // perform measurement and estimate capacity
{
    _adc_config_droop = 0; // clear battery droop monitor
    if(_battery_level_percent > 160){ // clear exception indicator
        _battery_level_percent = 160;
    }
    clear_stuck_count(); // clear imu int stuck low counter
    battery_measurement_begin(battery_level_measured, 0); // initiate measurement
}

void battery_update_droop() // perform measurement and monitor droop
{
    battery_measurement_begin(battery_level_measured, 0); // initiate monitor
}

static adc_measure_callback_t _adc_measure_callback;

static int8_t _adc_cycle_count;
static uint8_t _adc_config_count;

/* @brief Function for handling the ADC interrupt.
 * @details  This function will fetch the conversion result from the ADC, convert the value into
 *           percentage and send it to peer.
 */

void ADC_IRQHandler(void)
{
    uint16_t adc_count;
    uint8_t next_measure_input = 0; // indicate adc sequence complete

    if (NRF_ADC->EVENTS_END)
    {
        NRF_ADC->EVENTS_END     = 0;
        adc_t adc_result      = NRF_ADC->RESULT;
        NRF_ADC->TASKS_STOP     = 1;

        if(_adc_measure_callback) // provided for IRQ
        {
            if (_adc_cycle_count > 0) { // Vmcu:0, Vbat:1, ... enabled and settled
                adc_count = _adc_cycle_count++; // 1 2 3 ... 63
                next_measure_input = _adc_measure_callback(adc_result, adc_count);

            } else { // still evaluating Vmcu to ensure that
                if (adc_result < 0x0300) { // 0x0259 resistor (523K||523K)
                    _adc_cycle_count = 1; // dividers have settled
#ifdef PLATFORM_HAS_BATTERY
                    next_measure_input = LDO_VBAT_ADC_INPUT;
#endif
                } else { // Vmcu not yet coming off the peg
                    _adc_cycle_count++; // dividers are in process of settling
#ifdef PLATFORM_HAS_BATTERY
                    next_measure_input = LDO_VMCU_ADC_INPUT;
#endif
                }
            }

            if (next_measure_input) // continue adc measurement
            {
              if (_adc_config_count--) { // failsafe
                _adc_config_psel = next_measure_input; // save adc input selection

                NRF_ADC->CONFIG = (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) |
                                  (ADC_CONFIG_INPSEL_AnalogInputNoPrescaling << ADC_CONFIG_INPSEL_Pos) |
                                  (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos)  |
                /* port select */ (next_measure_input << ADC_CONFIG_PSEL_Pos)  |
                                  (ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos);

                NRF_ADC->TASKS_STOP = 0; // Not sure if this is required.
                // Trigger a new ADC sampling, the callback will be called again
                NRF_ADC->TASKS_START = 1; // start adc to make next reading

              }else{
                  // max number of readings limit exceeded
                  // TODO double check with dyke
                  next_measure_input = 0;
              }
            } // no follow on measurement input given
        } // no callback provided
        else
        {
#ifdef PLATFORM_HAS_BATTERY
            if (_adc_config_psel == LDO_VBAT_ADC_INPUT){ // have Vbat reading
                battery_set_voltage_cached(adc_result); // update saved voltage
            }
#endif
        }
    }
    if (next_measure_input == 0){ // end of adc measurement
        battery_module_power_off();
    }
}

uint16_t battery_get_initial_cached(uint8_t mode){
#ifdef PLATFORM_HAS_VERSION
    if (mode) _adc_config_droop = 0;
    return _battery_level_initial;
#else
    return BATTERY_INVALID_MEASUREMENT;
#endif
}

uint16_t battery_get_voltage_cached(){
#ifdef PLATFORM_HAS_VERSION
    return _battery_level_voltage;
#else
    return BATTERY_INVALID_MEASUREMENT;
#endif
}

uint8_t battery_get_percent_cached(){
#ifdef PLATFORM_HAS_VERSION
    return _battery_level_percent;
#else
    return BATTERY_INVALID_MEASUREMENT;
#endif
}

adc_t battery_get_offset_cached(){
#ifdef PLATFORM_HAS_VERSION
    return _adc_config_offset;
#else
    return BATTERY_INVALID_MEASUREMENT;
#endif
}

void battery_init()
{
    _adc_config_psel = 0; // indicate adc released

    _adc_config_result = 0; // Vbat adc reading (battery reference)
    _adc_config_offset = ADC_OFFSET; // adc offset (ground reference)
    _adc_config_droop = 0; // Vbat adc reading (battery minimum)

#ifdef PLATFORM_HAS_VERSION
    nrf_gpio_pin_set(VBAT_VER_EN); // negate open drain pin inactive to
    nrf_gpio_cfg_output(VBAT_VER_EN); // disable Vbat resistor divider
#endif
 // original power_on gpio_config
 // gpio_input_disconnect(VMCU_SENSE);
 // gpio_cfg_s0s1_output_connect(VBAT_VER_EN, 0);
 // nrf_gpio_cfg_input(VBAT_SENSE, NRF_GPIO_PIN_NOPULL);

 // original power_off gpio_config
 // gpio_cfg_s0s1_output_connect(VBAT_VER_EN, 1);
 // gpio_cfg_d0s1_output_disconnect(VBAT_VER_EN);  // on: 0
 // gpio_input_disconnect(VMCU_SENSE);
 // gpio_input_disconnect(VBAT_SENSE);
}

void battery_module_power_on()
{
#ifdef PLATFORM_HAS_VERSION
 // if (_adc_config_psel) {
 //     return 0; // adc busy with prior reading sequence
 // } else {
        nrf_gpio_pin_clear(VBAT_VER_EN); // assert to active Vbat resistor divider

     // gpio_input_disconnect(VMCU_SENSE);
     // gpio_cfg_s0s1_output_connect(VBAT_VER_EN, 0);
     // nrf_gpio_cfg_input(VBAT_SENSE, NRF_GPIO_PIN_NOPULL);
 // }
#endif
}

uint32_t battery_measurement_begin(adc_measure_callback_t callback, uint16_t count)
{
#ifdef PLATFORM_HAS_BATTERY
    uint32_t err_code;

    _adc_measure_callback = callback; // returning next adc input (result, count)

    if (count) { // start with Vbat, presume resistor divider already settled
        _adc_cycle_count = 1; // use adc cycle count to indicate start with Vbat
        _adc_config_psel = LDO_VBAT_ADC_INPUT; // start w/eval Vmcu < 0x3FF
        _adc_config_count = count; // indicate max number of adc cycles
    } else { // confirm Vmcu settled, then Vbat
        battery_module_power_on(); // enable Vbat/Vmcu resistor dividers
        _adc_cycle_count = -3; // use adc cycle count to indicate start with Vmcu
        _adc_config_psel = LDO_VMCU_ADC_INPUT; // start with Vmcu, then Vbat
     // _adc_config_count = 64; // indicate max number of adc cycles
    }

    if (_adc_config_count == 0 ||_adc_config_count > 64){
        _adc_config_count = 64; // indicate max number of adc cycles
    }

    // Configure ADC
    NRF_ADC->INTENSET   = ADC_INTENSET_END_Msk;
    NRF_ADC->CONFIG     = (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos)     |
                        (ADC_CONFIG_INPSEL_AnalogInputNoPrescaling << ADC_CONFIG_INPSEL_Pos)  |
                        (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos)  |
     /* Vbat Sense */  (_adc_config_psel << ADC_CONFIG_PSEL_Pos)    |
                        (ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos);
 // NRF_ADC->INTENSET   = ADC_INTENSET_END_Msk;
    NRF_ADC->EVENTS_END = 0;
    NRF_ADC->ENABLE     = ADC_ENABLE_ENABLE_Enabled;

    // Enable ADC interrupt
    
    err_code = sd_nvic_ClearPendingIRQ(ADC_IRQn);
    APP_ERROR_CHECK(err_code);

    err_code = sd_nvic_SetPriority(ADC_IRQn, NRF_APP_PRIORITY_LOW);
    APP_ERROR_CHECK(err_code);

    err_code = sd_nvic_EnableIRQ(ADC_IRQn);
    APP_ERROR_CHECK(err_code);
    
    NRF_ADC->EVENTS_END  = 0;    // Stop any running conversions.
    NRF_ADC->TASKS_START = 1;

    return err_code;
#else
    return NRF_SUCCESS;
#endif
}

/**
 * @}
 */
