#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <random>

PhoneMonitorProcessor::PhoneMonitorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    server.start();
}

PhoneMonitorProcessor::~PhoneMonitorProcessor()
{
    tunnel.stopTunnel();
    server.stop();
}

bool PhoneMonitorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PhoneMonitorProcessor::prepareToPlay (double sampleRate, int)
{
    server.setSampleRate (sampleRate);
}

void PhoneMonitorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // 音はそのまま素通し。聴いている人がいる時だけ送信用FIFOへコピー
    if (! sendEnabled.load() || ! server.hasListeners() || buffer.getNumChannels() == 0)
        return;

    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    tap.push (l, r, buffer.getNumSamples());
}

juce::String PhoneMonitorProcessor::makeToken()
{
    static const char* chars = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    juce::String t;
    for (int i = 0; i < 22; ++i)
        t += chars[rd() % 56];
    return t;
}

void PhoneMonitorProcessor::startRemote()
{
    // 開始のたびにトークンを作り直す（以前のURLは無効になる）
    const auto t = makeToken();
    {
        const juce::ScopedLock sl (stateLock);
        token = t;
    }
    server.setRemoteToken (t);
    remoteActive = true;
    tunnel.startTunnel (server.getPort(), juce::StringArray::fromTokens (getAllowedMails(), ",; ", ""));
}

void PhoneMonitorProcessor::stopRemote()
{
    remoteActive = false;
    tunnel.stopTunnel();
    server.setRemoteToken ({});
}

juce::String PhoneMonitorProcessor::getRemoteShareUrl() const
{
    if (! remoteActive.load() || tunnel.getState() != TunnelManager::State::running)
        return {};
    const juce::ScopedLock sl (stateLock);
    return tunnel.getPublicUrl() + "/?k=" + token;
}

juce::String PhoneMonitorProcessor::getAllowedMails() const     { const juce::ScopedLock sl (stateLock); return allowedMails; }
void PhoneMonitorProcessor::setAllowedMails (const juce::String& s) { const juce::ScopedLock sl (stateLock); allowedMails = s; }
juce::String PhoneMonitorProcessor::getPreferredAddress() const { const juce::ScopedLock sl (stateLock); return preferredAddress; }
void PhoneMonitorProcessor::setPreferredAddress (const juce::String& s) { const juce::ScopedLock sl (stateLock); preferredAddress = s; }

void PhoneMonitorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("PhoneMonitor");
    xml.setAttribute ("send", sendEnabled.load());
    xml.setAttribute ("mails", getAllowedMails());
    xml.setAttribute ("address", getPreferredAddress());
    copyXmlToBinary (xml, destData);
}

void PhoneMonitorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName ("PhoneMonitor"))
    {
        sendEnabled = xml->getBoolAttribute ("send", true);
        setAllowedMails (xml->getStringAttribute ("mails"));
        setPreferredAddress (xml->getStringAttribute ("address"));
    }
}

juce::AudioProcessorEditor* PhoneMonitorProcessor::createEditor() { return new PhoneMonitorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PhoneMonitorProcessor(); }
