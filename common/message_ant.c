#include <ant_interface.h>
#include <app_timer.h>
#include <app_error.h>
#include <ant_parameters.h>
#include "message_ant.h"
#include "util.h"
#include "app.h"
#include "crc16.h"
#include "antutil.h"
#include "ant_devices.h"

#define ANT_EVENT_MSG_BUFFER_MIN_SIZE 32u  /**< Minimum size of an ANT event message buffer. */
#define ANT_SESSION_NUM 2
enum{
    ANT_DISCOVERY_CENTRAL = 0,
    ANT_DISCOVERY_PERIPHERAL
}ANT_DISCOVERY_ROLE;

typedef struct{
    ANT_HeaderPacket_t header;
    MSG_Data_t * payload;
    union{
        //tx
        uint16_t idx;
        //rx
        struct{
            uint8_t network;
            uint8_t type;
        }phy;
    };
    union{
        //tx
        uint16_t count;
        //rx
        uint16_t prev_crc;
    };
}ConnectionContext_t;

typedef struct{
    ANT_ChannelID_t id;
    ConnectionContext_t rx_ctx;
    ConnectionContext_t tx_ctx;
}ANT_Session_t;

static struct{
    MSG_Base_t base;
    MSG_Central_t * parent;
    uint8_t discovery_role;
    ANT_Session_t sessions[ANT_SESSION_NUM];
}self;

static char * name = "ANT";

/**
 * Static declarations
 */
static uint16_t _calc_checksum(MSG_Data_t * data);
/*
 * Closes the channel, context are freed at channel closure callback
 */
static MSG_Status _destroy_channel(uint8_t channel);

/* Allocates payload based on receiving header packet */
static MSG_Data_t * INCREF _allocate_payload_rx(ANT_HeaderPacket_t * buf);

/* Returns status of the channel */
static uint8_t _get_channel_status(uint8_t channel);

/* Finds inverse of the channel type eg master -> slave */
static uint8_t _match_channel_type(uint8_t remote);

/* Finds an unassigned Channel */
static ANT_Session_t * _find_unassigned_session(void);

/* Finds session by id */
static ANT_Session_t * _get_session_by_id(const ANT_ChannelID_t * id);

static uint8_t
_match_channel_type(uint8_t remote){
    switch(remote){
        case CHANNEL_TYPE_SLAVE:
            return CHANNEL_TYPE_MASTER;
        case CHANNEL_TYPE_MASTER:
            return CHANNEL_TYPE_SLAVE;
        case CHANNEL_TYPE_SHARED_SLAVE:
            return CHANNEL_TYPE_SHARED_MASTER;
        case CHANNEL_TYPE_SHARED_MASTER:
            return CHANNEL_TYPE_SHARED_SLAVE;
        case CHANNEL_TYPE_SLAVE_RX_ONLY:
            return CHANNEL_TYPE_MASTER_TX_ONLY;
        case CHANNEL_TYPE_MASTER_TX_ONLY:
            return CHANNEL_TYPE_SLAVE_RX_ONLY;
        default:
            return 0xFF;
    }
}
static ANT_Session_t * _get_session_by_id(const ANT_ChannelID_t * id){
    int i;
    for(i = 0; i < ANT_SESSION_NUM; i++){
        //if(self.sessions[i].rx_ctx.
        if(self.sessions[i].id.device_number == id->device_number){
            return &self.sessions[i];
        }

    }
    return NULL;

}

