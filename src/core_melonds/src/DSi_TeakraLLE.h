// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef DSI_TEAKRALLE_H
#define DSI_TEAKRALLE_H

#include <array>
#include <functional>
#include <teakra/teakra.h>
#include "DSi_DSP.h"

namespace melonDS {

// Adapts Azahar's Teakra (externals/teakra) to melonDS's DSPInterface, letting
// the DSi DSP run through the shared teakra library. Azahar's Teakra lives in
// its own namespace and cannot depend on melonDS, so it is wrapped here.
//
// Note: Azahar's teakra does not implement melonDS savestate support, so LLE
// savestates cannot capture the DSP core state (savestate is a best-effort no-op
// for the DSP in this mode).
class DSi_TeakraLLE final : public DSPInterface {
public:
    DSi_TeakraLLE() = default;
    ~DSi_TeakraLLE() override = default;

    void Reset() override {
        teakra.Reset();
    }
    void DoSavestate(Savestate*) override {
        // not supported: azahar's teakra has no savestate serialization
    }

    u32 GetID() override {
        return 0x7EAC0000; // Teakra::ID
    }

    // APBP Data
    bool SendDataIsEmpty(std::uint8_t index) const override {
        return teakra.SendDataIsEmpty(index);
    }
    void SendData(std::uint8_t index, std::uint16_t value) override {
        teakra.SendData(index, value);
    }
    bool RecvDataIsReady(std::uint8_t index) const override {
        return teakra.RecvDataIsReady(index);
    }
    std::uint16_t RecvData(std::uint8_t index) override {
        return teakra.RecvData(index);
    }

    // APBP Semaphore
    void SetSemaphore(std::uint16_t value) override {
        teakra.SetSemaphore(value);
    }
    void ClearSemaphore(std::uint16_t value) override {
        teakra.ClearSemaphore(value);
    }
    void MaskSemaphore(std::uint16_t value) override {
        teakra.MaskSemaphore(value);
    }
    std::uint16_t GetSemaphore() const override {
        return teakra.GetSemaphore();
    }

    // for implementing DSP_PDATA/PADR DMA transfers
    std::uint16_t ProgramRead(std::uint32_t address) const override {
        return teakra.ProgramRead(address);
    }
    void ProgramWrite(std::uint32_t address, std::uint16_t value) override {
        teakra.ProgramWrite(address, value);
    }
    std::uint16_t DataReadA32(std::uint32_t address) const override {
        return teakra.DataReadA32(address);
    }
    void DataWriteA32(std::uint32_t address, std::uint16_t value) override {
        teakra.DataWriteA32(address, value);
    }
    std::uint16_t MMIORead(std::uint16_t address) override {
        return teakra.MMIORead(address);
    }
    void MMIOWrite(std::uint16_t address, std::uint16_t value) override {
        teakra.MMIOWrite(address, value);
    }

    // DSP_PADR is only 16-bit, so this is where the DMA interface gets the
    // upper 16-bits from
    std::uint16_t DMAChan0GetSrcHigh() override {
        return teakra.DMAChan0GetSrcHigh();
    }
    std::uint16_t DMAChan0GetDstHigh() override {
        return teakra.DMAChan0GetDstHigh();
    }

    std::uint16_t AHBMGetUnitSize(std::uint16_t i) const override {
        return teakra.AHBMGetUnitSize(i);
    }
    std::uint16_t AHBMGetDirection(std::uint16_t i) const override {
        return teakra.AHBMGetDirection(i);
    }
    std::uint16_t AHBMGetDmaChannel(std::uint16_t i) const override {
        return teakra.AHBMGetDmaChannel(i);
    }
    std::uint16_t AHBMRead16(std::uint32_t addr) override {
        return teakra.AHBMRead16(addr);
    }
    void AHBMWrite16(std::uint32_t addr, std::uint16_t value) override {
        teakra.AHBMWrite16(addr, value);
    }
    std::uint16_t AHBMRead32(std::uint32_t addr) override {
        return teakra.AHBMRead32(addr);
    }
    void AHBMWrite32(std::uint32_t addr, std::uint32_t value) override {
        teakra.AHBMWrite32(addr, value);
    }

    // core
    void SampleClock(s16 output[2], s16 input) override {
        teakra.SampleClock(output, input);
    }
    void Run(unsigned cycle) override {
        teakra.Run(cycle);
    }

    // teakra-specific setup (called from DSi_DSP::StartDSPLLE)
    void SetRecvDataHandler(std::uint8_t index, std::function<void()> handler) {
        teakra.SetRecvDataHandler(index, std::move(handler));
    }
    void SetSemaphoreHandler(std::function<void()> handler) {
        teakra.SetSemaphoreHandler(std::move(handler));
    }
    void SetSharedMemoryCallback(std::function<std::uint16_t(std::uint32_t)> read16,
                                 std::function<void(std::uint32_t, std::uint16_t)> write16) {
        teakra.SetSharedMemoryCallback(
            Teakra::Teakra::SharedMemoryCallback{std::move(read16), std::move(write16)});
    }
    void SetAHBMCallback(const Teakra::AHBMCallback& cb) {
        teakra.SetAHBMCallback(cb);
    }
    void SetMicEnableCallback(std::function<void(bool)> cb) {
        teakra.SetMicEnableCallback(std::move(cb));
    }
    void SetAudioCallback(std::function<void(std::array<s16, 2>)> cb) {
        teakra.SetAudioCallback(std::move(cb));
    }

private:
    Teakra::Teakra teakra;
};

} // namespace melonDS

#endif // DSI_TEAKRALLE_H