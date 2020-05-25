#include "playList.h"

String playList::toHTML() {
  String s{"playlist\n"};
  if (!list.size()) return s;
  playListItem tmp;
  for (std::vector<playListItem>::const_iterator i = list.begin(); i != list.end(); ++i) {
    tmp = *i;
    s += "<p class=\"plitem\">" + tmp.url + "</p>";
  }
  return s;
}

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
