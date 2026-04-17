#include "ChatFormat.h"

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

VoteState parseVote(const juce::String &text) {
  VoteState v;
  if (!text.startsWith("[voting system]"))
    return v;

  // Both formats are fixed in the server and are the only contract we have --
  // there is no structured vote message on the wire, so this is parsing English
  // on purpose (references/libninjam/ninjam/server/usercon.cpp:1054, :1074,
  // :1089, :1107):
  //
  //   [voting system] leading candidate: %d/%d votes for %d BPM [each vote
  //                   expires in %ds]
  //   [voting system] setting BPM to %d
  //
  // ...and the same pair for BPI.

  if (text.contains("setting BPM to") || text.contains("setting BPI to")) {
    v.valid = true;
    v.settled = true;
    v.isBpm = text.contains("setting BPM to");
    const auto tail = text.fromFirstOccurrenceOf("to ", false, false);
    v.target = tail.trim().getIntValue();
    return v;
  }

  if (!text.contains("leading candidate:"))
    return v;

  // "3/5 votes for 137 BPM [each vote expires in 60s]"
  const auto tail = text.fromFirstOccurrenceOf("leading candidate:", false, false).trim();
  v.votes = tail.getIntValue();
  v.needed = tail.fromFirstOccurrenceOf("/", false, false).getIntValue();

  const auto forPart = tail.fromFirstOccurrenceOf("votes for ", false, false).trim();
  v.target = forPart.getIntValue();
  v.isBpm = forPart.containsIgnoreCase("BPM");

  if (text.contains("expires in "))
    v.timeoutSeconds =
        text.fromFirstOccurrenceOf("expires in ", false, false).getIntValue();

  // A candidate of zero is not a proposal; treat a line we could not read as
  // not-a-vote rather than showing a chip for nothing.
  v.valid = v.target > 0;
  return v;
}

} // namespace ChatFormat
