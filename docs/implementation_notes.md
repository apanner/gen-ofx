# GenTools OFX (`Alpha_Gen` & `Clean_Gen`) — Implementation Notes

## 1. External Output Export & `Read` Node Creation
- **`getRawOutputPattern()` (`src/AlphaGenSAM.cpp`)**: Reads `node['outputPath'].value()` via Nuke Python (expanding `[...]` TCL expressions only) instead of OFX `paramGetValue()`, preventing Nuke from substituting `0000` into `%04d` / `####`.
- **Case-Insensitive Node Lookup**: `autoDerivePathInNuke()` and `spitReadNode()` match Nuke's lowercase OFX class names (`OFXcom.apanner.alpha_gen_v1`, `OFXcom.apanner.clean_gen_v1`) and read `_GENTOOLS_DERIVED_PATH` via `GetEnvironmentVariableA`.
- **Pre-Job Path Derivation**: `doProcess()` runs `autoDerivePathInNuke()` *before* `buildJob()` when `outputPath` is empty, ensuring `Clean_Gen` writes frames externally to `<shot>/GenAi/Cleanplate/...` instead of only caching inside `dugco_cache`.
- **`Read` Node Spawning (`spitReadNode`)**: Uses `r['file'].setValue(out_pattern)` with `first`, `last`, `origfirst`, and `origlast` so Nuke loads the full `%04d` sequence immediately.

---

## 2. Interactive Viewer Toolbar & Custom Icons
- **Screen-Space OpenGL HUD (`overlayDraw` / `overlayPen` in `src/AlphaGenSAM.cpp`)**:
  - Anchored at top-left of the Nuke Viewer (fixed pixel size regardless of zoom/pan).
  - **Tools**: `Select (+)` (plain Left-Click adds positive point), `Minus (-)` (plain Left-Click adds exclude point), `Erase (×)` (plain Left-Click deletes point), `Pan / Off` (pointer mode; `Ctrl+Click`, `Ctrl+Shift+Click`, `Alt+Click` remain active in all modes).
  - **Object Slots (`1`–`6`)**: Color-coded badges for `Alpha_Gen` multi-object selection.
  - **Actions**: `Undo`, `Clear` (`Shift+Click` clears all frames), `Preview`, `Show/Hide Points`, and `Collapse («)`.
  - **Live Cursor Badge & Tooltips**: Draws `+` / `-` / `×` ring at cursor over the plate and displays hovered tool label.
- **Custom Icons (`icons/` & `~/.nuke/icons/`)**:
  - `Alpha_Gen.png` / `com.apanner.alpha_gen.png` (`32x32`, `128x128`)
  - `Clean_Gen.png` / `com.apanner.clean_gen.png` (`32x32`, `128x128`)
  - `GenTools.png` / `Gen Tools.png` / `Gen_Tools.png`

---

## 3. Comfy Cloud Loopback Bridge (`src/comfy.cpp`)
- `Server::cloudServer()` checks `http://127.0.0.1:8199/health` first (and auto-launches `~/.nuke/python/comfygizmos/cloud_bridge.py` if present) so Nuke never needs direct outbound internet access (`WinHTTP error 12029` / `WinError 10013` safe).
- API key is stored outside git in `~/Documents/DUGco/comfy_cloud_api_key.txt` (or `COMFY_CLOUD_API_KEY`).

---

## 4. Required Models (`Alpha_Gen` & `Clean_Gen`)
- **Core `Alpha_Gen` (~41.8 GB local, 0 GB on Comfy Cloud):**
  - `checkpoints/sam3.1_multiplex_fp16.safetensors` (~1.75 GB)
  - `diffusion_models/ltx-2.5-22b-distilled-transformer-comfy-int8-convrot.safetensors` (~21.50 GB)
  - `text_encoders/gemma4-12b-with-proj-ltx-2.5-comfy-int8-convrot.safetensors` (~15.37 GB)
  - `loras/ltx-2.5-22b-ic-lora-alpha-gen-0.9.safetensors` (~1.30 GB)
  - `vae/ltx-2.5-video-vae-bf16.safetensors` (~1.47 GB)
  - `vae/ltx-2.5-audio-vae-bf16.safetensors` (~0.37 GB)
- **Additional for `Clean_Gen` / `clean` mode:**
  - `loras/ltx-2.5-22b-ic-lora-clean-plate-1.0.safetensors` (~0.33 GB)
  - `latent_upscale_models/ltx-2.5-latent-spatial-upscaler-x2-bf16-1.0.safetensors` (~0.26 GB)
  - *(Optional)* `depthanything/depth_anything_3_mono_large.safetensors` (~1.34 GB)
