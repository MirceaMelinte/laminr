# Skin controls and game-state safety

## Automatic mask versus skin colour controls

`AutoMask` is NVIDIA's internal automatic-mask input. It is sent during feature creation and
evaluation, and changing an active pass's setting rebuilds that feature. `SkinStructure=-1`
follows `LocalStructure`, so the default does not request separate skin detail strength. To compare,
try `AutoMask=true`, `LocalStructure=1`, `SkinStructure=0` on a still character close-up.
`SkinStructure=0` is not a switch that removes all skin lighting/colour changes.

The forwarder explicitly clears `DLSSNR.ControlMask` before setting AutoMask. An explicit mask
overrides the automatic mask; a stale entry in the reusable parameter block must not win silently.
The ControlMask precedence is also described in [these independent feature-18 probes](https://github.com/kibblerz/DLSS5-Reshade-AIO/blob/main/lab/PRIVATE-CONTRACT-FINDINGS.md#controlmask).
Logs distinguish parameter-table readback from proof of a visible effect and identify the final
model pass whose values remain in that table.

The **Skin and environment (final edit)** section is independent of AutoMask. It has:

- An opt-in **Separate skin / environment controls** switch.
- **Allow skin tone / colour changes**, a separate on/off switch.
- Skin and environment sliders for detail/lighting and colour (0..1).
- **Preview colour-based mask**, which shows the colour-based selection in white.

This is an approximate colour-based selection, not a semantic skin/face detector. Wood, sand and
other warm materials can match; unusual or strongly coloured lighting can prevent skin from matching.
Check the preview before using it. The filter is off by default and all four strengths default to 1,
so existing setups keep their output. It applies once to the combined NR result, including replace
mode. It does not add another neural model pass. Per-pass model sliders remain separate; the final
filter does not expose the model's private semantic mask.

Colour off preserves hue/chroma in fully selected pixels but allows the lighting slider to change
brightness. Set skin detail/lighting to 0 as well to restore original pixels where the mask is fully
white. At soft edges the skin and environment settings blend.

## Game state around the pass

NR's graphics-state envelope covers model creation, recreation and evaluation. When
`RestoreComputeSignature` or `RestoreGraphicSignature` is on and the game's root signature cannot be
restored on that command list, the frame is skipped before any NR command is recorded. The
forwarder returns no handle when model creation reports an error, so a partially created feature is
never used.

## Validation

`tests/nr_skin_shader_smoke.cpp` executes the shared HLSL headlessly using D3D11 WARP:
disabled/default identity, full bypass, skin/environment separation, mask preview and colour preservation.
This is shader validation, not an in-game test of NVIDIA's automatic mask.

Build/run from a VS x64 Native Tools prompt in the `optiscaler` folder:

```bat
cl /nologo /std:c++20 /EHsc tests\nr_skin_shader_smoke.cpp /Fe:x64\nr_skin_shader_smoke.exe /Fo:x64\nr_skin_shader_smoke.obj /link d3d11.lib d3dcompiler.lib
x64\nr_skin_shader_smoke.exe OptiScaler\shaders\dlssnr\precompile\dlssnr.hlsl
```
