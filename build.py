"""Build the C++ / Dear ImGui example mod into imgui_example.wasm (+ mod.ini).

    python build.py --wasi-sdk C:\\wasi-sdk-34.0-x86_64-windows [--install <game dir>]

Needs wasi-sdk (https://github.com/WebAssembly/wasi-sdk, any recent release; or set WASI_SDK_PATH).
Dear ImGui is fetched once, pinned, into .deps/ next to this file. The module is a WASI "reactor"
(-mexec-model=reactor): the host calls its _initialize (libc + C++ static constructors), then the
brn_mod_* exports. It imports only brn.* and the handful of wasi_snapshot_preview1 calls libc needs
to start; the host's WASI stubs give it no file system, network or environment.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

IMGUI_VERSION = "1.92.9"
IMGUI_SHA256 = "bbdcee6a1d56e38ccddaa58383182b348cfb4f2fc59f47a9ed9ef31f8f966351"
IMGUI_URL = f"https://github.com/ocornut/imgui/archive/refs/tags/v{IMGUI_VERSION}.zip"

HERE = Path(__file__).resolve().parent
DEPS = HERE / ".deps"
OUT = HERE / "out"


def fetch_imgui() -> Path:
    target = DEPS / f"imgui-{IMGUI_VERSION}"
    if (target / "imgui.cpp").is_file():
        return target
    DEPS.mkdir(parents=True, exist_ok=True)
    archive = DEPS / "imgui.zip"
    print(f"Downloading Dear ImGui {IMGUI_VERSION} ...", flush=True)
    with urllib.request.urlopen(IMGUI_URL, timeout=120) as response, archive.open("wb") as output:
        shutil.copyfileobj(response, output)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != IMGUI_SHA256:
        raise RuntimeError("Dear ImGui archive SHA-256 does not match the pinned release")
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            if not (DEPS / name).resolve().is_relative_to(DEPS.resolve()):
                raise RuntimeError("Dear ImGui archive contains an unsafe path")
        package.extractall(DEPS)
    archive.unlink()
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--wasi-sdk", default=os.environ.get("WASI_SDK_PATH"), help="wasi-sdk folder (or WASI_SDK_PATH)")
    parser.add_argument("--install", help="game folder: copies the mod to <dir>\\mods\\imgui_example\\")
    args = parser.parse_args()
    if not args.wasi_sdk:
        parser.error("pass --wasi-sdk or set WASI_SDK_PATH")
    sdk = Path(args.wasi_sdk)
    clang = sdk / "bin" / ("clang++.exe" if os.name == "nt" else "clang++")
    if not clang.is_file():
        parser.error(f"no clang++ in {sdk / 'bin'}")
    imgui = fetch_imgui()
    OUT.mkdir(exist_ok=True)
    wasm = OUT / "imgui_example.wasm"
    sources = [HERE / "main.cpp"] + [imgui / name for name in
                                     ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")]
    command = [
        str(clang), "--target=wasm32-wasip1", f"--sysroot={sdk / 'share' / 'wasi-sysroot'}",
        "-mexec-model=reactor", "-O2", "-std=c++17", "-fno-exceptions", "-fno-rtti",
        "-DIMGUI_DISABLE_FILE_FUNCTIONS",            # no file system: ImGui must not try fopen
        "-DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS",   # nor fork/exec a URL opener
        f"-I{HERE}", f"-I{imgui}",
        *map(str, sources),
        "-Wl,--strip-debug", "-o", str(wasm),
    ]
    print(" ".join(command), flush=True)
    subprocess.run(command, check=True)
    shutil.copy2(HERE / "mod.ini", OUT / "mod.ini")
    print(f"built {wasm} ({wasm.stat().st_size} bytes)")
    if args.install:
        target = Path(args.install) / "mods" / "imgui_example"
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(wasm, target)
        shutil.copy2(HERE / "mod.ini", target)
        print(f"installed -> {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
