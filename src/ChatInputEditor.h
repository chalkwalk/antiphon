#pragma once

#include <JuceHeader.h>

// A single-line editable text editor with an accessibility handler designed
// specifically for screen readers (VoiceOver, NVDA, JAWS), mirroring the
// architecture of ChatHistoryEditor.
//
// By overriding getDescription() to return empty, VoiceOver on macOS does not
// duplicate the title or mask the text content with a redundant static description.
class ChatInputEditor : public juce::TextEditor {
public:
  ChatInputEditor() {
    setMultiLine(false);
    setReturnKeyStartsNewLine(false);
    setCaretVisible(true);
    setTitle("Chat message");
    setDescription("Chat message");
  }

  std::unique_ptr<juce::AccessibilityHandler>
  createAccessibilityHandler() override {
    return std::make_unique<ChatInputAccessibilityHandler>(*this);
  }

private:
  class ChatInputAccessibilityHandler final
      : public juce::AccessibilityHandler {
  public:
    explicit ChatInputAccessibilityHandler(ChatInputEditor &editorToWrap)
        : juce::AccessibilityHandler(
              editorToWrap, juce::AccessibilityRole::editableText, {},
              {std::make_unique<ChatInputTextInterface>(editorToWrap)}),
          editor(editorToWrap) {}

    juce::String getTitle() const override { return editor.getTitle(); }

    // Returning empty description prevents VoiceOver on macOS from repeating
    // the label or masking the input/cursor text.
    juce::String getDescription() const override { return {}; }

    juce::String getHelp() const override { return {}; }

  private:
    class ChatInputTextInterface final
        : public juce::AccessibilityTextInterface {
    public:
      explicit ChatInputTextInterface(ChatInputEditor &e) : editor(e) {}

      bool isDisplayingProtectedText() const override {
        return editor.getPasswordCharacter() != 0;
      }
      bool isReadOnly() const override { return editor.isReadOnly(); }

      int getTotalNumCharacters() const override {
        return editor.getText().length();
      }

      juce::Range<int> getSelection() const override {
        return editor.getHighlightedRegion();
      }

      void setSelection(juce::Range<int> r) override {
        editor.setHighlightedRegion(r);
      }

      juce::String getText(juce::Range<int> r) const override {
        return editor.getTextInRange(r);
      }

      void setText(const juce::String &t) override { editor.setText(t); }

      int getTextInsertionOffset() const override {
        return editor.getCaretPosition();
      }

      juce::RectangleList<int>
      getTextBounds(juce::Range<int> textRange) const override {
        auto localRects = editor.getTextBounds(textRange);
        if (localRects.isEmpty()) {
          localRects.add(editor.getCaretRectangle());
        }
        juce::RectangleList<int> globalRects;
        for (const auto &r : localRects)
          globalRects.add(editor.localAreaToGlobal(r));
        return globalRects;
      }

      int getOffsetAtPoint(juce::Point<int> point) const override {
        return editor.getTextIndexAt(editor.getLocalPoint(nullptr, point));
      }

    private:
      ChatInputEditor &editor;
      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatInputTextInterface)
    };

    ChatInputEditor &editor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatInputAccessibilityHandler)
  };

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatInputEditor)
};
