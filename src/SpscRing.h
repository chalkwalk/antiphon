#pragma once

// Adopted from chalkwalk-ninjam (libs/ninjam, MIT). See ../ECOSYSTEM.md.
//
// One producer, one consumer, no locks. The single-writer/single-reader
// requirement and what happens if you break it are documented on the library
// header, along with the TSan run that checks it.

#include <chalkwalk/ninjam/SpscRing.h>

using chalkwalk::ninjam::SpscRing;
