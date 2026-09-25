// ===========================================================================
//  SoloTrack Studio — Phase 1: Stereo pass-through proof of concept
//
//  A single-file juce::AudioAppComponent that routes live hardware input
//  straight to output. Purpose: verify the audio driver (WASAPI Exclusive /
//  ASIO), the sample-rate lock, and buffer-size switching from the selector
//  box — before we add the mixer, Rubber Band DSP, and loop engine.
//
//  AUDIO-THREAD RULES (PRD §4) — the callback is real-time and must stay:
//     * allocation-free      * I/O-free      * lock-free      * GUI-free
//  This pass-through does all of that (memcpy does not allocate).
// ===========================================================================

#include "JuceHeader.h"
#include <cstring>

namespace solotrack
{

class SoloTrackAudioApp final
    : public juce::AudioAppComponent
{
public:
    SoloTrackAudioApp()
    {
        // Stereo loopback: 2 hardware inputs -> 2 hardware outputs.
        // The selector component lets the user pick driver / sample rate /
        // buffer size at runtime; 128 is a sensible default.
        addAndMakeVisible(selector);
        setAudioChannels(2, 2);
        setAudioBufferSize(128);
    }

    ~SoloTrackAudioApp() override = default;

    // ------------------------------------------------------------------
    //  REAL-TIME AUDIO THREAD  (deterministic, no allocations, no locks)
    // ------------------------------------------------------------------
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
    {
        // Fill EVERY output channel so no device layout (0/2, 2/2, 4/2, ...)
        // ever emits uninitialised samples. Channels with a matching input
        // get a straight copy; the rest are zeroed.
        const int numIn  = info.totalNumInputChannels;
        const int numOut = info.totalNumOutputChannels;
        const size_t bytes = static_cast<size_t>(info.numSamples) * sizeof(float);

        for (int ch = 0; ch < numOut; ++ch)
        {
            float* const out = info.getWritePointer(ch);
            if (out == nullptr)
                continue;

            const float* const in = (ch < numIn) ? info.getReadPointer(ch) : nullptr;

            if (in != nullptr)
                std::memcpy(out, in, bytes);   // pass-through
            else
                std::memset(out, 0, bytes);    // silence
        }
    }

    // ------------------------------------------------------------------
    //  UI  —  just a title + the device selector box for Phase 1.
    // ------------------------------------------------------------------
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1e2128));
        g.setColour(juce::Colours::white);
        g.setFont(20.0f);
        g.drawText("SoloTrack Studio — Phase 1: stereo pass-through\n"
                   "Select your driver, sample rate and buffer size below, "
                   "then send a signal to your input and hear it echoed out.",
                   getLocalBounds().withHeight(64),
                   juce::Justification::topLeft);
    }

    void resized() override
    {
        selector.setBounds(getLocalBounds()
                               .withHeight(getHeight())
                               .withTop(72)
                               .reduced(16));
    }

private:
    juce::AudioDeviceSelectorComponent selector{&audioDeviceManager};

    JUCE_DECLARE_NON_COPYABLE(SoloTrackAudioApp)
};

class SoloTrackApplication final
    : public juce::JuceApplication
{
public:
    void initialise(const juce::String&) override
    {
        auto* audioApp = new SoloTrackAudioApp();
        appWindow.setContent(audioApp, true);
        appWindow.setResizable(true, false);
        appWindow.setResizeLimits(380, 260, 2000, 1400);
        appWindow.centreWithMinimumCurrentSize(380, 420);
        appWindow.setVisible(true);
    }

    void shutdown() override
    {
        appWindow.setContent(nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    juce::DocumentWindow appWindow{
        nullptr,
        "SoloTrack Studio",
        juce::Desktop::getInstance().getDefaultLookAndFeel()
            .findColour(juce::Desktop::windowBackgroundColour),
        juce::DocumentWindow::allButtons};

    JUCE_DECLARE_NON_COPYABLE(SoloTrackApplication)
};

} // namespace solotrack

START_JUCE_APPLICATION(solotrack::SoloTrackApplication)
