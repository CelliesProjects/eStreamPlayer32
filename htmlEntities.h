// https://www.w3schools.com/tags/ref_urlencode.asp

// https://www.fon.hum.uva.nl/praat/manual/Special_symbols.html

String htmlEntities(const char* plaintext) {
  String result{};
  uint32_t cnt{0};
  while (plaintext[cnt] != 0) {
    if (plaintext[cnt] > 0x7F || plaintext[cnt] < 0x20) {
      switch (plaintext[cnt]) {

        case 0xC2 : {
            const uint8_t firstByte = plaintext[cnt];
            cnt++;
            const uint8_t secondByte = plaintext[cnt];
            switch (secondByte) {
              case 0xA0 ... 0xBF : {
                  result.concat((char)firstByte);
                  result.concat((char)secondByte);
                }
                break;
              default: {
                result.concat("?");
                ESP_LOGE(TAG, "Invalid 16-bit utf8 sequence. Dropped 2 bytes.");
              }
            }
          }
          break;

        case 0xC3 : {
            const uint8_t firstByte = plaintext[cnt];
            cnt++;
            const uint8_t secondByte = plaintext[cnt];
            switch (secondByte) {
              case 0x80 ... 0xBF : {
                  result.concat((char)firstByte);
                  result.concat((char)secondByte);
                }
                break;
              default: {
                result.concat("?");
                ESP_LOGE(TAG, "Invalid 16-bit utf8 sequence. Dropped 2 bytes.");
              }
            }
          }
          break;

        case 0xC9 :
          result.concat("&Eacute;"); // É
          break;

        case 0xE1 :
          result.concat("&aacute;"); // á
          break;

        case 0xE4 :
          result.concat("&auml;"); // ä
          break;

        case 0xE7 :
          result.concat("&ccedil;"); // ç
          break;

        case 0xE8 :
          result.concat("&egrave;"); // è
          break;

        case 0xE9 :
          result.concat("&eacute;"); // é
          break;

        case 0xEA :
          result.concat("&ecirc;"); // ê
          break;

        case 0xEB :
          result.concat("&euml;"); // ë
          break;

        case 0xED :
          result.concat("&iacute;"); // í
          break;

        case 0xF3 :
          result.concat("&oacute; "); // ó
          break;

        case 0xF6 :
          result.concat("&ouml;"); // ö
          break;

        case 0xFC :
          result.concat("&uuml;"); // ü
          break;

        default : result.concat("?");
          ESP_LOGE(TAG, "ERROR: Unhandled char 0x%x", plaintext[cnt]);
      }
    }
    else
      result.concat(plaintext[cnt]);
    cnt++;
  }
  ESP_LOGD(TAG, "Input str: %s", plaintext);
  ESP_LOGD(TAG, "Returning html encoded str: %s", result.c_str());
  return result;
}
