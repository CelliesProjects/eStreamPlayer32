#ifndef __PLAYLIST_H
#define __PLAYLIST_H

#include <Arduino.h>
#include <vector>
#include "presets.h"

# define PLAYLIST_MAX_ITEMS 200

enum streamType {
  HTTP_FILE, HTTP_STREAM, HTTP_FAVORITE, HTTP_PRESET, SDCARD_FILE
};

struct playListItem {
  streamType type;
  String name;      // used when (type == HTTP_FAVORITE ) to store the name of the stream
  String url;
  uint32_t index;   // used when (type == HTTP_PRESET) to indicate which preset to play
};

class playList {

  public:
    playList() {}
    ~playList() {list.clear();}
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
      if (previousSize >= PLAYLIST_MAX_ITEMS) return;
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
    String toClientString();

  private:
    std::vector<playListItem> list;
};

#endif
