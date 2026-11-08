#include "PluginEditor.h"

using namespace juce;

namespace
{
//==============================================================================
// Palette (identique à la version web)
const Colour cBg     (0xff121418);
const Colour cPanel  (0xff1b1e25);
const Colour cPanel2 (0xff242831);
const Colour cLine   (0xff333948);
const Colour cText   (0xffe7e9ee);
const Colour cDim    (0xff8a91a2);
const Colour cAcc    (0xffff6a2b);
const Colour cAcc2   (0xff27d3ff);

// JUCE lit les littéraux « char* » comme de l'ASCII : tout texte accentué passe par U8().
String U8 (const char* utf8) { return String::fromUTF8 (utf8); }

StringArray U8list (std::initializer_list<const char*> items)
{
    StringArray a;
    for (auto* it : items) a.add (U8 (it));
    return a;
}

Font uiFont (float h, bool bold = false)
{
    return Font (FontOptions (h, bold ? Font::bold : Font::plain));
}

String noteName (int n)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String (names[((n % 12) + 12) % 12]) + String (n / 12 - 2);   // 60 = C3 (convention Cubase)
}

bool isBlackKey (int n) { const int k = ((n % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

//==============================================================================
struct SH32LookAndFeel : public LookAndFeel_V4
{
    SH32LookAndFeel()
    {
        setColour (ResizableWindow::backgroundColourId, cBg);
        setColour (Label::textColourId, cText);
        setColour (Slider::rotarySliderFillColourId, cAcc);
        setColour (Slider::rotarySliderOutlineColourId, Colour (0xff2c3240));
        setColour (Slider::thumbColourId, cText);
        setColour (Slider::trackColourId, cAcc);
        setColour (Slider::backgroundColourId, cPanel2);
        setColour (Slider::textBoxTextColourId, cAcc2);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (ComboBox::backgroundColourId, cPanel2);
        setColour (ComboBox::textColourId, cText);
        setColour (ComboBox::outlineColourId, cLine);
        setColour (ComboBox::arrowColourId, cDim);
        setColour (TextButton::buttonColourId, cPanel2);
        setColour (TextButton::buttonOnColourId, cAcc);
        setColour (TextButton::textColourOffId, cText);
        setColour (TextButton::textColourOnId, Colour (0xff111111));
        setColour (PopupMenu::backgroundColourId, cPanel);
        setColour (PopupMenu::textColourId, cText);
        setColour (PopupMenu::highlightedBackgroundColourId, cAcc);
        setColour (PopupMenu::highlightedTextColourId, Colour (0xff111111));
        setColour (TextEditor::backgroundColourId, cPanel2);
        setColour (TextEditor::textColourId, cText);
        setColour (TextEditor::outlineColourId, cLine);
    }
};

//==============================================================================
// Une cellule de l'onglet « Son » : potard, liste ou interrupteur lié à un paramètre.
class ParamCell : public Component
{
public:
    ParamCell (AudioProcessorValueTreeState& apvts, const sh32::ParamDef& d) : kind (d.kind)
    {
        label.setText (d.label, dontSendNotification);
        label.setJustificationType (Justification::centred);
        label.setFont (uiFont (11.0f));
        label.setColour (Label::textColourId, cDim);
        addAndMakeVisible (label);

        switch (kind)
        {
            case sh32::Kind::Knob:
                slider.setSliderStyle (Slider::RotaryVerticalDrag);
                slider.setTextBoxStyle (Slider::TextBoxBelow, false, 64, 14);
                slider.setRotaryParameters (degreesToRadians (225.0f), degreesToRadians (495.0f), true);
                slider.setMouseDragSensitivity (160);
                addAndMakeVisible (slider);
                sliderAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (apvts, d.id, slider);
                slider.setDoubleClickReturnValue (true, (double) d.def);
                slider.setTooltip (String ("CC") + String (d.cc));
                break;

            case sh32::Kind::Choice:
            {
                StringArray names;
                for (int i = d.minV; i <= d.maxV; ++i) names.add (String (i));
                combo.addItemList (names, 1);
                addAndMakeVisible (combo);
                comboAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, d.id, combo);
                break;
            }

            case sh32::Kind::Toggle:
                button.setButtonText (d.label);
                button.setClickingTogglesState (true);
                label.setVisible (false);
                addAndMakeVisible (button);
                buttonAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (apvts, d.id, button);
                break;
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        switch (kind)
        {
            case sh32::Kind::Knob:   label.setBounds (r.removeFromTop (14)); slider.setBounds (r); break;
            case sh32::Kind::Choice: label.setBounds (r.removeFromTop (14)); combo.setBounds (r.removeFromTop (30).reduced (2, 4)); break;
            case sh32::Kind::Toggle: button.setBounds (r.removeFromTop (34).reduced (2, 5)); break;
        }
    }

private:
    sh32::Kind kind;
    Label label;
    Slider slider;
    ComboBox combo;
    TextButton button;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>   sliderAtt;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment>   buttonAtt;
};

constexpr int kCellW = 68, kCellH = 88, kMaxCols = 9, kPad = 8, kTitleH = 22;

class SectionBox : public Component
{
public:
    SectionBox (AudioProcessorValueTreeState& apvts, const char* sectionName, int first, int count)
        : title (sectionName), n (count)
    {
        for (int i = 0; i < count; ++i)
        {
            auto* c = cells.add (new ParamCell (apvts, sh32::kParams[first + i]));
            addAndMakeVisible (c);
        }
    }

    int cols() const { return jmin (n, kMaxCols); }
    int rows() const { return (n + kMaxCols - 1) / kMaxCols; }
    int idealW() const { return cols() * kCellW + 2 * kPad; }
    int idealH() const { return kTitleH + rows() * kCellH + kPad; }

    void paint (Graphics& g) override
    {
        g.setColour (cPanel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        g.setColour (cLine);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);
        g.setColour (cAcc);
        g.setFont (uiFont (11.0f, true));
        g.drawText (title.toUpperCase(), kPad + 2, 4, getWidth() - 2 * kPad, 16, Justification::centredLeft);
    }

    void resized() override
    {
        for (int i = 0; i < cells.size(); ++i)
            cells[i]->setBounds (kPad + (i % kMaxCols) * kCellW, kTitleH + (i / kMaxCols) * kCellH, kCellW, kCellH);
    }

private:
    String title;
    int n;
    OwnedArray<ParamCell> cells;
};

class SoundPanel : public Component
{
public:
    explicit SoundPanel (AudioProcessorValueTreeState& apvts)
    {
        int i = 0;
        while (i < sh32::kNumParams)
        {
            int j = i;
            while (j < sh32::kNumParams && std::strcmp (sh32::kParams[j].section, sh32::kParams[i].section) == 0) ++j;
            auto* s = boxes.add (new SectionBox (apvts, sh32::kParams[i].section, i, j - i));
            addAndMakeVisible (s);
            i = j;
        }
    }

    void resized() override
    {
        int x = 6, y = 6, rowH = 0;
        for (auto* b : boxes)
        {
            const int w = b->idealW(), h = b->idealH();
            if (x + w > getWidth() - 6 && x > 6) { x = 6; y += rowH + 8; rowH = 0; }
            b->setBounds (x, y, w, h);
            x += w + 8;
            rowH = jmax (rowH, h);
        }
    }

private:
    OwnedArray<SectionBox> boxes;
};

//==============================================================================
// Gammes pour la génération aléatoire
struct ScaleDef { const char* name; std::vector<int> iv; };
const std::vector<ScaleDef>& scales()
{
    static const std::vector<ScaleDef> s = {
        { "Chromatique", { 0,1,2,3,4,5,6,7,8,9,10,11 } }, { "Majeure", { 0,2,4,5,7,9,11 } },
        { "Mineure", { 0,2,3,5,7,8,10 } }, { "Pentatonique mineure", { 0,3,5,7,10 } },
        { "Dorienne", { 0,2,3,5,7,9,10 } }, { "Blues", { 0,3,5,6,7,10 } } };
    return s;
}

//==============================================================================
// Grille mono, piano roll poly et lane, dans un seul composant (colonnes alignées).
class SeqCanvas : public Component
{
public:
    static constexpr int keyW = 48, rulerH = 18, rows = 24, rowH = 14, laneH = 56, gapY = 6;
    static constexpr int mainH = rows * rowH;
    static constexpr int totalH = rulerH + mainH + gapY + laneH + 4;

    explicit SeqCanvas (SH32Processor& p) : proc (p)
    {
        modeP = proc.apvts.getRawParameterValue ("mode");
        lenP  = proc.apvts.getRawParameterValue ("len");
        slotP = proc.apvts.getRawParameterValue ("slot");
        chP   = proc.apvts.getRawParameterValue ("ch");
        laneP = proc.apvts.getRawParameterValue ("laneOn");
        setWantsKeyboardFocus (false);
    }

    bool isPoly() const { return modeP->load() >= 0.5f; }
    int  len()    const { return jlimit (1, 16, (int) std::lround (lenP->load()) + 1); }
    int  slot()   const { return jlimit (0, sh32::kSlots - 1, (int) std::lround (slotP->load())); }
    int  chan()   const { return jlimit (0, 15, (int) std::lround (chP->load())); }
    sh32::Pattern& pat() { return proc.uiBank().p[(size_t) slot()]; }

    void commit() { proc.publishBank(); repaint(); }
    void setRollBase (int b) { rollBase = jlimit (0, 96, b); repaint(); }
    int  getRollBase() const { return rollBase; }
    void setAccentTool (bool on) { accentTool = on; }

    void setCurrentStep (int s) { if (s != curStep) { curStep = s; repaint(); } }

    void audition (int note)
    {
        stopAudition();
        proc.keyboardState.noteOn (chan() + 1, note, 0.7f);
        audNote = note; audCh = chan() + 1;
        audUntil = Time::getMillisecondCounter() + 160;
    }
    void stopAudition()
    {
        if (audNote >= 0) { proc.keyboardState.noteOff (audCh, audNote, 0.0f); audNote = -1; }
    }
    void tickAudition()
    {
        if (audNote >= 0 && Time::getMillisecondCounter() > audUntil) stopAudition();
    }

    //--------------------------------------------------------------------------
    void paint (Graphics& g) override
    {
        g.fillAll (cPanel);
        paintRuler (g);
        if (isPoly()) paintRoll (g); else paintMono (g);
        paintLane (g);
    }

    void mouseDown (const MouseEvent& e) override
    {
        drag = Drag::None;
        const auto p = e.position;
        if (laneRect().toFloat().contains (p)) { drag = Drag::Lane; laneEdit (e); return; }
        if (! mainRect().toFloat().contains (p)) return;
        if (isPoly()) polyDown (e); else monoDown (e);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        switch (drag)
        {
            case Drag::Lane:     laneEdit (e); break;
            case Drag::MonoNote: monoNoteEdit (e, false); break;
            case Drag::PolyResize:
            case Drag::PolyMove: polyDrag (e); break;
            case Drag::None:     break;
        }
    }

    void mouseUp (const MouseEvent&) override
    {
        if (drag == Drag::PolyMove && ! polyMoved) { pat().removeNote (polyIdx); commit(); }
        drag = Drag::None;
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (isPoly() || ! mainRect().toFloat().contains (e.position)) return;
        const int c = colAt (e.position.x);
        if (c < 0 || c >= sh32::kSteps || e.position.y >= rulerH + barH()) return;
        auto& s = pat().steps[(size_t) c];
        const int d = (w.deltaY > 0 ? 1 : -1) * (e.mods.isShiftDown() ? 12 : 1);
        s.note = jlimit (0, 127, s.note + d);
        audition (s.note);
        commit();
    }

private:
    enum class Drag { None, Lane, MonoNote, PolyResize, PolyMove };

    SH32Processor& proc;
    std::atomic<float> *modeP = nullptr, *lenP = nullptr, *slotP = nullptr, *chP = nullptr, *laneP = nullptr;
    Drag drag = Drag::None;
    int  curStep = -1, monoCol = 0, polyIdx = -1, polyGrab = 0, rollBase = 48;
    bool polyMoved = false, accentTool = false;
    int  audNote = -1, audCh = 1;
    uint32 audUntil = 0;

    float colW() const { return (getWidth() - keyW) / (float) sh32::kSteps; }
    int   colAt (float x) const { return (int) std::floor ((x - keyW) / colW()); }
    int   barH() const { return mainH - 56; }
    Rectangle<int> mainRect() const { return { 0, rulerH, getWidth(), mainH }; }
    Rectangle<int> laneRect() const { return { 0, rulerH + mainH + gapY, getWidth(), laneH }; }

    //--------------------------------------------------------------------------
    void paintRuler (Graphics& g)
    {
        const float cw = colW();
        g.setFont (uiFont (10.0f));
        for (int c = 0; c < sh32::kSteps; ++c)
        {
            Rectangle<float> r (keyW + c * cw, 0.0f, cw, (float) rulerH);
            if (c == curStep) { g.setColour (cAcc2); g.fillRoundedRectangle (r.reduced (2, 1), 3.0f); }
            g.setColour (c == curStep ? Colour (0xff001018) : (c % 4 == 0 ? cAcc2 : cDim));
            g.drawText (String (c + 1), r.toNearestInt(), Justification::centred);
        }
    }

    void dimOutsideLength (Graphics& g, Rectangle<int> area)
    {
        const int L = len();
        if (L >= sh32::kSteps) return;
        g.setColour (Colour (0x99121418));
        g.fillRect ((int) (keyW + L * colW()), area.getY(), (int) (area.getRight() - (keyW + L * colW())), area.getHeight());
    }

    //--------------------------------------------------------------------------
    void paintMono (Graphics& g)
    {
        const float cw = colW();
        const int top = rulerH, bh = barH();
        g.setFont (uiFont (10.0f));
        g.setColour (cDim);
        g.drawText ("Note", 0, top, keyW - 4, 16, Justification::centredRight);
        g.drawText ("On",   0, top + bh + 6, keyW - 4, 22, Justification::centredRight);
        g.drawText ("A / T", 0, top + bh + 32, keyW - 4, 20, Justification::centredRight);

        auto& p = pat();
        for (int c = 0; c < sh32::kSteps; ++c)
        {
            const auto& s = p.steps[(size_t) c];
            const float x = keyW + c * cw + 2.0f, w = cw - 4.0f;

            // barre de hauteur
            Rectangle<float> bar (x, (float) top, w, (float) bh);
            g.setColour (Colour (0xff14171d)); g.fillRoundedRectangle (bar, 4.0f);
            const float f = jlimit (0.0f, 1.0f, (s.note - sh32::kNoteMin) / (float) (sh32::kNoteMax - sh32::kNoteMin));
            g.setColour (s.on ? cAcc : Colour (0xff5a3220));
            g.fillRect (Rectangle<float> (x, top + bh * (1.0f - f), w, bh * f));
            g.setColour (c == curStep ? cAcc2 : cLine);
            g.drawRoundedRectangle (bar, 4.0f, 1.0f);
            g.setColour (Colours::white);
            g.setFont (uiFont (10.0f));
            g.drawText (noteName (s.note), bar.toNearestInt().removeFromTop (16), Justification::centred);

            // bouton on
            Rectangle<float> on (x, (float) (top + bh + 6), w, 22.0f);
            g.setColour (s.on ? cAcc : cPanel2); g.fillRoundedRectangle (on, 5.0f);
            g.setColour (s.on ? Colour (0xff111111) : cDim);
            g.fillEllipse (on.getCentreX() - 3.5f, on.getCentreY() - 3.5f, 7.0f, 7.0f);

            // accent / liaison
            Rectangle<float> fl (x, (float) (top + bh + 32), w, 20.0f);
            auto a = fl.removeFromLeft (fl.getWidth() * 0.5f - 1.0f), t = fl.withTrimmedLeft (2.0f);
            g.setFont (uiFont (10.0f, true));
            g.setColour (s.acc ? cAcc2 : cPanel2); g.fillRoundedRectangle (a, 4.0f);
            g.setColour (s.acc ? Colour (0xff001018) : cDim); g.drawText ("A", a.toNearestInt(), Justification::centred);
            g.setColour (s.tie ? cAcc2 : cPanel2); g.fillRoundedRectangle (t, 4.0f);
            g.setColour (s.tie ? Colour (0xff001018) : cDim); g.drawText ("T", t.toNearestInt(), Justification::centred);
        }
        dimOutsideLength (g, mainRect());
    }

    void monoDown (const MouseEvent& e)
    {
        const int c = colAt (e.position.x);
        if (c < 0 || c >= sh32::kSteps) return;
        auto& s = pat().steps[(size_t) c];
        const float y = e.position.y - rulerH;
        const int bh = barH();

        if (y < bh)                      // barre : hauteur de la note
        {
            drag = Drag::MonoNote; monoCol = c;
            monoNoteEdit (e, true);
        }
        else if (y >= bh + 6 && y < bh + 28)      // bouton on
        {
            s.on = ! s.on; commit();
        }
        else if (y >= bh + 32 && y < bh + 52)     // accent / liaison
        {
            const float rel = e.position.x - (keyW + c * colW());
            if (rel < colW() * 0.5f) s.acc = ! s.acc; else s.tie = ! s.tie;
            commit();
        }
    }

    void monoNoteEdit (const MouseEvent& e, bool first)
    {
        auto& s = pat().steps[(size_t) monoCol];
        const float f = jlimit (0.0f, 1.0f, 1.0f - (e.position.y - rulerH) / (float) barH());
        const int n = sh32::kNoteMin + (int) std::lround (f * (sh32::kNoteMax - sh32::kNoteMin));
        const bool changed = n != s.note || ! s.on;
        s.note = n; s.on = true;
        if (changed || first) audition (n);
        if (changed) commit();
    }

    //--------------------------------------------------------------------------
    int rowOf (int note) const { return rollBase + rows - 1 - note; }

    void paintRoll (Graphics& g)
    {
        const float cw = colW();
        const int top = rulerH, topNote = rollBase + rows - 1;
        g.setFont (uiFont (9.0f));
        for (int r = 0; r < rows; ++r)
        {
            const int n = topNote - r;
            const int y = top + r * rowH;
            const bool blk = isBlackKey (n);
            g.setColour (blk ? Colour (0xff171a20) : Colour (0xff1e222b));
            g.fillRect (keyW, y, getWidth() - keyW, rowH);
            g.setColour (blk ? Colour (0xff0f1115) : Colour (0xffe9e9e5));
            g.fillRect (0, y, keyW - 2, rowH - 1);
            if (n % 12 == 0)
            {
                g.setColour (Colour (0xff2f3542)); g.fillRect (keyW, y + rowH - 1, getWidth() - keyW, 1);
                g.setColour (Colour (0xff555555)); g.drawText (noteName (n), 4, y, keyW - 8, rowH, Justification::centredLeft);
            }
        }
        for (int c = 0; c <= sh32::kSteps; ++c)
        {
            g.setColour (c % 4 == 0 ? Colour (0xff3a4152) : Colour (0xff2a2f3a));
            g.fillRect ((int) (keyW + c * cw), top, 1, mainH);
        }
        if (curStep >= 0)
        {
            g.setColour (cAcc2.withAlpha (0.14f));
            g.fillRect (Rectangle<float> (keyW + curStep * cw, (float) top, cw, (float) mainH));
        }

        auto& p = pat();
        for (int i = 0; i < p.numNotes; ++i)
        {
            const auto& nt = p.notes[(size_t) i];
            if (nt.n < rollBase || nt.n > topNote) continue;
            Rectangle<float> r (keyW + nt.s * cw + 1.0f, (float) (top + (topNote - nt.n) * rowH + 1), nt.l * cw - 2.0f, (float) rowH - 2.0f);
            const float alpha = nt.s >= len() ? 0.35f : 1.0f;
            g.setColour ((nt.a ? cAcc2 : cAcc).withAlpha (alpha));
            g.fillRect (r);
            g.setColour (Colours::black.withAlpha (0.28f * alpha));
            g.fillRect (r.removeFromRight (4.0f));
        }
        dimOutsideLength (g, mainRect());
    }

    void polyDown (const MouseEvent& e)
    {
        auto& p = pat();
        const int row = (int) std::floor ((e.position.y - rulerH) / (float) rowH);
        if (row < 0 || row >= rows) return;
        const int note = rollBase + rows - 1 - row;

        if (e.position.x < keyW) { audition (note); return; }

        const float cwf = (e.position.x - keyW) / colW();
        const int col = (int) std::floor (cwf);
        const float fx = cwf - (float) col;
        if (col < 0 || col >= sh32::kSteps) return;

        const int hit = p.findNote (col, note);

        if (e.mods.isPopupMenu())                 // clic droit : accent
        {
            if (hit >= 0) { p.notes[(size_t) hit].a = ! p.notes[(size_t) hit].a; commit(); }
            return;
        }
        if (hit >= 0)
        {
            auto& nt = p.notes[(size_t) hit];
            if (e.mods.isShiftDown() || accentTool) { nt.a = ! nt.a; commit(); return; }
            const bool resize = (col == nt.s + nt.l - 1) && fx > 0.55f;
            drag = resize ? Drag::PolyResize : Drag::PolyMove;
            polyIdx = hit; polyGrab = col - nt.s; polyMoved = resize;
        }
        else
        {
            const int idx = p.addNote (col, note, 1, false);
            if (idx < 0) return;
            drag = Drag::PolyResize; polyIdx = idx; polyMoved = true;
            audition (note);
            commit();
        }
    }

    void polyDrag (const MouseEvent& e)
    {
        auto& p = pat();
        if (polyIdx < 0 || polyIdx >= p.numNotes) return;
        auto& nt = p.notes[(size_t) polyIdx];
        const int col = (int) std::floor ((e.position.x - keyW) / colW());
        const int row = (int) std::floor ((e.position.y - rulerH) / (float) rowH);

        if (drag == Drag::PolyResize)
        {
            int l = jlimit (1, sh32::kSteps - nt.s, col - nt.s + 1);
            for (int i = 0; i < p.numNotes; ++i)
                if (i != polyIdx && p.notes[(size_t) i].n == nt.n && p.notes[(size_t) i].s > nt.s)
                    l = jmin (l, p.notes[(size_t) i].s - nt.s);
            if (l != nt.l) { nt.l = l; polyMoved = true; commit(); }
        }
        else
        {
            const int s = jlimit (0, sh32::kSteps - nt.l, col - polyGrab);
            const int n = jlimit (rollBase, rollBase + rows - 1, rollBase + rows - 1 - row);
            if ((s != nt.s || n != nt.n) && ! p.overlaps (polyIdx, s, n, nt.l))
            {
                const bool pitch = n != nt.n;
                nt.s = s; nt.n = n; polyMoved = true;
                if (pitch) audition (n);
                commit();
            }
        }
    }

    //--------------------------------------------------------------------------
    void paintLane (Graphics& g)
    {
        const auto lr = laneRect();
        const float cw = colW();
        const bool on = laneP->load() >= 0.5f;
        g.setFont (uiFont (10.0f));
        g.setColour (cDim);
        g.drawText ("Lane", 0, lr.getY(), keyW - 4, lr.getHeight(), Justification::centredRight);
        auto& p = pat();
        for (int c = 0; c < sh32::kSteps; ++c)
        {
            Rectangle<float> r (keyW + c * cw + 2.0f, (float) lr.getY(), cw - 4.0f, (float) lr.getHeight());
            g.setColour (Colour (0xff14171d)); g.fillRoundedRectangle (r, 4.0f);
            const float f = p.steps[(size_t) c].lv / 127.0f;
            g.setColour (Colour (0xff1f6f86).withAlpha (on ? 1.0f : 0.35f));
            g.fillRect (Rectangle<float> (r.getX(), r.getBottom() - r.getHeight() * f, r.getWidth(), r.getHeight() * f));
            g.setColour (cLine); g.drawRoundedRectangle (r, 4.0f, 1.0f);
        }
        dimOutsideLength (g, lr);
    }

    void laneEdit (const MouseEvent& e)
    {
        const int c = colAt (e.position.x);
        if (c < 0 || c >= sh32::kSteps) return;
        const auto lr = laneRect();
        const float f = jlimit (0.0f, 1.0f, 1.0f - (e.position.y - lr.getY()) / (float) lr.getHeight());
        const int v = (int) std::lround (f * 127.0f);
        auto& s = pat().steps[(size_t) c];
        if (s.lv != v) { s.lv = v; commit(); }
    }
};

//==============================================================================
// Onglet « Séquenceur » : réglages + canvas
class SeqPanel : public Component
{
public:
    explicit SeqPanel (SH32Processor& p) : proc (p), canvas (p)
    {
        auto& a = proc.apvts;
        addAndMakeVisible (canvas);

        toggle (seqOn, U8 ("Séquenceur ON"), "seqOn", 110);
        combo (sync, "Sync", "sync", U8list ({ "Hôte", "Interne" }), 80);
        slider (bpm, "BPM", "bpm", 90);
        toggle (freeRun, "Lecture interne", "freeRun", 110);
        combo (mode, "Mode", "mode", { "Mono", "Poly" }, 70);
        combo (rate, "Vitesse", "rate", { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" }, 76);
        { StringArray l; for (int i = 1; i <= 16; ++i) l.add (String (i)); combo (len, "Longueur", "len", l, 60); }
        combo (dir, "Sens", "dir", U8list ({ "Avant", "Arrière", "Aller-retour", "Aléatoire" }), 110);
        slider (swing, "Swing", "swing", 110);
        slider (gate, "Gate", "gate", 110);
        slider (trans, "Transpo", "trans", 110);
        toggle (laneOn, "Lane par pas", "laneOn", 100);
        {
            StringArray l;
            for (int o = 0; o < sh32::knobCount(); ++o)
            {
                const auto& d = sh32::knobByOrdinal (o);
                l.add (String (d.section) + " - " + d.label);
            }
            combo (laneParam, "Cible", "laneParam", l, 190);
        }

        for (int k = 0; k < sh32::kSlots; ++k)
        {
            auto* b = slotBtns.add (new TextButton (String (k + 1)));
            b->setClickingTogglesState (false);
            b->onClick = [this, k] { setSlot (k); };
            addAndMakeVisible (b);
        }

        for (const auto& s : scales()) scaleBox.addItem (s.name, scaleBox.getNumItems() + 1);
        scaleBox.setSelectedItemIndex (2);
        addAndMakeVisible (scaleBox);
        for (int i = 0; i < 12; ++i) rootBox.addItem (noteName (i + 24).dropLastCharacters (1), i + 1);
        rootBox.setSelectedItemIndex (0);
        addAndMakeVisible (rootBox);

        button (randBtn, U8 ("Aléatoire"), [this] { randomize(); });
        button (clearBtn, "Vider", [this] { clearPattern(); });
        button (shL, "<", [this] { shift (-1); });
        button (shR, ">", [this] { shift (1); });
        button (octDn, "Octave -", [this] { canvas.setRollBase (canvas.getRollBase() - 12); updateRange(); });
        button (octUp, "Octave +", [this] { canvas.setRollBase (canvas.getRollBase() + 12); updateRange(); });
        button (accBtn, "Accent", [this] { canvas.setAccentTool (accBtn.getToggleState()); });
        accBtn.setClickingTogglesState (true);
        accBtn.setTooltip ("Poly : un clic sur une note l'accentue (aussi Maj+clic ou clic droit)");

        rangeLabel.setJustificationType (Justification::centred);
        addAndMakeVisible (rangeLabel);
        hint.setFont (uiFont (11.0f));
        hint.setColour (Label::textColourId, cDim);
        hint.setJustificationType (Justification::topLeft);
        hint.setText (U8 ("Mono : glisse une barre pour la note, ● active, A accent, T liaison (legato). "
                          "Poly : clic = note, tire le bord droit pour allonger, tire le corps pour déplacer, clic sur une note = supprimer. "
                          "Lane : valeur d'un paramètre envoyée à chaque pas."), dontSendNotification);
        addAndMakeVisible (hint);
        updateRange();
        (void) a;
    }

    void syncFromProcessor()
    {
        const int s = (int) std::lround (proc.apvts.getRawParameterValue ("slot")->load());
        for (int k = 0; k < slotBtns.size(); ++k) slotBtns[k]->setToggleState (k == s, dontSendNotification);
        slotLast = s;
        canvas.setCurrentStep (proc.currentStepForUi());
        canvas.tickAudition();
        if (canvas.isPoly() != lastPoly) { lastPoly = canvas.isPoly(); canvas.repaint(); resized(); }
    }

    void paint (Graphics& g) override { g.fillAll (cBg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);

        // réglages : ligne par ligne, retour à la ligne automatique
        int x = r.getX(), y = r.getY();
        const int rowH = 26, gap = 10;
        auto flow = [&] (Component& c, int w, Label* l = nullptr, int lw = 0)
        {
            if (x + lw + w > r.getRight() && x > r.getX()) { x = r.getX(); y += rowH + 6; }
            if (l) { l->setBounds (x, y, lw, rowH); x += lw; }
            c.setBounds (x, y, w, rowH);
            x += w + gap;
        };
        for (auto& e : ctrls) flow (*e.comp, e.w, e.label.get(), e.label ? e.lw : 0);

        const int sw = 30;
        if (x + sw * 8 > r.getRight()) { x = r.getX(); y += rowH + 6; }
        for (auto* b : slotBtns) { b->setBounds (x, y, sw, rowH); x += sw + 2; }
        x += gap;
        flow (scaleBox, 150); flow (rootBox, 56);
        flow (randBtn, 84); flow (clearBtn, 60); flow (shL, 30); flow (shR, 30);
        flow (octDn, 80); flow (rangeLabel, 70); flow (octUp, 80); flow (accBtn, 70);

        y += rowH + 8;
        const int ch = SeqCanvas::totalH;
        canvas.setBounds (r.getX(), y, r.getWidth(), ch);
        hint.setBounds (r.getX(), y + ch + 4, r.getWidth(), 34);
    }

private:
    struct Ctl { std::unique_ptr<Label> label; Component* comp; int w, lw; };

    SH32Processor& proc;
    SeqCanvas canvas;
    std::vector<Ctl> ctrls;
    int slotLast = -1;
    bool lastPoly = false;

    TextButton seqOn, freeRun, laneOn, randBtn, clearBtn, shL, shR, octDn, octUp, accBtn;
    ComboBox sync, mode, rate, len, dir, laneParam, scaleBox, rootBox;
    Slider bpm, swing, gate, trans;
    OwnedArray<TextButton> slotBtns;
    Label rangeLabel, hint;

    std::vector<std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment>>   bAtt;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>> cAtt;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>>   sAtt;

    void addCtl (Component& c, int w, const String& text)
    {
        Ctl e; e.comp = &c; e.w = w; e.lw = 0;
        if (text.isNotEmpty())
        {
            e.label = std::make_unique<Label> (String(), text);
            e.label->setFont (uiFont (11.0f));
            e.label->setColour (Label::textColourId, cDim);
            e.label->setJustificationType (Justification::centredRight);
            e.lw = (int) std::ceil (GlyphArrangement::getStringWidth (uiFont (11.0f), text)) + 8;
            addAndMakeVisible (*e.label);
        }
        addAndMakeVisible (c);
        ctrls.push_back (std::move (e));
    }

    void toggle (TextButton& b, const String& text, const char* id, int w)
    {
        b.setButtonText (text); b.setClickingTogglesState (true);
        bAtt.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, b));
        addCtl (b, w, {});
    }
    void combo (ComboBox& c, const String& text, const char* id, const StringArray& items, int w)
    {
        c.addItemList (items, 1);
        cAtt.push_back (std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c));
        addCtl (c, w, text);
    }
    void slider (Slider& s, const String& text, const char* id, int w)
    {
        s.setSliderStyle (Slider::LinearHorizontal);
        s.setTextBoxStyle (Slider::TextBoxRight, false, 38, 20);
        sAtt.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s));
        addCtl (s, w, text);
    }
    void button (TextButton& b, const String& text, std::function<void()> fn)
    {
        b.setButtonText (text); b.onClick = std::move (fn);
        addAndMakeVisible (b);
    }

    void setSlot (int k)
    {
        auto* p = proc.apvts.getParameter ("slot");
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) k));
        p->endChangeGesture();
        syncFromProcessor();
        canvas.repaint();
    }

    void updateRange()
    {
        const int b = canvas.getRollBase();
        rangeLabel.setText (noteName (b) + " - " + noteName (b + SeqCanvas::rows - 1), dontSendNotification);
    }

    int laneIndexOfMode() const { return canvas.isPoly() ? 1 : 0; }

    void randomize()
    {
        Random rnd;
        auto& p = canvas.pat();
        const auto& sc = scales()[(size_t) jmax (0, scaleBox.getSelectedItemIndex())].iv;
        const int root = jmax (0, rootBox.getSelectedItemIndex());
        const int L = canvas.len();

        if (! canvas.isPoly())
        {
            std::vector<int> pool;
            for (int n = sh32::kNoteMin; n <= sh32::kNoteMax - 12; ++n)
                if (std::find (sc.begin(), sc.end(), (((n - root) % 12) + 12) % 12) != sc.end()) pool.push_back (n);
            for (int i = 0; i < sh32::kSteps; ++i)
            {
                auto& s = p.steps[(size_t) i];
                s.on  = i == 0 || rnd.nextFloat() < 0.65f;
                s.note = pool[(size_t) (rnd.nextFloat() * rnd.nextFloat() * (float) pool.size())];
                s.acc = rnd.nextFloat() < 0.2f;
                s.tie = s.on && rnd.nextFloat() < 0.12f;
                s.lv  = rnd.nextInt (128);
            }
        }
        else
        {
            std::vector<int> degs = (scaleBox.getSelectedItemIndex() <= 0 || sc.size() < 5) ? scales()[2].iv : sc;
            const int rootN = 48 + root, D = (int) degs.size();
            auto degNote = [&] (int k) { return rootN + 12 * (k / D) + degs[(size_t) (k % D)]; };
            static const int pick[7] = { 0, 5, 3, 4, 2, 6, 1 };
            p.clearPoly();
            for (int b = 0; b * 4 < L; ++b)
            {
                const int k = b == 0 ? 0 : pick[rnd.nextInt (7)];
                std::vector<int> chord { degNote (k), degNote (k + 2), degNote (k + 4) };
                if (rnd.nextFloat() < 0.4f) chord.push_back (degNote (k + 6));
                const float r = rnd.nextFloat();
                std::vector<std::pair<int,int>> hits;
                if (r < 0.5f) hits = { { 0, 4 } }; else if (r < 0.75f) hits = { { 0, 2 }, { 2, 2 } }; else hits = { { 0, 1 }, { 2, 2 } };
                for (auto& h : hits)
                {
                    const bool acc = rnd.nextFloat() < 0.25f;
                    for (size_t j = 0; j < chord.size(); ++j)
                        if (chord[j] >= 0 && chord[j] <= 127 && b * 4 + h.first < sh32::kSteps)
                            p.addNote (b * 4 + h.first, chord[j], jmin (h.second, sh32::kSteps - (b * 4 + h.first)), acc && j == 0);
                }
            }
            canvas.setRollBase (jlimit (0, 96, (rootN / 12) * 12));
            updateRange();
        }
        canvas.commit();
    }

    void clearPattern()
    {
        auto& p = canvas.pat();
        if (canvas.isPoly()) p.clearPoly(); else p.clearMono();
        canvas.commit();
    }

    void shift (int amount)
    {
        auto& p = canvas.pat();
        const int L = canvas.len();
        if (canvas.isPoly())
        {
            for (int i = 0; i < p.numNotes; ++i)
            {
                auto& n = p.notes[(size_t) i];
                if (n.s < L) { n.s = (n.s + amount + L) % L; n.l = jmin (n.l, sh32::kSteps - n.s); }
            }
        }
        else
        {
            std::vector<sh32::Step> v (p.steps.begin(), p.steps.begin() + L);
            if (amount < 0) std::rotate (v.begin(), v.begin() + 1, v.end());
            else         std::rotate (v.rbegin(), v.rbegin() + 1, v.rend());
            std::copy (v.begin(), v.end(), p.steps.begin());
        }
        canvas.commit();
    }
};

