#pragma once

#include <JuceHeader.h>

#include "ChatEntry.h"

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

  // Told when the field gains or loses focus, so the window can show where
  // keys are going and say when they stop coming here.
  std::function<void(bool focused, FocusChangeType cause)> onFocusChange;

  // Up and Down walk what was sent; Tab completes a name (ChatEntry). Text set
  // from code is not reliably read out by a screen reader, so what the field
  // now says is handed to `onSpeak` as well.
  std::function<juce::StringArray()> namesForCompletion;
  std::function<void(const juce::String &)> onSpeak;

  void rememberSent(const juce::String &text) { history.add(text); }

  bool keyPressed(const juce::KeyPress &key) override {
    const auto mods = key.getModifiers();
    const bool plain =
        !mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown();
    if (plain && !mods.isShiftDown()) {
      const int code = key.getKeyCode();
      if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        const auto recalled = code == juce::KeyPress::upKey
                                  ? history.older(getText())
                                  : history.newer();
        if (recalled.has_value()) {
          setText(*recalled, false);
          moveCaretToEnd();
          speak(recalled->isEmpty() ? juce::String("Empty") : *recalled);
          return true;
        }
      } else if (code == juce::KeyPress::tabKey && namesForCompletion) {
        // Only when there is a name to complete: otherwise Tab moves focus as
        // it always does, so a keyboard user is never trapped in the field.
        const auto c = ChatEntry::completeName(getText(), getCaretPosition(),
                                               namesForCompletion());
        if (c.applies) {
          setText(c.text, false);
          setCaretPosition(c.caret);
          speak(c.candidates.size() == 1
                    ? c.candidates[0]
                    : c.candidates.joinIntoString(", ", 0, 8));
          return true;
        }
      }
    }
    return juce::TextEditor::keyPressed(key);
  }

  void focusGained(FocusChangeType cause) override {
    juce::TextEditor::focusGained(cause);
    if (onFocusChange)
      onFocusChange(true, cause);
  }

  void focusLost(FocusChangeType cause) override {
    juce::TextEditor::focusLost(cause);
    repaint(); // the focused outline
    if (onFocusChange)
      onFocusChange(false, cause);
  }

private:
  ChatEntry::History history;

  void speak(const juce::String &s) const {
    if (onSpeak)
      onSpeak(s);
  }

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
