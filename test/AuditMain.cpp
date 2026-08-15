// Accessibility audit over the real component tree, as a build gate.
//
// The rules in AccessibilityAudit are unit-tested against a synthetic tree, but
// the tree that ships is the one that rots. It cannot be reached from
// NinjamTests: PluginProcessor and the UI need the JucePlugin_* defines that
// only juce_add_plugin supplies. So this links the plugin's own shared-code
// library and drives the genuine editor.
//
// Exit code is the number of findings, so ctest fails the build when a control
// arrives without a name. Run it by hand to see the report.
//
// States matter as much as rules. The connect dialog spent its whole life
// outside the audit because it is a separate top-level window, and remote
// player strips do not exist until somebody joins. Auditing one default screen
// is how that goes unnoticed, so each state below is walked in turn.

#include <JuceHeader.h>

#include "AccessibilityTree.h"
#include "AudioDeviceStartup.h"
#include "AudioTroubleView.h"
#include "FakeNinjamServer.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ServerBrowserDialog.h"
#include "ShortcutsDialog.h"

namespace {

struct StateResult {
  juce::String name;
  int findings = 0;
  AccessibilityAudit::Coverage coverage;
  juce::String report;
};

// Lets the editor catch up with the processor.
//
// Strips are not created when a channel is added -- the editor reconciles
// itself against the processor on its 30 Hz timer. Without a message loop that
// timer never fires, so a state change silently audits the previous tree. That
// is not hypothetical: before this existed, "two channels" reported exactly the
// same 43 components as "default", and passed.
void pump(int ms) {
  juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

// Lays the editor out as if it were on screen. Children are only positioned by
// resized(), and an unpositioned tree still audits -- but auditing what the
// user actually sees is the point.
void settle(juce::Component &c) {
  c.setBounds(0, 0, c.getWidth() > 0 ? c.getWidth() : 1100,
              c.getHeight() > 0 ? c.getHeight() : 700);
  c.resized();
}

StateResult auditState(const juce::String &name,
                       const std::vector<const juce::Component *> &roots) {
  // Announced before the work, on stderr, unbuffered. Findings are printed
  // together at the end, which reads better but says nothing at all when the
  // run wedges instead of finishing -- and this has hung on CI runners with no
  // output to say where. A trace line costs nothing and turns "it hung" into
  // "it hung in the device picker".
  std::fprintf(stderr, "audit: entering state '%s'\n", name.toRawUTF8());
  std::fflush(stderr);

  StateResult r;
  r.name = name;
  std::vector<AccessibilityAudit::Finding> all;
  for (const auto *c : roots) {
    if (c == nullptr)
      continue;
    const auto tree = AccessibilityAudit::buildAuditTree(*c);
    ++r.coverage.roots;
    AccessibilityAudit::measure(tree, r.coverage);
    auto found = AccessibilityAudit::run(tree);
    all.insert(all.end(), found.begin(), found.end());
  }
  r.findings = (int)all.size();
  r.report = AccessibilityAudit::format(all, r.coverage);
  std::fprintf(stderr, "audit: left state '%s'\n", name.toRawUTF8());
  std::fflush(stderr);
  return r;
}

} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI juceInit;

  // AccessibleNaming needs juce_gui_basics, which NinjamTests deliberately does
  // not link so it can run headless. This target already links it, so its unit
  // tests ride along here rather than going uncovered.
  {
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTestsInCategory("AccessibleNaming");
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
      failures += runner.getResult(i)->failures;
    if (failures > 0) {
      std::fprintf(stderr, "audit: %d AccessibleNaming unit-test failure(s)\n",
                   failures);
      return failures;
    }
  }

  AntiphonAudioProcessor processor;
  processor.setRateAndBufferSizeDetails(48000, 512);

  std::unique_ptr<juce::AudioProcessorEditor> editorHolder(
      processor.createEditorIfNeeded());
  auto *editor = editorHolder.get();
  if (editor == nullptr) {
    std::fprintf(stderr, "audit: the processor produced no editor\n");
    return 1;
  }
  pump(300);
  settle(*editor);

  std::vector<StateResult> results;

  results.push_back(auditState("default", {editor}));

  // A second local channel, and the buses that make the routing dropdowns
  // meaningful. One strip cannot show a DUPLICATE NAME between strips, and the
  // strip is the container that makes a repeated "Mute" legitimate -- so this
  // is the state where that rule first has anything to say.
  processor.addLocalChannel();
  processor.addInputBus();
  processor.addOutputBus();
  pump(300);
  settle(*editor);
  results.push_back(auditState("two channels, extra buses", {editor}));

  // Practice mode, which puts three echo taps in the mixer's remote half with a
  // delay picker where a real channel has Recv. Those controls exist nowhere
  // else, so without this state they would never be audited -- which is exactly
  // how the connect dialog went unchecked for its whole life.
  {
    processor.prepareToPlay(48000.0, 512);
    const bool started = processor.setPracticeEnabled(true);
    pump(300);
    settle(*editor);
    if (started)
      results.push_back(auditState("practice mode, echo taps", {editor}));
    else
      std::printf("practice mode did not start; state not audited\n");
    processor.setPracticeEnabled(false);
    pump(100);
  }

