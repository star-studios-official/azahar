# Vulkan third-party license inventory

This inventory covers the dependencies introduced by the Vulkan renderer.
Packagers must ship the license text supplied with the exact dependency version
they distribute; package-manager metadata is not a substitute in a binary
archive.

| Component | Use in melonDS | License material |
| --- | --- | --- |
| MoltenVK | Vulkan implementation bundled with macOS apps | Apache-2.0; `MoltenVK-LICENSE.txt` and `MoltenVK-NOTICE.txt` |
| glslang (including its `libSPIRV` code generator) | Runtime GLSL-to-SPIR-V compilation | Upstream composite `LICENSE.txt` (BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0, GPL-3.0 with the Bison exception, and component-specific terms; applicability varies by version/build) |
| SPIRV-Tools | Linked by some glslang packages | Apache-2.0 `LICENSE` from the SPIRV-Tools package |
| SPIRV-Headers | Build-time headers used by glslang/SPIRV-Tools | MIT `LICENSE` from the SPIRV-Headers package; no separate library is bundled by melonDS |
| Vulkan-Headers | Build-time Vulkan declarations | Upstream `LICENSE.md`; no separate library is bundled by melonDS |

`tools/mac-libs.rb` detects the Vulkan libraries actually copied into a macOS
app and adds the matching MoltenVK, glslang, and SPIRV-Tools license files to
`Contents/Resources/ThirdPartyLicenses`. Other platform packaging must provide
the corresponding dependency licenses through its normal packaging system.
