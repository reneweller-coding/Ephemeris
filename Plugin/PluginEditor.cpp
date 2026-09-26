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

void ArrangeView::rebuild()
{
    Score s;
    proc_.copyScore(s);
    lanes_.assign(static_cast<size_t>(kLanes * kBins), 0.0f);
    if (s.lengthBeats <= 0.0) return;
    std::vector<uint8_t> rows(static_cast<size_t>(kBins), 0);   // a bit per row playing in the column
    for (const NoteEvent& n : s.notes) {
        int lane = -1;
        const int part = static_cast<int>(n.part);
        if (part < kRows) lane = 0;
        else if (n.part == Part::Lead) lane = 1;
        else if (n.part == Part::TapeKeys) lane = 2;
        else if (n.part == Part::Strings || n.part == Part::Pad) lane = 3;
        else if (n.part == Part::Drone) lane = 4;
        else if (n.part == Part::Drums) lane = 5;
        if (lane < 0) continue;
        const int b0 = std::clamp(static_cast<int>(n.beat / s.lengthBeats * kBins), 0, kBins - 1);
        const int b1 = std::clamp(static_cast<int>((n.beat + std::max(n.length, 0.25)) / s.lengthBeats * kBins), b0, kBins - 1);
        for (int b = b0; b <= b1; ++b) {
            if (lane == 0) rows[static_cast<size_t>(b)] = static_cast<uint8_t>(rows[static_cast<size_t>(b)] | (1u << part));
            else lanes_[static_cast<size_t>(lane * kBins + b)] = 1.0f;
        }
    }
    for (int b = 0; b < kBins; ++b) {
        int count = 0;
        for (uint8_t v = rows[static_cast<size_t>(b)]; v != 0; v &= static_cast<uint8_t>(v - 1)) ++count;
        lanes_[static_cast<size_t>(b)] = count == 0 ? 0.0f : 0.35f + 0.65f * static_cast<float>(count) / static_cast<float>(kRows);
    }
}