//==============================================================================
// Contenu complet de la fenêtre
class EditorContent : public Component, private Timer
{
public:
    explicit EditorContent (SH32Processor& p)
        : proc (p), soundPanel (p.apvts), seqPanel (p),
          keyboard (p.keyboardState, MidiKeyboardComponent::horizontalKeyboard)
    {
        setLookAndFeel (&laf);

        title.setText ("ROLAND SH-32  -  CONTROLLER", dontSendNotification);
        title.setFont (uiFont (16.0f, true));
        title.setColour (Label::textColourId, cText);
        addAndMakeVisible (title);

        chLabel.setText ("Canal", dontSendNotification);
        chLabel.setColour (Label::textColourId, cDim);
        addAndMakeVisible (chLabel);
        for (int i = 1; i <= 16; ++i) chBox.addItem (String (i), i);
        addAndMakeVisible (chBox);
        chAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "ch", chBox);

        thruBtn.setButtonText ("MIDI Thru");   thruBtn.setClickingTogglesState (true);
        playBtn.setButtonText ("Envoyer au Play"); playBtn.setClickingTogglesState (true);
        addAndMakeVisible (thruBtn); addAndMakeVisible (playBtn);
        thruAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "thru", thruBtn);
        playAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "sendOnPlay", playBtn);

        sendAllBtn.setButtonText ("Tout envoyer");
        sendAllBtn.setTooltip (U8 ("Envoie la valeur de tous les paramètres au SH-32"));
        sendAllBtn.onClick = [this] { proc.sendAllRequest.store (true); };
        panicBtn.setButtonText ("PANIC");
        panicBtn.setColour (TextButton::buttonColourId, Colour (0xff6b2a37));
        panicBtn.onClick = [this] { proc.panicRequest.store (true); };
        addAndMakeVisible (sendAllBtn); addAndMakeVisible (panicBtn);

        for (auto* s : { &bankMsb, &bankLsb, &progNum })
        {
            s->setSliderStyle (Slider::IncDecButtons);
            s->setRange (0, 127, 1);
            s->setTextBoxStyle (Slider::TextBoxLeft, false, 36, 22);
            addAndMakeVisible (*s);
        }
        bankLbl.setText ("Bank MSB / LSB / Prog", dontSendNotification);
        bankLbl.setColour (Label::textColourId, cDim);
        addAndMakeVisible (bankLbl);
        pcBtn.setButtonText ("Envoyer PC");
        pcBtn.onClick = [this]
        {
            proc.programRequest.store (((int) bankMsb.getValue() << 14) | ((int) bankLsb.getValue() << 7) | (int) progNum.getValue());
        };
        addAndMakeVisible (pcBtn);

        tabSound.setButtonText ("Son");
        tabSeq.setButtonText (U8 ("Séquenceur"));
        for (auto* t : { &tabSound, &tabSeq })
        {
            t->setClickingTogglesState (true);
            t->setRadioGroupId (101);
            addAndMakeVisible (*t);
        }
        tabSound.onClick = [this] { showTab (0); };
        tabSeq.onClick   = [this] { showTab (1); };

        addAndMakeVisible (soundPanel);
        addChildComponent (seqPanel);
        tabSound.setToggleState (true, dontSendNotification);

        keyboard.setKeyWidth (22.0f);
        keyboard.setAvailableRange (24, 96);
        keyboard.setOctaveForMiddleC (3);
        keyboard.setScrollButtonsVisible (false);
        keyboard.setWantsKeyboardFocus (false);
        keyboard.clearKeyMappings();             // ne vole pas le clavier de l'ordinateur à Cubase
        addAndMakeVisible (keyboard);

        startTimerHz (30);
    }

    ~EditorContent() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (Graphics& g) override { g.fillAll (cBg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);

        // bandeau haut : deux lignes
        auto top1 = r.removeFromTop (30);
        title.setBounds (top1.removeFromLeft (290));
        chLabel.setBounds (top1.removeFromLeft (46));
        chBox.setBounds (top1.removeFromLeft (64).reduced (0, 2));
        top1.removeFromLeft (10);
        thruBtn.setBounds (top1.removeFromLeft (92).reduced (0, 2));
        playBtn.setBounds (top1.removeFromLeft (130).reduced (2, 2));
        sendAllBtn.setBounds (top1.removeFromLeft (110).reduced (2, 2));
        panicBtn.setBounds (top1.removeFromLeft (80).reduced (2, 2));

        r.removeFromTop (4);
        auto top2 = r.removeFromTop (26);
        bankLbl.setBounds (top2.removeFromLeft (150));
        bankMsb.setBounds (top2.removeFromLeft (96));
        bankLsb.setBounds (top2.removeFromLeft (96));
        progNum.setBounds (top2.removeFromLeft (96));
        pcBtn.setBounds (top2.removeFromLeft (96).reduced (4, 0));
        auto tabs = top2.removeFromRight (230);
        tabSound.setBounds (tabs.removeFromLeft (110).reduced (2, 0));
        tabSeq.setBounds (tabs.reduced (2, 0));

        r.removeFromTop (6);
        keyboard.setBounds (r.removeFromBottom (74));
        r.removeFromBottom (6);
        soundPanel.setBounds (r);
        seqPanel.setBounds (r);
    }

