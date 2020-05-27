#include <Preferences.h>
#include <AsyncTCP.h>                                   /* https://github.com/me-no-dev/AsyncTCP */
#include <ESPAsyncWebServer.h>                          /* https://github.com/me-no-dev/ESPAsyncWebServer */
#include <WM8978.h>
#include <Audio.h>

#include "playList.h"
#include "index_htm.h"

/* webserver core */
#define HTTP_RUN_CORE 0

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

struct {
  uint32_t id;
  bool connected{false};
} newClient;

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
/*
  void audio_info(const char *info) {
  ESP_LOGI(TAG, "Info: %s", info);
  }
*/

static char showstation[400]; ///////////////////////////////////////////////////??
void audio_showstation(const char *info) {
  ESP_LOGI(TAG, "showstation: %s", info);
  snprintf(showstation, sizeof(showstation), "showstation\n%s", info);
  ws.textAll(showstation);
}
/*
  void audio_bitrate(const char *info) {
  ESP_LOGI(TAG, "bitrate: %s", info);
  ws.printfAll("bitrate\n%s", info);
  }
*/

static char streamtitle[400]; ///////////////////////////////////////////////////??
void audio_showstreamtitle(const char *info) {
  ESP_LOGI(TAG, "streamtitle: %s", info);
  snprintf(streamtitle, sizeof(streamtitle), "streamtitle\n%s", info);
  ws.printfAll("streamtitle\n%s", info);
}

/*
  void audio_showstreaminfo(const char *info) {
  ESP_LOGI(TAG, "streaminfo: %s", info);
  ws.printfAll("streaminfo\n%s",info);
  }
*/
void audio_id3data(const char *info) {
  ESP_LOGI(TAG, "id3data: %s", info);
  ws.printfAll("id3data\n%s", info);
}