  // The connect dialog is its own top-level window, so it has to be handed in
  // as a root of its own. Built directly rather than through the editor: this
  // wants no message loop, no modal state and no network.
  {
    ServerBrowserDialog browser;
    browser.setSize(700, 460);
    settle(browser);
    // The password field is hidden until Anonymous is unticked, and the audit
    // rightly skips invisible controls -- so untick it, or it never gets read.
    browser.anonymousToggle.setToggleState(false, juce::sendNotification);
    settle(browser);
    results.push_back(auditState("connect dialog", {editor, &browser}));
  }

  {
    ShortcutsDialog shortcutsDlg;
    shortcutsDlg.setSize(600, 460);
    settle(shortcutsDlg);
    results.push_back(auditState("shortcuts dialog", {editor, &shortcutsDlg}));
  }

  // The screen a user only meets when something has already gone wrong, which
  // makes it the last one that should go unchecked -- and it lives in the
  // standalone's window, so no amount of walking the editor reaches it.
  //
  // Enumerating device types is not opening one: the wedge this whole view
  // exists for happens on open, which nothing here does.
  {
    // Traced separately from the state below because it happens before it, and
    // because this is the line most likely to be the one that hangs.
    std::fprintf(stderr, "audit: constructing AudioDeviceManager\n");
    std::fflush(stderr);
    juce::AudioDeviceManager dm;
    std::fprintf(stderr, "audit: AudioDeviceManager constructed\n");
    std::fflush(stderr);
    AudioTroubleView trouble(
        dm, 2, 2,
        AudioDeviceStartup::describe(AudioDeviceStartup::State::TimedOut),
        [] {});
    trouble.setSize(560, 420);
    settle(trouble);
    results.push_back(auditState("audio device trouble view", {&trouble}));
  }

  // Remote player strips do not exist until somebody joins, so the whole
  // remote half of the surface -- the per-channel faders, mutes, solos, Recv
  // buttons and bus dropdowns -- had never been audited at all. The loopback
  // server is already hermetic and already speaks the real protocol, so a real
  // session is cheaper than a mock and audits the strips the user gets.
  {
    FakeNinjamServer server;
    if (!server.start(120, 8)) {
      std::fprintf(stderr, "audit: could not start the loopback server\n");
      return 1;
    }

    processor.ninjamClient.connectToServer("127.0.0.1", server.port(),
                                           "anonymous", "");
    for (int i = 0; i < 60 && !server.hasClient(); ++i)
      pump(50);

    server.sendUserInfo("guitarist", 0, "Guitar");
    server.sendUserInfo("guitarist", 1, "Vox");
    server.sendUserInfo("drummer", 0, "Kit");
    pump(600);
    settle(*editor);

    results.push_back(
        auditState("two remote players, three channels", {editor}));

    // A chart announced in chat grows the header by a row and puts the key
    // suggestion on the chip, so it is a surface of its own -- and an
    // unaudited state is how the connect dialog went unchecked for its whole
    // life. The chart goes in as an ordinary chat message because that is how
    // one really arrives.
    server.sendChat("MSG", "guitarist", "| Dm7 | G7 | Cmaj7 |");
    pump(400);
    settle(*editor);

    results.push_back(auditState("a chart announced in chat", {editor}));

    // Traced line by line: the audit has hung somewhere in this teardown on CI
    // and every call in it is meant to be bounded, so the next hang needs to
    // name the one that is not. See ROADMAP.md.
    std::fprintf(stderr, "audit: disconnecting\n");
    std::fflush(stderr);
    processor.ninjamClient.disconnectFromServer();

    std::fprintf(stderr, "audit: disconnected, pumping\n");
    std::fflush(stderr);
    pump(200);

    std::fprintf(stderr, "audit: stopping the loopback server\n");
    std::fflush(stderr);
    server.stop();

    std::fprintf(stderr, "audit: server stopped\n");
    std::fflush(stderr);
  }

  std::fprintf(stderr, "audit: teardown complete, reporting\n");
  std::fflush(stderr);

  // A state that examines exactly what the previous one did has not been
  // reached, and its clean verdict means nothing. Fail loudly rather than bank
  // it.
  //
  // Keyboard reach is part of the comparison, not just the component count: a
  // state whose difference is which controls are OFFERED rather than which
  // exist -- a chip appearing, say -- has the same tree with two more stops in
  // it. Counting only nodes would call that state unreached when it is the
  // whole point of it.
  for (size_t i = 1; i < results.size(); ++i) {
    const auto &now = results[i].coverage;
    const auto &before = results[i - 1].coverage;
    if (now.nodes == before.nodes && now.roots == before.roots &&
        now.focusable == before.focusable) {
      std::fprintf(stderr,
                   "audit: state '%s' examined the same %d component(s) as "
                   "'%s' -- it was never reached\n",
                   results[i].name.toRawUTF8(), now.nodes,
                   results[i - 1].name.toRawUTF8());
      return 1;
    }
  }

  int total = 0;
  for (const auto &r : results) {
    std::printf("=== %s ===\n%s\n", r.name.toRawUTF8(), r.report.toRawUTF8());
    total += r.findings;
  }

  std::printf("%d finding(s) across %d state(s).\n", total,
              (int)results.size());
  return total;
}
