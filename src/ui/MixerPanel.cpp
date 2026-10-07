// raw-radio-studio — multitrack mixer UI (see MixerPanel.h).

#include "MixerPanel.h"

#include "BrandColours.h"
#include "BrandFonts.h"
#include "FaderTaper.h"

namespace rrs
{
    namespace
    {
        // Meter scale only (dBFS -> 0..1 for the level bars): the shared
        // -60..0 dBFS display scale from MeterBallistics (half = -30 dBFS). The
        // faders have their own, documented taper in FaderTaper.h — keep them
        // independent so changing the fader law never silently rescales the
        // meters.
        constexpr float meterFloorDb = MeterBallistics::meterFloorDb;
        constexpr float meterCeilDb  = MeterBallistics::meterCeilDb;
    }

    MixerPanel::MixerPanel (Session& sessionRef)
        : session (sessionRef)
    {
        session.addChangeListener (this);
        rebuildStrips();
        lastMeterMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (30);
    }

    MixerPanel::~MixerPanel()
    {
        stopTimer();
        session.removeChangeListener (this);
    }

    //==============================================================================
    void MixerPanel::rebuildStrips()
    {
        strips.clear();

        const int numTracks = session.getNumAudioTracks();

        if (numTracks <= 0 && session.getEdit() == nullptr)
        {
            stripCount = 0;
            meters.clear();
            return;
        }

        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (16); // title

        const int totalStrips = numTracks + 1; // + master
        const int gap = 6;
        const int masterWidth = juce::jlimit (56, 84, area.getWidth() / 8);
        const int trackSpace = area.getWidth() - masterWidth - gap * totalStrips;
        const int trackWidth = juce::jlimit (46, 88, trackSpace / juce::jmax (1, numTracks));

        int x = area.getX();

        // Only record/input tracks expose arm + input-mapping controls; imported
        // backing tracks are playback-only (BUG-2).
        const auto inputIndices = session.getInputTrackIndices();

        auto makeStrip = [&] (bool isMaster, int trackIndex, int width)
        {
            StripControls c;
            c.isMaster = isMaster;
            c.trackIndex = trackIndex;
            c.isInput = ! isMaster && inputIndices.contains (trackIndex);
            c.strip = juce::Rectangle<int> (x, area.getY(), width, area.getHeight());
            x += width + gap;

            auto inner = c.strip.reduced (4, 4);
            c.name = inner.removeFromTop (16);

            if (! isMaster)
            {
                // Always reserve the arm row so every strip's fader/meter stays
                // vertically aligned; only input tracks get the controls.
                auto armRow = inner.removeFromTop (18);

                if (c.isInput)
                {
                    c.arm = armRow.removeFromLeft (armRow.getWidth() / 2).reduced (1, 1);
                    armRow.removeFromLeft (2);
                    c.input = armRow.reduced (1, 1);
                }

                // Record trim row (FR-REC-4): reserved on every track strip so
                // faders stay aligned; only input tracks draw/accept it.
                auto trimRow = inner.removeFromTop (14);

                if (c.isInput)
                    c.trim = trimRow.reduced (1, 1);
            }

            c.meter = inner.removeFromRight (12);
            inner.removeFromRight (4);

            if (isMaster)
            {
                // FR-MIX-1: master fader + pan + mute (no solo: the master bus has
                // no sibling to isolate, so solo is not applicable at the master).
                auto buttons = inner.removeFromBottom (20);
                c.mute = buttons.reduced (1);

                auto panRow = inner.removeFromBottom (18);
                inner.removeFromBottom (4);
                c.pan = panRow;
                c.fader = inner;
            }
            else
            {
                auto buttons = inner.removeFromBottom (20);
                c.mute = buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1);
                buttons.removeFromLeft (2);
                c.solo = buttons.reduced (1);

                auto panRow = inner.removeFromBottom (18);
                inner.removeFromBottom (4);
                c.pan = panRow;
                c.fader = inner;
            }

            return c;
        };

        for (int i = 0; i < numTracks; ++i)
            strips.push_back (makeStrip (false, i, trackWidth));

        x += gap; // visual separation before the master strip
        strips.push_back (makeStrip (true, -1, juce::jmax (56, masterWidth - gap)));

        // Preserve the meter envelopes across a rebuild when the strip count is
        // unchanged. Rebuilds are triggered by every Session change message —
        // including each fader move — so resetting them here made the meters
        // jump/flicker exactly while the engineer was riding a fader.
        if (meters.size() != strips.size())
            meters.assign (strips.size(), MeterVisual {});

