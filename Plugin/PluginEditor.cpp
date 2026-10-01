/**
 * @file PluginEditor.cpp
 * @brief The plugin's panel.
 */
#include "PluginEditor.h"
#include "EditorMixer.h"
#include "EditorOrrery.h"
#include "EditorPerform.h"
#include "EditorGestures.h"
#include "EditorStyle.h"
#include "EditorTheme.h"
#include "eph/compose/Composer.h"
#include "eph/Presets.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdlib>

using namespace eph;

namespace {

const juce::Colour kBack = ephui::colour::bg, kPanel = ephui::colour::panel, kInk = ephui::colour::ink, kDim = ephui::colour::dim,
                   kAccent = ephui::colour::amber;

/**
 * @brief Colour of a section by the first word of its marker: the families' colours, darkened -- the spaces (the
 *        atmosphere, the bridges, the fade) in the room's blue, the machine's stages from the sources' amber through the
 *        filter's copper at the peak, the breakdown in the teal of what moves by itself.
 */
juce::Colour sectionColour(const juce::String& name)
{
    using ephui::Family;
    const juce::String n = name.fromLastOccurrenceOf(": ", false, false);
    auto tone = [](Family f, float k) { return ephui::familyColour(f).interpolatedWith(ephui::colour::bg, k); };
    if (n.startsWith("Atmo") || n.startsWith("Ausklang") || n.startsWith("Zwischenspiel")) return tone(Family::Space, 0.72f);
    if (n.startsWith("Bruecke")) return tone(Family::Space, 0.64f);
    if (n.startsWith("Einsatz")) return tone(Family::Source, 0.78f);
    if (n.startsWith("Aufbau")) return tone(Family::Source, 0.68f);
    if (n.startsWith("Lead")) return tone(Family::Source, 0.56f);
    if (n.startsWith("Hoehepunkt")) return tone(Family::Filter, 0.52f);
    if (n.startsWith("Abbau")) return tone(Family::Motion, 0.66f);
    return tone(Family::Space, 0.8f);
}

} // namespace

// ---------------------------------------------------------------------------------------------------

ParamPage::ParamPage(EphemerisProcessor& p, std::vector<std::pair<Module, int>> groups, int instances)
    : proc_(p), groups_(std::move(groups)), instances_(instances)
{
    if (instances_ > 1) {
        for (int i = 0; i < instances_; ++i) instance_.addItem(juce::String(i + 1), i + 1);
        instance_.setSelectedId(1, juce::dontSendNotification);
        instance_.onChange = [this] { build(); resized(); };
        addAndMakeVisible(instance_);
    }
    // The synth's factory presets, in their groups (a submenu each), where the page is a synth's.
    const Module m = groups_.empty() ? Module::Count : groups_.front().first;
    const std::vector<SoundPreset>& list = factoryPresets(m);
    if (!list.empty()) {
        presetModule_ = m;
        presetCount_ = static_cast<int>(list.size());
        fillPresets();
        preset_.onChange = [this] {
            const int id = preset_.getSelectedId();
            if (id > 5000 && id - 5001 < static_cast<int>(user_.size())) {
                presetIndex_ = -1;
                const int one = instances_ > 1 ? instance_.getSelectedId() - 1 : 0;
                for (int i = 0; i < instances_; ++i)
                    if (i == one || allRows_.getToggleState())
                        proc_.applyPresetValues(presetModule_, i, user_[static_cast<size_t>(id - 5001)]);
            } else if (id > 0) {
                choosePreset(id - 1);
            }
        };
        // Save...: the synth's knobs as they stand, under a name, into the user's presets.
        save_.onClick = [this] {
            nameDialog_ = std::make_unique<juce::AlertWindow>("Save preset", "A name for the sound as it is:", juce::MessageBoxIconType::NoIcon);
            nameDialog_->addTextEditor("name", "", "Name");
            nameDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            nameDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            nameDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
                if (result == 1 && nameDialog_ != nullptr) {
                    const juce::String name = nameDialog_->getTextEditorContents("name");
                    if (proc_.saveUserPreset(presetModule_, instances_ > 1 ? instance_.getSelectedId() - 1 : 0, name)) fillPresets();
                }
                nameDialog_.reset();
            }), false);
        };
        addAndMakeVisible(save_);
        composed_.setColour(juce::Label::textColourId, kDim);
        composed_.setTooltip("The preset the composer chose for this synth in the piece that plays (Composer's Sounds on the "
                             "Master page; reroll sounds draws others)");
        addAndMakeVisible(composed_);
        startTimerHz(2);
        prev_.onClick = [this] { choosePreset(((presetIndex_ < 0 ? 0 : presetIndex_) - 1 + presetCount_) % presetCount_); };
        next_.onClick = [this] { choosePreset((presetIndex_ + 1) % presetCount_); };
        prev_.setTooltip("the preset before");
        next_.setTooltip("the next preset");
        addAndMakeVisible(preset_);
        addAndMakeVisible(prev_);
        addAndMakeVisible(next_);
        if (instances_ > 1) {
            allRows_.setTooltip("a preset goes to every row, not only the one chosen");
            addAndMakeVisible(allRows_);
        }
    } else if (m == Module::Echo || m == Module::Reverb) {
        // The effect pages: the user's own presets of the whole page (all its rooms), saved and recalled by full key.
        pagePresets_ = true;
        presetCount_ = 0;
        fillPagePresets();
        preset_.onChange = [this] {
            const int id = preset_.getSelectedId();
            if (id > 0 && id - 1 < static_cast<int>(pageUser_.size())) proc_.applyKeyText(pageUser_[static_cast<size_t>(id - 1)].second);
        };
        save_.onClick = [this] {
            nameDialog_ = std::make_unique<juce::AlertWindow>("Save preset", "A name for the page as it is:", juce::MessageBoxIconType::NoIcon);
            nameDialog_->addTextEditor("name", "", "Name");
            nameDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            nameDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            nameDialog_->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
                if (result == 1 && nameDialog_ != nullptr) {
                    const juce::String name = juce::File::createLegalFileName(nameDialog_->getTextEditorContents("name").trim());
                    const ParamStore& st = proc_.store();
                    juce::String text;
                    for (const auto& g : groups_)
                        for (int k = 0; k < ParamStore::moduleCount(g.first); ++k) {
                            const int id = st.id(g.first, g.second, k);
                            text << juce::String(st.key(id)) << "=" << juce::String(st.get(id), 6) << "\n";
                        }
                    if (name.isNotEmpty() && pageFolder().createDirectory() && pageFolder().getChildFile(name + ".txt").replaceWithText(text))
                        fillPagePresets();
                }
                nameDialog_.reset();
            }), false);
        };
        addAndMakeVisible(preset_);
        addAndMakeVisible(save_);
    }
    build();
}

juce::File ParamPage::pageFolder() const
{
    const juce::String page = groups_.front().first == Module::Echo ? "Echo + Spring" : "Hall";
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Ephemeris").getChildFile("Presets").getChildFile(page);
}

void ParamPage::fillPagePresets()
{
    preset_.clear(juce::dontSendNotification);
    pageUser_.clear();
    juce::Array<juce::File> files = pageFolder().findChildFiles(juce::File::findFiles, false, "*.txt");
    files.sort();
    for (const juce::File& f : files) {
        pageUser_.push_back({ f.getFileNameWithoutExtension(), f.loadFileAsString() });
        preset_.addItem(f.getFileNameWithoutExtension(), static_cast<int>(pageUser_.size()));
    }
    preset_.setTextWhenNothingSelected(pageUser_.empty() ? juce::String("your presets (Save...)") : juce::String(pageUser_.size()) + " of yours");
}

