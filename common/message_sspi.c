#include "message_sspi.h"
#include <nrf_gpio.h>
#include "util.h"

#define REG_READ_FROM_SSPI  0
#define REG_WRITE_TO_SSPI 1
#define TEST_STR "RELLO"
typedef enum{
    IDLE = 0,
    READING,
    WRITING,
    ERROR
}SSPIState;

static struct{
    MSG_Base_t base;
    MSG_Central_t * parent;
    spi_slave_config_t config;
    volatile SSPIState current_state;
    uint8_t control_reg;
    /*
     * A transaction is a series of 3 CS pin transitions
     * That may not be interrupted
     */
    struct{
        enum{
            WAIT_READ_RX_CTX = 0,
            WRITE_TX_CTX,
            WAIT_READ_RX_BUF,
            WRITE_TX_BUF,
            FIN_READ,
            FIN_WRITE,
            TXRX_ERROR
        }state;
        struct{
            uint16_t length;
            union{
                MSG_Address_t address;
                uint16_t pad;
            };
        }context_reg PACKED;
        MSG_Data_t * payload;
    }transaction;

    /*
     * Only one queue_tx right now
     */
    MSG_Data_t * queued_tx;
    MSG_Data_t * dummy;
}self;

static char * name = "SSPI";
static void _spi_evt_handler(spi_slave_evt_t event);

static SSPIState
_reset(void){
    PRINTS("RESET\r\nWaiting Input....\r\n");
    spi_slave_buffers_set(&self.control_reg, &self.control_reg, 1, 1);
    return IDLE;
}
static MSG_Data_t *
_dequeue_tx(void){
    MSG_Data_t * ret = self.queued_tx;
#ifdef PLATFORM_HAS_SSPI
    if(SSPI_INT != 0){
        nrf_gpio_cfg_output(SSPI_INT);
        nrf_gpio_pin_write(SSPI_INT, 0);
    }
#endif
    self.queued_tx = NULL;
    //return ret;
    //this function pops the tx queue for spi, for now, simply returns a new buffer with test code
    return MSG_Base_AllocateStringAtomic(TEST_STR);
}
static uint32_t
_queue_tx(MSG_Data_t * o){
    if(self.queued_tx){
        //we are forced to drop since something shouldn't be here
        MSG_Base_ReleaseDataAtomic(self.queued_tx);
    }
    self.queued_tx = o;
#ifdef PLATFORM_HAS_SSPI
    if(SSPI_INT != 0){
        nrf_gpio_cfg_output(SSPI_INT);
        nrf_gpio_pin_write(SSPI_INT, 1);
    }
#endif
    
    return 0;

}

