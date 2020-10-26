#include <FFat.h>
#include <AsyncTCP.h>                                   /* https://github.com/me-no-dev/AsyncTCP */
#include <ESPAsyncWebServer.h>                          /* https://github.com/me-no-dev/ESPAsyncWebServer */
#include <Audio.h>                                      /* https://github.com/schreibfaul1/ESP32-audioI2S */
#include "board.h"
#include "htmlEntities.h"

#include "wifi_setup.h"
#include "playList.h"
#include "index_htm.h"
#include "icons.h"

/* webserver core */
#define HTTP_RUN_CORE 1

#define I2S_MAX_VOLUME 21

#ifdef A1S_AUDIO_KIT
#include <AC101.h>                                      /* https://github.com/Yveaux/AC101 */
/* A1S Audiokit I2S pins */
#define I2S_BCK     27
#define I2S_WS      26
#define I2S_DOUT    25
#define I2S_MCLK     0
/* A1S Audiokit I2C pins */
#define I2C_SCL     32
#define I2C_SDA     33
AC101 dac;
#endif  //A1S_AUDIO_KIT

#ifdef M5STACK_NODE
#include <M5Stack.h>
#include <WM8978.h>                                     /* https://github.com/CelliesProjects/wm8978-esp32 */
#include "Free_Fonts.h"
/* M5Stack Node I2S pins */
#define I2S_BCK      5
#define I2S_WS      13
#define I2S_DOUT     2
#define I2S_MCLK     0
/* M5Stack Node WM8978 I2C pins */
#define I2C_SDA     21
#define I2C_SCL     22

WM8978 dac;

void M5updateCurrentItemName(const playListItem& item) {
  const int LOC_X{M5.Lcd.width() / 2}, LOC_Y{M5.Lcd.height() / 2};
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setFreeFont(FSS12);
  M5.Lcd.fillRect(0, LOC_Y, 320, M5.Lcd.fontHeight(GFXFF), TFT_BLACK); //clear area
  M5.Lcd.setTextDatum(TC_DATUM); // TC = Top Center
  switch (item.type) {
    case HTTP_FAVORITE :
      M5.Lcd.drawString(item.name, LOC_X, LOC_Y);
      break;
    case HTTP_FILE :
      M5.Lcd.drawString(item.url.substring(item.url.lastIndexOf("/") + 1), LOC_X, LOC_Y);
      break;
    case HTTP_PRESET :
      M5.Lcd.drawString(preset[item.index].name, LOC_X, LOC_Y);
      break;
    case HTTP_STREAM :
      M5.Lcd.drawString(item.url, LOC_X, LOC_Y);
      break;
    default : ESP_LOGE(TAG, "Unhandled item.type");
  }
  M5.Lcd.display();
}

void M5updateCurrentAndTotal(const int current, const int total) {
  const int LOC_X{M5.Lcd.width() / 2}, LOC_Y{70};
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setFreeFont(FSS18);
  M5.Lcd.fillRect(0, LOC_Y, 320, M5.Lcd.fontHeight(GFXFF), TFT_BLACK); //clear area
  M5.Lcd.setTextDatum(TC_DATUM); // TC = Top Center
  String currentAndTotal;
  currentAndTotal.concat(current + 1); /* we are talking to humans here */
  currentAndTotal.concat(" / ");
  currentAndTotal.concat(total);
  M5.Lcd.drawString(currentAndTotal, LOC_X, LOC_Y);
  M5.Lcd.display();
}
#endif  //M5STACK_NODE

#ifdef GENERIC_I2S_DAC
/* I2S pins on Cellie's dev board */
#define I2S_BCK     21
#define I2S_WS      26
#define I2S_DOUT    22
#endif  //GENERIC_I2S_DAC

enum {
  PAUSED,
  PLAYING,
  PLAYLISTEND,
} playerStatus{PLAYLISTEND}; //we have an empty playlist after boot

#define     NOTHING_PLAYING_VAL   -1
const char* NOTHING_PLAYING_STR   {
  "Nothing playing"
};

int currentItem {NOTHING_PLAYING_VAL};

bool volumeIsUpdated{false};

struct {
  uint32_t id;
  bool connected{false};
} newClient;

