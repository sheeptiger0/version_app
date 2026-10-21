#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace DMSAlgo {

// Decode a JPEG in memory into tightly-packed, top-down 32bpp BGRA pixels.
// This is the exact layout the GDI viewer (and a future OpenCV-free path) wants.
// Returns false if the data is not a decodable JPEG.
bool decodeJpegToBgra(const uint8_t* jpeg, size_t len, int& outW, int& outH,
                      std::vector<uint8_t>& outBgra);

} // namespace DMSAlgo
