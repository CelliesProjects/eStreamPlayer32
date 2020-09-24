#include <FFat.h>
#include <AsyncTCP.h>                                   /* https://github.com/me-no-dev/AsyncTCP */
#include <ESPAsyncWebServer.h>                          /* https://github.com/me-no-dev/ESPAsyncWebServer */
#include <WM8978.h>
#include <Audio.h>

#include "wifi_setup.h"
#include "playList.h"
#include "index_htm.h"
#include "icons.h"

/* webserver core */
#define HTTP_RUN_CORE 1

/* M5Stack Node WM8978 I2C pins */
#define I2C_SDA     21
#define I2C_SCL     22

/* dev board with wm8978 breakout  */
#define I2S_BCK     21
#define I2S_WS      26
#define I2S_DOUT    22
//#define I2S_DIN     34

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
  uint32_t clientId;
} newUrl;

bool currentToFavorites{false};
struct {
  bool requested{false};
  bool updated{false};
  uint32_t clientId;
} favorites;

struct {
  bool requested{false};
  String name;
} favoriteToPlaylist;

struct {
  bool requested{false};
  String name;
  uint32_t clientId;
} deletefavorite;

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
    /*
      //else if (c == '@') encodedstr += "%40";
      //else if (c == '[') encodedstr += "%5B";
      //else if (c == ']') encodedstr += "%5D";
    */
  }
  ESP_LOGD(TAG, "encoded url: %s", encodedstr.c_str());
  return encodedstr;
}

void playListHasEnded() {
  currentItem = -1;
  playerStatus = PLAYLISTEND;
  audio_showstation("Nothing playing");
  audio_showstreamtitle("&nbsp;");
}

/*
  void audio_info(const char *info) {
  ESP_LOGI(TAG, "Info: %s", info);
  }
*/

static char showstation[200]; /////////////////////////////////////////////////// These are kept to update new clients only on connection
void audio_showstation(const char *info) {
  ESP_LOGD(TAG, "showstation: %s", info);
  snprintf(showstation, sizeof(showstation), "showstation\n%s", info);
  ws.textAll(showstation);
}
/*
  void audio_bitrate(const char *info) {
  ESP_LOGI(TAG, "bitrate: %s", info);
  ws.printfAll("bitrate\n%s", info);
  }
*/

