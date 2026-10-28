/*
 * ESP32-CAM live video over the local WiFi.
 *
 *   http://<ip>/          control page (resolution, quality, flash, flip)
 *   http://<ip>/capture   single JPEG snapshot
 *   http://<ip>/status    current sensor settings as JSON
 *   http://<ip>/control   ?var=<name>&val=<value> to change a setting
 *   http://<ip>:81/stream MJPEG stream (multipart/x-mixed-replace)
 *
 * The stream runs on its own HTTP server (port 81) because a stream handler
 * never returns; on port 80 it would block the page and the controls.
 * One stream client at a time — a second viewer waits until the first closes.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "esp_camera.h"
#include "esp_http_server.h"

#include "camera_pins.h"
#include "secrets.h"
#include "web_page.h"

static const char *HostName = "esp32-camera";

// LEDC channel 0 / timer 0 are taken by the camera's XCLK.
static const int FlashLedChannel = 7;
static const uint32_t WifiCheckIntervalMs = 5000;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *StreamContentType = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *StreamBoundary = "\r\n--" PART_BOUNDARY "\r\n";
static const char *StreamPartHeader = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static httpd_handle_t _controlServer = nullptr;
static httpd_handle_t _streamServer = nullptr;
static int _flashLevel = 0;

static const char *ResetReasonName(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT (weak 5V supply or cable)";
    default:               return "other";
    }
}

static bool InitCamera()
{
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;

    const bool hasPsram = psramFound();
    if (hasPsram)
    {
        // Frame buffers are sized by the initial frame size, so allocate for
        // UXGA (the largest the page offers) and drop to VGA right after.
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
    }
    else
    {
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    const esp_err_t error = esp_camera_init(&config);
    if (error != ESP_OK)
    {
        Serial.printf("Camera init failed: 0x%x (%s)\n", error, esp_err_to_name(error));
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (hasPsram)
    {
        sensor->set_framesize(sensor, FRAMESIZE_VGA);
    }
    Serial.printf("Camera ready: sensor PID 0x%02x, PSRAM %s\n",
                  sensor->id.PID, hasPsram ? "yes" : "NO (limited to QVGA)");
    return true;
}

static esp_err_t HandleIndex(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html");
    return httpd_resp_send(request, IndexHtml, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t HandleCapture(httpd_req_t *request)
{
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr)
    {
        httpd_resp_send_500(request);
        return ESP_FAIL;
    }

    httpd_resp_set_type(request, "image/jpeg");
    httpd_resp_set_hdr(request, "Content-Disposition", "inline; filename=capture.jpg");
    const esp_err_t result = httpd_resp_send(request, reinterpret_cast<const char *>(frame->buf), frame->len);
    esp_camera_fb_return(frame);
    return result;
}

static esp_err_t HandleStream(httpd_req_t *request)
{
    esp_err_t result = httpd_resp_set_type(request, StreamContentType);
    if (result != ESP_OK)
    {
        return result;
    }
    httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");

    Serial.println("Stream client connected");
    char partHeader[64];
    uint32_t frameCount = 0;
    uint32_t windowStartMs = millis();

    while (true)
    {
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame == nullptr)
        {
            Serial.println("Frame capture failed");
            result = ESP_FAIL;
            break;
        }

        const size_t headerLength = snprintf(partHeader, sizeof(partHeader), StreamPartHeader, frame->len);
        result = httpd_resp_send_chunk(request, StreamBoundary, strlen(StreamBoundary));
        if (result == ESP_OK)
        {
            result = httpd_resp_send_chunk(request, partHeader, headerLength);
        }
        if (result == ESP_OK)
        {
            result = httpd_resp_send_chunk(request, reinterpret_cast<const char *>(frame->buf), frame->len);
        }
        esp_camera_fb_return(frame);

        if (result != ESP_OK)
        {
            // The browser closed the tab or stopped the stream.
            break;
        }

        frameCount++;
        const uint32_t elapsedMs = millis() - windowStartMs;
        if (elapsedMs >= 10000)
        {
            Serial.printf("Streaming %.1f fps\n", frameCount * 1000.0f / elapsedMs);
            frameCount = 0;
            windowStartMs = millis();
        }
    }

    Serial.println("Stream client disconnected");
    return result;
}

static void SetFlashLevel(int level)
{
    _flashLevel = constrain(level, 0, 255);
    ledcWrite(FlashLedChannel, _flashLevel);
}

static esp_err_t HandleControl(httpd_req_t *request)
{
    char query[64];
    char name[32];
    char valueText[16];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "var", name, sizeof(name)) != ESP_OK ||
        httpd_query_key_value(query, "val", valueText, sizeof(valueText)) != ESP_OK)
    {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "expected ?var=<name>&val=<value>");
        return ESP_FAIL;
    }

    const int value = atoi(valueText);
    sensor_t *sensor = esp_camera_sensor_get();
    int error = 0;

    if (strcmp(name, "framesize") == 0)
    {
        error = sensor->set_framesize(sensor, static_cast<framesize_t>(value));
    }
    else if (strcmp(name, "quality") == 0)
    {
        error = sensor->set_quality(sensor, value);
    }
    else if (strcmp(name, "brightness") == 0)
    {
        error = sensor->set_brightness(sensor, value);
    }
    else if (strcmp(name, "contrast") == 0)
    {
        error = sensor->set_contrast(sensor, value);
    }
    else if (strcmp(name, "vflip") == 0)
    {
        error = sensor->set_vflip(sensor, value);
    }
    else if (strcmp(name, "hmirror") == 0)
    {
        error = sensor->set_hmirror(sensor, value);
    }
    else if (strcmp(name, "flash") == 0)
    {
        SetFlashLevel(value);
    }
    else
    {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "unknown setting");
        return ESP_FAIL;
    }

    if (error != 0)
    {
        httpd_resp_send_500(request);
        return ESP_FAIL;
    }

    Serial.printf("Set %s = %d\n", name, value);
    return httpd_resp_send(request, nullptr, 0);
}

static esp_err_t HandleStatus(httpd_req_t *request)
{
    const sensor_t *sensor = esp_camera_sensor_get();
    char json[192];
    snprintf(json, sizeof(json),
             "{\"framesize\":%u,\"quality\":%u,\"brightness\":%d,\"contrast\":%d,"
             "\"vflip\":%u,\"hmirror\":%u,\"flash\":%d}",
             sensor->status.framesize, sensor->status.quality, sensor->status.brightness,
             sensor->status.contrast, sensor->status.vflip, sensor->status.hmirror, _flashLevel);

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
}

static void StartWebServers()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    const httpd_uri_t indexUri = {"/", HTTP_GET, HandleIndex, nullptr};
    const httpd_uri_t captureUri = {"/capture", HTTP_GET, HandleCapture, nullptr};
    const httpd_uri_t controlUri = {"/control", HTTP_GET, HandleControl, nullptr};
    const httpd_uri_t statusUri = {"/status", HTTP_GET, HandleStatus, nullptr};

    if (httpd_start(&_controlServer, &config) == ESP_OK)
    {
        httpd_register_uri_handler(_controlServer, &indexUri);
        httpd_register_uri_handler(_controlServer, &captureUri);
        httpd_register_uri_handler(_controlServer, &controlUri);
        httpd_register_uri_handler(_controlServer, &statusUri);
    }
    else
    {
        Serial.println("Failed to start control server on port 80");
    }

    // Second server instance needs its own port and its own control socket.
    config.server_port = 81;
    config.ctrl_port += 1;
    const httpd_uri_t streamUri = {"/stream", HTTP_GET, HandleStream, nullptr};

    if (httpd_start(&_streamServer, &config) == ESP_OK)
    {
        httpd_register_uri_handler(_streamServer, &streamUri);
    }
    else
    {
        Serial.println("Failed to start stream server on port 81");
    }
}

static void ConnectWifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HostName);
    // Modem sleep adds latency between frames; streaming wants the radio awake.
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.printf("Connecting to WiFi \"%s\"", WIFI_SSID);
    const uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startMs > 20000)
        {
            Serial.println("\nWiFi connect timed out — check secrets.h. Restarting.");
            delay(1000);
            ESP.restart();
        }
        delay(500);
        Serial.print('.');
    }

    Serial.printf("\nWiFi connected, RSSI %d dBm\n", WiFi.RSSI());
}

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.printf("ESP32-CAM starting, reset reason: %s\n", ResetReasonName(esp_reset_reason()));

    ledcSetup(FlashLedChannel, 5000, 8);
    ledcAttachPin(FLASH_LED_GPIO_NUM, FlashLedChannel);
    SetFlashLevel(0);

    if (!InitCamera())
    {
        Serial.println("Check the ribbon cable is seated and latched. Restarting in 5 s.");
        delay(5000);
        ESP.restart();
    }

    ConnectWifi();

    if (MDNS.begin(HostName))
    {
        MDNS.addService("http", "tcp", 80);
    }

    StartWebServers();

    Serial.printf("Open http://%s/  (or http://%s.local/)\n",
                  WiFi.localIP().toString().c_str(), HostName);
}

void loop()
{
    static uint32_t lastCheckMs = 0;
    if (millis() - lastCheckMs < WifiCheckIntervalMs)
    {
        delay(100);
        return;
    }
    lastCheckMs = millis();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi lost, reconnecting...");
        WiFi.reconnect();
    }
}
