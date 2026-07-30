#include "Sha1.h"
#include <cstring>

static uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static uint32_t beu32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

Sha1::Sha1() {
  h[0] = 0x67452301u;
  h[1] = 0xEFCDAB89u;
  h[2] = 0x98BADCFEu;
  h[3] = 0x10325476u;
  h[4] = 0xC3D2E1F0u;
  byteCount = 0;
}

void Sha1::processBlock(const uint8_t *block) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i)
    w[i] = beu32(block + i * 4);
  for (int i = 16; i < 80; ++i)
    w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    uint32_t t = rotl32(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rotl32(b, 30);
    b = a;
    a = t;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
}

void Sha1::add(const void *data, int len) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  int bufFill = static_cast<int>(byteCount & 63);
  byteCount += static_cast<uint64_t>(len);
  while (len > 0) {
    int space = 64 - bufFill;
    int take = len < space ? len : space;
    memcpy(buf + bufFill, p, static_cast<size_t>(take));
    p += take;
    len -= take;
    bufFill += take;
    if (bufFill == 64) {
      processBlock(buf);
      bufFill = 0;
    }
  }
}

void Sha1::result(void *out) {
  uint64_t bits = byteCount * 8;
  uint8_t pad = 0x80;
  add(&pad, 1);
  uint8_t zero = 0;
  while ((byteCount & 63) != 56)
    add(&zero, 1);
  uint8_t lenBytes[8];
  for (int i = 7; i >= 0; --i) {
    lenBytes[i] = static_cast<uint8_t>(bits & 0xFF);
    bits >>= 8;
  }
  add(lenBytes, 8);

  uint8_t *o = static_cast<uint8_t *>(out);
  for (int i = 0; i < 5; ++i) {
    o[i * 4 + 0] = static_cast<uint8_t>(h[i] >> 24);
    o[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
    o[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
    o[i * 4 + 3] = static_cast<uint8_t>(h[i]);
  }

  // reset for reuse
  *this = Sha1();
}
