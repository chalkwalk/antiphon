#pragma once

#include "BandPlayState.h"
#include "BotAddress.h"
#include "BotAnswer.h"
#include "BotBand.h"
#include <JuceHeader.h>

// What a bot SAYS and DOES about one message, as a pure function.
//
// The three halves of talking already exist and were measured separately:
// `BotAddress` decides WHO was asked, `BotLanguage` decides WHAT was asked, and
// `BotAnswer` decides the WORDS. This is the join, and it is deliberately the
// only place that knows all three.
//
// Pure because the alternative is untestable. `PracticeBot` decides and sends in
// one step, through `NinjamClient`, so every answer it can give needs a socket
// and a running room to observe -- which is why the recognisers ended up
// measured to three decimal places while nothing checked what a bot actually
// says. Given the same context and the same message this returns the same
// `Response`, so a seed and a script of events give a byte-identical transcript
// (ROADMAP, "Bots that talk").
//
// The bot's own mutable state stays in `PracticeBot`. What crosses this
// boundary is a snapshot in and an intention out; nothing here touches the
// network, the clock, or the audio thread.

namespace BotChat {

// This bot, as far as answering is concerned. A snapshot -- `PracticeBot` holds
// the live copy under its lock and passes a copy in.
struct Self {
  juce::String name;

  // What a player TYPES to address this bot: "Ravo", where `name` is
  // "Ravo[keys-bot]". Every reply that quotes a command back has to use this
  // one -- `say "Ravo[keys-bot] play"` is not something anybody would type,
  // and a bot whose instructions cannot be followed is worse than one that
  // gives none. Falls back to `name` when it is empty.
  juce::String handle;
  BotBand::Voice voice = BotBand::Voice::Drums;
  BotBand::Settings settings;

  // Whether it is playing, and if it is stopping, how far through the ending.
  // A bot answering "stop" needs this: telling somebody it is wrapping up when
  // it is already silent is as wrong as not answering.
  BandPlayState::State phase = BandPlayState::State::Silent;

  // Told to stop talking, and still playing. Chat and music are separate
  // requests here -- "be quiet" is about the commentary, and somebody who
  // wanted the band to stop would have said so.
  bool chatMuted = false;
};

// Everything a reply can depend on. Two different `Room` types, which is not an
// accident: one is who is present, the other is what the music is, and no
// question needs both to be one object.
struct Context {
  BotAddress::Room room;
  BotAnswer::Room music;
  Self self;
};

// What the bot should DO, separately from what it says. A command both acts and
// speaks, and only the action touches state that outlives the message -- so
// keeping them apart is what lets the words be tested without running a band.
enum class Act {
  None,
  Reshuffle,         // `shake`: rerolls the band
  Part,              // leave the room
  SetLeadInstrument, // `value` is a BotVoice::LeadInstrument
  SetChatMuted,      // `value` is 1 for quiet, 0 for talking again
  SetArticulation,   // `value` is 0..100: staccato, as written, legato
  StartPlaying,      // come in, or cancel an ending already under way
  StopPlaying        // bring it to an end: wrap up, resolve, then silence
};

struct Response {
  bool speak = false;
  juce::String text;

  // Answer where you were asked. A public question answered privately looks
  // like no answer at all, and the public path is how anybody else in the room
  // discovers the bots can be spoken to.
  bool privately = false;

  Act act = Act::None;
  int value = 0;

  // This reply is on behalf of everyone, so exactly ONE bot should say it.
  //
  // Set when the message was addressed to the band AND the answer would be the
  // same from every bot. What each one is playing differs and all four should
  // say so; "wrapping it up" does not, and four bots saying it is the chorus
  // this design exists to prevent (docs/BOT-CHAT.md section 5).
  //
  // ACTING is still collective -- every addressed bot does the thing. Only the
  // LINE about it is rationed, and the rationing belongs to the caller, which
  // is the only part of this that needs a clock.
  bool forBand = false;
};

// The decision for one message. `attention` is read and updated the way
// `BotAddress::classify` updates it -- explicit state rather than hidden state,
// so a scripted conversation replays exactly.
//
// Returning a `Response` with `speak == false` and `act == Act::None` is the
// commonest outcome by a wide margin, and it is the right one: nobody is
// addressed by default.
Response respond(const Context &ctx, const BotAddress::Incoming &in,
                 BotAddress::Attention &attention);

} // namespace BotChat
