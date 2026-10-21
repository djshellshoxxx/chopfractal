#pragma once
// The right-hand drawer region of the shell. A drawer is any component registered under an id; at most one is open at a
// time. The host draws the title bar and the close button and gives the open component the rest of the area. The
// component stays owned by whoever registered it (it is only parented here while open).
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

class DrawerHost : public juce::Component {
 public:
  struct Entry {
    juce::String id, title;
    juce::Component* content = nullptr;
    int preferredWidth = 320;  // logical pixels at scale 1
  };
  DrawerHost();

  // Registers (or replaces) a drawer. The id is also the component ID the toggle button in the top bar gets.
  void registerDrawer(const juce::String& id, const juce::String& title, juce::Component& content, int preferredWidth = 320);
  const std::vector<Entry>& drawers() const { return entries_; }
  bool open(const juce::String& id);  // false when the id is unknown
  void close();
  bool toggle(const juce::String& id);
  juce::String openId() const { return openId_; }
  bool isOpen() const { return openId_.isNotEmpty(); }
  int openPreferredWidth() const;  // 0 when closed

  std::function<void()> onChanged;  // fired after open / close; the editor re-runs its layout

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  const Entry* find(const juce::String& id) const;

  std::vector<Entry> entries_;
  juce::String openId_;
  juce::TextButton close_{"Close drawer"};
};
