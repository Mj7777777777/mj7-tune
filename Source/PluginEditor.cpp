#include "PluginEditor.h"

using namespace mj7;
namespace mj7ui
{
static const float twoPi = juce::MathConstants<float>::twoPi;

juce::Font font (float size, bool bold, float kerning)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kerning);
}

/** Majuscules avec les accents francais (toUpperCase ne les gere pas sur tous les systemes). */
static juce::String upper (const juce::String& s)
{
    static const char* pairs[][2] = { { "é", "É" }, { "è", "È" }, { "ê", "Ê" }, { "à", "À" }, { "â", "Â" }, { "ç", "Ç" }, { "î", "Î" }, { "ô", "Ô" }, { "û", "Û" }, { "ù", "Ù" } };
    auto out = s.toUpperCase();
    for (const auto& p : pairs) out = out.replace (U8 (p[0]), U8 (p[1]));
    return out;
}

// ================================================================================================
Look::Look()
{
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
   #elif JUCE_MAC
    setDefaultSansSerifTypefaceName ("Avenir Next");
   #endif
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::gold.withAlpha (0.9f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::bg0);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::bg0);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col::gold.withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, col::bg0);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::gold.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::focusedOutlineColourId, col::gold);
    setColour (juce::CaretComponent::caretColourId, col::gold);
    setColour (juce::TooltipWindow::backgroundColourId, col::panel);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::line);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (area.getWidth(), area.getHeight()) - 6.0f;
    const auto c = area.getCentre();
    const float r = size * 0.5f, track = juce::jmax (3.0f, size * 0.075f), angle = start + pos * (end - start);

    juce::Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, r - track * 0.5f, r - track * 0.5f, 0.0f, start, end, true);
    g.setColour (col::line); g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // l'arc part du centre pour les reglages bipolaires (gain +/-), du debut sinon
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 && std::abs (s.getMinimum() + s.getMaximum()) < 1.0e-6;
    const float from = bipolar ? start + 0.5f * (end - start) : start;
    valArc.addCentredArc (c.x, c.y, r - track * 0.5f, r - track * 0.5f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    const bool enabled = s.isEnabled();
    g.setColour (accent.withAlpha (enabled ? 0.22f : 0.08f)); g.strokePath (valArc, juce::PathStrokeType (track + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.withAlpha (enabled ? 1.0f : 0.35f));  g.strokePath (valArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float br = r - track - 4.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2932), c.x, c.y - br, juce::Colour (0xff121116), c.x, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (s.isMouseOverOrDragging() ? 0.22f : 0.09f));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.6f, -br + 3.0f, 3.2f, br * 0.46f, 1.6f);
    g.setColour (enabled ? col::text : col::dim);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    const bool lit = b.getToggleState() || down;
    g.setColour (lit ? accent : col::panel.brighter (over ? 0.12f : 0.0f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (lit ? accent : (over ? accent.withAlpha (0.8f) : col::line.brighter (0.25f)));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (font (juce::jmin (15.0f, (float) b.getHeight() * 0.5f), true, 0.04f));
    g.setColour ((b.getToggleState() || down) ? col::bg0 : col::text.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 2);
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const bool on = b.getToggleState();
    const juce::Rectangle<float> pill ((float) b.getWidth() * 0.5f - 21.0f, 2.0f, 42.0f, 22.0f);
    g.setColour (on ? accent : col::line.brighter (over ? 0.2f : 0.0f)); g.fillRoundedRectangle (pill, 11.0f);
    g.setColour (on ? col::bg0 : col::dim); g.fillEllipse (on ? pill.getRight() - 20.0f : pill.getX() + 2.0f, pill.getY() + 2.0f, 18.0f, 18.0f);
    g.setColour (col::text); g.setFont (font (13.0f));
    g.drawFittedText (on ? "Oui" : "Non", 0, 26, b.getWidth(), 18, juce::Justification::centred, 1);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const juce::Rectangle<float> r (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (col::panel); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.isMouseOver (true) ? col::gold.withAlpha (0.8f) : col::line.brighter (0.25f)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow; const float ax = (float) w - 15.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f); arrow.lineTo (ax, ay + 2.5f); arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (col::dim); g.strokePath (arrow, juce::PathStrokeType (1.6f));
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

// ================================================================================================
bool AnalyseButton::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) < 80.0f;
}

void AnalyseButton::mouseUp (const juce::MouseEvent& e)
{
    if (hitTest (e.x, e.y)) proc.startAnalysis();
}

