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
  //PLAYLISTEMPTY
} playerStatus{PLAYING};

int currentItem{0};

struct newUrl {
  bool waiting{false};
  String url;
} newUrl;

uint32_t lastConnectedClient;
String sdcardfolder{"/"};
struct {
  uint32_t id;
  String folder;
  bool changed{false};
} clientSDfolder;

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

// TODO: ombouwen van string naar char *
String listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  File root = fs.open(dirname);
  if (!root) {
    ESP_LOGE(TAG, "Failed to open directory");
    return "<p>Failed to open directory</p>";
  }
  if (!root.isDirectory()) {
    ESP_LOGE(TAG, "Not a directory");
    return "<p>Not a directory</p>";
  }
  String content{"sdfolder\n"};
  File file = root.openNextFile();
  while (file) {
    audio.loop();
    if (file.isDirectory()) {
      ESP_LOGD(TAG, "DIR : %s", file.name());
      content += "<p class=\"sdfolderlink\">" + String(file.name()) + "</p>\n";
      audio.loop();
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      static char upperCased[200];
      int len = strlen(file.name());
      for (int i = 0; i < len; i++) {
        upperCased[i] = toupper(file.name()[i]);
      }
      upperCased[len] = 0;
      audio.loop();
      ESP_LOGD(TAG, "Uppercased filename: %s", upperCased);
      if (strstr(upperCased, ".MP3") || strstr(upperCased, ".AAC")) {
        audio.loop();
        content += "<p class=\"sdfilelink\">" + String(file.name()) + "</p>\n";
      }
    }
    audio.loop();
    file = root.openNextFile();
  }
  return content;
}

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    ESP_LOGI(TAG, "ws[%s][%u] connect", server->url(), client->id());
    clientConnect = true;
    lastConnectedClient = client->id();
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
            currentItem = playList.size() - 1;
            playerStatus = PLAYING;
          }
        }

        if (!strcmp("sdtoplaylist", pch)) {
          pch = strtok(NULL, "\n");
          playList.add({SDCARD, pch});
          ESP_LOGD(TAG, "Added sd file: %s", pch);
          if (!audio.isRunning() && playerStatus == PLAYLISTEND) {
            currentItem = playList.size() - 1;
            playerStatus = PLAYING;
          }
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
            playList.remove(num);
            audio.stopSong();
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
          }
          else return;
        }

        else if (!strcmp("next", pch)) {
          if (PLAYLISTEND == playerStatus) return;
          if (currentItem < playList.size() - 1) {
            audio.stopSong();
            currentItem++;
          }
          else return;
        }

        else if (!strcmp("sdfolder", pch)) {
          pch = strtok(NULL, "\n");
          ESP_LOGI(TAG, "client %i changed sdfolder: %s", client->id(), pch);
          clientSDfolder.id = client->id();
          clientSDfolder.folder = pch;
          clientSDfolder.changed = true;
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

  ESP_LOGD(TAG, "Root SD folder:\n%s", listDir(SD, "/", 0).c_str());

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

  if (clientConnect) {
    ws.text(lastConnectedClient, listDir(SD, sdcardfolder.c_str(), 0));
  }

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
    if (currentItem < playList.size()) {
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

  if (clientSDfolder.changed) {
    ws.text(clientSDfolder.id, listDir(SD, clientSDfolder.folder.c_str(), 0));
    clientSDfolder.changed = false;
  }
}
/*
  void audio_info(const char *info) {
  ESP_LOGI(TAG, "audio info: %s", info);
  }
*/
void audio_eof_mp3(const char *info) {
  audio.stopSong();
  if (currentItem < playList.size()) currentItem++;
}
/*
  void audio_lasthost(const char *info) {
  ESP_LOGI(TAG, "audio EOF: %s", info);
  }
*/