void ArrangeView::paint(juce::Graphics& g)
{
    g.fillAll(kPanel);
    if (proc_.scoreVersion() != version_) { version_ = proc_.scoreVersion(); rebuild(); }
    std::vector<Marker> markers;
    double beats = 0.0, secs = 0.0;
    proc_.arrangement(markers, beats, secs);
    if (beats <= 0.0) return;
    const float w = static_cast<float>(getWidth()), h = static_cast<float>(getHeight());
    for (size_t i = 0; i < markers.size(); ++i) {
        const double b0 = markers[i].beat, b1 = i + 1 < markers.size() ? markers[i + 1].beat : beats;
        const float x0 = static_cast<float>(b0 / beats) * w, x1 = static_cast<float>(b1 / beats) * w;
        const juce::String name(markers[i].text);
        g.setColour(sectionColour(name));
        g.fillRect(x0 + 1.0f, 6.0f, std::max(1.0f, x1 - x0 - 2.0f), h - 12.0f);
        g.setColour(kInk);
        g.setFont(juce::FontOptions(11.0f));
        if (x1 - x0 > 30.0f)
            g.drawFittedText(name.fromLastOccurrenceOf(": ", false, false), juce::Rectangle<int>(static_cast<int>(x0) + 4, 8, static_cast<int>(x1 - x0) - 8, 16),
                             juce::Justification::topLeft, 1);
    }
    // The instrumentation matrix under the names: a lane per layer, lit where it has notes.
    const float top = 26.0f, laneH = (h - 12.0f - top) / static_cast<float>(kLanes);
    if (!lanes_.empty() && laneH > 3.0f) {
        const float bw = w / static_cast<float>(kBins);
        for (int l = 0; l < kLanes; ++l) {
            const float y = top + laneH * static_cast<float>(l);
            for (int b = 0; b < kBins; ++b) {
                const float a = lanes_[static_cast<size_t>(l * kBins + b)];
                if (a <= 0.0f) continue;
                g.setColour(kInk.withAlpha(0.55f * a));
                g.fillRect(bw * static_cast<float>(b), y + 1.0f, bw + 0.5f, laneH - 2.0f);
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
    const float x = static_cast<float>(proc_.positionBeats() / beats) * w;
    g.setColour(kAccent);
    g.fillRect(x - 1.0f, 0.0f, 2.0f, h);
}

void ArrangeView::mouseDown(const juce::MouseEvent& e)
{
    std::vector<Marker> markers;
    double beats = 0.0, secs = 0.0;
    proc_.arrangement(markers, beats, secs);
    proc_.seekTo(beats * e.position.x / std::max(1.0f, static_cast<float>(getWidth())));
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
    auto slider = [&](juce::Slider& sl, juce::Label& label, const char* text, int id) {
        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        sliders_.push_back(std::make_unique<juce::SliderParameterAttachment>(*proc_.parameter(id), sl));
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, kDim);
        body_.addAndMakeVisible(sl);
        body_.addAndMakeVisible(label);
    };
    slider(minutes_, minutesLabel_, "Piece min", s.id(Module::Compose, 0, compose::PieceMinutes));
    slider(concert_, concertLabel_, "Concert min", s.id(Module::Compose, 0, compose::ConcertMinutes));
    nightAttach_ = std::make_unique<juce::ButtonParameterAttachment>(*proc_.parameter(s.id(Module::Compose, 0, compose::NightSet)), night_);
    night_.setColour(juce::ToggleButton::textColourId, kDim);
    night_.setTooltip("Night set: the concert's pieces in mixed styles along waves of energy, each mixed into the next "
                      "as a DJ does it (up to 12 hours)");
    body_.addAndMakeVisible(night_);

    compose_.onClick = [this] { proc_.compose(); };
    seed_.onClick = [this] { proc_.newSeed(); };
    play_.onClick = [this] { proc_.setPlaying(!proc_.isPlaying()); };
    for (auto* b : { &compose_, &seed_, &play_, &save_, &load_, &export_ }) body_.addAndMakeVisible(b);
    // Mute, as in Phosphene: silence at the output; EPH_MUTE (or the screenshot mode) holds it on.
    mute_.setClickingTogglesState(true);
    mute_.setToggleState(proc_.muted(), juce::dontSendNotification);
    mute_.setEnabled(!proc_.muteForced());
    mute_.setColour(juce::TextButton::buttonOnColourId, ephui::colour::red.withAlpha(0.55f));
    mute_.setTooltip(proc_.muteForced() ? "Muted by EPH_MUTE: an automated run makes no sound" : "Silence the output");
    mute_.onClick = [this] { proc_.setMuted(mute_.getToggleState()); };
    body_.addAndMakeVisible(mute_);
    play_.setColour(juce::TextButton::buttonColourId, kAccent.withAlpha(0.22f));
    compose_.setColour(juce::TextButton::buttonColourId, kAccent.withAlpha(0.14f));
    // The standalone's full screen (F11), as Phosphene's; hidden in a host, which owns its window.
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

void EphemerisEditor::layoutBody()
{
    auto area = body_.getLocalBounds().reduced(10);
    auto top = area.removeFromTop(34);
    logo_ = top.removeFromLeft(34).toFloat().reduced(2.0f);
    title_.setBounds(top.removeFromLeft(112));
    style_.setBounds(top.removeFromLeft(110).reduced(3));
    key_.setBounds(top.removeFromLeft(64).reduced(3));
    scale_.setBounds(top.removeFromLeft(120).reduced(3));
    minutesLabel_.setBounds(top.removeFromLeft(66));
    minutes_.setBounds(top.removeFromLeft(120).reduced(2));
    concertLabel_.setBounds(top.removeFromLeft(84));
    concert_.setBounds(top.removeFromLeft(106).reduced(2));
    night_.setBounds(top.removeFromLeft(72).reduced(2));
    play_.setBounds(top.removeFromRight(80).reduced(3));
    seed_.setBounds(top.removeFromRight(90).reduced(3));
    compose_.setBounds(top.removeFromRight(100).reduced(3));
    area.removeFromTop(6);
    auto second = area.removeFromTop(28);
    for (auto* b : rerollButtons_) b->setBounds(second.removeFromLeft(82).reduced(2));
    export_.setBounds(second.removeFromRight(140).reduced(2));
    load_.setBounds(second.removeFromRight(80).reduced(2));
    save_.setBounds(second.removeFromRight(80).reduced(2));
    second.removeFromRight(8);
    mute_.setBounds(second.removeFromRight(proc_.muteForced() ? 100 : 70).reduced(2));
    area.removeFromTop(4);
    auto third = area.removeFromTop(20);
    if (full_.isVisible()) full_.setBounds(third.removeFromRight(100));
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
    play_.setButtonText(proc_.isPlaying() ? "Stop" : "Play");
    mute_.setToggleState(proc_.muted(), juce::dontSendNotification);
    mute_.setButtonText(proc_.muted() ? (proc_.muteForced() ? "Muted (env)" : "Muted") : "Mute");
    compose_.setEnabled(!proc_.isComposing());
    arrange_.repaint();
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
