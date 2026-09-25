/**
 * @file PluginEditor.cpp
 * @brief The plugin's panel.
 */
#include "PluginEditor.h"
#include "EditorMixer.h"
#include "EditorOrrery.h"
#include "EditorPerform.h"
#include "eph/compose/Composer.h"
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
    build();
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

void ParamPage::resized()
{
    auto area = getLocalBounds().reduced(10);
    if (instances_ > 1) instance_.setBounds(area.removeFromTop(26).removeFromLeft(90));
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

void ArrangeView::paint(juce::Graphics& g)
{
    g.fillAll(kPanel);
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
            g.drawFittedText(name.fromLastOccurrenceOf(": ", false, false), juce::Rectangle<int>(static_cast<int>(x0) + 4, 8, static_cast<int>(x1 - x0) - 8, static_cast<int>(h) - 16),
                             juce::Justification::topLeft, 3);
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

    compose_.onClick = [this] { proc_.compose(); };
    seed_.onClick = [this] { proc_.newSeed(); };
    play_.onClick = [this] { proc_.setPlaying(!proc_.isPlaying()); };
    for (auto* b : { &compose_, &seed_, &play_, &save_, &load_, &export_ }) addAndMakeVisible(b);
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
        documents.createDirectory();
        chooser_ = std::make_unique<juce::FileChooser>("Export", documents.getChildFile("ephemeris.wav"), "*.wav");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) proc_.exportTo(fc.getResult().withFileExtension(".wav")); });
    };

    addAndMakeVisible(arrange_);
    using M = Module;
    auto page = [&](const char* name, std::vector<std::pair<M, int>> groups, int instances) {
        tabs_.addTab(name, kPanel, new ParamPage(proc_, std::move(groups), instances), true);
    };
    tabs_.addTab("Mixer", kPanel, new MixerConsole(proc_), true);
    tabs_.addTab("Perform", kPanel, new PerformPage(proc_, std::make_unique<ParamPage>(proc_, std::vector<std::pair<M, int>>{ { M::Perform, 0 } }, 1)), true);
    tabs_.addTab("Rack", kPanel, new RackPage(proc_, std::make_unique<ParamPage>(proc_, std::vector<std::pair<M, int>>{ { M::Row, 0 } }, kRows)), true);
    page("Voices", { { M::Voice, 0 } }, kRows);
    page("Lead", { { M::Lead, 0 } }, 1);
    page("Drone", { { M::Drone, 0 } }, 1);
    page("Tape Keys", { { M::Tape, 0 } }, 1);
    page("Strings", { { M::Strings, 0 } }, 1);
    page("Atmosphere", { { M::Atmos, 0 } }, 1);
    page("Echo + Spring", { { M::Echo, 0 }, { M::Spring, 0 } }, 1);
    page("Hall", { { M::Reverb, 0 } }, 1);
    page("Drums", { { M::Drums, 0 } }, 1);
    page("Master", { { M::Master, 0 }, { M::Compose, 0 } }, 1);
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
    title_.setBounds(top.removeFromLeft(150));
    style_.setBounds(top.removeFromLeft(110).reduced(3));
    key_.setBounds(top.removeFromLeft(64).reduced(3));
    scale_.setBounds(top.removeFromLeft(150).reduced(3));
    minutesLabel_.setBounds(top.removeFromLeft(70));
    minutes_.setBounds(top.removeFromLeft(150).reduced(2));
    concertLabel_.setBounds(top.removeFromLeft(80));
    concert_.setBounds(top.removeFromLeft(140).reduced(2));
    play_.setBounds(top.removeFromRight(80).reduced(3));
    seed_.setBounds(top.removeFromRight(90).reduced(3));
    compose_.setBounds(top.removeFromRight(100).reduced(3));
    area.removeFromTop(6);
    auto second = area.removeFromTop(28);
    for (auto* b : rerollButtons_) b->setBounds(second.removeFromLeft(92).reduced(2));
    export_.setBounds(second.removeFromRight(140).reduced(2));
    load_.setBounds(second.removeFromRight(80).reduced(2));
    save_.setBounds(second.removeFromRight(80).reduced(2));
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
