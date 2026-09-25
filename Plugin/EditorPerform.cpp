/**
 * @file EditorPerform.cpp
 * @brief The perform page (EditorPerform.h).
 */
#include "EditorPerform.h"
#include "EditorTheme.h"
#include "PluginEditor.h"

using namespace eph;

namespace {
const juce::Colour kInk = ephui::colour::ink, kDim = ephui::colour::dim, kAccent = ephui::colour::amber;
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
        bindings_[i]->setBounds(row);
    }
}

void PerformPage::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().reduced(16, 0);
    r.removeFromTop(controlsHeight_ + 10 + 30 * bindings_.size() + 12);
    g.setColour(kDim);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawFittedText("A MIDI keyboard's keys transpose the rows by their distance from middle C (C3 plays as composed); "
                     "the transposition holds until the next key. The mod wheel grabs the rows' filters, the expression "
                     "pedal throws the echo, the sustain pedal holds the hands on the knobs. Every control can be learned "
                     "for any controller; the bindings are saved with the set.",
                     r.removeFromTop(80), juce::Justification::topLeft, 4);
}