void ParamPage::fillPresets()
{
    const std::vector<SoundPreset>& list = factoryPresets(presetModule_);
    preset_.clear(juce::dontSendNotification);
    std::vector<std::string> order;
    std::map<std::string, juce::PopupMenu> menus;
    for (size_t i = 0; i < list.size(); ++i) {
        if (menus.find(list[i].group) == menus.end()) order.push_back(list[i].group);
        menus[list[i].group].addItem(static_cast<int>(i) + 1, list[i].name);
    }
    for (const std::string& g : order) preset_.getRootMenu()->addSubMenu(g, menus[g]);
    user_ = proc_.userPresets(presetModule_);
    if (!user_.empty()) {
        juce::PopupMenu mine;
        for (size_t i = 0; i < user_.size(); ++i) mine.addItem(5001 + static_cast<int>(i), user_[i].name);
        preset_.getRootMenu()->addSubMenu("User", mine);
    }
    preset_.setTextWhenNothingSelected(juce::String(presetCount_) + " presets" + (user_.empty() ? "" : " + " + juce::String(user_.size()) + " of yours"));
}

void ParamPage::timerCallback()
{
    const int inst = instances_ > 1 ? instance_.getSelectedId() - 1 : 0;
    const int index = proc_.composedPreset(presetModule_, inst);
    if (index == shown_) return;
    shown_ = index;
    const std::vector<SoundPreset>& list = factoryPresets(presetModule_);
    composed_.setText(index >= 0 && index < static_cast<int>(list.size())
                          ? juce::String("this piece: ") + list[static_cast<size_t>(index)].name + " (" + list[static_cast<size_t>(index)].group + ")"
                          : juce::String(), juce::dontSendNotification);
}

void ParamPage::choosePreset(int index)
{
    if (presetCount_ == 0 || index < 0 || index >= presetCount_) return;
    presetIndex_ = index;
    preset_.setSelectedId(index + 1, juce::dontSendNotification);
    const int one = instances_ > 1 ? instance_.getSelectedId() - 1 : 0;
    for (int i = 0; i < instances_; ++i)
        if (i == one || allRows_.getToggleState()) proc_.applyPreset(presetModule_, i, index);
}

ModSlotControl::ModSlotControl(int number, juce::Colour colour) : number_(number)
{
    for (juce::ComboBox* c : { &source, &target }) {
        c->setColour(juce::ComboBox::arrowColourId, colour);
        addAndMakeVisible(*c);
    }
    amount.setSliderStyle(juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18);
    amount.setColour(juce::Slider::trackColourId, colour);
    amount.setColour(juce::Slider::thumbColourId, ephui::colour::ink);
    amount.setDoubleClickReturnValue(true, 0.0);
    amount.setTooltip("Amount (double-click: none)");
    addAndMakeVisible(amount);
}

void ModSlotControl::resized()
{
    auto r = getLocalBounds().reduced(2, 0);
    r.removeFromLeft(18);   // the slot's number
    const int w = r.getWidth();
    source.setBounds(r.removeFromLeft(w * 30 / 100).withSizeKeepingCentre(w * 30 / 100 - 4, 24));
    target.setBounds(r.removeFromLeft(w * 34 / 100).withSizeKeepingCentre(w * 34 / 100 - 4, 24));
    amount.setBounds(r.withSizeKeepingCentre(r.getWidth(), 24));
}

void ModSlotControl::paint(juce::Graphics& g)
{
    g.setColour(ephui::colour::dim);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(juce::String(number_), 2, 0, 16, getHeight(), juce::Justification::centred);
}

void ParamPage::build()
{
    sliders_.clear();
    combos_.clear();
    buttons_.clear();
    controls_.clear();
    labels_.clear();
    boxes_.clear();
    const int inst = instances_ > 1 ? instance_.getSelectedId() - 1 : 0;
    ParamStore& s = proc_.store();
    // A control for parameter id, in a box of the given colour: a knob, a menu or a switch by its descriptor.
    // A control's name in its box, without the box's title in front ("Rate" in "LFO 1", "Attack" in "Amp Envelope").
    auto shortName = [](const juce::String& name, const juce::String& title) {
        if (name.startsWith(title + " ")) return name.substring(title.length() + 1);
        const juce::String first = title.upToFirstOccurrenceOf(" ", false, false);
        if (first != title && name.startsWith(first + " ")) return name.substring(first.length() + 1);
        return name;
    };
    auto make = [&](int id, juce::Colour colour, bool big, const juce::String& title = {}, bool narrow = false) {
        StoreParameter* param = proc_.parameter(id);
        if (param == nullptr) return Cell{};
        const ParamDesc& d = s.desc(id);
        auto* label = labels_.add(new juce::Label({}, title.isEmpty() ? juce::String(d.name) : shortName(d.name, title)));
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, big ? ephui::colour::ink : ephui::colour::dim);
        label->setFont(juce::FontOptions(big ? 13.0f : 12.0f));
        label->setMinimumHorizontalScale(0.75f);
        addAndMakeVisible(label);
        Cell cell;
        cell.big = big;
        if (d.curve == Curve::Choice && d.choices != nullptr) {
            auto* box = new juce::ComboBox();
            for (int c = 0; c <= static_cast<int>(d.maxValue); ++c) box->addItem(d.choices[c], c + 1);
            box->setColour(juce::ComboBox::arrowColourId, colour);
            controls_.add(box);
            combos_.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(*param, *box));
            cell.kind = 1;
            cell.narrow = narrow;
        } else if (d.curve == Curve::Toggle) {
            auto* b = new juce::ToggleButton();
            b->setColour(juce::ToggleButton::tickColourId, colour);
            controls_.add(b);
            buttons_.push_back(std::make_unique<juce::ButtonParameterAttachment>(*param, *b));
            cell.kind = 2;
        } else {
            auto* sl = new juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, big ? 96 : 70, 16);
            sl->setColour(juce::Slider::rotarySliderFillColourId, colour);
            sl->setTextValueSuffix(d.unit[0] != 0 ? juce::String(" ") + d.unit : juce::String());
            sl->setTooltip(juce::String(d.name) + (d.unit[0] != 0 ? juce::String(" (") + d.unit + ")" : juce::String()));
            controls_.add(sl);
            sliders_.push_back(std::make_unique<juce::SliderParameterAttachment>(*param, *sl));
        }
        addAndMakeVisible(controls_.getLast());
        cell.control = controls_.size() - 1;
        return cell;
    };
    for (const auto& g : groups_) {
        const int count = ParamStore::moduleCount(g.first);
        const int instance = instances_ > 1 ? inst : g.second;
        // The module's keys (the part after the dot: "cutoff" of "voice3.cutoff").
        std::vector<std::string> keys(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const std::string& k = s.key(s.id(g.first, instance, i));
            keys[static_cast<size_t>(i)] = k.substr(k.find('.') + 1);
        }
        std::vector<bool> placed(static_cast<size_t>(count), false);
        auto indexOf = [&](const std::string& name) {
            for (int i = 0; i < count; ++i) if (!placed[static_cast<size_t>(i)] && keys[static_cast<size_t>(i)] == name) return i;
            return -1;
        };
        // A slot of the modulation matrix ("@mod3": mod3_src, mod3_dst, mod3_amt) as one control.
        auto makeSlot = [&](const std::string& name, juce::Colour colour) {
            const int src = indexOf(name + "_src"), dst = indexOf(name + "_dst"), amt = indexOf(name + "_amt");
            if (src < 0 || dst < 0 || amt < 0) return Cell{};
            StoreParameter* ps = proc_.parameter(s.id(g.first, instance, src));
            StoreParameter* pd = proc_.parameter(s.id(g.first, instance, dst));
            StoreParameter* pa = proc_.parameter(s.id(g.first, instance, amt));
            if (ps == nullptr || pd == nullptr || pa == nullptr) return Cell{};
            auto* slot = new ModSlotControl(std::atoi(name.c_str() + 3), colour);
            for (const auto& [box, id] : { std::pair<juce::ComboBox*, int>{ &slot->source, src }, std::pair<juce::ComboBox*, int>{ &slot->target, dst } }) {
                const ParamDesc& d = s.desc(s.id(g.first, instance, id));
                for (int c = 0; c <= static_cast<int>(d.maxValue); ++c) box->addItem(d.choices[c], c + 1);
            }
            combos_.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(*ps, slot->source));
            combos_.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(*pd, slot->target));
            sliders_.push_back(std::make_unique<juce::SliderParameterAttachment>(*pa, slot->amount));
            for (int i : { src, dst, amt }) placed[static_cast<size_t>(i)] = true;
            addAndMakeVisible(labels_.add(new juce::Label()));   // none: the slot shows its number
            controls_.add(slot);
            addAndMakeVisible(slot);
            Cell cell;
            cell.kind = 3;
            cell.control = controls_.size() - 1;
            return cell;
        };
        for (const ephui::GroupSpec& spec : ephui::layoutOf(g.first)) {
            Box box;
            box.title = spec.title;
            box.colour = ephui::familyColour(spec.family);
            for (const char* key : spec.keys) {
                if (key[0] == '@') {
                    const Cell c = makeSlot(key + 1, box.colour);
                    if (c.control >= 0) box.cells.push_back(c);
                    continue;
                }
                const bool big = key[0] == '*', narrow = key[0] == '~';
                const std::string name = big || narrow ? key + 1 : key;
                for (int i = 0; i < count; ++i) {
                    if (placed[static_cast<size_t>(i)] || keys[static_cast<size_t>(i)] != name) continue;
                    const Cell c = make(s.id(g.first, instance, i), box.colour, big, box.title, narrow);
                    if (c.control >= 0) box.cells.push_back(c);
                    placed[static_cast<size_t>(i)] = true;
                }
            }
            if (!box.cells.empty()) boxes_.push_back(std::move(box));
        }
        // Whatever the panel does not name, so nothing added to a table is lost from the editor.
        Box more;
        more.title = ephui::layoutOf(g.first).empty() ? juce::String("Settings") : juce::String("More");
        more.colour = ephui::familyColour(ephui::Family::Space);
        for (int i = 0; i < count; ++i) {
            if (placed[static_cast<size_t>(i)]) continue;
            const Cell c = make(s.id(g.first, instance, i), more.colour, false);
            if (c.control >= 0) more.cells.push_back(c);
        }
        if (!more.cells.empty()) boxes_.push_back(std::move(more));
    }
}

