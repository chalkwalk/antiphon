# What still ties the bots to Antiphon.
#
# `src/jambot/` is the code destined for `chalkwalk-jambot`, staged inside this
# repository first so the separation is proven by the tests that already exist
# rather than by a migration. The boundary is only real if something checks it:
# a directory is otherwise just a directory, and one `#include "../PluginX.h"`
# added in passing would go unnoticed until extraction day.
#
# So this lists the outward includes and fails when the set CHANGES. It does not
# demand zero -- a blocker may be listed -- but an unlisted one is a decision,
# and it should be made deliberately.
#
# TWO lists, because the extraction unit is the code AND its tests. The sources
# are EMPTY, which is the state this check was written to reach: everything the
# bots need comes from a shared library, the theory from chalkwalk-music and
# the room conventions from chalkwalk-ninjam. The tests have ONE left, and it
# is named and explained where it is set.
#
# The lists stay here rather than the check being deleted, because the property
# they guard is the one that matters from now on: this directory is
# extractable, and it should stay that way until it is extracted.

set(ALLOWED "")

file(GLOB JAMBOT_SOURCES "${SRC_DIR}/jambot/*.h" "${SRC_DIR}/jambot/*.cpp")
if(NOT JAMBOT_SOURCES)
  message(FATAL_ERROR "no sources found in ${SRC_DIR}/jambot")
endif()

# ---------------------------------------------------------------------------
# The TESTS are part of the extraction unit, and were the half nobody checked.
#
# A suite that reaches back into Antiphon is exactly as much of a blocker as a
# header that does, and it is easier to miss: the sources here were clean while
# the tests still included `../src/AudioMeasure.h` sixty-three times, and the
# check above could not see it because it only ever looked at src/jambot.
#
# Listed by name rather than globbed, because test/ holds Antiphon's suites
# too and only these move. A new bot suite goes in this list.
set(JAMBOT_TESTS
    BandPatchTests.cpp
    BandPlayStateTests.cpp
    BotAddressTests.cpp
    BotAnswerTests.cpp
    BotBandTests.cpp
    BotChatTests.cpp
    BotDspTests.cpp
    BotLanguageTests.cpp
    BotNamesTests.cpp
    PracticeBotTests.cpp)

# The tests may use JUCE -- they are compiled by Antiphon's juce::UnitTest
# target today and get a Catch2 harness on the way out, the same shim
# chalkwalk-ninjam and chalkwalk-dsp already use. What they may NOT do is
# depend on Antiphon's own headers, because those do not travel.
#
# `../src/MusicalKey.h` is the one that is left, and it is glue rather than
# knowledge: five inline functions composing chalkwalk-ninjam's `[key: ...]`
# envelope with chalkwalk-music's notation. Antiphon needs them and so does
# jambot, and the two are siblings, so at extraction jambot's own `Music.h`
# gains the same five and each side composes the shared libraries for itself.
# It cannot simply be moved there today: both headers would open
# `namespace MusicalKey` in one build, and that is a collision, not a boundary.
set(TEST_ALLOWED "../src/MusicalKey.h")

set(FOUND "")
foreach(path ${JAMBOT_SOURCES})
  get_filename_component(name "${path}" NAME)
  file(STRINGS "${path}" lines REGEX "^#include \"")
  foreach(line ${lines})
    string(REGEX REPLACE "^#include \"([^\"]+)\".*$" "\\1" header "${line}")
    if(header MATCHES "^\\.\\./")
      list(FIND ALLOWED "${header}" at)
      if(at EQUAL -1)
        list(APPEND FOUND "${name} reaches out to ${header}")
      else()
        list(APPEND SEEN "${header}")
      endif()
    endif()
  endforeach()
endforeach()

if(FOUND)
  string(REPLACE ";" "\n  " report "${FOUND}")
  message(FATAL_ERROR
      "src/jambot must not gain new dependencies on Antiphon:\n  ${report}\n"
      "The bots are being extracted; every outward include is a blocker.\n"
      "If this one is genuinely needed, add it to ALLOWED in this file and say "
      "in the commit message how it will be resolved at extraction.")
