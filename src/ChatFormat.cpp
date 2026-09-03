#include "ChatFormat.h"

#include <chalkwalk/ninjam/Voting.h>

#include "Harmony.h"
#include "MusicalKey.h"

namespace ChatFormat {

Line render(const juce::String &type, const juce::String &username,
            const juce::String &text, const juce::String &selfUsername) {
  Line out;

  // The voting system speaks as an ordinary MSG from the server, so it has to
  // be recognised by content before anything else claims it.
  if (parseVote(text).valid) {
    out.category = Category::Voting;
    out.text = "~~ " + text;
    return out;
  }

  // A key announcement is recognised wherever it came from, so the same line
  // works whether it was typed as chat or left in the topic -- and in either of
  // the two forms, since `/key G minor` is what a bot can actually say.
  if (MusicalKey::parseAnnouncement(text.toStdString()).valid) {
    out.category = Category::Key;
    out.text = "~~ " + text;
    return out;
  }

  if (isChordProgression(text)) {
    out.category = Category::ChordProgression;
    out.text = "~~ " + text;
    return out;
  }

  if (type == "PRIVMSG") {
    out.category = Category::PrivateMessage;
    out.text = "[PM] <" + username + "> " + text;
    return out;
  }

  if (type == "MSG") {
    // The server sends its own notices as MSG with an empty username. Rendering
    // those as "<> ..." was how every server notice looked before this.
    if (username.isEmpty()) {
      out.category = Category::ServerNotice;
      out.text = "*** " + text;
      return out;
    }

    if (text.startsWithIgnoreCase("/me ")) {
      out.category = Category::Action;
      out.text = "* " + username + " " + text.substring(4);
      return out;
    }

    out.category = (selfUsername.isNotEmpty() && username == selfUsername)
                       ? Category::SelfMessage
                       : Category::OtherMessage;
    out.text = "<" + username + "> " + text;
    return out;
  }

  if (type == "TOPIC") {
    out.category = Category::Topic;
    out.text = "*** Topic: " + text;
    return out;
  }

  if (type == "JOIN" || type == "PART") {
    out.category = Category::JoinPart;
    out.text = "*** " + text;
    return out;
  }

  out.category = Category::ServerNotice;
  out.text = "*** " + text;
  return out;
}

bool isChordProgression(const juce::String &text) {
  // Deliberately not a second parser. This was one once -- it validated the
  // first letter and shrugged at the rest -- so a line could be coloured as a
  // chart here and rejected by the band, or the other way round. One tokeniser
  // decides both (`PRINCIPLES §8`).
  return Harmony::looksLikeChart(text.toStdString());
}

VoteState parseVote(const juce::String &text) {
  // One parser, in `chalkwalk::ninjam::voting`, because the bots read the same
  // sentence and a second reading of it is a second answer (`PRINCIPLES 8`).
  // What stays here is the shape the UI wants.
  const auto line = chalkwalk::ninjam::voting::parseLine(text.toStdString());

  VoteState v;
  // A candidate of zero is not a proposal: a line we could not read shows no
  // chip rather than a chip for nothing.
  v.valid = line.valid && line.value > 0;
  if (!v.valid)
    return v;

  v.isBpm = line.isBpm;
  v.target = line.value;
  v.votes = line.votes;
  v.needed = line.required;
  v.timeoutSeconds = line.timeoutSeconds;
  v.settled = line.settled;
  return v;
}

} // namespace ChatFormat
