# NR motion-vector metadata integration

Adapted from [cmh1448's commit 6446cc8](https://github.com/cmh1448/OptiScaler_DLSSNR/commit/6446cc8dcc76e2c2b786384300e217f352b2c18b),
published in [v0.2.0-motion-vector-fixed](https://github.com/cmh1448/OptiScaler_DLSSNR/releases/tag/v0.2.0-motion-vector-fixed).

The integration forwards independent depth and motion-vector origins and valid sizes to NR.
`MVLowRes` chooses render-resolution vectors; otherwise the DLSS output extent is used,
including when NR runs on the smaller pre-SR colour input. Each region is bounded
against its own allocation. Empty regions skip NR. Missing dimensions fall back to that
resource's available region rather than borrowing depth dimensions for motion vectors.

The working-resolution conversion is applied independently on X and Y. Model precision, pre-SR
placement, per-pass histories, colour composition, RR placement and frame-generation ownership are
preserved. Each NR pass receives the same guide metadata.

The forwarder's evaluate export is `dlssnr_call_evaluate_v2`, which carries the additional
arguments. If the loaded `nvngx.dll_dlssnr.dll` does not export it, NR does not start and reports
that the forwarder needs updating.

Validation: headless `nr_guides_smoke.cpp` covering render/output-resolution vectors, padded
allocations, independent origins, clipping and invalid regions; padded-colour GPU copy tests on WARP.
No new model weights or shaders are introduced by this integration. Visual shimmering improvement
remains unverified.
