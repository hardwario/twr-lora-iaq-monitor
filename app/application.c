#include <application.h>
#include <at.h>

#define SEND_DATA_INTERVAL          (15 * 60 * 1000)
#define MEASURE_INTERVAL            (1 * 60 * 1000)
#define MEASURE_INTERVAL_BAROMETER  (5 * 60 * 1000)
#define MEASURE_INTERVAL_CO2        (5 * 60 * 1000)
#define MEASURE_INTERVAL_VOC        (5 * 60 * 1000)

#define CALIBRATION_START_DELAY (15 * 60 * 1000)
#define CALIBRATION_MEASURE_INTERVAL (2 * 60 * 1000)

// LED instance
bc_led_t led;
// Button instance
bc_button_t button;
// Lora instance
bc_cmwx1zzabz_t lora;
// VOC-LP tag instance
bc_tag_voc_lp_t voc_lp;
// Humidity tag instance
bc_tag_humidity_t humidity_tag;
// Barometer tag instance
bc_tag_barometer_t barometer;

BC_DATA_STREAM_FLOAT_BUFFER(sm_temperature_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_co2_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL_CO2))
BC_DATA_STREAM_FLOAT_BUFFER(sm_voc_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL_VOC))
BC_DATA_STREAM_FLOAT_BUFFER(sm_humidity_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
BC_DATA_STREAM_FLOAT_BUFFER(sm_pressure_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL_BAROMETER))
BC_DATA_STREAM_FLOAT_BUFFER(sm_voltage_buffer, 8)

bc_data_stream_t sm_temperature;
bc_data_stream_t sm_co2;
bc_data_stream_t sm_voc;
bc_data_stream_t sm_humidity;
bc_data_stream_t sm_pressure;
bc_data_stream_t sm_voltage;

bc_scheduler_task_id_t battery_measure_task_id;

enum {
    HEADER_BOOT         = 0x00,
    HEADER_UPDATE       = 0x01,
    HEADER_BUTTON_CLICK = 0x02,
    HEADER_BUTTON_HOLD  = 0x03,

} header = HEADER_BOOT;

bc_scheduler_task_id_t calibration_task_id = 0;
int calibration_counter;


void calibration_task(void *param);

void calibration_start()
{
    calibration_counter = 32;

    bc_led_set_mode(&led, BC_LED_MODE_BLINK_FAST);
    calibration_task_id = bc_scheduler_register(calibration_task, NULL, bc_tick_get() + CALIBRATION_START_DELAY);
    bc_atci_printf("$CO2_CALIBRATION: \"START\"");

}

void calibration_stop()
{
    if (!calibration_task_id)
    {
        return;
    }

    bc_led_set_mode(&led, BC_LED_MODE_OFF);
    bc_scheduler_unregister(calibration_task_id);
    calibration_task_id = 0;

    bc_module_co2_set_update_interval(MEASURE_INTERVAL_CO2);
    bc_atci_printf("$CO2_CALIBRATION: \"STOP\"");

}

void calibration_task(void *param)
{
    (void) param;

    bc_led_set_mode(&led, BC_LED_MODE_BLINK_SLOW);

    bc_atci_printf("$CO2_CALIBRATION_COUNTER: \"%d\"", calibration_counter);


    bc_module_co2_set_update_interval(CALIBRATION_MEASURE_INTERVAL);
    bc_module_co2_calibration(BC_LP8_CALIBRATION_BACKGROUND_FILTERED);

    calibration_counter--;

    if (calibration_counter == 0)
    {
        calibration_stop();
    }

    bc_scheduler_plan_current_relative(CALIBRATION_MEASURE_INTERVAL);
}
  
void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_CLICK)
    {
        header = HEADER_BUTTON_CLICK;

        bc_scheduler_plan_now(0);
    }
    else if (event == BC_BUTTON_EVENT_HOLD)
    {
        if (!calibration_task_id)
        {
            calibration_start();
        }
        else
        {
            calibration_stop();
        }
    }
}

void co2_module_event_handler(bc_module_co2_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float value;

    bc_log_debug("CO2 MEASSUREMENT");

    if (bc_module_co2_get_concentration_ppm(&value))
    {
        bc_data_stream_feed(&sm_co2, &value);
    }
    else
    {
        bc_data_stream_reset(&sm_co2);
    }
}

