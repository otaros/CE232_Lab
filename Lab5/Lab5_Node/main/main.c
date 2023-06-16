// Node with ESP32 read temp and humi from AHT20 sensor and display on OLED SSD1306
// ESP32 connect to WiFi AP and MQTT broker
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include <stdio.h>
#include "esp_log.h"
#include "driver/i2c.h"
#include "sdkconfig.h"
#include "ssd1306.h"
#include "aht.h"

#include <time.h>
#include <sys/time.h>
#include "unity.h"
#include "ssd1306_fonts.h"

// CONFIG WIFI AP CONNECT TO MQTT
#define EXAMPLE_ESP_WIFI_SSID "DESKTOP-OI97DKV 6851"
#define EXAMPLE_ESP_WIFI_PASS "40n9X+82"
#define EXAMPLE_ESP_MAXIMUM_RETRY 5
#define CONFIG_BROKER_URL "mqtts://410e88d4c6f74067af21eb2712fbc8a4.s2.eu.hivemq.cloud:8883"

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

// CONFIG I2C OLED
#define I2C_MASTER_SCL_IO 26      /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO 25      /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM I2C_NUM_0  /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ 100000 /*!< I2C master clock frequency */
#define AHT_ADDR_GND 0x38         /*!< AHT20 I2C address with ADDR pin connected to GND */

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static EventGroupHandle_t s_mqtt_event_group;

/* FreeRTOS Task handle*/

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_SUBSCRIBED_BIT BIT1

static const char *TAG_WiFi = "wifi station";
static const char *TAG_MQTT = "mqtt";

static int s_retry_num = 0;
esp_mqtt_client_handle_t client;
ssd1306_handle_t ssd1306 = NULL;
aht_t aht20;
static const char *TAG_I2C = "I2C";

static void log_error_if_nonzero(const char *TAG, const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        // ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        // ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        // ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        // ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        // ESP_LOGI(TAG_MQTT, "sent unsubscribe successful, msg_id=%d", msg_id);
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        // ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero(TAG_MQTT, "reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero(TAG_MQTT, "reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero(TAG_MQTT, "captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG_MQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
        // .credentials.username = "HbI4VBzXtoM1fq7UT9KjSQeyTRPMreZXfyRYZeLgsn7vE2dl0OuIsx49cIMtMBgP",
        .credentials.username = "esp32",
        .credentials.authentication.password = "3W645!r7",
        .credentials.client_id = "esp32_node",
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static void WiFi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    // Wi-Fi Connect Phase
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    // Wi-Fi Disconnect Phase
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG_WiFi, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG_WiFi, "connect to the AP fail");
    }
    // Wi-Fi 'Got-IP' Phase
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG_WiFi, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    // Wi-Fi/LwIP Init Phase

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Wi-Fi Configuration Phase
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WiFi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WiFi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // Wi-Fi Start Phase
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WiFi, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by WiFi_event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG_WiFi, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG_WiFi, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG_WiFi, "UNEXPECTED EVENT");
    }
}

// i2c master initialization
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_NUM_0;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        // .clk_flags = 0,          /*!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here. */
    };
    esp_err_t err = i2c_param_config(i2c_master_port, &conf);
    if (err != ESP_OK)
    {
        return err;
    }
    return i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0);
}

void app_main(void)
{
    s_mqtt_event_group = xEventGroupCreate();
    s_wifi_event_group = xEventGroupCreate();

    ESP_LOGI(TAG_MQTT, "[APP] Startup..");
    ESP_LOGI(TAG_MQTT, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG_MQTT, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    aht20.mode = AHT_MODE_NORMAL;
    aht20.type = AHT_TYPE_AHT20;

    i2cdev_init();
    aht_init_desc(&aht20, AHT_I2C_ADDRESS_GND, 0, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    aht_init(&aht20);

    bool calibrated;
    aht_get_status(&aht20, NULL, &calibrated);
    if (calibrated)
        ESP_LOGI(TAG_I2C, "Sensor calibrated");
    else
        ESP_LOGW(TAG_I2C, "Sensor not calibrated!");

    i2cdev_done();
    vTaskDelay(pdMS_TO_TICKS(500));
    i2c_master_init();
    ssd1306_handle_t ssd1306 = ssd1306_create(I2C_NUM_0, SSD1306_I2C_ADDRESS);
    ssd1306_clear_screen(ssd1306, 0);
    ret = ssd1306_refresh_gram(ssd1306);
    if (ret != ESP_OK)
    {
        printf("refresh failed\n");
    }
    i2c_driver_delete(I2C_NUM_0);

    ESP_LOGI(TAG_WiFi, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    mqtt_app_start();

    float temperature, humidity;
    uint8_t str[64];

    for (;;)
    {
        // ssd1306_show_sensor_data(&ssd1306, &aht20);
        i2cdev_init();
        esp_err_t res = aht_get_data(&aht20, &temperature, &humidity);
        if (res == ESP_OK)
            ESP_LOGI(TAG_I2C, "Temperature: %.1f°C, Humidity: %.2f%%", temperature, humidity);
        else
            ESP_LOGE(TAG_I2C, "Error reading data: %d (%s)", res, esp_err_to_name(res));
        i2cdev_done();

        i2c_master_init();
        sprintf((char *)str, "Temperature: %.2f", temperature);
        ssd1306_draw_string(ssd1306, 0, 0, (uint8_t *)str, 12, 1);
        sprintf((char *)str, "Humidity: %.2f", humidity);
        ssd1306_draw_string(ssd1306, 0, 16, (uint8_t *)str, 12, 1);
        ssd1306_refresh_gram(ssd1306);
        i2c_driver_delete(I2C_NUM_0);

        sprintf((char *)str, "%.2f,%.2f", temperature, humidity);
        esp_mqtt_client_publish(client, "temperature_humidity/data", (char *)str, 0, 1, 0);

        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
