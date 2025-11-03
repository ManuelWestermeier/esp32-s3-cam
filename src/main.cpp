#include "Arduino.h"
#include "esp_camera.h"
#include "SD_MMC.h"
#include "WiFi.h"

// =====================
// Pin definitions (from your ESP32-S3-CAM-N16R8 GOOUUU pinout)
// =====================
#define CAM_XCLK 15
#define CAM_PCLK 13
#define CAM_VSYNC 6
#define CAM_HREF 7
#define CAM_SIOD 4 // SDA
#define CAM_SIOC 5 // SCL
#define CAM_Y9 16
#define CAM_Y8 17
#define CAM_Y7 18
#define CAM_Y6 12
#define CAM_Y5 10
#define CAM_Y4 8
#define CAM_Y3 9
#define CAM_Y2 11

#define SD_CMD 38
#define SD_CLK 39
#define SD_DATA 40

// #define LED_ON    45
// #define LED_RGB   47

// =====================
// Wi-Fi
// =====================
const char *ssid = "ESP32S3_CAM_AP";
const char *password = "12345678";

// =====================
// Camera configuration
// =====================
camera_config_t config = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = CAM_XCLK,
    .pin_sscb_sda = CAM_SIOD,
    .pin_sscb_scl = CAM_SIOC,
    .pin_d7 = CAM_Y9,
    .pin_d6 = CAM_Y8,
    .pin_d5 = CAM_Y7,
    .pin_d4 = CAM_Y6,
    .pin_d3 = CAM_Y5,
    .pin_d2 = CAM_Y4,
    .pin_d1 = CAM_Y3,
    .pin_d0 = CAM_Y2,
    .pin_vsync = CAM_VSYNC,
    .pin_href = CAM_HREF,
    .pin_pclk = CAM_PCLK,
    .xclk_freq_hz = 10000000, // lower clock = more reliable on S3
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 12,
    .fb_count = 1,
    .grab_mode = CAMERA_GRAB_LATEST};

// =====================
// HTTP server for streaming
// =====================
#include "esp_http_server.h"
static httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;

  res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  if (res != ESP_OK)
    return res;

  static const char *_boundary = "\r\n--frame\r\n";
  static const char *_jpg_hdr = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  while (true)
  {
    fb = esp_camera_fb_get();
    if (!fb)
    {
      Serial.println("Camera capture failed");
      break;
    }
    char header[64];
    size_t hlen = snprintf(header, 64, _jpg_hdr, fb->len);
    httpd_resp_send_chunk(req, _boundary, strlen(_boundary));
    httpd_resp_send_chunk(req, header, hlen);
    httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
  }

  return res;
}

static esp_err_t snapshot_handler(httpd_req_t *req)
{
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb)
  {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  File file = SD_MMC.open("/capture.jpg", FILE_WRITE);
  if (file)
  {
    file.write(fb->buf, fb->len);
    file.close();
    Serial.println("Saved /capture.jpg to SD card");
  }
  else
  {
    Serial.println("SD write failed!");
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return ESP_OK;
}

void startServer()
{
  httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
  httpd_uri_t stream_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL};
  httpd_uri_t shot_uri = {
      .uri = "/capture",
      .method = HTTP_GET,
      .handler = snapshot_handler,
      .user_ctx = NULL};
  if (httpd_start(&stream_httpd, &conf) == ESP_OK)
  {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    httpd_register_uri_handler(stream_httpd, &shot_uri);
  }
}
// =====================
// SETUP
// =====================
void setup()
{
  Serial.begin(115200);
  delay(500);
  if (!psramFound())
  {
    Serial.println("❌ PSRAM not found!");
  }
  else
  {
    Serial.printf("✅ PSRAM found, size = %u bytes\n", ESP.getPsramSize());
  }


  Serial.println("ESP32-S3-CAM + SD demo");

  // pinMode(LED_ON, OUTPUT);
  // digitalWrite(LED_ON, HIGH);

  config.frame_size = FRAMESIZE_QVGA; // Statt VGA
  config.jpeg_quality = 15;           // Etwas schlechtere Qualität
  config.fb_count = 1;                // Nur ein Framebuffer
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  config.xclk_freq_hz = 20000000;

  // Init camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }
  Serial.println("Camera initialized");

  // Init SD
  if (!SD_MMC.begin("/sdcard", true))
  {
    Serial.println("SD_MMC mount failed!");
  }
  else
  {
    Serial.printf("SD card OK, %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
  }

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("AP started: %s  IP: %s\n", ssid, ip.toString().c_str());

  startServer();

  Serial.printf("Stream:  http://%s/\n", ip.toString().c_str());
  Serial.printf("Capture: http://%s/capture\n", ip.toString().c_str());
}

void loop() {}
