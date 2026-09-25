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
#include "eph/compose/Composer.h"
#include "eph/Presets.h"
#include <map>
#include <cstdlib>

using namespace eph;

namespace {

const juce::Colour kBack(0xff15171c), kPanel(0xff1e2129), kInk(0xffd8d4c8), kDim(0xff8a8f99), kAccent(0xffc9a45c);

/** @brief Colour of a section by the first word of its marker. */
juce::Colour sectionColour(const juce::String& name)
{
    const juce::String n = name.fromLastOccurrenceOf(": ", false, false);
    if (n.startsWith("Atmo") || n.startsWith("Ausklang")) return juce::Colour(0xff2d3a4f);
    if (n.startsWith("Einsatz")) return juce::Colour(0xff3b4a3a);
    if (n.startsWith("Aufbau")) return juce::Colour(0xff4d5a33);
    if (n.startsWith("Lead")) return juce::Colour(0xff6a5a2e);
    if (n.startsWith("Hoehepunkt")) return juce::Colour(0xff7a4a2a);
    if (n.startsWith("Abbau")) return juce::Colour(0xff4a3a4f);
    if (n.startsWith("Bruecke")) return juce::Colour(0xff34405a);
    return juce::Colour(0xff333844);
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

void ParamPage::build()
{
    sliders_.clear();
    combos_.clear();
    buttons_.clear();
    controls_.clear();
    labels_.clear();
    const int inst = instances_ > 1 ? instance_.getSelectedId() - 1 : 0;
    ParamStore& s = proc_.store();
    for (const auto& g : groups_) {
        const int count = ParamStore::moduleCount(g.first);
        const int instance = instances_ > 1 ? inst : g.second;
        for (int i = 0; i < count; ++i) {
            const int id = s.id(g.first, instance, i);
            StoreParameter* param = proc_.parameter(id);
            if (param == nullptr) continue;
            const ParamDesc& d = s.desc(id);
            auto* label = labels_.add(new juce::Label({}, d.name));
            label->setJustificationType(juce::Justification::centred);
            label->setColour(juce::Label::textColourId, kDim);
            label->setFont(juce::FontOptions(12.0f));
            addAndMakeVisible(label);
            if (d.curve == Curve::Choice && d.choices != nullptr) {
                auto* box = new juce::ComboBox();
                for (int c = 0; c <= static_cast<int>(d.maxValue); ++c) box->addItem(d.choices[c], c + 1);
                controls_.add(box);
                combos_.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(*param, *box));
            } else if (d.curve == Curve::Toggle) {
                auto* b = new juce::ToggleButton();
                controls_.add(b);
                buttons_.push_back(std::make_unique<juce::ButtonParameterAttachment>(*param, *b));
            } else {
                auto* sl = new juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 18);
                sl->setColour(juce::Slider::rotarySliderFillColourId, kAccent);
                sl->setTextValueSuffix(d.unit[0] != 0 ? juce::String(" ") + d.unit : juce::String());
                controls_.add(sl);
                sliders_.push_back(std::make_unique<juce::SliderParameterAttachment>(*param, *sl));
            }
            addAndMakeVisible(controls_.getLast());
        }
    }
}

