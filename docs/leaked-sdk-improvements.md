# Leaked SDK → emulator improvements (compatibility & performance)

Sources: `ref/ctr/sources/libraries/` (official shared libraries) and `ref/ctr/include/nn/` (client headers). These are the **actual implementations** Nintendo shipped — where the emulator's code is a reverse-engineered approximation, the SDK gives the exact algorithm to port or validate against. Companion docs: `leaked-sdk-findings.md` (save formats), `missing-functions-inventory.md` (service gaps), `firm-booting.md`, `official-networking.md`, `gamecard-emulation.md`.

## 1. GPU (PICA200) — the `gd` + `gr` + `gx` libraries

The emulator's `src/video_core/pica/` and `src/video_core/shader/` were reverse-engineered. The SDK contains the official GPU libraries:

- `sources/libraries/gd/CTR/` — `gd_System.cpp` (init/resource/state), `gd_Shader.cpp`, `gd_Lighting.cpp`, `gd_Texture.cpp`, `gd_Rasterizer.cpp`, `gd_OutputStage.cpp`, `gd_Combiner.cpp`, `gd_GasFog.cpp`, `gd_ProceduralTexture.cpp`, `gd_VertexInput.cpp`.
- `sources/libraries/gr/CTR/` — the higher-level "graphics" layer (`gr_Shader.cpp`, `gr_Combiner.cpp`, `gr_FragmentLight.cpp`) **including the official shader-binary (DVLP/DVLE) parser**: `gr_Shader.cpp:63` asserts the `0x454c5644` DVLP signature and `*binary == 0x504C5644` magic.
- `include/nn/gx/CTR/gx_MacroShader.h` + `gx_MacroReg.h` — the exact PICA register addresses and **macro command sequences** for every GPU operation.

### Concrete compatibility items

| Item | SDK source | Emulator counterpart | Improvement |
|---|---|---|---|
| Float→PICA conversions | `gd/CTR/gd_ProceduralTexture.cpp:37-41` (`Float32ToFix16` / `Float32ToFloat16` packing of proctex noise/phase), `gd_Lighting.cpp:466` (light pos f16 packing) | `rasterizer_accelerated.cpp` proctex/fog/light uniform sync | Verify the emulator's f16/fix16 packing against the official helpers (in `gd/common/`); fix any mismatch — this affects proctex noise, light positions, fog LUTs. |
| Proctex LUT upload format | `gd_ProceduralTexture.cpp:48-75` (`ConvertLookUpTableDataFloatToNative`, `__gdUploadLUT` → `PICA_REG_PROCTEX_LUT`/`PROCTEX_LUT_DATA0`) | `pica/pica_core.cpp` proctex LUT sync, `sw_proctex.cpp` | Cross-check the emulator's float→LUT-format conversion (value+delta packing) against the official one — this is a known-hard area with exact reference math. |
| Fog/gas settings & defaults | `gd_GasFog.cpp` (`SetFogColor`, `SetGasLightXY/Z`, `m_DirtyFieldMask`) | `pica/regs_texturing.h` fog_mode/gas, `sw_rasterizer.cpp:800` (`WriteFog` — comment: "Not fully accurate") | Port exact gas-lighting register semantics; validate fog LUT interpolation against the official dirty-field logic. |
| Lighting defaults/limits | `gd_Lighting.cpp` (`COUNT_LIGHTS`, default `m_FragmentLightingEnabled`, `MiscReg1c3 = 0x80000400`) | `pica/regs_lighting.h`, shader generators | Confirm default register values and light count (8); the `MiscReg1c3` default is an official quirk to reproduce. |
| Vertex attribute layout | `gd_VertexInput.cpp` (`GetShaderTypeSize`, attr mapping regs 0x201-0x228, IB format/offset commands) | `pica/vertex_loader.cpp`, `regs_pipeline.h` | Validate attribute-format sizes and the index-buffer offset formula (`addrOffset = base + startIndex × (1|2)`). |
| Default rasterizer state | `gd_Rasterizer.cpp:54-61` (viewport 240×320, `CULLING_CLOCKWISE` default, scissor off) | `pica/regs_rasterizer.h` | Confirm the emulator's reset state matches (culling default is a common compat bug source). |
| Shader binary parsing | `gr/CTR/gr_Shader.cpp` (DVLP `0x504C5644` / `0x454c5644`) | `video_core/pica/shader_setup.cpp` | The official parser is ground truth for the DVLP/DVLE/DVPE layout the emulator's shader loader re-implements — validate offsets, especially for homebrew. |

