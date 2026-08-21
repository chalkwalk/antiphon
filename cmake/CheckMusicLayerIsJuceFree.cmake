# What is still on its way out must not reach for JUCE.
#
# Almost everything this guarded has arrived: `Harmony` and the key are in
# `chalkwalk-music`, the room conventions in `chalkwalk-ninjam`, both strictly
# JUCE-free. What is left is the glue and the policy that have not moved yet.
#
# `MusicalKey.h` composes the envelope with the notation -- three inline
# functions -- and `RoomHarmony.h` is what a chat line does to a room. The
# second travels with `PracticeBot` when the bots leave, so it has to stay
# JUCE-free until it does; the first is small enough that the cost of checking
# is nil and the cost of noticing late is a build that will not extract.
#
# This is a test rather than a convention because the failure is silent and
# late: one `juce::String` added in passing still builds, still passes, and is
# only discovered when somebody tries to move the file. Cheap to check, and it
# fails on the line that broke it. The same shape as
# CheckNoStandaloneMacro.cmake, and for the same reason.

set(GUARDED
    MusicalKey.h
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