struct {
  bool waiting{false};
  String url;
  uint32_t clientId;
} newUrl;

struct {
  bool requested{false};
  String filename;
  uint32_t clientId;
} currentToFavorites;

struct {
  bool requested{false};
  bool updated{false};
  uint32_t clientId;
} favorites;

struct {
  bool requested{false};
  String name;
  bool startNow;
} favoriteToPlaylist;

struct {
  bool requested{false};
  String name;
  uint32_t clientId;
} deletefavorite;

Audio audio(I2S_BCK, I2S_WS, I2S_DOUT);
playList playList;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const String urlEncode(const String& s) {
  //https://en.wikipedia.org/wiki/Percent-encoding
  String encodedstr{""};
  for (int i = 0; i < s.length(); i++) {
    switch (s.charAt(i)) {
      case ' ' : encodedstr += "%20";
        break;
      case '!' : encodedstr += "%21";
        break;
      case '&' : encodedstr += "%26";
        break;
      case  39 : encodedstr += "%27"; //39 == single quote '
        break;
      default : encodedstr += s.charAt(i);
    }
  }
  ESP_LOGD(TAG, "encoded url: %s", encodedstr.c_str());
  return encodedstr;
}

void playListHasEnded() {
  currentItem = NOTHING_PLAYING_VAL;
  playerStatus = PLAYLISTEND;
  audio_showstation(NOTHING_PLAYING_STR);
  audio_showstreamtitle("&nbsp;");
  ESP_LOGD(TAG, "End of playlist.");

#ifdef M5STACK_NODE
  M5updateCurrentItemName({HTTP_FAVORITE, ""});
  M5updateCurrentAndTotal(currentItem, playList.size());
#endif  //M5STACK_NODE
}

static char showstation[200]; // These are kept global to update new clients in loop()
void audio_showstation(const char *info) {
  if (!strcmp(info, "")) return;
  playListItem item;
  playList.get(currentItem, item);
  snprintf(showstation, sizeof(showstation), "showstation\n%s\n%s", info, typeStr[item.type]);
  ESP_LOGD(TAG, "showstation: %s", showstation);
  ws.textAll(showstation);
}

static char streamtitle[200]; // These are kept global to update new clients in loop()
void audio_showstreamtitle(const char *info) {
  snprintf(streamtitle, sizeof(streamtitle), "streamtitle\n%s", htmlEntities(info).c_str());
  ESP_LOGD(TAG, "streamtitle: %s", streamtitle);
  ws.printfAll(streamtitle);
}

