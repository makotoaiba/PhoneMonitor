#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <deque>
#include <memory>
#include "AudioTap.h"

// プラグイン内蔵のHTTP + WebSocketサーバー
//  GET /    : 再生ページ
//  GET /ws  : 音声ストリーム（WebSocket, 16bitステレオPCM）
// Cloudflareトンネル経由のアクセス（cf-connecting-ipヘッダー付き）は ?k=トークン が一致した時だけ許可する。
class StreamServer
{
public:
    explicit StreamServer (AudioTap& tap);
    ~StreamServer();

    bool start (int firstPort = 8765, int numTries = 30);
    void stop();

    int  getPort() const noexcept           { return port.load(); }
    void setSampleRate (double sr) noexcept { sampleRate.store ((int) sr); }

    // 空文字でトンネル経由のアクセスを全面拒否（既存のトンネル経由の接続も切断）
    void setRemoteToken (const juce::String& token);

    int  getLanClientCount() const noexcept    { return lanClients.load(); }
    int  getRemoteClientCount() const noexcept { return remoteClients.load(); }
    bool hasListeners() const noexcept         { return lanClients.load() + remoteClients.load() > 0; }

    struct Connection;

private:
    class Listener;
    class Broadcaster;
    friend struct Connection;

    void addConnection (juce::StreamingSocket* s);
    void reapFinished();
    void broadcast (std::shared_ptr<const juce::MemoryBlock> frame, bool control);

    juce::String getRemoteToken() const;

    AudioTap& tap;
    std::atomic<int> port { 0 }, sampleRate { 48000 };
    std::atomic<int> lanClients { 0 }, remoteClients { 0 };

    std::unique_ptr<juce::StreamingSocket> listenSocket;
    std::unique_ptr<Listener> listener;
    std::unique_ptr<Broadcaster> broadcaster;

    juce::CriticalSection connLock;
    juce::OwnedArray<Connection> connections;

    mutable juce::CriticalSection tokenLock;
    juce::String remoteToken;
};
