#ifndef __PLAYLIST_H
#define __PLAYLIST_H

#include <Arduino.h>
#include <vector>
#include "presets.h"

#define PLAYLIST_MAX_ITEMS 100

enum streamType                  {HTTP_FILE,   HTTP_STREAM,   HTTP_FAVORITE,   HTTP_PRESET,   SDCARD_FILE};
static const char * typeStr[] = {"FILE",      "STREAM",      "FAVO",          "PRESET",      "SDCARD" };

struct playListItem {
  streamType type;
  String name;
  String url;
  uint32_t index;
};

class playList {

  public:
    playList() {
      ESP_LOGD(TAG, "allocating %i items", PLAYLIST_MAX_ITEMS);
      list.reserve(PLAYLIST_MAX_ITEMS);
    }
    ~playList() {
      list.clear();
    }
    int size() {
      return list.size();
    }
    bool isUpdated{false};
    void get(const uint32_t index, playListItem& item) {
      item = (index < list.size()) ? list[index] : (playListItem) {};
    }
    void add(const playListItem& item) {
      const uint32_t previousSize = list.size();
      if (previousSize < PLAYLIST_MAX_ITEMS)
        list.push_back(item);
      isUpdated = (previousSize < list.size()) ? true : false;
    }
    void remove(const uint32_t index) {
      if (index < list.size()) {
        list.erase(list.begin() + index);
        isUpdated = true;
      }
    }
    void clear() {
      if (list.size()) {
        list.clear();
        isUpdated = true;
      }
    }
    String& toString(String& s);

  private:
    std::vector<playListItem> list;
};

#endif
