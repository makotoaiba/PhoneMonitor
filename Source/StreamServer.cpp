#include "StreamServer.h"
#include "Sha1.h"
#include "WebPage.h"

namespace
{
juce::MemoryBlock makeWsFrame (uint8_t opcode, const void* payload, size_t len)
{
    juce::MemoryOutputStream out (len + 10);
    out.writeByte ((char) (0x80 | opcode));
    if (len < 126)
        out.writeByte ((char) len);
    else if (len < 65536)
    {
        out.writeByte (126);
        out.writeByte ((char) ((len >> 8) & 0xff));
        out.writeByte ((char) (len & 0xff));
    }
    else
    {
        out.writeByte (127);
        for (int i = 7; i >= 0; --i)
            out.writeByte ((char) (((uint64_t) len >> (i * 8)) & 0xff));
    }
    if (len > 0)
        out.write (payload, len);
    return out.getMemoryBlock();
}

juce::MemoryBlock makeHello (int sr)
{
    const auto s = juce::String ("{\"sr\":") + juce::String (sr) + ",\"ch\":2}";
    return makeWsFrame (0x1, s.toRawUTF8(), s.getNumBytesAsUTF8());
}

bool writeAll (juce::StreamingSocket& s, const void* data, size_t len)
{
    return s.write (data, (int) len) == (int) len;
}

bool writeString (juce::StreamingSocket& s, const juce::String& str)
{
    return writeAll (s, str.toRawUTF8(), str.getNumBytesAsUTF8());
}
} // namespace

//==============================================================================
struct StreamServer::Connection : public juce::Thread
{
    Connection (StreamServer& o, juce::StreamingSocket* s)
        : juce::Thread ("PhoneMonitor client"), owner (o), socket (s) {}

    ~Connection() override
    {
        socket->close();
        stopThread (2000);
    }

    void enqueue (std::shared_ptr<const juce::MemoryBlock> frame, bool control)
    {
        {
            const juce::ScopedLock sl (queueLock);
            queue.push_back ({ std::move (frame), control });

            // 約1秒分を超えたら古い音声から捨てて遅延が伸び続けないようにする
            int audioCount = 0;
            for (auto& q : queue)
                if (! q.control) ++audioCount;
            while (audioCount > 50)
            {
                for (auto it = queue.begin(); it != queue.end(); ++it)
                    if (! it->control) { queue.erase (it); break; }
                --audioCount;
            }
        }
        wake.signal();
    }

    void kick() { socket->close(); wake.signal(); }

    void run() override
    {
        handle();
        if (isWebSocket.load())
            (isRemote.load() ? owner.remoteClients : owner.lanClients)--;
        isWebSocket = false;
        socket->close();
        finished = true;
    }

    bool readRequest (juce::String& requestLine, juce::StringPairArray& headers)
    {
        juce::MemoryBlock buf;
        char tmp[1024];
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;

        for (;;)
        {
            if (threadShouldExit() || juce::Time::getMillisecondCounter() > deadline || buf.getSize() > 16384)
                return false;
            if (socket->waitUntilReady (true, 200) != 1)
                continue;
            const int n = socket->read (tmp, sizeof (tmp), false);
            if (n <= 0)
                return false;
            buf.append (tmp, (size_t) n);
            const auto text = buf.toString();
            const int end = text.indexOf ("\r\n\r\n");
            if (end >= 0)
            {
                auto lines = juce::StringArray::fromLines (text.substring (0, end));
                if (lines.isEmpty())
                    return false;
                requestLine = lines[0];
                for (int i = 1; i < lines.size(); ++i)
                {
                    const int colon = lines[i].indexOfChar (':');
                    if (colon > 0)
                        headers.set (lines[i].substring (0, colon).trim().toLowerCase(),
                                     lines[i].substring (colon + 1).trim());
                }
                return true;
            }
        }
    }

    static juce::String queryParam (const juce::String& query, const juce::String& name)
    {
        for (auto& part : juce::StringArray::fromTokens (query, "&", ""))
            if (part.upToFirstOccurrenceOf ("=", false, false) == name)
                return juce::URL::removeEscapeChars (part.fromFirstOccurrenceOf ("=", false, false));
        return {};
    }

    void sendSimple (int code, const juce::String& reason, const juce::String& body)
    {
        writeString (*socket, "HTTP/1.1 " + juce::String (code) + " " + reason + "\r\n"
                              "Content-Type: text/plain; charset=utf-8\r\n"
                              "Cache-Control: no-store\r\n"
                              "Content-Length: " + juce::String ((int) body.getNumBytesAsUTF8()) + "\r\n"
                              "Connection: close\r\n\r\n" + body);
    }

