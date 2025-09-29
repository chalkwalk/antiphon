#include "VorbisCodec.h"

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// VorbisDecoder
// ---------------------------------------------------------------------------

struct VorbisDecoder::Impl {
  ogg_sync_state   oy;
  ogg_stream_state os;
  ogg_page         og;
  ogg_packet       op;
  vorbis_info      vi;
  vorbis_comment   vc;
  vorbis_dsp_state vd;
  vorbis_block     vb;

  int  packetsSeen  = 0;
  bool streamInited = false;
  bool dspInited    = false;

  std::vector<float> outBuf;
  size_t             readOffset = 0;

  Impl() {
    memset(&oy, 0, sizeof(oy));
    memset(&os, 0, sizeof(os));
    memset(&og, 0, sizeof(og));
    memset(&op, 0, sizeof(op));
    memset(&vi, 0, sizeof(vi));
    memset(&vc, 0, sizeof(vc));
    memset(&vd, 0, sizeof(vd));
    memset(&vb, 0, sizeof(vb));
    ogg_sync_init(&oy);
  }

  ~Impl() {
    if (dspInited) {
      vorbis_block_clear(&vb);
      vorbis_dsp_clear(&vd);
    }
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    if (streamInited)
      ogg_stream_clear(&os);
    ogg_sync_clear(&oy);
  }

  void compact() {
    if (readOffset > outBuf.size() / 2 && readOffset > 0) {
      outBuf.erase(outBuf.begin(),
                   outBuf.begin() + static_cast<ptrdiff_t>(readOffset));
      readOffset = 0;
    }
  }

  void decode(const void* data, int len) {
    char* buf = ogg_sync_buffer(&oy, len);
    if (!buf) return;
    memcpy(buf, data, static_cast<size_t>(len));
    ogg_sync_wrote(&oy, len);

    while (ogg_sync_pageout(&oy, &og) > 0) {
      int serial = ogg_page_serialno(&og);
      if (!streamInited) {
        ogg_stream_init(&os, serial);
        streamInited = true;
        vorbis_info_init(&vi);
        vorbis_comment_init(&vc);
      }
      ogg_stream_pagein(&os, &og);

      while (ogg_stream_packetout(&os, &op) > 0) {
        if (packetsSeen < 3) {
          if (vorbis_synthesis_headerin(&vi, &vc, &op) < 0)
            return;
          ++packetsSeen;
          if (packetsSeen == 3) {
            vorbis_synthesis_init(&vd, &vi);
            vorbis_block_init(&vd, &vb);
            dspInited = true;
          }
        } else {
          float** pcm;
          if (vorbis_synthesis(&vb, &op) == 0)
            vorbis_synthesis_blockin(&vd, &vb);
          int samples;
          while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
            int ch = vi.channels;
            size_t base = outBuf.size();
            outBuf.resize(base + static_cast<size_t>(samples * ch));
            float* dst = outBuf.data() + base;
            for (int n = 0; n < samples; ++n)
              for (int c = 0; c < ch; ++c)
                *dst++ = pcm[c][n];
            vorbis_synthesis_read(&vd, samples);
          }
        }
      }
    }
    compact();
  }
};

VorbisDecoder::VorbisDecoder() : p(std::make_unique<Impl>()) {}
VorbisDecoder::~VorbisDecoder() = default;

void VorbisDecoder::decode(const void* data, int len) { p->decode(data, len); }

int VorbisDecoder::available() const {
  return static_cast<int>(p->outBuf.size() - p->readOffset);
}

const float* VorbisDecoder::pcm() const {
  return p->outBuf.data() + p->readOffset;
}

void VorbisDecoder::skip(int count) {
  p->readOffset = std::min(p->readOffset + static_cast<size_t>(count),
                           p->outBuf.size());
  p->compact();
}

int VorbisDecoder::sampleRate() const  { return p->vi.rate; }
int VorbisDecoder::numChannels() const { return p->vi.channels ? p->vi.channels : 1; }

// ---------------------------------------------------------------------------
// VorbisEncoder
// ---------------------------------------------------------------------------

