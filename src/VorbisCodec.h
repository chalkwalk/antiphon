#pragma once
#include <memory>

class VorbisDecoder {
public:
  VorbisDecoder();
  ~VorbisDecoder();
  VorbisDecoder(const VorbisDecoder &) = delete;
  VorbisDecoder &operator=(const VorbisDecoder &) = delete;

  void decode(const void *data, int len);

  int available() const;
  const float *pcm() const;
  void skip(int count);

  int sampleRate() const;
  int numChannels() const;

private:
  struct Impl;
  std::unique_ptr<Impl> p;
};

class VorbisEncoder {
public:
  VorbisEncoder(int sampleRate, int numChannels, int bitrateKbps,
                int serialNumber);
  ~VorbisEncoder();
  VorbisEncoder(const VorbisEncoder &) = delete;
  VorbisEncoder &operator=(const VorbisEncoder &) = delete;

  // Pass nullptr/0 to flush end-of-stream.
  void encode(const float *interleaved, int numFrames);

  int available() const;
  const void *data() const;
  void advance(int count);

private:
  struct Impl;
  std::unique_ptr<Impl> p;
};