int ParamPage::top() const
{
    return instances_ > 1 || presetCount_ > 0 || pagePresets_ ? 32 : 0;
}

int ParamPage::layoutBoxes(juce::Rectangle<int> area, bool apply)
{
    // The panel of an instrument: titled boxes side by side, flowing into rows across the page, every box of a row
    // as tall as the tallest; inside a box its controls in a line (wrapping where the page is narrow), the large
    // encoders among the small ones, all centred on the line.
    constexpr int kTitle = 22, kPad = 8, kGap = 10;
    auto cellSize = [](const Cell& c) {
        switch (c.kind) {
        case 1: return juce::Point<int>(c.narrow ? 112 : 140, 100);
        case 2: return juce::Point<int>(92, 100);
        case 3: return juce::Point<int>(360, 34);
        default: return c.big ? juce::Point<int>(108, 136) : juce::Point<int>(82, 100);
        }
    };
    const int width = std::max(200, area.getWidth());
    int x = 0, y = 0, lineH = 0;
    std::vector<size_t> line;
    auto closeLine = [&]() {
        for (size_t b : line) boxes_[b].bounds.setHeight(lineH);
        line.clear();
    };
    for (size_t b = 0; b < boxes_.size(); ++b) {
        Box& box = boxes_[b];
        // Lines of cells inside the box, no wider than the page.
        const int inner = width - 2 * kPad;
        std::vector<std::pair<int, int>> rows;   // (width, height) of each line of cells
        int rw = 0, rh = 0;
        for (const Cell& c : box.cells) {
            const auto sz = cellSize(c);
            if (rw > 0 && rw + sz.x > inner) { rows.push_back({ rw, rh }); rw = 0; rh = 0; }
            rw += sz.x;
            rh = std::max(rh, sz.y);
        }
        if (rw > 0) rows.push_back({ rw, rh });
        int bw = 0, bh = kTitle + kPad;
        for (const auto& r : rows) { bw = std::max(bw, r.first); bh += r.second; }
        bw = std::max(bw + 2 * kPad, 120);
        bh += kPad / 2;
        if (x > 0 && x + bw > width) { closeLine(); x = 0; y += lineH + kGap; lineH = 0; }
        box.bounds = { area.getX() + x, area.getY() + y, bw, bh };
        line.push_back(b);
        lineH = std::max(lineH, bh);
        if (apply) {
            int cx = 0, cy = kTitle, row = 0;
            int rowW = rows.empty() ? 0 : rows[0].first, rowH = rows.empty() ? 0 : rows[0].second;
            for (Cell& c : box.cells) {
                const auto sz = cellSize(c);
                if (cx > 0 && cx + sz.x > rowW) { cy += rowH; cx = 0; ++row; rowW = rows[static_cast<size_t>(row)].first; rowH = rows[static_cast<size_t>(row)].second; }
                const int left = box.bounds.getX() + kPad + (bw - 2 * kPad - rowW) / 2;
                c.bounds = { left + cx, box.bounds.getY() + cy + (rowH - sz.y) / 2, sz.x, sz.y };
                cx += sz.x;
                juce::Component* comp = controls_[c.control];
                juce::Label* label = labels_[c.control];
                const auto r = c.bounds.reduced(3, 2);
                label->setBounds(r.getX(), r.getY(), r.getWidth(), c.kind == 3 ? 0 : 16);
                if (c.kind == 0) comp->setBounds(r.withTrimmedTop(16));
                else if (c.kind == 3) comp->setBounds(c.bounds);
                else comp->setBounds(r.getX() + 2, r.getCentreY() - 12, r.getWidth() - 4, 24);
            }
        }
        x += bw + kGap;
    }
    closeLine();
    return y + lineH;
}

int ParamPage::heightFor(int width) const
{
    // Measured on a copy: the boxes as they are laid out and drawn stay where resized() put them.
    auto* self = const_cast<ParamPage*>(this);
    const std::vector<Box> kept = boxes_;
    const int h = self->layoutBoxes({ 10, 10, width - 20, 100 }, false);
    self->boxes_ = kept;
    return 20 + top() + h + 10;
}

void ParamPage::paint(juce::Graphics& g)
{
    for (const Box& box : boxes_) {
        const auto r = box.bounds.toFloat();
        g.setColour(ephui::colour::group);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(ephui::colour::edge);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
        g.setColour(box.colour);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(box.title.toUpperCase(), box.bounds.getX() + 10, box.bounds.getY() + 4, box.bounds.getWidth() - 20, 14,
                   juce::Justification::centredLeft);
        g.setColour(box.colour.withAlpha(0.45f));
        g.fillRect(r.getX() + 10.0f, r.getY() + 19.0f, r.getWidth() - 20.0f, 1.0f);
    }
}

void ParamPage::resized()
{
    auto area = getLocalBounds().reduced(10);
    if (instances_ > 1 || presetCount_ > 0 || pagePresets_) {
        auto top = area.removeFromTop(26);
        if (instances_ > 1) { instance_.setBounds(top.removeFromLeft(90)); top.removeFromLeft(12); }
        if (presetCount_ > 0) {
            prev_.setBounds(top.removeFromLeft(28));
            top.removeFromLeft(4);
            preset_.setBounds(top.removeFromLeft(300));
            top.removeFromLeft(4);
            next_.setBounds(top.removeFromLeft(28));
            top.removeFromLeft(8);
            save_.setBounds(top.removeFromLeft(70));
            if (instances_ > 1) { top.removeFromLeft(8); allRows_.setBounds(top.removeFromLeft(90)); }
            top.removeFromLeft(12);
            composed_.setBounds(top);
        } else if (pagePresets_) {
            preset_.setBounds(top.removeFromLeft(300));
            top.removeFromLeft(8);
            save_.setBounds(top.removeFromLeft(70));
        }
    }
    area.removeFromTop(6);
    layoutBoxes(area, true);
}

// ---------------------------------------------------------------------------------------------------

ArrangeView::ArrangeView(EphemerisProcessor& p) : proc_(p)
{
    setTooltip("Click: jump there. Mouse wheel: zoom in and out around the pointer; drag, or Shift + wheel: move along; "
               "double click: the whole length.");
    if (const char* z = std::getenv("EPH_SHOT_ZOOM")) {   // "from:to", beats
        const juce::String s(z);
        zoomTo(s.upToFirstOccurrenceOf(":", false, false).getDoubleValue(), s.fromFirstOccurrenceOf(":", false, false).getDoubleValue());
    }
    startTimerHz(20);
}