### Concrete performance items

- **Uniform/LUT upload size**: the emulator uploads proctex/fog/lighting LUTs as host textures/uniform blocks on every dirty flag (`gl_rasterizer.cpp:950`, `vk_rasterizer.cpp:915`). The SDK's `m_DirtyFieldMask` pattern (`gd_GasFog.cpp`) is the official minimal-dirty tracking — porting per-field dirty granularity (already partly present) to the renderers avoids full-LUT re-uploads.
- **Vertex cache / IB handling**: `gd_VertexInput.cpp` computes exact IB offsets; the emulator can reuse this to avoid re-uploading index buffers when only the offset changes.

## 2. Audio — official DSP-ADPCM decoder

- `sources/libraries/snd/CTR/snd_AdpcmDecoder.cpp` — `DecodeAdpcmData` (the exact DSP-ADPCM nibble→sample algorithm), `snd_Adpcm.cpp` (`ConvertAdpcmPos2Nib`/`ConvertAdpcmNib2Pos`), `snd_Bcwav.cpp` (BCWAV parsing incl. `DspAdpcmInfo`, `TYPE_ID_DSP_ADPCM = 0x0300`), and `snd_Voice.cpp` (channel state, `SetAdpcmParam`).
- Emulator counterpart: `src/audio_core/codec.cpp` (`DecodeADPCM`, GC-ADPCM with scale + 16 coefficients) and `src/audio_core/hle/source.cpp`.

**Improvement**: the emulator's decoder is already functionally correct, but the SDK is the reference for the **ADPCM context/state transition** (predictor+scale state, first-sample handling, block nibble order `NN_SND_ADPCM_DOL_*` constants). Porting the exact block arithmetic removes the risk of off-by-one drift on the first frame of each wave buffer — a classic audio crackle/pitch bug source. BCWAV constants also let the emulator's `BCWAV`/`BCSTM` parsing (`src/audio_core` and file_sys containers) be validated byte-for-byte.

## 3. Video — Y2R (color conversion) reference

- `sources/libraries/y2r/CTR/y2r_Y2r.cpp`, `y2r_Api.cpp`, `include/nn/y2r/CTR/y2r_Types.h` — the official YUV→RGB conversion with per-channel coefficients and input format (YUV422/420, indiv8/16).
- Emulator counterpart: `src/core/hw/y2r.cpp` (`ConvertYUVToRGB` template over input formats, `CoefficientSet`).

**Improvement**: validate the emulator's coefficient application and the 8x8-tile output layout against `y2r_Y2r.cpp` — this is the path used by camera photos and video playback; a coefficient/rounding mismatch shows up as washed-out or green-tinted screenshots in titles using the camera.

## 4. Crypto — official AES/SHA/HMAC/Cipher implementations

- `sources/libraries/crypto/` — `crypto_Aes.cpp`, `crypto_AesCbcContextBase.cpp`, `crypto_AesCtrContextBase.cpp`, `crypto_AesCmac.cpp`, `Hash/` (`crypto_Sha1.cpp`, `crypto_Sha256.cpp`, `crypto_Hmac.cpp`, `crypto_Md5.cpp`), `crypto_CcmDecryptor.cpp`, `crypto_CtrDecryptor.cpp`, plus the hardware-backed `crypto_BlockCipher.cpp` and `Aes/` (software AES contexts: `crypto_SwAesCbcContext.cpp`, `crypto_SwAesCtrContext.cpp`, `crypto_SwAesCmac.cpp`).
- Emulator counterparts: `src/core/hw/aes/` (AES hardware emulation), `src/core/hw/rsa/`, `src/core/hw/ecc.cpp`, plus `src/common/crypto` helpers.

