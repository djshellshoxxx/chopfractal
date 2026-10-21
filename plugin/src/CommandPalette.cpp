#include "CommandPalette.h"

#include <algorithm>
#include <chopfractal/plugin_ui_adapter/command_match.hpp>

#include "CdlLookAndFeel.h"
#include "PluginEditor.h"

namespace cf = chopfractal;

namespace {
constexpr int kVisibleRows = 8;
constexpr int kMaxRows = 120;
std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
int categoryOrder(const std::string& c) { return c == "Action" ? 0 : c == "Parameter" ? 1 : c == "Preset" ? 2 : 3; }
juce::String sentence(const std::string& reason) {
  juce::String t(reason);
  return t.substring(0, 1).toUpperCase() + t.substring(1) + (t.endsWithChar('.') ? "" : ".");
}
}  // namespace

CommandPalette::CommandPalette(ChopFractalEditor& ed, CommandRegistry& registry, QuickStartTracker& tracker) : ed_(ed), registry_(registry), tracker_(tracker) {
  setComponentID("CommandPalette");
  setTitle("Command palette");
  setWantsKeyboardFocus(false);
  search_.setTitle("Command search");
  search_.setTooltip("Type a command, parameter or preset name");
  search_.setTextToShowWhenEmpty("Type a command or parameter...", cdl::pal().inkMuted);
  search_.setEscapeAndReturnKeysConsumed(false);
  search_.setSelectAllWhenFocused(true);
  search_.addListener(this);
  search_.addKeyListener(this);
  addAndMakeVisible(search_);
  list_.setTitle("Command results");
  list_.setTooltip("Matching commands, Up and Down choose, Enter runs");
  list_.setRowHeight(32);
  list_.setMouseMoveSelectsRows(false);
  list_.setWantsKeyboardFocus(false);
  addAndMakeVisible(list_);
  setVisible(false);
}

CommandPalette::~CommandPalette() = default;

void CommandPalette::open() {
  registry_.refreshPresets();
  std::vector<std::string> ids;
  for (const auto& c : registry_.all()) ids.push_back(c.id);
  tracker_.keepRecents(ids);
  if (auto* parent = getParentComponent()) {
    setBounds(parent->getLocalBounds());
    toFront(false);
  }
  returnFocusTo_ = juce::Component::getCurrentlyFocusedComponent();
  setVisible(true);
  resized();
  search_.setText({}, juce::dontSendNotification);
  rebuild();
  search_.grabKeyboardFocus();
}

void CommandPalette::close() {
  if (!isVisible()) return;
  setVisible(false);
  if (returnFocusTo_ != nullptr && returnFocusTo_->isShowing()) returnFocusTo_->grabKeyboardFocus();
  else ed_.grabKeyboardFocus();
}

void CommandPalette::resized() {
  const int w = std::min(520, std::max(200, getWidth() - 40));
  const int h = std::min(360, std::max(160, getHeight() - 40));
  panel_ = juce::Rectangle<int>(w, h).withCentre({getWidth() / 2, std::max(h / 2 + 24, getHeight() / 3)});
  panel_ = panel_.constrainedWithin(getLocalBounds().reduced(8));
  auto r = panel_.reduced(10);
  search_.setBounds(r.removeFromTop(30));
  r.removeFromTop(6);
  r.removeFromBottom(24);  // the footer line, painted
  list_.setBounds(r);
}

void CommandPalette::mouseDown(const juce::MouseEvent& e) {
  if (!panel_.contains(e.getPosition())) close();
}

bool CommandPalette::keyPressed(const juce::KeyPress& k) {
  keyPressed(k, nullptr);
  return true;  // modal: nothing falls through to the editor's shortcuts while the palette is up
}

bool CommandPalette::keyPressed(const juce::KeyPress& k, juce::Component*) {
  if (k == juce::KeyPress::escapeKey) {
    close();
    return true;
  }
  if (k == juce::KeyPress::downKey) return moveSelection(1) || true;
  if (k == juce::KeyPress::upKey) return moveSelection(-1) || true;
  if (k == juce::KeyPress::returnKey) {
    runSelected();
    return true;
  }
  if (k == juce::KeyPress::pageDownKey) return moveSelection(kVisibleRows) || true;
  if (k == juce::KeyPress::pageUpKey) return moveSelection(-kVisibleRows) || true;
  const auto m = k.getModifiers();
  if ((m.isCommandDown() || m.isCtrlDown()) && (k.getKeyCode() == 'K' || k.getKeyCode() == 'k')) {
    close();
    return true;
  }
  return false;
}

void CommandPalette::setQuery(const juce::String& q) {
  search_.setText(q, juce::dontSendNotification);
  rebuild();
}

