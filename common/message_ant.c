#include <ant_interface.h>
#include <app_timer.h>
#include <app_error.h>
#include <ant_parameters.h>
#include "message_ant.h"
#include "util.h"
#include "app.h"
#include "crc16.h"

#define NUM_ANT_CHANNELS 8
#define ANT_EVENT_MSG_BUFFER_MIN_SIZE 32u  /**< Minimum size of an ANT event message buffer. */

typedef struct{
    MSG_Data_t * header;
    MSG_Data_t * payload;
    uint16_t idx;
    uint16_t count;
}ChannelContext_t;

static struct{
    MSG_Base_t base;
    MSG_Central_t * parent;
    uint8_t discovery_role;
    app_timer_id_t discovery_timeout;
    ANT_DiscoveryProfile_t profile;
    ChannelContext_t channel_ctx[NUM_ANT_CHANNELS];
}self;
static char * name = "ANT";
#define CHANNEL_NUM_CHECK(ch) (ch < NUM_ANT_CHANNELS)

/**
 * Static declarations
 */
static uint16_t _calc_checksum(MSG_Data_t * data);
static MSG_Status _destroy_channel(uint8_t channel);

static MSG_Status
_destroy_channel(uint8_t channel){
    //this destroys the channel regardless of what state its in
    sd_ant_channel_close(channel);
    sd_ant_channel_unassign(channel);
    return SUCCESS;
}
/*
 *#define STATUS_UNASSIGNED_CHANNEL                  ((uint8_t)0x00) ///< Indicates channel has not been assigned.
 *#define STATUS_ASSIGNED_CHANNEL                    ((uint8_t)0x01) ///< Indicates channel has been assigned.
 *#define STATUS_SEARCHING_CHANNEL                   ((uint8_t)0x02) ///< Indicates channel is active and in searching state.
 *#define STATUS_TRACKING_CHANNEL                    ((uint8_t)0x03) ///< Indicates channel is active and in tracking state.
 */
static uint8_t
_get_channel_status(uint8_t channel){
    uint8_t ret;
    sd_ant_channel_status_get(channel, &ret);
    return ret;
}
static MSG_Data_t * INCREF
_make_header_tx(MSG_Data_t * payload){
    MSG_Data_t * ret = MSG_Base_AllocateDataAtomic(8);
    if(ret){
        ANT_HeaderPacket_t * header = ret->buf;
        header->page  = 0;
        header->page_count = payload->len / 6 + ( ((payload->len)%6)?1:0);
        header->src_mod = 0;
        header->src_submod = 0;
        header->dst_mod = 0;
        header->dst_submod = 0;
        header->checksum = _calc_checksum(payload);
    }
    return ret;
}
static void DECREF
_free_context(ChannelContext_t * ctx){
    MSG_Base_ReleaseDataAtomic(ctx->header);
    MSG_Base_ReleaseDataAtomic(ctx->payload);
    ctx->header = NULL;
    ctx->payload = NULL;
}
static uint16_t
_calc_checksum(MSG_Data_t * data){
    uint16_t ret;
    ret = crc16_compute(data->buf, data->len, NULL);
    PRINTS("CS: ");
    PRINT_HEX(&ret,2);
    PRINTS("\r\n");
    return ret;
}
static MSG_Status
_connect(uint8_t channel, const ANT_ChannelID_t * id){
    //needs at least assigned
    MSG_Status ret = FAIL;
    if(_get_channel_status(channel)){
        sd_ant_channel_close(channel);
        sd_ant_channel_id_set(channel, id->device_number, id->device_type, id->transmit_type);
        if(!sd_ant_channel_open(channel)){
            ret = SUCCESS;
        }
    }
    return ret;
}

static MSG_Status
_disconnect(uint8_t channel){
    if(!sd_ant_channel_close(channel)){
        return SUCCESS;
    }
    return FAIL;
}

static MSG_Status
_configure_channel(uint8_t channel, const ANT_ChannelPHY_t * spec){
    MSG_Status ret = SUCCESS;
    if(_get_channel_status(channel)){
        ret = _destroy_channel(channel);
    }
    if(!ret){
        ret += sd_ant_channel_assign(channel, spec->channel_type, spec->network, 0);
        ret += sd_ant_channel_radio_freq_set(channel, spec->frequency);
        ret += sd_ant_channel_period_set(channel, spec->period);
    }
    return ret;
}

