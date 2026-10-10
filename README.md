# laminr

Image effects, presets and NVIDIA DLSS neural rendering in one in-game menu for Microsoft Flight Simulator 2024 on NVIDIA GeForce RTX 50-series cards. DirectX 12 only.

## Built on

- [OptiScaler](https://github.com/optiscaler/OptiScaler), through the [OptiScaler-DLSSNR-PreSR-Multipass](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass) fork: DLSS integration and neural rendering.
- [ReShade](https://github.com/crosire/reshade): image effects.
- [RenoDX](https://github.com/clshortfuse/renodx): colour processing.
- NVIDIA DLSS and Streamline: downloaded during setup.

Inspired by [OptiShade](https://github.com/GamingWithGravy/OptiShade).

GPL-3.0 ([LICENSE](LICENSE)); ReShade is BSD 3-Clause. See [NOTICE.md](NOTICE.md).

## Install

You need Microsoft Flight Simulator 2024 (Steam or Xbox) and an NVIDIA GeForce RTX 50-series card.

1. Build the manager as described in [BUILDING.md](BUILDING.md). The EXE ends up in `dist`.
2. Close the simulator and run the manager.
3. Open **Setup** and pick your installation. If it isn't listed, click **Browse** and select the folder with `FlightSimulator2024.exe` (for Xbox, the game's `Content` folder).
4. Click **Install laminr**. Answer **Yes** to DLSS 5 for neural rendering, or **No** for image effects only.
5. Start the game and press **Insert** to open the menu (**Ctrl+Shift+O** also works).

## Troubleshooting

Close the simulator before using the manager.

- **Menu missing or black screen:** in the manager, open **Troubleshooting** and click **Back up and reset laminr settings**. If that doesn't help, click **Repair laminr files**, then **Remove laminr and restore original files**. If MSFS still fails, verify its files in Steam or Xbox.
- **A download failed:** on **Setup**, open **Advanced options and downloads** and click **Retry unfinished downloads**.
- **DLSS or neural-rendering controls unavailable:** select DLSS Super Resolution in the simulator's graphics settings and enter a flight. Neural rendering needs **Yes** to DLSS 5 at install, and always starts off: turn it on with **Use neural rendering** on the menu's **Neural rendering** page.
- **Frame generation unavailable:** turn on hardware-accelerated GPU scheduling in Windows.
- **Logs:** `%LOCALAPPDATA%\laminr\Installer.log` and `laminrData\Performance.log` in the game folder. Check them for personal information before sharing.
