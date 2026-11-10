#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

// What one remote channel's interval costs to hold, and whether we will.
//
// Each remote channel's interval is decoded into a buffer reserved when it
// begins, on the network thread. The server allows tempos whose interval runs
// to hundreds of megabytes a channel (ROADMAP "A legal BPI can exhaust
// memory"), so past a ceiling we receive nothing rather than allocate it: the
// room plays on, we stay connected and keep sending, and the UI says why the
// others went quiet. The tempo shown is never clamped.
namespace IntervalBudget {

// 64 MB is a little under two minutes of interval at 48 kHz: 32 BPI at 20 BPM
// fits, 1000 BPI at any tempo a server allows does not. It is per channel, and
// a channel can hold two or three intervals at once (queued, playing, retired),
// so a room of four players already stands at three quarters of a gigabyte when
// every one of them is at the ceiling.
inline constexpr std::int64_t kMaxBytesPerRemoteInterval = 64LL << 20;

// Room for the interval and half as much again, because a peer's interval can
// run long and an encoder's tail runs past the boundary. The decode stops at
// the end of the buffer, so this is the most a channel's interval can hold.
inline int samplesPerRemoteInterval(double sampleRate, int bpm, int bpi) {
  const double s =
      sampleRate * 60.0 / std::max(1, bpm) * std::max(0, bpi) * 1.5;
  return s >= 2147483647.0 ? 2147483647 : static_cast<int>(s);
}

// Stereo float, which is what the decoded buffer is.
inline std::int64_t bytesPerRemoteInterval(double sampleRate, int bpm,
                                           int bpi) {
  return static_cast<std::int64_t>(
             samplesPerRemoteInterval(sampleRate, bpm, bpi)) *
         2 * static_cast<std::int64_t>(sizeof(float));
}

inline bool affordable(double sampleRate, int bpm, int bpi) {
  return bytesPerRemoteInterval(sampleRate, bpm, bpi) <=
         kMaxBytesPerRemoteInterval;
}

// What the chat says, and a screen reader reads, when receiving stops or
// starts again. Plain text, no prefix: the caller marks it as local.
inline std::string receiveNotice(bool muted, int bpm, int bpi,
                                 std::int64_t bytesPerChannel) {
  const auto mb = [](std::int64_t b) {
    return std::to_string((b + (1LL << 19)) >> 20) + " MB";
  };
  const std::string tempo =
      std::to_string(bpi) + " BPI at " + std::to_string(bpm) + " BPM";
  if (!muted)
    return "The interval fits again (" + tempo +
           "). You will hear the others from the next one.";
  return "This room's interval (" + tempo + ") needs " + mb(bytesPerChannel) +
         " for each player, more than Antiphon will hold (" +
         mb(kMaxBytesPerRemoteInterval) +
         "). You are still connected and still sending, but you will not "
         "hear the others until the interval is shorter.";
}

} // namespace IntervalBudget
