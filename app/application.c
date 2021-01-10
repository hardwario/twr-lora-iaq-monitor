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
twr_led_t led;
// Button instance
twr_button_t button;
// Lora instance
twr_cmwx1zzabz_t lora;
// VOC-LP tag instance
twr_tag_voc_lp_t voc_lp;
// Humidity tag instance
twr_tag_humidity_t humidity_tag;
// Barometer tag instance
twr_tag_barometer_t barometer;

TWR_DATA_STREAM_FLOAT_BUFFER(sm_temperature_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_co2_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL_CO2))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_voc_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL_VOC))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_humidity_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_pressure_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL_BAROMETER))
TWR_DATA_STREAM_FLOAT_BUFFER(sm_voltage_buffer, 8)

twr_data_stream_t sm_temperature;
twr_data_stream_t sm_co2;
twr_data_stream_t sm_voc;
twr_data_stream_t sm_humidity;
twr_data_stream_t sm_pressure;
twr_data_stream_t sm_voltage;

twr_scheduler_task_id_t battery_measure_task_id;

enum {
    HEADER_BOOT         = 0x00,
    HEADER_UPDATE       = 0x01,
    HEADER_BUTTON_CLICK = 0x02,
    HEADER_BUTTON_HOLD  = 0x03,

} header = HEADER_BOOT;

twr_scheduler_task_id_t calibration_task_id = 0;
int calibration_counter;

config_t initial_config = { 2 };
config_t config;

void calibration_task(void *param);

void calibration_start()
{
    calibration_counter = 32;

    twr_led_set_mode(&led, TWR_LED_MODE_BLINK_FAST);
    calibration_task_id = twr_scheduler_register(calibration_task, NULL, twr_tick_get() + CALIBRATION_START_DELAY);
    twr_atci_printf("$CO2_CALIBRATION: \"START\"");

}

void calibration_stop()
{
    if (!calibration_task_id)
    {
        return;
    }

    twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    twr_scheduler_unregister(calibration_task_id);
    calibration_task_id = 0;

    twr_module_co2_set_update_interval(MEASURE_INTERVAL_CO2);
    twr_atci_printf("$CO2_CALIBRATION: \"STOP\"");

}

void calibration_task(void *param)
{
    (void) param;

    twr_led_set_mode(&led, TWR_LED_MODE_BLINK_SLOW);

    twr_atci_printf("$CO2_CALIBRATION_COUNTER: \"%d\"", calibration_counter);


    twr_module_co2_set_update_interval(CALIBRATION_MEASURE_INTERVAL);
    twr_module_co2_calibration(TWR_LP8_CALIBRATION_BACKGROUND_FILTERED);

    calibration_counter--;

    if (calibration_counter == 0)
    {
        calibration_stop();
    }

    twr_scheduler_plan_current_relative(CALIBRATION_MEASURE_INTERVAL);
}
  
void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    if (event == TWR_BUTTON_EVENT_CLICK)
    {
        header = HEADER_BUTTON_CLICK;

        twr_scheduler_plan_now(0);
    }
    else if (event == TWR_BUTTON_EVENT_HOLD)
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

void co2_module_event_handler(twr_module_co2_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float value;

    twr_log_debug("CO2 MEASSUREMENT");

    if (twr_module_co2_get_concentration_ppm(&value))
    {
        twr_data_stream_feed(&sm_co2, &value);
    }
    else
    {
        twr_data_stream_reset(&sm_co2);
    }
}

void voc_lp_tag_event_handler(twr_tag_voc_lp_t *self, twr_tag_voc_lp_event_t event, void *event_param)
{
    
    if (event == TWR_TAG_VOC_LP_EVENT_UPDATE)
    {
        twr_log_debug("VOC MEASUREMENT");
        uint16_t value;

        if (twr_tag_voc_lp_get_tvoc_ppb(self, &value))
        {
            float v = value;
            twr_data_stream_feed(&sm_voc, &v);
        }
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        twr_log_debug("BATTERY MEASUREMENT");
        float voltage = NAN;

        twr_module_battery_get_voltage(&voltage);

        twr_data_stream_feed(&sm_voltage, &voltage);
    }
}