void ArrangeView::rebuild()
{
    Score s;
    proc_.copyScore(s);
    if (std::fabs(s.lengthBeats - beats_) > 1.0e-6) { from_ = 0.0; span_ = 0.0; }   // another length: the whole of it
    beats_ = s.lengthBeats;
    markers_ = s.markers;
    tempo_ = s.tempo;
    longest_ = 0.0;
    for (auto& lane : hits_) lane.clear();
    bins_ = beats_ > 0.0 ? std::clamp(static_cast<int>(std::ceil(beats_)), 1, 1 << 17) : 0;
    lanes_.assign(static_cast<size_t>(kLanes) * static_cast<size_t>(bins_), 0.0f);
    cachedVersion_ = -1;
    if (bins_ == 0) return;
    std::vector<uint8_t> rows(static_cast<size_t>(bins_), 0);   // a bit per row playing in the column
    for (const NoteEvent& n : s.notes) {
        int lane = -1;
        const int part = static_cast<int>(n.part);
        if (part < kRows) lane = 0;
        else if (n.part == Part::Lead) lane = 1;
        else if (n.part == Part::TapeKeys) lane = 2;
        else if (n.part == Part::Strings || n.part == Part::Pad) lane = 3;
        else if (n.part == Part::Drone) lane = 4;
        else if (n.part == Part::Drums) lane = 5;
        if (lane < 0 || n.velocity <= 0.0f) continue;
        // The drums as short strokes, so their hits stand apart zoomed in; the rest as long as it sounds.
        const double length = lane == 5 ? 0.12 : std::max(n.length, 0.1);
        hits_[lane].push_back({ static_cast<float>(n.beat), static_cast<float>(n.beat + length), std::min(1.0f, n.velocity) });
        longest_ = std::max(longest_, length);
        const int b0 = std::clamp(static_cast<int>(n.beat / beats_ * bins_), 0, bins_ - 1);
        const int b1 = std::clamp(static_cast<int>((n.beat + std::max(length, 0.25)) / beats_ * bins_), b0, bins_ - 1);
        for (int b = b0; b <= b1; ++b) {
            if (lane == 0) rows[static_cast<size_t>(b)] = static_cast<uint8_t>(rows[static_cast<size_t>(b)] | (1u << part));
            else lanes_[static_cast<size_t>(lane) * static_cast<size_t>(bins_) + static_cast<size_t>(b)] = 1.0f;
        }
    }
    for (int b = 0; b < bins_; ++b) {
        int count = 0;
        for (uint8_t v = rows[static_cast<size_t>(b)]; v != 0; v &= static_cast<uint8_t>(v - 1)) ++count;
        lanes_[static_cast<size_t>(b)] = count == 0 ? 0.0f : 0.35f + 0.65f * static_cast<float>(count) / static_cast<float>(kRows);
    }
    for (auto& lane : hits_) std::sort(lane.begin(), lane.end(), [](const Hit& x, const Hit& y) { return x.from < y.from; });
}

void ArrangeView::window(double& from, double& to) const
{
    if (span_ <= 0.0 || span_ >= beats_) { from = 0.0; to = std::max(beats_, 1.0); return; }
    from = std::clamp(from_, 0.0, beats_ - span_);
    to = from + span_;
}

bool ArrangeView::zoomed(double& from, double& to) const
{
    window(from, to);
    return span_ > 0.0 && span_ < beats_;
}

void ArrangeView::show(double from, double span)
{
    if (beats_ <= 0.0) return;
    span = std::clamp(span, std::min(beats_, kNarrowest), beats_);
    if (span >= beats_ - 1.0e-9) { from_ = 0.0; span_ = 0.0; return; }
    span_ = span;
    from_ = std::clamp(from, 0.0, beats_ - span);
}

void ArrangeView::zoomAround(float x, double factor)
{
    double a = 0.0, b = 0.0;
    window(a, b);
    const double t = std::clamp(static_cast<double>(x) / std::max(1, getWidth()), 0.0, 1.0);
    const double at = a + (b - a) * t, span = (b - a) * factor;
    show(at - span * t, span);
    repaint();
}

double ArrangeView::beatAt(float x) const
{
    double a = 0.0, b = 0.0;
    window(a, b);
    return a + (b - a) * std::clamp(static_cast<double>(x) / std::max(1, getWidth()), 0.0, 1.0);
}

void ArrangeView::timerCallback()
{
    if (!isShowing()) return;
    // A zoomed view pages on when the playhead runs out of it -- not when the view was moved away from the playhead.
    const double pos = proc_.positionBeats();
    double a = 0.0, b = 0.0;
    if (zoomed(a, b) && !dragged_ && a == lastFrom_ && b == lastTo_ && lastPos_ >= a && lastPos_ <= b && (pos < a || pos > b)) {
        show(pos - 0.05 * (b - a), b - a);
        window(a, b);
    }
    lastPos_ = pos;
    lastFrom_ = a;
    lastTo_ = b;
    repaint();
}

void ArrangeView::paint(juce::Graphics& g)
{
    if (proc_.scoreVersion() != version_) { version_ = proc_.scoreVersion(); rebuild(); }
    if (wanted_.second > wanted_.first && beats_ > 0.0) {
        show(wanted_.first, wanted_.second - wanted_.first);
        wanted_ = { 0.0, 0.0 };
    }
    if (beats_ <= 0.0) {
        g.fillAll(kPanel);
        return;
    }
    double a = 0.0, b = 0.0;
    const bool zoom = zoomed(a, b);
    const float w = static_cast<float>(getWidth()), h = static_cast<float>(getHeight());
    // The picture at the screen's own pixels (the panel is scaled with the window), drawn again when the window moves.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int iw = std::max(1, juce::roundToInt(w * scale)), ih = std::max(1, juce::roundToInt(h * scale));
    if (!cache_.isValid() || cache_.getWidth() != iw || cache_.getHeight() != ih || cachedFrom_ != a || cachedTo_ != b || cachedVersion_ != version_) {
        cache_ = juce::Image(juce::Image::RGB, iw, ih, false);
        juce::Graphics ig(cache_);
        ig.addTransform(juce::AffineTransform::scale(static_cast<float>(iw) / w, static_cast<float>(ih) / h));
        render(ig, w, h, a, b, zoom);
        cachedFrom_ = a;
        cachedTo_ = b;
        cachedVersion_ = version_;
    }
    g.drawImage(cache_, getLocalBounds().toFloat());
    const double pos = proc_.positionBeats();
    const float x = static_cast<float>((pos - a) / (b - a)) * w;
    g.setColour(kAccent);
    if (x >= -1.0f && x <= w + 1.0f) g.fillRect(x - 1.0f, 0.0f, 2.0f, h);
    if (zoom) g.fillRect(w * static_cast<float>(pos / beats_) - 1.0f, h - 4.0f, 2.0f, 4.0f);   // and on the bar of the whole
}