endif()

# A blocker that has been resolved should be struck off rather than left to rot.
foreach(header IN LISTS ALLOWED)
  list(FIND SEEN "${header}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR
        "src/jambot no longer includes ${header}, so it is no longer a blocker: "
        "remove it from ALLOWED in this file.")
  endif()
endforeach()

set(TEST_FOUND "")
set(TEST_SEEN "")
foreach(name ${JAMBOT_TESTS})
  set(path "${TEST_DIR}/${name}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR
        "${name} is listed as a jambot suite but is not in ${TEST_DIR}: "
        "update JAMBOT_TESTS in this file.")
  endif()
  file(STRINGS "${path}" lines REGEX "^#include \"")
  foreach(line ${lines})
    string(REGEX REPLACE "^#include \"([^\"]+)\".*$" "\\1" header "${line}")
    if(header MATCHES "^\\.\\./" AND NOT header MATCHES "^\\.\\./src/jambot/")
      list(FIND TEST_ALLOWED "${header}" at)
      if(at EQUAL -1)
        list(APPEND TEST_FOUND "${name} reaches out to ${header}")
      else()
        list(APPEND TEST_SEEN "${header}")
      endif()
    endif()
  endforeach()
endforeach()

if(TEST_FOUND)
  string(REPLACE ";" "\n  " report "${TEST_FOUND}")
  message(FATAL_ERROR
      "jambot's tests must not gain new dependencies on Antiphon:\n  ${report}\n"
      "They move with the code they cover. Reach for the shared library "
      "directly -- chalkwalk-music, chalkwalk-dsp, chalkwalk-ninjam -- rather "
      "than for Antiphon's alias header, or add it to TEST_ALLOWED and say in "
      "the commit message how it will be resolved at extraction.")
endif()

foreach(header IN LISTS TEST_ALLOWED)
  list(FIND TEST_SEEN "${header}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR
        "jambot's tests no longer include ${header}, so it is no longer a "
        "blocker: remove it from TEST_ALLOWED in this file.")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# ...and no JUCE, which is the other half of being extractable.
#
# `chalkwalk-jambot` is to be JUCE-free: the bots run in a plugin today and are
# meant to run from a command line tomorrow, and a library that drags a GUI
# framework in for its strings cannot do the second. Everything here is
# std::string, std::mutex and a timer the host supplies -- see
# jambot/BotClient.h for why scheduling is asked for rather than assumed.
#
# Checked rather than trusted for the reason the music layer is: one
# `juce::String` added in passing still builds and still passes, and is only
# discovered when somebody tries to move the file.

set(JUCE_FOUND "")
foreach(path ${JAMBOT_SOURCES})
  get_filename_component(name "${path}" NAME)
  file(STRINGS "${path}" lines)
  set(lineNumber 0)
  foreach(line ${lines})
    math(EXPR lineNumber "${lineNumber} + 1")
    string(REGEX REPLACE "//.*" "" code "${line}")
    if(code MATCHES "juce::|JuceHeader|JUCE_")
      list(APPEND JUCE_FOUND "${name}:${lineNumber}: ${line}")
    endif()
  endforeach()
endforeach()

if(JUCE_FOUND)
  string(REPLACE ";" "\n  " report "${JUCE_FOUND}")
  message(FATAL_ERROR
      "src/jambot must stay JUCE-free:\n  ${report}\n"
      "Use std::string, or ask the host -- BotClient supplies the timer.")
endif()

list(LENGTH ALLOWED n)
if(n EQUAL 0)
  message(STATUS "jambot boundary: clean -- JUCE-free, and nothing reaches back into Antiphon")
else()
  message(STATUS "jambot boundary: ${n} outward dependencies, all known")
endif()

list(LENGTH TEST_ALLOWED tn)
message(STATUS "jambot tests: ${tn} outward dependencies, all known")
