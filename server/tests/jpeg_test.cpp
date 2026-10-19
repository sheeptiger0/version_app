// Standalone check for video/Jpeg.cpp (WIC decoder). Build:
//   g++ -std=c++17 -I src tests/jpeg_test.cpp src/video/Jpeg.cpp \
//       -o build/jpeg_test.exe -lwindowscodecs -lole32
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "video/Jpeg.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: jpeg_test <file.jpg>\n");
    return 2;
  }
  std::ifstream f(argv[1], std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 2;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

  int w = 0, h = 0;
  std::vector<uint8_t> bgra;
  if (!fatigue::decodeJpegToBgra(data.data(), data.size(), w, h, bgra)) {
    std::fprintf(stderr, "DECODE FAILED\n");
    return 1;
  }
  std::printf("DECODE OK %dx%d bytes=%zu\n", w, h, bgra.size());
  return 0;
}
