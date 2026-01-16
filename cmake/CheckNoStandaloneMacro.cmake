# Guards against a trap that silently disabled the DAW sync flow in every
# plugin build.
#
# JucePlugin_Build_Standalone is a PROJECT-level flag: juce_add_plugin defines
# it as 1 whenever Standalone appears in FORMATS, including for the shared-code
# target that the VST3 and CLAP link against. It does NOT mean "this instance is
# running as the standalone app". Branching on it in src/ therefore gave the
# plugin the standalone behaviour: transport sync compiled out, tempo never
# compared, sync status text replaced by the standalone phase readout.
#
# The runtime test is NinjamAudioProcessor::isStandaloneApp(), which compares
# juce::AudioProcessor::wrapperType.

file(GLOB_RECURSE sources "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h")

set(offenders "")
foreach(f IN LISTS sources)
  # Leading [^/*]* keeps this to real uses: a line that mentions the macro in a
  # // or * comment (as the ones explaining this trap do) has a slash or star
  # before it and is skipped.
  file(STRINGS "${f}" hits REGEX "^[^/*]*JucePlugin_Build_Standalone")
  if(hits)
    list(APPEND offenders "${f}")
  endif()
endforeach()

if(offenders)
  string(REPLACE ";" "\n  " pretty "${offenders}")
  message(FATAL_ERROR
    "JucePlugin_Build_Standalone is used in:\n  ${pretty}\n"
    "It is 1 in the shared code linked into the VST3 and CLAP, so this does "
    "not mean 'running standalone'. Use isStandaloneApp() instead.")
endif()
