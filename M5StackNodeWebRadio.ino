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

/* sd card reader pins */
#define SPI_CLK  18
#define SPI_MISO 19
#define SPI_MOSI 23
#define SPI_SS   16

//WM8978 dac;
Audio audio;

playList playList;
bool clientConnect{false};

enum {
  PAUSED,
  PLAYING,
  PLAYLISTEND,
} playerStatus{PLAYLISTEND}; //we have an empty playlist after boot

int currentItem{ -1};

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
        ESP_LOGI(TAG, "ws request: %s", reinterpret_cast<char*>(data));
        char *pch = strtok((char*)data, "\n");

        if (!strcmp("toplaylist", pch)) {
          pch = strtok(NULL, "\n");
          playList.add({HTTP, pch});
          ESP_LOGD(TAG, "Added http url: %s", pch);
          if (!audio.isRunning() && playerStatus == PLAYLISTEND) {
            currentItem = playList.size() - 2;
            playerStatus = PLAYING;
          }
        }








        else if (!strcmp("alot_toplaylist", pch)) {
          pch = strtok(NULL, "\n");
          while (pch) {
            ESP_LOGI(TAG, "argument: %s", pch);
            pch = strtok(NULL, "\n");
          }
          return;
        }












        else if (!strcmp("playitem", pch)) {
          pch = strtok(NULL, "\n");
          currentItem = atoi(pch);
          audio.stopSong();
          playerStatus = PLAYING;
        }

        /* delete an item and adjust currentItem */
        else if (!strcmp("deleteitem", pch)) {
          if (!playList.size()) return;  //nothing to delete get outta here
          pch = strtok(NULL, "\n");
          int num = atoi(pch);

          if (num == currentItem) {
            audio.stopSong();
            playList.remove(num);
            if (!playList.size()) {
              playerStatus = PLAYLISTEND;
              currentItem = 0;
            }
            return;
          }
          if (num > -1 && num < playList.size()) {
            playList.remove(num);
          } else {
            return;
          }
          if (num < currentItem) {
            currentItem--;
          }
        }

        else if (!strcmp("previous", pch)) {
          if (PLAYLISTEND == playerStatus) return;
          if (currentItem > 0) {
            audio.stopSong();
            currentItem--;
            currentItem--;
          }
          else return;
        }

        else if (!strcmp("next", pch)) {
          if (PLAYLISTEND == playerStatus) return;
          if (currentItem < playList.size() - 1) {
            audio.stopSong();
          }
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

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, SPI_SS);
  SPI.setFrequency(40 * 1000 * 1000);
  if (!SD.begin()) {
    ESP_LOGE(TAG, "SD Mount Failed");
  } else {
    ESP_LOGI(TAG, "SD Mounted - capacity: %" PRId64 " Bytes - used: %" PRId64 " Bytes", SD.totalBytes(), SD.usedBytes());
  }

  ESP_LOGD(TAG, "Root SD folder:\n%s", listcDir(SD, "/", 0));

  WiFi.begin();
  WiFi.setSleep(false);
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

  //server.serveStatic("/", SD, "/");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
  ESP_LOGI(TAG, "HTTP server started.");

}

inline __attribute__((always_inline))
void sendCurrentItem() {
  if (playerStatus == PLAYLISTEND) ws.textAll("currentPLitem\n-1");
  else ws.textAll("currentPLitem\n" + String(currentItem));
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
    if (currentItem < playList.size() - 1) {
      currentItem++;
      ESP_LOGI(TAG, "Starting playlist item: %i", currentItem);
      static playListItem item;
      playList.get(currentItem, item);
      if (HTTP == item.type) audio.connecttohost(urlEncode(item.url));  // TODO: check for result?
      if (SDCARD == item.type) audio.connecttoSD(item.url);             // TODO: check for result?

    } else {
      currentItem = 0;
      ESP_LOGI(TAG, "End of playlist.");
      playerStatus = PLAYLISTEND;
    }
    sendCurrentItem();
  }
}
/*
  void audio_info(const char *info) {
  ESP_LOGI(TAG, "audio info: %s", info);
  }
*/
void audio_eof_mp3(const char *info) {
  ESP_LOGI(TAG, "EOF");
  audio.stopSong();
  if (currentItem < playList.size()) currentItem++;
}
/*
  void audio_lasthost(const char *info) {
  ESP_LOGI(TAG, "audio EOF: %s", info);
  }
*/
