#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

// cloudflared の Quick Tunnel（trycloudflare.com）を起動・監視する
class TunnelManager : private juce::Thread
{
public:
    enum class State { idle, notFound, downloading, starting, running, failed };

    TunnelManager();
    ~TunnelManager() override;

    void startTunnel (int localPort, const juce::StringArray& allowedMails);
    void stopTunnel();
    void downloadCloudflared();

    State        getState() const noexcept { return state.load(); }
    juce::String getPublicUrl() const;
    juce::String getMessage() const;

    static juce::File findExecutable();
    static juce::File defaultInstallLocation();
    static juce::String extractTunnelUrl (const juce::String& text);

private:
    enum class Job { none, tunnel, download };

    void run() override;
    void runTunnel();
    void runDownload();
    void setStatus (State s, const juce::String& msg, const juce::String& url = {});

    std::atomic<State> state { State::idle };
    Job job = Job::none;
    int port = 0;
    juce::StringArray mails;

    mutable juce::CriticalSection lock;
    juce::String publicUrl, message;
    std::unique_ptr<juce::ChildProcess> process;
};
