#include "playList.h"

String playList::toClientString() {
  String s{"playlist\n"};
  if (!list.size()) return s;
  playListItem item;
  for (std::vector<playListItem>::const_iterator i = list.begin(); i != list.end(); ++i) {
    item = *i;
    if (item.type == HTTP_FILE || item.type == HTTP_STREAM) s += item.url.substring(item.url.lastIndexOf("/") + 1) + "\n";
    else if (item.type == HTTP_PRESET) s += preset[item.index].name + "\n";
  }
  return s;
}
