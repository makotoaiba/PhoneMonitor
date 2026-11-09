#include "TunnelManager.h"

namespace
{
#if JUCE_WINDOWS
const char* exeName = "cloudflared.exe";
#else
const char* exeName = "cloudflared";
#endif
const char* downloadUrl = "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-windows-amd64.exe";
}

TunnelManager::TunnelManager() : juce::Thread ("PhoneMonitor tunnel") {}

TunnelManager::~TunnelManager() { stopTunnel(); }

juce::File TunnelManager::defaultInstallLocation()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("PhoneMonitor").getChildFile (exeName);
}

juce::File TunnelManager::findExecutable()
{
    // 1) テスト・上書き用の環境変数
    const auto env = juce::SystemStats::getEnvironmentVariable ("PHONEMONITOR_CLOUDFLARED", {});
    if (env.isNotEmpty() && juce::File::isAbsolutePath (env) && juce::File (env).existsAsFile())
        return juce::File (env);

    // 2) このプラグインが取得した場所
    if (auto f = defaultInstallLocation(); f.existsAsFile())
        return f;

    // 3) winget などでのインストール先
   #if JUCE_WINDOWS
    for (auto base : { juce::File::globalApplicationsDirectoryX86, juce::File::globalApplicationsDirectory })
        if (auto f = juce::File::getSpecialLocation (base).getChildFile ("cloudflared").getChildFile (exeName); f.existsAsFile())
            return f;
   #endif

    // 4) PATH
   #if JUCE_WINDOWS
    const auto sep = ";";
   #else
    const auto sep = ":";
   #endif
    for (auto dir : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), sep, "\""))
        if (dir.isNotEmpty() && juce::File::isAbsolutePath (dir))
            if (auto f = juce::File (dir).getChildFile (exeName); f.existsAsFile())
                return f;

    return {};
}

juce::String TunnelManager::extractTunnelUrl (const juce::String& text)
{
    int pos = 0;
    while ((pos = text.indexOf (pos, "https://")) >= 0)
    {
        int end = pos + 8;
        while (end < text.length() && (juce::CharacterFunctions::isLetterOrDigit (text[end]) || text[end] == '-' || text[end] == '.'))
            ++end;
        const auto url = text.substring (pos, end);
        const auto host = url.substring (8);
        if (host.endsWith (".trycloudflare.com") && host != "api.trycloudflare.com")
            return url;
        pos = end;
    }
    return {};
}

void TunnelManager::setStatus (State s, const juce::String& msg, const juce::String& url)
{
    const juce::ScopedLock sl (lock);
    state = s;
    message = msg;
    publicUrl = url;
}

juce::String TunnelManager::getPublicUrl() const { const juce::ScopedLock sl (lock); return publicUrl; }
juce::String TunnelManager::getMessage() const   { const juce::ScopedLock sl (lock); return message; }

void TunnelManager::startTunnel (int localPort, const juce::StringArray& allowedMails)
{
    stopTunnel();
    port = localPort;
    mails = allowedMails;
    job = Job::tunnel;
    setStatus (State::starting, juce::String::fromUTF8 (u8"公開URLを準備中…"));
    startThread();
}

void TunnelManager::stopTunnel()
{
    signalThreadShouldExit();
    {
        const juce::ScopedLock sl (lock);
        if (process != nullptr)
            process->kill(); // 読み取り待ちを解除
    }
    stopThread (5000);
    {
        const juce::ScopedLock sl (lock);
        process.reset();
    }
    if (job == Job::tunnel)
        setStatus (State::idle, {});
    job = Job::none;
}

void TunnelManager::downloadCloudflared()
{
    if (isThreadRunning())
        return;
    job = Job::download;
    setStatus (State::downloading, juce::String::fromUTF8 (u8"cloudflaredをダウンロード中…（約30MB）"));
    startThread();
}

