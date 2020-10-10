#include "playList.h"

String playList::toClientString() {
  String s{"playlist\n"};
  if (list.size()) {
    for (auto& item : list) {
      switch (item.type) {

        case HTTP_FILE :
          s += String(item.url.substring(item.url.lastIndexOf("/") + 1) + "\n" + typeStr[item.type] + "\n");
          break;

        case HTTP_PRESET :
          s += String(preset[item.index].name + "\n" + typeStr[item.type] + "\n");
          break;

        case HTTP_STREAM :
        case HTTP_FAVORITE :
          s += String(item.name + "\n" + typeStr[item.type] + "\n");
          break;

        default :
          ESP_LOGE(TAG, "ERROR! Enum item is unhandled!");
          break;
      }
    }
  }
  return s;
}