void ArrangeView::render(juce::Graphics& g, float w, float h, double a, double b, bool zoom)
{
    g.fillAll(kPanel);
    const auto xOf = [&](double beat) { return static_cast<float>((beat - a) / (b - a)) * w; };
    const float rule = 12.0f, ry = h - rule, top = 20.0f;
    const auto endOf = [&](size_t i) { return i + 1 < markers_.size() ? markers_[i + 1].beat : beats_; };
    // The sections, behind the lanes.
    for (size_t i = 0; i < markers_.size(); ++i) {
        const float x0 = xOf(markers_[i].beat), x1 = xOf(endOf(i));
        if (x1 < 0.0f || x0 > w) continue;
        const float l = std::max(x0 + 1.0f, -2.0f), r = std::min(x1 - 1.0f, w + 2.0f);
        g.setColour(sectionColour(juce::String(markers_[i].text)));
        g.fillRect(l, 2.0f, std::max(1.0f, r - l), ry - 3.0f);
    }
    // The names. A concert's pieces ("Stueck 2 (Cosmic): Aufbau") apart by a line and their number at their start ("#2
    // Cosmic"); the sections' names where one of the piece's has room -- else the number alone over all of them.
    const juce::Font font{ juce::FontOptions(11.0f) };
    g.setFont(font);
    const auto pieceOf = [](const std::string& t) { const size_t k = t.rfind(": "); return k == std::string::npos ? std::string() : t.substr(0, k); };
    for (size_t i = 0; i < markers_.size();) {
        const std::string piece = pieceOf(markers_[i].text);
        size_t j = i + 1;
        while (!piece.empty() && j < markers_.size() && pieceOf(markers_[j].text) == piece) ++j;
        const float gx0 = xOf(markers_[i].beat), gx1 = xOf(endOf(j - 1));
        if (gx1 >= 0.0f && gx0 <= w) {
            if (i > 0 && !piece.empty()) {
                g.setColour(kInk.withAlpha(0.75f));
                g.fillRect(gx0 - 1.0f, 0.0f, 2.0f, ry);
            }
            bool roomy = false;
            for (size_t k = i; k < j && !roomy; ++k) roomy = xOf(endOf(k)) - xOf(markers_[k].beat) >= 40.0f;
            const juce::String tag = juce::String(piece).replace("Stueck ", "#").replace("Zwischenspiel ", "Z").removeCharacters("()");
            g.setColour(kInk);
            if (roomy) {
                for (size_t k = i; k < j; ++k) {
                    const float l = std::max(xOf(markers_[k].beat), 0.0f) + 4.0f, r = std::min(xOf(endOf(k)), w) - 4.0f;
                    const juce::String part = juce::String(markers_[k].text).fromLastOccurrenceOf(": ", false, false);
                    juce::String text = part;
                    if (k == i && tag.isNotEmpty()) {
                        text = tag + "  " + part;
                        if (juce::GlyphArrangement::getStringWidth(font, text) > r - l) text = tag;
                    }
                    if (r - l < (text == tag ? 12.0f : 26.0f)) continue;
                    g.drawText(text, juce::Rectangle<float>(l, 3.0f, r - l, 15.0f), juce::Justification::centredLeft, true);
                }
            } else {
                const float l = std::max(gx0, 0.0f) + 3.0f, r = std::min(gx1, w) - 2.0f;
                if (r - l >= 12.0f) g.drawText(tag, juce::Rectangle<float>(l, 3.0f, r - l, 15.0f), juce::Justification::centredLeft, true);
            }
        }
        i = j;
    }
    // The ruler: a piece's bars, a concert's minutes -- at a step that leaves the numbers room; faint lines through the
    // lanes.
    {
        const auto clock = [](double secs) {
            const int s = static_cast<int>(std::lround(secs)), hours = s / 3600, mins = (s / 60) % 60;
            return (hours > 0 ? juce::String(hours) + ":" + juce::String(mins).paddedLeft('0', 2) : juce::String(mins)) + ":"
                   + juce::String(s % 60).paddedLeft('0', 2);
        };
        const float room = 52.0f;
        g.setFont(juce::FontOptions(9.0f));
        const auto tick = [&](double beat, const juce::String& label) {
            const float x = xOf(beat);
            g.setColour(kInk.withAlpha(0.07f));
            g.fillRect(x, top, 1.0f, ry - top);
            g.setColour(kDim);
            g.fillRect(x, ry, 1.0f, 4.0f);
            g.drawText(label, juce::Rectangle<float>(x + 3.0f, ry, room, rule), juce::Justification::centredLeft);
        };
        if (proc_.playingKind() != 0) {
            const double s0 = tempo_.secondsAt(a), s1 = tempo_.secondsAt(b);
            static const int kClockSteps[] = { 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
            int step = 3600;
            for (int k : kClockSteps) if (static_cast<double>(k) * w / std::max(1.0, s1 - s0) >= room) { step = k; break; }
            for (double t = std::ceil(s0 / step) * step; t <= s1; t += step) tick(tempo_.beatAt(t), clock(t));
        } else {
            const double perBar = w / std::max(1.0e-9, (b - a) / kBeatsPerBar);
            int step = 1;
            while (step * perBar < room && step < 4096) step *= 2;
            for (int bar = static_cast<int>(std::ceil(a / kBeatsPerBar / step)) * step; static_cast<double>(kBeatsPerBar) * bar <= b; bar += step)
                tick(static_cast<double>(kBeatsPerBar) * bar, juce::String(bar + 1));
        }
    }
    // The lanes: zoomed in far enough the notes themselves, else the activity under each pixel.
    const float laneH = (ry - top - 1.0f) / static_cast<float>(kLanes);
    const double pxPerBeat = w / std::max(1.0e-9, b - a);
    if (laneH > 2.0f) {
        for (int l = 0; l < kLanes; ++l) {
            const float y = top + laneH * static_cast<float>(l) + 1.0f, hh = laneH - 2.0f;
            if (pxPerBeat >= 2.0) {
                const std::vector<Hit>& row = hits_[l];
                auto it = std::lower_bound(row.begin(), row.end(), a - longest_, [](const Hit& x, double v) { return x.from < v; });
                for (; it != row.end() && it->from < b; ++it) {
                    if (it->to <= a) continue;
                    const float x0 = std::max(-1.0f, xOf(it->from)), x1 = std::min(w + 1.0f, xOf(it->to));
                    g.setColour(kInk.withAlpha(0.25f + 0.45f * it->velocity));
                    g.fillRect(x0, y, std::max(1.2f, x1 - x0 - (x1 - x0 > 3.0f ? 1.0f : 0.0f)), hh);
                }
                continue;
            }
            const float* lane = lanes_.data() + static_cast<size_t>(l) * static_cast<size_t>(bins_);
            const int cols = static_cast<int>(std::ceil(w));
            float runV = 0.0f;
            int runX = 0;
            for (int x = 0; x <= cols; ++x) {
                float v = 0.0f;
                if (x < cols) {
                    const double f0 = (a + (b - a) * x / w) / beats_, f1 = (a + (b - a) * (x + 1) / w) / beats_;
                    const int b0 = std::clamp(static_cast<int>(f0 * bins_), 0, bins_ - 1);
                    const int b1 = std::clamp(static_cast<int>(std::ceil(f1 * bins_)), b0 + 1, bins_);
                    for (int k = b0; k < b1; ++k) v = std::max(v, lane[k]);
                }
                if (x < cols && v == runV) continue;
                if (runV > 0.0f) {
                    g.setColour(kInk.withAlpha(0.55f * runV));
                    g.fillRect(static_cast<float>(runX), y, static_cast<float>(x - runX), hh);
                }
                runV = v;
                runX = x;
            }
        }
        static const char* const names[kLanes] = { "rows", "lead", "tape", "str+poly", "drone", "drums" };
        g.setFont(juce::FontOptions(9.0f));
        for (int l = 0; l < kLanes; ++l) {
            const juce::Rectangle<float> r(2.0f, top + laneH * static_cast<float>(l), 44.0f, laneH);
            g.setColour(kPanel.withAlpha(0.7f));
            g.fillRect(r.withWidth(40.0f));
            g.setColour(kDim);
            g.drawText(names[l], r.reduced(2.0f, 0.0f), juce::Justification::centredLeft);
        }
    }
    // Zoomed: where the window lies in the whole, under the ruler.
    if (zoom) {
        g.setColour(kInk.withAlpha(0.12f));
        g.fillRect(0.0f, h - 2.0f, w, 2.0f);
        g.setColour(kAccent.withAlpha(0.85f));
        g.fillRect(w * static_cast<float>(a / beats_), h - 2.0f, std::max(3.0f, w * static_cast<float>((b - a) / beats_)), 2.0f);
    }
}

void ArrangeView::mouseDown(const juce::MouseEvent& e)
{
    downX_ = e.position.x;
    dragged_ = false;
    double a = 0.0, b = 0.0;
    const bool zoom = zoomed(a, b);
    downFrom_ = a;
    if (!zoom) proc_.seekTo(beatAt(e.position.x));   // the whole length: at once, as ever
}

void ArrangeView::mouseDrag(const juce::MouseEvent& e)
{
    double a = 0.0, b = 0.0;
    if (!zoomed(a, b)) return;
    const float dx = e.position.x - downX_;
    if (!dragged_ && std::abs(dx) < 4.0f) return;
    dragged_ = true;
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    show(downFrom_ - (b - a) * dx / std::max(1, getWidth()), b - a);
    repaint();
}

void ArrangeView::mouseUp(const juce::MouseEvent& e)
{
    if (dragged_) {
        dragged_ = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    double a = 0.0, b = 0.0;
    if (zoomed(a, b)) proc_.seekTo(beatAt(e.position.x));
}

void ArrangeView::mouseDoubleClick(const juce::MouseEvent&)
{
    show(0.0, 0.0);
    repaint();
}

void ArrangeView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    double a = 0.0, b = 0.0;
    window(a, b);
    // Sideways (a trackpad, a tilting wheel) or with Shift: along the length; else in and out, about 1.4 times a notch.
    const bool sideways = std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (sideways || e.mods.isShiftDown()) {
        const float d = sideways ? wheel.deltaX : wheel.deltaY;
        show(a - (b - a) * 0.5 * d, b - a);
        repaint();
        return;
    }
    if (wheel.deltaY != 0.0f) zoomAround(e.position.x, std::pow(2.0, -2.0 * wheel.deltaY));
}

void ArrangeView::mouseMagnify(const juce::MouseEvent& e, float scale)
{
    if (scale > 0.0f) zoomAround(e.position.x, 1.0 / scale);
}

// ---------------------------------------------------------------------------------------------------

ScrollingPage::ScrollingPage(std::unique_ptr<ParamPage> page) : page_(std::move(page))
{
    view_.setViewedComponent(page_.get(), false);
    view_.setScrollBarsShown(true, false);
    addAndMakeVisible(view_);
}

void ScrollingPage::resized() { fit(*page_, view_, getLocalBounds()); }

void ScrollingPage::fit(ParamPage& page, juce::Viewport& view, juce::Rectangle<int> area)
{
    view.setBounds(area);
    int w = area.getWidth(), h = page.heightFor(w);
    if (h > area.getHeight()) { w -= view.getScrollBarThickness(); h = page.heightFor(w); }
    page.setSize(w, std::max(h, area.getHeight()));
}

namespace {

/**
 * @brief The logo (Deploy/make_icon.py, drawn as vectors): the Rack page's orrery -- a warm sun, orbits and their
 *        planets on a dark tile; two orbits up to 24 px, three up to 48, five above.
 */
void drawLogo(juce::Graphics& g, juce::Rectangle<float> r)
{
    const float s = std::min(r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre(s, s);
    const juce::Point<float> c = r.getCentre();
    g.setColour(juce::Colour(21, 23, 28));
    g.fillRoundedRectangle(r, s / 5.0f);
    const int orbits = s <= 24.0f ? 2 : (s <= 48.0f ? 3 : 5);
    const float sun = s * (s > 24.0f ? 0.16f : 0.2f), width = std::max(1.0f, s / 64.0f);
    static const juce::uint32 planets[5] = { 0xffe0a458, 0xffd9825b, 0xff9fbf6f, 0xff6fb8ae, 0xff6f8fb8 };
    for (int k = 0; k < orbits; ++k) {
        const float rad = sun + (s * 0.42f - sun) * static_cast<float>(k + 1) / static_cast<float>(orbits);
        g.setColour(juce::Colour(201, 164, 92).withAlpha(150.0f / 255.0f));
        g.drawEllipse(c.x - rad, c.y - rad, 2.0f * rad, 2.0f * rad, width);
        const float a = -juce::MathConstants<float>::halfPi + 2.1f * static_cast<float>(k + 1);
        const float pr = std::max(s * 0.035f, width * 2.2f);
        g.setColour(juce::Colour(planets[k % 5]));
        g.fillEllipse(c.x + rad * std::cos(a) - pr, c.y + rad * std::sin(a) - pr, 2.0f * pr, 2.0f * pr);
    }
    g.setColour(juce::Colour(232, 178, 92));
    g.fillEllipse(c.x - sun, c.y - sun, 2.0f * sun, 2.0f * sun);
}

} // namespace

EphemerisEditor::EphemerisEditor(EphemerisProcessor& p) : juce::AudioProcessorEditor(p), proc_(p), arrange_(p)
{
    setLookAndFeel(&lnf_);
    addAndMakeVisible(body_);
    body_.painter = [this](juce::Graphics& g) { g.fillAll(kBack); drawLogo(g, logo_); };
    body_.onResize = [this] { layoutBody(); };
    ParamStore& s = proc_.store();
    title_.setText("EPHEMERIS", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, kAccent);
    body_.addAndMakeVisible(title_);

    auto combo = [&](juce::ComboBox& box, int id) {
        const ParamDesc& d = s.desc(id);
        for (int c = 0; c <= static_cast<int>(d.maxValue); ++c) box.addItem(d.choices[c], c + 1);
        combos_.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(*proc_.parameter(id), box));
        body_.addAndMakeVisible(box);
    };
    combo(style_, s.id(Module::Compose, 0, compose::Style));
    combo(key_, s.id(Module::Compose, 0, compose::Key));
    combo(scale_, s.id(Module::Compose, 0, compose::Scale));
    // A piece, a concert or a night set (01.10.2026 -- the user: "In der GUI ist es etwas verwirrend, ob man jetzt einen
    // Einzeltrack erzeugt oder einen Mix"; as Parhelion and Totality have it): three buttons that compose what they name,
    // and one length, the one of what is chosen (compose.piece_minutes or compose.concert_minutes).
    pieceMode_.setTooltip("Compose a single piece (its length beside)");
    concertMode_.setTooltip("Compose a concert: pieces one after another through related keys, along an arc of tension (its "
                            "length beside; with Morph To on the Master page from one style to another)");
    nightMode_.setTooltip("Compose a night set: pieces in mixed styles along waves of energy, each mixed into the next as a DJ "
                          "does it (its length beside, up to 12 hours)");
    pieceMode_.onClick = [this] { proc_.chooseKind(0); };
    concertMode_.onClick = [this] { proc_.chooseKind(1); };
    nightMode_.onClick = [this] { proc_.chooseKind(2); };
    pieceMode_.setConnectedEdges(juce::Button::ConnectedOnRight);
    concertMode_.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    nightMode_.setConnectedEdges(juce::Button::ConnectedOnLeft);
    for (auto* b : { &pieceMode_, &concertMode_, &nightMode_ }) {
        b->setColour(juce::TextButton::buttonOnColourId, kAccent.withAlpha(0.5f));
        body_.addAndMakeVisible(b);
    }
    length_.setSliderStyle(juce::Slider::LinearHorizontal);
    length_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 20);
    length_.onValueChange = [this] {
        if (syncing_) return;
        const ParamStore& st = proc_.store();
        proc_.setFromUi(lengthKind_ == 0 ? st.id(Module::Compose, 0, compose::PieceMinutes) : st.id(Module::Compose, 0, compose::ConcertMinutes),
                        static_cast<float>(length_.getValue()));
    };
    lengthLabel_.setText("Length", juce::dontSendNotification);
    lengthLabel_.setColour(juce::Label::textColourId, kDim);
    lengthLabel_.setJustificationType(juce::Justification::centredRight);
    body_.addAndMakeVisible(length_);
    body_.addAndMakeVisible(lengthLabel_);
    showLength(proc_.chosenKind());

    compose_.onClick = [this] { proc_.compose(); };
    seed_.onClick = [this] { proc_.newSeed(); };
    play_.onClick = [this] { proc_.setPlaying(!proc_.isPlaying()); };
    for (auto* b : { &compose_, &seed_, &play_, &save_, &load_, &export_ }) body_.addAndMakeVisible(b);
    // Mute, as in Phosphene: silence at the output; EPH_MUTE (or the screenshot mode) holds it on.
    mute_.setClickingTogglesState(true);
    mute_.setToggleState(proc_.muted(), juce::dontSendNotification);
    mute_.setEnabled(!proc_.muteForced() || std::getenv("EPH_SHOT") != nullptr);   // (a picture shows it as a player finds it)
    mute_.setColour(juce::TextButton::buttonOnColourId, ephui::colour::red.withAlpha(0.55f));
    mute_.setTooltip(proc_.muteForced() ? "Muted by EPH_MUTE: an automated run makes no sound" : "Silence the output");
    mute_.onClick = [this] { proc_.setMuted(mute_.getToggleState()); };
    body_.addAndMakeVisible(mute_);
    play_.setColour(juce::TextButton::buttonColourId, kAccent.withAlpha(0.22f));
    compose_.setColour(juce::TextButton::buttonColourId, kAccent.withAlpha(0.14f));
    // The standalone's full screen (F11), as Phosphene's; hidden in a host, which owns its window.
    // The update check: once a day it asks GitHub for the latest release (nothing else is sent); a newer one shows here.
    checkUpdates_.setToggleState(updates_->enabled(), juce::dontSendNotification);
    checkUpdates_.setTooltip("Once a day, ask GitHub whether a newer Ephemeris is out (nothing else is sent, nothing is downloaded)");
    checkUpdates_.onClick = [this] { updates_->setEnabled(checkUpdates_.getToggleState()); };
    body_.addAndMakeVisible(checkUpdates_);
    update_.setColour(juce::HyperlinkButton::textColourId, ephui::colour::amber);
    update_.setTooltip("Open the release page");
    body_.addChildComponent(update_);
    full_.setTooltip("Full screen (F11; Esc leaves it)");
    full_.onClick = [this] { toggleFullScreen(); };
    body_.addChildComponent(full_);
    setWantsKeyboardFocus(true);

    for (const char* unit : kUnitNames) {
        auto* b = rerollButtons_.add(new juce::TextButton(juce::String("reroll ") + unit));
        const juce::String u(unit);
        b->onClick = [this, u] { proc_.reroll(u); };
        body_.addAndMakeVisible(b);
    }
    rerolls_.setColour(juce::Label::textColourId, kDim);
    status_.setColour(juce::Label::textColourId, kInk);
    body_.addAndMakeVisible(rerolls_);
    body_.addAndMakeVisible(status_);

    auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ephemeris");
    save_.onClick = [this, documents] {
        documents.createDirectory();
        chooser_ = std::make_unique<juce::FileChooser>("Save set", documents.getChildFile("set.ephset"), "*.ephset");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) proc_.saveSet(fc.getResult()); });
    };
    load_.onClick = [this, documents] {
        chooser_ = std::make_unique<juce::FileChooser>("Load set", documents, "*.ephset");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc_.loadSet(fc.getResult()); });
    };
    export_.onClick = [this, documents] {
        // The mix and its MIDI, or with the stems as well (a WAV per channel strip and the rooms: large files).
        juce::PopupMenu menu;
        menu.addItem(1, "WAV + MIDI");
        menu.addItem(2, "WAV + MIDI + stems (a WAV per channel strip)");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&export_), [this, documents](int choice) {
            if (choice == 0) return;
            const bool stems = choice == 2;
            documents.createDirectory();
            chooser_ = std::make_unique<juce::FileChooser>("Export", documents.getChildFile("ephemeris.wav"), "*.wav");
            chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, stems](const juce::FileChooser& fc) {
                                      if (fc.getResult() != juce::File()) proc_.exportTo(fc.getResult().withFileExtension(".wav"), stems);
                                  });
        });
    };

    body_.addAndMakeVisible(arrange_);
    using M = Module;
    auto page = [&](const char* name, std::vector<std::pair<M, int>> groups, int instances) {
        tabs_.addTab(name, kPanel, new ScrollingPage(std::make_unique<ParamPage>(proc_, std::move(groups), instances)), true);
    };
    tabs_.addTab("Mixer", kPanel, new MixerConsole(proc_), true);
    tabs_.addTab("Perform", kPanel, new PerformPage(proc_, std::make_unique<ParamPage>(proc_, std::vector<std::pair<M, int>>{ { M::Perform, 0 } }, 1)), true);
    tabs_.addTab("Rack", kPanel, new RackPage(proc_, std::make_unique<ParamPage>(proc_, std::vector<std::pair<M, int>>{ { M::Row, 0 } }, kRows)), true);
    tabs_.addTab("Gestures", kPanel, new GestureView(proc_), true);
    page("Voices", { { M::Voice, 0 } }, kRows);
    page("Lead", { { M::Lead, 0 } }, 1);
    page("Drone", { { M::Drone, 0 } }, 1);
    page("Tape Keys", { { M::Tape, 0 } }, 1);
    page("Strings", { { M::Strings, 0 } }, 1);
    page("Poly", { { M::Poly, 0 } }, 1);
    page("Atmosphere", { { M::Atmos, 0 } }, 1);
    page("Echo + Spring", { { M::Echo, 0 }, { M::Spring, 0 }, { M::Echo2, 0 } }, 1);
    page("Hall", { { M::Reverb, 0 }, { M::Blend, 0 }, { M::Early, 0 }, { M::Shimmer, 0 } }, 1);
    page("Drums", { { M::Drums, 0 } }, 1);
    page("Master", { { M::Master, 0 }, { M::Compose, 0 }, { M::Cue, 0 } }, 1);
    tabs_.addTab("Style", kPanel, new StylePage(proc_, std::make_unique<ParamPage>(proc_, std::vector<std::pair<M, int>>{ { M::Custom, 0 } }, 1)), true);
    body_.addAndMakeVisible(tabs_);

    setResizable(true, true);
    setResizeLimits(800, 520, 4800, 3100);
    setSize(1180, 760);

    if (const char* shot = std::getenv("EPH_SHOT")) {
        shotPath_ = shot;
        if (const char* tab = std::getenv("EPH_TAB")) tabs_.setCurrentTabIndex(juce::String(tab).getIntValue());
        // EPH_SHOT_SIZE ("1600x2400"): a larger window, so a long page shows whole.
        if (const char* size = std::getenv("EPH_SHOT_SIZE")) {
            const juce::String sz(size);
            setSize(sz.upToFirstOccurrenceOf("x", false, false).getIntValue(), sz.fromFirstOccurrenceOf("x", false, false).getIntValue());
        }
    }
    startTimerHz(15);
}