**Improvement**: the emulator's software AES/SHA paths were written from the hardware spec; the SDK gives the exact CBC/CTR/CMAC/CCM and HMAC semantics used by titles. Use it to (a) verify the emulator's block-cipher chaining for `ncch` decryption and CIA installs (correctness = more games load), and (b) as a reference if optimizing the software paths (e.g. replacing the generic AES with the SDK's `Aes/` software contexts).

## 5. MVD (video decoding) — potential big win, big effort

- `sources/processes/mvd/` and the `mvd` library implement the hardware video decoder (H.264 etc.) the `mvd:std`/`mvd:exi` services expose. The emulator's `mvd` service is mostly present but the decode path is stubbed/simplified.
- **Compatibility**: real MVD behavior (buffer ping-pong, `ProcessFrame` semantics, `mvd_Ipc.hid` structures) would let video-heavy titles (some FMV sequences) work without host codecs.
- **Performance**: MVD decoding on real hardware is a dedicated DSP; emulating it in software is expensive. The realistic improvement is **not** to port the decoder, but to use the `mvd` service's exact frame-size/format negotiation (`PpInImage`/`PpOutImage` in `mvd_Ipc.hid`) to route to the host's hardware decoder cleanly — better than the current approximation.

## 6. Camera — calibration data for realistic output

- `sources/libraries/camera/CTR/camera_Camera.hid` — `ImageQualityCalibrationData` (the per-console calibration the camera module reads), `camera_Api.cpp:1097` (the P = √(w²+h²) calibration math).
- Emulator counterpart: `src/core/hle/service/cam/` (23 stubs, mostly no-op).

**Improvement**: the emulator can synthesize a plausible `ImageQualityCalibrationData` instead of returning empty/stub data — titles that read calibration (for distortion/quality correction) currently get garbage. The struct layout in `camera_Camera.hid` is the exact spec.

## 7. Other small, high-value validations

| Area | SDK source | Emulator | Note |
|---|---|---|---|
| NCCH/CXI/CCI detection | `sources/libraries/rfmt/CTR/rfmt_ProgramInfo.cpp` (`Is08Cci`/`Is08Cxi`, NCCH offset helpers) | `core/file_sys/ncch_container.cpp` | Validate the emulator's "is this a CCI/CXI" sniffing for drag-and-drop loading. |
| DSP channel constants | `sources/libraries/snd/CTR/snd_VoiceImpl.cpp` (`NN_SND_CYCLE_CH_ALIGN_*`) | `audio_core/hle/` | Cycle-alignment constants affect emulated DSP timing; validate. |
| Mii data | `include/nn/mii/mii_StoreData.h` | `core/hle/service/mii/` | Validate store layout for Mii Maker/homebrew interop. |
| FS program registry | `sources/libraries/fs/CTR/MPCore/fs_UserFileSystem.cpp` (card save mount/format, `GetCardType`) | `core/hle/service/fs/` | See `gamecard-emulation.md` — the official FS card API is the direct reference. |
| System update / NIM progress structs | `sources/processes/nim/CTR/` (`TitleDownloadState`, progress structs — emulator has matching `SystemUpdateProgress` static_asserts) | `core/hle/service/nim/nim_u.cpp` | The emulator already mirrors these layouts; keep them in sync with the SDK for the fork's NUS downloader UI. |

## Priority ranking

1. **GPU validation** (gd/gr/gx) — float packing + proctex LUT math + defaults: high compat impact (rendering artifacts), low risk (reference math).
2. **ADPCM block arithmetic** (snd) — audio correctness, small change.
3. **Y2R coefficient check** — camera/photo path, small change.
4. **Crypto semantics** (crypto lib) — install/decrypt correctness, medium.
5. **Camera calibration data** — fills 23 stubs' data dependency, small.
6. **Shader-binary (DVLP) validation** — homebrew compatibility, medium.
7. **MVD routing** — big effort, only for video-heavy titles.
