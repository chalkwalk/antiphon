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
#   ../Harmony.h      music theory; destined for chalkwalk-music
#   ../MusicalKey.h   likewise
#   ../ChatFormat.h   splits: the vote ranges are protocol, the rest is UI
#
# When the first two land in chalkwalk-music and ChatFormat is split, this list
# goes empty and the extraction can happen.

set(ALLOWED "../Harmony.h" "../MusicalKey.h" "../ChatFormat.h")

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
foreach(header ${ALLOWED})
  list(FIND SEEN "${header}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR
        "src/jambot no longer includes ${header}, so it is no longer a blocker: "
        "remove it from ALLOWED in this file.")
  endif()
endforeach()

list(LENGTH ALLOWED n)
message(STATUS "jambot boundary: ${n} outward dependencies, all known")
