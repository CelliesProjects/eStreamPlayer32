#ifndef __PLAYLIST_H
#define __PLAYLIST_H

#include <Arduino.h>
#include <vector>
#include "presets.h"

enum streamType {
  HTTP_FILE, HTTP_STREAM, HTTP_PRESET, SDCARD_FILE
};

struct playListItem {
  streamType type;
  String url;
  int index; // used for (type == HTTP_PRESET) to indicate which preset to play
};

class playList {

  public:
    playList(){}
    ~playList(){}
    int size(){return list.size();}
    bool isUpdated{false};
    bool get(size_t num, playListItem &item){if (num < list.size()) item = list[num];}
    bool add(playListItem item){list.push_back(item);isUpdated=true;}
    bool remove(size_t num) {if (num < list.size()) list.erase(list.begin() + num);isUpdated=true;}
    void clear(){list.clear();isUpdated=true;}
    String toHTML();
    String toClientString();

  private:
    std::vector<playListItem> list;
};

#endif
