// vi:noet:sw=4 ts=4
#include "app.h"
#include "platform.h"

#include <app_timer.h>
#include <spi.h>
#include <simple_uart.h>
#include <util.h>

#include <app_error.h>
#include <nrf_delay.h>
#include <nrf_gpio.h>
#include <string.h>
#include <app_gpiote.h>
#include "imu.h"

#include "message_imu.h"
#include "mpu_6500_registers.h"
#include "sensor_data.h"
#include "message_base.h"
#include "timedfifo.h"

#ifdef ANT_STACK_SUPPORT_REQD
#include "message_ant.h"
#endif

#include <watchdog.h>
#include "shake_detect.h"
#include "gpio_nor.h"

#include "message_time.h"


enum {
    IMU_COLLECTION_INTERVAL = 6553, // in timer ticks, so 200ms (0.2*32768)
};

static app_timer_id_t _wom_timer;
static app_gpiote_user_id_t _gpiote_user;
static uint32_t _last_active_time;

static char * name = "IMU";
static bool initialized = false;

static const MSG_Central_t * parent;
static MSG_Base_t base;
static uint8_t stuck_counter;
static uint32_t top_of_minute = 0;
static uint32_t active_time = 0;


static struct imu_settings _settings = {
	.active_wom_threshold = IMU_ACTIVE_WOM,
    .inactive_wom_threshold = IMU_INACTIVE_WOM,
	.inactive_sampling_rate = IMU_INACTIVE_FREQ,  //IMU_HZ_15_63; IMU_HZ_31_25; IMU_HZ_62_50; IMU_HZ_7_81; IMU_HZ_3_91; IMU_HZ_1_95; IMU_HZ_0_98
#ifdef IMU_DYNAMIC_SAMPLING
    .active_sampling_rate = IMU_ACTIVE_FREQ, //IMU_HZ_15_63; IMU_HZ_31_25; IMU_HZ_62_50; IMU_HZ_7_81; IMU_HZ_3_91; IMU_HZ_1_95; IMU_HZ_0_98
#else
    .active_sampling_rate = IMU_CONSTANT_FREQ,
#endif
	.active_sensors = IMU_SENSORS_ACCEL,//|IMU_SENSORS_GYRO,
    .accel_range = IMU_ACCEL_RANGE_2G,
    .data_ready_callback = NULL,
    .wom_callback = NULL,
    .is_active = false,
};


static inline void _reset_accel_range(enum imu_accel_range range)
{
	_settings.accel_range = range;
    imu_set_accel_range(range);
}


void imu_set_wom_callback(imu_wom_callback_t callback)
{
    _settings.wom_callback = callback;
}

inline void imu_get_settings(struct imu_settings *settings)
{
	*settings = _settings;
}



static void _dispatch_motion_data_via_ant(const int16_t* values, size_t len)
{
#ifdef ANT_STACK_SUPPORT_REQD
	/* do not advertise if has at least one bond */
	MSG_Data_t * message_data = MSG_Base_AllocateDataAtomic(len);
	if(message_data){
		memcpy(message_data->buf, values, len);
		parent->dispatch((MSG_Address_t){0, 0},(MSG_Address_t){ANT, 1}, message_data);
		MSG_Base_ReleaseDataAtomic(message_data);
	}
#endif
}

static uint32_t _aggregate_motion_data(const int16_t* raw_xyz, size_t len)
{
    int16_t values[3];
//    uint16_t range;
//    uint16_t maxrange
    //auxillary_data_t * paux;
    memcpy(values, raw_xyz, len);

    //int32_t aggregate = ABS(values[0]) + ABS(values[1]) + ABS(values[2]);
    uint32_t aggregate = values[0] * values[0] + values[1] * values[1] + values[2] * values[2];
	
    /*
    tf_unit_t curr = TF_GetCurrent();

    PRINTS("Current value: ");
    PRINT_HEX(&curr, sizeof(curr));
    PRINTS("\r\n");
    PRINTS("New value: ");
    PRINT_HEX(&aggregate, sizeof(tf_unit_t));
    PRINTS("\r\n");
    */

	//TF_SetCurrent((uint16_t)values[0]);
	tf_unit_t* current = TF_GetCurrent();
    ++current->num_meas;
    if(current->max_amp < aggregate){
        current->max_amp = aggregate;
        PRINTF( "NEW MAX: %d\r\n", aggregate);
    }
    for(int i=0;i<3;++i){
        current->avg_accel[i] += (values[i] - current->avg_accel[i])/current->num_meas;
    }
    return aggregate;
}

