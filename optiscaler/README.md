# OptiScaler in laminr

This folder holds the OptiScaler fork used by laminr. The laminr manager installs the DLL built
here into the game folder as `dxgi.dll` for the Steam version and as `winmm.dll` for the Xbox /
Microsoft Store version.

It is cut down to what Microsoft Flight Simulator 2024 needs on an NVIDIA RTX 50-series GPU:

- DirectX 12 only.
- The game's own NVIDIA DLSS upscaling, DLSS Frame Generation and Reflex pass through.
- DLSS neural rendering (DLSSNR) is added on top. It is set from the in-game menu and is off when the
  game starts.
- It loads laminr's modified ReShade (`ReShade64.dll`).

Neural rendering can run before Super Resolution, after it, deferred (generated before SR and applied
after), or on the finished picture. It supports several model passes, separate skin and scenery
controls, a lower model resolution, and an optional hybrid precision mode. The hybrid mode is off by
default and its assets are not packaged.

Neural rendering needs two more files beside the game executable: NVIDIA's model,
`nvngx_dlssnr.dll`, which the laminr manager downloads, and `nvngx.dll_dlssnr.dll`, a small forwarder
built from this folder.

## What is here

| Path | Contents |
|---|---|
| `OptiScaler/` | Source of the DLL |
| `OptiScaler/dlssnr/` | The neural rendering module. See its [README](OptiScaler/dlssnr/README.md) |
| `OptiScaler/dlssnr/forwarder/` | Source of `nvngx.dll_dlssnr.dll` |
| `docs/` | Notes on neural rendering modes and mechanisms |
| `tests/` | Standalone smoke tests, mostly for neural rendering helpers and shaders |
| `external/` | Vendored dependencies |
| `Licenses/` | RenoDX attribution and licence |
| `laminr.ini` | Settings reference |
| `get_hybrid_assets.ps1` | Fetches the optional hybrid precision assets |

## Building

Build it with the rest of laminr. See [BUILDING.md](../BUILDING.md).

`OptiScaler.sln` has two projects. The DLL builds to `x64/Release/OptiScaler.dll` and the forwarder to
`x64/Release/a/nvngx.dll_dlssnr.dll`. `package.ps1` copies them into the installer payload as
`winmm.dll` and `nvngx.dll_dlssnr.dll`. In a Steam copy the manager installs that `winmm.dll` under
the name `dxgi.dll`.

## Settings

[laminr.ini](laminr.ini) lists the settings with their defaults. The installer writes its own
short `laminr.ini` into the game folder.

## Credits

Built on [OptiScaler](https://github.com/optiscaler/OptiScaler), [Dagherbou's Neural Rendering fork](https://github.com/Dagherbou/OptiScaler_DLSSNR)
and the [OptiScaler DLSSNR PreSR Multipass](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass)
fork, with colour processing from [RenoDX](https://github.com/clshortfuse/renodx).

[Full credits](docs/CREDITS.md) · [Licence](LICENSE)