void AnalyseButton::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float R = 76.0f;
    const auto st = proc.analysisState();
    const bool busy = st == MJ7TuneProcessor::waiting || st == MJ7TuneProcessor::listening || st == MJ7TuneProcessor::computing;
    const float breathe = 0.5f + 0.5f * std::sin (phase);

    // spectre circulaire de la voix (symetrique gauche / droite)
    for (int i = 0; i < numBands; ++i)
    {
        const float len = 3.0f + 40.0f * bands[i];
        for (int side = 0; side < 2; ++side)
        {
            const float t = ((float) i + 0.5f) / (float) numBands;
            const float a = (side == 0 ? 1.0f : -1.0f) * (0.04f + t * 0.92f) * juce::MathConstants<float>::pi;   // 0 = bas, pi = haut
            const float sx = std::sin (a), cy2 = std::cos (a);
            const float r0 = R + 17.0f, r1 = r0 + len;
            g.setColour (col::gold.interpolatedWith (col::amber, bands[i]).withAlpha (0.25f + 0.75f * bands[i]));
            g.drawLine (c.x + sx * r0, c.y + cy2 * r0, c.x + sx * r1, c.y + cy2 * r1, 2.6f);
        }
    }

    // halo
    const float glow = busy ? 0.9f : (st == MJ7TuneProcessor::done ? 0.6f : 0.25f + 0.25f * breathe) + (hover ? 0.2f : 0.0f);
    for (int i = 6; i >= 1; --i)
    {
        const float rr = R + (float) i * 3.0f;
        g.setColour (col::gold.withAlpha (0.035f * glow * (float) (7 - i)));
        g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
    }

    // disque
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff25232b), c.x, c.y - R, juce::Colour (0xff0c0c0f), c.x, c.y + R, false));
    g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
    g.setColour (col::gold.withAlpha (hover ? 0.95f : 0.55f)); g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.4f);

    // anneau de progression
    const float rr = R + 9.0f;
    juce::Path ring; ring.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.0f, twoPi, true);
    g.setColour (col::line); g.strokePath (ring, juce::PathStrokeType (3.0f));
    juce::Path prog;
    if (st == MJ7TuneProcessor::listening)      prog.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.0f, twoPi * juce::jlimit (0.005f, 1.0f, proc.analysisProgress()), true);
    else if (st == MJ7TuneProcessor::done)      prog.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.0f, twoPi, true);
    else if (st == MJ7TuneProcessor::waiting || st == MJ7TuneProcessor::computing) prog.addCentredArc (c.x, c.y, rr, rr, 0.0f, phase * 2.0f, phase * 2.0f + 1.2f, true);
    g.setColour (st == MJ7TuneProcessor::failed ? col::family (2) : col::gold);
    g.strokePath (prog, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::String title = "ANALYSER", sub = U8 ("lancez la lecture\npuis appuyez");
    switch (st)
    {
        case MJ7TuneProcessor::waiting:   title = "EN ATTENTE"; sub = U8 ("lancez la lecture\nde la voix chantée"); break;
        case MJ7TuneProcessor::listening: title = U8 ("ÉCOUTE");  sub = juce::String (juce::jmax (0, juce::roundToInt (proc.analysisSeconds() * (1.0f - proc.analysisProgress())))) + " s"; break;
        case MJ7TuneProcessor::computing: title = "CALCUL";     sub = U8 ("réglage en cours"); break;
        case MJ7TuneProcessor::done:
        {
            const int k = (int) proc.apvts.getRawParameterValue ("key")->load(), sc = (int) proc.apvts.getRawParameterValue ("scale")->load();
            title = upper (U8 (mj7::noteName (k))) + (sc == 1 ? " MAJ" : (sc == 2 ? " MIN" : ""));
            sub = U8 ("appuyez pour\nréanalyser"); break;
        }
        case MJ7TuneProcessor::failed:    title = U8 ("RÉESSAYER"); sub = U8 ("aucune voix\ndétectée"); break;
        case MJ7TuneProcessor::idle: default: break;
    }
    g.setColour (col::gold); g.setFont (font (20.0f, true, 0.12f));
    g.drawText (title, juce::Rectangle<float> (c.x - R, c.y - 30.0f, R * 2.0f, 26.0f), juce::Justification::centred);
    g.setColour (col::text.withAlpha (0.82f)); g.setFont (font (st == MJ7TuneProcessor::listening ? 20.0f : 13.0f));
    g.drawFittedText (sub, juce::Rectangle<float> (c.x - R + 8.0f, c.y - 2.0f, R * 2.0f - 16.0f, 40.0f).toNearestInt(), juce::Justification::centredTop, 2);
}

// ================================================================================================
void MeterBar::push (float peakLinear)
{
    const float db = juce::Decibels::gainToDecibels (peakLinear, -90.0f);
    level = db > level ? db : level - 1.2f;
    if (db >= hold) { hold = db; holdCount = 60; } else if (--holdCount < 0) hold -= 0.6f;
    repaint();
}

void MeterBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto lab = r.removeFromBottom (18.0f);
    g.setColour (col::dim); g.setFont (font (12.0f, true, 0.06f)); g.drawText (label, lab.expanded (10.0f, 0.0f), juce::Justification::centred);
    r = r.withSizeKeepingCentre (9.0f, r.getHeight());
    g.setColour (col::line); g.fillRoundedRectangle (r, 4.0f);
    auto yFor = [&r] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    const float top = yFor (level);
    if (top < r.getBottom() - 1.0f)
    {
        g.setGradientFill (juce::ColourGradient (col::family (2), r.getX(), r.getY(), col::gold, r.getX(), r.getY() + r.getHeight() * 0.2f, false));
        g.fillRoundedRectangle (r.withTop (top), 4.0f);
    }
    g.setColour (hold > -1.0f ? col::family (2) : col::text); g.fillRect (r.getX() - 2.0f, yFor (hold) - 1.0f, r.getWidth() + 4.0f, 2.0f);
}

