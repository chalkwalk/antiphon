#pragma once

#include "HostControl.h"

#include <JuceHeader.h>

#include <functional>
#include <memory>

// Reaper's plugin API, reached from inside a plugin.
//
// Both formats end at the same place -- a function that resolves a Reaper API
// function by name -- and get there differently:
//   CLAP: the host extension "cockos.reaper_extension", a reaper_plugin_info_t
//         whose GetFunc is the resolver.
//   VST3: the IHostApplication JUCE hands us answers IReaperHostApplication,
//         whose getReaperApi is the resolver.
//
// Plugin target only: it needs the VST3 SDK's interface headers, which the
// test target does not have. Everything testable is in HostControl.h.

using ReaperResolver = std::function<void *(const char *)>;

// Null when the resolver cannot find the functions we need, so a Reaper build
// that lacks one behaves as any other DAW.
std::unique_ptr<HostControl> makeReaperBridge(ReaperResolver resolve);

// The resolver from a CLAP host extension pointer, or an empty function.
ReaperResolver reaperResolverFromClapExtension(const void *extension);

// The VST3 client extensions object the processor returns from
// getVST3ClientExtensions(). Calls `found` on the message thread when the host
// turns out to be Reaper.
std::unique_ptr<juce::VST3ClientExtensions>
makeReaperVst3Extensions(std::function<void(ReaperResolver)> found);
