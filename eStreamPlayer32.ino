#include <FFat.h>
#include <AsyncTCP.h>                                   /* https://github.com/me-no-dev/AsyncTCP */
#include <ESPAsyncWebServer.h>                          /* https://github.com/me-no-dev/ESPAsyncWebServer */
#include <Audio.h>                                      /* https://github.com/schreibfaul1/ESP32-audioI2S */

#include "percentEncode.h"
#include "system_setup.h"
#include "playList.h"
#include "index_htm_gz.h"
#include "icons.h"

#define I2S_MAX_VOLUME      21
#define I2S_INITIAL_VOLUME  5

const char* VERSION_STRING {
  "eStreamPlayer32 v1.0.5"
};

enum {
  PAUSED,
  PLAYING,
  PLAYLISTEND,
} playerStatus{PLAYLISTEND};

#define     NOTHING_PLAYING_VAL   -1
const char* NOTHING_PLAYING_STR   {
  "Nothing playing"
};

/* websocket message headers */
const char* VOLUME_HEADER {
  "volume\n"
};

const char* CURRENT_HEADER{"currentPLitem\n"};
const char* MESSAGE_HEADER{"message\n"};

int currentItem {NOTHING_PLAYING_VAL};

playList_t playList;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

#if defined (A1S_AUDIO_KIT)
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

#if defined (M5STACK_NODE)
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