// Piecewise-linear kbps -> VBR quality mapping ported from WDL vorbisencdec.h.
static float bitrateToQuality(int kbps) {
  float qv;
  if      (kbps < 40)  qv = -0.1f;
  else if (kbps < 64)  qv = -0.10f + (kbps - 40) * (0.10f / 24.0f);
  else if (kbps < 75)  qv = (kbps - 64) * (0.1f / 9.0f);
  else if (kbps < 95)  qv = 0.1f  + (kbps - 75)  * (0.2f / 20.0f);
  else if (kbps < 110) qv = 0.3f  + (kbps - 95)  * (0.2f / 15.0f);
  else if (kbps < 140) qv = 0.5f  + (kbps - 110) * (0.25f / 30.0f);
  else                 qv = 0.75f + (kbps - 140) * (0.25f / 100.0f);
  if (qv < -0.1f) qv = -0.1f;
  if (qv >  1.0f) qv =  1.0f;
  return qv;
}

struct VorbisEncoder::Impl {
  ogg_stream_state os;
  vorbis_info      vi;
  vorbis_comment   vc;
  vorbis_dsp_state vd;
  vorbis_block     vb;

  int    nch;
  bool   ok = false;

  std::vector<uint8_t> outBuf;
  size_t               readOffset = 0;

  Impl(int sampleRate, int numChannels, int bitrateKbps, int serialNumber)
    : nch(numChannels)
  {
    memset(&os, 0, sizeof(os));
    memset(&vi, 0, sizeof(vi));
    memset(&vc, 0, sizeof(vc));
    memset(&vd, 0, sizeof(vd));
    memset(&vb, 0, sizeof(vb));

    vorbis_info_init(&vi);
    float qv = bitrateToQuality(bitrateKbps);
    if (vorbis_encode_init_vbr(&vi, nch, sampleRate, qv) != 0)
      return;

    vorbis_comment_init(&vc);
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    ogg_stream_init(&os, serialNumber);
    ok = true;

    // Emit the 3 Vorbis header packets immediately so callers can drain them.
    ogg_packet hdr, hdr_comm, hdr_code;
    vorbis_analysis_headerout(&vd, &vc, &hdr, &hdr_comm, &hdr_code);
    ogg_stream_packetin(&os, &hdr);
    ogg_stream_packetin(&os, &hdr_comm);
    ogg_stream_packetin(&os, &hdr_code);

    ogg_page og;
    while (ogg_stream_flush(&os, &og)) {
      outBuf.insert(outBuf.end(), og.header, og.header + og.header_len);
      outBuf.insert(outBuf.end(), og.body,   og.body   + og.body_len);
    }
  }

  ~Impl() {
    if (ok) {
      ogg_stream_clear(&os);
      vorbis_block_clear(&vb);
      vorbis_dsp_clear(&vd);
      vorbis_comment_clear(&vc);
      vorbis_info_clear(&vi);
    } else {
      vorbis_info_clear(&vi);
    }
  }

  void compact() {
    if (readOffset > outBuf.size() / 2 && readOffset > 0) {
      outBuf.erase(outBuf.begin(),
                   outBuf.begin() + static_cast<ptrdiff_t>(readOffset));
      readOffset = 0;
    }
  }

  void encode(const float* interleaved, int numFrames) {
    if (!ok) return;

    if (!interleaved || numFrames == 0) {
      vorbis_analysis_wrote(&vd, 0);
    } else {
      float** buf = vorbis_analysis_buffer(&vd, numFrames);
      for (int i = 0; i < numFrames; ++i)
        for (int c = 0; c < nch; ++c)
          buf[c][i] = interleaved[i * nch + c];
      vorbis_analysis_wrote(&vd, numFrames);
    }

    ogg_packet op;
    ogg_page   og;
    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
      vorbis_analysis(&vb, nullptr);
      vorbis_bitrate_addblock(&vb);
      while (vorbis_bitrate_flushpacket(&vd, &op)) {
        ogg_stream_packetin(&os, &op);
        while (ogg_stream_pageout(&os, &og)) {
          outBuf.insert(outBuf.end(), og.header, og.header + og.header_len);
          outBuf.insert(outBuf.end(), og.body,   og.body   + og.body_len);
        }
      }
    }
    compact();
  }
};

VorbisEncoder::VorbisEncoder(int sr, int nch, int brkbps, int serno)
  : p(std::make_unique<Impl>(sr, nch, brkbps, serno)) {}
VorbisEncoder::~VorbisEncoder() = default;

void VorbisEncoder::encode(const float* interleaved, int numFrames) {
  p->encode(interleaved, numFrames);
}

int VorbisEncoder::available() const {
  return static_cast<int>(p->outBuf.size() - p->readOffset);
}

const void* VorbisEncoder::data() const {
  return p->outBuf.data() + p->readOffset;
}

void VorbisEncoder::advance(int count) {
  p->readOffset = std::min(p->readOffset + static_cast<size_t>(count),
                           p->outBuf.size());
  p->compact();
}