EphemerisEditor::~EphemerisEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}


void EphemerisEditor::paint(juce::Graphics& g) { g.fillAll(kBack); }

void EphemerisEditor::resized()
{
    // The body at the design size (1180 x 760), scaled to the window by its height, and as wide as the window then
    // allows: a larger window shows the panel larger, a wider one gives the pages more room.
    const float scale = juce::jlimit(0.5f, 4.0f, std::min(static_cast<float>(getWidth()) / 1180.0f, static_cast<float>(getHeight()) / 760.0f));
    body_.setTransform(juce::AffineTransform::scale(scale));
    body_.setBounds(0, 0, juce::roundToInt(static_cast<float>(getWidth()) / scale), juce::roundToInt(static_cast<float>(getHeight()) / scale));
}

void EphemerisEditor::parentHierarchyChanged()
{
    // As Phosphene and Noctuary: a maximise button beside the other two, on the next turn of the message loop (the
    // standalone's window is still putting its content in when this is called). A host's window finds nothing here.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<EphemerisEditor>(this)] {
        if (safe == nullptr) return;
        auto* window = safe->findParentComponentOfClass<juce::DocumentWindow>();
        if (window != nullptr)
            window->setTitleBarButtonsRequired(juce::DocumentWindow::minimiseButton | juce::DocumentWindow::maximiseButton
                                                   | juce::DocumentWindow::closeButton, false);
        safe->full_.setVisible(window != nullptr);
        safe->layoutBody();
    });
}