static char streamtitle[200]; /////////////////////////////////////////////////// These are kept to update new clients only on connection
void audio_showstreamtitle(const char *info) {
  ESP_LOGD(TAG, "streamtitle: %s", info);
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
/*
  void audio_eof_mp3(const char *info) {
  ESP_LOGI(TAG, "EOF");
  audio.stopSong();
  }
*/
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
    favorites.requested = true;
    favorites.clientId = client->id();
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
            playList.add({HTTP_FILE, "", pch});
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
          playListHasEnded();
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
              playListHasEnded();
              return;
            }
            currentItem--;
            return;
          }
          if (num > -1 && num < playList.size()) {
            playList.remove(num);
            if (!playList.size()) {
              playListHasEnded();
              return;
            }
          } else {
            return;
          }
          if (num < currentItem) {
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
          ESP_LOGD(TAG, "received new url: %s", pch);
          newUrl.url = pch;
          newUrl.clientId = client->id();
          newUrl.waiting = true;
          return;
        }

        else if (!strcmp("currenttofavorites", pch)) {
          currentToFavorites = true;
        }

        else if (!strcmp("favorites", pch)) {
          favorites.clientId = client->id();
          favorites.requested = true;
        }

        else if (!strcmp("favoritetoplaylist", pch)) {
          favoriteToPlaylist.name = strtok(NULL, "\n");
          favoriteToPlaylist.requested = true;
        }

        else if (!strcmp("deletefavorite", pch)) {
          deletefavorite.name = strtok(NULL, "\n");
          deletefavorite.requested = true;
        }

        else if (!strcmp("presetstation", pch)) {
          const uint32_t index = atoi(strtok(NULL, "\n"));
          if (index < sizeof(preset) / sizeof(station)) { // only add really existing presets to the playlist
            playList.add({HTTP_PRESET, "", "", index});
            ESP_LOGD(TAG, "Added '%s' to playlist", preset[index].name.c_str());
            client->printf("message\nAdded '%s' to playlist", preset[index].name.c_str());
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
          if (!strcmp("toplaylist", pch)) {
            ESP_LOGD(TAG, "multi frame playlist");

            int previousSize = playList.size();
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

  ////////////////////////////////////////////////////////////  serve icons as files - use the cache to only serve each icon once
  // TODO: set a 304 to save on bandwidth

  server.on("/radioicon.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "image/svg+xml", radioicon);
    response->addHeader("Vary", "Accept-Encoding");
    request->send(response);
  });
  /*
    server.on("/delete.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "image/svg+xml", deleteicon);
      response->addHeader("Vary", "Accept-Encoding");
      request->send(response);
    });

    server.on("/empty.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "image/svg+xml", emptyicon);
      response->addHeader("Vary", "Accept-Encoding");
      request->send(response);
    });

    server.on("/folderup.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "image/svg+xml", folderupicon);
      response->addHeader("Vary", "Accept-Encoding");
      request->send(response);
    });

    server.on("/save.svg", HTTP_GET, [] (AsyncWebServerRequest * request) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "image/svg+xml", saveicon);
      response->addHeader("Vary", "Accept-Encoding");
      request->send(response);
    });

      server.on("/thrashcan.svg", HTTP_GET, [] (AsyncWebServerRequest * request) { // is the effing same icon as delete.svg!!
        AsyncWebServerResponse *response = request->beginResponse_P(200, "image/svg+xml", thrashcanicon);
        response->addHeader("Vary","Accept-Encoding");
        request->send(response);
      });
  */
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



  /* check if a ffat partition is defined and halt the system if it is not defined*/
  if (!esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "ffat")) {
    ESP_LOGE( TAG, "No FFat partition defined. Halting.\nCheck 'Tools>Partition Scheme' in the Arduino IDE and select a FFat partition." );
    while (true) delay(1000); /* system is halted */
  }

  /* partition is defined - try to mount it */
  if ( FFat.begin() )
    ESP_LOGI( TAG, "FFat mounted." );

  /* partition is present, but does not mount so now we just format it */
  else {
    const char * formatStr = "Formatting...";
    ESP_LOGI( TAG, "%s", formatStr );
    if (!FFat.format( true, (char*)"ffat" ) || !FFat.begin()) {
      ESP_LOGE( TAG, "FFat error while formatting. Halting." );
      while (true) delay(1000); /* system is halted */;
    }
  }

  WiFi.begin(SSID, PSK);
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

  ESP_LOGI(TAG, "We have %i presets", sizeof(preset) / sizeof(station));
}

inline __attribute__((always_inline))
void sendCurrentItem() {
  ws.textAll("currentPLitem\n" + String(currentItem));
}

