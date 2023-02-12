/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdio.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_vfs.h"
#include "nvs_flash.h"
#include "math.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "json_generator.h"
#include "json_parser.h"
#include "esp_timer.h"
// External Flash Stuff
#include "esp_flash.h"
#include "esp_partition.h"
// #include "soc/spi_pins.h"
#include "ds18b20.c"
#include "esp_adc_cal.h"
#include "app_http_server.h"
#include "wifi_connecting.h"
#include <stdio.h>
#include <stdlib.h>
#include "esp_system.h"
#include <driver/adc.h>
// #include "esp_adc_cal.h"
static struct example_info_store
{
    int mode_led;
    int state_led;
} __attribute__((packed)) store;
esp_err_t err;
nvs_handle_t my_handle;
static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "Node1";
#define D1 GPIO_NUM_25 // các chân của led7segment
#define D2 GPIO_NUM_27
#define D3 GPIO_NUM_5
#define D4 GPIO_NUM_21

#define A GPIO_NUM_26
#define B GPIO_NUM_13
#define C GPIO_NUM_18
#define D GPIO_NUM_16
#define E GPIO_NUM_4
#define F GPIO_NUM_15
#define G GPIO_NUM_19
#define DP GPIO_NUM_17

#define INPUT_PIN 0
static EventGroupHandle_t wifi_event_group;
int state = 0;
xQueueHandle interputQueue; // xQueueHandle cho ngắt ngoài

float Temperature = 0;
uint8_t Presence = 0;
uint8_t Temp_byte1, Temp_byte2;
uint16_t TEMP;
static const char *TAG_MQTT = "MQTT_EXAMPLE";
static esp_mqtt_client_handle_t client = NULL;
static TaskHandle_t loopHandle = NULL;

char message_mqtt[20];
#define DEFAULT_VREF 1100
#define NO_OF_SAMPLES 64 // Multisampling
static esp_adc_cal_characteristics_t *adc_chars;
static esp_adc_cal_characteristics_t *adc_chars_mq02;
static const adc_channel_t channel = ADC1_CHANNEL_0;      // GPIO
static const adc_channel_t channel_mq02 = ADC1_CHANNEL_3; // GPIO
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11; // 3.3V range

float ADC_value = 0.00080566406, LDR_value = 0.0;
int lux = 0;
typedef struct
{
    char buf[256];
    size_t offset;
} json_gen_test_result_t;

static json_gen_test_result_t result;

typedef struct
{
    int Device;
    char Tem[10];
    int Lux;
    int Gas_Check;
} data_sensor_t;
char *json_gen(json_gen_test_result_t *result, char *key0, int *value0, char *key1, char *value1, // dong goi thanh json
               char *key2, int value2, char *key3, int value3);
static data_sensor_t data_sensor;

#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

static char data_tx[30];

/* FreeRTOS event group to signal when we are connected*/
EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t mqtt_event_data_rx;
// Các cờ bit trong EventGroup
const int CONNECTED_BIT = BIT8;
int WIFI_RECV_INFO = BIT0;
int WIFI_CONNECTED_BIT = BIT1;
int WIFI_FAIL_BIT = BIT2;
int MQTT_EVENT_DATA_RX = BIT3;
int MQTT_EVENT_CONNECT = BIT4;
const int MESSAGE_ARRIVE_BIT = BIT5;
const int MESSAGE_TX_ARRIVE_BIT = BIT6;
int MESSAGE_BIT = BIT7;
typedef enum
{
    INITIAL_STATE,
    NORMAL_STATE,
    LOST_WIFI_STATE,
    CHANGE_PASSWORD_STATE,

} wifi_state_t;

struct wifi_info_t // struct gồm các thông tin của wifi
{
    char SSID[20];
    char PASSWORD[10];
    char SSID_AP[15];
    char PASSWORD_AP[10];
    wifi_state_t state;
} __attribute__((packed)) wifi_info = {
    .SSID = "FPT",
    .PASSWORD = "toanluong",
    .SSID_AP = "local",
    .PASSWORD_AP = "12345678",
    .state = INITIAL_STATE,
};

static const char *TAG = "WSN";
static const char *TAG_TASK = "HELLO :";

static int s_retry_num = 0;
/**
 *  void nvs_save_wifiInfo(nvs_handle_t c_handle, const char *key, void *out_value)
 *  @brief lưu lại thông tin wifi từ nvs
 *
 *  @param[in] c_handle nvs_handle_t
 *  @param[in] key Key để lấy dữ liệu
 *  @param[in] value Data output
 *  @param[in] length chiều dài dữ liệu
 *  @return None
 */