void EphemerisEditor::toggleFullScreen()
{
    auto* window = findParentComponentOfClass<juce::DocumentWindow>();
    if (window == nullptr) return;
    auto& desktop = juce::Desktop::getInstance();
    const bool on = desktop.getKioskModeComponent() != window;
    desktop.setKioskModeComponent(on ? window : nullptr, false);
    full_.setToggleState(on, juce::dontSendNotification);
    grabKeyboardFocus();
}

bool EphemerisEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F11Key) { toggleFullScreen(); return true; }
    if (key.getKeyCode() == juce::KeyPress::escapeKey && juce::Desktop::getInstance().getKioskModeComponent() != nullptr) {
        toggleFullScreen();
        return true;
    }
    return false;
}

void EphemerisEditor::showLength(int kind)
{
    // A piece: 4 to 40 minutes; a concert or a night set: 20 minutes to 12 hours, its first two hours over most of the way.
    const juce::ScopedValueSetter<bool> quiet(syncing_, true);
    lengthKind_ = kind;
    if (kind != 0) {
        length_.setRange(20.0, 720.0, 1.0);
        length_.setSkewFactorFromMidPoint(90.0);
        length_.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " min"; };
        length_.setTooltip(kind == 2 ? "The night set's length (Compose night set makes it)" : "The concert's length (Compose concert makes it)");
    } else {
        length_.setRange(4.0, 40.0, 0.5);
        length_.setSkewFactor(1.0);
        length_.textFromValueFunction = [](double v) { return juce::String(v, 1) + " min"; };
        length_.setTooltip("The piece's length (Compose piece makes it)");
    }
    length_.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
    const ParamStore& st = proc_.store();
    length_.setValue(st.get(kind != 0 ? st.id(Module::Compose, 0, compose::ConcertMinutes) : st.id(Module::Compose, 0, compose::PieceMinutes)),
                     juce::dontSendNotification);
    length_.updateText();
}