void ChainTile::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    if (hasPower && e.x < 30) { if (onPower) onPower(); }
    else if (onSelect) onSelect();
}

void ChainTile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto accent = col::family (familyIndex);
    g.setColour (selected ? accent.withAlpha (0.16f) : col::panel.withAlpha (isMouseOver() ? 1.0f : 0.8f)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (selected ? accent : col::line.brighter (isMouseOver() ? 0.5f : 0.1f)); g.drawRoundedRectangle (r, 6.0f, selected ? 1.5f : 1.0f);

    float textX = 10.0f;
    if (hasPower)
    {
        const juce::Rectangle<float> dot (10.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
        if (active) { g.setColour (accent.withAlpha (0.25f)); g.fillEllipse (dot.expanded (3.0f)); g.setColour (accent); g.fillEllipse (dot); }
        else        { g.setColour (col::dim.withAlpha (0.7f)); g.drawEllipse (dot, 1.3f); }
        textX = 28.0f;
    }
    g.setColour (active ? col::text : col::dim.withAlpha (0.75f)); g.setFont (font (13.5f, selected || ! hasPower));
    g.drawText (name, juce::Rectangle<float> (textX, 0.0f, r.getWidth() - textX - 4.0f, r.getHeight()), hasPower ? juce::Justification::centredLeft : juce::Justification::centred);
    if (gr < -0.3f && active)       // reduction de gain en cours
    {
        const float wBar = (r.getWidth() - 12.0f) * juce::jlimit (0.0f, 1.0f, -gr / 12.0f);
        g.setColour (accent); g.fillRoundedRectangle (r.getX() + 6.0f, r.getBottom() - 4.5f, wBar, 2.5f, 1.2f);
    }
}


// ================================================================================================
//  Clavier
// ================================================================================================
juce::Rectangle<float> Keyboard::keyRect (int note) const
{
    static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float ww = r.getWidth() / 7.0f;
    if (! isBlack (note)) return { r.getX() + ww * (float) whiteIndex[note], r.getY(), ww, r.getHeight() };
    const float bw = ww * 0.62f;
    return { r.getX() + ww * (float) (whiteIndex[note] + 1) - bw * 0.5f, r.getY(), bw, r.getHeight() * 0.6f };
}

int Keyboard::noteAt (juce::Point<float> p) const
{
    for (int n = 0; n < 12; ++n) if (isBlack (n) && keyRect (n).contains (p)) return n;
    for (int n = 0; n < 12; ++n) if (! isBlack (n) && keyRect (n).contains (p)) return n;
    return -1;
}

void Keyboard::mouseUp (const juce::MouseEvent& e)
{
    const int n = noteAt (e.position);
    if (n >= 0) { proc.toggleNote (n); repaint(); }
}

void Keyboard::paint (juce::Graphics& g)
{
    const auto mask = proc.allowedNotes();
    const int tonic = (int) proc.apvts.getRawParameterValue ("key")->load();
    auto draw = [&] (int n)
    {
        const auto r = keyRect (n).reduced (1.5f, 0.0f);
        const bool allowed = (mask >> n) & 1u, black = isBlack (n);
        juce::Colour fill = black ? juce::Colour (0xff1d1c22) : juce::Colour (0xff3a3842);
        if (allowed) fill = black ? col::gold.withMultipliedBrightness (0.62f) : col::gold.withMultipliedBrightness (0.92f);
        g.setColour (fill); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::bg0.withAlpha (0.7f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        if (n == target) { g.setColour (col::text); g.drawRoundedRectangle (r.reduced (2.0f), 4.0f, 2.2f); }
        if (n == sung)
        {
            g.setColour (col::family (mj7::Correction).withAlpha (0.35f)); g.fillRoundedRectangle (r.reduced (3.0f), 3.0f);
            g.setColour (col::family (mj7::Correction)); g.drawRoundedRectangle (r.reduced (3.0f), 3.0f, 2.0f);
        }
        if (n == tonic) { g.setColour (allowed ? col::bg0 : col::gold); g.fillEllipse (r.getCentreX() - 3.5f, r.getY() + 8.0f, 7.0f, 7.0f); }
        g.setColour (allowed ? col::bg0 : col::dim); g.setFont (font (black ? 10.5f : 12.5f, true));
        g.drawText (U8 (mj7::noteName (n)), r.withTrimmedTop (r.getHeight() - 22.0f), juce::Justification::centred);
    };
    for (int n = 0; n < 12; ++n) if (! isBlack (n)) draw (n);
    for (int n = 0; n < 12; ++n) if (isBlack (n)) draw (n);
}

// ================================================================================================
MainView::MainView (MJ7TuneProcessor& p) : proc (p), analyse (p), keyboard (p)
{
    setLookAndFeel (&look);
    setOpaque (true);
    setSize (W, H);
    curveIn.assign (mj7::TuneEngine::kHistory, 0.0f); curveOut.assign (mj7::TuneEngine::kHistory, 0.0f);
    grain = juce::Image (juce::Image::ARGB, 128, 128, true);
    juce::Random rnd (7);
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) grain.setPixelAt (x, y, juce::Colours::white.withAlpha (rnd.nextFloat() * 0.035f));

    // --- barre du haut ---
    int id = 1;
    for (const auto& f : factoryPresets()) presetBox.addItem (U8 (f.name), id++);
    presetBox.setTooltip (U8 ("Style de correction. Il ne change ni la tonalité ni la gamme."));
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) { userPresetName.clear(); proc.loadFactoryPreset (idx); shownPreset = idx; }
    };
    addAndMakeVisible (presetBox);

    saveBtn.setButtonText ("Sauver"); loadBtn.setButtonText ("Ouvrir"); abBtn.setButtonText ("A"); undoBtn.setButtonText ("Annuler");
    bypassBtn.setButtonText ("BYPASS"); tabCurve.setButtonText ("COURBE"); tabAnalyse.setButtonText ("ANALYSE");
    for (auto* b : { &saveBtn, &loadBtn, &abBtn, &undoBtn, &bypassBtn, &tabCurve, &tabAnalyse })
    { b->setColour (juce::TextButton::buttonOnColourId, col::gold); addAndMakeVisible (b); }
    bypassBtn.setClickingTogglesState (true); bypassBtn.setColour (juce::TextButton::buttonOnColourId, col::family (2));
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassBtn);
    saveBtn.setTooltip (U8 ("Enregistrer tous les réglages dans un fichier de preset."));
    loadBtn.setTooltip (U8 ("Ouvrir un preset enregistré."));
    abBtn.setTooltip (U8 ("Comparer deux versions des réglages (A et B)."));
    bypassBtn.setTooltip (U8 ("Écouter la voix sans correction."));
    undoBtn.setTooltip (U8 ("Annuler l'analyse : remet la tonalité et la gamme d'avant."));
    saveBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Enregistrer le preset", MJ7TuneProcessor::presetFolder().getChildFile ("Mon tune.mj7tune"), "*.mj7tune");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (proc.savePresetFile (f.withFileExtension ("mj7tune"))) { userPresetName = f.getFileNameWithoutExtension(); presetBox.setText (userPresetName, juce::dontSendNotification); }
            });
    };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Ouvrir un preset", MJ7TuneProcessor::presetFolder(), "*.mj7tune");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile() && proc.loadPresetFile (f)) { userPresetName = f.getFileNameWithoutExtension(); presetBox.setText (userPresetName, juce::dontSendNotification); }
            });
    };
    abBtn.onClick = [this] { proc.toggleAB(); abBtn.setButtonText (proc.abSlot() == 0 ? "A" : "B"); };
    undoBtn.onClick = [this] { proc.undoAnalysis(); };
    tabCurve.onClick = [this] { setTab (0); };
    tabAnalyse.onClick = [this] { setTab (1); };
    setTab (0);
    undoBtn.setVisible (false);

    presetBox.setBounds (330, 12, 250, 30); saveBtn.setBounds (588, 12, 70, 30); loadBtn.setBounds (664, 12, 70, 30);
    abBtn.setBounds (742, 12, 34, 30); bypassBtn.setBounds (784, 12, 90, 30);
    tabCurve.setBounds (48, 64, 88, 24); tabAnalyse.setBounds (140, 64, 88, 24); undoBtn.setBounds (268, 64, 80, 24);

    inMeter.label = "IN"; outMeter.label = "OUT";
    addAndMakeVisible (inMeter); addAndMakeVisible (outMeter); addAndMakeVisible (analyse);
    inMeter.setBounds (12, 66, 24, 268); outMeter.setBounds (W - 36, 66, 24, 268);
    analyse.setBounds (360, 58, 280, 280);

    // --- chaine ---
    essentialTile.name = "ESSENTIEL"; essentialTile.hasPower = false; essentialTile.familyIndex = Neutre;
    essentialTile.onSelect = [this] { showModule (-1); };
    essentialTile.setBounds (20, 346, 110, 62);
    addAndMakeVisible (essentialTile);
    const auto& mods = modules();
    for (int i = 0; i < (int) mods.size(); ++i)
    {
        auto* t = tiles.add (new ChainTile());
        t->name = U8 (mods[(size_t) i].name); t->familyIndex = mods[(size_t) i].family; t->hasPower = mods[(size_t) i].bypass >= 0;
        t->onSelect = [this, i] { showModule (i); };
        t->onPower = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter (paramDef (modules()[(size_t) i].bypass).id))
            { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f); prm->endChangeGesture(); }
        };
        t->setBounds (136 + i * 212, 346, 206, 62);
        t->setTooltip (U8 (mods[(size_t) i].help));
        addAndMakeVisible (t);
    }
    addChildComponent (keyboard);
    keyboard.setBounds (28, 452, 336, 150);
    showModule (-1);
    startTimerHz (30);
}