static void _imu_switch_mode(bool is_active)
{
    if(is_active)
    {
        imu_set_accel_freq(_settings.active_sampling_rate);
        imu_wom_set_threshold(_settings.active_wom_threshold);
        PRINTS("IMU Active.\r\n");
        app_timer_start(_wom_timer, IMU_ACTIVE_INTERVAL, NULL);
        _settings.is_active = true;
    }else{
        imu_set_accel_freq(_settings.inactive_sampling_rate);
        imu_wom_set_threshold(_settings.inactive_wom_threshold);
        PRINTF( "IMU Inactive\r\n");
        app_timer_stop(_wom_timer);
        _settings.is_active = false;
    }
}

static bool reading = false;
static void _imu_gpiote_process(uint32_t event_pins_low_to_high, uint32_t event_pins_high_to_low)
{
    APP_OK(app_gpiote_user_disable(_gpiote_user));
    if( !reading ) {
        parent->dispatch( (MSG_Address_t){IMU, 0}, (MSG_Address_t){IMU, IMU_READ_XYZ}, NULL);
        reading = true;
    }
    PRINTS("I\r");
}

#define PRINT_HEX_X(x) PRINT_HEX(&x, sizeof(x)); PRINTS("\r\n");

static void _on_wom_timer(void* context)
{
    uint32_t current_time = 0;
    app_timer_cnt_get(&current_time);
    uint32_t time_diff = 0;
    app_timer_cnt_diff_compute(current_time, top_of_minute, &time_diff);
    uint32_t active_time_diff = 0;
    app_timer_cnt_diff_compute(current_time, active_time, &active_time_diff);

    time_diff /= APP_TIMER_TICKS( 1000, APP_TIMER_PRESCALER );
    TF_GetCurrent()->motion_mask |= 1ull<<(time_diff%60);
    PRINTS("mask\r\n");
    
    PRINT_HEX_X( TF_GetCurrent()->motion_mask  );
    
    ShakeDetectDecWindow();

    if(active_time_diff >= IMU_ACTIVE_INTERVAL && _settings.is_active)
    {
        _imu_switch_mode(false);
        
        PRINTF( "time diff %d\r\n", time_diff);
    }
}

uint8_t
clear_stuck_count(void)
{
	uint8_t value = stuck_counter;
	stuck_counter = 0;
	return value;
}

void top_of_meas_minute(void) {
    app_timer_cnt_get(&top_of_minute);
}

uint8_t
fix_imu_interrupt(void){
	uint32_t gpio_pin_state;
	uint8_t value = 0;
	if(initialized){
		if(NRF_SUCCESS == app_gpiote_pins_state_get(_gpiote_user, &gpio_pin_state)){
			if(!(gpio_pin_state & (1<<IMU_INT))){
				parent->dispatch( (MSG_Address_t){IMU, 0}, (MSG_Address_t){IMU, IMU_READ_XYZ}, NULL);
				if (stuck_counter < 15)
				{
					++stuck_counter;
				}
				value = stuck_counter; // talley imu int stuck low
			}else{
			}
		}else{
		}
	}
	return value;
}

static void _on_pill_pairing_guesture_detected(void){
	static uint8_t counter;
    //TODO: send pairing request packets via ANT
#ifdef ANT_ENABLE
    MSG_Data_t* data_page = MSG_Base_AllocateDataAtomic(sizeof(MSG_ANT_PillData_t) + sizeof(pill_shakedata_t));
    if(data_page){
        memset(&data_page->buf, 0, sizeof(data_page->len));
        MSG_ANT_PillData_t* ant_data = (MSG_ANT_PillData_t*)&data_page->buf;
		pill_shakedata_t * shake_data = (pill_shakedata_t*)ant_data->payload;
        ant_data->version = ANT_PROTOCOL_VER;
        ant_data->type = ANT_PILL_SHAKING;
        ant_data->UUID = GET_UUID_64();
		shake_data->counter = counter++;
        parent->dispatch((MSG_Address_t){IMU,1}, (MSG_Address_t){ANT,1}, data_page);
        MSG_Base_ReleaseDataAtomic(data_page);
    }
#endif

    PRINTS("Shake detected\r\n");
}


