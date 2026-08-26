#include "PracticeRoom.h"
#include <JuceHeader.h>
#include <atomic>
#include <csignal>
#include <iostream>

// antiphon-practice: hosts a practice room and waits, so you can join it with
// the ordinary client and find out what having a band in the room feels like.
//
// The room was designed as a DESTINATION rather than a mode (`PracticeRoom.h`):
// a real server on loopback with real bots connected to it, so the whole
// connected UI -- phase bar, remote strips, routing, chat, recording -- works
// without knowing the room is any different. That design is what makes this
// tool a dozen lines rather than a feature: there is nothing to integrate, only
// something to start.
//
// It exists because the room is not reachable from the plugin yet. Nothing in
// `src/` constructs a PracticeRoom; only the tests do. This closes that gap for
// a human without pretending the feature is finished, and it is the harness the
// remaining chat work needs anyway.
//
//   ./build/tools/AntiphonPractice_artefacts/antiphon-practice
//   ...then connect the standalone to 127.0.0.1:<the port it prints>
//
// Console app on purpose: it renders no audio locally and opens no device. The
// band's audio reaches you through the server, the same way another player's
// would.

namespace {

std::atomic<bool> stopping{false};

void onSignal(int) { stopping.store(true); }

juce::String flag(const juce::StringArray &args, const juce::String &name,
                  const juce::String &fallback) {
  const int i = args.indexOf(name);
  return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : fallback;
}

} // namespace

int main(int argc, char **argv) {
  juce::StringArray args;
  for (int i = 1; i < argc; ++i)
    args.add(juce::String(argv[i]));

  if (args.contains("--help") || args.contains("-h")) {
    std::cout
        << "antiphon-practice -- host a practice room and wait\n\n"
           "  --bpm N        tempo (default 100)\n"
           "  --bpi N        beats per interval (default 16)\n"
           "  --rate N       sample rate (default 48000)\n"
           "  --key NAME     starting key, e.g. \"D minor\" (default C major)\n"
           "  --seed N       band seed; the same seed is the same band\n"
           "  --owner NAME   the username the band treats as its owner\n"
           "  --no-tutor     leave out the bot that teaches the six lines\n\n"
           "Then connect the standalone to 127.0.0.1 on the port printed.\n";
    return 0;
  }

  juce::ScopedJuceInitialiser_GUI juceInit;

  PracticeRoom::Config cfg;
  cfg.bpm = flag(args, "--bpm", "100").getIntValue();
  cfg.bpi = flag(args, "--bpi", "16").getIntValue();
  cfg.sampleRate = flag(args, "--rate", "48000").getDoubleValue();
  cfg.ownerName = flag(args, "--owner", "you");
  cfg.seed = (std::uint32_t)flag(args, "--seed", "20260811").getLargeIntValue();

  // Anybody reaching for this tool has almost certainly seen the tutorial, but
  // it stays ON by default here too: the room a flag produces should be the
  // room the button produces, or one of them is lying about what a practice
  // room is.
  cfg.withTutor = !args.contains("--no-tutor");

  const auto keyName = flag(args, "--key", "C major");
  if (const auto key = MusicalKey::parseName(keyName.toStdString());
      key.valid) {
    cfg.key = key;
  } else {
    std::cerr << "not a key: " << keyName << "\n";
    return 2;
  }

  PracticeRoom room;
  if (!room.start(cfg)) {
    std::cerr << "could not start the practice room\n";
    return 1;
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  std::cout << "practice room on " << PracticeRoom::host() << ":" << room.port()
            << "\n"
            << "  " << cfg.bpm << " bpm, " << cfg.bpi << " bpi, "
            << MusicalKey::displayName(cfg.key) << ", seed " << cfg.seed << "\n"
            << "  band: " << room.botNames().joinIntoString(", ")
            << (cfg.withTutor ? " (plus a tutor, which leaves when done)" : "")
            << "\n\n"
            << "connect the standalone to that address as \"" << cfg.ownerName
            << "\".\n"
            << "in chat: \"shake\" rerolls, \"[key: D minor]\" or \"/key D "
               "minor\" moves the key,\n"
            << "a line like \"| Am | F | C | G |\" sets the chart, \"part\" "
               "sends them home.\n\n"
            << "ctrl-c to stop.\n";
  std::cout.flush();

  // RUN THE MESSAGE LOOP. Everything a bot does in response to the room --
  // every chat callback, via NinjamClient's callAsyncIfAlive, and every
  // juce::Timer, including the arrival roster -- runs on the message thread.
  // Sleeping here instead queued all of it and ran none of it: the band played
  // perfectly and ignored every word said to it, because audio is driven by the
  // conductor and network threads and needs no loop at all.
  while (!stopping.load() && room.isRunning())
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);

  std::cout << "\nstopping...\n";
  room.stop();
  return 0;
}
