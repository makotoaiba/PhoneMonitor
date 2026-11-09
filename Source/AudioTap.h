#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// オーディオスレッド → 送信スレッドへ音を渡すロックフリーFIFO（ステレオ・インターリーブ）
class AudioTap
{
public:
    static constexpr int capacityFrames = 192000 * 2; // 192kHzでも約2秒

    AudioTap() : fifo (capacityFrames) { data.resize ((size_t) capacityFrames * 2); }

    // オーディオスレッドから呼ぶ。空きが足りない分は捨てる（ブロックしない）
    void push (const float* l, const float* r, int numFrames) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (numFrames, s1, z1, s2, z2);
        write (l, r, 0, s1, z1);
        write (l, r, z1, s2, z2);
        fifo.finishedWrite (z1 + z2);
    }

    int available() const noexcept { return fifo.getNumReady(); }

    // 送信スレッドから呼ぶ。dest はフレーム数×2 の float
    int pull (float* dest, int maxFrames) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (maxFrames, s1, z1, s2, z2);
        std::memcpy (dest, data.data() + (size_t) s1 * 2, sizeof (float) * (size_t) z1 * 2);
        std::memcpy (dest + (size_t) z1 * 2, data.data() + (size_t) s2 * 2, sizeof (float) * (size_t) z2 * 2);
        fifo.finishedRead (z1 + z2);
        return z1 + z2;
    }

    void discardAll() noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2);
        fifo.finishedRead (z1 + z2);
    }

private:
    void write (const float* l, const float* r, int srcOffset, int start, int count) noexcept
    {
        float* d = data.data() + (size_t) start * 2;
        for (int i = 0; i < count; ++i)
        {
            d[i * 2]     = l[srcOffset + i];
            d[i * 2 + 1] = r[srcOffset + i];
        }
    }

    juce::AbstractFifo fifo;
    std::vector<float> data;
};
