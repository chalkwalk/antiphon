#pragma once

#include <JuceHeader.h>

// A read-only multi-line text editor with an accessibility handler designed
// specifically for screen readers (VoiceOver, NVDA, JAWS).
//
// JUCE's default TextEditorAccessibilityHandler switches to
// AccessibilityRole::staticText when read-only, which causes VoiceOver on macOS
// to treat the entire multi-line log as a single static label, and setting an
// accessible description further masks the actual text content.
//
// ChatHistoryEditor exposes AccessibilityRole::editableText with a read-only
// text interface so that screen readers recognize it as a navigable document /
// text area while preventing edits.
class ChatHistoryEditor : public juce::TextEditor {
public:
  ChatHistoryEditor() {
    setMultiLine(true);
    setReadOnly(true);
    setScrollbarsShown(true);
    setCaretVisible(true);
    setTitle("Chat history");
    setDescription("Messages from the server and the other players");
  }

  void appendMessage(const juce::String &lineText, juce::Colour colour) {
    setColour(juce::TextEditor::textColourId, colour);
    moveCaretToEnd();
    insertTextAtCaret(lineText + "\n");
    if (auto *handler = getAccessibilityHandler()) {
      handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
      handler->notifyAccessibilityEvent(juce::AccessibilityEvent::textChanged);
    }
  }

  std::unique_ptr<juce::AccessibilityHandler>
  createAccessibilityHandler() override {
    return std::make_unique<ChatAccessibilityHandler>(*this);
  }

private:
  class ChatAccessibilityHandler final : public juce::AccessibilityHandler {
  public:
    explicit ChatAccessibilityHandler(ChatHistoryEditor &editorToWrap)
        : juce::AccessibilityHandler(
              editorToWrap, juce::AccessibilityRole::editableText, {},
              {std::make_unique<ChatTextInterface>(editorToWrap)}),
          editor(editorToWrap) {}

    juce::String getTitle() const override { return editor.getTitle(); }

    // Returning empty description prevents VoiceOver on macOS from replacing the
    // editor's text content with the static description label.
    juce::String getDescription() const override { return {}; }

    juce::String getHelp() const override { return editor.getDescription(); }

  private:
    class ChatTextInterface final : public juce::AccessibilityTextInterface {
    public:
      explicit ChatTextInterface(ChatHistoryEditor &e) : editor(e) {}

      bool isDisplayingProtectedText() const override {
        return editor.getPasswordCharacter() != 0;
      }
      bool isReadOnly() const override { return true; }

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

      void setText(const juce::String &) override {}

      int getTextInsertionOffset() const override {
        return editor.getCaretPosition();
      }

      juce::RectangleList<int>
      getTextBounds(juce::Range<int> textRange) const override {
        auto localRects = editor.getTextBounds(textRange);
        juce::RectangleList<int> globalRects;
        for (const auto &r : localRects)
          globalRects.add(editor.localAreaToGlobal(r));
        return globalRects;
      }

      int getOffsetAtPoint(juce::Point<int> point) const override {
        return editor.getTextIndexAt(editor.getLocalPoint(nullptr, point));
      }

    private:
      ChatHistoryEditor &editor;
      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatTextInterface)
    };

    ChatHistoryEditor &editor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatAccessibilityHandler)
  };

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatHistoryEditor)
};
