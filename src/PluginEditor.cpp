#include "PluginEditor.h"
#include "PluginProcessor.h"

NinjamAudioProcessorEditor::NinjamAudioProcessorEditor(NinjamAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setSize(800, 600);
}

NinjamAudioProcessorEditor::~NinjamAudioProcessorEditor() {}

void NinjamAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  g.setColour(juce::Colours::white);
  g.setFont(24.0f);
  g.drawFittedText("Ninjam JUCE Plugin", getLocalBounds(),
                   juce::Justification::centred, 1);
}

void NinjamAudioProcessorEditor::resized() {}
