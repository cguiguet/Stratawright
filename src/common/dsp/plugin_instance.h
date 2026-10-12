#pragma once

#include <atomic>

namespace DSP {

// A plugin slot's instance pointer is read by the audio thread once per block and swapped by other
// threads. Both sides go through these so that, once a writer has swapped the old instance out and
// called IDSPKernel::waitForBlocksInFlight(), no block can still be using it and it can be freed.
inline void* loadPluginInstance(void*& slot) {
    return std::atomic_ref<void*>(slot).load(std::memory_order_seq_cst);
}

inline void* exchangePluginInstance(void*& slot, void* instance) {
    return std::atomic_ref<void*>(slot).exchange(instance, std::memory_order_seq_cst);
}

} // namespace DSP