void M5_displayItemName(const playListItem& item) {
  const int LOC_X{M5.Lcd.width() / 2}, LOC_Y{M5.Lcd.height() / 2};
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setFreeFont(FSS12);
  M5.Lcd.fillRect(0, LOC_Y, 320, M5.Lcd.fontHeight(GFXFF), TFT_BLACK); //clear area
  if (!item.name && item.type == HTTP_FAVORITE) return; /* shortcut to just delete itemName on the lcd is to call 'M5_displayItemName({HTTP_FAVORITE})' */
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

void M5_displayCurrentAndTotal() {
  const int LOC_X{M5.Lcd.width() / 2}, LOC_Y{70};
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setFreeFont(FSS18);
  M5.Lcd.fillRect(0, LOC_Y, 320, M5.Lcd.fontHeight(GFXFF), TFT_BLACK); //clear area
  M5.Lcd.setTextDatum(TC_DATUM); // TC = Top Center
  String currentAndTotal;
  currentAndTotal.concat(currentItem + 1); /* we are talking to humans here */
  currentAndTotal.concat(" / ");
  currentAndTotal.concat(playList.size());
  M5.Lcd.drawString(currentAndTotal, LOC_X, LOC_Y);
  M5.Lcd.display();
}
#endif  //M5STACK_NODE

Audio audio;

struct {
  bool waiting{false};
  String url;
  uint32_t clientId;
} newUrl;

inline __attribute__((always_inline))
void updateHighlightedItemOnClients() {
  ws.textAll(CURRENT_HEADER + String(currentItem));
}

inline __attribute__((always_inline))
void muteVolumeAndStopSong() {
  const auto saved = audio.getVolume();
  audio.setVolume(0);
  audio.stopSong();
  audio.loop();
  audio.setVolume(saved);
}

const String urlEncode(const String& s) {
  //https://en.wikipedia.org/wiki/Percent-encoding
  String encodedstr{""};
  for (int i = 0; i < s.length(); i++) {
    switch (s.charAt(i)) {
      case ' ' : encodedstr.concat("%20");
        break;
      case '!' : encodedstr.concat("%21");
        break;
      case '&' : encodedstr.concat("%26");
        break;
      case  39 : encodedstr.concat("%27"); //39 == single quote '
        break;
      default : encodedstr.concat(s.charAt(i));
    }
  }
  ESP_LOGD(TAG, "encoded url: %s", encodedstr.c_str());
  return encodedstr;
}

void playListHasEnded() {
  currentItem = NOTHING_PLAYING_VAL;
  playerStatus = PLAYLISTEND;
  audio_showstation(NOTHING_PLAYING_STR);
  audio_showstreamtitle(VERSION_STRING);
  updateHighlightedItemOnClients();
  ESP_LOGD(TAG, "End of playlist.");

#if defined (M5STACK_NODE)
  M5_displayItemName({HTTP_FAVORITE});
  M5_displayCurrentAndTotal();
#endif  //M5STACK_NODE
}

void updateFavoritesOnClients() {
  String s;
  ws.textAll(favoritesToString(s));
  ESP_LOGD(TAG, "Favorites and clients are updated.");
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
  snprintf(streamtitle, sizeof(streamtitle), "streamtitle\n%s", percentEncode(info).c_str());
  ESP_LOGD(TAG, "streamtitle: %s", streamtitle);
  ws.printfAll(streamtitle);
}

void audio_id3data(const char *info) {
  ESP_LOGI(TAG, "id3data: %s", info);
  ws.printfAll("id3data\n%s", info);
}

// https://sookocheff.com/post/networking/how-do-websockets-work/
// https://noio-ws.readthedocs.io/en/latest/overview_of_websockets.html

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(VOLUME_HEADER + String(audio.getVolume()));
    {
      String s;
      client->text(playList.toString(s));
      client->text(favoritesToString(s));
    }
    client->text(CURRENT_HEADER + String(currentItem));
    client->text(showstation);
    if (currentItem != NOTHING_PLAYING_VAL)
      client->text(streamtitle);
    else {
      char buffer[200];
      snprintf(buffer, sizeof(buffer), "streamtitle\n%s", VERSION_STRING);
      client->text(buffer);
    }
    ESP_LOGD(TAG, "ws[%s][%u] connect", server->url(), client->id());
    return;
  } else if (type == WS_EVT_DISCONNECT) {
    ESP_LOGD(TAG, "ws[%s][%u] disconnect: %u", server->url(), client->id());
    return;
  } else if (type == WS_EVT_ERROR) {
    ESP_LOGE(TAG, "ws[%s][%u] error(%u): %s", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
    return;
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;

    // here all data is contained in a single packet
    if (info->final && info->index == 0 && info->len == len) {
      if (info->opcode == WS_TEXT) {
        data[len] = 0;

        ESP_LOGD(TAG, "ws request: %s", reinterpret_cast<char*>(data));

        char *pch = strtok(reinterpret_cast<char*>(data), "\n");
        if (!pch) return;

        if (!strcmp("volume", pch)) {
          pch = strtok(NULL, "\n");
          if (pch) {
            const uint8_t volume = atoi(pch);
            audio.setVolume(volume > I2S_MAX_VOLUME ? I2S_MAX_VOLUME : volume);
            ws.textAll(VOLUME_HEADER + String(audio.getVolume()));
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
          const uint32_t itemsAdded{playList.size() - previousSize};
          client->printf("%sAdded %i items to playlist", MESSAGE_HEADER, itemsAdded);

          ESP_LOGD(TAG, "Added %i items to playlist", addedSongs);

          if (!itemsAdded) return;

          if (startnow) {
            if (audio.isRunning()) muteVolumeAndStopSong();
            currentItem = previousSize - 1;
            playerStatus = PLAYING;
            return;
          }
          // start playing at the correct position if not already playing
          if (!audio.isRunning() && PAUSED != playerStatus) {
            currentItem = previousSize - 1;
            playerStatus = PLAYING;
          }
          return;
        }

        else if (!strcmp("clearlist", pch)) {
          if (!playList.size()) return;
          muteVolumeAndStopSong();
          playList.clear();
          playListHasEnded();
          return;
        }

        else if (!strcmp("playitem", pch)) {
          pch = strtok(NULL, "\n");
          if (pch) {
            const uint32_t index = atoi(pch);
            if (index < playList.size()) {
              currentItem = index - 1;
              muteVolumeAndStopSong();
              playerStatus = PLAYING;
            }
          }
          return;
        }

        else if (!strcmp("deleteitem", pch)) {
          if (!playList.size()) return;
          pch = strtok(NULL, "\n");
          if (!pch) return;
          const uint32_t index = atoi(pch);
          if (index == currentItem) {
            muteVolumeAndStopSong();
            playList.remove(index);
            if (!playList.size()) {
              playListHasEnded();
              return;
            }
            currentItem--;
            return;
          }
          if (index < playList.size()) {
            playList.remove(index);
            if (!playList.size()) {
              playListHasEnded();
              return;
            }
          } else return;

          if (currentItem != NOTHING_PLAYING_VAL && index < currentItem)
            currentItem--;
          return;
        }
        /*
                else if (!strcmp("pause", pch)) {
                  switch (playerStatus) {
                    case PAUSED :{
                      const uint8_t savedVolume = audio.getVolume();
                      audio.setVolume(0);
                      audio.pauseResume();
                      audio.loop();
                      audio.setVolume(savedVolume);
                      playerStatus = PLAYING;
                      //send play icon to clients
                    }
                    break;
                    case PLAYING : {
                      const uint8_t savedVolume = audio.getVolume();
                      audio.setVolume(0);
                      audio.pauseResume();
                      delay(2);
                      audio.setVolume(savedVolume);
                      playerStatus = PAUSED;
                      //send pause icon to clients
                    }
                    break;
                    default : {};
                  }
                }
        */
        else if (!strcmp("previous", pch)) {
          if (PLAYLISTEND == playerStatus) return;
          ESP_LOGD(TAG, "current: %i size: %i", currentItem, playList.size());
          if (currentItem > 0) {
            muteVolumeAndStopSong();
            currentItem--;
            currentItem--;
            return;
          }
          else return;
        }

        else if (!strcmp("next", pch)) {
          if (PLAYLISTEND == playerStatus) return;
          ESP_LOGD(TAG, "current: %i size: %i", currentItem, playList.size());
          if (currentItem < playList.size() - 1) {
            muteVolumeAndStopSong();
            return;
          }
          else return;
        }

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
          if (pch)
            handleCurrentToFavorites((String)pch, client->id());
          return;
        }

        else if (!strcmp("favoritetoplaylist", pch) ||
                 !strcmp("_favoritetoplaylist", pch)) {
          const bool startNow = (pch[0] == '_');
          pch = strtok(NULL, "\n");
          if (pch)
            handleFavoriteToPlaylist((String)pch, startNow);
          return;
        }

        else if (!strcmp("deletefavorite", pch)) {
          pch = strtok(NULL, "\n");
          if (pch) {
            if (!FFat.remove("/" + (String)pch)) {
              ws.printf(client->id(), "%sCould not delete %s", MESSAGE_HEADER, pch);
            } else {
              ws.printfAll("%sDeleted favorite %s", MESSAGE_HEADER, pch);
              updateFavoritesOnClients();
            }
          }
          return;
        }

        else if (!strcmp("presetstation", pch) ||
                 !strcmp("_presetstation", pch)) {
          const bool startnow = (pch[0] == '_');
          const uint32_t index = atoi(strtok(NULL, "\n"));
          if (index < sizeof(preset) / sizeof(source)) { // only add really existing presets to the playlist
            playList.add({HTTP_PRESET, "", "", index});

            if (!playList.isUpdated) return;

            ESP_LOGD(TAG, "Added '%s' to playlist", preset[index].name.c_str());
            client->printf("%sAdded '%s' to playlist", MESSAGE_HEADER, preset[index].name.c_str());

            if (startnow) {
              if (audio.isRunning()) muteVolumeAndStopSong();
              currentItem = playList.size() - 2;
              playerStatus = PLAYING;
              return;
            }

            // start playing at the correct position if not already playing
            if (!audio.isRunning() && PAUSED != playerStatus) {
              currentItem = playList.size() - 2;
              playerStatus = PLAYING;
            }
            return;
          }
        }
      }
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      static char* buffer = nullptr;
      if (info->index == 0) {
        if (info->num == 0) {
          ESP_LOGD(TAG, "ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
        }

        ESP_LOGD(TAG, "ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
        //allocate info->len bytes of memory

        if (!buffer) {
          // we need at least twice the amount of free memory that is requested (buffer + playlist data)
          if (info->len * 2 > ESP.getFreeHeap()) {
            client->printf("%sout of memory", MESSAGE_HEADER);
            client->close();
            return;
          }
          buffer = new char[info->len + 1];
        }
        else {
          ESP_LOGE(TAG, "request for buffer but transfer already running. dropping client %i multi frame transfer", client->id());
          client->printf("%sservice currently unavailable", MESSAGE_HEADER);
          client->close();
          return;
        }
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

            client->printf("%sAdded %i items to playlist", MESSAGE_HEADER, playList.size() - previousSize);

            if (!playList.isUpdated) return;

            if (startnow) {
              if (audio.isRunning()) muteVolumeAndStopSong();
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

const char* HEADER_MODIFIED_SINCE = "If-Modified-Since";

static inline __attribute__((always_inline)) bool htmlUnmodified(const AsyncWebServerRequest * request, const char* date) {
  return request->hasHeader(HEADER_MODIFIED_SINCE) && request->header(HEADER_MODIFIED_SINCE).equals(date);
}

void setup() {
  audio.setVolume(0);

  btStop();

#if defined (M5STACK_NODE)
  M5.begin(true, false);
  M5.Lcd.setBrightness(15);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setTextDatum(TC_DATUM); // TC = Top Center
  M5.Lcd.setFreeFont(FSS12);
  const uint16_t ypos = M5.Lcd.fontHeight(GFXFF);
  M5.Lcd.drawString("-eStreamPlayer32-", M5.Lcd.width() / 2, 0);
#endif  //M5STACK_NODE

  if (psramInit()) {
    ESP_LOGI(TAG, "%.2fMB PSRAM free.", ESP.getFreePsram() / (1024.0 * 1024));
  }

  /* check if a ffat partition is defined and halt the system if it is not defined*/
  if (!esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "ffat")) {
    ESP_LOGE(TAG, "FATAL ERROR! No FFat partition defined. System is halted.\nCheck 'Tools>Partition Scheme' in the Arduino IDE and select a partition table with a FFat partition.");

#if defined (M5STACK_NODE)
    M5_displayItemName({HTTP_FAVORITE, "ERROR no FFat partition!"});
#endif  //M5STACK_NODE

    while (true) delay(1000); /* system is halted */
  }

  /* partition is defined - try to mount it */
  if (FFat.begin(0, "", 2)) // see: https://github.com/lorol/arduino-esp32fs-plugin#notes-for-fatfs
    ESP_LOGI(TAG, "FFat mounted.");

  /* partition is present, but does not mount so now we just format it */
  else {
    ESP_LOGI(TAG, "Formatting...");

#if defined (M5STACK_NODE)
    M5_displayItemName({HTTP_FAVORITE, "Formatting.Please wait..."});
#endif  //M5STACK_NODE

    if (!FFat.format(true, (char*)"ffat") || !FFat.begin(0, "", 2)) {
      ESP_LOGE(TAG, "FFat error while formatting. Halting.");

#if defined (M5STACK_NODE)
      M5_displayItemName({HTTP_FAVORITE, "ERROR formatting!"});
#endif  //M5STACK_NODE

      while (true) delay(1000); /* system is halted */;
    }
  }

  ESP_LOGI(TAG, "Found %i presets", sizeof(preset) / sizeof(source));

#if defined (A1S_AUDIO_KIT)
  ESP_LOGI(TAG, "Starting 'A1S_AUDIO_KIT' dac");
  if (!dac.begin(I2C_SDA, I2C_SCL))
  {
    ESP_LOGE(TAG, "AC101 dac failed to init! Halting.");
    while (true) delay(1000); /* system is halted */;
  }
  audio.i2s_mclk_pin_select(I2S_MCLK);
  dac.SetVolumeSpeaker(30);
  dac.SetVolumeHeadphone(63);
#endif  //A1S_AUDIO_KIT

#if defined (M5STACK_NODE)
  M5_displayItemName({HTTP_FAVORITE, "Connecting..."});
#endif  //M5STACK_NODE

  if (SET_STATIC_IP && !WiFi.config(STATIC_IP, GATEWAY, SUBNET, PRIMARY_DNS, SECONDARY_DNS)) {
    ESP_LOGE(TAG, "Setting static IP failed");
  }

  WiFi.begin(SSID, PSK);
  WiFi.setSleep(false);
  WiFi.waitForConnectResult();

#if defined (M5STACK_NODE)
  if (!WiFi.isConnected()) {
    M5_displayItemName({HTTP_FAVORITE, "HALTED: No network!"});
    while (true) delay(1000); /* system is halted */;
  }
  M5_displayItemName({HTTP_FAVORITE});
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setFreeFont(FF6);
  M5.Lcd.drawString(WiFi.localIP().toString() , M5.Lcd.width() / 2, ypos);
  ESP_LOGI(TAG, "Starting 'M5STACK_NODE' dac");
  if (!dac.begin(I2C_SDA, I2C_SCL))
  {
    ESP_LOGE(TAG, "WM8978 dac failed to init! Halting.");
    M5_displayItemName({HTTP_FAVORITE, "HALTED: No WM8978 DAC!"});
    while (true) delay(1000); /* system is halted */;
  }
  M5_displayCurrentAndTotal();
  M5.Lcd.display();
  dac.setSPKvol(0);
  dac.setHPvol(0, 0);
  audio.i2s_mclk_pin_select(I2S_MCLK);
  dac.setHPvol(63, 63);
#endif  //M5STACK_NODE

#if defined (GENERIC_I2S_DAC)
  ESP_LOGI(TAG, "Starting 'GENERIC_I2S_DAC' - BCK=%i LRC=%i DOUT=%i", I2S_BCK, I2S_WS, I2S_DOUT);
#endif  //GENERIC_I2S_DAC

  if (!WiFi.isConnected()) {
    ESP_LOGE(TAG, "Could not connect to Wifi! System halted! Check 'wifi_setup.h'!");
    while (true) delay(1000); /* system is halted */;
  }

  ESP_LOGI(TAG, "WiFi: %s", WiFi.localIP().toString().c_str());

  /* sync with ntp */
  configTzTime(TIMEZONE, NTP_POOL);

  struct tm timeinfo {
    0
  };

  ESP_LOGI(TAG, "Waiting for NTP sync..");

  while (!getLocalTime(&timeinfo, 0))
    delay(10);

  time_t bootTime;
  time(&bootTime);
  static char modifiedDate[30];
  strftime(modifiedDate, sizeof(modifiedDate), "%a, %d %b %Y %X GMT", gmtime(&bootTime));

  static const char* HTML_MIMETYPE{"text/html"};
  static const char* HEADER_LASTMODIFIED{"Last-Modified"};
  static const char* CONTENT_ENCODING_HEADER{"Content-Encoding"};
  static const char* CONTENT_ENCODING_VALUE{"gzip"};

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, HTML_MIMETYPE, index_htm_gz, index_htm_gz_len);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    response->addHeader(CONTENT_ENCODING_HEADER, CONTENT_ENCODING_VALUE);
    request->send(response);
  });

  server.on("/stations", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncResponseStream *response = request->beginResponseStream(HTML_MIMETYPE);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    for (int i = 0; i < sizeof(preset) / sizeof(source); i++) {
      response->printf("%s\n", preset[i].name.c_str());
    }
    request->send(response);
  });

  server.on("/scripturl", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncResponseStream *response = request->beginResponseStream(HTML_MIMETYPE);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    response->print(SCRIPT_URL);
    request->send(response);
  });

  static const char* SVG_MIMETYPE{"image/svg+xml"};

  server.on("/radioicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, radioicon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.on("/playicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, playicon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.on("/libraryicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, libraryicon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.on("/favoriteicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, favoriteicon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.on("/streamicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, pasteicon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.on("/deleteicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, deleteicon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.on("/addfoldericon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, addfoldericon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.on("/emptyicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, emptyicon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.on("/starticon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, SVG_MIMETYPE, starticon);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.onNotFound([](AsyncWebServerRequest * request) {
    ESP_LOGE(TAG, "404 - Not found: 'http://%s%s'", request->host().c_str(), request->url().c_str());
    request->send(404);
  });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  server.begin();
  ESP_LOGI(TAG, "Ready to rock!");

  audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT);
  audio.setVolume(I2S_INITIAL_VOLUME);
}

String& favoritesToString(String & s) {
  File root = FFat.open("/");
  s = "";
  if (!root || !root.isDirectory()) {
    ESP_LOGE(TAG, "ERROR - root folder problem");
    return s;
  }
  s = "favorites\n";
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      s.concat(file.name());
      s.concat("\n");
    }
    file = root.openNextFile();
  }
  return s;
}

bool startPlaylistItem(const playListItem & item) {
  switch (item.type) {
    case HTTP_FILE :
      ESP_LOGD(TAG, "STARTING file: %s", item.url.c_str());
      audio_showstation(item.url.substring(item.url.lastIndexOf("/") + 1).c_str());
      audio_showstreamtitle(item.url.substring(0, item.url.lastIndexOf("/")).c_str());
      audio.connecttohost(urlEncode(item.url).c_str());
      break;
    case HTTP_STREAM :
      ESP_LOGD(TAG, "STARTING stream: %s", item.url.c_str());
      audio_showstation(item.url.substring(item.url.lastIndexOf("/") + 1).c_str());
      audio_showstreamtitle("");
      audio.connecttohost(urlEncode(item.url).c_str());
      break;
    case HTTP_PRESET :
      ESP_LOGD(TAG, "STARTING preset: %s -> %s", preset[item.index].name.c_str(), preset[item.index].url.c_str());
      audio_showstreamtitle("");
      audio_showstation(preset[item.index].name.c_str());
      audio.connecttohost(urlEncode(preset[item.index].url).c_str());
      break;
    case HTTP_FAVORITE :
      ESP_LOGD(TAG, "STARTING favorite: %s -> %s", item.name.c_str(), item.url.c_str());
      audio_showstation(item.name.c_str());
      audio_showstreamtitle("");
      audio.connecttohost(urlEncode(item.url).c_str());
      break;
    case SDCARD_FILE :
      ESP_LOGD(TAG, "STARTING sd file: %s", item.url.c_str());
      audio.connecttoSD(item.url.c_str());
      break;
    default : ESP_LOGE(TAG, "Unhandled item.type.");
  }
  return audio.isRunning();
}

bool saveItemToFavorites(const playListItem & item, const String & filename) {
  switch (item.type) {
    case HTTP_FILE :
      ESP_LOGD(TAG, "file (wont save)%s", item.url.c_str());
      return false;
      break;
    case HTTP_PRESET :
      ESP_LOGD(TAG, "preset (wont save) %s %s", preset[item.index].name.c_str(), preset[item.index].url.c_str());
      return false;
      break;
    case HTTP_STREAM :
    case HTTP_FAVORITE :
      {
        if (filename.equals("")) {
          ESP_LOGE(TAG, "Could not save current item. No filename given!");
          return false;
        }
        ESP_LOGD(TAG, "saving stream: %s -> %s", filename.c_str(), item.url.c_str());
        File file = FFat.open("/" + filename, FILE_WRITE);
        if (!file) {
          ESP_LOGE(TAG, "failed to open file for writing");
          return false;
        }
        bool result = file.print(item.url.c_str());
        file.close();
        ESP_LOGD(TAG, "%s writing to '%s'", result ? "ok" : "WARNING - failed", filename);
        return result;
      }
      break;
    default :
      {
        ESP_LOGW(TAG, "Unhandled item.type.");
        return false;
      }
  }
}

void handlePastedUrl() {

  if (playList.size() > PLAYLIST_MAX_ITEMS - 1) {
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "%sPlaylist is full.", MESSAGE_HEADER);
    ws.text(newUrl.clientId, buffer);
    return;
  }

  ESP_LOGI(TAG, "STARTING new url: %s with %i items in playList", newUrl.url.c_str(), playList.size());
  muteVolumeAndStopSong();
  audio_showstreamtitle("starting new stream");
  audio_showstation("");
  const playListItem item {HTTP_STREAM, newUrl.url, newUrl.url};
  if (startPlaylistItem(item)) {
    ESP_LOGD(TAG, "url started successful");
    playList.add(item);

    currentItem = playList.size() - 1;
    playerStatus = PLAYING;
    audio_showstation(newUrl.url.c_str());

#if defined (M5STACK_NODE)
    M5_displayItemName({HTTP_STREAM, "", newUrl.url});
#endif //M5STACK_NODE

  }
  else {
    char buff[100];
    snprintf(buff, sizeof(buff), "%sFailed to play stream", MESSAGE_HEADER);
    ws.text(newUrl.clientId, buff);
    playListHasEnded();
    ESP_LOGI(TAG, "url failed to start");
  }
}

