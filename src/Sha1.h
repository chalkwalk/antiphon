#pragma once

// Adopted from chalkwalk-ninjam (libs/ninjam, MIT). See ../ECOSYSTEM.md.
//
// This header exists only so that call sites keep saying `Sha1` rather than
// `chalkwalk::ninjam::Sha1`. The implementation, its comments and its tests
// all live in the library now.

#include <chalkwalk/ninjam/Sha1.h>

using chalkwalk::ninjam::Sha1;
