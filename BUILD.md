# Building the Web Emulator (Emscripten)

This covers building the minimal Emscripten/WebAssembly port under
`DesktopEmulator/` (see [finished_porting.md](finished_porting.md) for what
it actually contains and why). It bypasses `DesktopEmulator/CMakeLists.txt`
entirely - that one builds the full native desktop emulator (ImGui, glad,
PNG, a system SDL2) and isn't used for the web build at all.

The build script (`DesktopEmulator/build_web.sh`) is a bash script and
needs a bash shell to run it - on Linux/macOS that's just the system shell;
on Windows, use Git Bash (installed with [Git for Windows](https://git-scm.com/download/win))
or WSL. There is no separate `.bat`/PowerShell version.

## 1. Install the Emscripten SDK

You need `emsdk` - it is not something you compile yourself, it downloads
a prebuilt toolchain (clang/LLVM built for WebAssembly, Node.js, Python).

### Linux / macOS

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
```

### Windows (Git Bash)

Same commands work as-is from a Git Bash prompt:

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
```

This downloads a Windows-native clang/LLVM/Node/Python toolchain (a few
hundred MB) into the `emsdk` folder - no MSYS2/mingw/WSL compiler required,
just the bash shell itself to run the `emsdk`/`build_web.sh` scripts.

## 2. Activate the environment

`emsdk activate` only registers the SDK; each new shell still needs its
environment variables (`PATH`, `EMSDK`, etc.) set before `em++` is
available. Do this once per shell session, from wherever you cloned
`emsdk`:

```bash
source /path/to/emsdk/emsdk_env.sh
```

(This is the same one-liner on both platforms when run from Git Bash on
Windows - there is a separate `emsdk_env.bat` for a plain
`cmd.exe`/PowerShell session, but that path hasn't been tested against
this project's build script, since the script itself requires bash
regardless.)

Verify it worked:

```bash
em++ --version
```

## 3. Build

From `DesktopEmulator/`:

```bash
bash build_web.sh
```

Output goes to `DesktopEmulator/WebBuild/`: `Vircon32Web.html`,
`Vircon32Web.js`, `Vircon32Web.wasm`, and `Vircon32Web.data` (the BIOS,
baked in at build time).

No ROM is baked into the build. Copy the `.v32` cartridge you want to run
into `WebBuild/` and name it `game.v32` - it's fetched at page-load time
from wherever the page itself is hosted, not compiled in, so swapping games
doesn't require a rebuild. A different filename can be used instead via a
`?rom=` query parameter, e.g. `Vircon32Web.html?rom=mygame.v32`.

## 4. Run it

The generated page fetches `Vircon32Web.wasm`/`.data`/`game.v32` over
HTTP(S) - it will not work opened directly via `file://`. Serve the
`WebBuild/` folder with any static file server, for example:

```bash
cd DesktopEmulator/WebBuild
python3 -m http.server 8877
```

Then open `http://localhost:8877/Vircon32Web.html`. Click the page once to
start - browsers block audio until a real user gesture, and the same click
focuses the canvas so keyboard input works immediately.

## Rebuilding after a source change

`build_web.sh` recompiles everything from scratch each run (no incremental
build/object caching) - for this project's size that's a matter of
seconds, so this hasn't been a problem worth solving. Just re-run
`bash build_web.sh` after editing any of the files it lists
(`ConsoleLogic/*.cpp`, the handful of `DesktopInfrastructure/` files, or
anything under `Emulator/`).

## Known-harmless console noise

Opening the browser console will show a `Failed to load resource: 404` -
that's the browser's own automatic `favicon.ico` request; this project
doesn't ship one, and it has nothing to do with the emulator itself.