int ParamPage::heightFor(int width) const
{
    const int perRow = std::max(1, (width - 20) / 104);
    const int rows = (controls_.size() + perRow - 1) / perRow;
    return 20 + (instances_ > 1 || presetCount_ > 0 || pagePresets_ ? 32 : 6) + rows * 96;
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
    const int w = 104, h = 96;
    const int perRow = std::max(1, area.getWidth() / w);
    for (int i = 0; i < controls_.size(); ++i) {
        const int x = area.getX() + (i % perRow) * w, y = area.getY() + (i / perRow) * h;
        labels_[i]->setBounds(x, y, w - 4, 16);
        auto* c = controls_[i];
        if (dynamic_cast<juce::Slider*>(c) != nullptr) c->setBounds(x + 4, y + 16, w - 12, h - 20);
        else c->setBounds(x + 4, y + 36, w - 12, 24);
    }
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

EphemerisEditor::EphemerisEditor(EphemerisProcessor& p) : juce::AudioProcessorEditor(p), proc_(p), arrange_(p)
{
    ParamStore& s = proc_.store();
    title_.setText("EPHEMERIS", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, kAccent);
    addAndMakeVisible(title_);

    auto combo = [&](juce::ComboBox& box, int id) {
        const ParamDesc& d = s.desc(id);
        for (int c = 0; c <= static_cast<int>(d.maxValue); ++c) box.addItem(d.choices[c], c + 1);
        combos_.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(*proc_.parameter(id), box));
        addAndMakeVisible(box);
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
        addAndMakeVisible(sl);
        addAndMakeVisible(label);
    };
    slider(minutes_, minutesLabel_, "Piece min", s.id(Module::Compose, 0, compose::PieceMinutes));
    slider(concert_, concertLabel_, "Concert min", s.id(Module::Compose, 0, compose::ConcertMinutes));
    nightAttach_ = std::make_unique<juce::ButtonParameterAttachment>(*proc_.parameter(s.id(Module::Compose, 0, compose::NightSet)), night_);
    night_.setColour(juce::ToggleButton::textColourId, kDim);
    night_.setTooltip("Night set: the concert's pieces in mixed styles along waves of energy, each mixed into the next "
                      "as a DJ does it (up to 12 hours)");
    addAndMakeVisible(night_);

    compose_.onClick = [this] { proc_.compose(); };
    seed_.onClick = [this] { proc_.newSeed(); };
    play_.onClick = [this] { proc_.setPlaying(!proc_.isPlaying()); };
    for (auto* b : { &compose_, &seed_, &play_, &save_, &load_, &export_ }) addAndMakeVisible(b);
    // Mute, as in Phosphene: silence at the output; EPH_MUTE (or the screenshot mode) holds it on.
    mute_.setClickingTogglesState(true);
    mute_.setToggleState(proc_.muted(), juce::dontSendNotification);
    mute_.setEnabled(!proc_.muteForced());
    mute_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb04a4a));
    mute_.setTooltip(proc_.muteForced() ? "Muted by EPH_MUTE: an automated run makes no sound" : "Silence the output");
    mute_.onClick = [this] { proc_.setMuted(mute_.getToggleState()); };
    addAndMakeVisible(mute_);
    play_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a4a36));

    for (const char* unit : kUnitNames) {
        auto* b = rerollButtons_.add(new juce::TextButton(juce::String("reroll ") + unit));
        const juce::String u(unit);
        b->onClick = [this, u] { proc_.reroll(u); };
        addAndMakeVisible(b);
    }
    rerolls_.setColour(juce::Label::textColourId, kDim);
    status_.setColour(juce::Label::textColourId, kInk);
    addAndMakeVisible(rerolls_);
    addAndMakeVisible(status_);

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

    addAndMakeVisible(arrange_);
    using M = Module;
    auto page = [&](const char* name, std::vector<std::pair<M, int>> groups, int instances) {
        tabs_.addTab(name, kPanel, new ParamPage(proc_, std::move(groups), instances), true);
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
    addAndMakeVisible(tabs_);

    setResizable(true, true);
    setResizeLimits(960, 640, 2400, 1600);
    setSize(1180, 760);

    if (const char* shot = std::getenv("EPH_SHOT")) {
        shotPath_ = shot;
        if (const char* tab = std::getenv("EPH_TAB")) tabs_.setCurrentTabIndex(juce::String(tab).getIntValue());
    }
    startTimerHz(15);
}

EphemerisEditor::~EphemerisEditor() { stopTimer(); }

void EphemerisEditor::paint(juce::Graphics& g) { g.fillAll(kBack); }

void EphemerisEditor::resized()
{
    auto area = getLocalBounds().reduced(10);
    auto top = area.removeFromTop(34);
    title_.setBounds(top.removeFromLeft(130));
    style_.setBounds(top.removeFromLeft(110).reduced(3));
    key_.setBounds(top.removeFromLeft(64).reduced(3));
    scale_.setBounds(top.removeFromLeft(120).reduced(3));
    minutesLabel_.setBounds(top.removeFromLeft(66));
    minutes_.setBounds(top.removeFromLeft(120).reduced(2));
    concertLabel_.setBounds(top.removeFromLeft(84));
    concert_.setBounds(top.removeFromLeft(120).reduced(2));
    night_.setBounds(top.removeFromLeft(58).reduced(2));
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
