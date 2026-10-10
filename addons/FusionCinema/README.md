# Fusion Cinema — optional look

An optional SDR finishing shader and preset by gravy, with subtle shadow fill,
highlight softening, warm colour adjustment and light LumaSharpen sharpening.
It does not require neural rendering. Keep Windows and MSFS HDR off for this preset.

The look is not bundled with laminr.
Install both files from this folder as described below.

For manual downloads, open [Gravy_FusionCinema.fx](Gravy_FusionCinema.fx) and
[Gravy - Fusion Cinema Custom v1.ini](Gravy%20-%20Fusion%20Cinema%20Custom%20v1.ini)
from this folder, then choose **Download raw file** for each.
Keep both filenames unchanged. Source is provided under the repository's GPL-3.0 licence.
LumaSharpen is a separate SweetFX effect, credited and licensed with that package.

## Install

1. Install laminr with **Install laminr** in the manager. The standard and SweetFX
   effects, including LumaSharpen, are bundled.
2. In the in-game menu, open **Image effects** and use **Install FX...** to select
   `Gravy_FusionCinema.fx`.
3. Use **Import INI...** to select `Gravy - Fusion Cinema Custom v1.ini`, then
   **Copy INI and load look**.
4. Keep Image effects enabled. The preset enables Fusion Cinema and LumaSharpen.

For manual installation, locate the folder where laminr is installed (the folder
that contains `FlightSimulator2024.exe`):

* **Xbox:** your MSFS 2024 `Content` folder, for example
  `E:\Xbox games\Microsoft Flight Simulator 2024\Content`.
* **Steam:** Library → Microsoft Flight Simulator 2024 → Manage → Browse local files.
  Use the game executable folder containing `laminrData`.

Copy the FX file to `laminrData\Shaders\Custom\` and the INI to
`laminrData\Presets\`. In game, click **Recompile installed FX**, then choose the
INI from the **Preset 1** list on the Image effects page.
Back up any existing same-name files before manually replacing them.

## Optional REX Atmos CORE companion presets

REX Atmos CORE is a separate product and is not included with laminr.
In REX, enable **Search only public sets** and search **Gravy Fusion Cinema**.
Select and apply these in their respective sections:

* Atmospheric Sets: **Gravy Fusion Cinema SDR v1 - Atmosphere** (ID 28220).
* Environmental Sets: **Gravy Fusion Cinema SDR v2 - Neutral Environment** (ID 17640).

These are public REX sets: no JSON files need to be copied into the REX installation.
The Environment set uses REX's neutral default values. The pair leaves weather selection
to your chosen weather source. Results vary with time, weather, airport and camera angle;
this preset does not create new scene lighting or depth-of-field.

Keep your existing REX sets so you can switch back. Never share your REX `profile.json`;
it contains account and product-key information.