MainView::~MainView() { stopTimer(); setLookAndFeel (nullptr); }

void MainView::setTab (int t)
{
    tab = t;
    tabCurve.setToggleState (t == 0, juce::dontSendNotification);
    tabAnalyse.setToggleState (t == 1, juce::dontSendNotification);
    repaint();
}

std::unique_ptr<Control> MainView::makeControl (int pid, const juce::String& labelText, juce::Colour accent)
{
    auto c = std::make_unique<Control>();
    const auto& d = paramDef (pid);
    c->label = std::make_unique<juce::Label> (juce::String(), labelText);
    c->label->setJustificationType (juce::Justification::centred);
    c->label->setColour (juce::Label::textColourId, col::dim.brighter (0.25f));
    c->label->setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (*c->label);
    if (d.type == PType::tC)
    {
        c->combo = std::make_unique<juce::ComboBox>();
        c->combo->addItemList (proc.apvts.getParameter (d.id)->getAllValueStrings(), 1);
        c->cAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, d.id, *c->combo);
        addAndMakeVisible (*c->combo);
    }
    else if (d.type == PType::tB)
    {
        c->toggle = std::make_unique<juce::ToggleButton>();
        c->toggle->setColour (juce::ToggleButton::tickColourId, accent);
        c->bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, d.id, *c->toggle);
        addAndMakeVisible (*c->toggle);
    }
    else
    {
        c->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        c->slider->setColour (juce::Slider::rotarySliderFillColourId, accent);
        c->slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, d.id, *c->slider);
        addAndMakeVisible (*c->slider);
    }
    return c;
}

