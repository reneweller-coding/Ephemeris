/**
 * @file EditorPerform.cpp
 * @brief The perform page (EditorPerform.h).
 */
#include "EditorPerform.h"
#include "EditorTheme.h"
#include "PluginEditor.h"

using namespace eph;

namespace {
const juce::Colour kInk = ephui::colour::ink;   ///< text: parchment
const juce::Colour kDim = ephui::colour::dim;   ///< names, secondary text
const juce::Colour kAccent = ephui::colour::amber;   ///< the sun: the accent
}

PerformPage::PerformPage(EphemerisProcessor& p, std::unique_ptr<ParamPage> params) : proc_(p), params_(std::move(params))
{
    addAndMakeVisible(*params_);
    const ParamStore& s = proc_.store();
    for (int index : { perform::Filter, perform::Transpose, perform::Hold, perform::Throw }) {
        const int id = s.id(Module::Perform, 0, index);
        ids_.push_back(id);
        auto* label = bindings_.add(new juce::Label());
        label->setColour(juce::Label::textColourId, kInk);
        addAndMakeVisible(label);
        auto* button = learn_.add(new juce::TextButton("Learn"));
        button->setTooltip("Binds the next MIDI controller that moves to " + juce::String(s.desc(id).name));
        button->onClick = [this, id] { proc_.learn(proc_.learning() == id ? -1 : id); timerCallback(); };
        addAndMakeVisible(button);
    }
    timerCallback();
    startTimerHz(5);
}

PerformPage::~PerformPage() { stopTimer(); }

void PerformPage::timerCallback()
{
    // The headset's box: shown while one sends (or always, as the settings say), its hands live.
    const bool hs = proc_.headset().shown(frame::Settings::of("Ephemeris").headset());
    if (hs != headset_) { headset_ = hs; resized(); repaint(); }
    if (headset_) repaint(headsetArea_);
    const ParamStore& s = proc_.store();
    for (size_t i = 0; i < ids_.size(); ++i) {
        const int id = ids_[i];
        const int cc = proc_.controllerFor(id);
        const bool learning = proc_.learning() == id;
        juce::String text = juce::String(s.desc(id).name) + ": ";
        if (learning) text << "move a controller ...";
        else if (cc >= 0) text << "CC " << cc;
        else text << "not bound";
        if (id == s.id(Module::Perform, 0, perform::Transpose)) text << "  (and the keys)";
        bindings_[static_cast<int>(i)]->setText(text, juce::dontSendNotification);
        bindings_[static_cast<int>(i)]->setColour(juce::Label::textColourId, learning ? kAccent : kInk);
        learn_[static_cast<int>(i)]->setButtonText(learning ? "Cancel" : "Learn");
    }
}

void PerformPage::resized()
{
    auto r = getLocalBounds();
    controlsHeight_ = params_->heightFor(r.getWidth());   // the controls' group as tall as it draws itself
    params_->setBounds(r.removeFromTop(controlsHeight_));
    r.removeFromTop(10);
    r = r.reduced(16, 0);
    for (int i = 0; i < bindings_.size(); ++i) {
        auto row = r.removeFromTop(30);
        learn_[i]->setBounds(row.removeFromLeft(90).reduced(0, 3));
        row.removeFromLeft(12);
        bindings_[i]->setBounds(row.withWidth(std::min(row.getWidth(), 420)));
    }
    headsetArea_ = headset_ ? juce::Rectangle<int>(r.getRight() - 380, getLocalBounds().getY() + controlsHeight_ + 10, 380, 190) : juce::Rectangle<int>();
}

void PerformPage::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().reduced(16, 0);
    r.removeFromTop(controlsHeight_ + 10 + 30 * bindings_.size() + 12);
    g.setColour(kDim);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawFittedText("A MIDI keyboard's keys transpose the rows by their distance from middle C (C3 plays as composed); "
                     "the transposition holds until the next key. With Keyboard Plays on a voice the keys play that voice "
                     "instead (Replace leaves out its composed notes, Layer plays over them; Composer off leaves out every "
                     "composed note). Controller 74 (brightness) grabs the rows' filters, the "
                     "expression pedal throws the echo, the sustain pedal holds the composed moves on the knobs -- as in every "
                     "generator. Every control can be learned for any controller (a right click on it); the bindings are "
                     "saved with the set.",
                     r.removeFromTop(80).withTrimmedRight(headset_ ? 400 : 0), juce::Justification::topLeft, 5);
    if (headset_ && !headsetArea_.isEmpty())   // the headset (the frame): what the hands do, and how they stand now
        frame::drawHeadsetBox(g, headsetArea_, ephui::skin(), proc_.headset(), "hold the moves / let go", {});
}