static MSG_Status
_set_discovery_mode(uint8_t role){
    ANT_ChannelID_t id = {0};
    ANT_ChannelPHY_t phy = { 
        .period = 32768,
        .frequency = 66,
        .channel_type = CHANNEL_TYPE_SLAVE,
        .network = 0};
    if(role != self.discovery_role){
        app_timer_stop(self.discovery_timeout);
        _destroy_channel(ANT_DISCOVERY_CHANNEL);
        self.discovery_role = role;
    }else if(_get_channel_status(ANT_DISCOVERY_CHANNEL)>1){
        return SUCCESS;
    }
    switch(role){
        case 0:
            //central mode
            PRINTS("SLAVE\r\n");
     //       phy.period = 1092;
            _configure_channel(ANT_DISCOVERY_CHANNEL, &phy);
            _connect(ANT_DISCOVERY_CHANNEL, &id);
            _free_context(&self.channel_ctx[0]);
            break;
        case 1:
            //peripheral mode
            //configure shit here
            PRINTS("MASTER\r\n");
            phy.channel_type = CHANNEL_TYPE_MASTER;
      //      phy.period = 1092;
            //set up id
            {
                //test only
                //id.transmit_type = ANT_TRANS_TYPE_2_BYTE_SHARED_ADDRESS;
                id.transmit_type = 3;
                id.device_type = 1;
                id.device_number = 0x5354;
            }
            _configure_channel(ANT_DISCOVERY_CHANNEL, &phy);
            _connect(ANT_DISCOVERY_CHANNEL, &id);
            {
            //    uint32_t to = APP_TIMER_TICKS(2000, APP_TIMER_PRESCALER);
             //   app_timer_start(self.discovery_timeout,to, NULL);
            }
            {
                //test only
                _free_context(&self.channel_ctx[0]);
                self.channel_ctx[0].payload = MSG_Base_AllocateStringAtomic("HelloWorld");
                self.channel_ctx[0].header = _make_header_tx(self.channel_ctx[0].payload);
                self.channel_ctx[0].idx = 0;
                self.channel_ctx[0].count = 199;
            }
            break;
        default:
            break;
    }
    return SUCCESS;
}
/**
 * Handles the timeout of discovery_mode
 */
static void
_discovery_timeout(void * ctx){
    PRINTS("Good Night Sweet Prince\r\n");
    self.discovery_role = 0xFF;
    _destroy_channel(ANT_DISCOVERY_CHANNEL);
}