    void handle()
    {
        juce::String requestLine;
        juce::StringPairArray headers;
        if (! readRequest (requestLine, headers))
            return;

        const auto parts  = juce::StringArray::fromTokens (requestLine, " ", "");
        const auto target = parts[1];
        const auto path   = target.upToFirstOccurrenceOf ("?", false, false);
        const auto query  = target.fromFirstOccurrenceOf ("?", false, false);

        isRemote = headers.getAllKeys().contains ("cf-connecting-ip") || headers.getAllKeys().contains ("cf-ray");

        if (parts[0] != "GET")
            return sendSimple (405, "Method Not Allowed", "GET only");

        if (isRemote)
        {
            const auto token = owner.getRemoteToken();
            if (token.isEmpty() || queryParam (query, "k") != token)
                return sendSimple (403, "Forbidden", "このリンクは無効です。共有した人に新しいURLをもらってください。");
        }

        if (path == "/" || path == "/index.html")
        {
            const juce::String html (juce::CharPointer_UTF8 (pm::getWebPage().c_str()));
            writeString (*socket, "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/html; charset=utf-8\r\n"
                                  "Cache-Control: no-store\r\n"
                                  "Referrer-Policy: no-referrer\r\n"
                                  "Content-Length: " + juce::String ((int) html.getNumBytesAsUTF8()) + "\r\n"
                                  "Connection: close\r\n\r\n");
            writeString (*socket, html);
            return;
        }

        if (path == "/ws" && headers["upgrade"].equalsIgnoreCase ("websocket"))
            return runWebSocket (headers["sec-websocket-key"]);

        sendSimple (404, "Not Found", "not found");
    }

    void runWebSocket (const juce::String& key)
    {
        if (key.isEmpty())
            return sendSimple (400, "Bad Request", "missing key");

        const auto digest = pm::sha1 ((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toStdString());
        const auto accept = juce::Base64::toBase64 (digest.data(), digest.size());

        if (! writeString (*socket, "HTTP/1.1 101 Switching Protocols\r\n"
                                    "Upgrade: websocket\r\n"
                                    "Connection: Upgrade\r\n"
                                    "Sec-WebSocket-Accept: " + accept + "\r\n\r\n"))
            return;

        {
            auto hello = makeHello (owner.sampleRate.load());
            if (! writeAll (*socket, hello.getData(), hello.getSize()))
                return;
        }

        (isRemote.load() ? owner.remoteClients : owner.lanClients)++;
        isWebSocket = true;

        juce::MemoryBlock inbuf;
        auto lastPing = juce::Time::getMillisecondCounter();

        while (! threadShouldExit())
        {
            wake.wait (50);

            // 送信
            for (;;)
            {
                Item item;
                {
                    const juce::ScopedLock sl (queueLock);
                    if (queue.empty())
                        break;
                    item = std::move (queue.front());
                    queue.pop_front();
                }
                if (! writeAll (*socket, item.frame->getData(), item.frame->getSize()))
                    return;
            }

            // 無音で通信が止まってもトンネルに切られないよう定期的にping
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastPing > 20000)
            {
                lastPing = now;
                auto ping = makeWsFrame (0x9, nullptr, 0);
                if (! writeAll (*socket, ping.getData(), ping.getSize()))
                    return;
            }

            // 受信（close / ping のみ扱う）
            const int ready = socket->waitUntilReady (true, 0);
            if (ready < 0)
                return;
            if (ready == 1)
            {
                char tmp[4096];
                const int n = socket->read (tmp, sizeof (tmp), false);
                if (n <= 0)
                    return;
                inbuf.append (tmp, (size_t) n);
                if (inbuf.getSize() > 65536 || ! processIncoming (inbuf))
                    return;
            }
        }
    }

    // false を返したら接続終了
    bool processIncoming (juce::MemoryBlock& buf)
    {
        for (;;)
        {
            auto* d = static_cast<const uint8_t*> (buf.getData());
            const size_t size = buf.getSize();
            if (size < 2)
                return true;

            const int opcode = d[0] & 0x0f;
            const bool masked = (d[1] & 0x80) != 0;
            uint64_t len = d[1] & 0x7f;
            size_t pos = 2;
            if (len == 126)
            {
                if (size < 4) return true;
                len = ((uint64_t) d[2] << 8) | d[3];
                pos = 4;
            }
            else if (len == 127)
            {
                if (size < 10) return true;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | d[2 + i];
                pos = 10;
            }
            if (len > 60000)
                return false;
            uint8_t mask[4] = {};
            if (masked)
            {
                if (size < pos + 4) return true;
                std::memcpy (mask, d + pos, 4);
                pos += 4;
            }
            if (size < pos + len)
                return true;

            juce::MemoryBlock payload (d + pos, (size_t) len);
            auto* p = static_cast<uint8_t*> (payload.getData());
            for (size_t i = 0; i < len; ++i)
                p[i] ^= mask[i & 3];

            buf.removeSection (0, pos + (size_t) len);

            if (opcode == 0x8)
            {
                auto close = makeWsFrame (0x8, nullptr, 0);
                writeAll (*socket, close.getData(), close.getSize());
                return false;
            }
            if (opcode == 0x9)
            {
                auto pong = makeWsFrame (0xA, payload.getData(), payload.getSize());
                if (! writeAll (*socket, pong.getData(), pong.getSize()))
                    return false;
            }
        }
    }