void TunnelManager::run()
{
    if (job == Job::download) runDownload();
    else if (job == Job::tunnel) runTunnel();
}

void TunnelManager::runDownload()
{
    const auto dest = defaultInstallLocation();
    dest.getParentDirectory().createDirectory();
    const auto tmp = dest.getSiblingFile ("cloudflared.download");

    auto stream = juce::URL (downloadUrl).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (20000)
            .withNumRedirectsToFollow (10));

    if (stream == nullptr)
        return setStatus (State::failed, juce::String::fromUTF8 (u8"ダウンロードできませんでした。ネット接続を確認してください。"));

    {
        tmp.deleteFile();
        juce::FileOutputStream out (tmp);
        if (! out.openedOk())
            return setStatus (State::failed, juce::String::fromUTF8 (u8"保存先に書き込めませんでした: ") + tmp.getFullPathName());

        juce::HeapBlock<char> buf (65536);
        while (! threadShouldExit())
        {
            const int n = stream->read (buf, 65536);
            if (n <= 0) break;
            out.write (buf, (size_t) n);
        }
    }

    juce::MemoryBlock head;
    tmp.loadFileAsData (head);
    const bool looksExe = head.getSize() > 1000000 && static_cast<const char*> (head.getData())[0] == 'M'
                                                   && static_cast<const char*> (head.getData())[1] == 'Z';
    if (threadShouldExit() || ! looksExe)
    {
        tmp.deleteFile();
        return setStatus (State::failed, juce::String::fromUTF8 (u8"ダウンロードが途中で失敗しました。もう一度試してください。"));
    }

    dest.deleteFile();
    if (! tmp.moveFileTo (dest))
        return setStatus (State::failed, juce::String::fromUTF8 (u8"cloudflaredを配置できませんでした: ") + dest.getFullPathName());

    setStatus (State::idle, juce::String::fromUTF8 (u8"cloudflaredを取得しました。「外部公開を開始」を押してください。"));
}

void TunnelManager::runTunnel()
{
    const auto exe = findExecutable();
    if (! exe.existsAsFile())
        return setStatus (State::notFound, juce::String::fromUTF8 (u8"cloudflaredが見つかりません。「cloudflaredを取得」を押してください。"));

    juce::StringArray args { exe.getFullPathName(), "tunnel", "--no-autoupdate",
                             "--url", "http://localhost:" + juce::String (port) };
    for (auto& m : mails)
        if (m.trim().isNotEmpty())
            args.addArray ({ "--allowed-mail", m.trim() });

    auto proc = std::make_unique<juce::ChildProcess>();
    if (! proc->start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return setStatus (State::failed, juce::String::fromUTF8 (u8"cloudflaredを起動できませんでした。"));

    juce::ChildProcess* p = proc.get();
    {
        const juce::ScopedLock sl (lock);
        process = std::move (proc);
    }
    if (threadShouldExit())
        p->kill();

    juce::String log;      // 直近の出力（エラー表示用）
    juce::String line;
    bool found = false;
    char c = 0;

    while (! threadShouldExit())
    {
        if (p->readProcessOutput (&c, 1) != 1)
            break; // 終了
        if (c == '\n')
        {
            log += line + "\n";
            if (log.length() > 4000)
                log = log.getLastCharacters (2000);
            if (! found)
            {
                const auto url = extractTunnelUrl (line);
                if (url.isNotEmpty())
                {
                    found = true;
                    setStatus (State::running, {}, url);
                }
            }
            line.clear();
        }
        else if (c != '\r')
            line += c;
    }

    if (! threadShouldExit())
    {
        auto lines = juce::StringArray::fromLines (log.trim());
        const auto tail = lines.isEmpty() ? juce::String() : lines[lines.size() - 1];
        setStatus (State::failed, juce::String::fromUTF8 (u8"公開が止まりました。") + (tail.isNotEmpty() ? "\n" + tail.substring (0, 160) : juce::String()));
    }
}
