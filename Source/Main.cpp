#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <functional>

namespace solotrack
{

class PlaybackEngine final
{
public:
    static constexpr int trackCount = 4;

    PlaybackEngine()
        : timeSliceThread("SoloTrack audio readers")
    {
        formatManager.registerBasicFormats();
        timeSliceThread.startThread(juce::Thread::Priority::normal);
    }

    ~PlaybackEngine()
    {
        releaseResources();
        timeSliceThread.stopThread(2000);
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate)
    {
        mixBuffer.setSize(2, samplesPerBlockExpected, false, true, true);
        for (auto& transport : transports)
            transport.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void releaseResources()
    {
        for (auto& transport : transports)
            transport.releaseResources();
        mixBuffer.setSize(0, 0);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
    {
        bufferToFill.buffer->clear(bufferToFill.startSample, bufferToFill.numSamples);

        const auto samples = juce::jmin(bufferToFill.numSamples, mixBuffer.getNumSamples());
        if (samples <= 0)
            return;

        bool anySolo = false;
        for (const auto& track : tracks)
            anySolo = anySolo || track.solo.load(std::memory_order_relaxed);

        for (int index = 0; index < trackCount; ++index)
        {
            mixBuffer.clear(0, 0, samples);
            mixBuffer.clear(1, 0, samples);

            juce::AudioSourceChannelInfo sourceInfo(&mixBuffer, 0, samples);
            transports[index].getNextAudioBlock(sourceInfo);

            const auto& track = tracks[index];
            if (track.mute.load(std::memory_order_relaxed)
                || (anySolo && !track.solo.load(std::memory_order_relaxed)))
                continue;

            const auto gain = track.gain.load(std::memory_order_relaxed);
            const auto pan = track.pan.load(std::memory_order_relaxed);
            const auto leftGain = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
            const auto rightGain = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);

            if (bufferToFill.buffer->getNumChannels() > 0)
                bufferToFill.buffer->addFrom(0, bufferToFill.startSample,
                                             mixBuffer, 0, 0, samples, leftGain);
            if (bufferToFill.buffer->getNumChannels() > 1)
                bufferToFill.buffer->addFrom(1, bufferToFill.startSample,
                                             mixBuffer, 1, 0, samples, rightGain);
        }
    }

    bool loadTrack(int index, const juce::File& file)
    {
        if (!juce::isPositiveAndBelow(index, trackCount))
            return false;

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
            return false;

        transports[index].stop();
        transports[index].setSource(nullptr);
        readerSources[index].reset();
        readerSources[index] = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
        transports[index].setSource(readerSources[index].get(), 32768, &timeSliceThread,
                                     readerSources[index]->getAudioFormatReader()->sampleRate, 2);
        loadedFiles[index] = file;
        return true;
    }

    void clearTrack(int index)
    {
        if (!juce::isPositiveAndBelow(index, trackCount))
            return;

        transports[index].stop();
        transports[index].setSource(nullptr);
        readerSources[index].reset();
        loadedFiles[index] = {};
    }

    void play()
    {
        for (auto& transport : transports)
            transport.start();
    }

    void pause()
    {
        for (auto& transport : transports)
            transport.stop();
    }

    void stop()
    {
        pause();
        setPosition(0.0);
    }

    void setPosition(double seconds)
    {
        for (auto& transport : transports)
            transport.setPosition(juce::jmax(0.0, seconds));
    }

    double getPosition() const
    {
        return transports[0].getCurrentPosition();
    }

    double getLength() const
    {
        double length = 0.0;
        for (const auto& transport : transports)
            length = juce::jmax(length, transport.getLengthInSeconds());
        return length;
    }

    std::array<std::atomic<float>, trackCount> gains{{1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<std::atomic<float>, trackCount> pans{{0.0f, 0.0f, 0.0f, 0.0f}};
    std::array<std::atomic<bool>, trackCount> mutes{{false, false, false, false}};
    std::array<std::atomic<bool>, trackCount> solos{{false, false, false, false}};

private:
    struct TrackState
    {
        std::atomic<float>& gain;
        std::atomic<float>& pan;
        std::atomic<bool>& mute;
        std::atomic<bool>& solo;
    };

    std::array<TrackState, trackCount> tracks{{
        { gains[0], pans[0], mutes[0], solos[0] },
        { gains[1], pans[1], mutes[1], solos[1] },
        { gains[2], pans[2], mutes[2], solos[2] },
        { gains[3], pans[3], mutes[3], solos[3] }
    }};

    juce::AudioFormatManager formatManager;
    juce::TimeSliceThread timeSliceThread;
    std::array<juce::AudioTransportSource, trackCount> transports;
    std::array<std::unique_ptr<juce::AudioFormatReaderSource>, trackCount> readerSources;
    std::array<juce::File, trackCount> loadedFiles;
    juce::AudioBuffer<float> mixBuffer;
};

class TrackStrip final : public juce::Component
{
public:
    TrackStrip(juce::String name, std::function<void(float)> gainChanged,
               std::function<void(float)> panChanged,
               std::function<void(bool)> muteChanged,
               std::function<void(bool)> soloChanged)
        : gainChanged(std::move(gainChanged)), panChanged(std::move(panChanged)),
          muteChanged(std::move(muteChanged)), soloChanged(std::move(soloChanged))
    {
        title.setText(std::move(name), juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(title);

        volume.setRange(-100.0, 6.0, 0.1);
        volume.setValue(0.0);
        volume.setSkewFactorFromMidPoint(-12.0);
        volume.textFromValueFunction = [] (double value)
        {
            return value <= -99.9 ? juce::String("-inf")
                                  : juce::String(value, 1) + " dB";
        };
        volume.setSliderStyle(juce::Slider::LinearVertical);
        volume.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 20);
        volume.onValueChange = [this]
        {
            if (this->gainChanged != nullptr)
                this->gainChanged(juce::Decibels::decibelsToGain((float) volume.getValue(), -100.0f));
        };
        addAndMakeVisible(volume);

        pan.setRange(-1.0, 1.0, 0.01);
        pan.setValue(0.0);
        pan.setTextValueSuffix(" pan");
        pan.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 20);
        pan.onValueChange = [this]
        {
            if (this->panChanged != nullptr)
                this->panChanged((float) pan.getValue());
        };
        addAndMakeVisible(pan);

        mute.setButtonText("Mute");
        mute.onClick = [this]
        {
            if (this->muteChanged != nullptr)
                this->muteChanged(mute.getToggleState());
        };
        addAndMakeVisible(mute);

        solo.setButtonText("Solo");
        solo.onClick = [this]
        {
            if (this->soloChanged != nullptr)
                this->soloChanged(solo.getToggleState());
        };
        addAndMakeVisible(solo);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff30343b));
        graphics.setColour(juce::Colour(0xff505761));
        graphics.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8);
        title.setBounds(bounds.removeFromTop(24));
        volume.setBounds(bounds.removeFromTop(150));
        pan.setBounds(bounds.removeFromTop(70));
        mute.setBounds(bounds.removeFromTop(28));
        solo.setBounds(bounds.removeFromTop(28));
    }

private:
    juce::Label title;
    juce::Slider volume;
    juce::Slider pan;
    juce::ToggleButton mute;
    juce::ToggleButton solo;
    std::function<void(float)> gainChanged;
    std::function<void(float)> panChanged;
    std::function<void(bool)> muteChanged;
    std::function<void(bool)> soloChanged;
};

