#include "ReaperBridge.h"

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wshadow-field-in-constructor",
                                    "-Wnon-virtual-dtor")
#include <pluginterfaces/base/funknown.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

// The declarations below are copied from Cockos's REAPER SDK
// (github.com/justinfrankel/reaper-sdk: reaper_plugin.h and
// reaper_vst3_interfaces.h), which is under the zlib licence -- see
// THIRDPARTY.md. Only the parts we call are reproduced. The function
// signatures are the ones REAPER's own Python binding declares
// (Plugins/reaper_python.py, shipped with REAPER).

namespace {

// reaper_plugin.h: the struct the CLAP extension "cockos.reaper_extension"
// points at. HWND is a pointer on every platform REAPER runs on.
struct ReaperPluginInfo {
  int caller_version;
  void *hwnd_main;
  int (*Register)(const char *name, void *infostruct);
  void *(*GetFunc)(const char *name);
};

// reaper_vst3_interfaces.h, altered only to sit outside the Steinberg
// namespace's headers it was written to be included among.
class IReaperHostApplication : public Steinberg::FUnknown {
public:
  virtual void *PLUGIN_API getReaperApi(Steinberg::CStringA funcname) = 0;
  virtual void *PLUGIN_API getReaperParent(Steinberg::uint32 w) = 0;
  virtual void *PLUGIN_API reaperExtended(Steinberg::uint32 call, void *parm1,
                                          void *parm2, void *parm3) = 0;
  static const Steinberg::FUID iid;
};
DECLARE_CLASS_IID(IReaperHostApplication, 0x79655E36, 0x77EE4267, 0xA573FEF7,
                  0x4912C27C)
DEF_CLASS_IID(IReaperHostApplication)

class ReaperBridge final : public HostControl {
public:
  using SetCurrentBPM = void (*)(void *project, double bpm, bool wantUndo);
  using OnPlayButton = void (*)();
  using GetPlayState = int (*)();
  using GetSetLoopTimeRange = void (*)(bool isSet, bool isLoop, double *start,
                                       double *end, bool allowAutoSeek);
  using TimeMapQNToTime = double (*)(double qn);
  using GetSetRepeat = int (*)(int val); // -1 query, 0 off, 1 on, 2 toggle

  bool resolve(const ReaperResolver &r) {
    setBpm = reinterpret_cast<SetCurrentBPM>(r("SetCurrentBPM"));
    playButton = reinterpret_cast<OnPlayButton>(r("OnPlayButton"));
    playState = reinterpret_cast<GetPlayState>(r("GetPlayState"));
    loopRange =
        reinterpret_cast<GetSetLoopTimeRange>(r("GetSet_LoopTimeRange"));
    qnToTime = reinterpret_cast<TimeMapQNToTime>(r("TimeMap_QNToTime"));
    repeat = reinterpret_cast<GetSetRepeat>(r("GetSetRepeat"));
    return setBpm != nullptr && playButton != nullptr && playState != nullptr;
  }

  bool available() const override { return setBpm != nullptr; }

  bool setTempo(double bpm) override {
    if (setBpm == nullptr || bpm <= 0.0)
      return false;
    setBpm(nullptr, bpm, true);
    return true;
  }

  bool play() override {
    if (playButton == nullptr)
      return false;
    playButton();
    return true;
  }

  bool isPlaying() const override {
    return playState != nullptr && (playState() & 1) != 0;
  }

  bool setLoopBeats(double startPpq, double endPpq) override {
    if (loopRange == nullptr || qnToTime == nullptr || endPpq <= startPpq)
      return false;
    double start = qnToTime(startPpq);
    double end = qnToTime(endPpq);
    loopRange(true, true, &start, &end, false);
    // The loop points alone do not loop: Reaper's repeat is a separate switch.
    if (repeat != nullptr)
      repeat(1);
    return true;
  }

private:
  SetCurrentBPM setBpm = nullptr;
  OnPlayButton playButton = nullptr;
  GetPlayState playState = nullptr;
  GetSetLoopTimeRange loopRange = nullptr;
  TimeMapQNToTime qnToTime = nullptr;
  GetSetRepeat repeat = nullptr;
};

class ReaperVst3Extensions final : public juce::VST3ClientExtensions {
public:
  explicit ReaperVst3Extensions(std::function<void(ReaperResolver)> f)
      : found(std::move(f)) {}

  void setIHostApplication(Steinberg::FUnknown *host) override {
    if (host == nullptr || !found)
      return;
    void *obj = nullptr;
    if (host->queryInterface(IReaperHostApplication::iid, &obj) !=
            Steinberg::kResultOk ||
        obj == nullptr)
      return;
    auto *reaper = static_cast<IReaperHostApplication *>(obj);
    found([reaper](const char *name) { return reaper->getReaperApi(name); });
  }

private:
  std::function<void(ReaperResolver)> found;
};

} // namespace

std::unique_ptr<HostControl> makeReaperBridge(const ReaperResolver &resolve) {
  if (!resolve)
    return nullptr;
  auto bridge = std::make_unique<ReaperBridge>();
  if (!bridge->resolve(resolve))
    return nullptr;
  return bridge;
}

ReaperResolver reaperResolverFromClapExtension(const void *extension) {
  if (extension == nullptr)
    return {};
  const auto *info = static_cast<const ReaperPluginInfo *>(extension);
  if (info->GetFunc == nullptr)
    return {};
  return [info](const char *name) { return info->GetFunc(name); };
}

std::unique_ptr<juce::VST3ClientExtensions>
makeReaperVst3Extensions(std::function<void(ReaperResolver)> found) {
  return std::make_unique<ReaperVst3Extensions>(std::move(found));
}
