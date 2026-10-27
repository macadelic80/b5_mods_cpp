# b5_mods_imgui

A C++ / Dear ImGui mod for Burnout 5 (`Burnout_PC.exe` built from
[b5-decomp](https://github.com/BurnoutDecomp/b5-decomp), branch `feat/pc-mods-wasm`), as a
sandboxed **WebAssembly** module -- the C/C++ counterpart of the Rust + egui
[b5_mods](https://github.com/macadelic80/b5_mods) debug menu.

F8 opens a window with the player's position and car, a teleport target, and the game's own
debug menu tree (`brn.debugvars.v1`). ImGui's window layout is kept in the mod's storage
(`mods/imgui_example/data/imgui.ini`).

- `main.cpp` -- the mod. The whole renderer is ~60 lines: `ImDrawVert` is `brn_vertex` byte for
  byte, 16-bit indices go to `brn_ui_submit16`, ImGui 1.92's texture requests map onto
  `brn_ui_texture_create / update / destroy`.
- `brn_mod_api.h` -- a copy of the game's API header (`src/pc/mods/api/brn_mod_api.h`).

## Build and install

Needs [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) and Python. Dear ImGui is fetched once,
pinned (SHA-256), into `.deps/`.

```bat
python build.py --wasi-sdk <wasi-sdk folder> --install <game folder>
```

builds `out/imgui_example.wasm` (`--target=wasm32-wasip1 -mexec-model=reactor`) and copies it with
`mod.ini` into `<game folder>\mods\imgui_example\`.
