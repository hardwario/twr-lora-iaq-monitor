#ifndef _APPLICATION_H
#define _APPLICATION_H

#ifndef VERSION
#define VERSION "vdev"
#endif

#include <twr.h>

typedef struct
{
    uint8_t channel;
    float value;
    twr_tick_t next_pub;

} event_param_t;

typedef struct
{
    twr_tag_humidity_t self;
    event_param_t param;

} humidity_tag_t;

#define CONFIG_STRUCT_VER 1
typedef struct config_t
{
    uint16_t lora_port;
} config_t;

extern config_t initial_config;
extern config_t config;
#endif // _APPLICATION_H