void MainView::showModule (int index)
{
    controls.clear(); listenBtn.reset();
    selected = index;
    essentialTile.selected = index < 0; essentialTile.repaint();
    for (int i = 0; i < tiles.size(); ++i) { tiles[i]->selected = i == index; tiles[i]->repaint(); }
    keyboard.setVisible (index < 0 || index == 0);

    const int top = 452, height = 152;
    auto place = [&] (Control& c, juce::Rectangle<int> cell)
    {
        c.label->setBounds (cell.removeFromTop (20));
        if (c.slider != nullptr) { c.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (cell.getWidth(), 84), 20); c.slider->setBounds (cell.reduced (2, 0)); }
        if (c.combo != nullptr)  c.combo->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() - 6, 200), 32));
        if (c.toggle != nullptr) c.toggle->setBounds (cell.withSizeKeepingCentre (60, 46));
    };
    if (index < 0)
    {
        struct Macro { int pid; const char* label; int family; const char* tip; };
        static const Macro macros[] = {
            { speed,    "VITESSE",    Correction, "Temps pour rejoindre la note juste. 0 ms = effet robot, 20 à 50 ms = moderne, 80 ms et plus = naturel." },
            { natural,  "VIBRATO",    Correction, "Garde le vibrato du chanteur : la correction suit le centre de la note, pas chaque oscillation." },
            { humanize, "HUMANISER",  Correction, "Adoucit la correction sur les notes tenues, garde les notes courtes bien nettes." },
            { formant,  "FORMANTS",   Couleur,    "Timbre plus grave (négatif) ou plus aigu (positif) sans changer la note." },
            { mix,      "MIX",        Neutre,     "Part de voix corrigée. Moins de 100 % : un peu de voix d'origine, effet plus doux." } };
        int x = 376;
        for (const auto& m : macros)
        {
            auto c = makeControl (m.pid, U8 (m.label), col::family (m.family));
            c->label->setFont (font (12.5f, true, 0.08f)); c->label->setColour (juce::Label::textColourId, col::text);
            c->slider->setTooltip (U8 (m.tip));
            place (*c, { x, top - 6, 90, height + 6 }); x += 92;
            controls.push_back (std::move (c));
        }
        listenBtn = std::make_unique<ThrowButton> (proc.apvts.getParameter ("bypass"), U8 ("ÉCOUTER\nL'ORIGINAL"));
        listenBtn->setColour (juce::TextButton::buttonOnColourId, col::family (2));
        listenBtn->setTooltip (U8 ("Maintenez pour entendre la voix sans correction."));
        addAndMakeVisible (*listenBtn);
        listenBtn->setBounds (846, top + 20, 134, 96);
    }
    else
    {
        const auto& mod = modules()[(size_t) index];
        const int count = (int) mod.params.size();
        const int left = index == 0 ? 380 : 20, avail = 980 - left;
        const int cellW = juce::jmin (index == 0 ? 240 : 130, avail / count);
        int x = left + (avail - cellW * count) / 2;
        for (int pid : mod.params)
        {
            auto name = U8 (paramDef (pid).name);
            auto c = makeControl (pid, name, col::family (mod.family));
            place (*c, { x, top + (index == 0 ? 30 : 0), cellW, index == 0 ? 80 : height }); x += cellW;
            controls.push_back (std::move (c));
        }
    }
    repaint();
}

