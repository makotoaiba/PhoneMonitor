#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class QrView : public juce::Component
{
public:
    void setText (const juce::String& t);
    void paint (juce::Graphics& g) override;

private:
    juce::String text;
    std::vector<std::vector<bool>> modules;
};

class PhoneMonitorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PhoneMonitorEditor (PhoneMonitorProcessor&);
    ~PhoneMonitorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshAddresses();
    juce::String lanUrl() const;
    void styleLabel (juce::Label& l, float size, juce::Colour c, bool bold = false);

    PhoneMonitorProcessor& proc;

    struct LnF : public juce::LookAndFeel_V4
    {
        LnF();
    } lnf;

    juce::ToggleButton sendToggle;

    juce::Label lanTitle, lanUrlLabel, lanInfo;
    QrView lanQr;
    juce::TextButton copyLanBtn, switchAddrBtn;

    juce::Label remoteTitle, mailLabel, remoteStatus, remoteUrlLabel, remoteInfo;
    juce::TextEditor mailEditor;
    QrView remoteQr;
    juce::TextButton remoteBtn, copyRemoteBtn, downloadBtn;

    juce::StringArray addresses;
    int addressIndex = 0;

    juce::Rectangle<int> lanPanel, remotePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhoneMonitorEditor)
};