static void
_handle_channel_closure(uint8_t * channel, uint8_t * buf, uint8_t buf_size){
    PRINTS("Channel Close\r\n");
    if(*channel == ANT_DISCOVERY_CHANNEL){
        PRINTS("Re-opening Channel ");
        PRINT_HEX(channel, 1);
        PRINTS("\r\n");
        MSG_SEND(self.parent,ANT,ANT_SET_ROLE,&self.discovery_role,1);
    }
}
static MSG_Data_t * INCREF
_allocate_header_rx(ANT_HeaderPacket_t * buf){
    MSG_Data_t * ret = MSG_Base_AllocateDataAtomic(sizeof(ANT_HeaderPacket_t));
    if(ret){
        memcpy(ret->buf, buf, sizeof(ANT_HeaderPacket_t));
    }
    return ret;
}
static MSG_Data_t *
_allocate_payload(ANT_HeaderPacket_t * buf){
    uint32_t ret;
    PRINTS("Pages: ");
    PRINT_HEX(&buf->page_count, 1);
    PRINTS("\r\n");
    return MSG_Base_AllocateDataAtomic( 6 * buf->page_count );
}
static void INCREF
_allocate_context(ChannelContext_t * ctx, ANT_HeaderPacket_t * header){
    ctx->header = _allocate_header_rx( header );
    if(header){
        ctx->payload = _allocate_payload( header );
    }
}
static void
_assemble_payload(ChannelContext_t * ctx, ANT_PayloadPacket_t * packet){
    //technically if checksum is xor, is possible to do incremental xor to 
    //find out if the data is valid without doing it at the header packet
    //but for simplicity's sake, lets just leave the optomizations later...
    uint16_t offset = (packet->page - 1) * 6;
    ctx->payload->buf[offset] = packet->payload[0];
    ctx->payload->buf[offset + 1] = packet->payload[1];
    ctx->payload->buf[offset + 2] = packet->payload[2];
    ctx->payload->buf[offset + 3] = packet->payload[3];
    ctx->payload->buf[offset + 4] = packet->payload[4];
    ctx->payload->buf[offset + 5] = packet->payload[5];

}
static uint8_t
_integrity_check(ChannelContext_t * ctx){
    if(ctx->header && ctx->payload){
        ANT_HeaderPacket_t * cmp = ctx->header->buf;
        if(_calc_checksum(ctx->payload) == cmp->checksum){
            return 1;
        }
    }
    return 0;
}
static uint8_t
_new_message_check(ChannelContext_t * ctx, ANT_HeaderPacket_t * packet){
    ANT_HeaderPacket_t * cmp = (ANT_HeaderPacket_t*)ctx->header->buf;
    if(cmp->checksum == packet->checksum){
        return 0;
    }else{
        return 1;
    }
}
static MSG_Data_t * 
_assemble_rx(ChannelContext_t * ctx, uint8_t * buf, uint32_t buf_size){
    MSG_Data_t * ret = NULL;
    if(ctx->header){
        PRINTS("H");
        //header already exist
        if(buf[0] == 0){
            PRINTS("header");
            uint8_t _message_complete = _integrity_check(ctx);
            //uint8_t _new_message = _new_message_check(ctx, (ANT_HeaderPacket_t *)buf);
            if(_message_complete){
                PRINTS("Complete\r\n");
                ret = ctx->payload;
                MSG_Base_AcquireDataAtomic(ret);
                _free_context(ctx);
            }
            /*
             *if(_new_message){
             *    PRINTS("A New Message was found\r\n");
             *    _free_context(ctx);
             *    _allocate_context(ctx, (ANT_HeaderPacket_t *)buf);
             *}
             */
        }else{
            //payload packet
            _assemble_payload(ctx, (ANT_PayloadPacket_t *)buf);
        }
    }else{
        PRINTS("NH");
        //header does not exist
        //payload packets are ignored for simplicity
        if(buf[0] == 0){ 
            _allocate_context(ctx, (ANT_HeaderPacket_t*)buf);
        }
    }
    return ret;
}
static uint8_t DECREF
_assemble_tx(ChannelContext_t * ctx, uint8_t * out_buf, uint32_t buf_size){
    if(ctx->count == 0 && ctx->header && ctx->payload){
        memcpy(out_buf, ctx->header->buf, 8);
        _free_context(ctx);
        //get next in line
    }else if(ctx->count == 0){
        return 0;
    }else{
        ANT_HeaderPacket_t * header = ctx->header->buf;
        if(ctx->idx == 0){
            memcpy(out_buf, header, sizeof(*header));
        }else{
            uint16_t offset = (ctx->idx - 1) * 6;
            int i;
            out_buf[0] = ctx->idx;
            out_buf[1] = header->page_count;
            for(i = 0; i < 6; i++){
                if( offset + i < ctx->payload->len ){
                    out_buf[2+i] = ctx->payload->buf[offset+i];
                }else{
                    out_buf[2+i] = 0;
                }
            }
        }
        if(++ctx->idx > header->page_count){
            ctx->idx = 0;
            ctx->count--;
        }
    }
    return 1;

}

static void
_handle_tx(uint8_t * channel, uint8_t * buf, uint8_t buf_size){
    uint8_t message[ANT_STANDARD_DATA_PAYLOAD_SIZE] = {1,2,3,4,5,6,7,8};
    uint32_t ret;
    if(*channel == ANT_DISCOVERY_CHANNEL){
        /*
         **((uint16_t*)message) = (uint16_t)0x5354;
         *message[7]++;
         */
        ChannelContext_t * ctx = &self.channel_ctx[*channel];
        if(!_assemble_tx(ctx, message, ANT_STANDARD_DATA_PAYLOAD_SIZE)){
            PRINTS("FIN\r\n");
        }
    }else{
        ChannelContext_t * ctx = &self.channel_ctx[*channel];
        if(!_assemble_tx(ctx, message, ANT_STANDARD_DATA_PAYLOAD_SIZE)){
            PRINTS("FIN\r\n");
        }
    }
    ret = sd_ant_broadcast_message_tx(0,sizeof(message), message);
}
static void
_handle_rx(uint8_t * channel, uint8_t * buf, uint8_t buf_size){
    ANT_MESSAGE * msg = (ANT_MESSAGE*)buf;
    uint8_t * rx_payload = msg->ANT_MESSAGE_aucPayload;
    PRINT_HEX(rx_payload,ANT_STANDARD_DATA_PAYLOAD_SIZE);
    if(*channel == ANT_DISCOVERY_CHANNEL){
        //discovery mode channel
        uint16_t dev_id;
        uint8_t dev_type;
        uint8_t transmit_type;
        PRINTS(" CH: ");
        if(!sd_ant_channel_id_get(*channel, &dev_id, &dev_type, &transmit_type)){
            PRINT_HEX(&dev_id, sizeof(dev_id));
            PRINTS(" | ");
            PRINT_HEX(&dev_type, sizeof(dev_type));
            PRINTS(" | ");
            PRINT_HEX(&transmit_type, sizeof(transmit_type));
        }
        //allocate channel, configure it as slave, then open...
        ChannelContext_t * ctx = &self.channel_ctx[*channel];
        MSG_Data_t * ret = _assemble_rx(ctx, rx_payload, buf_size);
        if(ret){
            PRINTS("GOT A NEw MESSAGE OMFG\r\n");
            MSG_Base_ReleaseDataAtomic(ret);
        }
    }else{
        //assemble context
        ChannelContext_t * ctx = &self.channel_ctx[*channel];
        MSG_Data_t * ret = _assemble_rx(ctx, rx_payload, buf_size);
        if(ret){
            PRINTS("GOT A NEw MESSAGE OMFG\r\n");
            MSG_Base_ReleaseDataAtomic(ret);
        }
    }
    PRINTS("\r\n");
}