private:
    SH32Processor& proc;
    SH32LookAndFeel laf;

    Label title, chLabel, bankLbl;
    ComboBox chBox;
    TextButton thruBtn, playBtn, sendAllBtn, panicBtn, pcBtn, tabSound, tabSeq;
    Slider bankMsb, bankLsb, progNum;
    SoundPanel soundPanel;
    SeqPanel seqPanel;
    MidiKeyboardComponent keyboard;

    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> chAtt;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> thruAtt, playAtt;

    void showTab (int t)
    {
        soundPanel.setVisible (t == 0);
        seqPanel.setVisible (t == 1);
    }

    void timerCallback() override
    {
        keyboard.setMidiChannel (jlimit (0, 15, (int) std::lround (proc.apvts.getRawParameterValue ("ch")->load())) + 1);
        if (seqPanel.isVisible()) seqPanel.syncFromProcessor();
    }
};
} // namespace

//==============================================================================
SH32Editor::SH32Editor (SH32Processor& p) : AudioProcessorEditor (&p)
{
    content = std::make_unique<EditorContent> (p);
    addAndMakeVisible (*content);
    setSize (1120, 790);
}

SH32Editor::~SH32Editor() = default;

void SH32Editor::resized()
{
    if (content != nullptr) content->setBounds (getLocalBounds());
}
