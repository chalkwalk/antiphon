// Antiphon's standalone application.
//
// This replaces JUCE's stock StandaloneFilterApp (via
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP) for one reason: the stock one opens
// the audio device inside the window's constructor. StandaloneFilterWindow
// builds its StandalonePluginHolder as a constructor argument, so
// deviceManager.initialise() runs to completion before any window exists, and
// its returned error string is discarded. A backend that blocks there leaves no
// window, no error and no route to the device picker -- the app simply sits at
// the command line.
//
// Antiphon shows its window first, then opens the device on a worker thread
// with a budget. AudioDeviceStartup holds the policy and is tested separately.
//
// See DESIGN.md section 16.

#include "AudioDeviceStartup.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <cstdio>
// Stamps AudioProcessor::wrapperType before construction, which is what makes
// AntiphonAudioProcessor::isStandaloneApp() true. Constructing the processor
// any other way would silently leave it thinking it is a plugin.
#include <juce_audio_plugin_client/detail/juce_CreatePluginFilter.h>

namespace {

// Long enough that a slow-but-working device is not given up on, short enough
// that a wedged one does not feel like a hang.
constexpr int kDeviceOpenBudgetMs = 5000;

// Startup progress goes to stderr, not juce::Logger: with no logger installed
// writeToLog is a no-op in a release build, and "why is there no audio" is
// exactly the question someone runs this from a terminal to answer.
void report(const juce::String &line) {
  std::fprintf(stderr, "[antiphon] %s\n", line.toRawUTF8());
  std::fflush(stderr);
}

// Opens the device off the message thread.
//
// On timeout this object is deliberately leaked rather than joined: the whole
// premise is that the backend may never return, so stopThread() would block
// exactly as long as the bug we are working around.
class DeviceOpenProbe : public juce::Thread {
public:
  DeviceOpenProbe(juce::AudioDeviceManager &dm, int ins, int outs,
                  std::unique_ptr<juce::XmlElement> state)
      : juce::Thread("Antiphon audio open"), deviceManager(dm), numIns(ins),
        numOuts(outs), savedState(std::move(state)) {}

  void run() override {
    auto err = deviceManager.initialise(numIns, numOuts, savedState.get(), true);
    // Written before the flag, read after it, so the flag publishes it.
    error = err;
    finished.store(true);
  }

  std::atomic<bool> finished{false};
  juce::String error;

private:
  juce::AudioDeviceManager &deviceManager;
  int numIns, numOuts;
  std::unique_ptr<juce::XmlElement> savedState;
};

// What the window shows instead of the mixer when there is no audio device.
class AudioTroubleView : public juce::Component {
public:
  AudioTroubleView(juce::AudioDeviceManager &dm, int ins, int outs,
                   const juce::String &reason, std::function<void()> onAccept)
      : accept(std::move(onAccept)) {
    message.setText(reason, juce::dontSendNotification);
    message.setJustificationType(juce::Justification::centredLeft);
    message.setTitle("Audio device problem");
    message.setDescription(reason);
    addAndMakeVisible(message);

    selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        dm, ins, ins, outs, outs, false, false, true, false);
    selector->setTitle("Audio device settings");
    selector->setDescription("Choose an audio device for Antiphon to use");
    addAndMakeVisible(*selector);

    useButton.setButtonText("Use this device");
    useButton.setTitle("Use this device");
    useButton.setDescription(
        "Start Antiphon with the device selected above, and remember it");
    useButton.onClick = [this] { if (accept) accept(); };
    addAndMakeVisible(useButton);
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
  }

  void resized() override {
    auto r = getLocalBounds().reduced(16);
    message.setBounds(r.removeFromTop(48));
    r.removeFromTop(8);
    useButton.setBounds(r.removeFromBottom(30).removeFromLeft(160));
    r.removeFromBottom(8);
    selector->setBounds(r);
  }

private:
  juce::Label message;
  std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
  juce::TextButton useButton;
  std::function<void()> accept;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioTroubleView)
};