void loop() {
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
  if (playList.isUpdated) {
    ESP_LOGI(TAG, "Playlist updated. %i items. Free mem: %i", playList.size(), ESP.getFreeHeap());
    ws.textAll(playList.toClientString());
    sendCurrentItem();
    playList.isUpdated = false;
  }

  if (newClient.connected) {
    ws.text(newClient.id, playList.toClientString());
    ws.text(newClient.id, String("currentPLitem\n" + currentItem));
    ws.text(newClient.id, showstation);
    ws.text(newClient.id, streamtitle);
    newClient.connected = false;
  }

  if (newUrl.waiting) {
    ESP_LOGI(TAG, "trying new url: %s with %i items in playList", newUrl.url.c_str(), playList.size());
    if (audio.connecttohost(urlEncode(newUrl.url))) {
      playList.add({HTTP_STREAM, newUrl.url, newUrl.url});
      currentItem = playList.size() - 1;
      playerStatus = PLAYING;
      playList.isUpdated = true;
    }
    else {
      playListHasEnded();
      ws.text(newUrl.clientId, "message\nFailed to play stream");
      sendCurrentItem();
    }
    newUrl.waiting = false;
  }

  if (favorites.requested || favorites.updated) {
    ESP_LOGI(TAG, "Favorites requested by client %i", favorites.clientId);
    File root = FFat.open("/");
    if (!root) {
      ESP_LOGE(TAG, "ERROR- failed to open root");
      if (favorites.requested)
        ws.text(favorites.clientId, "message\nNo FFat found");
      else
        ws.textAll("message\nNo FFat found");
      return;
    }
    if (!root.isDirectory()) {
      ESP_LOGE(TAG, "ERROR- root is not a directory");
      if (favorites.requested)
        ws.text(favorites.clientId, "message\nNo root found");
      else
        ws.textAll("message\nNo root found");
      return;
    }

    String response{"favorites\n"};
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        response += file.name() + String("\n");
      }
      file = root.openNextFile();
    }
    if (favorites.requested)
      ws.text(favorites.clientId, response);
    else
      ws.textAll(response);

    if (favorites.requested) favorites.requested = false;
    if (favorites.updated) favorites.updated = false;
  }

  if (currentToFavorites) {
    playListItem item;
    playList.get(currentItem, item);

    if (item.type == HTTP_FILE) {
      ESP_LOGI(TAG, "file (wont save) %s %s", &showstation[12], item.url.c_str());
    }

    else if (item.type == HTTP_PRESET) {
      ESP_LOGI(TAG, "preset (wont save) %s %s", &showstation[12], preset[item.index].url.c_str());
    }

    else if (item.type == HTTP_STREAM || item.type == HTTP_FAVORITE) {
      ESP_LOGI(TAG, "saving stream: %s -> %s", &showstation[12], item.url.c_str());
      File file = FFat.open("/" + String(&showstation[12]), FILE_WRITE);
      if (!file) {
        ESP_LOGE(TAG, "failed to open file for writing");
        currentToFavorites = false;
        return;
      }
      audio.loop();
      if (file.print(item.url.c_str())) {
        ESP_LOGD(TAG, "FFat file %s written", &showstation[12]);
        favorites.updated = true;
      } else {
        ESP_LOGE(TAG, "FFat writing to %s failed", &showstation[12]);
      }
      audio.loop();
      file.close();
    }
    currentToFavorites = false;
  }

  if (favoriteToPlaylist.requested) {
    File file = FFat.open("/" + favoriteToPlaylist.name);
    String url;
    if (file) {
      while (file.available()) url += (char)file.read();
      file.close();
    }
    playList.add({HTTP_FAVORITE, favoriteToPlaylist.name, url});
    ESP_LOGI(TAG, "favorite to playlist: %s -> %s", favoriteToPlaylist.name.c_str(), url.c_str());
    ws.printfAll("message\nAdded '%s' to playlist", favoriteToPlaylist.name.c_str());
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
      ESP_LOGI(TAG, "Starting playlist item: %i", currentItem);
      playListItem item;
      playList.get(currentItem, item);

      if (HTTP_FILE == item.type || HTTP_STREAM == item.type) {
        ESP_LOGI(TAG, "file or stream: %s", item.url.c_str());
        audio.connecttohost(urlEncode(item.url));
        audio_showstreamtitle(item.url.substring(0, item.url.lastIndexOf("/")).c_str());
      }
      else if (HTTP_PRESET == item.type) {
        ESP_LOGI(TAG, "preset: %s -> %s", preset[item.index].name.c_str(), preset[item.index].url.c_str());
        audio_showstreamtitle("&nbsp;");
        audio.connecttohost(urlEncode(preset[item.index].url));
      }

      else if (HTTP_FAVORITE == item.type) {
        ESP_LOGI(TAG, "favorite: %s -> %s", item.name.c_str(), item.url.c_str());
        audio_showstreamtitle("&nbsp;");
        audio.connecttohost(urlEncode(item.url));
      }

      else if (SDCARD_FILE == item.type) {
        ESP_LOGI(TAG, "sd file: %s", item.url.c_str());
        audio.connecttoSD(item.url);
      }
    } else {
      ESP_LOGI(TAG, "End of playlist.");
      playListHasEnded();
    }
    sendCurrentItem();
  }
}