static MSG_Status _init(void){
	if(!initialized){
        imu_power_on();
#ifdef IMU_DYNAMIC_SAMPLING
		if(!imu_init_low_power(SPI_Channel_1, SPI_Mode0, IMU_SPI_MISO, IMU_SPI_MOSI, IMU_SPI_SCLK, IMU_SPI_nCS, 
			_settings.inactive_sampling_rate, _settings.accel_range, _settings.inactive_wom_threshold))
#else
        if(!imu_init_low_power(SPI_Channel_1, SPI_Mode0, IMU_SPI_MISO, IMU_SPI_MOSI, IMU_SPI_SCLK, IMU_SPI_nCS, 
            _settings.active_sampling_rate, _settings.accel_range, _settings.active_wom_threshold))
#endif
		{
			nrf_gpio_cfg_input(IMU_INT, GPIO_PIN_CNF_PULL_Pullup);

		    imu_clear_interrupt_status();
			APP_OK(app_gpiote_user_enable(_gpiote_user));
			PRINTS("IMU: initialization done.\r\n");
			initialized = true;
        }
        app_timer_cnt_get(&top_of_minute);
	}
    return SUCCESS;
}

static MSG_Status _destroy(void){
	if(initialized){
		initialized = false;
		APP_OK(app_gpiote_user_disable(_gpiote_user));
		imu_clear_interrupt_status();
		gpio_input_disconnect(IMU_INT);
		imu_power_off();
	}
    return SUCCESS;
}

static MSG_Status _flush(void){
    return SUCCESS;
}
static MSG_Status _handle_self_test(void){
	MSG_Status ret = FAIL;
	if( !imu_self_test() ){
		ret = SUCCESS;
	}
	parent->unloadmod(&base);
	parent->loadmod(&base);
	return ret;
}
static MSG_Status _handle_read_xyz(void){
	int16_t values[3];
	uint32_t mag;
	imu_accel_reg_read((uint8_t*)values);
    PRINTS("R\r\n");

    reading = false;
	//uint8_t interrupt_status = imu_clear_interrupt_status();
	if(_settings.wom_callback){
		_settings.wom_callback(values, sizeof(values));
	}
	mag = _aggregate_motion_data(values, sizeof(values));
	ShakeDetect(mag);
#ifdef IMU_DYNAMIC_SAMPLING        
	app_timer_cnt_get(&_last_active_time);
	if(!_settings.is_active)
	{
		_imu_switch_mode(true);
	}

    app_timer_cnt_get(&active_time);
#endif
    APP_OK(app_gpiote_user_enable(_gpiote_user));
    
	return SUCCESS;
}

static MSG_Status _send(MSG_Address_t src, MSG_Address_t dst, MSG_Data_t * data){
	MSG_Status ret = SUCCESS;
	switch(dst.submodule){
		default:
		case IMU_PING:
			PRINTS(name);
			PRINTS("\r\n");
			break;
		case IMU_READ_XYZ:
			ret = _handle_read_xyz();
			imu_clear_interrupt_status();
			break;
		case IMU_SELF_TEST:
			ret = _handle_self_test();
			break;
	}
	return ret;
}


MSG_Base_t * MSG_IMU_Init(const MSG_Central_t * central)
{
	imu_power_off();

	parent = central;
	base.init = _init;
	base.destroy = _destroy;
	base.flush = _flush;
	base.send = _send;
	base.type = IMU;
	base.typestr = name;
#ifdef IMU_DYNAMIC_SAMPLING
	APP_OK(app_timer_create(&_wom_timer, APP_TIMER_MODE_REPEATED, _on_wom_timer));
#endif
	APP_OK(app_gpiote_user_register(&_gpiote_user, 0, 1 << IMU_INT, _imu_gpiote_process));
	APP_OK(app_gpiote_user_disable(_gpiote_user));
    ShakeDetectReset(SHAKING_MOTION_THRESHOLD);
    set_shake_detection_callback(_on_pill_pairing_guesture_detected);

	return &base;

}
MSG_Base_t * MSG_IMU_GetBase(void){
	return &base;
}
