#include "PluginEditor.h"
#include "PluginProcessor.h"

NinjamAudioProcessorEditor::NinjamAudioProcessorEditor(NinjamAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setSize(800, 600);
  startTimerHz(30);
}

NinjamAudioProcessorEditor::~NinjamAudioProcessorEditor() {}

void NinjamAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  g.setColour(juce::Colours::white);
  g.setFont(24.0f);
  g.drawFittedText("Ninjam JUCE Plugin", 0, 10, getWidth(), 30,
                   juce::Justification::centred, 1);

  g.setFont(16.0f);
  juce::String hostInfo = "Host Sync: ";
  hostInfo += audioProcessor.hostIsPlaying ? "Playing" : "Stopped";
  hostInfo += " | BPM: " + juce::String(audioProcessor.hostBpm, 1);
  hostInfo += " | PPQ: " + juce::String(audioProcessor.hostPpqPosition, 2);
  g.drawFittedText(hostInfo, 10, 50, getWidth() - 20, 30,
                   juce::Justification::left, 1);

  juce::String internalInfo = "Metronome: ";
  internalInfo += "BPM: " + juce::String(audioProcessor.internalBpm, 1);
  internalInfo += " | BPI: " + juce::String(audioProcessor.internalBpi);
  internalInfo +=
      " | Phase: " + juce::String(audioProcessor.internalPhaseBeats, 2);
  g.drawFittedText(internalInfo, 10, 80, getWidth() - 20, 30,
                   juce::Justification::left, 1);

  if (std::abs(audioProcessor.hostBpm - audioProcessor.internalBpm) > 0.1) {
    g.setColour(juce::Colours::red);
    g.drawFittedText("WARNING: Host BPM does not match Server BPM!", 10, 110,
                     getWidth() - 20, 30, juce::Justification::left, 1);
  }
}

void NinjamAudioProcessorEditor::resized() {}

void NinjamAudioProcessorEditor::timerCallback() { repaint(); }