static ANT_Session_t *
_find_unassigned_session(void){
    ANT_Session_t * ret = NULL;
    int i;
    for(i = 0; i < ANT_SESSION_NUM; i++){
        if(self.sessions[i].id.device_number == 0){
            ret = &self.sessions[i];
            break;
        }
    }
    return ret;
}
static MSG_Status
_destroy_channel(uint8_t channel){
    //this destroys the channel regardless of what state its in
    sd_ant_channel_close(channel);
    //sd_ant_channel_unassign(channel);
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
static void DECREF
_free_context(ConnectionContext_t * ctx){
    MSG_Base_ReleaseDataAtomic(ctx->payload);
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
static MSG_Data_t *
_allocate_payload_rx(ANT_HeaderPacket_t * buf){
    MSG_Data_t * ret;
    PRINTS("Pages: ");
    PRINT_HEX(&buf->page_count, 1);
    PRINTS("\r\n");
    ret = MSG_Base_AllocateDataAtomic( 6 * buf->page_count );
    if(ret){
        //readjusting length of the data, previously
        //allocated in multiples of page_size to make assembly easier
        uint16_t ss = ((uint8_t*)buf)[5] << 8;
        ss += ((uint8_t*)buf)[4];
        ret->len = ss;
        PRINTS("OBJ LEN\r\n");
        PRINT_HEX(&ret->len, 2);
    }
    return ret;
}
static MSG_Status
_connect(uint8_t channel){
    //needs at least assigned
    MSG_Status ret = FAIL;
    uint8_t inprogress;
    uint8_t pending;
    sd_ant_channel_status_get(channel, &inprogress);
    if(!sd_ant_channel_open(channel)){
        ret = SUCCESS;
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
_configure_channel(uint8_t channel, const ANT_ChannelPHY_t * spec, const ANT_ChannelID_t * id, uint8_t  ext_fields){
    uint32_t ret = 0;
    if(_get_channel_status(channel)){
        ret = _destroy_channel(channel);
    }
    if(!ret){
        ret += sd_ant_channel_assign(channel, spec->channel_type, spec->network, ext_fields);
        ret += sd_ant_channel_radio_freq_set(channel, spec->frequency);
        ret += sd_ant_channel_period_set(channel, spec->period);
        ret += sd_ant_channel_id_set(channel, id->device_number, id->device_type, id->transmit_type);
        ret += sd_ant_channel_low_priority_rx_search_timeout_set(channel, 0xFF);
        ret += sd_ant_channel_rx_search_timeout_set(channel, 0);
    }
    return ret?FAIL:SUCCESS;
}

static MSG_Status
_set_discovery_mode(uint8_t role){
    ANT_ChannelID_t id = {0};
    ANT_ChannelPHY_t phy = {
        .period = 273,
        .frequency = 66,
        .channel_type = CHANNEL_TYPE_SLAVE,
        .network = 0};
    self.discovery_role = role;
    switch(role){
        case ANT_DISCOVERY_CENTRAL:
            //central mode
            PRINTS("CENTRAL\r\n");
            APP_OK(_configure_channel(ANT_DISCOVERY_CHANNEL, &phy, &id, 0));
            APP_OK(sd_ant_lib_config_set(ANT_LIB_CONFIG_MESG_OUT_INC_DEVICE_ID | ANT_LIB_CONFIG_MESG_OUT_INC_RSSI | ANT_LIB_CONFIG_MESG_OUT_INC_TIME_STAMP));
            sd_ant_rx_scan_mode_start(0);
            break;
        case ANT_DISCOVERY_PERIPHERAL:
            //peripheral mode
            //configure shit here
            PRINTS("PERIPHERAL\r\n");
            phy.channel_type = CHANNEL_TYPE_MASTER;
            id.device_number = GET_UUID_16();
            id.device_type = HLO_ANT_DEVICE_TYPE_PILL_EVT;
            id.transmit_type = 0;
            phy.frequency = 66;
            phy.period = 1092;
            APP_OK(sd_ant_lib_config_set(ANT_LIB_CONFIG_MESG_OUT_INC_DEVICE_ID | ANT_LIB_CONFIG_MESG_OUT_INC_RSSI | ANT_LIB_CONFIG_MESG_OUT_INC_TIME_STAMP));
            _configure_channel(ANT_DISCOVERY_CHANNEL, &phy, &id, 0);
            _connect(ANT_DISCOVERY_CHANNEL);
            break;
        default:
            break;
    }
    return SUCCESS;
}

static void
_handle_channel_closure(uint8_t * channel, uint8_t * buf, uint8_t buf_size){
}
static void
_assemble_payload(ConnectionContext_t * ctx, ANT_PayloadPacket_t * packet){
    //technically if checksum is xor, is possible to do incremental xor to
    //find out if the data is valid without doing it at the header packet
    //but for simplicity's sake, lets just leave the optomizations later...
    uint16_t offset = (packet->page - 1) * 6;
    if(ctx->payload){
        ctx->payload->buf[offset] = packet->payload[0];
        ctx->payload->buf[offset + 1] = packet->payload[1];
        ctx->payload->buf[offset + 2] = packet->payload[2];
        ctx->payload->buf[offset + 3] = packet->payload[3];
        ctx->payload->buf[offset + 4] = packet->payload[4];
        ctx->payload->buf[offset + 5] = packet->payload[5];
    }
//    PRINT_HEX(&ctx->payload->buf, ctx->payload->len);
    PRINTS("LEN: | ");
    PRINT_HEX(&ctx->payload->len, sizeof(ctx->payload->len));
    PRINTS(" |");
    PRINTS("\r\n");

}
static uint8_t
_integrity_check(ConnectionContext_t * ctx){
    if(ctx->payload){
        if(_calc_checksum(ctx->payload) == ctx->header.checksum){
            return 1;
        }
    }
    return 0;
}
/**
 * Assembles the pyload portion
 */
static MSG_Data_t *
_assemble_rx(ConnectionContext_t * ctx, uint8_t * buf, uint32_t buf_size){
    MSG_Data_t * ret = NULL;
    if(buf[0] == 0 && buf[1] > 0){
        //standard header
        ANT_HeaderPacket_t * new_header = (ANT_HeaderPacket_t *)buf;
        uint16_t new_crc = (uint16_t)(buf[7] << 8) + buf[6];
        if(_integrity_check(ctx)){
            ret = ctx->payload;
            MSG_Base_AcquireDataAtomic(ret);
            _free_context(ctx);
        }
        if(ctx->header.checksum != new_crc){
            _free_context(ctx);
            ctx->header = *new_header;
            ctx->payload = _allocate_payload_rx(&ctx->header);
        }
    }else if(buf[0] <= buf[1] && buf[1] > 0){
        //payload
        if(ctx->payload){
            _assemble_payload(ctx, (ANT_PayloadPacket_t *)buf);
        }
    }else{
        //Unknown
    }
    return ret;
}
static uint8_t DECREF
_assemble_tx(ConnectionContext_t * ctx, uint8_t * out_buf, uint32_t buf_size){
    ANT_HeaderPacket_t * header = &ctx->header;
    if(ctx->count == 0){
        memcpy(out_buf, &ctx->header, 8);
        return 0;
    }else{
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
    uint8_t message[ANT_STANDARD_DATA_PAYLOAD_SIZE];
    uint32_t ret;
    if(self.discovery_role == ANT_DISCOVERY_CENTRAL){
        //central, only channels 1-7 are tx
        //channel 1-7 maps to index 0-6
        PRINTS("C");
        uint8_t idx = *channel - 1;
        if(idx < ANT_SESSION_NUM){
            ret = _assemble_tx(&self.sessions[idx].tx_ctx, message, ANT_STANDARD_DATA_PAYLOAD_SIZE);
        }
        if(!ret){
            _free_context(&self.sessions[*channel].tx_ctx);
            _destroy_channel(*channel);
        }
    }else{
        //periperal, all channels are tx
        //channels 0-7 mapes to index 0-7
        PRINTS("P");
        if(*channel < ANT_SESSION_NUM){
            ret = _assemble_tx(&self.sessions[*channel].tx_ctx, message, ANT_STANDARD_DATA_PAYLOAD_SIZE);
        }
        if(!ret){
            _free_context(&self.sessions[*channel].tx_ctx);
            _destroy_channel(*channel);
        }

    }
    sd_ant_broadcast_message_tx(*channel,sizeof(message), message);
    if(!ret){
        //close channel
        _destroy_channel(*channel);
    }
}
static void
_handle_rx(uint8_t * channel, uint8_t * buf, uint8_t buf_size){
    ANT_MESSAGE * msg = (ANT_MESSAGE*)buf;
    uint8_t * rx_payload = msg->ANT_MESSAGE_aucPayload;
    ANT_Session_t * session = NULL;
    ANT_ChannelID_t id = {0};
    if(self.discovery_role == ANT_DISCOVERY_CENTRAL){
        EXT_MESG_BF ext = msg->ANT_MESSAGE_sExtMesgBF;
        uint8_t * extbytes = msg->ANT_MESSAGE_aucExtData;
        if(ext.ucExtMesgBF & MSG_EXT_ID_MASK){
            id = (ANT_ChannelID_t){
                .device_number = *((uint16_t*)extbytes),
                .device_type = extbytes[4],
                .transmit_type = extbytes[3],
            };
        }else{
            //error
            PRINTS("RX Error\r\n");
            return;
        }
    }else{
        if(sd_ant_channel_id_get(*channel, &id.device_number, &id.device_type, &id.transmit_type)){
            //error
            PRINTS("RX Error\r\n");
            return;
        }
    }
    if(rx_payload[0] == rx_payload[1] && rx_payload[1] == 0){
        PRINTS("Discovery Packet\r\n");
        //handle discovery with id
        return;
    }
    session = _get_session_by_id(&id);
    //handle
    PRINTS("MSG FROM ID = ");
    PRINT_HEX(&id.device_number, 2);
    PRINTS("\r\n");
    if(session){
        ConnectionContext_t * ctx = &session->rx_ctx;
        MSG_Data_t * ret = _assemble_rx(ctx, rx_payload, buf_size);
        if(ret){
            PRINTS("GOT A NEw MESSAGE OMFG\r\n");
            {
                MSG_Address_t src = (MSG_Address_t){ANT, channel+1};
                MSG_Address_t dst = (MSG_Address_t){UART, 1};
                //then dispatch to uart for printout
                self.parent->dispatch(src, dst, ret);
            }
            MSG_Base_ReleaseDataAtomic(ret);
        }
    }else{
        PRINTS("Session Not Found");
    }
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
                PRINT_HEX(&antcmd->param.role, 0x1);
                return _set_discovery_mode(antcmd->param.role);
            case ANT_CREATE_SESSION:
                //since scanning in central mode, rx does not map directly to channels
                //sessions become virtual channels
                {
                    ANT_Session_t * s = _find_unassigned_session();
                    PRINTS("Create Session\r\n");
                    switch(self.discovery_role){
                        case ANT_DISCOVERY_CENTRAL:
                            if(s){
                                s->id = antcmd->param.session_info;
                            }else{
                                PRINTS("Out of sessions");
                            }
                            break;
                        case ANT_DISCOVERY_PERIPHERAL:
                            PRINTS("Already configured as peripheral, sending as channel 0");
                            break;
                        default:
                            PRINTS("Need to define a role first!\r\n");
                            break;
                    }
                }
                break;
        }
    }else{
        uint8_t channel = dst.submodule - 1;
        if(self.discovery_role == ANT_DISCOVERY_CENTRAL){
            if(channel == 0){
                PRINTS("Can not send over discovery 0");
            }
        }else{
            if(channel < ANT_SESSION_NUM){
                ANT_Session_t * session = &self.sessions[channel];
                if(session->tx_ctx.payload){
                    PRINTS("Channel In Use\r\n");
                }else{
                    PRINTS("Sending...\r\n");
                    session->tx_ctx.payload = data;
                    MSG_Base_AcquireDataAtomic(data);
                    session->tx_ctx.header.size = data->len;
                    session->tx_ctx.header.checksum = _calc_checksum(data);
                    session->tx_ctx.header.page = 0;
                    session->tx_ctx.header.page_count = data->len/6 + (((data->len)%6)?1:0);
                    session->tx_ctx.count = 3;
                    session->tx_ctx.idx = 0;
                    _connect(channel);
                }
            }

        }
    }
    return SUCCESS;
}

static MSG_Status
_init(){
    uint32_t ret;
    uint8_t network_key[8] = {0,0,0,0,0,0,0,0};
    ret = sd_ant_stack_reset();
    ret += sd_ant_network_address_set(0,network_key);
    /* Exclude list, don't scan for canceled devices */
    //ret += sd_ant_id_list_config(ANT_DISCOVERY_CHANNEL, 4, 1);
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
    switch(event){
        case EVENT_RX_FAIL:
            //PRINTS("FRX\r\n");
            break;
        case EVENT_RX:
            PRINTS("R");
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
            PRINTS("T");
            _handle_tx(&ant_channel, event_message_buffer, ANT_EVENT_MSG_BUFFER_MIN_SIZE);
            break;
        case EVENT_TRANSFER_TX_FAILED:
            break;
        case EVENT_CHANNEL_COLLISION:
            PRINTS("XX\r\n");
            break;
        case EVENT_CHANNEL_CLOSED:
            //_handle_channel_closure(&ant_channel, event_message_buffer, ANT_EVENT_MSG_BUFFER_MIN_SIZE);
            break;
        default:
            {
                PRINTS("UE:");
                PRINT_HEX(&event,1);
                PRINTS("\\");
            }
            break;
    }

}
uint8_t MSG_ANT_BondCount(void){
    uint8_t i, ret = 0;
    for(i = 0; i < NUM_ANT_CHANNELS; i++){
        if(i != ANT_DISCOVERY_CHANNEL){
            if(_get_channel_status(i) > 0){
                ret++;
            }
        }
    }
    return ret;
}