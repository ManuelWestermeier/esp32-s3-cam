// CameraWebServer for ESP32-S3 (select one camera model below)
//
// NOTE: Edit ssid/password below before uploading.
// If your board uses a different camera model, replace the uncommented line
// with the correct `#define CAMERA_MODEL_...` for your board.

// #define CAMERA_MODEL_WROVER_KIT // Has PSRAM
// #define CAMERA_MODEL_ESP_EYE  // Has PSRAM
#define CAMERA_MODEL_ESP32S3_EYE  // Has PSRAM   <-- SELECTED for ESP32-S3 \
                                  // #define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM \
                                  // #define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM \
                                  // #define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM \
                                  // #define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM \
                                  // #define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM \
                                  // #define CAMERA_MODEL_M5STACK_CAMS3_UNIT  // Has PSRAM \
                                  // #define CAMERA_MODEL_AI_THINKER // Has PSRAM \
                                  // #define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM \
                                  // #define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM \
                                  // ** Espressif Internal Boards ** \
                                  // #define CAMERA_MODEL_ESP32_CAM_BOARD \
                                  // #define CAMERA_MODEL_ESP32S2_CAM_BOARD \
                                  // #define CAMERA_MODEL_ESP32S3_CAM_LCD \
                                  // #define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3 // Has PSRAM \
                                  // #define CAMERA_MODEL_DFRobot_Romeo_ESP32S3 // Has PSRAM

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <esp_timer.h>
#include <Arduino.h>
#include "camera_pins.h"  // uses the selected CAMERA_MODEL_... define

//
// Replace with your WiFi credentials
//
const char* ssid = "io";
const char* password = "hhhhhh90";

WebServer server(80);

// ----------------------------------------------------------------------------
// Simple index page (shows snapshot + stream link)
const char* INDEX_HTML = R"rawliteral(
<html>
  <head>
    <title>ESP32 Camera</title>
  </head>
  <body>
    <h1>ESP32 Camera Web Server</h1>
    <p><a href="/stream">MJPEG Stream</a></p>
    <p><img src="/jpg" width="320" /></p>
    <p><a href="/capture">Download Last Capture (jpg)</a></p>
  </body>
</html>
)rawliteral";

// ----------------------------------------------------------------------------
// Helpers: send single JPEG frame
void handle_jpg() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Length", String(fb->len));
  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// Stream handler (multipart/x-mixed-replace)
void handle_stream() {
  WiFiClient client = server.client();

  String boundary = "frameboundary";
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Pragma", "no-cache");
  client.printf("Content-Type: image/jpeg\r\n");
  server.sendHeader("Connection", "close");
  server.sendHeader("Content-Type", "multipart/x-mixed-replace; boundary=" + boundary);

  // we must not call server.send() here because we will directly write stream bytes to client
  // send initial boundary
  client.print("--" + boundary + "\r\n");

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      break;
    }

    client.printf("Content-Type: image/jpeg\r\n");
    client.printf("Content-Length: %u\r\n\r\n", (unsigned)fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n--" + boundary + "\r\n");

    esp_camera_fb_return(fb);

    // small delay to allow other tasks to run (tweak for FPS)
    if (!client.connected()) break;
    delay(10);
  }
}

// Simple capture that triggers a download (same as /jpg but with content-disposition)
void handle_capture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=capture.jpg");
  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Length", String(fb->len));
  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handle_root() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handle_not_found() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(404, "text/plain", message);
}

// ----------------------------------------------------------------------------
// Optional: setup LED flash pin if camera_pins.h defines LED_GPIO_NUM
void setupLedFlash(int pin) {
#if defined(LED_GPIO_NUM)
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
#endif
}

// ----------------------------------------------------------------------------
void startCameraServer() {
  server.on("/", HTTP_GET, handle_root);
  server.on("/jpg", HTTP_GET, handle_jpg);
  server.on("/capture", HTTP_GET, handle_capture);

  // streaming: use a lambda that runs in current connection context
  server.on("/stream", HTTP_GET, []() {
    // Important: call handle_stream directly so we can stream a multipart response
    handle_stream();
  });

  server.onNotFound(handle_not_found);

  server.begin();
  Serial.println("HTTP server started");
}

// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  if (!psramFound()) {
    Serial.println("❌ PSRAM not found!");
  } else {
    Serial.printf("✅ PSRAM found, size = %u bytes\n", ESP.getPsramSize());
  }

  Serial.println("ESP32-S3-CAM + SD demo");
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("Starting camera...");

  camera_config_t config;
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
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  config.frame_size = FRAMESIZE_UXGA;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());

  startCameraServer();

  Serial.println("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  // If you use WebServer (synchronous), you must call handleClient() often
  server.handleClient();
  delay(2);
}
