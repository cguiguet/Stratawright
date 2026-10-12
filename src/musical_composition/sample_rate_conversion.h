#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace composition {

// Converts timeline positions, which are stored in samples, when the project's sample rate
// changes: the same musical time is (to / from) times as many samples. Positions round to the
// nearest sample; convert a length through its end (length(start, length)) so that items that
// touched before still touch after.
struct SampleRateConversion {
    uint32_t from = 0;
    uint32_t to = 0;

    bool changesAnything() const { return from != 0 && to != 0 && from != to; }

    uint64_t operator()(uint64_t samples) const {
        if (!changesAnything() || samples == std::numeric_limits<uint64_t>::max()) {
            return samples;
        }
        if (samples <= (std::numeric_limits<uint64_t>::max() - from / 2) / to) {
            return (samples * to + from / 2) / from;
        }
        return static_cast<uint64_t>(std::llround(static_cast<long double>(samples) * to / from));
    }

    uint64_t length(uint64_t start, uint64_t samples) const {
        return (*this)(start + samples) - (*this)(start);
    }

    uint32_t operator()(uint32_t samples) const {
        const uint64_t converted = (*this)(static_cast<uint64_t>(samples));
        return converted > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                                : static_cast<uint32_t>(converted);
    }
};

} // namespace composition
