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
    void get(const size_t index, playListItem& item) {
      if (index < list.size()) item = list[index];
      else item = {};
    }
    void add(const playListItem& item) {
      const size_t previousSize = list.size();
      if (previousSize > PLAYLIST_MAX_ITEMS - 1) return;
      list.push_back(item);
      if (previousSize < list.size())
        isUpdated = true;
    }
    void remove(const size_t index) {
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
