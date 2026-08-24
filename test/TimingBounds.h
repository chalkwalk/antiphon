#pragma once

// Whether this build distorts wall-clock time badly enough that a timing
// assertion cannot mean anything.
//
// A few tests here assert that something takes LESS than a bound: that
// `botCount()` does not wait for a Vorbis encode, that a render is one bot's
// work and not four, that enqueuing does not encode inline. Every one of them
// is a claim about the shape of the code, expressed in milliseconds because
// that is the only way to observe it.
//
// Under a sanitiser those milliseconds are worthless. TSan runs this suite
// roughly ten times slower, which took one bot's render from 435 ms to
// 1569 ms -- so the bound either fails in a sanitiser build or is loosened
// until it no longer fails in an ordinary one, and a bound that cannot fail is
// not a test. The measurement is still logged either way; only the assertion
// is skipped, and it is skipped loudly.
//
// Everything else in these tests -- that the interval arrives, that the band
// plays, that the roster is right -- runs normally. It is only the stopwatch
// that is meaningless here.

namespace timingbounds {

#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
constexpr bool kDistorted = true;
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer) ||     \
    __has_feature(memory_sanitizer)
constexpr bool kDistorted = true;
#else
constexpr bool kDistorted = false;
#endif
#else
constexpr bool kDistorted = false;
#endif

} // namespace timingbounds
