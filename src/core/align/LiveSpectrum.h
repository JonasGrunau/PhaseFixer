#pragma once

#include "Capture.h"
#include "CrossSpectrum.h"
#include "dsp/Fft.h"

#include <memory>

namespace pf
{

// A running cross-spectrum of what is playing now (last ~3 s where both play),
// for the phase display. Silence freezes it instead of fading it out. Fed from
// a Capture on the message thread.
class LiveSpectrum
{
public:
    void prepare (double sampleRate);
    void update (const Capture& capture);   // processes whatever arrived since the last call
    void clear();

    const CrossSpectrum& spectrum() const noexcept { return spec; }
    bool hasData() const noexcept { return ! spec.empty(); }

private:
    CrossSpectrum spec;
    std::unique_ptr<dsp::Fft> fft;
    std::vector<float> window, a, b;
    std::vector<dsp::cplx> fa, fb, scratch;
    int size = 0, hop = 0;
    int64_t nextEnd = -1;
};

} // namespace pf
