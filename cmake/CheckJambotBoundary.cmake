# What still ties the bots to Antiphon.
#
# `src/jambot/` is the code destined for `chalkwalk-jambot`, staged inside this
# repository first so the separation is proven by the tests that already exist
# rather than by a migration. The boundary is only real if something checks it:
# a directory is otherwise just a directory, and one `#include "../PluginX.h"`
# added in passing would go unnoticed until extraction day.
#
# So this lists the outward includes and fails when the set CHANGES. It does not
# demand zero -- three are expected and are the extraction's blockers -- but a
# fourth is a decision, and it should be made deliberately.
#
# EMPTY, which is the state this check was written to reach. Everything the
# bots need now comes from a shared library: the theory from chalkwalk-music,
# the room conventions from chalkwalk-ninjam. Nothing in src/jambot reaches
# back into Antiphon.
#
# The list stays here rather than the check being deleted, because the property
# it guards is the one that matters from now on: this directory is extractable,
# and it should stay that way while the remaining work -- the client interface,
# and PracticeBot -- happens.

set(ALLOWED "")

file(GLOB JAMBOT_SOURCES "${SRC_DIR}/jambot/*.h" "${SRC_DIR}/jambot/*.cpp")
if(NOT JAMBOT_SOURCES)
  message(FATAL_ERROR "no sources found in ${SRC_DIR}/jambot")
endif()

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