static MSG_Status
_destroy(void){
    return SUCCESS;
}

static MSG_Status
_flush(void){
    return SUCCESS;
}
static MSG_Status
_send(MSG_Address_t src, MSG_Address_t dst, MSG_Data_t * data){
    if(!data){
        return FAIL;
    }
    if(dst.submodule == 0){
        MSG_ANTCommand_t * antcmd = (MSG_ANTCommand_t*)data->buf;
        switch(antcmd->cmd){
            default:
            case ANT_PING:
                PRINTS("ANT_PING\r\n");
                break;
            case ANT_SET_ROLE:
                PRINTS("ANT_SET_ROLE\r\n");
                PRINT_HEX(&antcmd->param.role, 1);
                return _set_discovery_mode(antcmd->param.role);
            case ANT_SET_DISCOVERY_PROFILE:
//                self.profile = antcmd->param.profile;
                PRINTS("claiming profile");
                return SUCCESS;
        }
    }
}

static MSG_Status
_init(){
    uint32_t ret;
    uint8_t network_key[8] = {0,0,0,0,0,0,0,0};
    ret = sd_ant_stack_reset();
    ret += sd_ant_network_address_set(0,network_key);
    ret += app_timer_create(&self.discovery_timeout, APP_TIMER_MODE_SINGLE_SHOT, _discovery_timeout);
    if(!ret){
        return SUCCESS;
    }else{
        return FAIL;
    }
}
MSG_Base_t * MSG_ANT_Base(MSG_Central_t * parent){
    self.parent = parent;
    {
        self.base.init =  _init;
        self.base.flush = _flush;
        self.base.send = _send;
        self.base.destroy = _destroy;
        self.base.type = ANT;
        self.base.typestr = name;
    }
    return &self.base;

}
void ant_handler(ant_evt_t * p_ant_evt){
    uint8_t event = p_ant_evt->event;
    uint8_t ant_channel = p_ant_evt->channel;
    uint32_t * event_message_buffer = p_ant_evt->evt_buffer;
    PRINT_HEX(&event,1);
    PRINTS("\r\n");
    switch(event){
        case EVENT_RX_FAIL:
            //PRINTS("FRX\r\n");
            break;
        case EVENT_RX:
            PRINTS("RX\r\n");
            _handle_rx(&ant_channel,event_message_buffer, ANT_EVENT_MSG_BUFFER_MIN_SIZE);
            break;
        case EVENT_RX_SEARCH_TIMEOUT:
            PRINTS("RXTO\r\n");
            break;
        case EVENT_RX_FAIL_GO_TO_SEARCH:
            PRINTS("RXFTS\r\n");
            break;
        case EVENT_TRANSFER_RX_FAILED:
            PRINTS("RFFAIL\r\n");
            break;
        case EVENT_TX:
            //PRINTS("TX\r\n");
            _handle_tx(&ant_channel,event_message_buffer, ANT_EVENT_MSG_BUFFER_MIN_SIZE);
            break;
        case EVENT_TRANSFER_TX_FAILED:
            break;
        case EVENT_CHANNEL_COLLISION:
            PRINTS("XX\r\n");
            break;
        case EVENT_CHANNEL_CLOSED:
            _handle_channel_closure(&ant_channel, event_message_buffer, ANT_EVENT_MSG_BUFFER_MIN_SIZE);
            break;
        default:
            break;
    }

}