        stripCount = numTracks;
    }

    //==============================================================================
    float MixerPanel::meterNormalised (float db) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (db - meterFloorDb) / (meterCeilDb - meterFloorDb));
    }

    void MixerPanel::updateMeters()
    {
        if ((int) meters.size() != (int) strips.size())
            meters.assign (strips.size(), MeterVisual {});

        // dt-based ballistics: smooth and frame-rate independent, so the meter
        // falls at a fixed dB/s instead of jumping with each UI block.
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto dt  = lastMeterMs > 0.0 ? (float) ((now - lastMeterMs) / 1000.0)
                                           : 1.0f / 30.0f;
        lastMeterMs = now;

        for (size_t i = 0; i < strips.size(); ++i)
        {
            const auto& strip = strips[i];
            const auto reading = strip.isMaster ? session.readMasterMeter()
                                                : session.readTrackMeter (strip.trackIndex);
            const auto peakDb = juce::jmax (reading.peakDb[0], reading.peakDb[1]);

            auto& visual = meters[i];
            visual.ballistics.update (peakDb, dt);

            if (reading.clipped)
                visual.clipHold = 60;

            visual.clipped = visual.clipHold > 0;

            if (visual.clipHold > 0)
                --visual.clipHold;
        }
    }

    void MixerPanel::timerCallback()
    {
        if (session.getNumAudioTracks() != stripCount)
            rebuildStrips();

        updateMeters();
        repaint();
    }

    void MixerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        rebuildStrips();
        repaint();
    }

    //==============================================================================
    void MixerPanel::paint (juce::Graphics& g)
    {
        g.setColour (brand::bgPanel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

        auto title = getLocalBounds().reduced (12).removeFromTop (16);
        g.setColour (brand::textSecondary);
        g.setFont (brand::uiMedium (12.0f));
        g.drawText ("Mixer", title, juce::Justification::centredLeft);

        if (strips.empty())
        {
            g.setColour (brand::textTertiary);
            g.setFont (brand::uiRegular (12.0f));
            g.drawText ("No tracks", getLocalBounds().reduced (12), juce::Justification::centred);
            return;
        }

        for (size_t i = 0; i < strips.size(); ++i)
        {
            const auto& strip = strips[i];
            const auto gainDb = strip.isMaster ? session.getMasterGainDb()
                                               : session.getTrackGainDb (strip.trackIndex);
            const auto pan = strip.isMaster ? session.getMasterPan()
                                            : session.getTrackPan (strip.trackIndex);
            const auto muted = strip.isMaster ? session.isMasterMuted()
                                              : session.isTrackMuted (strip.trackIndex);
            const auto soloed = ! strip.isMaster && session.isTrackSolo (strip.trackIndex);
            const auto armed = ! strip.isMaster && session.isTrackArmed (strip.trackIndex);

            drawStrip (g, strip, meters[i], gainDb, pan, muted, soloed, armed);
        }
    }

    void MixerPanel::drawStrip (juce::Graphics& g, const StripControls& strip, const MeterVisual& meter,
                                float gainDb, float pan, bool muted, bool soloed, bool armed)
    {
        // Card background.
        g.setColour (strip.isMaster ? brand::bgElevated : brand::bgTertiary);
        g.fillRoundedRectangle (strip.strip.toFloat().reduced (1.0f), 6.0f);

        // Name.
        g.setColour (brand::textPrimary);
        g.setFont (brand::uiMedium (11.0f));
        g.drawText (strip.isMaster ? "Master" : session.getTrackName (strip.trackIndex),
                    strip.name, juce::Justification::centred, true);

        // Record-arm chip (FR-REC-2). Only input tracks are armable.
        if (strip.isInput && ! strip.arm.isEmpty())
        {
            const auto a = strip.arm;
            g.setColour (armed ? brand::accentMuted : brand::bgPanel);
            g.fillRoundedRectangle (a.toFloat(), 4.0f);
            g.setColour (armed ? brand::accent : brand::border);
            g.drawRoundedRectangle (a.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (armed ? brand::accent : brand::textSecondary);
            g.setFont (brand::uiSemiBold (11.0f));
            g.drawText ("R", a, juce::Justification::centred);
        }

        // Input-assignment chip (FR-REC-3): layout code + 1-based hardware
        // channel, e.g. "M1", "S2", "A1", "N1". Click to open the mapping menu.
        // Input tracks only — imported/backing tracks are playback-only (BUG-2).
        if (strip.isInput && ! strip.input.isEmpty())
        {
            const auto mapping = session.getTrackInputMapping (strip.trackIndex);

            juce::String code;
            switch (mapping.layout)
            {
                case InputLayout::Mono:         code = "M"; break;
                case InputLayout::Stereo:       code = "S"; break;
                case InputLayout::MultiChannel: code = "N"; break;
                case InputLayout::Auto:
                default:                        code = "A"; break;
            }

            const auto i = strip.input;
            g.setColour (brand::bgPanel);
            g.fillRoundedRectangle (i.toFloat(), 4.0f);
            g.setColour (brand::border);
            g.drawRoundedRectangle (i.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (brand::textSecondary);
            g.setFont (brand::uiMedium (10.0f));
            g.drawText (code + juce::String (mapping.firstChannel + 1), i, juce::Justification::centred);
        }

        // Record trim (FR-REC-4): a small horizontal bar, 0 dB centred, that
        // scales the input before it is monitored and recorded. Input tracks only.
        if (strip.isInput && ! strip.trim.isEmpty())
        {
            const auto trimDb = session.getTrackInputGainDb (strip.trackIndex);

            auto trimArea = strip.trim;
            auto valueArea = trimArea.removeFromLeft (26);
            auto barArea = trimArea;

            const auto left = (float) barArea.getX() + 3.0f;
            const auto right = (float) barArea.getRight() - 3.0f;
            const auto cy = (float) barArea.getCentreY();

            g.setColour (brand::meterTrough);
            g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 1.5f, juce::jmax (1.0f, right - left), 3.0f), 1.5f);

            // 0 dB centre tick: where the trim law is at unity (0.5; the
            // S-curve is symmetric about it).
            const auto centreX = left + fader::inputTrim.unityPos() * (right - left);
            g.setColour (brand::border);
            g.fillRect (juce::Rectangle<float> (centreX - 0.5f, cy - 3.5f, 1.0f, 7.0f));

            const auto thumbX = left + fader::inputTrim.dbToPos (trimDb) * (right - left);

            g.setColour (std::abs (trimDb) > 0.05f ? brand::accent : brand::textPrimary);
            g.fillEllipse (thumbX - 3.0f, cy - 3.0f, 6.0f, 6.0f);

            g.setColour (brand::textTertiary);
            g.setFont (brand::monoRegular (9.0f));
            g.drawText (juce::String (trimDb, 1), valueArea, juce::Justification::centred);
        }

        drawMeter (g, strip.meter, meter);

        // Fader.
        const auto& f = strip.fader;
        const auto cx = (float) f.getCentreX();
        const auto top = (float) f.getY() + 4.0f;
        const auto bottom = (float) f.getBottom() - 4.0f;
        // Fader law is linear in dB (FaderTaper.h): the thumb sits at the taper
        // position for the stored dB, and bottoms out to a mute detent. The
        // gain label shows "-inf" once the taper is at/below its floor.
        const auto t = fader::level.dbToPos (gainDb);
        const auto thumbY = bottom - t * (bottom - top);

        g.setColour (brand::meterTrough);
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - 2.0f, top, 4.0f, bottom - top), 2.0f);

        g.setColour (muted ? brand::textDisabled : brand::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - 2.0f, thumbY, 4.0f, bottom - thumbY), 2.0f);

        g.setColour (brand::textPrimary);
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - 9.0f, thumbY - 3.0f, 18.0f, 6.0f), 3.0f);

        g.setColour (brand::textTertiary);
        g.setFont (brand::monoRegular (10.0f));
        g.drawText (fader::level.isSilentDb (gainDb) ? juce::String ("-inf")
                                                     : juce::String (gainDb, 1),
                    juce::Rectangle<int> (f.getX(), (int) top - 2, f.getWidth(), 12),
                    juce::Justification::centred);

        // Pan (per track and master, FR-MIX-1).
        const auto& p = strip.pan;
        const auto pcx = (float) p.getCentreY();
        g.setColour (brand::meterTrough);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) p.getX() + 6.0f, pcx - 1.5f,
                                                        (float) p.getWidth() - 12.0f, 3.0f), 1.5f);
        const auto panX = (float) juce::jmap (juce::jlimit (-1.0f, 1.0f, pan), -1.0f, 1.0f,
                                              (float) p.getX() + 6.0f, (float) p.getRight() - 6.0f);
        g.setColour (brand::textPrimary);
        g.fillEllipse (panX - 3.5f, pcx - 3.5f, 7.0f, 7.0f);

        // Mute (per track and master) / Solo (tracks only — the master bus has no
        // sibling to isolate, so master solo is not applicable).
        auto drawChip = [&] (juce::Rectangle<int> area, const juce::String& label, bool on,
                             juce::Colour onColour)
        {
            const auto colour = on ? onColour : brand::textSecondary;
            g.setColour (on ? onColour.withAlpha (0.22f) : brand::bgPanel);
            g.fillRoundedRectangle (area.toFloat(), 4.0f);
            g.setColour (on ? onColour : brand::border);
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (colour);
            g.setFont (brand::uiSemiBold (11.0f));
            g.drawText (label, area, juce::Justification::centred);
        };

        drawChip (strip.mute, "M", muted, brand::record);

        if (! strip.isMaster)
            drawChip (strip.solo, "S", soloed, brand::warning);
    }

    void MixerPanel::drawMeter (juce::Graphics& g, juce::Rectangle<int> area, const MeterVisual& meter)
    {
        auto bar = area.reduced (2, 0).toFloat();
        g.setColour (brand::meterTrough);
        g.fillRoundedRectangle (bar, 2.0f);

        const auto level = meterNormalised (meter.ballistics.getDb());
        const auto hold  = meterNormalised (meter.ballistics.getHoldDb());

        const auto fill = bar.getHeight() * level;
        g.setColour (brand::vuGreen);
        g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - fill), 2.0f);

        const auto holdY = bar.getY() + bar.getHeight() * (1.0f - hold);

        if (hold > 0.0f)
        {
            g.setColour (brand::vuYellow);
            g.fillRect (juce::Rectangle<float> (bar.getX(), holdY - 1.0f, bar.getWidth(), 2.0f));
        }

        if (meter.clipped)
        {
            g.setColour (brand::vuRed);
            g.fillRoundedRectangle (bar.withHeight (3.0f), 1.5f);
        }
    }

    //==============================================================================
    void MixerPanel::resized()
    {
        rebuildStrips();
    }

    //==============================================================================
    void MixerPanel::mouseDown (const juce::MouseEvent& e)
    {
        dragTarget = DragTarget::None;
        dragIndex = -1;

        for (size_t i = 0; i < strips.size(); ++i)
        {
            const auto& strip = strips[i];

            if (strip.isInput && strip.arm.contains (e.getPosition()))
            {
                session.setTrackArmed (strip.trackIndex, ! session.isTrackArmed (strip.trackIndex));
                return;
            }

            if (strip.isInput && strip.input.contains (e.getPosition()))
            {
                showInputMenu (strip.trackIndex);
                return;
            }

            if (strip.isInput && strip.trim.contains (e.getPosition()))
            {
                dragTarget = DragTarget::Trim;
                dragIndex = (int) i;
                mouseDrag (e);
                return;
            }

            if (strip.mute.contains (e.getPosition()))
            {
                if (strip.isMaster)
                    session.setMasterMute (! session.isMasterMuted());
                else
                    session.setTrackMute (strip.trackIndex, ! session.isTrackMuted (strip.trackIndex));

                return;
            }

            if (strip.solo.contains (e.getPosition()))
            {
                if (! strip.isMaster)
                    session.setTrackSolo (strip.trackIndex, ! session.isTrackSolo (strip.trackIndex));

                return;
            }

            if (strip.fader.contains (e.getPosition()))
            {
                dragTarget = DragTarget::Fader;
                dragIndex = (int) i;
                mouseDrag (e);
                return;
            }

            if (strip.pan.contains (e.getPosition()))
            {
                dragTarget = DragTarget::Pan;
                dragIndex = (int) i;
                mouseDrag (e);
                return;
            }
        }
    }

    void MixerPanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragTarget == DragTarget::None || ! juce::isPositiveAndBelow (dragIndex, (int) strips.size()))
            return;

        const auto& strip = strips[(size_t) dragIndex];
        const auto pos = e.getPosition();

        if (dragTarget == DragTarget::Fader)
        {
            const auto top = (float) strip.fader.getY() + 4.0f;
            const auto bottom = (float) strip.fader.getBottom() - 4.0f;
            const auto t = juce::jlimit (0.0f, 1.0f, (bottom - (float) pos.y) / juce::jmax (1.0f, bottom - top));
            const auto db = fader::level.posToDb (t);

            if (strip.isMaster)
                session.setMasterGainDb (db);
            else
                session.setTrackGainDb (strip.trackIndex, db);
        }
        else if (dragTarget == DragTarget::Pan)
        {
            const auto left = (float) strip.pan.getX() + 6.0f;
            const auto right = (float) strip.pan.getRight() - 6.0f;
            const auto t = juce::jlimit (0.0f, 1.0f, ((float) pos.x - left) / juce::jmax (1.0f, right - left));

            if (strip.isMaster)
                session.setMasterPan (-1.0f + 2.0f * t);
            else
                session.setTrackPan (strip.trackIndex, -1.0f + 2.0f * t);
        }
        else if (dragTarget == DragTarget::Trim && strip.isInput)
        {
            auto bar = strip.trim;
            bar.removeFromLeft (26);
            const auto left = (float) bar.getX() + 3.0f;
            const auto right = (float) bar.getRight() - 3.0f;

            const auto t = juce::jlimit (0.0f, 1.0f,
                                         ((float) pos.x - left) / juce::jmax (1.0f, right - left));

            // Apply the gain live but defer persisting: writing the session file
            // on every mouse-move made the drag lag. `mouseUp` saves once.
            session.setTrackInputGainDb (strip.trackIndex, fader::inputTrim.posToDb (t), false);
            trimDragDirty = true;
        }

        repaint();
    }

    void MixerPanel::mouseUp (const juce::MouseEvent&)
    {
        // Persist a trim drag exactly once, after the pointer is released.
        if (dragTarget == DragTarget::Trim && trimDragDirty)
        {
            session.save();
            trimDragDirty = false;
        }

        dragTarget = DragTarget::None;
        dragIndex = -1;
    }

    //==============================================================================
    void MixerPanel::showInputMenu (int trackIndex)
    {
        auto mapping = session.getTrackInputMapping (trackIndex);
        const auto numChannels = juce::jmax (1, session.getNumInputChannels());

        juce::PopupMenu channelMenu;

        for (int ch = 0; ch < numChannels; ++ch)
            channelMenu.addItem (100 + ch, "Input " + juce::String (ch + 1), true,
                                 ch == mapping.firstChannel);

        juce::PopupMenu layoutMenu;
        layoutMenu.addItem (200, "Auto", true, mapping.layout == InputLayout::Auto);
        layoutMenu.addItem (201, "Mono", true, mapping.layout == InputLayout::Mono);
        layoutMenu.addItem (202, "Stereo", true, mapping.layout == InputLayout::Stereo);
        layoutMenu.addItem (203, "Multi-channel", true, mapping.layout == InputLayout::MultiChannel);

        juce::PopupMenu menu;
        menu.addSectionHeader ("Input for " + session.getTrackName (trackIndex));
        menu.addSubMenu ("Channel", channelMenu);
        menu.addSubMenu ("Layout", layoutMenu);

        juce::Component::SafePointer<MixerPanel> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe, trackIndex, numChannels] (int result)
                            {
                                auto* self = safe.getComponent();

                                if (self == nullptr || result == 0)
                                    return;

                                auto m = self->session.getTrackInputMapping (trackIndex);

                                if (result >= 100 && result < 100 + numChannels)
                                    m.firstChannel = result - 100;
                                else if (result == 200) m.layout = InputLayout::Auto;
                                else if (result == 201) m.layout = InputLayout::Mono;
                                else if (result == 202) m.layout = InputLayout::Stereo;
                                else if (result == 203) m.layout = InputLayout::MultiChannel;

                                // Keep the channel count coherent with the layout:
                                // Multi uses every remaining hardware channel.
                                if (m.layout == InputLayout::MultiChannel)
                                    m.numChannels = juce::jmax (1, numChannels - m.firstChannel);
                                else if (m.layout == InputLayout::Stereo)
                                    m.numChannels = 2;
                                else
                                    m.numChannels = 1;

                                self->session.setTrackInputMapping (trackIndex, m);
                                self->rebuildStrips();
                                self->repaint();
                            });
    }

    void MixerPanel::mouseDoubleClick (const juce::MouseEvent& e)
    {
        for (const auto& strip : strips)
        {
            if (strip.isInput && strip.trim.contains (e.getPosition()))
            {
                session.setTrackInputGainDb (strip.trackIndex, 0.0f);
                return;
            }

            if (strip.fader.contains (e.getPosition()))
            {
                if (strip.isMaster)
                    session.setMasterGainDb (0.0f);
                else
                    session.setTrackGainDb (strip.trackIndex, 0.0f);

                return;
            }

            if (strip.pan.contains (e.getPosition()))
            {
                if (strip.isMaster)
                    session.setMasterPan (0.0f);
                else
                    session.setTrackPan (strip.trackIndex, 0.0f);

                return;
            }
        }
    }
}
