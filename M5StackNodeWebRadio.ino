#include <HTTPClient.h>
#include <M5Stack.h>
#include <WM8978.h>
#include <AsyncTCP.h>                                   /* https://github.com/me-no-dev/AsyncTCP */
#include <ESPAsyncWebServer.h>                          /* https://github.com/me-no-dev/ESPAsyncWebServer */
#include <Audio.h>

#include "index_htm.h"

#define EXAMPLE_I2S_NUM (i2s_port_t)0
#define SAMPLE_RATE     11025
#define BITS_PER_SAMPLE 16

/* M5Stack Node WM8978 I2C pins */
#define I2C_SDA     21
#define I2C_SCL     22

/* M5Stack Node I2S pins */
#define I2S_BCK      5
#define I2S_WS      13
#define I2S_DOUT     2
#define I2S_DIN     34

/* M5Stack WM8978 MCLK gpio number and frequency */
#define I2S_MCLKPIN  0
#define I2S_MFREQ  (24 * 1000 * 1000)

#define FILESERVER_URL "http://192.168.0.50/muziek"
#define FILESERVER_SCRIPTNAME "ESP32.php"

WM8978 dac;
Audio audio;


struct updateLibrary {
  bool waiting{false};
  uint32_t clientId;
  String folder;
} updateLibrary;

struct newUrl {
  bool waiting{false};
  String url;
} newUrl;



AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

String urlEncode(String s) {
  //https://en.wikipedia.org/wiki/Percent-encoding
  s.replace(" ", "%20");
  s.replace("!", "%21");
  s.replace("&", "%26");
  s.replace("'", "%27");
  return s;
}

bool updateClientFolder(const String librarypath, const uint32_t clientId) {
  HTTPClient http;
  String url = (String)(FILESERVER_URL) + '/' + (String)(FILESERVER_SCRIPTNAME) + "?folder=" + librarypath;
  http.begin(urlEncode(url));
  const int httpCode = http.GET();
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      ESP_LOGE(TAG, "[HTTP] GET %s code: %i", url.c_str(), httpCode);
      String result = http.getString();
      http.end();
      result = String("folder\n") + librarypath + String("\n") + result;
      ws.text(clientId, result);
      return true;
    } else {
      ESP_LOGE(TAG, "[HTTP] GET %s code: %i", url.c_str(), httpCode);
    }
  } else {
    http.end();
    ESP_LOGE(TAG, "[HTTP] GET %s failed, error: %s", url, http.errorToString(httpCode).c_str());
    ws.printf(clientId, "[HTTP] GET %s failed, error: %s", url, http.errorToString(httpCode).c_str());
    return false;
  }
}

// https://stackoverflow.com/questions/17158890/transform-char-array-into-string/40311667#40311667

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    ESP_LOGI(TAG, "ws[%s][%u] connect", server->url(), client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    ESP_LOGI(TAG, "ws[%s][%u] disconnect: %u", server->url(), client->id());
  } else if (type == WS_EVT_ERROR) {
    ESP_LOGE(TAG, "ws[%s][%u] error(%u): %s", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len) {
      if (info->opcode == WS_TEXT) {
        data[len] = 0;
        String test = (char*)data;
        if (test.startsWith("getfolder")) {
          //ESP_LOGI(TAG, "req for folder:'%s'", test.c_str(), test.substring(9).c_str());
          updateLibrary.folder = test.substring(9);
          updateLibrary.clientId = client->id();
          updateLibrary.waiting = true;
          return;
        }
        if (test.startsWith("playdirect")) {
          ESP_LOGI(TAG, "req for direct play: '%s'", test.substring(10).c_str());
          newUrl.url = (String)(FILESERVER_URL)+"/"+test.substring(10).c_str();
          newUrl.waiting = true;
        }
      }
    }
  }
}


void setup() {
  pinMode(25, OUTPUT);
  WiFi.setSleep(false);
  WiFi.begin();
  while (!WiFi.isConnected()) {
    delay(10);
  }
  M5.begin();
  ESP_LOGI(TAG, "Connected as IP: %s", WiFi.localIP().toString().c_str());

  if (!dac.begin(I2C_SDA, I2C_SCL)) {
    ESP_LOGE(TAG, "Error setting up dac. System halted");
    while (1) delay(100);
  }
  double retval = dac.setPinMCLK(I2S_MCLKPIN, I2S_MFREQ);
  if (!retval)
    ESP_LOGE(TAG, "Could not set %.2fMHz clock signal on GPIO %i", I2S_MFREQ / (1000.0 * 1000.0), I2S_MCLKPIN);
  else
    ESP_LOGI(TAG, "Generating %.2fMHz clock on GPIO %i", retval / (1000.0 * 1000.0), I2S_MCLKPIN);

  dac.setSPKvol(40); /* max 63 */
  dac.setHPvol(16, 16);

  audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT, I2S_DIN);


  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_htm, index_htm_len);
    request->send(response);
  });


  server.onNotFound([](AsyncWebServerRequest * request) {
    ESP_LOGE(TAG, "404 - Not found: 'http://%s%s'", request->host().c_str(), request->url().c_str());
    request->send(404);
  });

  //server.serveStatic("/", SD, "/");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
  ESP_LOGI(TAG, "HTTP server started.");
}

void loop() {
  M5.update();
  audio.loop();

  if (newUrl.waiting) {
    audio.connecttohost(urlEncode(newUrl.url));
    newUrl.waiting = false;
  }

  if (updateLibrary.waiting) {
    if (!updateClientFolder(updateLibrary.folder, updateLibrary.clientId))
      ESP_LOGE(TAG, "error updating folder for client %i", updateLibrary.clientId);
    updateLibrary.waiting = false;
  }

}

void audio_info(const char *info) {
  ESP_LOGI(TAG, "audio info: %s", info);
}
