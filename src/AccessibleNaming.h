#pragma once

#include <JuceHeader.h>

// Give a control the accessible name its visible label already provides.
//
// A stock JUCE component we embed is annotated for sighted users and not for a
// reader: juce::AudioDeviceSelectorComponent labels its Output and Input
// dropdowns with Labels attached via Label::attachToComponent, and sets no
// accessible title on the dropdowns themselves. A sighted user reads "Output:"
// next to the combo; a reader user is told nothing, on the one screen that
// exists because something already went wrong.
//
// The association is JUCE's own, so this is not a guess: getAttachedComponent()
// is exactly the link the label was created with. We only ever fill a blank --
// a control that named itself keeps its name.

namespace AccessibleNaming {

// Trailing colons read badly when spoken: "Output colon" is not what a label
// means by "Output:".
inline juce::String speakable(const juce::String &labelText) {
  return labelText.trim().trimCharactersAtEnd(":").trim();
}

inline void adoptLabelNames(juce::Component &root) {
  for (auto *child : root.getChildren()) {
    if (child == nullptr)
      continue;

    if (auto *label = dynamic_cast<juce::Label *>(child)) {
      if (auto *target = label->getAttachedComponent()) {
        const auto name = speakable(label->getText());
        if (name.isNotEmpty() && target->getTitle().isEmpty())
          target->setTitle(name);
        if (name.isNotEmpty() && target->getDescription().isEmpty())
          target->setDescription(name);
      }
    }

    adoptLabelNames(*child);
  }
}

} // namespace AccessibleNaming
