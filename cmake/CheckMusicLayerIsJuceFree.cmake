# The music-theory layer must not reach for JUCE.
#
# `MusicalKey` and `Harmony` are used by the bots AND by the plugin's chat UI --
# announcing a key and reading a chord chart are room features that work with no
# band in the room -- so they belong to neither, and their destination is
# `chalkwalk-music`, which is strictly JUCE-free. `juce::String` was the only
# thing keeping them here.
#
# This is a test rather than a convention because the failure is silent and
# late: one `juce::String` added in passing still builds, still passes, and is
# only discovered when somebody tries to move the file. Cheap to check, and it
# fails on the line that broke it. The same shape as
# CheckNoStandaloneMacro.cmake, and for the same reason.

set(GUARDED
    MusicalKey.h MusicalKey.cpp
    Harmony.h Harmony.cpp
    RoomHarmony.h
    TextUtil.h)

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