class AntiphonWindow : public juce::DocumentWindow, private juce::Timer {
public:
  AntiphonWindow(const juce::String &title, juce::PropertySet *settingsToUse)
      : DocumentWindow(title,
                       juce::LookAndFeel::getDefaultLookAndFeel().findColour(
                           juce::ResizableWindow::backgroundColourId),
                       DocumentWindow::allButtons),
        settings(settingsToUse) {
    setUsingNativeTitleBar(true);
    setResizable(true, false);

    processor = juce::createPluginFilterOfType(
        juce::AudioProcessor::wrapperType_Standalone);
    // JUCE's standalone host offers exactly one input and one output bus, so
    // the extra buses the plugin exposes for stem routing have no home here.
    processor->disableNonMainBuses();
    processor->setRateAndBufferSizeDetails(44100, 512);
    restoreProcessorState();

    numIns = processor->getMainBusNumInputChannels();
    numOuts = processor->getMainBusNumOutputChannels();

    // The editor goes up immediately, before any device is touched. This is
    // the entire point of this class.
    setContentOwned(processor->createEditorIfNeeded(), true);
    setBoundsFromSettings();
    setVisible(true);
    report("window is up; opening the audio device in the background");

    beginOpeningDevice();
  }

  ~AntiphonWindow() override {
    stopTimer();
    clearContentComponent();

    if (deviceManager != nullptr) {
      deviceManager->removeAudioCallback(&player);
      saveAudioSetup();
    }
    player.setProcessor(nullptr);

    saveProcessorState();
    if (settings != nullptr) {
      settings->setValue("windowX", getX());
      settings->setValue("windowY", getY());
    }
    processor->editorBeingDeleted(processor->getActiveEditor());
    processor = nullptr;
  }

  void closeButtonPressed() override { juce::JUCEApplication::quit(); }

private:
  void beginOpeningDevice() {
    startup.reset();
    deviceManager = std::make_unique<juce::AudioDeviceManager>();

    std::unique_ptr<juce::XmlElement> saved;
    if (settings != nullptr) {
      auto xml = settings->getValue("audioSetup");
      if (xml.isNotEmpty()) saved = juce::parseXML(xml);
    }

    openStartedMs = juce::Time::getMillisecondCounter();
    probe = std::make_unique<DeviceOpenProbe>(*deviceManager, numIns, numOuts,
                                              std::move(saved));
    probe->startThread();
    startTimer(50);
  }

  void timerCallback() override {
    AudioDeviceStartup::Inputs in;
    in.probeFinished = probe != nullptr && probe->finished.load();
    in.probeSucceeded = in.probeFinished && probe->error.isEmpty();
    in.elapsedMs =
        (int64_t)(juce::Time::getMillisecondCounter() - openStartedMs);

    if (!startup.update(in)) return;

    stopTimer();

    switch (startup.get()) {
    case AudioDeviceStartup::State::Ready:
      report("audio device open: " + currentDeviceName());
      probe->stopThread(1000);
      probe = nullptr;
      startPlaying();
      break;

    case AudioDeviceStartup::State::Failed:
      showTrouble(juce::String(AudioDeviceStartup::describe(startup.get())) +
                  "\n" + probe->error);
      probe->stopThread(1000);
      probe = nullptr;
      break;

    case AudioDeviceStartup::State::TimedOut:
      // The probe is still inside the backend and may never come out. Abandon
      // both it and the device manager it is holding: destroying either would
      // block, and closing a device that never opened is not meaningful.
      // Leaking them is the price of staying usable.
      probe.release();
      deviceManager.release();
      deviceManager = std::make_unique<juce::AudioDeviceManager>();
      showTrouble(AudioDeviceStartup::describe(startup.get()));
      break;

    case AudioDeviceStartup::State::Opening:
      break;
    }
  }

  juce::String currentDeviceName() const {
    if (deviceManager == nullptr) return "(none)";
    if (auto *d = deviceManager->getCurrentAudioDevice()) return d->getName();
    return "(none)";
  }