class MainContentComponent final : public juce::AudioAppComponent,
                                   private juce::Timer
{
public:
    MainContentComponent()
    {
        openButton.setButtonText("Open Stem Folder");
        openButton.onClick = [this] { chooseStemFolder(); };
        addAndMakeVisible(openButton);

        playButton.setButtonText("Play");
        playButton.onClick = [this] { engine.play(); };
        addAndMakeVisible(playButton);

        pauseButton.setButtonText("Pause");
        pauseButton.onClick = [this] { engine.pause(); };
        addAndMakeVisible(pauseButton);

        stopButton.setButtonText("Stop");
        stopButton.onClick = [this] { engine.stop(); };
        addAndMakeVisible(stopButton);

        positionSlider.setRange(0.0, 1.0, 0.01);
        positionSlider.setTextValueSuffix(" s");
        positionSlider.onValueChange = [this]
        {
            if (positionSlider.isMouseButtonDown())
                engine.setPosition(positionSlider.getValue());
        };
        addAndMakeVisible(positionSlider);

        positionLabel.setText("00:00 / 00:00", juce::dontSendNotification);
        positionLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(positionLabel);

        statusLabel.setText("No stem folder loaded", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(statusLabel);

        const std::array<juce::String, PlaybackEngine::trackCount> names{
            "Drums", "Bass", "Vocals", "Other"
        };
        for (int index = 0; index < PlaybackEngine::trackCount; ++index)
        {
            strips[index] = std::make_unique<TrackStrip>(
                names[index],
                [this, index] (float value) { engine.gains[index].store(value, std::memory_order_relaxed); },
                [this, index] (float value) { engine.pans[index].store(value, std::memory_order_relaxed); },
                [this, index] (bool value) { engine.mutes[index].store(value, std::memory_order_relaxed); },
                [this, index] (bool value) { engine.solos[index].store(value, std::memory_order_relaxed); });
            addAndMakeVisible(strips[index].get());
        }

        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 2, 0, 2, false, false, false, false);
        addAndMakeVisible(deviceSelector.get());

        setAudioChannels(2, 2);
        setSize(900, 760);
        startTimerHz(20);
    }

    ~MainContentComponent() override
    {
        shutdownAudio();
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        engine.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        engine.getNextAudioBlock(bufferToFill);
    }

    void releaseResources() override
    {
        engine.releaseResources();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff222222));
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(12);
        auto controls = bounds.removeFromTop(34);
        openButton.setBounds(controls.removeFromLeft(150));
        controls.removeFromLeft(8);
        playButton.setBounds(controls.removeFromLeft(62));
        pauseButton.setBounds(controls.removeFromLeft(62));
        stopButton.setBounds(controls.removeFromLeft(62));
        positionLabel.setBounds(controls.removeFromRight(110));
        controls.removeFromRight(8);
        positionSlider.setBounds(controls);

        statusLabel.setBounds(bounds.removeFromTop(26));
        bounds.removeFromTop(8);

        auto stripArea = bounds.removeFromTop(270);
        const auto stripWidth = stripArea.getWidth() / PlaybackEngine::trackCount;
        for (int index = 0; index < PlaybackEngine::trackCount; ++index)
            strips[index]->setBounds(stripArea.removeFromLeft(stripWidth).reduced(3));

        bounds.removeFromTop(8);
        deviceSelector->setBounds(bounds);
    }