int CommandPalette::firstSelectable(int from, int step) const {
  for (int i = from; i >= 0 && i < static_cast<int>(rows_.size()); i += step)
    if (!rows_[static_cast<std::size_t>(i)].header) return i;
  return -1;
}

bool CommandPalette::moveSelection(int delta) {
  if (rows_.empty()) return false;
  int target = selected_;
  const int step = delta < 0 ? -1 : 1;
  for (int n = std::abs(delta); n > 0; --n) {
    const int next = firstSelectable(target + step, step);
    if (next < 0) break;
    target = next;
  }
  if (target == selected_ || target < 0) return false;
  select(target);
  return true;
}

void CommandPalette::select(int row) {
  selected_ = row;
  list_.selectRow(row, true, true);
  defaultFooter();
  list_.repaint();
}

void CommandPalette::setFooter(const juce::String& t, bool error) {
  footerText_ = t;
  footerError_ = error;
  repaint();
}

void CommandPalette::defaultFooter() {
  if (argCmd_ != nullptr) {
    const auto& c = *argCmd_;
    if (c.kind == CommandKind::Number)
      setFooter("Value (" + juce::String(registry_.rangeText(c)) + ", now " + juce::String(registry_.valueText(c)) + "): type a number, Enter sets it", false);
    else
      setFooter("Choose a value for " + juce::String(c.title) + ", Enter sets it", false);
    return;
  }
  if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size()) && !rows_[static_cast<std::size_t>(selected_)].reason.empty()) {
    const auto& r = rows_[static_cast<std::size_t>(selected_)];
    setFooter(sentence(r.reason), false);
    return;
  }
  if (rows_.empty() && search_.getText().trim().isNotEmpty()) {
    setFooter("No command matches \"" + search_.getText().trim() + "\".", false);
    return;
  }
  setFooter("Enter run, Up/Down choose, Esc close", false);
}

void CommandPalette::rebuild() {
  if (rebuilding_) return;
  rebuilding_ = true;
  rows_.clear();
  argCmd_ = nullptr;
  argText_.clear();
  const std::string q = search_.getText().toStdString();
  const std::string lq = lower(q);
  // Argument mode: the query starts with a parameter's name and a space ("density 0.6", "break profile jun").
  const Command* best = nullptr;
  for (const auto& c : registry_.all()) {
    if (c.param == nullptr) continue;
    const std::string t = lower(c.title);
    if (lq.size() > t.size() && lq.compare(0, t.size(), t) == 0 && lq[t.size()] == ' ' && (best == nullptr || t.size() > best->title.size())) best = &c;
  }
  if (best != nullptr) {
    argCmd_ = best;
    std::string rest = q.substr(best->title.size());
    const auto b = rest.find_first_not_of(' ');
    argText_ = b == std::string::npos ? std::string() : rest.substr(b);
    fillArgument(*best, argText_);
  } else if (lq.find_first_not_of(' ') == std::string::npos) {
    fillEmptyQuery();
  } else {
    fillSearch(q);
  }
  list_.updateContent();
  selected_ = firstSelectable(0, 1);
  if (selected_ >= 0) list_.selectRow(selected_, true, true);
  else list_.deselectAllRows();
  defaultFooter();
  list_.repaint();
  rebuilding_ = false;
}

void CommandPalette::fillArgument(const Command& c, const std::string& arg) {
  Row r;
  r.cmd = &c;
  if (c.kind == CommandKind::Number) {
    r.title = c.title;
    r.right = "now " + registry_.valueText(c);
    rows_.push_back(r);
    if (!arg.empty()) {
      double v = 0;
      std::string err;
      if (!cf::ui::parseNumberArg(arg, c.param->minValue, c.param->maxValue, v, err)) setFooter(juce::String(cf::ui::rangeErrorText(c.param->minValue, c.param->maxValue)), true);
    }
    return;
  }
  const auto labels = registry_.choiceLabels(c);
  const std::string current = registry_.valueText(c);
  std::vector<std::string_view> views(labels.begin(), labels.end());
  auto ranked = cf::ui::fuzzyRank(arg, views, 100);
  if (arg.empty())  // no filter: the entries in their own order
    for (std::size_t i = 0; i < ranked.size(); ++i) ranked[i] = {static_cast<int>(i), 0};
  for (const auto& [idx, score] : ranked) {
    (void)score;
    Row row;
    row.cmd = &c;
    row.choice = idx;
    row.title = c.title + ": " + labels[static_cast<std::size_t>(idx)];
    row.right = labels[static_cast<std::size_t>(idx)] == current ? "now" : "";
    row.hits = cf::ui::fuzzyScore(arg, labels[static_cast<std::size_t>(idx)]).positions;
    for (auto& h : row.hits) h = static_cast<std::uint8_t>(h + c.title.size() + 2);
    rows_.push_back(std::move(row));
  }
}