void humidity_tag_event_handler(twr_tag_humidity_t *self, twr_tag_humidity_event_t event, void *event_param)
{
    float value;

    if (event != TWR_TAG_HUMIDITY_EVENT_UPDATE)
    {
        return;
    }

    if (twr_tag_humidity_get_humidity_percentage(self, &value))
    {
        twr_log_debug("HUMIDITY MEASUREMENT");
        twr_data_stream_feed(&sm_humidity, &value);
    }

    if(twr_tag_humidity_get_temperature_celsius(self, &value))
    {
        twr_log_debug("TEMPERATURE MEASUREMENT");

        twr_data_stream_feed(&sm_temperature, &value);
    }
    
}

void barometer_tag_event_handler(twr_tag_barometer_t *self, twr_tag_barometer_event_t event, void *event_param)
{
    float pascal;

    twr_log_debug("BAROMETER MEASUREMENT");

    if (event != TWR_TAG_BAROMETER_EVENT_UPDATE)
    {
        return;
    }

    if (!twr_tag_barometer_get_pressure_pascal(self, &pascal))
    {
        return;
    }

    twr_data_stream_feed(&sm_pressure, &pascal);
}

void lora_callback(twr_cmwx1zzabz_t *self, twr_cmwx1zzabz_event_t event, void *event_param)
{
    if (event == TWR_CMWX1ZZABZ_EVENT_ERROR)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_BLINK_FAST);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_ON);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_READY)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        twr_atci_printf("$JOIN_OK");
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        twr_atci_printf("$JOIN_ERROR");
    }
}

static void humidity_tag_init(twr_tag_humidity_revision_t revision, twr_i2c_channel_t i2c_channel, humidity_tag_t *tag)
{
    memset(tag, 0, sizeof(*tag));

    if (revision == TWR_TAG_HUMIDITY_REVISION_R1)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == TWR_TAG_HUMIDITY_REVISION_R2)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R2_I2C0_ADDRESS_DEFAULT;
    }
    else if (revision == TWR_TAG_HUMIDITY_REVISION_R3)
    {
        tag->param.channel = TWR_RADIO_PUB_CHANNEL_R3_I2C0_ADDRESS_DEFAULT;
    }
    else
    {
        return;
    }

    if (i2c_channel == TWR_I2C_I2C1)
    {
        tag->param.channel |= 0x80;
    }

    twr_tag_humidity_init(&tag->self, revision, i2c_channel, TWR_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);

    twr_tag_humidity_set_update_interval(&tag->self, MEASURE_INTERVAL);

    twr_tag_humidity_set_event_handler(&tag->self, humidity_tag_event_handler, &tag->param);
}

bool at_send(void)
{
    twr_scheduler_plan_now(0);

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
        twr_data_stream_t *stream;
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

        if (twr_data_stream_get_average(values[i].stream, &value_avg))
        {
            twr_atci_printf("$STATUS: \"%s\",%.*f", values[i].name, values[i].precision, value_avg);
        }
        else
        {
            twr_atci_printf("$STATUS: \"%s\",", values[i].name);
        }
    }

    return true;
}


