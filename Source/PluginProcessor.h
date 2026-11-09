#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioTap.h"
#include "StreamServer.h"
#include "TunnelManager.h"

class PhoneMonitorProcessor : public juce::AudioProcessor
{
public:
    PhoneMonitorProcessor();
    ~PhoneMonitorProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PhoneMonitor"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    StreamServer&  getServer() noexcept { return server; }
    TunnelManager& getTunnel() noexcept { return tunnel; }

    std::atomic<bool> sendEnabled { true };

    // 外部公開
    void startRemote();
    void stopRemote();
    bool isRemoteActive() const noexcept { return remoteActive.load(); }
    juce::String getRemoteShareUrl() const;   // 公開URL + トークン（準備中は空）

    juce::String getAllowedMails() const;
    void setAllowedMails (const juce::String& s);

    juce::String getPreferredAddress() const;
    void setPreferredAddress (const juce::String& s);

private:
    static juce::String makeToken();

    AudioTap tap;
    StreamServer server { tap };
    TunnelManager tunnel;

    std::atomic<bool> remoteActive { false };
    mutable juce::CriticalSection stateLock;
    juce::String token, allowedMails, preferredAddress;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhoneMonitorProcessor)
};
