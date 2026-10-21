#include "DrawerHost.h"

#include "CdlLookAndFeel.h"

DrawerHost::DrawerHost() {
  setComponentID("DrawerHost");
  close_.setTitle("Close drawer");
  close_.setTooltip("Close the drawer and give the space back");
  close_.onClick = [this] { close(); };
  addChildComponent(close_);
  setVisible(false);
}

const DrawerHost::Entry* DrawerHost::find(const juce::String& id) const {
  for (const auto& e : entries_)
    if (e.id == id) return &e;
  return nullptr;
}

void DrawerHost::registerDrawer(const juce::String& id, const juce::String& title, juce::Component& content, int preferredWidth) {
  for (auto& e : entries_)
    if (e.id == id) {
      if (e.content != &content) removeChildComponent(e.content);
      e = {id, title, &content, preferredWidth};
      addChildComponent(content);
      return;
    }
  entries_.push_back({id, title, &content, preferredWidth});
  addChildComponent(content);
  content.setVisible(false);
}

int DrawerHost::openPreferredWidth() const {
  const auto* e = find(openId_);
  return e ? e->preferredWidth : 0;
}

bool DrawerHost::open(const juce::String& id) {
  const auto* e = find(id);
  if (!e) return false;
  for (const auto& other : entries_) other.content->setVisible(false);
  openId_ = id;
  e->content->setVisible(true);
  close_.setVisible(true);
  setVisible(true);
  resized();
  repaint();
  if (onChanged) onChanged();
  return true;
}

void DrawerHost::close() {
  if (openId_.isEmpty()) return;
  for (const auto& e : entries_) e.content->setVisible(false);
  openId_ = {};
  close_.setVisible(false);
  setVisible(false);
  if (onChanged) onChanged();
}

bool DrawerHost::toggle(const juce::String& id) {
  if (openId_ == id) {
    close();
    return true;
  }
  return open(id);
}

void DrawerHost::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  auto r = getLocalBounds().toFloat().reduced(0.5f);
  g.setColour(p.panel);
  g.fillRoundedRectangle(r, 4.0f);
  g.setColour(p.edgeHi);
  g.drawRoundedRectangle(r, 4.0f, 1.0f);
  if (const auto* e = find(openId_)) cdl::paintSectionLabel(g, e->title, getLocalBounds().removeFromTop(32).withTrimmedLeft(10).withTrimmedRight(100));
}

void DrawerHost::resized() {
  auto area = getLocalBounds().reduced(1);
  auto head = area.removeFromTop(32);
  close_.setBounds(head.removeFromRight(100).reduced(6, 4));
  if (const auto* e = find(openId_)) e->content->setBounds(area.reduced(4, 2));
}
