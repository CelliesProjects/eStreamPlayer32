# eStreamPlayer32
An esp32 app to play webstreams and music from a lamp server.

## Setup:
1. Adjust your credentials in `wifi_setup.h`.
2. Copy the php script to your music folder on the server.
3. Change the following line in `index.htm` so it points to the script you just copied to your server:
<br>`var libraryURL="http://192.168.0.50/muziek/ESP32.php";`
4. Use (in a terminal) xxd to convert `index.htm` to a C style header file:
<br>`xxd -i index.htm > index_htm.h`
5. Change the first line in `index_htm.h` to: `const uint8_t index_htm[] = {`
<br>and change the last line to: `const unsigned int index_htm_len = xxxxxx;`
where you leave the number xxxxxx unchanged.
6. Flash the sketch to your esp32.
