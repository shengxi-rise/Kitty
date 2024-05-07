/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <lwip/apps/sntp.h>
#include <esp_netif.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"
#include "smartconfig_ack.h"

/* The examples use smartconfig type that you can set via project configuration menu.

   If you'd rather not, just change the below entries to enum with
   the config you want - ie #define EXAMPLE_ESP_SMARTCOFNIG_TYPE SC_TYPE_ESPTOUCH
*/
#define EXAMPLE_ESP_SMARTCOFNIG_TYPE      SC_TYPE_AIRKISS
#define EXAMPLE_ESP_MAXIMUM_RETRY   3
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_HTTP_OUTPUT_BUFFER 2048

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;       // 两种连接方式用同一个事件组？

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "smartconfig_example";
static int s_retry_num = 0;
static uint8_t wifi_flag = 0;


// declaration
static void smartconfig_example_task(void *parm);

static void save_wifi(wifi_sta_config_t wifi_config_to_store);

static wifi_sta_config_t read_wifi(void);

static void wifi_clear(void);

// smart wifi connect
static void smart_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *) event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = {0};
        uint8_t password[65] = {0};

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;

        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        // save
        wifi_sta_config_t wifi_save;
        memcpy(wifi_save.ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(wifi_save.password, evt->password, sizeof(evt->password));
        save_wifi(wifi_save);

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void initialise_wifi(void) {
    tcpip_adapter_init();
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &smart_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &smart_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &smart_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void smartconfig_example_task(void *parm) {
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(EXAMPLE_ESP_SMARTCOFNIG_TYPE));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);

        if (uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
            // connect success
            wifi_sta_config_t test;
            test = read_wifi();
            printf("[test]: ssid:%s passwd:%s\r\n", test.ssid, test.password);
            wifi_flag = 1;

        }

        if (uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

// wifi connected
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
        wifi_flag = 0;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static uint8_t wifi_config(wifi_sta_config_t wifi_ini) {
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

//    wifi_config_t wifi_config = {
//            .sta = {
//                    .ssid = "LLS",
//                    .password = "lls123123"
//            },
//    };

    wifi_config_t wifi_config = {0};
    memcpy(&wifi_config.sta.ssid, &wifi_ini.ssid, sizeof(wifi_ini.ssid));
    memcpy(&wifi_config.sta.password, &wifi_ini.password, sizeof(wifi_ini.password));
//    wifi_config.sta = wifi_ini;       为什么结构体直接赋值不行?

    /* Setting a password implies station will connect to all security modes including WEP/WPA.
        * However these modes are deprecated and not advisable to be used. Incase your Access point
        * doesn't support WPA2, these mode can be enabled by commenting below line */

    if (strlen((char *) wifi_config.sta.password)) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 wifi_config.sta.ssid, wifi_config.sta.password);
        wifi_flag = 1;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 wifi_config.sta.ssid, wifi_config.sta.password);
        wifi_flag = 0;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler));
    vEventGroupDelete(s_wifi_event_group);
//    return 1;
    return wifi_flag;
}

// wifi data handle
static void save_wifi(wifi_sta_config_t wifi_config_to_store) {
    nvs_handle handle;
    // 写入一个整形数据，一个字符串，WIFI信息以及版本信息
    static const char *NVS_CUSTOMER = "customer data";
    static const char *WIFI = "blob_wifi";

    ESP_ERROR_CHECK(nvs_open(NVS_CUSTOMER, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_blob(handle, WIFI, &wifi_config_to_store, sizeof(wifi_config_to_store)));

    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

}

static wifi_sta_config_t read_wifi(void) {
    nvs_handle handle;
    static const char *NVS_CUSTOMER = "customer data";
    static const char *WIFI = "blob_wifi";

    uint32_t str_length = 32;
    char str_data[32] = {0};
    wifi_sta_config_t wifi_config_stored;

    memset(&wifi_config_stored, 0x0, sizeof(wifi_config_stored));
    uint32_t wifi_len = sizeof(wifi_config_stored);

    ESP_ERROR_CHECK(nvs_open(NVS_CUSTOMER, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK (nvs_get_blob(handle, WIFI, &wifi_config_stored, &wifi_len));

    printf("[data1]: %s len:%u\r\n", str_data, str_length);
    printf("[data3]: ssid:%s passwd:%s\r\n", wifi_config_stored.ssid, wifi_config_stored.password);

    nvs_close(handle);
    return wifi_config_stored;
}

static void wifi_clear(void) {
    nvs_flash_erase();
}

// date time
static void text_get(void *pvParameters) {

//02-1 定义需要的变量
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   //用于接收通过http协议返回的数据
    int content_length = 0;  //http协议头的长度

    //02-2 配置http结构体
    //定义http配置结构体，并且进行清零
    esp_http_client_config_t config;
    memset(&config, 0, sizeof(config));

    //向配置结构体内部写入url
    static const char *URL = "http://rap2api.taobao.org/app/mock/315262/oled";
    config.url = URL;

    //初始化结构体
    esp_http_client_handle_t client = esp_http_client_init(&config);    //初始化http连接

    //设置发送请求
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    //02-3 循环通讯

    while (1) {

        // 与目标主机创建连接，并且声明写入内容长度为0
        esp_err_t err = esp_http_client_open(client, 0);

        //如果连接失败
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        }
            //如果连接成功
        else {

            //读取目标主机的返回内容的协议头
            content_length = esp_http_client_fetch_headers(client);

            //如果协议头长度小于0，说明没有成功读取到
            if (content_length < 0) {
                ESP_LOGE(TAG, "HTTP client fetch headers failed");
            }

                //如果成功读取到了协议头
            else {

                //读取目标主机通过http的响应内容
                int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
                if (data_read >= 0) {

                    //打印响应内容，包括响应状态，响应体长度及其内容
                    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                             esp_http_client_get_status_code(client),                //获取响应状态信息
                             esp_http_client_get_content_length(client));            //获取响应信息长度
                    printf("data:%s\n", output_buffer);
                    //对接收到的数据作相应的处理
                    cJSON *root = NULL;
                    root = cJSON_Parse(output_buffer);

                    cJSON *time = cJSON_GetObjectItem(root, "text");

                    printf("%s\n", time->valuestring);

                }
                    //如果不成功
                else {
                    ESP_LOGE(TAG, "Failed to read response");
                }
            }
        }

        //关闭连接
        esp_http_client_close(client);


        //延时
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }

}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void obtain_time(void)
{
    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

static void sntp_task(void *arg)
{
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    time(&now);
    localtime_r(&now, &timeinfo);

    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        obtain_time();
    }

    // Set timezone to Eastern Standard Time and print local time
    // setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    // tzset();

    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();

    while (1) {
        // update 'now' variable with current time
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year < (2016 - 1900)) {
            ESP_LOGE(TAG, "The current date/time error");
        } else {
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            ESP_LOGI(TAG, "The current date/time in Shanghai is: %s", strftime_buf);
        }

        ESP_LOGI(TAG, "Free heap size: %d\n", esp_get_free_heap_size());    // 获取整个系统的可用空间
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}


void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
//    wifi_clear();
    wifi_sta_config_t wifi;
    wifi = read_wifi();
    if (wifi_config(wifi)) {
        printf("Connected to %s\n", wifi.ssid);
    } else {
        initialise_wifi();      // smartconfig wifi
    }
    if (wifi_flag) {
        xTaskCreate(&sntp_task, "sntp_task", 8192, NULL, 5, NULL);
        xTaskCreate(&text_get, "text_get", 8192, NULL, 6, NULL);

    }

}

