# What is still on its way out must not reach for JUCE.
#
# `Harmony` and the key itself have gone to `chalkwalk-music`, which is strictly
# JUCE-free; what this guards now is the rest of the same journey. `MusicalKey`
# is the `[key: ...]` tag and the `/key` advice line -- NINJAM protocol text,
# headed for `chalkwalk-ninjam`, which is JUCE-free too. `RoomHarmony` is room
# policy that the bots read.
#
# This is a test rather than a convention because the failure is silent and
# late: one `juce::String` added in passing still builds, still passes, and is
# only discovered when somebody tries to move the file. Cheap to check, and it
# fails on the line that broke it. The same shape as
# CheckNoStandaloneMacro.cmake, and for the same reason.

set(GUARDED
    MusicalKey.h MusicalKey.cpp
    RoomHarmony.h)

set(OFFENDERS "")
foreach(name ${GUARDED})
  set(path "${SRC_DIR}/${name}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "guarded file is missing: ${path}")
  endif()

  file(STRINGS "${path}" lines)
  set(lineNumber 0)
  foreach(line ${lines})
    math(EXPR lineNumber "${lineNumber} + 1")
    # Comments may discuss JUCE -- several explain why it is not here.
    string(REGEX REPLACE "//.*" "" code "${line}")
    if(code MATCHES "juce::|JuceHeader|JUCE_")
      list(APPEND OFFENDERS "${name}:${lineNumber}: ${line}")
    endif()
  endforeach()
endforeach()

if(OFFENDERS)
  string(REPLACE ";" "\n  " report "${OFFENDERS}")
  message(FATAL_ERROR
      "The music-theory layer must stay JUCE-free:\n  ${report}\n"
      "Use src/TextUtil.h, or std::string directly. See src/MusicalKey.h.")
endif()

message(STATUS "music layer is JUCE-free")