void handleFavoriteToPlaylist(const String & filename, const bool startNow) {
  File file = FFat.open("/" + filename);
  String url;
  if (file) {
    while (file.available() && (file.peek() != '\n') && url.length() < 1024) /* only read the first line and limit the size of the resulting string - unknown/leftover files might contain garbage*/
      url += (char)file.read();
    file.close();
  }
  else {
    ESP_LOGE(TAG, "Could not open %s", filename.c_str());
    ws.printfAll("%sCould not add '%s' to playlist", MESSAGE_HEADER, filename.c_str());
    return;
  }
  playList.add({HTTP_FAVORITE, filename, url});

  if (!playList.isUpdated) return;

  ESP_LOGD(TAG, "favorite to playlist: %s -> %s", filename.c_str(), url.c_str());
  ws.printfAll("%sAdded '%s' to playlist", MESSAGE_HEADER, filename.c_str());
  if (startNow) {
    if (audio.isRunning()) muteVolumeAndStopSong();
    currentItem = playList.size() - 2;
    playerStatus = PLAYING;
    return;
  }
  if (!audio.isRunning() && PAUSED != playerStatus) {
    currentItem = playList.size() - 2;
    playerStatus = PLAYING;
  }
}

void handleCurrentToFavorites(const String & filename, const uint32_t clientId) {
  playListItem item;
  playList.get(currentItem, item);

  if (saveItemToFavorites(item, filename)) {
    ws.printfAll("%sAdded '%s' to favorites!", MESSAGE_HEADER, filename.c_str());
    updateFavoritesOnClients();
  }
  else
    ws.printf(clientId, "%sSaving '%s' failed!", MESSAGE_HEADER, filename.c_str());
}

