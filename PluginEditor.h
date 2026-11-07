#pragma once

// =============================================================================
//  PluginEditor.h  -  UPFRONT's interface (spectrum, buttons, knobs)
// =============================================================================
#include "PluginProcessor.h"

//==============================================================================
class UpfrontLookAndFeel : public juce::LookAndFeel_V4
{
public:
    UpfrontLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

//==============================================================================
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (UpfrontProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateLive();   // call from timer

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float xForFreq (double f) const;
    double freqForX (float x) const;
    float yForEqDb (float db) const;
    float eqDbForY (float y) const;
    float yForSpecDb (float db) const;
    int hitNode (juce::Point<float>) const;
    juce::Point<float> nodePos (int band) const;

    UpfrontProcessor& proc;
    juce::Rectangle<float> plot;

    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> fftData, window, liveDb;   // liveDb per pixel column
    std::vector<double> colFreqs;

    int hovered = -1, dragging = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};

//==============================================================================
class UpfrontEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit UpfrontEditor (UpfrontProcessor&);
    ~UpfrontEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name);

    UpfrontProcessor& proc;
    UpfrontLookAndFeel lnf;
    SpectrumView spectrum;

    juce::TextButton analyzeButton { "ANALYZE" }, balanceButton { "BALANCE" };
    juce::Slider amountKnob, tiltKnob, bandsKnob, hpKnob, driveKnob, outputKnob, listenSlider;
    juce::Label amountLabel, tiltLabel, bandsLabel, hpLabel, driveLabel, outputLabel, listenLabel;
    juce::ToggleButton hpToggle { "LOW CUT" }, infToggle { juce::String::fromUTF8 ("\xe2\x88\x9e") },
                       dynamicToggle { "DYNAMIC" }, analogToggle { "ANALOG" };

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> amountAtt, tiltAtt, bandsAtt, hpAtt, driveAtt, outputAtt, listenAtt;
    std::unique_ptr<BA> hpOnAtt, infAtt, dynamicAtt, analogAtt;

    juce::Rectangle<int> readoutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpfrontEditor)
};
