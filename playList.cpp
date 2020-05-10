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