void CommandPalette::fillEmptyQuery() {
  auto push = [this](const Command& c, bool recent) {
    for (const auto& r : rows_)
      if (r.cmd == &c) return;
    Row row;
    row.cmd = &c;
    row.title = c.title;
    row.right = registry_.valueText(c);
    row.reason = registry_.disabledReason(c);
    (void)recent;
    rows_.push_back(std::move(row));
  };
  std::vector<const Command*> recents, suggested;
  for (const auto& id : tracker_.recents())
    if (const auto* c = registry_.find(id)) recents.push_back(c);
  const auto& v = ed_.model().view();
  std::vector<std::string> sug;
  if (!v.hasSource) sug = {"action.load"};
  else if (v.chops.empty()) sug = {"action.detect", "action.load"};
  else if (!v.hasPattern) sug = {"action.generate", "action.detect", "action.load"};
  else sug = {"action.mutate", "action.play", "action.exportwav", "action.generate"};
  for (const auto& id : sug)
    if (const auto* c = registry_.find(id)) suggested.push_back(c);
  auto header = [this](const char* t) {
    Row h;
    h.header = true;
    h.title = t;
    rows_.push_back(std::move(h));
  };
  if (!recents.empty()) {
    header("Recent");
    for (const auto* c : recents) push(*c, true);
  }
  header("Suggested");
  for (const auto* c : suggested) push(*c, false);
  if (rows_.size() > static_cast<std::size_t>(kMaxRows)) rows_.resize(kMaxRows);
}

void CommandPalette::fillSearch(const std::string& q) {
  struct Hit {
    int score;
    const Command* cmd;
    std::vector<std::uint8_t> positions;
  };
  std::vector<Hit> hits;
  for (const auto& c : registry_.all()) {
    auto m = cf::ui::fuzzyScore(q, c.title);
    int best = m.matched ? m.score : -100000;
    std::vector<std::uint8_t> pos = m.matched ? m.positions : std::vector<std::uint8_t>{};
    for (const auto& kw : c.keywords) {
      const auto km = cf::ui::fuzzyScore(q, kw);
      if (km.matched && km.score - 4 > best) {
        best = km.score - 4;
        pos.clear();
      }
    }
    if (best > -100000) hits.push_back({best, &c, std::move(pos)});
  }
  std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
    if (a.score != b.score) return a.score > b.score;
    const int ca = categoryOrder(a.cmd->category), cb = categoryOrder(b.cmd->category);
    if (ca != cb) return ca < cb;
    return a.cmd->title < b.cmd->title;
  });
  for (const auto& h : hits) {
    if (rows_.size() >= static_cast<std::size_t>(kMaxRows)) break;
    Row r;
    r.cmd = h.cmd;
    r.title = h.cmd->title;
    r.right = registry_.valueText(*h.cmd);
    r.reason = registry_.disabledReason(*h.cmd);
    r.hits = h.positions;
    rows_.push_back(std::move(r));
  }
}

void CommandPalette::remember(const Command& c) { tracker_.pushRecent(c.id); }

bool CommandPalette::runSelected() {
  if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) {
    if (argCmd_ == nullptr) return false;
  }
  if (argCmd_ != nullptr) {
    const Command& c = *argCmd_;
    if (c.kind == CommandKind::Number) {
      std::string err;
      if (!registry_.setNumber(c, argText_, err)) {
        setFooter(juce::String(err), true);
        return false;
      }
      remember(c);
      close();
      return true;
    }
    if (selected_ < 0) return false;
    const Row& r = rows_[static_cast<std::size_t>(selected_)];
    if (!registry_.setChoice(c, r.choice)) return false;
    remember(c);
    close();
    return true;
  }
  const Row row = rows_[static_cast<std::size_t>(selected_)];
  const Command& c = *row.cmd;
  if (!row.reason.empty()) {
    setFooter(juce::String(c.title) + ": " + juce::String(row.reason) + ".", true);
    return false;
  }
  if (c.kind == CommandKind::Number || c.kind == CommandKind::Choice) {
    search_.setText(juce::String(c.title) + " ", juce::dontSendNotification);
    search_.moveCaretToEnd();
    rebuild();
    return true;
  }
  remember(c);
  close();  // actions that open a dialog or a drawer start from a closed palette
  std::string err;
  registry_.runCommand(c, err);
  return true;
}

juce::String CommandPalette::getNameForRow(int row) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
  const auto& r = rows_[static_cast<std::size_t>(row)];
  if (r.header) return juce::String(r.title);
  juce::String t = juce::String(r.cmd->category) + " " + juce::String(r.title);
  if (!r.right.empty()) t += ", " + juce::String(r.right);
  if (!r.reason.empty()) t += ", unavailable, " + juce::String(r.reason);
  return t;
}

