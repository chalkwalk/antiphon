#pragma once

// Adopted from chalkwalk-ninjam (libs/ninjam, MIT). See ../ECOSYSTEM.md.
//
// Unlike the other five files that moved out to that library, this one changed
// shape on the way: a JUCE-free library cannot have juce::MemoryBlock and
// juce::String in its signatures, so payloads are now ByteBuffer
// (std::vector<uint8_t>) and every string is std::string.
//
// The namespace alias keeps `NinjamProtocol::` spelled as it always was, which
// is most of the call sites. The conversions that remain are real and are
// written out at each site rather than hidden behind a wrapper: juce::String
// constructs from std::string implicitly, so parsed fields flow into the UI
// untouched, and the other direction costs an explicit .toStdString() that
// says plainly where the boundary is.

#include <chalkwalk/ninjam/Bytes.h>
#include <chalkwalk/ninjam/NinjamProtocol.h>

namespace NinjamProtocol = chalkwalk::ninjam::protocol;

using chalkwalk::ninjam::ByteBuffer;