void MainView::timerCallback()
{
    ++tick;
    analyse.phase = std::fmod (analyse.phase + 0.09f, twoPi * 4.0f);
    // le cercle d'ANALYSER montre la note : une barre par demi-ton, allumee sur la note chantee
    auto& eng = proc.engineForDisplay();
    const float det = eng.detectedMidi.load(), tgt = eng.targetMidi.load();
    for (int b = 0; b < AnalyseButton::numBands; ++b)
    {
        float val = 0.0f;
        if (det > 0.0f)
        {
            const float pos = std::fmod (det, 12.0f) / 12.0f * (float) AnalyseButton::numBands;
            const float d = std::abs ((float) b + 0.5f - pos);
            val = juce::jlimit (0.0f, 1.0f, 1.0f - std::min (d, (float) AnalyseButton::numBands - d) / 4.0f);
        }
        analyse.bands[b] = val > analyse.bands[b] ? val : analyse.bands[b] * 0.85f;
    }
    analyse.repaint();
    inMeter.push (proc.inPeak.exchange (0.0f)); outMeter.push (proc.outPeak.exchange (0.0f));

    if (det > 0.0f) { noteShown = det; targetShown = tgt; centsShown += 0.35f * (juce::jlimit (-50.0f, 50.0f, 100.0f * (det - tgt)) - centsShown); }
    const int sung = det > 0.0f ? (int) std::lround (det) % 12 : -1, target = det > 0.0f ? (int) std::lround (tgt) % 12 : -1;
    if (sung != keyboard.sung || target != keyboard.target || tick % 15 == 0) { keyboard.sung = sung; keyboard.target = target; keyboard.repaint(); }

    const int pos = eng.histPos.load (std::memory_order_acquire);
    for (int i = 0; i < mj7::TuneEngine::kHistory; ++i)
    {
        const int k = (pos + i) % mj7::TuneEngine::kHistory;
        curveIn[(size_t) i] = eng.histIn[k]; curveOut[(size_t) i] = eng.histOut[k];
    }
    repaint (40, 90, 320, 252);
    repaint (640, 60, 350, 282);

    if (tick % 6 != 0) return;
    const int st = (int) proc.analysisState();
    if (st != lastState)
    {
        if (st == MJ7TuneProcessor::done || st == MJ7TuneProcessor::failed) setTab (1);
        lastState = st;
    }
    if (userPresetName.isEmpty() && shownPreset != proc.currentPreset()) { shownPreset = proc.currentPreset(); presetBox.setSelectedId (shownPreset + 1, juce::dontSendNotification); }
    const auto& mods = modules();
    for (int i = 0; i < tiles.size(); ++i)
    {
        const auto& m = mods[(size_t) i];
        const bool act = m.bypass < 0 || proc.apvts.getRawParameterValue (paramDef (m.bypass).id)->load() > 0.5f;
        if (act != tiles[i]->active) { tiles[i]->active = act; tiles[i]->repaint(); }
    }
    undoBtn.setVisible (proc.hasAnalysis());
}