void voc_lp_tag_event_handler(bc_tag_voc_lp_t *self, bc_tag_voc_lp_event_t event, void *event_param)
{
    
    if (event == BC_TAG_VOC_LP_EVENT_UPDATE)
    {
        bc_log_debug("VOC MEASUREMENT");
        uint16_t value;

        if (bc_tag_voc_lp_get_tvoc_ppb(self, &value))
        {
            float v = value;
            bc_data_stream_feed(&sm_voc, &v);
        }
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    if (event == BC_MODULE_BATTERY_EVENT_UPDATE)
    {
        bc_log_debug("BATTERY MEASUREMENT");
        float voltage = NAN;

        bc_module_battery_get_voltage(&voltage);

        bc_data_stream_feed(&sm_voltage, &voltage);
    }
}

void humidity_tag_event_handler(bc_tag_humidity_t *self, bc_tag_humidity_event_t event, void *event_param)
{
    float value;

    if (event != BC_TAG_HUMIDITY_EVENT_UPDATE)
    {
        return;
    }

    if (bc_tag_humidity_get_humidity_percentage(self, &value))
    {
        bc_log_debug("HUMIDITY MEASUREMENT");
        bc_data_stream_feed(&sm_humidity, &value);
    }

    if(bc_tag_humidity_get_temperature_celsius(self, &value))
    {
        bc_log_debug("TEMPERATURE MEASUREMENT");

        bc_data_stream_feed(&sm_temperature, &value);
    }
    
}

void barometer_tag_event_handler(bc_tag_barometer_t *self, bc_tag_barometer_event_t event, void *event_param)
{
    float pascal;

    bc_log_debug("BAROMETER MEASUREMENT");

    if (event != BC_TAG_BAROMETER_EVENT_UPDATE)
    {
        return;
    }

    if (!bc_tag_barometer_get_pressure_pascal(self, &pascal))
    {
        return;
    }

    bc_data_stream_feed(&sm_pressure, &pascal);
}

void lora_callback(bc_cmwx1zzabz_t *self, bc_cmwx1zzabz_event_t event, void *event_param)
{
    if (event == BC_CMWX1ZZABZ_EVENT_ERROR)
    {
        bc_led_set_mode(&led, BC_LED_MODE_BLINK_FAST);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        bc_led_set_mode(&led, BC_LED_MODE_ON);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_READY)
    {
        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        bc_atci_printf("$JOIN_OK");
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        bc_atci_printf("$JOIN_ERROR");
    }
}

static void humidity_tag_init(bc_tag_humidity_revision_t revision, bc_i2c_channel_t i2c_channel, humidity_tag_t *tag)
{
    memset(tag, 0, sizeof(*tag));

    if (revision == BC_TAG_HUMIDITY_REVISION_R1)
    {
        tag->param.channel = BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == BC_TAG_HUMIDITY_REVISION_R2)
    {
        tag->param.channel = BC_RADIO_PUB_CHANNEL_R2_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == BC_TAG_HUMIDITY_REVISION_R3)
    {
        tag->param.channel = BC_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT;
    }
    else
    {
        return;
    }

    if (i2c_channel == BC_I2C_I2C1)
    {
        tag->param.channel |= 0x80;
    }

    bc_tag_humidity_init(&tag->self, revision, i2c_channel, BC_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);

    bc_tag_humidity_set_update_interval(&tag->self, MEASURE_INTERVAL);

    bc_tag_humidity_set_event_handler(&tag->self, humidity_tag_event_handler, &tag->param);
}

bool at_send(void)
{
    bc_scheduler_plan_now(0);

    return true;
}

bool at_calibration(void)
{
    if (calibration_task_id)
    {
        calibration_stop();
    }
    else
    {
        calibration_start();
    }

    return true;
}

bool at_status(void)
{
    float value_avg = NAN;

    static const struct {
        bc_data_stream_t *stream;
        const char *name;
        int precision;
    } values[] = {
            {&sm_voltage, "Voltage", 1},
            {&sm_temperature, "Temperature", 1},
            {&sm_humidity, "Humidity", 1},
            {&sm_voc, "VOC", 1},
            {&sm_pressure, "Pressure", 0},
            {&sm_co2, "CO2", 0},
    };

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
    {
        value_avg = NAN;

        if (bc_data_stream_get_average(values[i].stream, &value_avg))
        {
            bc_atci_printf("$STATUS: \"%s\",%.*f", values[i].name, values[i].precision, value_avg);
        }
        else
        {
            bc_atci_printf("$STATUS: \"%s\",", values[i].name);
        }
    }

    return true;
}


void application_init(void)
{

    bc_log_init(BC_LOG_LEVEL_DUMP, BC_LOG_TIMESTAMP_ABS);
    bc_data_stream_init(&sm_temperature, 1, &sm_temperature_buffer);
    bc_data_stream_init(&sm_co2, 1, &sm_co2_buffer);
    bc_data_stream_init(&sm_voc, 1, &sm_voc_buffer);
    bc_data_stream_init(&sm_humidity, 1, &sm_humidity_buffer);
    bc_data_stream_init(&sm_pressure, 1, &sm_pressure_buffer);
    bc_data_stream_init(&sm_voltage, 1, &sm_voltage_buffer);

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_ON);

    // Initilize CO2
    bc_module_co2_init();
    bc_module_co2_set_update_interval(MEASURE_INTERVAL_CO2);
    bc_module_co2_set_event_handler(co2_module_event_handler, NULL);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize battery
    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(MEASURE_INTERVAL);

    // Initialize VOC-LP Tag
    bc_tag_voc_lp_init(&voc_lp, BC_I2C_I2C0);
    bc_tag_voc_lp_set_event_handler(&voc_lp, voc_lp_tag_event_handler, NULL);
    bc_tag_voc_lp_set_update_interval(&voc_lp, MEASURE_INTERVAL_VOC);

    // Initialize Barometer Tag
    bc_tag_barometer_init(&barometer, BC_I2C_I2C0);
    bc_tag_barometer_set_update_interval(&barometer, MEASURE_INTERVAL_BAROMETER);
    bc_tag_barometer_set_event_handler(&barometer, barometer_tag_event_handler, NULL);

    // Hudmidity
    static humidity_tag_t humidity_tag_0_0;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R1, BC_I2C_I2C0, &humidity_tag_0_0);

    static humidity_tag_t humidity_tag_0_2;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R2, BC_I2C_I2C0, &humidity_tag_0_2);

    static humidity_tag_t humidity_tag_0_4;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R3, BC_I2C_I2C0, &humidity_tag_0_4);

    static humidity_tag_t humidity_tag_1_0;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R1, BC_I2C_I2C1, &humidity_tag_1_0);

    static humidity_tag_t humidity_tag_1_2;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R2, BC_I2C_I2C1, &humidity_tag_1_2);

    static humidity_tag_t humidity_tag_1_4;
    humidity_tag_init(BC_TAG_HUMIDITY_REVISION_R3, BC_I2C_I2C1, &humidity_tag_1_4);

    // Initialize lora module
    bc_cmwx1zzabz_init(&lora, BC_UART_UART1);
    bc_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    bc_cmwx1zzabz_set_mode(&lora, BC_CMWX1ZZABZ_CONFIG_MODE_ABP);
    bc_cmwx1zzabz_set_class(&lora, BC_CMWX1ZZABZ_CONFIG_CLASS_A);

    at_init(&led, &lora);
    static const bc_atci_command_t commands[] = {
            AT_LORA_COMMANDS,
            {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
            {"$CALIBRATION", at_calibration, NULL, NULL, NULL, "Immediately send packet"},
            {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
            AT_LED_COMMANDS,
            BC_ATCI_COMMAND_CLAC,
            BC_ATCI_COMMAND_HELP
    };
    bc_atci_init(commands, BC_ATCI_COMMANDS_LENGTH(commands));

    bc_log_debug("INIT");
    bc_scheduler_plan_current_relative(10 * 1000);
}

void application_task(void)
{
    if (!bc_cmwx1zzabz_is_ready(&lora))
    {
        bc_scheduler_plan_current_relative(100);

        return;
    }
    bc_log_debug("TASK");

    static uint8_t buffer[11];

    memset(buffer, 0xff, sizeof(buffer));

    buffer[0] = header;

    float voltage_avg = NAN;

    bc_data_stream_get_average(&sm_voltage, &voltage_avg);

    if (!isnan(voltage_avg))
    {
        buffer[1] = ceil(voltage_avg * 10.f);
    }

    float temperature_avg = NAN;

    bc_data_stream_get_average(&sm_temperature, &temperature_avg);

    if (!isnan(temperature_avg))
    {
        int16_t temperature_i16 = (int16_t) (temperature_avg * 10.f);

        buffer[2] = temperature_i16 >> 8;
        buffer[3] = temperature_i16;
    }

    float humidity_avg = NAN;

    bc_data_stream_get_average(&sm_humidity, &humidity_avg);

    if (!isnan(humidity_avg))
    {
        buffer[4] = humidity_avg * 2;
    }

    float voc_avg = NAN;

    bc_data_stream_get_average(&sm_voc, &voc_avg);

    if (!isnan(voc_avg))
    {
        uint16_t value = (uint16_t) voc_avg;
        buffer[5] = value >> 8;
        buffer[6] = value;
    }

    float pressure_avg = NAN;

    bc_data_stream_get_average(&sm_pressure, &pressure_avg);

    if (!isnan(pressure_avg))
    {
        uint16_t value = pressure_avg / 2.f;
        buffer[7] = value >> 8;
        buffer[8] = value;
    }

    float co2_avg = NAN;

    bc_data_stream_get_average(&sm_co2, &co2_avg);

    if (!isnan(co2_avg))
    {
        uint16_t value = co2_avg;
        buffer[9] = value >> 8;
        buffer[10] = value;
    }

    bc_cmwx1zzabz_send_message(&lora, buffer, sizeof(buffer));

    static char tmp[sizeof(buffer) * 2 + 1];
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
        sprintf(tmp + i * 2, "%02x", buffer[i]);
    }

    bc_atci_printf("$SEND: %s", tmp);

    bc_log_debug("TASK DONE");

    header = HEADER_UPDATE;
    bc_scheduler_plan_current_relative(SEND_DATA_INTERVAL);

}