juce::String CommandPalette::getTooltipForRow(int row) { return getNameForRow(row); }

void CommandPalette::listBoxItemClicked(int row, const juce::MouseEvent&) {
  if (row >= 0 && row < static_cast<int>(rows_.size()) && !rows_[static_cast<std::size_t>(row)].header) select(row);
}

void CommandPalette::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
  if (row >= 0 && row < static_cast<int>(rows_.size()) && !rows_[static_cast<std::size_t>(row)].header) {
    select(row);
    runSelected();
  }
}

void CommandPalette::paint(juce::Graphics& g) {
  const auto& p = cdl::pal();
  const float s = cdl::uiScale();
  g.fillAll(p.bg.withAlpha(0.55f));
  g.setColour(p.panel);
  g.fillRect(panel_);
  g.setColour(p.edgeHi);
  g.drawRect(panel_, 1);
  g.setColour(p.spray);
  g.fillRect(panel_.getX(), panel_.getY(), 3, panel_.getHeight());
  auto foot = panel_.reduced(14, 0).removeFromBottom(34).withTrimmedBottom(6);
  g.setColour(footerError_ ? p.clip : p.inkMuted);
  g.setFont(cdl::font(cdl::FontRole::Hint, s));
  g.drawText(footerText_, foot, juce::Justification::centredLeft, true);
}

void CommandPalette::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  const auto& r = rows_[static_cast<std::size_t>(row)];
  const auto& p = cdl::pal();
  const float s = cdl::uiScale();
  if (r.header) {
    g.setColour(p.inkMuted);
    g.setFont(cdl::font(cdl::FontRole::Label, s));
    g.drawText(juce::String(r.title).toUpperCase(), 8, 0, w - 16, h, juce::Justification::bottomLeft, false);
    return;
  }
  if (selected) {
    g.setColour(p.spray.withAlpha(0.20f));
    g.fillRect(0, 0, w, h);
    g.setColour(p.spray);
    g.fillRect(0, 0, 2, h);
  }
  const bool disabled = !r.reason.empty();
  const juce::Colour ink = disabled ? p.inkMuted : p.ink;
  const auto small = cdl::font(cdl::FontRole::Hint, s);
  g.setFont(small);
  g.setColour(p.inkMuted);
  const int catW = static_cast<int>(70 * s);
  g.drawText(juce::String(r.cmd->category), 8, 0, catW, h, juce::Justification::centredLeft, true);
  int rightW = r.right.empty() ? 0 : std::min(static_cast<int>(w * 0.32f), juce::GlyphArrangement::getStringWidthInt(small, juce::String(r.right)) + 12);
  juce::String right(r.right);
  if (disabled) {
    right = juce::String(r.reason);
    rightW = std::min(static_cast<int>(w * 0.45f), juce::GlyphArrangement::getStringWidthInt(small, right) + 12);
  }
  const int x0 = 8 + catW;
  const int nameW = w - x0 - rightW - 8;
  // Matched characters in bold: draw the title run by run.
  const auto body = cdl::font(cdl::FontRole::Body, s);
  const auto bold = body.boldened();
  float x = static_cast<float>(x0);
  const float limit = static_cast<float>(x0 + nameW);
  const juce::String title(r.title);
  const bool needsEllipsis = juce::GlyphArrangement::getStringWidth(body, title) > static_cast<float>(nameW);
  if (r.hits.empty() || needsEllipsis) {
    g.setFont(body);
    g.setColour(ink);
    g.drawText(title, x0, 0, nameW, h, juce::Justification::centredLeft, true);
  } else {
    for (int i = 0; i < title.length(); ++i) {
      const bool hit = std::find(r.hits.begin(), r.hits.end(), static_cast<std::uint8_t>(i)) != r.hits.end();
      const auto& f = hit ? bold : body;
      const juce::String ch = title.substring(i, i + 1);
      const float cw = juce::GlyphArrangement::getStringWidth(f, ch);
      if (x + cw > limit) break;
      g.setFont(f);
      g.setColour(hit ? p.sprayHi : ink);
      g.drawText(ch, juce::Rectangle<float>(x, 0.0f, cw + 1.0f, static_cast<float>(h)), juce::Justification::centredLeft, false);
      x += cw;
    }
  }
  if (!right.isEmpty()) {
    g.setFont(small);
    g.setColour(disabled ? p.warn : p.inkMuted);
    g.drawText(right, w - rightW - 8, 0, rightW, h, juce::Justification::centredRight, true);
  }
}