void audio_eof_mp3(const char *info) {
  ESP_LOGI(TAG, "EOF");
  audio.stopSong();
}
/*
  void audio_lasthost(const char *info) {
  ESP_LOGI(TAG, "audio EOF: %s", info);
  }
*/

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    ESP_LOGI(TAG, "ws[%s][%u] connect", server->url(), client->id());
    newClient.connected = true;
    newClient.id = client->id();
  } else if (type == WS_EVT_DISCONNECT) {
    ESP_LOGI(TAG, "ws[%s][%u] disconnect: %u", server->url(), client->id());
  } else if (type == WS_EVT_ERROR) {
    ESP_LOGE(TAG, "ws[%s][%u] error(%u): %s", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;

    // here all data is contained in a single packet
    if (info->final && info->index == 0 && info->len == len) {
      if (info->opcode == WS_TEXT) {
        data[len] = 0;

        ESP_LOGD(TAG, "ws request: %s", reinterpret_cast<char*>(data));

        char *pch = strtok((char*)data, "\n");
        if (!strcmp("toplaylist", pch)) {
          int previousSize = playList.size();
          pch = strtok(NULL, "\n");
          while (pch) {
            ESP_LOGD(TAG, "argument: %s", pch);
            playList.add({HTTP_FILE, pch});
            pch = strtok(NULL, "\n");
          }
          ESP_LOGD(TAG, "Added %i items to playlist", playList.size() - previousSize);
          client->printf("message\nAdded %i items to playlist", playList.size() - previousSize);
          // start playing at the correct position if not already playing
          if (!audio.isRunning() && PAUSED != playerStatus) {
            currentItem = previousSize - 1;
            playerStatus = PLAYING;
          }
        }

        else if (!strcmp("clearlist", pch)) {
          audio.stopSong();
          playList.clear();
          audio_showstreamtitle("&nbsp;");
          //audio_showstation("&nbsp;");
          currentItem = -1;
          playerStatus = PLAYLISTEND;
        }

        else if (!strcmp("playitem", pch)) {
          pch = strtok(NULL, "\n");
          currentItem = atoi(pch);
          audio.stopSong();
          playerStatus = PLAYING;
        }

        else if (!strcmp("deleteitem", pch)) {
          if (!playList.size()) return;
          pch = strtok(NULL, "\n");
          int num = atoi(pch);
          if (num == currentItem) {
            audio.stopSong();
            playList.remove(num);
            if (!playList.size()) {
              currentItem = -1;
              playerStatus = PLAYLISTEND;
              return;
            }
            currentItem--;
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

        else if (!strcmp("pause", pch)) {
          if (PLAYING == playerStatus) playerStatus = PAUSED;
          else if (PAUSED == playerStatus) playerStatus = PLAYING;
          if (PLAYLISTEND != playerStatus) audio.pauseResume();
        }

        else if (!strcmp("presetstation", pch)) {
          pch = strtok(NULL, "\n");
          playList.add({HTTP_PRESET, "", atoi(pch)});
          ESP_LOGD(TAG, "Added 1 preset item to playlist");
          client->printf("message\nAdded 1 preset item to playlist");
          // start playing at the correct position if not already playing
          if (!audio.isRunning() && PAUSED != playerStatus) {
            currentItem = playList.size() - 2;
            playerStatus = PLAYING;
          }
        }




      }
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      static char* buffer;
      if (info->index == 0) {
        if (info->num == 0)
          ESP_LOGD(TAG, "ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");

        ESP_LOGD(TAG, "ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
        //allocate info->len bytes of memory
        buffer = new char[info->len + 1];  //TODO: check if enough mem is available and if allocation succeeds
      }

      ESP_LOGD(TAG, "ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);
      //move the data to the buffer
      memcpy(buffer + info->index, data, len);
      ESP_LOGD(TAG, "Copied %i bytes to buffer at pos %llu", len, info->index);

      if ((info->index + len) == info->len) {
        ESP_LOGD(TAG, "ws[%s][%u] frame[%u] end[%llu]", server->url(), client->id(), info->num, info->len);
        if (info->final) {
          ESP_LOGD(TAG, "ws[%s][%u] %s-message end", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");

          //we should have the complete message now stored in buffer
          buffer[info->len] = 0;
          ESP_LOGD(TAG, "complete multi frame request: %s", reinterpret_cast<char*>(buffer));

          char* pch = strtok(buffer, "\n");
          if (!strcmp("toplaylist", pch)) {
            ESP_LOGD(TAG, "multi frame playlist");

            int previousSize = playList.size();
            pch = strtok(NULL, "\n");
            while (pch) {
              ESP_LOGD(TAG, "argument: %s", pch);
              playList.add({HTTP_FILE, pch});
              pch = strtok(NULL, "\n");
            }
            delete []buffer;
            ESP_LOGD(TAG, "Added %i items to playlist", playList.size() - previousSize);
            client->printf("message\nAdded %i items to playlist", playList.size() - previousSize);
            // start playing at the correct position if not already playing
            if (!audio.isRunning() && PAUSED != playerStatus) {
              currentItem = previousSize - 1;
              playerStatus = PLAYING;
            }
          }
        }
      }
    }
  }
}

void startWebServer(void * pvParameters) {
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_htm, index_htm_len);
    request->send(response);
  });

  server.on("/stations", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    for (uint16_t i = 0; i < sizeof(preset) / sizeof(station); i++) {
      response->printf("%s\n", preset[i].name.c_str());
    }
    request->send(response);
  });

  server.onNotFound([](AsyncWebServerRequest * request) {
    ESP_LOGE(TAG, "404 - Not found: 'http://%s%s'", request->host().c_str(), request->url().c_str());
    request->send(404);
  });

  //server.serveStatic("/", SD, "/");

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
  ESP_LOGI(TAG, "HTTP server started on core %i.", HTTP_RUN_CORE);
  vTaskDelete(NULL);
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

  xTaskCreatePinnedToCore(
    startWebServer,
    "http_ws",
    8000,
    NULL,
    5,
    NULL,
    HTTP_RUN_CORE);
}

inline __attribute__((always_inline))
void sendCurrentItem() {
  ws.textAll("currentPLitem\n" + String(currentItem));
}

void loop() {
  audio.loop();

  ws.cleanupClients();

  static uint32_t previousTime;
  if (previousTime != audio.getAudioCurrentTime()) {
    ESP_LOGI(TAG, "%i - %i", audio.getAudioCurrentTime(), audio.getAudioFileDuration());
    previousTime = audio.getAudioCurrentTime();
  }

  if (playList.isUpdated) {
    ESP_LOGI(TAG, "Free mem: %i", ESP.getFreeHeap());
    ws.textAll(playList.toClientString());
    sendCurrentItem();
    playList.isUpdated = false;
  }

  if (newClient.connected) {
    ws.text(newClient.id, playList.toClientString());
    ws.text(newClient.id, "currentPLitem\n" + String(currentItem));
    ws.text(newClient.id, showstation);
    ws.text(newClient.id, streamtitle);
    newClient.connected = false;
  }

  if (newUrl.waiting) {
    //TODO: Add to end of playlist (((( OR PLAY 'INBETWEEN' OR 'PREVIEW' MODE ))))
    audio.connecttohost(urlEncode(newUrl.url));
    newUrl.waiting = false;
  }

  if (!audio.isRunning() && playList.size() && PLAYING == playerStatus) {
    audio_showstreamtitle("&nbsp;");
    audio_showstation("&nbsp;");
    if (currentItem < playList.size() - 1) {
      currentItem++;
      ESP_LOGI(TAG, "Starting playlist item: %i", currentItem);
      playListItem item;
      playList.get(currentItem, item);

      if (HTTP_FILE == item.type) {
        audio.connecttohost(urlEncode(item.url));
        audio_showstreamtitle(item.url.c_str());
        ESP_LOGI(TAG, "Duration: %i", audio.getFileSize());
      }

      else if (HTTP_PRESET == item.type) {
        ESP_LOGI(TAG, "preset: %s -> %s", preset[item.index].name.c_str(), preset[item.index].url.c_str());
        audio.connecttohost(urlEncode(preset[item.index].url));

      }

      else if (HTTP_STREAM == item.type) {
        audio.connecttohost(urlEncode(item.url));
        ESP_LOGI(TAG, "Duration: %i", audio.getFileSize());
      }

      else if (SDCARD_FILE == item.type) {
        audio.connecttoSD(item.url);
      }
    } else {
      ESP_LOGI(TAG, "End of playlist.");
      currentItem = -1;
      playerStatus = PLAYLISTEND;
    }
    sendCurrentItem();
  }
  delay(1);
}