void MainView::paintCurve (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto r = area.toFloat();
    g.setColour (col::bg0.withAlpha (0.55f)); g.fillRoundedRectangle (r, 6.0f);
    float lo = 200.0f, hi = 0.0f;
    for (size_t i = 0; i < curveIn.size(); ++i)
        for (float m : { curveIn[i], curveOut[i] }) if (m > 0.0f) { lo = std::min (lo, m); hi = std::max (hi, m); }
    if (hi <= 0.0f) { lo = 52.0f; hi = 64.0f; }
    const float mid = 0.5f * (lo + hi), span = std::max (12.0f, hi - lo + 3.0f);
    lo = std::floor (mid - span * 0.5f); hi = lo + std::ceil (span);
    auto yFor = [&] (float m) { return r.getBottom() - 6.0f - (r.getHeight() - 12.0f) * (m - lo) / (hi - lo); };
    const auto mask = proc.allowedNotes();
    const int tonic = (int) proc.apvts.getRawParameterValue ("key")->load();
    g.setFont (font (10.5f, true));
    for (int n = (int) lo; n <= (int) hi; ++n)
    {
        const bool allowed = mj7::noteAllowed (mask, n);
        const float y = yFor ((float) n);
        g.setColour (allowed ? col::gold.withAlpha (n % 12 == tonic ? 0.45f : 0.22f) : col::line.withAlpha (0.5f));
        g.fillRect (r.getX() + 30.0f, y - 0.5f, r.getWidth() - 34.0f, n % 12 == tonic ? 1.6f : 1.0f);
        if (allowed && (hi - lo) <= 20.0f)
        {
            g.setColour (col::dim.withAlpha (0.9f));
            g.drawText (U8 (mj7::noteName (n)) + juce::String (n / 12 - 1), juce::Rectangle<float> (r.getX() + 2.0f, y - 7.0f, 28.0f, 14.0f), juce::Justification::centredLeft);
        }
    }
    auto line = [&] (const std::vector<float>& v)
    {
        juce::Path p; bool drawing = false;
        const float dx = (r.getWidth() - 34.0f) / (float) (v.size() - 1);
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float x = r.getX() + 30.0f + dx * (float) i;
            if (v[i] <= 0.0f) { drawing = false; continue; }
            const float y = yFor (v[i]);
            if (! drawing) { p.startNewSubPath (x, y); drawing = true; } else p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (col::dim.withAlpha (0.75f)); g.strokePath (line (curveIn), juce::PathStrokeType (1.4f));
    g.setColour (col::gold); g.strokePath (line (curveOut), juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto legend = area.withTop (area.getBottom() + 2).withHeight (18);
    g.setFont (font (12.0f));
    auto item = [&] (const juce::String& text, juce::Colour c, int w)
    {
        auto cell = legend.removeFromLeft (w);
        g.setColour (c); g.fillRoundedRectangle ((float) cell.getX(), (float) cell.getCentreY() - 2.0f, 14.0f, 4.0f, 2.0f);
        g.setColour (col::dim.brighter (0.2f)); g.drawText (text, cell.withTrimmedLeft (19), juce::Justification::centredLeft);
    };
    item (U8 ("chanté"), col::dim, 84); item (U8 ("corrigé"), col::gold, 84); item (U8 ("notes de la gamme"), col::gold.withAlpha (0.35f), 140);
}

void MainView::paintNote (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto& eng = proc.engineForDisplay();
    const bool singing = eng.detectedMidi.load() > 0.0f;
    auto big = r.removeFromTop (64);
    if (noteShown > 0.0f)
    {
        const int m = (int) std::lround (noteShown);
        g.setColour (singing ? col::text : col::dim.withAlpha (0.6f)); g.setFont (font (46.0f, true));
        const auto name = U8 (mj7::noteName (m));
        const int nw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name)) + 4;
        g.drawText (name, big.removeFromLeft (nw), juce::Justification::centredLeft);
        g.setColour (col::dim); g.setFont (font (20.0f, true));
        g.drawText (juce::String (m / 12 - 1), big.removeFromLeft (30).withTrimmedTop (18), juce::Justification::centredLeft);
        big.removeFromLeft (20);
        g.setFont (font (13.0f));
        g.drawFittedText (U8 ("corrigé vers\n") + U8 (mj7::noteName ((int) targetShown)) + juce::String ((int) targetShown / 12 - 1), big, juce::Justification::centredLeft, 2);
    }
    else { g.setColour (col::dim.withAlpha (0.6f)); g.setFont (font (40.0f, true)); g.drawText ("--", big, juce::Justification::centredLeft); }

    // ecart en cents : -50 a +50
    auto bar = r.removeFromTop (30).toFloat().withHeight (8.0f).translated (0.0f, 6.0f);
    g.setColour (col::line); g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (col::family (Correction).withAlpha (0.35f)); g.fillRoundedRectangle (bar.withSizeKeepingCentre (bar.getWidth() * 0.3f, bar.getHeight()), 4.0f);
    g.setColour (col::dim); g.fillRect (bar.getCentreX() - 0.5f, bar.getY() - 4.0f, 1.0f, 16.0f);
    if (singing)
    {
        const float x = bar.getCentreX() + centsShown / 50.0f * bar.getWidth() * 0.5f;
        g.setColour (std::abs (centsShown) < 15.0f ? col::family (Correction) : col::gold); g.fillEllipse (x - 7.0f, bar.getCentreY() - 7.0f, 14.0f, 14.0f);
    }
    auto labels = r.removeFromTop (16);
    g.setColour (col::dim); g.setFont (font (11.0f));
    g.drawText ("-50", labels, juce::Justification::centredLeft); g.drawText ("juste", labels, juce::Justification::centred); g.drawText ("+50 cents", labels, juce::Justification::centredRight);
    r.removeFromTop (8);

    auto stat = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
    {
        auto row = r.removeFromTop (27);
        g.setColour (col::line); g.fillRect (row.getX(), row.getY(), row.getWidth(), 1);
        g.setColour (col::dim); g.setFont (font (12.0f, true, 0.12f)); g.drawText (label, row, juce::Justification::centredLeft);
        g.setColour (c); g.setFont (font (15.0f, true)); g.drawText (value, row, juce::Justification::centredRight);
    };
    auto val = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const int k = (int) val ("key"), sc = (int) val ("scale");
    stat (U8 ("TONALITÉ"), U8 (mj7::noteName (k)) + " " + proc.apvts.getParameter ("scale")->getCurrentValueAsText().toLowerCase(), col::text);
    int count = 0; const auto mask = proc.allowedNotes(); for (int i = 0; i < 12; ++i) if ((mask >> i) & 1u) ++count;
    stat ("NOTES PERMISES", juce::String (count) + " / 12" + (sc == mj7::Personnalisee ? U8 (" (choix)") : juce::String()), col::text);
    stat ("CORRECTION", singing ? juce::String (juce::roundToInt (-centsShown)) + " cents" : juce::String ("--"), col::family (Correction));
    stat ("LATENCE", juce::String (proc.latencyMs()) + " ms", proc.latencyMs() <= 6 ? col::family (Correction) : col::text);
}