    struct Item
    {
        std::shared_ptr<const juce::MemoryBlock> frame;
        bool control = false;
    };

    StreamServer& owner;
    std::unique_ptr<juce::StreamingSocket> socket;
    juce::CriticalSection queueLock;
    std::deque<Item> queue;
    juce::WaitableEvent wake;
    std::atomic<bool> isWebSocket { false }, finished { false };
    std::atomic<bool> isRemote { false };
};

//==============================================================================
class StreamServer::Listener : public juce::Thread
{
public:
    explicit Listener (StreamServer& o) : juce::Thread ("PhoneMonitor listener"), owner (o) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            auto* s = owner.listenSocket->waitForNextConnection();
            if (s == nullptr)
            {
                if (threadShouldExit() || ! owner.listenSocket->isConnected())
                    break;
                continue;
            }
            owner.addConnection (s);
        }
    }

private:
    StreamServer& owner;
};

//==============================================================================
class StreamServer::Broadcaster : public juce::Thread
{
public:
    explicit Broadcaster (StreamServer& o) : juce::Thread ("PhoneMonitor sender"), owner (o) {}

    void run() override
    {
        std::vector<float> f;
        std::vector<int16_t> pcm;
        int lastSr = owner.sampleRate.load();

        while (! threadShouldExit())
        {
            wait (10);
            owner.reapFinished();

            const int sr = owner.sampleRate.load();
            if (sr != lastSr)
            {
                lastSr = sr;
                owner.tap.discardAll();
                owner.broadcast (std::make_shared<juce::MemoryBlock> (makeHello (sr)), true);
            }

            if (! owner.hasListeners())
            {
                owner.tap.discardAll();
                continue;
            }

            const int chunk = juce::jmax (64, sr / 50); // 20ms
            f.resize ((size_t) chunk * 2);
            pcm.resize ((size_t) chunk * 2);

            while (owner.tap.available() >= chunk && ! threadShouldExit())
            {
                owner.tap.pull (f.data(), chunk);
                for (size_t i = 0; i < f.size(); ++i)
                {
                    const float v = juce::jlimit (-1.0f, 1.0f, f[i]);
                    pcm[i] = (int16_t) juce::roundToInt (v * 32767.0f);
                }
                owner.broadcast (std::make_shared<juce::MemoryBlock> (
                                     makeWsFrame (0x2, pcm.data(), pcm.size() * sizeof (int16_t))),
                                 false);
            }
        }
    }

private:
    StreamServer& owner;
};

//==============================================================================
StreamServer::StreamServer (AudioTap& t) : tap (t) {}

StreamServer::~StreamServer() { stop(); }

bool StreamServer::start (int firstPort, int numTries)
{
    stop();
    for (int p = firstPort; p < firstPort + numTries; ++p)
    {
        auto s = std::make_unique<juce::StreamingSocket>();
        if (s->createListener (p))
        {
            listenSocket = std::move (s);
            port = p;
            listener = std::make_unique<Listener> (*this);
            broadcaster = std::make_unique<Broadcaster> (*this);
            listener->startThread();
            broadcaster->startThread();
            return true;
        }
    }
    return false;
}

void StreamServer::stop()
{
    if (listener != nullptr)
    {
        listener->signalThreadShouldExit();
        listenSocket->close();
        listener->stopThread (3000);
        listener.reset();
    }
    if (broadcaster != nullptr)
    {
        broadcaster->stopThread (3000);
        broadcaster.reset();
    }
    {
        const juce::ScopedLock sl (connLock);
        connections.clear(); // 各Connectionのデストラクタでソケットを閉じてスレッド停止
    }
    listenSocket.reset();
    port = 0;
}

void StreamServer::setRemoteToken (const juce::String& token)
{
    {
        const juce::ScopedLock sl (tokenLock);
        remoteToken = token;
    }
    if (token.isEmpty())
    {
        const juce::ScopedLock sl (connLock);
        for (auto* c : connections)
            if (c->isRemote.load())
                c->kick();
    }
}

juce::String StreamServer::getRemoteToken() const
{
    const juce::ScopedLock sl (tokenLock);
    return remoteToken;
}

void StreamServer::addConnection (juce::StreamingSocket* s)
{
    const juce::ScopedLock sl (connLock);
    if (connections.size() >= 32)
    {
        delete s;
        return;
    }
    auto* c = connections.add (new Connection (*this, s));
    c->startThread();
}

void StreamServer::reapFinished()
{
    const juce::ScopedLock sl (connLock);
    for (int i = connections.size(); --i >= 0;)
        if (connections[i]->finished.load())
            connections.remove (i);
}

void StreamServer::broadcast (std::shared_ptr<const juce::MemoryBlock> frame, bool control)
{
    const juce::ScopedLock sl (connLock);
    for (auto* c : connections)
        if (c->isWebSocket.load())
            c->enqueue (frame, control);
}
