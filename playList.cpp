#include "playList.h"

const String previous = R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M6 6h2v12H6zm3.5 6l8.5 6V6z"/><path d="M0 0h24v24H0z" fill="none"/></svg>)"; //"<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M6 6h2v12H6zm3.5 6l8.5 6V6z"/><path d="M0 0h24v24H0z" fill="none"/></svg>"};

String playList::toHTML() {
  String s{"playlist\n"};
  if (!list.size()) return s;
  playListItem tmp;
  for (std::vector<playListItem>::const_iterator i = list.begin(); i != list.end(); ++i) {
    tmp = *i;
    s += "<p class=\"plitem\">" + previous + tmp.url + "</p>";
  }
  return s;
}