private:
    void timerCallback() override
    {
        const auto length = engine.getLength();
        positionSlider.setRange(0.0, juce::jmax(1.0, length), 0.01);
        if (!positionSlider.isMouseButtonDown())
            positionSlider.setValue(engine.getPosition(), juce::dontSendNotification);

        positionLabel.setText(formatTime(engine.getPosition()) + " / " + formatTime(length),
                               juce::dontSendNotification);
    }

    static juce::String formatTime(double seconds)
    {
        const auto totalSeconds = juce::jmax(0, (int) seconds);
        return juce::String::formatted("%02d:%02d", totalSeconds / 60, totalSeconds % 60);
    }

    void chooseStemFolder()
    {
        folderChooser = std::make_unique<juce::FileChooser>("Choose a stem folder");
        folderChooser->launchAsync(juce::FileBrowserComponent::openMode
                                       | juce::FileBrowserComponent::canSelectDirectories,
                                   [this] (const juce::FileChooser& chooser)
        {
            const auto folder = chooser.getResult();
            if (folder.isDirectory())
                loadStemFolder(folder);
            folderChooser.reset();
        });
    }

    void loadStemFolder(const juce::File& folder)
    {
        const std::array<juce::String, PlaybackEngine::trackCount> preferredNames{
            "drums.wav", "bass.wav", "vocals.wav", "other.wav"
        };
        std::array<juce::File, PlaybackEngine::trackCount> files;
        juce::Array<juce::File> candidates;
        folder.findChildFiles(candidates, juce::File::findFiles, false, "*.wav");

        for (int index = 0; index < PlaybackEngine::trackCount; ++index)
        {
            const auto preferred = folder.getChildFile(preferredNames[index]);
            if (preferred.existsAsFile())
                files[index] = preferred;
        }

        int nextCandidate = 0;
        for (int index = 0; index < PlaybackEngine::trackCount; ++index)
        {
            if (files[index].existsAsFile())
                continue;

            while (nextCandidate < candidates.size())
            {
                const auto candidate = candidates[nextCandidate++];
                bool alreadyUsed = false;
                for (const auto& selected : files)
                    alreadyUsed = alreadyUsed || selected == candidate;
                if (!alreadyUsed)
                {
                    files[index] = candidate;
                    break;
                }
            }
        }

        int loaded = 0;
        for (int index = 0; index < PlaybackEngine::trackCount; ++index)
        {
            engine.clearTrack(index);
            if (files[index].existsAsFile() && engine.loadTrack(index, files[index]))
                ++loaded;
        }

        engine.stop();
        statusLabel.setText(juce::String(loaded) + " stem(s) loaded from " + folder.getFileName(),
                            juce::dontSendNotification);
    }

    PlaybackEngine engine;
    std::array<std::unique_ptr<TrackStrip>, PlaybackEngine::trackCount> strips;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    std::unique_ptr<juce::FileChooser> folderChooser;
    juce::TextButton openButton;
    juce::TextButton playButton;
    juce::TextButton pauseButton;
    juce::TextButton stopButton;
    juce::Slider positionSlider;
    juce::Label positionLabel;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name)
        : DocumentWindow(name, juce::Colour(0xff2b2b2b), DocumentWindow::allButtons)
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

class SoloTrackApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SoloTrack"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace solotrack

START_JUCE_APPLICATION(solotrack::SoloTrackApplication)
