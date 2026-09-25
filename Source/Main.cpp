#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace solotrack
{

class MainContentComponent : public juce::AudioAppComponent
{
public:
    MainContentComponent()
    {
        // Device selector for driver (WASAPI Exclusive/ASIO), sample rate, and buffer size
        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager,
            0, 2,     // min/max audio inputs
            0, 2,     // min/max audio outputs
            false,    // show MIDI input options
            false,    // show MIDI output options
            false,    // treat channels as stereo pairs
            false     // hide advanced options with button
        );

        addAndMakeVisible(deviceSelector.get());

        // Request 2 inputs and 2 outputs
        setAudioChannels(2, 2);
        setSize(650, 480);
    }

    ~MainContentComponent() override
    {
        shutdownAudio();
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        juce::ignoreUnused(samplesPerBlockExpected, sampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device == nullptr)
        {
            bufferToFill.clearActiveBufferRegion();
            return;
        }

        // In AudioAppComponent, the shared buffer already carries input audio directly to output.
        // We clear inactive output channels to avoid noise.
        auto activeInputChannels  = device->getActiveInputChannels();
        auto activeOutputChannels = device->getActiveOutputChannels();
        auto maxInputChannels     = activeInputChannels.getHighestBit() + 1;
        auto maxOutputChannels    = activeOutputChannels.getHighestBit() + 1;

        for (auto ch = 0; ch < maxOutputChannels; ++ch)
        {
            if (!activeOutputChannels[ch] || maxInputChannels == 0)
                bufferToFill.buffer->clear(ch, bufferToFill.startSample, bufferToFill.numSamples);
        }
    }

    void releaseResources() override
    {
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff222222));
    }

    void resized() override
    {
        if (deviceSelector != nullptr)
            deviceSelector->setBounds(getLocalBounds().reduced(8));
    }

private:
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name)
        : DocumentWindow(name,
                         juce::Colour(0xff2b2b2b),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainContentComponent(), true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class SoloTrackApplication : public juce::JUCEApplication
{
public:
    SoloTrackApplication() {}

    const juce::String getApplicationName() override       { return "SoloTrack"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace solotrack

START_JUCE_APPLICATION(solotrack::SoloTrackApplication)
