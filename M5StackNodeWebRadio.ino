#include <Preferences.h>
#include <AsyncTCP.h>                                   /* https://github.com/me-no-dev/AsyncTCP */
#include <ESPAsyncWebServer.h>                          /* https://github.com/me-no-dev/ESPAsyncWebServer */
#include <WM8978.h>
#include <Audio.h>

#include "playList.h"
#include "index_htm.h"

/* M5Stack Node WM8978 I2C pins */
#define I2C_SDA     21
#define I2C_SCL     22

/* dev board with wm8978 breakout  */
#define I2S_BCK     21
#define I2S_WS      17
#define I2S_DOUT    22
#define I2S_DIN     34

/* M5Stack Node I2S pins
  #define I2S_BCK      5
  #define I2S_WS      13
  #define I2S_DOUT     2
  #define I2S_DIN     34
*/
/* M5Stack WM8978 MCLK gpio number and frequency */
#define I2S_MCLKPIN  0
#define I2S_MFREQ  (24 * 1000 * 1000)

//WM8978 dac;
Audio audio;

playList playList;
int currentItem{PLAYLIST_END_REACHED};
bool clientConnect{false};

enum {
  PAUSED,
  PLAYING
} playerStatus{PLAYING};

struct newUrl {
  bool waiting{false};
  String url;
} newUrl;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences preferences;

String urlEncode(String s) {
  //https://en.wikipedia.org/wiki/Percent-encoding
  s.replace(" ", "%20");
  s.replace("!", "%21");
  s.replace("&", "%26");
  s.replace("'", "%27");
  return s;
}

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    ESP_LOGI(TAG, "ws[%s][%u] connect", server->url(), client->id());
    clientConnect = true;
  } else if (type == WS_EVT_DISCONNECT) {
    ESP_LOGI(TAG, "ws[%s][%u] disconnect: %u", server->url(), client->id());
  } else if (type == WS_EVT_ERROR) {
    ESP_LOGE(TAG, "ws[%s][%u] error(%u): %s", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len) {
      if (info->opcode == WS_TEXT) {
        data[len] = 0;
        ESP_LOGD(TAG, "ws request: %s", reinterpret_cast<char*>(&data));
        char *pch = strtok((char*)data, "\n");
        if (!strcmp("toplaylist", pch)) {
          pch = strtok(NULL, "\n");
          playList.add({HTTP, pch});
          ESP_LOGD(TAG, "Added: %s", pch);
        }

        else if (!strcmp("playitem", pch)) {
          pch = strtok(NULL, "\n");
          audio.stopSong();
          currentItem = atoi(pch) - 1;
          playerStatus = PLAYING;
        }

        /* delete an item and adjust currentItem */
        else if (!strcmp("deleteitem", pch)) {
          if (!playList.size()) return;  //nothing to delete get outta here
          pch = strtok(NULL, "\n");
          int num = atoi(pch);

          if (playList.size() && num == currentItem) {
            audio.stopSong();
            currentItem--;
            playList.remove(num);
            return;
          }
          if (playList.size() && num > -1 && num < playList.size()) {
            playList.remove(num);
          } else {
            return;
          }
          if (num < currentItem) {
            currentItem--;
          }
        }

        else if (!strcmp("previous", pch)) {
          if (currentItem > 0) {
            currentItem--;
            currentItem--;
            audio.stopSong();
          }
          else return;
        }

        else if (!strcmp("next", pch)) {
          if (currentItem < playList.size() - 1) audio.stopSong();
          else return;
        }


      }
    }
  }
}

void setup() {
  /* // M5Stack wm8978 setup
    //pinMode(25, OUTPUT);
    if (!dac.begin(I2C_SDA, I2C_SCL)) {
    ESP_LOGE(TAG, "Error setting up dac. System halted");
    while (1) delay(100);
    }

    double retval = dac.setPinMCLK(I2S_MCLKPIN, I2S_MFREQ);
    if (!retval)
    ESP_LOGE(TAG, "Could not set %.2fMHz clock signal on GPIO %i", I2S_MFREQ / (1000.0 * 1000.0), I2S_MCLKPIN);
    else
    ESP_LOGI(TAG, "Generating %.2fMHz clock on GPIO %i", retval / (1000.0 * 1000.0), I2S_MCLKPIN);
    //dac.setSPKvol(40); // max 63
    //dac.setHPvol(16, 16);
  */
  WiFi.setSleep(false);
  WiFi.begin();
  while (!WiFi.isConnected()) {
    delay(10);
  }
  ESP_LOGI(TAG, "Connected as IP: %s", WiFi.localIP().toString().c_str());

  audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT);

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

  server.serveStatic("/", SD, "/");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
  ESP_LOGI(TAG, "HTTP server started.");
}

inline __attribute__((always_inline))
void sendCurrentItem() {
  ws.textAll("currentPLitem\n" + String(currentItem));
}

void loop() {
  audio.loop();
  ws.cleanupClients();

  if (playList.isUpdated || clientConnect) {
    ws.textAll(playList.toString());
    sendCurrentItem();
    playList.isUpdated = false;
    clientConnect = false;
  }

  if (newUrl.waiting) {
    //TODO: Add to end of playlist
    audio.connecttohost(urlEncode(newUrl.url));
    newUrl.waiting = false;
  }

  if (!audio.isRunning() && playList.size() && PLAYING == playerStatus) {
    currentItem++;
    if (playList.size() == currentItem) {
      currentItem = PLAYLIST_END_REACHED;
      ESP_LOGI(TAG, "End of playlist.");
      playerStatus = PAUSED;
    } else {
      ESP_LOGI(TAG, "Starting playlist item: %i", currentItem);
      static playListItem item;
      playList.get(currentItem, item);
      if (HTTP == item.type) audio.connecttohost(urlEncode(item.url));  // TODO: check for result?
    }
    sendCurrentItem();
  }
}
/*
  void audio_info(const char *info) {
  ESP_LOGI(TAG, "audio info: %s", info);
  }
*/
/*
  void audio_lasthost(const char *info) {
  ESP_LOGI(TAG, "audio EOF: %s", info);
  }
*/