  void startPlaying() {
    player.setProcessor(processor.get());
    deviceManager->addAudioCallback(&player);
    saveAudioSetup();

    if (dynamic_cast<AudioTroubleView *>(getContentComponent()) != nullptr) {
      clearContentComponent();
      setContentOwned(processor->createEditorIfNeeded(), true);
      setBoundsFromSettings();
    }
  }

  void showTrouble(const juce::String &reason) {
    report(reason);
    clearContentComponent();
    setContentOwned(new AudioTroubleView(*deviceManager, numIns, numOuts, reason,
                                         [this] { acceptChosenDevice(); }),
                    true);
    centreWithSize(560, 420);
  }

  // The selector has already opened whatever the user picked, so there is
  // nothing to retry -- only to confirm, remember, and get on with it.
  void acceptChosenDevice() {
    if (deviceManager->getCurrentAudioDevice() == nullptr) return;
    startup.reset();
    AudioDeviceStartup::Inputs done;
    done.probeFinished = true;
    done.probeSucceeded = true;
    startup.update(done);
    startPlaying();
  }

  void saveAudioSetup() {
    if (settings == nullptr || deviceManager == nullptr) return;
    if (auto xml = deviceManager->createStateXml())
      settings->setValue("audioSetup", xml->toString());
  }

  void restoreProcessorState() {
    if (settings == nullptr) return;
    juce::MemoryBlock data;
    if (data.fromBase64Encoding(settings->getValue("filterState")) &&
        data.getSize() > 0)
      processor->setStateInformation(data.getData(), (int)data.getSize());
  }

  void saveProcessorState() {
    if (settings == nullptr) return;
    juce::MemoryBlock data;
    processor->getStateInformation(data);
    settings->setValue("filterState", data.toBase64Encoding());
  }

  void setBoundsFromSettings() {
    if (auto *editor = processor->getActiveEditor())
      setContentComponentSize(editor->getWidth(), editor->getHeight());
    if (settings != nullptr) {
      const int x = settings->getIntValue("windowX", -100);
      const int y = settings->getIntValue("windowY", -100);
      if (x != -100 && y != -100) { setBoundsConstrained(getBounds().withPosition(x, y)); return; }
    }
    centreWithSize(getWidth(), getHeight());
  }

  juce::PropertySet *settings;
  std::unique_ptr<juce::AudioProcessor> processor;
  juce::AudioProcessorPlayer player;
  std::unique_ptr<juce::AudioDeviceManager> deviceManager;
  std::unique_ptr<DeviceOpenProbe> probe;
  AudioDeviceStartup startup{kDeviceOpenBudgetMs};
  juce::uint32 openStartedMs = 0;
  int numIns = 0, numOuts = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiphonWindow)
};

class AntiphonStandaloneApp : public juce::JUCEApplication {
public:
  AntiphonStandaloneApp() {
    juce::PropertiesFile::Options options;
    options.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
    options.folderName = "~/.config";
#else
    options.folderName = "";
#endif
    appProperties.setStorageParameters(options);
  }

  const juce::String getApplicationName() override {
    return juce::CharPointer_UTF8(JucePlugin_Name);
  }
  const juce::String getApplicationVersion() override {
    return JucePlugin_VersionString;
  }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String &) override {
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) {
      report("no display available; nothing to show");
      quit();
      return;
    }
    mainWindow = std::make_unique<AntiphonWindow>(
        getApplicationName(), appProperties.getUserSettings());
  }

  void shutdown() override {
    mainWindow = nullptr;
    appProperties.saveIfNeeded();
  }

  void systemRequestedQuit() override { quit(); }

private:
  juce::ApplicationProperties appProperties;
  std::unique_ptr<AntiphonWindow> mainWindow;
};

} // namespace

juce::JUCEApplicationBase *juce_CreateApplication() {
  return new AntiphonStandaloneApp();
}
