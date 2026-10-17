#include "LiveSpectrum.h"

#include <algorithm>
#include <cmath>

namespace pf
{

void LiveSpectrum::prepare (double fs)
{
    // Same frame length rule as the analyzer (4x the shift range).
    size = 4096;
    while (size < 4.0 * limits::kMaxLagMs * 0.001 * fs) size <<= 1;
    hop = size / 4;
    int order = 0;
    while ((1 << order) < size) ++order;
    fft = std::make_unique<dsp::Fft> (order);
    window = dsp::hann (size);
    a.resize ((size_t) size);
    b.resize ((size_t) size);
    fa.resize ((size_t) size / 2 + 1);
    fb.resize ((size_t) size / 2 + 1);
    spec.reset (size, fs);
    nextEnd = -1;
}

void LiveSpectrum::clear()
{
    spec.reset (size, spec.sampleRate());
    nextEnd = -1;
    ++frames;
}

void LiveSpectrum::update (const Capture& cap)
{
    if (fft == nullptr || std::abs (cap.sampleRate() - spec.sampleRate()) > 0.5)
        prepare (cap.sampleRate());

    const int64_t end = cap.written();
    if (nextEnd < 0 || end < nextEnd - hop)   // first call, or the capture was reset
        nextEnd = std::max<int64_t> (size, end - (int64_t) (3.0 * cap.sampleRate()));
    // Far behind (editor was closed): only the last few seconds matter.
    nextEnd = std::max (nextEnd, end - (int64_t) (4.0 * cap.sampleRate()));

    // ~3 s memory at the hop rate.
    const double decay = std::exp (-(double) hop / (3.0 * cap.sampleRate()));
    // At most a few frames per call, so catching up never stalls the UI.
    for (int budget = 6; nextEnd <= end && budget > 0; nextEnd += hop, --budget)
    {
        if (! cap.copy (nextEnd - size, size, a.data(), b.data())) continue;
        double ea = 0.0, eb = 0.0;
        for (int i = 0; i < size; ++i)
        {
            ea += (double) a[(size_t) i] * a[(size_t) i];
            eb += (double) b[(size_t) i] * b[(size_t) i];
        }
        ea /= size;
        eb /= size;
        if (ea < 1e-6 || eb < 1e-6) continue;

        fft->forwardRealPair (a.data(), b.data(), size, window.data(), fa.data(), fb.data(), scratch);
        spec.scale (decay);
        spec.add (fa.data(), fb.data(), 1.0 / std::sqrt (std::sqrt (ea * eb)));
        ++frames;
    }
}

} // namespace pf
