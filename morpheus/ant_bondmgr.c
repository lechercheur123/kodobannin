#include "ant_bondmgr.h"
#include "pstorage.h"
#include "util.h"
#include "crc16.h"

#define ANT_BOND_MANAGER_DATA_SIGNATURE  0x53540000
#define ANT_BOND_MANAGER_DATA_COUNT  4

typedef struct{
    pstorage_handle_t   mp_flash_bond_info;                              /**< Pointer to flash location to write next Bonding Information. */
    pstorage_size_t m_bond_info_in_flash_count;                                /** < Number of bonded devices in flash **/
    uint32_t m_bond_crc;
    ANT_BondedDevice_t devices[ANT_BOND_MANAGER_DATA_COUNT];
}ANT_BondMgr_t;

static ANT_BondMgr_t self;
static void bm_pstorage_cb_handler(pstorage_handle_t * handle,
                                   uint8_t             op_code,
                                   uint32_t            result,
                                   uint8_t           * p_data,
                                   uint32_t            data_len);
static uint32_t bonding_info_load_from_flash(ANT_BondedDevice_t * p_bond, pstorage_size_t * block_idx);
static uint32_t load_all_from_flash(void);
static uint32_t crc_extract(uint32_t header, uint16_t * p_crc);




static uint32_t crc_extract(uint32_t header, uint16_t * p_crc){
    if ((header & 0xFFFF0000U) == ANT_BOND_MANAGER_DATA_SIGNATURE)
    {
        *p_crc = (uint16_t)(header & 0x0000FFFFU);

        return NRF_SUCCESS;
    }
    else if (header == 0xFFFFFFFFU)
    {
        return NRF_ERROR_NOT_FOUND;
    }
    else
    {
        return NRF_ERROR_INVALID_DATA;
    }
}
static uint32_t bonding_info_load_from_flash(ANT_BondedDevice_t * p_bond, pstorage_size_t * block_idx)
{
    pstorage_handle_t source_block;
    uint32_t          err_code;
    uint32_t          crc;
    uint32_t        m_crc_bond_info;
    uint16_t          crc_header;
    

    // Get block pointer from base
    err_code = pstorage_block_identifier_get(&self.mp_flash_bond_info,
                                                *block_idx,
                                             &source_block);
    PRINTS("1\r\n");
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
    
    PRINTS("RID = ");
    PRINT_HEX(&source_block.block_id, 4);
    PRINTS("\r\n");
    PRINTS("2\r\n");
//    err_code = pstorage_load((uint8_t *)&crc, &source_block, sizeof(uint32_t), 0);
    err_code = 0;
    memcpy(&crc, (uint8_t*)source_block.block_id, 4);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
    
    PRINTS("3 CRC\r\n");
    PRINT_HEX(&crc, 4);
    // Extract CRC from header.
    // if result is NRF_ERROR_NOT_FOUND, then no data needs to be loaded
    err_code = crc_extract(crc, &crc_header);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
    
    // Load devices.
    err_code = pstorage_load((uint8_t *)p_bond,
                             &source_block,
                             sizeof(*p_bond),
                             sizeof(uint32_t));
    PRINTS("4\r\n");
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    // Check CRC.
    m_crc_bond_info = crc16_compute((uint8_t *)p_bond,
                                    sizeof(*p_bond),
                                    NULL);
    PRINTS("5\r\n");
    if (m_crc_bond_info == crc_header)
    {
        (*block_idx)++;
        return NRF_SUCCESS;
    }
    else
    {
        return NRF_ERROR_INVALID_DATA;
    }
    return NRF_SUCCESS;
}

uint32_t ANT_BondMgrInit(void){
    pstorage_module_param_t param;
    uint32_t err_code;
          
    param.block_size  = sizeof (ANT_BondedDevice_t) + sizeof (uint32_t);
    param.block_count = ANT_BOND_MANAGER_DATA_COUNT;
    param.cb          = bm_pstorage_cb_handler;
    
    err_code = pstorage_register(&param, &self.mp_flash_bond_info);    
    if (err_code != NRF_SUCCESS)
    {
        PRINTS("ANT BOND Error =");
        PRINT_HEX(&err_code, 2);
        return err_code;
    }
    load_all_from_flash();
    return NRF_SUCCESS;
}

uint32_t ANT_BondMgrAdd(const ANT_BondedDevice_t * p_bond){
    uint32_t err_code, m_crc_bond_info;
    pstorage_handle_t dest_block;

    // Check if flash is full
    if (self.m_bond_info_in_flash_count >= ANT_BOND_MANAGER_DATA_COUNT)
    {
        return NRF_ERROR_NO_MEM;
    }
    PRINTS("a1\r\n");

    // Check if this is the first bond to be stored
    if (self.m_bond_info_in_flash_count == 0)
    {
        // Initialize CRC
        m_crc_bond_info = crc16_compute(NULL, 0, NULL);
    }

    // Get block pointer from base
    err_code = pstorage_block_identifier_get(&self.mp_flash_bond_info,self.m_bond_info_in_flash_count,&dest_block);
    PRINTS("a2 \r\n");
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
    
    PRINTS("WID = ");
    PRINT_HEX(&dest_block.block_id, 4);
    PRINTS("\r\n");
    // Write Bonding Information
    err_code = pstorage_store(&dest_block,
                              (uint8_t *)p_bond,
                              sizeof(*p_bond),
                              sizeof(uint32_t));


    PRINTS("a3 \r\n");
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
    m_crc_bond_info = crc16_compute((uint8_t *)p_bond,
                                     sizeof(*p_bond),
                                     NULL);

    // Write header
    self.m_bond_crc = (ANT_BOND_MANAGER_DATA_SIGNATURE | m_crc_bond_info);
    PRINTS("CS = ");
    PRINT_HEX(&self.m_bond_crc, 4);
    err_code = pstorage_store(&dest_block, (uint8_t *)&self.m_bond_crc,sizeof(uint32_t),0);
    PRINTS("a4\r\n");
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
    PRINTS("a5\r\n");
    self.m_bond_info_in_flash_count++;
    return NRF_SUCCESS;
}
uint32_t ANT_BondMgrRemove(const ANT_BondedDevice_t * p_bond){
    return 0;
}
void ANT_BondMgrForEach(ANT_BondMgrCB cb){
}
uint32_t ANT_BondMgr_EraseAll(void){
    uint32_t err_code;
    err_code = pstorage_clear(&self.mp_flash_bond_info, ANT_BOND_MANAGER_DATA_COUNT * (sizeof(uint32_t) + sizeof(ANT_BondedDevice_t)));
    PRINTS("Erase == ");
    PRINT_HEX(&err_code, 1);
    PRINTS("\r\n");
    return err_code;
}

//only call on init
static uint32_t load_all_from_flash(){
    ANT_BondedDevice_t dev;
    uint32_t err;
    do{
        err = bonding_info_load_from_flash(&dev, &self.m_bond_info_in_flash_count);
    }while(err == NRF_SUCCESS);
    PRINTS("ANT BOND COUNT = ");
    PRINT_HEX(&self.m_bond_info_in_flash_count, 2);
    return err;
}
static void bm_pstorage_cb_handler(pstorage_handle_t * handle,
                                   uint8_t             op_code,
                                   uint32_t            result,
                                   uint8_t           * p_data,
                                   uint32_t            data_len){

    PRINTS("H ");
    PRINT_HEX(handle, 4);
    PRINTS("X ");
    PRINT_HEX(&op_code, 1);
    PRINTS("D ");
    PRINT_HEX(p_data, 4);
    PRINTS("\r\n");
}
