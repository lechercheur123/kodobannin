#include "ant_user.h"
#include "message_uart.h"

static struct{
    MSG_Central_t * parent;
    volatile uint8_t pair_enable;
}self;

static void _on_message(const ANT_ChannelID_t * id, MSG_Address_t src, MSG_Data_t * msg){
    self.parent->dispatch(src, (MSG_Address_t){UART, 1}, msg);
    self.parent->dispatch(src, (MSG_Address_t){SSPI,1}, msg);
}

static void _on_unknown_device(const ANT_ChannelID_t * id){
    if(self.pair_enable){
        MSG_SEND_CMD(self.parent, ANT, MSG_ANTCommand_t, ANT_CREATE_SESSION, id, sizeof(*id));
        self.pair_enable = 0;
    }
}

static void _on_control_message(const ANT_ChannelID_t * id, MSG_Address_t src, uint8_t control_type, const uint8_t * control_payload){
    
}

MSG_ANTHandler_t * ANT_UserInit(MSG_Central_t * central){
    static MSG_ANTHandler_t handler = {
        .on_message = _on_message,
        .on_unknown_device = _on_unknown_device,
        .on_control_message = _on_control_message
    };
    self.parent = central;
    return &handler;
}

void ANT_UserPairNextDevice(void){
    self.pair_enable = 1;
}