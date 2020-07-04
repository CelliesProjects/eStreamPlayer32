#include "playList.h"

String playList::toClientString() {
  String s{"playlist\n"};
  if (list.size()) {
    for (std::vector<playListItem>::const_iterator i = list.begin(); i != list.end(); ++i) {
      switch (i->type) {
        case HTTP_FILE : s += i->url.substring(i->url.lastIndexOf("/") + 1) + "\n";
          break;
        case HTTP_STREAM : s += i->name + "\n";
          break;
        case HTTP_PRESET : s += preset[i->index].name + "\n";
          break;
        case HTTP_FAVORITE : s += i->name + "\n";
          break;
        default : break;
      }
    }
  }
  return s;
}
