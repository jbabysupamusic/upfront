#include "PluginEditor.h"

using namespace upfront;

namespace colours
{
const juce::Colour bg      { 0xff0d0f13 };
const juce::Colour panel   { 0xff161a21 };
const juce::Colour grid    { 0xff242a33 };
const juce::Colour text    { 0xffe8eaed };
const juce::Colour dim     { 0xff7d8590 };
const juce::Colour accent  { 0xffff7a1a };   // EQ curve / nodes
const juce::Colour live    { 0xff4a90e2 };   // live spectrum
const juce::Colour learned { 0xff3fd0c9 };   // analysed spectrum
}

static juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
// Look and feel
//==============================================================================
UpfrontLookAndFeel::UpfrontLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, colours::dim);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::text);
    setColour (juce::ToggleButton::textColourId, colours::text);
}

void UpfrontLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float a0, float a1, juce::Slider&)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c = r.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const float thick = 4.0f;

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, a0, a1, true);
    g.setColour (colours::grid);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, radius - thick, radius - thick, 0.0f, a0, angle, true);
    g.setColour (colours::accent);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (colours::panel.brighter (0.15f));
    g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (c));
    juce::Path tick;
    tick.addRoundedRectangle (-1.5f, -radius * 0.6f, 3.0f, radius * 0.35f, 1.5f);
    g.setColour (colours::text);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (c));
}

void UpfrontLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                               bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    auto base = b.findColour (juce::TextButton::buttonColourId);
    if (down) base = base.darker (0.2f);
    else if (highlighted) base = base.brighter (0.1f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (base.brighter (0.3f).withAlpha (0.6f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void UpfrontLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? colours::accent.withAlpha (0.25f) : colours::panel.brighter (highlighted ? 0.12f : 0.06f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? colours::accent : colours::grid.brighter (0.3f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (on ? colours::text : colours::dim);
    g.setFont (uiFont (12.0f, true));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

//==============================================================================
// Spectrum view
//==============================================================================
SpectrumView::SpectrumView (UpfrontProcessor& p) : proc (p)
{
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
}

void SpectrumView::resized()
{
    plot = getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (14.0f).withTrimmedLeft (22.0f);
    const int cols = juce::jmax (2, (int) plot.getWidth());
    colFreqs.resize ((size_t) cols);
    for (int i = 0; i < cols; ++i) colFreqs[(size_t) i] = freqForX (plot.getX() + (float) i);
    liveDb.assign ((size_t) cols, -120.0f);
}

float SpectrumView::xForFreq (double f) const
{
    return plot.getX() + plot.getWidth() * (float) (std::log (f / kMinF) / std::log (kMaxF / kMinF));
}
double SpectrumView::freqForX (float x) const
{
    return kMinF * std::pow (kMaxF / kMinF, (double) ((x - plot.getX()) / plot.getWidth()));
}
float SpectrumView::yForEqDb (float db) const { return plot.getCentreY() - db / 18.0f * plot.getHeight() * 0.5f; }
float SpectrumView::eqDbForY (float y) const  { return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * 18.0f; }
float SpectrumView::yForSpecDb (float db) const
{
    return juce::jmap (juce::jlimit (-96.0f, 0.0f, db), -96.0f, 0.0f, plot.getBottom(), plot.getY());
}

juce::Point<float> SpectrumView::nodePos (int b) const
{
    const float f = proc.apvts.getRawParameterValue (bandParamId (b, "freq"))->load();
    const float g = proc.apvts.getRawParameterValue (bandParamId (b, "gain"))->load();
    return { xForFreq (f), yForEqDb (g) };
}

int SpectrumView::hitNode (juce::Point<float> p) const
{
    const int active = (int) proc.apvts.getRawParameterValue ("bands")->load();
    int best = -1;
    float bestD = 10.0f;
    for (int b = 0; b < active; ++b)
    {
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void SpectrumView::updateLive()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    proc.copyDisplaySamples (fftData.data(), fftSize);
    for (int i = 0; i < fftSize; ++i) fftData[(size_t) i] *= window[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double sr = proc.getCurrentSampleRate();
    const double binHz = sr / fftSize;
    for (size_t c = 0; c < colFreqs.size(); ++c)
    {
        // max of the bins covered by this column (or interpolated at low frequencies)
        const double f0 = colFreqs[c], f1 = c + 1 < colFreqs.size() ? colFreqs[c + 1] : f0;
        int k0 = (int) (f0 / binHz), k1 = juce::jmax (k0, (int) (f1 / binHz));
        k1 = juce::jmin (k1, fftSize / 2 - 1);
        float m = 0.0f;
        for (int k = k0; k <= k1; ++k) m = juce::jmax (m, fftData[(size_t) k]);
        float db = juce::Decibels::gainToDecibels (m * 4.0f / fftSize, -120.0f);
        db += 4.5f * (float) std::log2 (f0 / 1000.0);   // +4.5 dB/oct display slope (common mix view)
        auto& v = liveDb[c];
        v = db > v ? db : v * 0.85f + db * 0.15f;       // fast attack, slow release
    }
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // grid
    g.setFont (uiFont (10.0f));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xForFreq (f);
        g.setColour (colours::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (colours::dim);
        const juce::String t = f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f);
        g.drawText (t, juce::Rectangle<float> (x - 20, plot.getBottom() + 1, 40, 12), juce::Justification::centred);
    }
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yForEqDb ((float) db);
        g.setColour (db == 0 ? colours::grid.brighter (0.25f) : colours::grid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (colours::dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (plot.getX() - 24, y - 6, 20, 12),
                    juce::Justification::centredRight);
    }

    // live spectrum
    {
        juce::Path p;
        p.startNewSubPath (plot.getX(), plot.getBottom());
        for (size_t c = 0; c < liveDb.size(); ++c)
            p.lineTo (plot.getX() + (float) c, yForSpecDb (liveDb[c]));
        p.lineTo (plot.getRight(), plot.getBottom());
        p.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colours::live.withAlpha (0.35f), 0, plot.getY(),
                                                 colours::live.withAlpha (0.05f), 0, plot.getBottom(), false));
        g.fillPath (p);
    }

    // analysed spectrum (normalised so its peak sits near the top)
    if (proc.hasAnalysis())
    {
        auto spec = proc.getAnalysedSpectrum();
        float mx = -200.0f;
        for (size_t i = 0; i < spec.size(); ++i)
        {
            spec[i] += 4.5f * (float) std::log2 (logPointFreq ((int) i) / 1000.0);
            mx = juce::jmax (mx, spec[i]);
        }
        juce::Path p;
        bool started = false;
        for (int i = 0; i < kLogPoints; ++i)
        {
            const float x = xForFreq (logPointFreq (i)), y = yForSpecDb (spec[(size_t) i] - mx - 6.0f);
            if (! started) { p.startNewSubPath (x, y); started = true; }
            else p.lineTo (x, y);
        }
        g.setColour (colours::learned.withAlpha (0.7f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    // harmonic markers + EQ curve
    const int active = (int) proc.apvts.getRawParameterValue ("bands")->load();
    const float f0 = proc.apvts.getRawParameterValue ("f0")->load();
    for (int b = 0; b < active; ++b)
    {
        const float x = nodePos (b).x;
        g.setColour (colours::accent.withAlpha (0.12f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }

    std::vector<float> resp;
    proc.getEqResponseDb (colFreqs, resp, false);
    juce::Path curve;
    for (size_t c = 0; c < resp.size(); ++c)
    {
        const float y = juce::jlimit (plot.getY(), plot.getBottom(), yForEqDb (resp[c]));
        if (c == 0) curve.startNewSubPath (plot.getX(), y);
        else curve.lineTo (plot.getX() + (float) c, y);
    }
    juce::Path fill (curve);
    fill.lineTo (plot.getRight(), plot.getCentreY());
    fill.lineTo (plot.getX(), plot.getCentreY());
    fill.closeSubPath();
    g.setColour (colours::accent.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (colours::accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // Dynamic mode: show what the EQ is doing right now
    if (proc.apvts.getRawParameterValue ("dynamic")->load() > 0.5f)
    {
        std::vector<float> liveResp;
        proc.getEqResponseDb (colFreqs, liveResp, true);
        juce::Path lp;
        for (size_t c = 0; c < liveResp.size(); ++c)
        {
            const float y = juce::jlimit (plot.getY(), plot.getBottom(), yForEqDb (liveResp[c]));
            if (c == 0) lp.startNewSubPath (plot.getX(), y); else lp.lineTo (plot.getX() + (float) c, y);
        }
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.strokePath (lp, juce::PathStrokeType (1.3f));
    }

    // nodes
    for (int b = 0; b < active; ++b)
    {
        const auto p = nodePos (b);
        const bool hot = (b == hovered || b == dragging);
        const float r = hot ? 7.0f : 5.5f;
        g.setColour (colours::bg);
        g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
        g.setColour (b == 0 ? juce::Colours::white : colours::accent);
        g.drawEllipse (p.x - r, p.y - r, r * 2, r * 2, 2.0f);

        const float f = proc.apvts.getRawParameterValue (bandParamId (b, "freq"))->load();
        const int n = f0 > 0 ? (int) std::lround (f / f0) : b + 1;
        g.setColour (colours::dim);
        g.setFont (uiFont (9.0f, true));
        g.drawText (n == 1 ? "F0" : juce::String (n), juce::Rectangle<float> (p.x - 12, plot.getY() + 2, 24, 10),
                    juce::Justification::centred);
    }

    // hover readout
    const int info = dragging >= 0 ? dragging : hovered;
    if (info >= 0)
    {
        const auto p = nodePos (info);
        const float f = proc.apvts.getRawParameterValue (bandParamId (info, "freq"))->load();
        const float gain = proc.apvts.getRawParameterValue (bandParamId (info, "gain"))->load();
        const float q = proc.apvts.getRawParameterValue (bandParamId (info, "q"))->load();
        const juce::String t = juce::String (f, f < 1000 ? 1 : 0) + " Hz  " + (gain >= 0 ? "+" : "")
                               + juce::String (gain, 1) + " dB  Q " + juce::String (q, 1);
        auto box = juce::Rectangle<float> (150, 20).withCentre ({ p.x, p.y - 20 });
        box = box.constrainedWithin (plot);
        g.setColour (colours::bg.withAlpha (0.9f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (colours::text);
        g.setFont (uiFont (11.0f));
        g.drawText (t, box, juce::Justification::centred);
    }
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hovered) { hovered = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void SpectrumView::mouseExit (const juce::MouseEvent&) { hovered = -1; repaint(); }

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitNode (e.position);
    if (dragging >= 0)
        if (auto* prm = proc.apvts.getParameter (bandParamId (dragging, "gain")))
            prm->beginChangeGesture();
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    if (auto* prm = proc.apvts.getParameter (bandParamId (dragging, "gain")))
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-18.0f, 18.0f, eqDbForY (e.position.y))));
    repaint();
}

void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        if (auto* prm = proc.apvts.getParameter (bandParamId (dragging, "gain")))
            prm->endChangeGesture();
    dragging = -1;
}

void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitNode (e.position);
    if (b < 0) return;
    if (auto* prm = proc.apvts.getParameter (bandParamId (b, "gain")))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (0.0f));
        prm->endChangeGesture();
    }
}

void SpectrumView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = hitNode (e.position);
    if (b < 0) return;
    if (auto* prm = proc.apvts.getParameter (bandParamId (b, "q")))
    {
        const float q = proc.apvts.getRawParameterValue (bandParamId (b, "q"))->load();
        const float nq = juce::jlimit (0.3f, 20.0f, q * std::pow (1.15f, w.deltaY * 4.0f));
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (nq));
        prm->endChangeGesture();
    }
}

//==============================================================================
// Editor
//==============================================================================
UpfrontEditor::UpfrontEditor (UpfrontProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (spectrum);

    analyzeButton.setColour (juce::TextButton::buttonColourId, colours::panel.brighter (0.1f));
    balanceButton.setColour (juce::TextButton::buttonColourId, colours::accent.darker (0.2f));
    analyzeButton.onClick = [this] { proc.toggleAnalyze(); };
    balanceButton.onClick = [this] { proc.balance(); spectrum.repaint(); };
    addAndMakeVisible (analyzeButton);
    addAndMakeVisible (balanceButton);

    setupKnob (amountKnob, amountLabel, "AMOUNT");
    setupKnob (tiltKnob, tiltLabel, "TILT");
    setupKnob (bandsKnob, bandsLabel, "BANDS");
    setupKnob (hpKnob, hpLabel, "LOW CUT");
    setupKnob (driveKnob, driveLabel, "DRIVE");
    setupKnob (outputKnob, outputLabel, "OUTPUT");
    hpLabel.setVisible (false);
    addAndMakeVisible (hpToggle);

    // listen time: drag the bar or double-click it and type a number (1-30 s), or press infinity
    listenSlider.setSliderStyle (juce::Slider::LinearBar);
    listenSlider.setTextValueSuffix (" s");
    listenSlider.setColour (juce::Slider::trackColourId, colours::learned.withAlpha (0.35f));
    listenSlider.setColour (juce::Slider::backgroundColourId, colours::panel.brighter (0.06f));
    listenSlider.setTooltip ("How long ANALYZE listens. Double-click to type.");
    addAndMakeVisible (listenSlider);
    listenLabel.setText ("LISTEN", juce::dontSendNotification);
    listenLabel.setFont (uiFont (11.0f, true));
    addAndMakeVisible (listenLabel);
    infToggle.setTooltip ("Listen until you click ANALYZE again");
    dynamicToggle.setTooltip ("Bands react to the music: tame harmonics when they jump out, fill them when they dip");
    analogToggle.setTooltip ("Warm, smooth analog saturation (8x oversampled, alias-free)");
    addAndMakeVisible (infToggle);
    addAndMakeVisible (dynamicToggle);
    addAndMakeVisible (analogToggle);

    amountAtt = std::make_unique<SA> (proc.apvts, "amount", amountKnob);
    tiltAtt   = std::make_unique<SA> (proc.apvts, "tilt", tiltKnob);
    bandsAtt  = std::make_unique<SA> (proc.apvts, "bands", bandsKnob);
    hpAtt     = std::make_unique<SA> (proc.apvts, "hpFreq", hpKnob);
    outputAtt = std::make_unique<SA> (proc.apvts, "output", outputKnob);
    hpOnAtt   = std::make_unique<BA> (proc.apvts, "hpOn", hpToggle);
    driveAtt  = std::make_unique<SA> (proc.apvts, "drive", driveKnob);
    listenAtt = std::make_unique<SA> (proc.apvts, "listenSec", listenSlider);
    infAtt    = std::make_unique<BA> (proc.apvts, "listenInf", infToggle);
    dynamicAtt= std::make_unique<BA> (proc.apvts, "dynamic", dynamicToggle);
    analogAtt = std::make_unique<BA> (proc.apvts, "analog", analogToggle);

    setResizable (true, true);
    setResizeLimits (860, 520, 1700, 1050);
    setSize (1000, 600);
    startTimerHz (30);
}

UpfrontEditor::~UpfrontEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void UpfrontEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (uiFont (11.0f, true));
    addAndMakeVisible (l);
}

void UpfrontEditor::timerCallback()
{
    if (proc.isListening())
    {
        const double t = proc.getSecondsHeard();
        const bool forever = proc.apvts.getRawParameterValue ("listenInf")->load() > 0.5f;
        juce::String txt = proc.isWaitingForSignal() ? "WAITING FOR AUDIO..."
                         : "LISTENING " + juce::String (t, 1) + " s"
                           + (forever ? juce::String() : " / " + juce::String (proc.apvts.getRawParameterValue ("listenSec")->load(), 1) + " s");
        analyzeButton.setButtonText (txt + "  (click to stop)");
        analyzeButton.setColour (juce::TextButton::buttonColourId, colours::learned.darker (0.6f));
    }
    else
    {
        analyzeButton.setButtonText ("ANALYZE");
        analyzeButton.setColour (juce::TextButton::buttonColourId, colours::panel.brighter (0.1f));
    }
    const bool has = proc.hasAnalysis();
    balanceButton.setEnabled (has);
    balanceButton.setAlpha (has ? 1.0f : 0.4f);
    listenSlider.setEnabled (proc.apvts.getRawParameterValue ("listenInf")->load() < 0.5f);
    listenSlider.setAlpha (listenSlider.isEnabled() ? 1.0f : 0.35f);
    const bool an = proc.apvts.getRawParameterValue ("analog")->load() > 0.5f;
    driveKnob.setAlpha (an ? 1.0f : 0.35f);
    driveLabel.setAlpha (an ? 1.0f : 0.35f);

    spectrum.updateLive();
    repaint (readoutArea);
    repaint (0, 0, getWidth(), 50);
}

void UpfrontEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);

    // header
    g.setColour (colours::text);
    g.setFont (uiFont (26.0f, true));
    g.drawText ("UPFRONT", 18, 10, 200, 32, juce::Justification::centredLeft);
    g.setColour (colours::accent);
    g.setFont (uiFont (11.0f, true));
    g.drawText ("HARMONIC EQ", 150, 18, 120, 18, juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (uiFont (12.0f));
    g.drawText (proc.getStatusText(), getWidth() - 520, 10, 502, 32, juce::Justification::centredRight);

    // readout
    auto r = readoutArea.toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    const float f0 = proc.apvts.getRawParameterValue ("f0")->load();
    const int bands = (int) proc.apvts.getRawParameterValue ("bands")->load();
    g.setColour (colours::dim);
    g.setFont (uiFont (10.0f, true));
    g.drawText ("FUNDAMENTAL", r.withHeight (24).translated (0, 8), juce::Justification::centred);
    g.setColour (proc.hasAnalysis() ? colours::text : colours::dim);
    g.setFont (uiFont (24.0f, true));
    g.drawText (juce::String (f0, 1) + " Hz", r.withHeight (30).translated (0, 30), juce::Justification::centred);
    g.setColour (colours::learned);
    g.setFont (uiFont (14.0f, true));
    g.drawText (noteNameForFrequency (f0), r.withHeight (20).translated (0, 62), juce::Justification::centred);
    g.setColour (colours::dim);
    g.setFont (uiFont (11.0f));
    g.drawText (juce::String (bands) + " harmonic bands", r.withHeight (18).translated (0, 86), juce::Justification::centred);
}

void UpfrontEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (38);
    auto bottom = area.removeFromBottom (168);
    area.removeFromBottom (10);
    spectrum.setBounds (area);

    // left column: ANALYZE, listen time, BALANCE, modes
    auto left = bottom.removeFromLeft (250);
    analyzeButton.setBounds (left.removeFromTop (44));
    left.removeFromTop (6);
    auto listenRow = left.removeFromTop (24);
    listenLabel.setBounds (listenRow.removeFromLeft (52));
    infToggle.setBounds (listenRow.removeFromRight (36));
    listenRow.removeFromRight (6);
    listenSlider.setBounds (listenRow);
    left.removeFromTop (8);
    balanceButton.setBounds (left.removeFromTop (44));
    left.removeFromTop (8);
    auto modes = left.removeFromTop (28);
    dynamicToggle.setBounds (modes.removeFromLeft (modes.getWidth() / 2).reduced (0, 0).withTrimmedRight (4));
    analogToggle.setBounds (modes.withTrimmedLeft (4));

    bottom.removeFromLeft (12);
    readoutArea = bottom.removeFromLeft (170).withHeight (120);
    bottom.removeFromLeft (8);

    const int kw = bottom.getWidth() / 6;
    auto place = [&] (juce::Slider& s, juce::Component& label) {
        auto col = bottom.removeFromLeft (kw);
        label.setBounds (col.removeFromTop (22));
        s.setBounds (col.removeFromTop (130));
    };
    place (amountKnob, amountLabel);
    place (tiltKnob, tiltLabel);
    place (bandsKnob, bandsLabel);
    place (hpKnob, hpToggle);
    hpToggle.setBounds (hpToggle.getBounds().withSizeKeepingCentre (84, 22));
    place (driveKnob, driveLabel);
    place (outputKnob, outputLabel);
}