void nvs_save_wifiInfo(nvs_handle_t c_handle, const char *key, const void *value, size_t length)
{
    esp_err_t err;
    nvs_open("storage0", NVS_READWRITE, &c_handle);
    // strcpy(wifi_info.SSID, "anhbung");
    nvs_set_blob(c_handle, key, value, length);
    err = nvs_commit(c_handle);
    if (err != ESP_OK)
        return err;

    // Close
    nvs_close(c_handle);
}
/**
 *  void nvs_get_wifiInfo(nvs_handle_t c_handle, const char *key, void *out_value)
 *  @brief Lấy lại trong tin wifi từ nvs
 *
 *  @param[in] c_handle nvs_handle_t
 *  @param[in] key Key để lấy dữ liệu
 *  @param[in] out_value Data output
 *  @return None
 */
void nvs_get_wifiInfo(nvs_handle_t c_handle, const char *key, void *out_value) // lấy thông tin wifi
{
    esp_err_t err;
    err = nvs_open("storage0", NVS_READWRITE, &c_handle);
    if (err != ESP_OK)
        return err;
    size_t required_size = 0; // value will default to 0, if not set yet in NVS
    err = nvs_get_blob(c_handle, NVS_KEY, NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return err;
    if (required_size == 0)
    {
        printf("Nothing saved yet!\n");
    }
    else
    {
        nvs_get_blob(c_handle, NVS_KEY, out_value, &required_size);

        err = nvs_commit(c_handle);
        if (err != ESP_OK)
            return err;

        // Close
        nvs_close(c_handle);
    }
}
/**
 *  void wifi_data_callback(char *data, int len)
 *  @brief Hàm này được gọi lại mỗi khi nhận được dữ liệu wifi local
 *
 *  @param[in] data dữ liệu
 *  @param[in] len chiều dài dữ liệu
 *  @return None
 */
void wifi_data_callback(char *data, int len) // ham nay xu ly thong tin wifi cau hinh ban dau
{
    char data_wifi[30];
    sprintf(data_wifi, "%.*s", len, data);
    printf("%.*s", len, data);
    char *pt = strtok(data_wifi, "/");
    strcpy(wifi_info.SSID, pt);
    pt = strtok(NULL, "/");
    strcpy(wifi_info.PASSWORD, pt);
    printf("\nssid: %s \n pass: %s\n", wifi_info.SSID, wifi_info.PASSWORD);
    nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info)); // luu lai
    xEventGroupSetBits(s_wifi_event_group, WIFI_RECV_INFO);                // set cờ
}
/**
 *  static esp_err_t nvs_flash_open(void)
 *  @brief Tạo file trong vùng nvs
 *
 *  @return None
 */
static esp_err_t nvs_flash_open(void)
{
    err = nvs_open("storage0", NVS_READWRITE, &my_handle);
    return err;
}
/**
 *  static void nvs_flash_read_stateLed(void)
 *  @brief Đọc dữ liệu state led từ nvs
 *
 *  @return None
 */