void EphemerisEditor::layoutBody()
{
    auto area = body_.getLocalBounds().reduced(10);
    auto top = area.removeFromTop(34);
    logo_ = top.removeFromLeft(34).toFloat().reduced(2.0f);
    title_.setBounds(top.removeFromLeft(112));
    style_.setBounds(top.removeFromLeft(110).reduced(3));
    key_.setBounds(top.removeFromLeft(64).reduced(3));
    scale_.setBounds(top.removeFromLeft(120).reduced(3));
    top.removeFromLeft(8);
    pieceMode_.setBounds(top.removeFromLeft(58).reduced(0, 3));
    concertMode_.setBounds(top.removeFromLeft(72).reduced(0, 3));
    nightMode_.setBounds(top.removeFromLeft(80).reduced(0, 3));
    play_.setBounds(top.removeFromRight(80).reduced(3));
    seed_.setBounds(top.removeFromRight(90).reduced(3));
    compose_.setBounds(top.removeFromRight(136).reduced(3));
    top.removeFromLeft(4);
    lengthLabel_.setBounds(top.removeFromLeft(48));
    length_.setBounds(top.reduced(2));
    area.removeFromTop(6);
    auto second = area.removeFromTop(28);
    for (auto* b : rerollButtons_) b->setBounds(second.removeFromLeft(82).reduced(2));
    export_.setBounds(second.removeFromRight(140).reduced(2));
    load_.setBounds(second.removeFromRight(80).reduced(2));
    save_.setBounds(second.removeFromRight(80).reduced(2));
    second.removeFromRight(8);
    mute_.setBounds(second.removeFromRight(proc_.muteForced() && shotPath_.isEmpty() ? 100 : 70).reduced(2));
    area.removeFromTop(4);
    auto third = area.removeFromTop(20);
    if (full_.isVisible()) full_.setBounds(third.removeFromRight(100));
    checkUpdates_.setBounds(third.removeFromRight(120));
    update_.setBounds(third.removeFromRight(190));
    status_.setBounds(third.removeFromLeft(third.getWidth() / 2));
    rerolls_.setBounds(third);
    area.removeFromTop(4);
    arrange_.setBounds(area.removeFromTop(110));
    area.removeFromTop(8);
    tabs_.setBounds(area);
}

void EphemerisEditor::timerCallback()
{
    status_.setText(proc_.status(), juce::dontSendNotification);
    rerolls_.setText(proc_.curationText(), juce::dontSendNotification);
    {
        // A newer version, where the check found one.
        const juce::String v = checkUpdates_.getToggleState() ? updates_->newer() : juce::String();
        if (v.isNotEmpty() && update_.getButtonText() != "Version " + v + " available") {
            update_.setButtonText("Version " + v + " available");
            update_.setURL(juce::URL(updates_->page()));
        }
        update_.setVisible(v.isNotEmpty());
    }
    play_.setButtonText(proc_.isPlaying() ? "Stop" : "Play");
    // The choice and its length as the parameters have them (a loaded set and a host move them too); Compose names
    // what it makes, and is lit while what plays is something else.
    {
        const int kind = proc_.chosenKind();
        if (kind != lengthKind_) showLength(kind);
        pieceMode_.setToggleState(kind == 0, juce::dontSendNotification);
        concertMode_.setToggleState(kind == 1, juce::dontSendNotification);
        nightMode_.setToggleState(kind == 2, juce::dontSendNotification);
        const ParamStore& st = proc_.store();
        const double v = st.get(kind != 0 ? st.id(Module::Compose, 0, compose::ConcertMinutes) : st.id(Module::Compose, 0, compose::PieceMinutes));
        if (!length_.isMouseButtonDown() && std::abs(length_.getValue() - v) > 1.0e-3) {
            const juce::ScopedValueSetter<bool> quiet(syncing_, true);
            length_.setValue(v, juce::dontSendNotification);
        }
        static const char* const kCompose[3] = { "Compose piece", "Compose concert", "Compose night set" };
        compose_.setButtonText(kCompose[kind]);
        const juce::Colour c = kAccent.withAlpha(kind != proc_.playingKind() && !proc_.isComposing() ? 0.45f : 0.14f);
        if (compose_.findColour(juce::TextButton::buttonColourId) != c) compose_.setColour(juce::TextButton::buttonColourId, c);
    }
    // A screenshot shows the switch as a player finds it: the run is muted all the same (EPH_SHOT forces it).
    const bool shown = proc_.muted() && shotPath_.isEmpty();
    mute_.setToggleState(shown, juce::dontSendNotification);
    mute_.setButtonText(shown ? (proc_.muteForced() ? "Muted (env)" : "Muted") : "Mute");
    compose_.setEnabled(!proc_.isComposing());
    // The test mode: the recording, when full, is written and the standalone quits.
    if (proc_.recordingDone()) {
        proc_.writeRecording();
        if (juce::JUCEApplicationBase::isStandaloneApp()) juce::JUCEApplicationBase::quit();
        return;
    }
    // The screenshot mode: wait for the first piece, then draw the panel into a file and quit.
    // EPH_SHOT_AT (a beat) moves the playhead there first, so the picture can show the middle of a piece.
    if (shotPath_.isNotEmpty() && !proc_.isComposing() && shotTicks_ == 0)
        if (const char* at = std::getenv("EPH_SHOT_AT")) proc_.seekTo(std::atof(at));
    // EPH_SHOT_FULL: the window grows until nothing of the page in front scrolls, so the picture shows all of it.
    if (shotPath_.isNotEmpty() && !proc_.isComposing() && (shotTicks_ == 6 || shotTicks_ == 12) && std::getenv("EPH_SHOT_FULL") != nullptr) {
        std::function<int(juce::Component&)> overflow = [&](juce::Component& c) {
            int most = 0;
            if (auto* v = dynamic_cast<juce::Viewport*>(&c))
                if (auto* inner = v->getViewedComponent()) most = inner->getHeight() - v->getMaximumVisibleHeight();
            for (auto* child : c.getChildren()) most = std::max(most, overflow(*child));
            return most;
        };
        if (auto* page = tabs_.getCurrentContentComponent())
            if (const int more = overflow(*page); more > 0) setSize(getWidth(), getHeight() + more + 4);
    }
    if (shotPath_.isNotEmpty() && !proc_.isComposing() && ++shotTicks_ > 20) {
        const juce::Image img = createComponentSnapshot(getLocalBounds());
        juce::File f(shotPath_);
        f.deleteFile();
        juce::FileOutputStream out(f);
        juce::PNGImageFormat().writeImageToStream(img, out);
        out.flush();
        shotPath_ = {};
        if (juce::JUCEApplicationBase::isStandaloneApp()) juce::JUCEApplicationBase::quit();
    }
}