void application_init(void)
{

    twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    // load configuration from EEPROM
    twr_config_init(0x1, &config, sizeof(config), &initial_config);

    twr_data_stream_init(&sm_temperature, 1, &sm_temperature_buffer);
    twr_data_stream_init(&sm_co2, 1, &sm_co2_buffer);
    twr_data_stream_init(&sm_voc, 1, &sm_voc_buffer);
    twr_data_stream_init(&sm_humidity, 1, &sm_humidity_buffer);
    twr_data_stream_init(&sm_pressure, 1, &sm_pressure_buffer);
    twr_data_stream_init(&sm_voltage, 1, &sm_voltage_buffer);

    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_ON);

    // Initilize CO2
    twr_module_co2_init();
    twr_module_co2_set_update_interval(MEASURE_INTERVAL_CO2);
    twr_module_co2_set_event_handler(co2_module_event_handler, NULL);

    // Initialize button
    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(MEASURE_INTERVAL);

    // Initialize VOC-LP Tag
    twr_tag_voc_lp_init(&voc_lp, TWR_I2C_I2C0);
    twr_tag_voc_lp_set_event_handler(&voc_lp, voc_lp_tag_event_handler, NULL);
    twr_tag_voc_lp_set_update_interval(&voc_lp, MEASURE_INTERVAL_VOC);

    // Initialize Barometer Tag
    twr_tag_barometer_init(&barometer, TWR_I2C_I2C0);
    twr_tag_barometer_set_update_interval(&barometer, MEASURE_INTERVAL_BAROMETER);
    twr_tag_barometer_set_event_handler(&barometer, barometer_tag_event_handler, NULL);

    // Hudmidity
    static humidity_tag_t humidity_tag_0_0;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R1, TWR_I2C_I2C0, &humidity_tag_0_0);

    static humidity_tag_t humidity_tag_0_2;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R2, TWR_I2C_I2C0, &humidity_tag_0_2);

    static humidity_tag_t humidity_tag_0_4;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R3, TWR_I2C_I2C0, &humidity_tag_0_4);

    static humidity_tag_t humidity_tag_1_0;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R1, TWR_I2C_I2C1, &humidity_tag_1_0);

    static humidity_tag_t humidity_tag_1_2;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R2, TWR_I2C_I2C1, &humidity_tag_1_2);

    static humidity_tag_t humidity_tag_1_4;
    humidity_tag_init(TWR_TAG_HUMIDITY_REVISION_R3, TWR_I2C_I2C1, &humidity_tag_1_4);

    // Initialize lora module
    twr_cmwx1zzabz_init(&lora, TWR_UART_UART1);
    twr_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    twr_cmwx1zzabz_set_mode(&lora, TWR_CMWX1ZZABZ_CONFIG_MODE_ABP);
    twr_cmwx1zzabz_set_class(&lora, TWR_CMWX1ZZABZ_CONFIG_CLASS_A);
    twr_cmwx1zzabz_set_port(&lora, config.lora_port);
    sprintf(lora_port_help, "Port: %d", config.lora_port);


    at_init(&led, &lora);
    static const twr_atci_command_t commands[] = {
            AT_LORA_COMMANDS,
            {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
            {"$CALIBRATION", at_calibration, NULL, NULL, NULL, "Immediately send packet"},
            {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
            AT_LED_COMMANDS,
            TWR_ATCI_COMMAND_CLAC,
            TWR_ATCI_COMMAND_HELP
    };
    twr_atci_init(commands, TWR_ATCI_COMMANDS_LENGTH(commands));

    twr_log_debug("INIT");
    twr_scheduler_plan_current_relative(10 * 1000);
}

void application_task(void)
{
    if (!twr_cmwx1zzabz_is_ready(&lora))
    {
        twr_scheduler_plan_current_relative(100);

        return;
    }
    twr_log_debug("TASK");

    static uint8_t buffer[11];

    memset(buffer, 0xff, sizeof(buffer));

    buffer[0] = header;

    float voltage_avg = NAN;

    twr_data_stream_get_average(&sm_voltage, &voltage_avg);

    if (!isnan(voltage_avg))
    {
        buffer[1] = ceil(voltage_avg * 10.f);
    }

    float temperature_avg = NAN;

    twr_data_stream_get_average(&sm_temperature, &temperature_avg);

    if (!isnan(temperature_avg))
    {
        int16_t temperature_i16 = (int16_t) (temperature_avg * 10.f);

        buffer[2] = temperature_i16 >> 8;
        buffer[3] = temperature_i16;
    }

    float humidity_avg = NAN;

    twr_data_stream_get_average(&sm_humidity, &humidity_avg);

    if (!isnan(humidity_avg))
    {
        buffer[4] = humidity_avg * 2;
    }

    float voc_avg = NAN;

    twr_data_stream_get_average(&sm_voc, &voc_avg);

    if (!isnan(voc_avg))
    {
        uint16_t value = (uint16_t) voc_avg;
        buffer[5] = value >> 8;
        buffer[6] = value;
    }

    float pressure_avg = NAN;

    twr_data_stream_get_average(&sm_pressure, &pressure_avg);

    if (!isnan(pressure_avg))
    {
        uint16_t value = pressure_avg / 2.f;
        buffer[7] = value >> 8;
        buffer[8] = value;
    }

    float co2_avg = NAN;

    twr_data_stream_get_average(&sm_co2, &co2_avg);

    if (!isnan(co2_avg))
    {
        uint16_t value = co2_avg;
        buffer[9] = value >> 8;
        buffer[10] = value;
    }

    twr_cmwx1zzabz_send_message(&lora, buffer, sizeof(buffer));

    static char tmp[sizeof(buffer) * 2 + 1];
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
        sprintf(tmp + i * 2, "%02x", buffer[i]);
    }

    twr_atci_printf("$SEND: %s", tmp);

    twr_log_debug("TASK DONE");

    header = HEADER_UPDATE;
    twr_scheduler_plan_current_relative(SEND_DATA_INTERVAL);

}
