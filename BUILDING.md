# Building laminr

## Tools

- Windows x64.
- Visual Studio 2022 / Build Tools, Desktop development with C++, MSVC v143 and Windows SDK. The included scripts locate the installation with vswhere.
- Go 1.23 or newer on PATH (release installer built with Go 1.27.1).
- Python 3 with `jinja2` on PATH for ReShade's GLAD generator (`python -m pip install jinja2`). These are upstream build dependencies.
- Windows PowerShell 5.1 and .NET Framework 4.x.

The engine dependencies are vendored under optiscaler/external and reshade/deps, with their notices. Do not replace these with arbitrary newer versions when reproducing this release. Their upstream .gitmodules files document provenance; this repository contains the files directly, without their tests, samples, documentation and unused prebuilt files. Restore any of those from the pinned upstream revisions if needed.

## Build

From the repository root:

```powershell
powershell -NoProfile -File .\build-candidate.ps1
```

Use a PowerShell environment permitted to run your locally reviewed build scripts. Network access is needed for the pinned Go resource generator on its first run, and for the two link libraries below on the first engine build. Check each command succeeds before continuing.

The candidate script compiles the engine DLLs, installer host and version resources, then records their hashes against the source used to build them. Packaging requires that matching record, verifies embedded files, and produces one installer EXE in `dist`. Use `-SkipPackage` to compile and verify only the core components. Packaging does not publish or deploy anything.

`detours.lib` and `freetype.lib` are not committed. `fetch-link-libraries.ps1` downloads them from upstream OptiScaler at the commit pinned in `optiscaler-revision.txt` (`e237f895623742b761f9e5f00067cb3dc62619f4`) and checks them against the SHA-256 hashes in `link-libraries.json`. `build-candidate.ps1`, the OptiScaler project and `build-snapshot-keyboard-test.cmd` run it; a file already in place is only verified. Without network access, download the files by hand and save them at the paths listed in `link-libraries.json`. Do not substitute newer libraries.

One input that upstream checkouts don't include is committed here, although the nested `.gitignore` files match it:

- `reshade/res/version.h` was written by hand as `6.8.0.0`, because the pinned ReShade revision is 18 commits after the `v6.8.0` tag and this repository has no ReShade tags for `tools/update_version.ps1` to read. Release builds increment its last number.

Other vendored files matched by those nested ignore rules are also committed. Stage new ones with `git add -f`.

Generated executables, objects, payload staging and dist are excluded from Git. Go tests run during packaging, after payload generation. An engine build is required before packaging a clean checkout. The release source was prepared from the tested workspace; a complete clean-room engine rebuild from this publication folder has not yet been performed.

## Runtime files

Standard and SweetFX packs are bundled from installer/DefaultEffects. When you install with your own INI, the FX it needs are downloaded using installer/EffectPackages.ini. The NVIDIA DLSS/Streamline files and the RTX 50 neural-rendering model are downloaded and hash-verified using the included manifests. The manager installs only on NVIDIA RTX 50-series GPUs and Microsoft Flight Simulator 2024.

The in-game ZIP import still uses `laminrData/Tools` (import-effects.ps1, EffectPackages.ini, store-paths.ps1), so those stay in the payload.

## Updates

The manager has no self-update. To update, run a newer manager EXE; Setup offers **Update laminr** when its bundled version is newer than the installed one.

## Hybrid runtime assets

Not packaged. The experimental FP8+NVFP4 hybrid precision mode is off by default and falls back to FP8 when its assets are absent. `optiscaler/get_hybrid_assets.ps1` can still fetch the hash-pinned upstream archive if the mode is ever reinstated.