void MainView::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (col::bg1, (float) W * 0.5f, 0.0f, col::bg0, (float) W * 0.5f, (float) H, false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (col::gold.withAlpha (0.07f), 500.0f, 198.0f, col::gold.withAlpha (0.0f), 500.0f, 480.0f, true));
    g.fillRect (150, 0, 700, 420);
    g.setTiledImageFill (grain, 0, 0, 1.0f); g.fillAll();

    g.setColour (col::gold); g.setFont (font (21.0f, true, 0.16f)); g.drawText ("MJ7", 20, 10, 60, 34, juce::Justification::centredLeft);
    g.setColour (col::text); g.setFont (font (15.0f, false, 0.22f)); g.drawText ("TUNE", 72, 10, 176, 34, juce::Justification::centredLeft);
    g.setColour (col::line); g.fillRect (0, 53, W, 1);

    auto rows = juce::Rectangle<int> (48, 62, 300, 276).withTrimmedTop (34);
    if (tab == 0) paintCurve (g, rows.withTrimmedBottom (22));
    else if (proc.summary.isEmpty())
    {
        static const char* steps[3] = { "Choisissez le style de correction en haut.", "Lancez la lecture d'un passage chanté (le refrain est idéal).",
                                        "Appuyez sur ANALYSER : 20 secondes d'écoute, puis la tonalité et la gamme se règlent." };
        for (int i = 0; i < 3; ++i)
        {
            auto row = rows.removeFromTop (i == 2 ? 62 : 48);
            g.setColour (col::gold); g.setFont (font (22.0f, true)); g.drawText (juce::String (i + 1), row.removeFromLeft (28), juce::Justification::topLeft);
            g.setColour (col::text.withAlpha (0.9f)); g.setFont (font (14.0f)); g.drawFittedText (U8 (steps[i]), row.withTrimmedTop (4), juce::Justification::topLeft, 3);
        }
        g.setColour (col::dim); g.setFont (font (13.0f));
        g.drawFittedText (U8 ("Vous connaissez déjà la tonalité ? Réglez-la dans le module Tonalité. Le clavier permet de couper une note gênante."), rows.removeFromTop (60), juce::Justification::topLeft, 3);
    }
    else
    {
        const int rowH = juce::jmin (40, rows.getHeight() / juce::jmax (1, proc.summary.size()));
        for (const auto& line : proc.summary)
        {
            auto row = rows.removeFromTop (rowH);
            const int colon = line.indexOf (" : ");
            g.setColour (col::gold); g.fillEllipse ((float) row.getX(), (float) row.getY() + 6.0f, 5.0f, 5.0f);
            g.setColour (col::text.withAlpha (0.92f)); g.setFont (font (12.5f));
            g.drawFittedText (colon > 0 ? upper (line.substring (0, colon)) + " : " + line.substring (colon + 3) : line, row.withTrimmedLeft (13), juce::Justification::topLeft, 3, 0.85f);
        }
    }

    g.setColour (col::dim); g.setFont (font (12.0f, true, 0.14f)); g.drawText ("NOTE", 656, 62, 170, 26, juce::Justification::centredLeft);
    paintNote (g, juce::Rectangle<int> (656, 94, 296, 244));

    g.setColour (col::line); g.fillRect (20, 340, W - 40, 1);
    g.setColour (col::panel.withAlpha (0.55f)); g.fillRoundedRectangle (12.0f, 416.0f, (float) W - 24.0f, 196.0f, 10.0f);
    g.setColour (col::line); g.drawRoundedRectangle (12.0f, 416.0f, (float) W - 24.0f, 196.0f, 10.0f, 1.0f);
    if (selected < 0)
    {
        g.setColour (col::gold); g.setFont (font (13.0f, true, 0.14f)); g.drawText ("ESSENTIEL", 28, 420, 120, 24, juce::Justification::centredLeft);
        g.setColour (col::dim); g.setFont (font (13.0f));
        g.drawText (U8 ("Clavier : notes permises en or, note chantée en vert. Cliquez une touche pour l'activer ou la couper."), 128, 420, 850, 24, juce::Justification::centredLeft);
    }
    else
    {
        const auto& mod = modules()[(size_t) selected];
        const auto title = upper (U8 (mod.title));
        g.setColour (col::family (mod.family)); g.setFont (font (13.0f, true, 0.14f));
        const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 16;
        g.drawText (title, 28, 420, tw, 24, juce::Justification::centredLeft);
        g.setColour (col::dim); g.setFont (font (13.0f)); g.drawFittedText (U8 (mod.help), 28 + tw, 420, W - 56 - tw, 24, juce::Justification::centredLeft, 1, 0.8f);
    }
}
} // namespace mj7ui

// ================================================================================================
MJ7TuneEditor::MJ7TuneEditor (MJ7TuneProcessor& p) : juce::AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) mj7ui::MainView::W / (double) mj7ui::MainView::H);
    setResizeLimits (900, 558, 1800, 1116);
    setSize (mj7ui::MainView::W, mj7ui::MainView::H);
}

void MJ7TuneEditor::resized()
{
    view.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) mj7ui::MainView::W));
    view.setBounds (0, 0, mj7ui::MainView::W, mj7ui::MainView::H);
}