static SSPIState
_initialize_transaction(){
    switch(self.control_reg){
        default:
            PRINTS("IN UNKNOWN MODE\r\n");
            return _reset();
            break;
        case REG_WRITE_TO_SSPI:
            PRINTS("READ FROM MASTER\r\n");
            self.transaction.state = WAIT_READ_RX_CTX;
            self.transaction.payload = MSG_Base_AllocateDataAtomic(128);
            return READING;
        case REG_READ_FROM_SSPI:
            PRINTS("WRITE TO MASTER\r\n");
            //prepare buffer here
            self.transaction.context_reg.length = 8; //get from tx queue;
            self.transaction.payload = _dequeue_tx();
            if(self.transaction.payload){
                //change to payload address
                self.transaction.context_reg.address = (MSG_Address_t){0,0}; 
            }else{
                self.transaction.context_reg.address = (MSG_Address_t){0,0};
            }
            self.transaction.state = WRITE_TX_CTX;
            return WRITING;
    }
}
static SSPIState
_handle_transaction(){
    switch(self.transaction.state){
        case WAIT_READ_RX_CTX:
            PRINTS("@WAIT RX LEN\r\n");
            spi_slave_buffers_set((uint8_t*)&self.transaction.context_reg, (uint8_t*)&self.transaction.context_reg, sizeof(self.transaction.context_reg), sizeof(self.transaction.context_reg));
            self.transaction.state = WAIT_READ_RX_BUF;
            break;
        case WRITE_TX_CTX:
            PRINTS("@WRITE TX LEN\r\n");
            spi_slave_buffers_set((uint8_t*)&self.transaction.context_reg,self.dummy->buf, sizeof(self.transaction.context_reg), sizeof(self.transaction.context_reg));
            self.transaction.state = WRITE_TX_BUF;
            break;
        case WAIT_READ_RX_BUF:
            PRINTS("@WAIT RX BUF\r\n");
            if(self.transaction.payload){
                spi_slave_buffers_set(self.transaction.payload->buf, &self.transaction.payload->buf, self.transaction.context_reg.length, self.transaction.context_reg.length);
                self.transaction.state = FIN_READ;
                break;
            }else{
                //no buffer wat do?
            }
            return _reset();
        case WRITE_TX_BUF:
            PRINTS("@WRITE TX BUF\r\n");
            if(self.transaction.payload){
                spi_slave_buffers_set(self.transaction.payload->buf, self.dummy->buf, self.transaction.context_reg.length, self.transaction.context_reg.length);
                self.transaction.state = FIN_WRITE;
                break;
            }else{
                //no buffer wat do?
            }
            return _reset();
        case FIN_READ:
            PRINTS("@FIN RX BUF\r\n");
            PRINTS("LEN = ");
            PRINT_HEX(&self.transaction.context_reg.length, sizeof(uint16_t));
            PRINTS(" ADDR  = ");
            PRINT_HEX(&self.transaction.context_reg.pad, sizeof(uint16_t));
            //send and release
            MSG_Base_ReleaseDataAtomic(self.transaction.payload);
            //only releasing now
            return _reset();
        case FIN_WRITE:
            PRINTS("@FIN TX BUF\r\n");
            PRINTS("LEN = ");
            PRINT_HEX(&self.transaction.context_reg.length, sizeof(uint16_t));
            PRINTS(" ADDR  = ");
            PRINT_HEX(&self.transaction.context_reg.pad, sizeof(uint16_t));
            MSG_Base_ReleaseDataAtomic(self.transaction.payload);
            return _reset();
        case TXRX_ERROR:
            //fall through to reset
        default:
            //fall through to reset
            return _reset();
    }
    return self.current_state;

}

static MSG_Status
_destroy(void){
    MSG_Base_ReleaseDataAtomic(self.dummy);
    return SUCCESS;
}
static MSG_Status
_flush(void){
    return SUCCESS;
}
static MSG_Status
_send(MSG_Address_t src, MSG_Address_t dst, MSG_Data_t * data){
    if(dst.submodule == 0){
        //command for this module

    }else if(dst.submodule == 1){
        //send to sspi slave
    }else{
        return FAIL;
    }
    return SUCCESS;
}

static void
_spi_evt_handler(spi_slave_evt_t event){
    uint8_t swap;
    char t[3] = {0};
    uint32_t ret = 0;
    switch(event.evt_type){
        case SPI_SLAVE_BUFFERS_SET_DONE:
            break;
        case SPI_SLAVE_XFER_DONE:
            //do things with buffer
            switch(self.current_state){
                case IDLE:
                    self.current_state = _initialize_transaction();
                    if(self.current_state == IDLE) break;
                case WRITING:
                case READING:
                    self.current_state = _handle_transaction();
                    break;
                default:
                    self.current_state = _reset();
                    break;
            }
            //reload
            break;
        default:
        case SPI_SLAVE_EVT_TYPE_MAX:
            //unknown???
            break;
    }
}
static MSG_Status
_init(){
    uint32_t ret;
    if(spi_slave_init(&self.config) || 
            spi_slave_evt_handler_register(_spi_evt_handler)){
        PRINTS("SPI FAIL");
        return FAIL;
    }
#ifdef PLATFORM_HAS_SSPI
    if(SSPI_INT != 0){
        nrf_gpio_cfg_output(SSPI_INT);
        nrf_gpio_pin_write(SSPI_INT, 0);
    }
#endif
    self.dummy = MSG_Base_AllocateDataAtomic(230);
    self.current_state = _reset();
    return SUCCESS;

}

MSG_Base_t * MSG_SSPI_Base(const spi_slave_config_t * p_spi_slave_config, const MSG_Central_t * parent){
    if(!p_spi_slave_config){
        return NULL;
    }
    self.config = *p_spi_slave_config;
    self.parent = parent;
    {
        self.base.init =  _init;
        self.base.flush = _flush;
        self.base.send = _send;
        self.base.destroy = _destroy;
        self.base.type = SSPI;
        self.base.typestr = name;
    }
    return &self.base;
}