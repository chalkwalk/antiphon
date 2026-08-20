#pragma once

// Adopted from chalkwalk-ninjam (libs/ninjam, MIT). See ../ECOSYSTEM.md.
//
// A namespace alias rather than a pile of using-declarations, because
// ChannelMix is a namespace of free functions and aliasing it keeps every
// `ChannelMix::` call site in this repository spelled exactly as it was.

#include <chalkwalk/ninjam/ChannelMix.h>

namespace ChannelMix = chalkwalk::ninjam::channelmix;