static int8_t nvs_flash_read_stateLed()
{
    int8_t data;
    if (nvs_flash_open() != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        printf("Done\n");

        // value will default to 0, if not set yet in NVS
        err = nvs_get_i8(my_handle, "stateLed", &data);
        switch (err)
        {
        case ESP_OK:
            printf("Done\n");
            printf("Restart stateLed = %d\n", data);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            printf("The value is not initialized yet!\n");
            break;
        default:
            printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
    }
    return data;
}
/**
 *  static void nvs_flash_read_modeLed(void)
 *  @brief Đọc dữ liệu mode led từ nvs
 *
 *  @return None
 */
static int8_t nvs_flash_read_modeLed(void) // doc du lieu tu nvs
{
    int8_t data;
    if (nvs_flash_open() != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        printf("Done\n");
        err = nvs_get_i8(my_handle, "modeLed", &data); // lay du lieu va luu vao data
        switch (err)
        {
        case ESP_OK:
            printf("Done\n");
            printf("Restart modeLed = %d\n", data);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            printf("The value is not initialized yet!\n");
            break;
        default:
            printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
    }
    return data;
}
/**
 *  static void nvs_flash_write_state(int8_t data)
 *  @brief Lưu lại state led vào nvs
 *
 *  @param[in] data Dữ liệu cần lưu
 *  @return None
 */
static void nvs_flash_write_state(int8_t data)
{
    nvs_open("storage0", NVS_READWRITE, &my_handle); // tao quyen readwrite cho file
    err = nvs_set_i8(my_handle, "stateLed", data);
    printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

    printf("Committing stateLed in NVS ... ");
    err = nvs_commit(my_handle);
    printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

    // Close
    nvs_close(my_handle);
}
/**
 *  static void nvs_flash_write_mode(int8_t data)
 *  @brief Lưu lại chế độ mode led vào nvs
 *
 *  @param[in] data Dữ liệu cần lưu
 *  @return None
 */
static void nvs_flash_write_mode(int8_t data)
{
    nvs_open("storage0", NVS_READWRITE, &my_handle);
    err = nvs_set_i8(my_handle, "modeLed", data); // kieu du lieu 8bit
    printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

    printf("Committing modeLed in NVS ... ");
    err = nvs_commit(my_handle); // xac nhan
    printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

    // Close
    nvs_close(my_handle);
}

static void flush_str(char *buf, void *priv)
{
    json_gen_test_result_t *result = (json_gen_test_result_t *)priv;
    if (result)
    {
        if (strlen(buf) > sizeof(result->buf) - result->offset)
        {
            printf("Result Buffer too small\r\n");
            return;
        }
        memcpy(result->buf + result->offset, buf, strlen(buf));
        result->offset += strlen(buf);
    }
}
/**
 *  int shmget(key_t key, size_t size, int shmflg)
 *  @brief Hàm được gọi mỗi khi sảy ra interrupt
 *
 *  @param[in] args tham số truyền vào
 *  @return None
 */
static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    xQueueSendFromISR(interputQueue, &pinNumber, NULL);
}
int check_mode_led = 0;
/**
 *  void Led_Local_Task(void *params)
 *  @brief Task thực hiện bật tắt led qua nút bấm được cấu hình interrupt
 *
 *  @param[in] pvParameter tham số truyền vào
 *  @return None
 */
void Led_Local_Task(void *params)
{
    int pinNumber, count = 0;
    while (true)
    {
        if (xQueueReceive(interputQueue, &pinNumber, portMAX_DELAY)) // doi su kien tu interrupt
        {
            gpio_set_level(22, 0);
            if (gpio_get_level(33) == 0)
            {
                printf("Led tat\n");
                store.state_led = 0;
                gpio_set_level(33, 1);
            }
            else if (gpio_get_level(33) == 1)
            {
                printf("Led bat\n");
                store.state_led = 1;
                gpio_set_level(33, 0);
            }
            nvs_flash_write_state(store.state_led);
        }
    }
    vTaskDelete(NULL);
}

int64_t start_time, end_time;
/**
 *  void Mqtt_Transmit_Task_Loop(void *pvParameter)
 *  @brief Task gửi dữ liệu đến gw
 *
 *  @param[in] pvParameter tham số truyền vào
 *  @return None
 * */
void Mqtt_Transmit_Task_Loop(void *pvParameter)
{
    while (1)
    {
        xEventGroupWaitBits(mqtt_event_data_rx, MQTT_EVENT_CONNECT, pdFALSE, pdFALSE, portMAX_DELAY); // đợi đến khi kết nối mqtt
        int msg_id = esp_mqtt_client_publish(client, "/smarthome/devices", result.buf, 0, 1, 0);
        vTaskDelay(5000 / portTICK_RATE_MS);
    }
}
/**
 *  void Periperal_Task_loop(void *pvParameter)
 *  @brief Task đọc các cảm biến và đóng gói thành chuỗi JSON
 *
 *  @param[in] pvParameter tham số truyền vào
 *  @return None
 */
void Periperal_Task_loop(void *pvParameter)
{
    char data[10];
    uint32_t adc_reading = 0;
    uint32_t adc_CH2_reading = 0;
    uint32_t mq02_value = 0;
    int Gas = 0;
    int node = 2;
    while (1)
    {
        Presence = DS18B20_Start();
        vTaskDelay(1 / (portTICK_RATE_MS));
        DS18B20_Write(0xCC); // skip ROM
        DS18B20_Write(0x44); // convert t
        vTaskDelay(750 / (portTICK_RATE_MS));
        Presence = DS18B20_Start();
        vTaskDelay(1 / (portTICK_RATE_MS));
        DS18B20_Write(0xCC); // skip ROM
        DS18B20_Write(0xBE); // Read Scratch-pad
        Temp_byte1 = DS18B20_Read();
        Temp_byte2 = DS18B20_Read();
        TEMP = (Temp_byte2 << 8) | Temp_byte1;
        Temperature = (float)TEMP / 16;
        sprintf(data, "%.2f", Temperature);
        // printf("%s\n", data);
        if (Temperature < 125)
        {
            // Multisampling measure
            for (int i = 0; i < NO_OF_SAMPLES; i++)
            {
                mq02_value += adc1_get_raw((adc1_channel_t)channel_mq02); // get value digital
            }
            mq02_value /= 64; // lay trung binh 64 lan sample

            if (mq02_value > 2200)
            {
                Gas = 1;
                gpio_set_level(22, 1); // bat coi bao
            }
            else
            {
                Gas = 0;
                gpio_set_level(22, 0); // bat coi bao
            }
            for (int i = 0; i < NO_OF_SAMPLES; i++)
            {
                LDR_value += adc1_get_raw((adc1_channel_t)channel); // get value digital
            }
            LDR_value /= 64;
            uint32_t voltage = 1;
            float resistorVoltage = ((float)LDR_value / 4096) * 3.3;
            // Since 5V is split between the 5 kohm resistor and the LDR, simply subtract the resistor voltage from 5V :
            float ldrVoltage = 3.3 - resistorVoltage;
            // The resistance of the LDR must be calculated based on the voltage (simple resistance calculation for a voltage divider circuit):
            float ldrResistance = ldrVoltage / resistorVoltage * 4700;
            int ldrLux = 12518931 * pow(ldrResistance, -1.405);
            if (store.mode_led == 1) // Auto Mode
            {
                if (ldrLux < 200)
                {
                    gpio_set_level(33, 0);
                }
                else
                {
                    gpio_set_level(33, 1);
                }
            }
            json_gen(&result, "Device", node, "Temperature", data, "illuminance", ldrLux, "Gas", Gas);
            // printf("%s\n", result.buf);
            vTaskDelay(5000 / portTICK_RATE_MS);
        }
        else
        {
            vTaskDelay(5000 / portTICK_RATE_MS);
        }
    }
    vTaskDelete(NULL);
}
/**
 *  void message_mqtt_rx_task(void *pvParameter)
 *  @brief Xử lý tin nhắn đến từ Broker
 *
 *  @param[in] pvParameter tham số truyền vào
 *  @return None
 */
void message_mqtt_rx_task(void *pvParameter) // task nhan tin nhan tu gw o che do internet
{
    char temp[5];
    char *ptr;
    int state_led;
    int mode_led;
    while (1)
    {
        // đợi nhận đc tin nhắn từ user
        xEventGroupWaitBits(mqtt_event_data_rx, MESSAGE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        if (strstr(message_mqtt, "Led") != NULL)
        {
            strtok(message_mqtt, " ");
            ptr = strtok(NULL, " ");
            sprintf(temp, "%s", ptr);
            store.state_led = atoi(temp);
        }
        else if (strstr(message_mqtt, "Mode") != NULL)
        {
            strtok(message_mqtt, " ");
            ptr = strtok(NULL, " ");
            sprintf(temp, "%s", ptr);
            store.mode_led = atoi(temp);
        }
        if (store.mode_led == 0)
        {
            check_mode_led = 0;
        }
        else
        {
            check_mode_led = 1;
        }
        if (store.state_led == 1)
        {
            gpio_set_level(33, 0);
        }
        else
        {
            gpio_set_level(33, 1);
        }
        nvs_flash_write_state(store.state_led);
        nvs_flash_write_mode(store.mode_led); // luu thong tin vao nvs
        xEventGroupClearBits(mqtt_event_data_rx, MESSAGE_BIT);
        vTaskDelay(5000 / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

const uint8_t led7Segment[10] = {
    0b11000000,
    0b11111001,
    0b10100100,
    0b10110000,
    0b10011001,
    0b10010010,
    0b10000010,
    0b11111000,
    0b10000000,
    0b10010000,
};
const uint8_t led7Segment_DOT[10] = {
    0b01000000,
    0b01111001,
    0b00100100,
    0b00110000,
    0b00011001,
    0b00010010,
    0b00000010,
    0b01111000,
    0b00000000,
    0b00010000,
};
const uint8_t pin_led7Segment[8] = {
    A, B, C, D, E, F, G, DP};
void print_led_7seg(uint8_t number)
{
    for (int i = 0; i < 8; i++)
    {
        gpio_set_level(pin_led7Segment[i], (number >> i) & 1);
    }
}
uint8_t chuky_led = 0;
uint8_t value_led[4] = {1, 2, 3, 4};
float data = 23.6;
/**
 *  void Timer_Callback(void *pvParameter)
 *  @brief Function được tạo ra được chạy định kỳ 2ms để hiển thị led 7 segemt
 *
 *  @param[in] pvParameter tham số truyền vào
 *  @return None
 */
void Timer_Callback(void *pvParameter) // task xu ly hien thi led 7segment
{
    uint8_t tram = Temperature / 100;
    uint8_t chuc = Temperature / 10 - 10 * tram;
    uint8_t donVi = Temperature / 1 - 100 * tram - chuc * 10;
    uint8_t phay = (uint8_t)((Temperature - (int)Temperature) * 10);
    if (chuky_led == 0)
    {
        if (tram == 1)
        {
            gpio_set_level(D1, 0);
            gpio_set_level(D2, 0);
            gpio_set_level(D3, 0);
            gpio_set_level(D4, 1);
            print_led_7seg(led7Segment[tram]);
        }
    }
    if (chuky_led == 1)
    {
        gpio_set_level(D1, 0);
        gpio_set_level(D2, 1);
        gpio_set_level(D3, 0);
        gpio_set_level(D4, 0);
        print_led_7seg(led7Segment[chuc]);
    }
    if (chuky_led == 2)
    {
        gpio_set_level(D1, 0);
        gpio_set_level(D2, 0);
        gpio_set_level(D3, 1);
        gpio_set_level(D4, 0);
        print_led_7seg(led7Segment_DOT[donVi]);
    }
    if (chuky_led == 3)
    {
        gpio_set_level(D1, 0);
        gpio_set_level(D2, 0);
        gpio_set_level(D3, 0);
        gpio_set_level(D4, 1);
        print_led_7seg(led7Segment[phay]);
    }
    chuky_led++;
    if (chuky_led > 3)
    {
        chuky_led = 0;
    }
}
/**
 *  static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
 *  @brief Xử lý các sự kiện từ Mqtt
 *
 *  @param[in] event Data về event được gửi đến
 *  @return ESP_OK
 */
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:

        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(mqtt_event_data_rx, MQTT_EVENT_CONNECT);
        msg_id = esp_mqtt_client_subscribe(client, "/device/led1", 0);
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
        xEventGroupClearBits(mqtt_event_data_rx, MQTT_EVENT_CONNECT);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "DA vao sub");
        break;
    case MQTT_EVENT_DATA: // nhan du lieu
    {
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        sprintf(message_mqtt, "%.*s", event->data_len, event->data);
        xEventGroupSetBits(mqtt_event_data_rx, MESSAGE_BIT);
        break;
    }
    default:
        ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
        break;
    }

    return ESP_OK;
}
/**
 *  static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
 *  @brief Nhận các event được kich hoạt
 *  @param[in] handler_args argument
 *  @param[in] base Tên Event
 *  @param[in] event_id Mã Event
 *  @param[in] event_data dữ liệu từ event loop
 *  @return None
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}
/**
 *  static void mqtt_app_start(void)
 *  @brief Kết nối đến Broker
 *  @return None
 */
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://192.168.1.22:8000", // uri cua broker gw
    };

    client = esp_mqtt_client_init(&mqtt_cfg); // khởi tạo mqtt init
    mqtt_event_data_rx = xEventGroupCreate();
    xTaskCreate(&Mqtt_Transmit_Task_Loop, "Mqtt_Transmit_Task_Loop", 2524, NULL, 5, NULL);    // tạo task publish dữ liệu
    xTaskCreate(&message_mqtt_rx_task, "message_mqtt_rx_task", 2524, NULL, 6, NULL);          // tạo task nhận dữ liệu từ Broker
    esp_mqtt_client_register_event(client, MQTT_EVENT_CONNECTED, mqtt_event_handler, client); // dang ky nhan su kien callback
    esp_mqtt_client_register_event(client, MQTT_EVENT_DISCONNECTED, mqtt_event_handler, client);
    esp_mqtt_client_register_event(client, MQTT_EVENT_DATA, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}
/**
 *  static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
 *  @brief Event Handler xử lý các sự kiện về kết nối đến Router
 *
 *  @param[in] arg argument
 *  @param[in] event_base Tên Event
 *  @param[in] event_id Mã Event
 *  @param[in] event_data IP được trả về
 *  @return None
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) // bắt đầu kết nối
    {
        printf("WIFI_EVENT EVENT WIFI_EVENT_STA_START : the event id is %d \n", event_id);
        esp_wifi_connect();
        // start task after the wifi is connected
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) // event báo mất kết nối đến AP
    {
        printf("WIFI_EVENT EVENT WIFI_EVENT_STA_DISCONNECTED : the event id is %d \n", event_id);
        gpio_set_level(32, 1); // tắt led báo mạng
        if (loopHandle != NULL)
        {
            printf("WIFI_EVENT task !NULL *** %d \n", (int)loopHandle);
            vTaskDelete(loopHandle);
        }
        else
        {
            printf("WIFI_EVENT task  NULL *** %d \n", (int)loopHandle);
        }

        if (client != NULL) // mất mạng
        {
            esp_mqtt_client_stop(client); // tắt mqtt
        }

        if (s_retry_num < 15) // cố gắng kết nối dưới 15 lần
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else // quá 15 thì coi như mất mạng
        {
            wifi_info.state = LOST_WIFI_STATE;
            nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info)); // lưu lại trạng thái wifi
            esp_restart();                                                         // reset esp32
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) // Event đã được cấp IP
    {
        printf("IP EVENT IP_EVENT_STA_GOT_IP : the event id is %d \n", event_id);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        // wifi connected ip assigned now start mqtt.
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);        // bit đồng bộ cho task xTaskTcpClient
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT); //
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) // Event đã kết nối thành công
    {
        gpio_set_level(32, 0); // bật led báo mạng
        printf("WIFI_EVENT EVENT WIFI_EVENT_STA_CONNECTED : the event id is %d \n", event_id);
        printf("Starting the MQTT app \n");
        mqtt_app_start(); // start MQTT
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) // K được cấp phát IP
    {
        printf("IP_EVENT EVENT IP_EVENT_STA_LOST_IP : the event id is %d \n", event_id);
    }
}
/**
 *  void wifi_init_sta(void)
 *  @brief Kết nối đến router ở chế độ Station
 *
 *  @return None
 */
void wifi_init_sta(void) // khoi tao wifi o che do station
{
    printf("wifi_init_sta\n");
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL)); // đăng ký function call back được gọi bất cứ có sự kiện nào
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };
    strcpy((char *)wifi_config.ap.ssid, wifi_info.SSID);
    strcpy((char *)wifi_config.ap.password, wifi_info.PASSWORD);
    printf("%s\n", wifi_info.SSID);
    printf("%s\n", wifi_info.PASSWORD);
    esp_wifi_stop();
    // esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    while (1)
    {
        /* Wait forever for WIFI_CONNECTED_BIT to be set within the event group.
        Clear the bits beforeexiting. */
        EventBits_t uxBits = xEventGroupWaitBits(s_wifi_event_group,
                                                 WIFI_CONNECTED_BIT, /* The bits within the event group to waitfor. */
                                                 pdTRUE,             /* WIFI_CONNECTED_BIT should be cleared before returning. */
                                                 pdFALSE,            /* Don't waitfor both bits, either bit will do. */
                                                 portMAX_DELAY);     /* Wait forever. */
        if ((uxBits & WIFI_CONNECTED_BIT) == WIFI_CONNECTED_BIT)
        {
            gpio_set_level(32, 0); // bật led báo mạng
            wifi_info.state = NORMAL_STATE;
            nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info)); // lưu lại trạng thái của wifi
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);            // bật tín hiệu báo mạng đã  kết nối
            ESP_LOGI(TAG, "WIFI_CONNECTED_BIT");
            break;
        }
    }
    ESP_LOGI(TAG, "Got IP Address.");
}
/**
 *  char *json_gen(json_gen_test_result_t *result, char *key0, int *value0, char *key1, char *value1, // dong goi thanh json
               char *key2, int value2, char *key3, int value3)
 *  @brief Đóng gói các key và value thành chuỗi JSON đầu ra
 *
 *  @param[in] result Chuỗi jSON đầu ra
 *  @param[in] key0 key Device
 *  @param[in] value0 value của key Device
 *  @param[in] key1 key Temperature
 *  @param[in] value1 value của key Temperature
 *  @param[in] key2 key Lux
 *  @param[in] value2 value của key Lux
 *  @param[in] key3  key Gas
 *  @param[in] value3 value của key Gas
 *  @return result->buf
 */
