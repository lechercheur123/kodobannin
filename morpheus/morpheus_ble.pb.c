/* Automatically generated nanopb constant definitions */
/* Generated by nanopb-0.2.8 at Tue Aug 19 15:42:48 2014. */

#include "morpheus_ble.pb.h"



const pb_field_t WifiEndPoint_fields[3] = {
    PB_FIELD2(  1, STRING  , OPTIONAL, CALLBACK, FIRST, WifiEndPoint, name, name, 0),
    PB_FIELD2(  2, BYTES   , REQUIRED, CALLBACK, OTHER, WifiEndPoint, mac, name, 0),
    PB_LAST_FIELD
};

const pb_field_t SelectedWifiEndPoint_fields[3] = {
    PB_FIELD2(  1, MESSAGE , OPTIONAL, STATIC  , FIRST, SelectedWifiEndPoint, endPoint, endPoint, &WifiEndPoint_fields),
    PB_FIELD2(  2, STRING  , OPTIONAL, CALLBACK, OTHER, SelectedWifiEndPoint, password, endPoint, 0),
    PB_LAST_FIELD
};

const pb_field_t MorpheusCommand_fields[5] = {
    PB_FIELD2(  1, INT32   , REQUIRED, STATIC  , FIRST, MorpheusCommand, version, version, 0),
    PB_FIELD2(  2, ENUM    , REQUIRED, STATIC  , OTHER, MorpheusCommand, type, version, 0),
    PB_FIELD2(  3, MESSAGE , OPTIONAL, STATIC  , OTHER, MorpheusCommand, selectedWIFIEndPoint, type, &SelectedWifiEndPoint_fields),
    PB_FIELD2(  4, BYTES   , OPTIONAL, CALLBACK, OTHER, MorpheusCommand, deviceId, selectedWIFIEndPoint, 0),
    PB_LAST_FIELD
};


/* Check that field information fits in pb_field_t */
#if !defined(PB_FIELD_32BIT)
/* If you get an error here, it means that you need to define PB_FIELD_32BIT
 * compile-time option. You can do that in pb.h or on compiler command line.
 * 
 * The reason you need to do this is that some of your messages contain tag
 * numbers or field sizes that are larger than what can fit in 8 or 16 bit
 * field descriptors.
 */
STATIC_ASSERT((pb_membersize(SelectedWifiEndPoint, endPoint) < 65536 && pb_membersize(MorpheusCommand, selectedWIFIEndPoint) < 65536), YOU_MUST_DEFINE_PB_FIELD_32BIT_FOR_MESSAGES_WifiEndPoint_SelectedWifiEndPoint_MorpheusCommand)
#endif

#if !defined(PB_FIELD_16BIT) && !defined(PB_FIELD_32BIT)
/* If you get an error here, it means that you need to define PB_FIELD_16BIT
 * compile-time option. You can do that in pb.h or on compiler command line.
 * 
 * The reason you need to do this is that some of your messages contain tag
 * numbers or field sizes that are larger than what can fit in the default
 * 8 bit descriptors.
 */
STATIC_ASSERT((pb_membersize(SelectedWifiEndPoint, endPoint) < 256 && pb_membersize(MorpheusCommand, selectedWIFIEndPoint) < 256), YOU_MUST_DEFINE_PB_FIELD_16BIT_FOR_MESSAGES_WifiEndPoint_SelectedWifiEndPoint_MorpheusCommand)
#endif


