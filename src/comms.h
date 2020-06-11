#include <stdio.h>

#define SSLA_TOPIC              "conversation"

enum request_type
{   PUBLISH_ALL = 1,
    LED_ON,
    LED_OFF,
    BUZZER_ON,
    BUZZER_OFF,
    MOSFET1_ON,
    MOSFET1_OFF,
    MOSFET2_ON,
    MOSFET2_OFF,
    MOSFET3_ON,
    MOSFET3_OFF,
    MOSFET4_ON,
    MOSFET4_OFF };

struct __attribute__((__packed__)) request
{
    int  src_device_id;                       // Application addr will be 0000. Broadcast address will be 1000
    int  dest_device_id;                      // destination addr of the thingy:91 or IoT device usually last 4 digits of IMEI
    enum request_type tag;                    // Requested message type
    int  length;                              // Requested length
    char *value;                              // Requested Value or data
};

struct __attribute__((__packed__)) response
{
    int src_device_id;                                     // Application addr will be 0000. Broadcast address will be 1000
    int dest_device_id;                                    // destination addr of the thingy:91 or IoT device usually last 4 digits of IMEI
    struct sensor_value temp, press, humidity, gas_res;    // temperature, air pressure, humidity, gas values
    // please add remaing structures for remaining sensors here
};