char *json_gen(json_gen_test_result_t *result, char *key0, int *value0, char *key1, char *value1, // dong goi thanh json
               char *key2, int value2, char *key3, int value3)
{
    char buf[30];
    memset(result, 0, sizeof(json_gen_test_result_t));
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, buf, sizeof(buf), flush_str, result);
    json_gen_start_object(&jstr);
    json_gen_obj_set_int(&jstr, key0, value0);
    json_gen_obj_set_string(&jstr, key1, value1);
    json_gen_obj_set_int(&jstr, key2, value2);
    json_gen_obj_set_int(&jstr, key3, value3);
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);
    return result->buf;
}
/**
 *  int json_parse_data_sensor(char *json, data_sensor_t *out_data)
 *  @brief Phân giã các key và value của chuỗi JSON đầu vào
 *
 *  @param[in] json Chuỗi jSON đầu vào
 *  @param[in] out_data Data sau khi được Parse
 *  @return 0 if OK, −1 on error
 */
int json_parse_data_sensor(char *json, data_sensor_t *out_data) // parse ban tin nhan duoc tu gw
{
    jparse_ctx_t jctx;
    int ret = json_parse_start(&jctx, json, strlen(json));
    if (ret != OS_SUCCESS)
    {
        printf("Parser failed\n");
        return -1;
    }
    if (json_obj_get_string(&jctx, "Temperature", &out_data->Tem, 20) != OS_SUCCESS)
    {
        printf("Parser failed\n");
    }
    if (json_obj_get_int(&jctx, "illuminance", &out_data->Lux) != OS_SUCCESS)
    {
        printf("Parser failed\n");
    }
    if (json_obj_get_int(&jctx, "Gas", &out_data->Gas_Check) != OS_SUCCESS)
    {
        printf("Parser failed\n");
    }
    json_parse_end(&jctx);
    return 0;
}
/**
 *  void output_create(int pin)
 *  @brief Config các chân đầu ra
 *
 *  @return None
 */
