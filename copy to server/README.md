### Use notes.

Make sure your fileserver serves files with the correct MIME type which is a `Content-Type: audio/mpeg` header.

- Copy this file to the root of your music collection.
- Adjust `libraryURL` at the top of `index.htm`.
- Use `xxd -i index.htm > index_htm.h` to get a C/C++ header file.
- Change the `index_htm[]` in `index_htm.h` to `const uint8_t`.<br>(To keep the web interface in flash memory instead of ram.)
- Change `const unsigned int index_htm_len` at the bottom of `index_htm.h` to `const unsigned int index_htm_len`.<br>(To keep the web interface in flash memory instead of ram.)
- Compile and flash your esp32.