void audio_id3data(const char *info) {
  ESP_LOGI(TAG, "id3data: %s", info);
  ws.printfAll("id3data\n%s", info);
}

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    ESP_LOGD(TAG, "ws[%s][%u] connect", server->url(), client->id());
    newClient.connected = true;
    newClient.id = client->id();
    favorites.requested = true;
    favorites.clientId = client->id();
  } else if (type == WS_EVT_DISCONNECT) {
    ESP_LOGD(TAG, "ws[%s][%u] disconnect: %u", server->url(), client->id());
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
        if (!pch) return;

        if (!strcmp("volume", pch)) {
          pch = strtok(NULL, "\n");
          if (pch) {
            const uint8_t volume = atoi(pch);
            audio.setVolume(volume > I2S_MAX_VOLUME ? I2S_MAX_VOLUME : volume);
            volumeIsUpdated = true;
          }
          return;
        }

        else if (!strcmp("filetoplaylist", pch)  ||
                 !strcmp("_filetoplaylist", pch)) {
          const bool startnow = (pch[0] == '_');
          const uint32_t previousSize = playList.size();
          pch = strtok(NULL, "\n");
          while (pch) {
            ESP_LOGD(TAG, "argument: %s", pch);
            playList.add({HTTP_FILE, "", pch});
            pch = strtok(NULL, "\n");
          }
          ESP_LOGD(TAG, "Added %i items to playlist", playList.size() - previousSize);
          client->printf("message\nAdded %i items to playlist", playList.size() - previousSize);
          if (startnow) {
            if (audio.isRunning()) audio.stopSong();
            currentItem = previousSize - 1;
            playerStatus = PLAYING;
            return;
          }
          // start playing at the correct position if not already playing
          if (!audio.isRunning() && PAUSED != playerStatus) {
            currentItem = previousSize - 1;
            playerStatus = PLAYING;
          }
        }

        else if (!strcmp("clearlist", pch)) {
          audio.stopSong();
          playList.clear();
          playListHasEnded();
        }

        else if (!strcmp("playitem", pch)) {
          pch = strtok(NULL, "\n");
          if (pch) {
            currentItem = atoi(pch);
            audio.stopSong();
            playerStatus = PLAYING;
          }
        }

        else if (!strcmp("deleteitem", pch)) {
          if (!playList.size()) return;
          pch = strtok(NULL, "\n");
          if (!pch) return;
          const uint32_t item = atoi(pch);
          if (item == currentItem) {
            audio.stopSong();
            playList.remove(item);
            if (!playList.size()) {
              playListHasEnded();
              return;
            }
            currentItem--;
            return;
          }
          if (item < playList.size()) {
            playList.remove(item);
            if (!playList.size()) {
              playListHasEnded();
              return;
            }
          } else {
            return;
          }
          if (item < currentItem) {
            currentItem--;
          }
        }

        else if (!strcmp("previous", pch)) {
          if (PLAYLISTEND == playerStatus) return;
          ESP_LOGD(TAG, "current: %i size: %i", currentItem, playList.size());
          if (currentItem > 0) {
            audio.stopSong();
            audio_showstation("&nbsp;");
            currentItem--;
            currentItem--;
          }
          else return;
        }

        else if (!strcmp("next", pch)) {
          if (PLAYLISTEND == playerStatus) return;
          ESP_LOGD(TAG, "current: %i size: %i", currentItem, playList.size());
          if (currentItem < playList.size() - 1) {
            audio.stopSong();
            audio_showstation("&nbsp;");
          }
          else return;
        }
        /*
                else if (!strcmp("pause", pch)) {
                  if (PLAYING == playerStatus) playerStatus = PAUSED;
                  else if (PAUSED == playerStatus) playerStatus = PLAYING;
                  if (PLAYLISTEND != playerStatus) audio.pauseResume();
                }
        */

        else if (!strcmp("newurl", pch)) {
          pch = strtok(NULL, "\n");
          if (pch) {
            ESP_LOGD(TAG, "received new url: %s", pch);
            newUrl.url = pch;
            newUrl.clientId = client->id();
            newUrl.waiting = true;
          }
          return;
        }

        else if (!strcmp("currenttofavorites", pch)) {
          pch = strtok(NULL, "\n");
          if (pch) {
            currentToFavorites.filename = pch;
            currentToFavorites.clientId = client->id();
            currentToFavorites.requested = true;
          }
        }

        else if (!strcmp("favorites", pch)) {
          favorites.clientId = client->id();
          favorites.requested = true;
        }

        else if (!strcmp("favoritetoplaylist", pch) ||
                 !strcmp("_favoritetoplaylist", pch)) {
          const bool startnow = (pch[0] == '_');
          favoriteToPlaylist.name = strtok(NULL, "\n");
          favoriteToPlaylist.startNow = startnow;
          favoriteToPlaylist.requested = true;
        }

        else if (!strcmp("deletefavorite", pch)) {
          deletefavorite.name = strtok(NULL, "\n");
          deletefavorite.requested = true;
        }

        else if (!strcmp("presetstation", pch) ||
                 !strcmp("_presetstation", pch)) {
          const bool startnow = (pch[0] == '_');
          const uint32_t index = atoi(strtok(NULL, "\n"));
          if (index < sizeof(preset) / sizeof(source)) { // only add really existing presets to the playlist
            playList.add({HTTP_PRESET, "", "", index});
            ESP_LOGD(TAG, "Added '%s' to playlist", preset[index].name.c_str());
            client->printf("message\nAdded '%s' to playlist", preset[index].name.c_str());
            if (startnow) {
              if (audio.isRunning()) audio.stopSong();
              currentItem = playList.size() - 2;
              playerStatus = PLAYING;
              return;
            }

            // start playing at the correct position if not already playing
            if (!audio.isRunning() && PAUSED != playerStatus) {
              currentItem = playList.size() - 2;
              playerStatus = PLAYING;
            }
          }
        }
      }
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      static char* buffer = nullptr;
      if (info->index == 0) {
        if (info->num == 0)
          ESP_LOGD(TAG, "ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");

        ESP_LOGD(TAG, "ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
        //allocate info->len bytes of memory

        // we need at least twice the amount of free memory that is requested (buffer + playlist data)
        if (info->len * 2 > ESP.getFreeHeap()) {
          client->text("message\nOut of memory.");
          return;
        }
        if (!buffer)
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
          if (!strcmp("filetoplaylist", pch) ||
              !strcmp("_filetoplaylist", pch)) {
            ESP_LOGD(TAG, "multi frame playlist");
            const bool startnow = (pch[0] == '_');
            const uint32_t previousSize = playList.size();
            pch = strtok(NULL, "\n");
            while (pch) {
              ESP_LOGD(TAG, "argument: %s", pch);
              playList.add({HTTP_FILE, "", pch});
              pch = strtok(NULL, "\n");
            }
            delete []buffer;
            buffer = nullptr;
            ESP_LOGD(TAG, "Added %i items to playlist", playList.size() - previousSize);
            client->printf("message\nAdded %i items to playlist", playList.size() - previousSize);
            if (startnow) {
              if (audio.isRunning()) audio.stopSong();
              currentItem = previousSize - 1;
              playerStatus = PLAYING;
              return;
            }
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

  // TODO: set a 304 on all static content to save on bandwidth

  static const char* HTML_HEADER = "text/html";

  server.on("/", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, HTML_HEADER, index_htm, index_htm_len);
    request->send(response);
  });

  server.on("/stations", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncResponseStream *response = request->beginResponseStream(HTML_HEADER);
    for (int i = 0; i < sizeof(preset) / sizeof(source); i++) {
      response->printf("%s\n", preset[i].name.c_str());
    }
    request->send(response);
  });

  //  serve icons as files - use the browser cache to only serve each icon once

  static const char* SVG_HEADER = "image/svg+xml";
  static const char* VARY_HEADER_STR = "Vary";
  static const char* ACCEPTENCODING_HEADER_STR = "Accept-Encoding";

  server.on("/radioicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, radioicon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.on("/playicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, playicon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.on("/libraryicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, libraryicon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.on("/favoriteicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, favoriteicon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.on("/streamicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, pasteicon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.on("/deleteicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, deleteicon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.on("/addfoldericon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, addfoldericon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.on("/emptyicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, emptyicon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.on("/starticon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_HEADER, starticon);
    response->addHeader(VARY_HEADER_STR, ACCEPTENCODING_HEADER_STR);
    request->send(response);
  });

  server.onNotFound([](AsyncWebServerRequest * request) {
    ESP_LOGE(TAG, "404 - Not found: 'http://%s%s'", request->host().c_str(), request->url().c_str());
    request->send(404);
  });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
  ESP_LOGI(TAG, "HTTP server started on core %i.", HTTP_RUN_CORE);
  vTaskDelete(NULL);
}

void setup() {
  btStop();
  if (psramInit()) ESP_LOGI(TAG, "%.2fMB PSRAM free.", ESP.getFreePsram() / (1024.0 * 1024));

  /* check if a ffat partition is defined and halt the system if it is not defined*/
  if (!esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "ffat")) {
    ESP_LOGE(TAG, "FATAL ERROR! No FFat partition defined. System is halted.\nCheck 'Tools>Partition Scheme' in the Arduino IDE and select a partition table with a FFat partition.");
    while (true) delay(1000); /* system is halted */
  }

  /* partition is defined - try to mount it */
  if (FFat.begin(0, "", 2)) // see: https://github.com/lorol/arduino-esp32fs-plugin#notes-for-fatfs
    ESP_LOGI(TAG, "FFat mounted.");

  /* partition is present, but does not mount so now we just format it */
  else {
    ESP_LOGI(TAG, "Formatting...");
    if (!FFat.format(true, (char*)"ffat") || !FFat.begin(0, "", 2)) {
      ESP_LOGE(TAG, "FFat error while formatting. Halting.");
      while (true) delay(1000); /* system is halted */;
    }
  }

  WiFi.begin(SSID, PSK);
  WiFi.setSleep(false);
  while (!WiFi.isConnected()) {
    delay(10);
  }
  ESP_LOGI(TAG, "Connected as IP: %s", WiFi.localIP().toString().c_str());

  ESP_LOGI(TAG, "Found %i presets", sizeof(preset) / sizeof(source));

#ifdef A1S_AUDIO_KIT
  ESP_LOGI(TAG, "Starting AC101 dac");
  if (!dac.begin(I2C_SDA, I2C_SCL))
  {
    ESP_LOGE(TAG, "AC101 dac failed to init! Halting.");
    while (true) delay(1000); /* system is halted */;
  }
  audio.i2s_mclk_pin_select(I2S_MCLK);
  dac.SetVolumeSpeaker(100);
  dac.SetVolumeHeadphone(50);
#endif  //A1S_AUDIO_KIT

#ifdef M5STACK_NODE
  M5.begin(true, false);
  M5.lcd.setBrightness(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextDatum(TC_DATUM); // TC = Top Center
  M5.Lcd.setFreeFont(FSS18);
  M5.Lcd.drawString("eStreamPlayer32", M5.Lcd.width() / 2, 0);
  const uint16_t ypos = M5.Lcd.fontHeight(GFXFF);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setFreeFont(FF6);
  M5.Lcd.drawString(WiFi.localIP().toString(), M5.Lcd.width() / 2, ypos);
  M5updateCurrentAndTotal(currentItem, playList.size());
  M5.Lcd.display();
  ESP_LOGI(TAG, "Starting WM8978 dac");
  if (!dac.begin(I2C_SDA, I2C_SCL))
  {
    ESP_LOGE(TAG, "WM8978 dac failed to init! Halting.");
    while (true) delay(1000); /* system is halted */;
  }
  audio.i2s_mclk_pin_select(I2S_MCLK);
  dac.setSPKvol(54);
  dac.setHPvol(32, 32);
#endif  //M5STACK_NODE

#ifdef GENERIC_I2S_DAC
  ESP_LOGI(TAG, "Starting I2S dac");
#endif  //GENERIC_I2S_DAC

  audio.setVolume(5); /* max 21 */

  xTaskCreatePinnedToCore(
    startWebServer,
    "http_ws",
    8000,
    NULL,
    5,
    NULL,
    HTTP_RUN_CORE);
}

const char* VOLUME_HEADER{"volume\n"};
const char* CURRENT_HEADER{"currentPLitem\n"};

inline __attribute__((always_inline))
void sendCurrentItem() {
  ws.textAll(CURRENT_HEADER + String(currentItem));
}

void loop() {

#ifdef M5STACK_NODE
  M5.update();
#endif  //M5STACK_NODE

  audio.loop();

  ws.cleanupClients();
  /*
    static uint32_t previousTime;
    if (previousTime != audio.getAudioCurrentTime()) {
      ESP_LOGI(TAG, "filetime: %i - %i", audio.getAudioCurrentTime(), audio.getAudioFileDuration());
      //ws.textAll("progress\n" + String(audio.getAudioCurrentTime()) +"\n" + String(audio.getAudioFileDuration()) +"\n");
      previousTime = audio.getAudioCurrentTime();
    }

    static uint32_t previousPos;
    if (previousPos != audio.getFilePos()) {
      ESP_LOGI(TAG, "position :%i - %i", audio.getFilePos(), audio.getFileSize());
      //ws.textAll("progress\n" + String(audio.getFilePos()) +"\n" + String(audio.getFileSize()) +"\n");
      previousPos = audio.getFilePos();
    }
  */
  if (volumeIsUpdated) {
    ws.textAll(VOLUME_HEADER + String(audio.getVolume()));
    volumeIsUpdated = false;
  }

  if (playList.isUpdated) {
    ESP_LOGD(TAG, "Playlist updated. %i items. Free mem: %i", playList.size(), ESP.getFreeHeap());
    ws.textAll(playList.toClientString());
    sendCurrentItem();

#ifdef M5STACK_NODE
    M5updateCurrentAndTotal(currentItem, playList.size());
#endif

    playList.isUpdated = false;
  }

  if (newClient.connected) {
    ws.text(newClient.id, playList.toClientString());
    ws.text(newClient.id, CURRENT_HEADER + String(currentItem));
    ws.text(newClient.id, showstation);
    ws.text(newClient.id, streamtitle);
    ws.text(newClient.id, VOLUME_HEADER + String(audio.getVolume()));
    newClient.connected = false;
  }

  if (newUrl.waiting) {
    ESP_LOGI(TAG, "STARTING new url: %s with %i items in playList", newUrl.url.c_str(), playList.size());
    if (audio.connecttohost(urlEncode(newUrl.url))) {
      playList.add({HTTP_STREAM, newUrl.url, newUrl.url});
      currentItem = playList.size() - 1;
      playerStatus = PLAYING;
      playList.isUpdated = true;
      audio_showstation(newUrl.url.c_str());
      audio_showstreamtitle("");

#ifdef M5STACK_NODE
      M5updateCurrentItemName({HTTP_STREAM, newUrl.url});
#endif //M5STACK_NODE

    }
    else {
      playListHasEnded();
      ws.text(newUrl.clientId, "message\nFailed to play stream");
      sendCurrentItem();
    }
    newUrl.waiting = false;
  }

  if (favorites.requested || favorites.updated) {
    File root = FFat.open("/");
    if (!root) {
      ESP_LOGE(TAG, "ERROR- failed to open root");
      const char * NO_FFAT_FOUND{"message\nNo FFat found"};
      if (favorites.requested)
        ws.text(favorites.clientId, NO_FFAT_FOUND);
      else
        ws.textAll(NO_FFAT_FOUND);
      return;
    }
    if (!root.isDirectory()) {
      ESP_LOGE(TAG, "ERROR- root is not a directory");
      const char * NO_ROOT_FOUND{"message\nNo root found"};
      if (favorites.requested)
        ws.text(favorites.clientId, NO_ROOT_FOUND);
      else
        ws.textAll(NO_ROOT_FOUND);
      return;
    }

    String response{"favorites\n"};
    File file = root.openNextFile();
    auto counter{0};
    while (file) {
      if (!file.isDirectory()) {
        response += file.name() + String("\n");
        counter++;
      }
      file = root.openNextFile();
    }
    if (favorites.requested) {
      ESP_LOGD(TAG, "Favorites requested by client %i", favorites.clientId);
      ws.text(favorites.clientId, response);
    }
    else {
      ESP_LOGD(TAG, "Favorites updated. %i items.", counter);
      ws.textAll(response);
    }
    if (favorites.requested) favorites.requested = false;
    if (favorites.updated) favorites.updated = false;
  }

  if (currentToFavorites.requested) {

    playListItem item;
    playList.get(currentItem, item);

    switch (item.type) {
      case HTTP_FILE :
        ESP_LOGD(TAG, "file (wont save)%s", item.url.c_str());
        break;
      case HTTP_PRESET :
        ESP_LOGD(TAG, "preset (wont save) %s %s", preset[item.index].name.c_str(), preset[item.index].url.c_str());
        break;
      case HTTP_STREAM :
      case HTTP_FAVORITE :
        {
          if (currentToFavorites.filename.equals("")) {
            ESP_LOGE(TAG, "Could not save current item. No filename given!");
            ws.text(currentToFavorites.clientId, "message\nNo filename given!");
            currentToFavorites.requested = false;
            return;
          }
          ESP_LOGD(TAG, "saving stream: %s -> %s", currentToFavorites.filename.c_str(), item.url.c_str());
          const char* SAVE_FAILED_MSG{"message\nSaving failed!"};
          File file = FFat.open("/" + currentToFavorites.filename, FILE_WRITE);
          if (!file) {
            ESP_LOGE(TAG, "failed to open file for writing");
            ws.text(currentToFavorites.clientId, SAVE_FAILED_MSG);
            currentToFavorites.filename = "";
            currentToFavorites.requested = false;
            return;
          }
          audio.loop();
          if (file.print(item.url.c_str())) {
            ESP_LOGD(TAG, "FFat file %s written", currentToFavorites.filename.c_str());
            ws.printfAll("message\nAdded '%s' to favorites!", currentToFavorites.filename.c_str());
            favorites.updated = true;
          } else {
            ESP_LOGE(TAG, "FFat writing to %s failed", currentToFavorites.filename.c_str());
            ws.text(currentToFavorites.clientId, SAVE_FAILED_MSG);
          }
          audio.loop();
          file.close();
        }
        break;
      default : ESP_LOGI(TAG, "Unhandled item.type.");
    }
    currentToFavorites.filename = "";
    currentToFavorites.requested = false;
  }

  if (favoriteToPlaylist.requested) {
    File file = FFat.open("/" + favoriteToPlaylist.name);
    String url;
    if (file) {
      while (file.available() && (file.peek() != '\n') && url.length() < 1024) /* only read the first line and limit the size of the resulting string - unknown/leftover files might contain garbage*/
        url += (char)file.read();
      file.close();
    }
    playList.add({HTTP_FAVORITE, favoriteToPlaylist.name, url});
    ESP_LOGD(TAG, "favorite to playlist: %s -> %s", favoriteToPlaylist.name.c_str(), url.c_str());
    ws.printfAll("message\nAdded '%s' to playlist", favoriteToPlaylist.name.c_str());
    if (favoriteToPlaylist.startNow) {
      if (audio.isRunning()) audio.stopSong();
      currentItem = playList.size() - 2;
      playerStatus = PLAYING;
      favoriteToPlaylist.startNow = false;
      favoriteToPlaylist.requested = false;
      return;
    }
    if (!audio.isRunning() && PAUSED != playerStatus) {
      currentItem = playList.size() - 2;
      playerStatus = PLAYING;
    }
    favoriteToPlaylist.requested = false;
  }

  if (deletefavorite.requested) {
    if (!FFat.remove("/" + deletefavorite.name)) {
      ws.text(deletefavorite.clientId, "message\nCould not delete " + deletefavorite.name);
    } else {
      ws.textAll("message\nDeleted favorite " + deletefavorite.name);
      favorites.updated = true;
    }
    deletefavorite.requested = false;
  }

  if (!audio.isRunning() && playList.size() && PLAYING == playerStatus) {
    if (currentItem < playList.size() - 1) {
      currentItem++;
      ESP_LOGD(TAG, "Starting next playlist item: %i", currentItem);
      playListItem item;
      playList.get(currentItem, item);

#ifdef M5STACK_NODE
      M5updateCurrentItemName(item);
      M5updateCurrentAndTotal(currentItem, playList.size());
#endif  //M5STACK_NODE

      switch (item.type) {
        case HTTP_FILE :
          ESP_LOGD(TAG, "STARTING file: %s", item.url.c_str());
          audio_showstation(item.url.substring(item.url.lastIndexOf("/") + 1).c_str());
          audio_showstreamtitle(item.url.substring(0, item.url.lastIndexOf("/")).c_str());
          audio.connecttohost(urlEncode(item.url));
          break;
        case HTTP_STREAM :
          ESP_LOGD(TAG, "STARTING stream: %s", item.url.c_str());
          audio_showstation(item.url.substring(item.url.lastIndexOf("/") + 1).c_str());
          audio_showstreamtitle("");
          audio.connecttohost(urlEncode(item.url));
          break;
        case HTTP_PRESET :
          ESP_LOGD(TAG, "STARTING preset: %s -> %s", preset[item.index].name.c_str(), preset[item.index].url.c_str());
          audio_showstreamtitle("");
          audio_showstation(preset[item.index].name.c_str());
          audio.connecttohost(urlEncode(preset[item.index].url));
          break;
        case HTTP_FAVORITE :
          ESP_LOGD(TAG, "STARTING favorite: %s -> %s", item.name.c_str(), item.url.c_str());
          audio_showstation(item.name.c_str());
          audio_showstreamtitle("");
          audio.connecttohost(urlEncode(item.url));
          break;
        case SDCARD_FILE :
          ESP_LOGD(TAG, "STARTING sd file: %s", item.url.c_str());
          audio.connecttoSD(item.url);
          break;
        default : ESP_LOGE(TAG, "Unhandled item.type.");
      }
    } else {
      playListHasEnded();
    }
    sendCurrentItem();
  }
}