void output_create(int pin)
{
    gpio_config_t io_conf;
    // disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    // bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = (1ULL << pin);
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    // disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);
}
/**
 *  void input_create(int pin)
 *  @brief Config các chân đầu vào
 *
 *  @return None
 */
void input_create(int pin)
{
    gpio_config_t io_conf;
    // disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_INPUT;
    // bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = (1ULL << pin);
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    // configure GPIO with the given settings
    gpio_config(&io_conf);
}
/**
 *  void segment_config(void)
 *  @brief Config các chân của Led 7 Segment làm đầu ra
 *
 *  @return None
 */
void segment_config(void)
{
    gpio_config_t io_conf;
    // disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    // bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = (1ULL << A) | (1ULL << B) | (1ULL << C) | (1ULL << D) | (1ULL << E) |
                           (1ULL << F) | (1ULL << G) | (1ULL << DP);
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    // disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);
}
/**
 *  void wifi_connect(void)
 *  @brief Kết nối đến gw
 *
 *  @return None
 */
void wifi_connect(void)
{
    wifi_config_t cfg = {
        // AP cua gateway
        .sta = {
            .ssid = "local",
            .password = "12345678",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());
}
/**
 *  void xTaskTcpClient(void *pvParam)
 *  @brief Task thực hiện kết nối và truyền nhận dữ liệu Local
 *
 *  @param[void] Tham số truyền vào trong xTaskCreate
 *  @return None
 */
void xTaskTcpClient(void *pvParam)
{
    ESP_LOGI(TAG, "tcp_client task started \n");
    struct sockaddr_in tcpServerAddr;
    tcpServerAddr.sin_addr.s_addr = inet_addr("192.168.4.1"); // địa chỉ socket server
    tcpServerAddr.sin_family = AF_INET;                       // ipv4
    tcpServerAddr.sin_port = htons(3000);                     // port
    int s, r;
    char recv_buf[64];
    char MESSAGE[20] = "Message Client";
    uint8_t temp = 1;
    char data_tcp[30];
    while (1)
    {
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY); // doi ket noi den AP cua gateway thanh cong
        s = socket(AF_INET, SOCK_STREAM, 0);                                              // STREAM SOCKET IPV4
        if (s < 0)
        {
            ESP_LOGE(TAG, "... Failed to allocate socket.\n");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket\n");
        if (connect(s, (struct sockaddr *)&tcpServerAddr, sizeof(tcpServerAddr)) != 0) // kết nối tới TCP server
        {
            ESP_LOGE(TAG, "... socket connect failed errno=%d \n", errno);
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... connected \n");

        if (write(s, result.buf, strlen(result.buf)) < 0) // kết nối thành công thì gửi tin nhắn
        {
            ESP_LOGE(TAG, "... Send failed \n");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... socket send success");
        do
        {
            bzero(recv_buf, sizeof(recv_buf)); // cho buf ban dau bang 0
            r = read(s, recv_buf, sizeof(recv_buf) - 1);
            sprintf(data_tcp, "%.*s", r, recv_buf);
            printf("%s", data_tcp);
            if (strstr(data_tcp, "/") != NULL) // xu ly thong tin wifi nhan dc
            {
                char *pt = strtok(data_tcp, "/");
                strcpy(wifi_info.SSID, pt);
                pt = strtok(NULL, "/");
                strcpy(wifi_info.PASSWORD, pt);
                printf("\nssid: %s \n pass: %s\n", wifi_info.SSID, wifi_info.PASSWORD);
                wifi_info.state = CHANGE_PASSWORD_STATE;
                nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info));
                esp_restart();
            }
            else if (strstr(data_tcp, "ON") != NULL) // bat led
            {
                store.state_led = 1;
                nvs_flash_write_state(store.state_led);
                gpio_set_level(33, 0);
            }
            else if (strstr(data_tcp, "OFF") != NULL) // tat led
            {
                store.state_led = 0;
                nvs_flash_write_state(store.state_led);
                gpio_set_level(33, 1);
            }
            if (strstr(data_tcp, "CHANGE") != NULL) // doi che do
            {
                wifi_info.state = NORMAL_STATE;
                nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info));
                esp_restart();
            }
        } while (r > 0);
        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
        close(s); // dong socket
        ESP_LOGI(TAG, "... new request in 1 seconds");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "...tcp_client task closed\n");
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    /*
                Khởi tạo các ngoại vi
    */
    input_create(0);
    segment_config();
    output_create(22);
    output_create(14);
    gpio_pad_select_gpio(GPIO_NUM_33);
    gpio_set_direction(32, GPIO_MODE_OUTPUT);       // Khởi tạo chân Led báo mạng làm output
    gpio_set_direction(33, GPIO_MODE_INPUT_OUTPUT); // Khởi tạo chân Led làm output
    gpio_set_level(32, 1);
    gpio_set_level(33, 1);
    gpio_set_level(14, 0);
    gpio_set_level(22, 0);
    gpio_set_direction(D1, GPIO_MODE_OUTPUT); // Khởi tạo Các chân của 7segment làm output
    gpio_set_direction(D2, GPIO_MODE_OUTPUT);
    gpio_set_direction(D3, GPIO_MODE_OUTPUT);
    gpio_set_direction(D4, GPIO_MODE_OUTPUT);

    /*
                khoi tao timer Cho Led 7segment
    */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &Timer_Callback, // CallBack function cho led 7Segment
        .name = "periodic"};
    esp_timer_handle_t periodic_timer;

    esp_timer_create(&periodic_timer_args, &periodic_timer);
    esp_timer_start_periodic(periodic_timer, 2000); // Function Callback được chạy mỗi 2ms
    adc1_config_width(width);                       // Cấu hình adc độ phân giải 12bit
    adc1_config_channel_atten(channel, atten);      // range 3.3V
    adc1_config_channel_atten(channel_mq02, atten); // MQ-02

    /*           Ngắt ngoài cho button         */
    gpio_set_intr_type(INPUT_PIN, GPIO_INTR_POSEDGE);
    interputQueue = xQueueCreate(10, sizeof(int));
    xTaskCreate(&Led_Local_Task, "Led_Local_Task", 2048 * 5, NULL, 7, NULL);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(INPUT_PIN, gpio_interrupt_handler, (void *)INPUT_PIN);
    wifi_event_group = xEventGroupCreate();
    ds18b20_init();
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_get_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info); // lấy thông tin wifi lưu trữ từ nvs
    int a = nvs_flash_read_stateLed();
    printf("%d\n%s\n%s\n", a, wifi_info.SSID, wifi_info.PASSWORD);
    store.mode_led = nvs_flash_read_modeLed();
    store.state_led = nvs_flash_read_stateLed(); // Đọc trạng thái Led gần nhất
    if (store.state_led == 1)
    {
        gpio_set_level(33, 0);
    }
    else
    {
        gpio_set_level(33, 1);
    }

    s_wifi_event_group = xEventGroupCreate();
    xTaskCreate(&Periperal_Task_loop, "ds18b20_task_loop", 2524, NULL, 5, NULL);
    if (wifi_info.state == INITIAL_STATE) // Xem state có phải ban đầu hay k
    {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
        assert(sta_netif);
        wifi_init_softap(); // chạy AP để user config wifi
    }
    else if (wifi_info.state == NORMAL_STATE) // Trạng thái bình thường
    {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
        assert(sta_netif);

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_init_sta();
    }
    else if (wifi_info.state == CHANGE_PASSWORD_STATE) // Thay đổi mật khẩu wifi
    {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
        assert(sta_netif);
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        printf("Da den wifi_init_sta\n");
        wifi_init_sta();
    }
    else if (wifi_info.state == LOST_WIFI_STATE) // Mất mạng
    {
        esp_log_level_set("wifi", ESP_LOG_NONE); // disable wifi driver logging
        tcpip_adapter_init();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_connect();
        ESP_ERROR_CHECK(esp_wifi_start());

        xTaskCreate(&xTaskTcpClient, "xTaskTcpClient", 4048, NULL, 5, NULL);
    }
}