void startCurrentItem() {
  playListItem item;
  playList.get(currentItem, item);

#if defined (M5STACK_NODE)
  M5_displayItemName(item);
  M5_displayCurrentAndTotal();
#endif  //M5STACK_NODE

  ESP_LOGD(TAG, "Starting playlist item: %i", currentItem);

  if (!startPlaylistItem(item))
    ws.printfAll("error - could not start %s", (item.type == HTTP_PRESET) ? preset[item.index].url.c_str() : item.url.c_str());

  updateHighlightedItemOnClients();
}

void loop() {

#if defined (M5STACK_NODE)
  M5.update();

  if (M5.BtnA.wasReleasefor(10)) {
    static bool speakerstate{false};
    speakerstate = !speakerstate;
    dac.setSPKvol(speakerstate ? 40 : 0);
    ESP_LOGD(TAG, "Speaker %s", speakerstate ? "on" : "off");
  }
#endif  //M5STACK_NODE

  audio.loop();

  ws.cleanupClients();

  if (playList.isUpdated) {
    {
      String s;
      ws.textAll(playList.toString(s));
    }

#if defined (M5STACK_NODE)
    M5_displayCurrentAndTotal();
#endif

    ESP_LOGI(TAG, "Playlist updated. %i items. Free mem: %i", playList.size(), ESP.getFreeHeap());

    updateHighlightedItemOnClients();

    playList.isUpdated = false;
  }

  if (newUrl.waiting) {
    handlePastedUrl();
    newUrl.waiting = false;
  }

  if (!audio.isRunning() && playList.size() && PLAYING == playerStatus) {
    if (currentItem < playList.size() - 1) {
      currentItem++;
      startCurrentItem();
    }
    else
      playListHasEnded();
  }
}
