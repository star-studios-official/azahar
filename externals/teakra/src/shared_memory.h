#pragma once
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include "common_types.h"

namespace Teakra {
struct SharedMemory {
    // We allocate our own memory if the user doesn't supply their own
    std::unique_ptr<std::array<u8, 0x80000>> own_memory;
    // Points to either own own memory or user-supplied memory
    u8* raw;

    // Optional external access callbacks (byte addressed). These are used by
    // emulators that need to route the DSP's shared memory through their own
    // memory system (e.g. melonDS's DSi NWRAM), rather than a flat buffer. When
    // set, ReadWord/WriteWord route all accesses through them.
    std::function<u16(u32)> read_external16;
    std::function<void(u32, u16)> write_external16;

    SharedMemory(u8* mem = nullptr) : raw{mem} {
        if (mem == nullptr) {
            own_memory = std::make_unique<std::array<u8, 0x80000>>();
            raw = own_memory->data();
        }
    }

    void SetExternalMemoryCallback(
        std::function<u16(u32)> read16, std::function<void(u32, u16)> write16) {

        read_external16 = std::move(read16);
        write_external16 = std::move(write16);
    }

    u16 ReadWord(u32 word_address) const {
        u32 byte_address = word_address * 2;
        if (read_external16) {
            return read_external16(byte_address);
        }
        u8 low = raw[byte_address];
        u8 high = raw[byte_address + 1];
        return low | ((u16)high << 8);
    }
    void WriteWord(u32 word_address, u16 value) {
        if (write_external16) {
            write_external16(word_address * 2, value);
            return;
        }
        u8 low = value & 0xFF;
        u8 high = value >> 8;
        u32 byte_address = word_address * 2;
        raw[byte_address] = low;
        raw[byte_address + 1] = high;
    }
};
} // namespace Teakra
