#ifndef __PLAYLIST_H
#define __PLAYLIST_H

#include <Arduino.h>
#include <vector>

enum streamType {
  HTTP, FS
};

struct playListItem {
  streamType type; //http,fs,
  String url;
};
class playList {

  public:
    playList(){}
    ~playList(){}
    size_t size(){return list.size();}
    bool isUpdated{false};
    bool get(size_t num, playListItem &item){if (num < size()) item = list[num];}
    bool add(playListItem item){list.push_back(item);isUpdated=true;}
    bool remove(size_t num) {if (num < size()) list.erase(list.begin() + num);isUpdated=true;}
    String toHTML();

private:
    std::vector<playListItem> list;    
};

#endif
