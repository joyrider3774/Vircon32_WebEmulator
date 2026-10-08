# Web Emulator: Porting Log

This documents the work done to get this repository actually running in a
browser, via Emscripten/WebAssembly. It follows a "minimal path" scope: get
a Vircon32 ROM loading and running in a browser page as directly as
possible, rather than a full, README-faithful port (no settings UI, no
gamepad/joystick support, no persistence — see "Known gaps" at the end).
It has since grown two things beyond that original minimal scope: a custom
page shell designed for embedding on a game page (itch.io and similar), and
an ES2/WebGL1 rendering path for better compatibility with WebKit-based
browsers - see their own sections below.

## Starting state

Before this work, the repository contained:

- `DesktopEmulator/` — a full, unmodified copy of the native desktop
  emulator's source (SDL2 + OpenGL, Dear ImGui settings/menu UI, gamepad
  support, savestates, etc.).
- `VirconDefinitions/` — shared data structures/constants used by both the
  console logic and the emulator layer.
- A `Readme.md` describing an *intended* reduced feature set for the web
  build, but none of that reduction had actually been implemented in code.
- No Emscripten integration at all: no toolchain file, no `.html` shell
  template, no CI, nothing that targeted a browser specifically.

One promising sign found during inspection: the GLSL shaders embedded in
`Emulator/VideoOutput.cpp` were already written as `#version 100` (GLSL ES),
suggesting the video layer had been written with a future web port in mind,
even though the build plumbing to get there had never been set up.

## What was installed

- **Emscripten SDK** (`emsdk`), cloned to `C:\github\emsdk`, `latest` version
  installed and activated. Its environment must be sourced before building:
  `source /c/github/emsdk/emsdk_env.sh` (or `emsdk_env.bat` on a plain
  Windows shell). See [BUILD.md](BUILD.md) for the full build walkthrough.

## Files changed

### `DesktopEmulator/Emulator/VideoOutput.hpp`

Swapped the OpenGL header include so the browser build doesn't try to use
`glad` (a desktop GL function-pointer loader that has nothing to load
under Emscripten, since the GL symbols are already linked in directly):

```cpp
#if defined(__EMSCRIPTEN__)
  #include <GLES2/gl2.h>
#else
  #include <glad/glad.h>
#endif
```

This targets ES2/WebGL1, not ES3/WebGL2 (see below for why, and for what
had to change to make that possible).

`QUAD_QUEUE_SIZE` (the batch size for this renderer's quad queue) is also
bigger under `__EMSCRIPTEN__` specifically (512 vs. the original 20) -
see "Performance: the electron_cat investigation" below for why, and for
the more important fix that came out of the same investigation.

### `DesktopEmulator/Emulator/VideoOutput.cpp`

**Context creation.** `CreateOpenGLWindow()` requests a GLES 2.0 context
under `__EMSCRIPTEN__` (merged with the existing `__arm__` branch, which
already requested the same thing for Raspberry Pi):

```cpp
#if defined(__EMSCRIPTEN__) || defined(__arm__)
  SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
  SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
  SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
#else
  ... (unchanged Core 3.0 request for desktop)
```

The `gladLoadGLLoader()` call is skipped entirely under Emscripten (no
loader needed — the function symbols are already available):

```cpp
#if !defined(__EMSCRIPTEN__)
  LOG( "Initializing GLAD" );
  if( !gladLoadGLLoader( (GLADloadproc)SDL_GL_GetProcAddress ) )
    THROW( "There was an error initializing GLAD" );
#endif
```

**Why ES2, not ES3.** The build originally targeted ES3/WebGL2 (compiling
against it required no code changes at all, since the code already used
VAOs, `glBlitFramebuffer`, `glDrawBuffers`, and sized texture formats -
all ES3/desktop-GL3 features with no ES2 equivalent). That worked and
shipped for a while. It was later revisited because ES3/WebGL2 support has
historically been noticeably less mature than ES2/WebGL1 in WebKit-based
browsers specifically (Safari, GNOME Web/WebKitGTK) - matching real user
reports of intense flickering in GNOME Web and flickering/black screens in
Safari on frames that didn't redraw. ES2/WebGL1 has far more consistent
support across every browser engine, including WebKit's, so the four
ES3-only usages were each given an ES2-safe alternative, guarded behind
`#ifdef __EMSCRIPTEN__` so the desktop Core-profile build is untouched:

1. **Sized texture format.** `CreateFramebuffer()`'s `glTexImage2D` call
   used `GL_RGB8` as the internal format; ES2 requires the internal format
   and format arguments to match (no sized variants), so this is now
   `GL_RGB` under Emscripten:

   ```cpp
   #if defined(__EMSCRIPTEN__)
     GLint ColorInternalFormat = GL_RGB;
   #else
     GLint ColorInternalFormat = GL_RGB8;
   #endif
   ```

2. **`glDrawBuffers`.** Multiple-render-target support, ES3/desktop-only,
   and unnecessary here regardless: this framebuffer only ever has one
   color attachment, which is already its implicit default draw target
   without an explicit call. Skipped entirely under `__EMSCRIPTEN__`.

3. **VAOs.** `glGenVertexArrays`/`glBindVertexArray` in
   `CompileShaderProgram()` are skipped under `__EMSCRIPTEN__`. Desktop
   Core profile *requires* an explicitly bound VAO for any vertex-array
   draw call to work at all (hence the original comment: *"on a core
   OpenGL profile, we need this since the default VAO is not valid!"*) -
   but ES contexts (ES2 and ES3 alike) always have a working default VAO,
   so this is safe to just omit. No other code needed to change to go
   along with it: `BeginFrame()` already re-issues
   `glVertexAttribPointer`/`glEnableVertexAttribArray` unconditionally
   every single frame regardless of VAO state, and `RenderQuadQueue()`
   already re-binds `GL_ARRAY_BUFFER` before every draw - neither one was
   actually depending on a VAO caching anything.

4. **`glBlitFramebuffer`.** `DrawFramebufferOnScreen()` used this to copy
   the offscreen framebuffer's content onto the visible canvas, scaled up
   to the window size. ES2 has no equivalent, so under `__EMSCRIPTEN__`
   this now draws the framebuffer's color texture as a single textured
   quad through the exact same shader used for everything else in this
   renderer instead - visually identical, since that texture already has
   `GL_NEAREST` filtering set. The one subtlety: this renderer's vertex
   shader flips Y when writing INTO the framebuffer (screen-space y=0/top
   ends up at the *last* texel row, not the first), so the replacement
   quad's texture coordinates have to follow that same correspondence, or
   the copied image comes out upside-down - a real bug caught via
   screenshot during testing (see below) and fixed by swapping the V
   coordinates between the quad's top and bottom vertex rows.

**A second bug from the same replacement**, found afterward from a report
that crisp-game-lib's ROM specifically rendered visibly darkened/tinted
under ES2 (desktop and the earlier ES3 build both showed it correctly). A
real hardware blit (the desktop/ES3 path) is a pure pixel copy that never
touches the fragment shader, so it could never be affected by
`MultiplyColor`. This replacement draws through that same shader like
everything else, though, so it inherited whatever tint the console's own
rendering had last left active (e.g. a HUD element's color) instead of
neutral white. Fixed by sending `glUniform4f( MultiplyColorLocation, 1, 1,
1, 1 )` directly right before this draw - bypassing `SetMultiplyColor()`,
which would also permanently overwrite the `MultiplyColor` member field;
this only needs to override the uniform for this one draw, since the next
frame's `BeginFrame()` reasserts the real value unconditionally anyway.

**A pre-existing bug this surfaced.** A GL-version sanity check right
after context creation only special-cased `__arm__` for the "ES 2.0 is
enough" branch; `__EMSCRIPTEN__` fell through to the desktop branch
expecting "OpenGL 3.0" and threw a fatal error on every single startup
once the context request above changed to ES 2.0. Fixed by covering both:

```cpp
#if defined(__arm__) || defined(__EMSCRIPTEN__)
if( glGetError() != GL_NO_ERROR || MajorVersion < 2 )
  THROW( "This computer does not support the minimum required OpenGL version (OpenGL ES 2.0)" );
#else
... (unchanged, requires major >= 3)
```

### `DesktopEmulator/Emulator/MainWeb.cpp` (new file)

A new, Emscripten-only entry point, written instead of trying to strip down
the existing `Main.cpp` (which pulls in Dear ImGui, `GUI.cpp`,
`Settings.cpp`, and `Languages.cpp` — none of which are needed for a
minimal build, and stripping them out of the shared desktop files risked
breaking the desktop build). Investigating each of those dependencies
turned up good news for a minimal port:

- `GamepadsInput.cpp`/`.hpp` in this repository already only supports a
  keyboard device type — the joystick/gamepad-specific code described as
  "removed" in the README had, in fact, already been stripped from this
  file specifically.
- `Settings.cpp`'s `SetDefaultSettings()` does no actual file I/O; its only
  dependency on the GUI layer is a single call to `SetWindowZoom1X()`
  (defined in `GUI.cpp`). `MainWeb.cpp` reimplements the same ~10 lines of
  default-setting logic directly (`SetDefaultSettingsWeb()`), avoiding the
  whole GUI/ImGui dependency chain for the sake of one function call.
- `EmulatorControl.cpp`, `AudioOutput.cpp`, and `Globals.cpp` have no
  dependency on the GUI/Settings/Languages layer at all.

`MainWeb.cpp` itself:

- Duplicates the two small ABI/port sanity checks from `Main.cpp`
  (`PerformABIAssertions`, `PerformPortAssertions`) rather than sharing them,
  to keep this file self-contained.
- Loads a BIOS from a fixed path (`/data/Bios/StandardBios.v32`, baked into
  the build) and a cartridge from `/data/cartridge.v32` - populated at
  *runtime*, not build time; see `preload_cartridge.js` below for how that
  file actually gets there.
- Replaces the desktop version's blocking `while (GlobalLoopActive) { ... }`
  loop with `emscripten_set_main_loop(WebMainLoopStep, 0, 1)`, which hands
  control to the browser: `WebMainLoopStep()` runs once per animation
  frame, polling SDL events, stepping the emulator based on elapsed time
  (same "accumulate frames, run as many as needed" logic as the desktop
  loop), and swapping the framebuffer to the canvas.
- Skips all ImGui setup/rendering and the desktop build's window-focus/
  minimize handling (not meaningful concepts for a browser tab in this
  minimal version).

Three real bugs were found and fixed while getting this file working:

1. **Duplicate audio initialization.** The first version of `MainWeb.cpp`
   called `Audio.Initialize()` explicitly (copying `Main.cpp`'s structure)
   *and* called `Emulator.Initialize()` afterward, which also calls
   `Audio.Initialize()` internally. Desktop SDL2 tolerates opening a second
   audio device silently; Emscripten's SDL2 port does not, and threw
   `"Failed to open audio device: Audio device already open"`. Fixed by
   deleting the redundant explicit call.
2. **Exceptions not caught.** Any `THROW()` (this codebase's C++ exception
   helper) was surfacing in the browser console as
   `Aborted(Assertion failed: Exception thrown, but exception catching is
   not enabled...)` instead of being caught by `MainWeb.cpp`'s own
   `try`/`catch`. Emscripten disables C++ exception catching by default for
   performance; fixed via the `-sDISABLE_EXCEPTION_CATCHING=0` build flag
   (see below), which is what surfaced bug #1 above with a readable message
   in the first place.
3. **A spiral of death in the frame catch-up loop**, reported as a specific
   ROM (a raw-C, non-crisp-game-lib game with a comparatively heavy
   per-frame draw load - a scrolling tunnel built from many individual
   rects) hanging/crashing the browser tab during actual sustained play,
   while the exact same ROM was confirmed fine on the native desktop
   build. `WebMainLoopStep()`'s catch-up logic had no upper bound on how
   many emulator frames it would run in a single callback:

   ```cpp
   PendingFrames += TimeStep * 60.0;
   while( PendingFrames >= 0.9 ) { Emulator.RunNextFrame(); PendingFrames -= 1; }
   ```

   If one frame takes longer than 16.67ms - far more likely under WebGL/
   software rendering, or a heavier-drawing game, than under native
   desktop GL - `PendingFrames` piles up and this loop tries to run
   several emulator frames back-to-back to catch up. If that catch-up
   itself takes longer than the shortfall (plausible under the exact same
   conditions that caused it), the next callback has an even bigger gap
   to make up next time - an accelerating loop that can consume the
   entire JS thread inside one `requestAnimationFrame` callback and never
   yield back to the browser, which looks exactly like a hung tab. This
   explains why it only showed up during sustained play of a
   heavier-drawing game and not at idle/menu, and why it wasn't something
   short automated testing reproduced (see "What this testing cannot
   confirm" below). Fixed by capping catch-up to 5 frames per callback -
   falling behind now just means briefly running in slow motion instead
   of spiraling.

**Memory card support**, added later (see "Memory cards: per-deployment
storage isolation" below for the full story - this is just what changed
in this file specifically). `main()` now derives a card filename from the
cartridge's own title (`GetMemoryCardPath()`), creates a blank card on
this game's first run, and loads it - both before the main loop starts.
`WebMainLoopStep()` pushes the card to IndexedDB via
`Module.persistMemoryCard()` (see `persist_storage.js` below) every ~120
frames (~2 seconds at 60fps, not every single frame - it's an async
IndexedDB write, and there's no reason to fire that dozens of times a
second). Worst case with this interval, a hard crash/tab close loses the
last ~2 seconds of card writes; `persist_storage.js`'s own `pagehide`
listener also attempts one last save on a normal tab close, on top of
this.

  **Bug found and fixed: saves never actually reached IndexedDB.** The
  first version of this periodic check only called `persistMemoryCard()`
  when `Console.WasMemoryCardModified()` was true - which sounds right,
  but never actually was, for a reason specific to how the existing
  (unmodified) console logic works: `V32Console::RunNextFrame()` already
  saves the card to its in-memory virtual file *every single frame* that
  a write happens, and clears that same modified flag immediately
  afterwards - all inside that one frame, before this web-only periodic
  check (which only runs once every ~120 frames) ever got a chance to see
  it as true. So the flag this code was checking had already been
  consumed internally, every time, by something else. The in-memory save
  file itself was always correct and up to date; it just never got pushed
  to IndexedDB after the initial blank-card creation, so it never
  survived a page reload. Confirmed with a real gameplay session (headless
  Playwright driving Worm to an actual game over, not just a static code
  read) showing the modified flag reading false on every check despite
  hundreds of real writes happening moments earlier, then fixed by
  dropping the flag check entirely and pushing to IndexedDB
  unconditionally on the same timer instead - simpler, and correct, since
  an unmodified card is cheap to sync anyway.

### `DesktopEmulator/Emulator/preload_cartridge.js` (new file)

A `--pre-js` hook, added after the first working version of this port
baked one chosen ROM into the build itself (via `--preload-file` and a
`CARTRIDGE_PATH` environment variable read by `build_web.sh`). That meant
switching games required a full rebuild. This hook instead `fetch()`es a
ROM from the same directory as the page itself at page-load time, and
writes it into the Emscripten virtual filesystem (at `/data/cartridge.v32`,
the same fixed path `MainWeb.cpp` already loads from) before `main()` is
allowed to run - via Emscripten's `addRunDependency`/`removeRunDependency`
pattern, which `MainWeb.cpp` never had to know anything about. Practical
effect: dropping a new ROM next to the built `.html`/`.js`/`.wasm`/`.data`
files (named `game.v32`, or any name passed via a `?rom=` query parameter
on the page URL) is now enough to run a different game - no rebuild
needed. The BIOS is still baked in via `--preload-file` (small, rarely
changes).

### `DesktopEmulator/Emulator/persist_storage.js` (new file)

A `--pre-js` hook mounting an IndexedDB-backed directory (via Emscripten's
IDBFS) so memory card saves survive page reloads and browser restarts.
Everything else in the virtual filesystem (BIOS, ROM) is plain in-memory
MEMFS and vanishes when the tab closes - fine for those, since they're
re-fetched/re-mounted fresh every page load anyway; only the memory card
needs to actually persist. Pulls in whatever was saved in a previous
session (`FS.syncfs(true, ...)`) as a blocking `preRun` step, the same
`addRunDependency`/`removeRunDependency` pattern `preload_cartridge.js`
already used for the ROM fetch, so `main()` never runs before this
completes. IDBFS does not sync automatically on every write (it would be
far too slow to hit IndexedDB on every single file write), so pushing
changes back out is a separate, explicit step
(`Module.persistMemoryCard()`), called from `MainWeb.cpp` on its periodic
timer - see there (and the bug fixed in that same section: an earlier
version gated this on a "was modified" flag that never actually read
true from this call site). See "Memory cards: per-deployment storage
isolation" below for why the actual mount path is derived from
`location.pathname` rather than a fixed string.

### `DesktopEmulator/Emulator/shell.html` (new file)

A custom `--shell-file`, replacing Emscripten's default generated page
(the "powered by emscripten" banner, "Resize canvas"/"Lock mouse pointer"
checkboxes, raw stdout `<textarea>`) with one designed for the README's
originally-stated use case: embedding on a game page (itch.io and
similar), in whatever size box that page gives it.

- **Responsive letterboxing**, not a fixed pixel size: the canvas fills
  its container while preserving the console's native 640×360 (16:9)
  aspect ratio, with `image-rendering: pixelated` so scaling up doesn't
  blur the pixel art.
- **No dev-facing controls** - just the screen, a loading indicator, and a
  small fullscreen button.
- **Loading progress**, wired to Emscripten's `monitorRunDependencies`
  (tracks both the BIOS preload and the `preload_cartridge.js` fetch).
  Originally followed by a "Click to start" prompt (browsers block audio
  until a real user gesture, so this doubled as focusing the canvas too),
  but that step was removed later: the overlay now just hides itself
  automatically once loading finishes and focuses the canvas directly.
  Audio still won't play until the browser sees a genuine gesture - its
  autoplay policy, not something this page enforces - but that happens
  naturally the first time someone actually presses a key to play.
- **An FPS counter**, opposite the fullscreen button. Deliberately a
  completely separate `requestAnimationFrame` loop with no dependency on
  `Module`/WASM internals, so it keeps reporting even if the emulator's
  own main loop stalls - which is exactly the scenario worth being able
  to see (e.g. the frame catch-up spiral described above, or the
  per-frame slowdown described in "Performance: the electron_cat
  investigation" below).
- **A small "i" info button**, next to the FPS counter, since this port
  has no controls editor/settings UI at all (see Readme.md) and the
  default keyboard mapping is otherwise invisible to a player. Toggles a
  small centered popup (author/site credit, plus a table of the default
  keyboard mapping read off `GamepadsInput::SetDefaultProfiles()` -
  arrows for the D-pad, X/Z/S/A for buttons A/B/X/Y, Q/W for L/R, Enter
  for Start) - dismissible via its own close button, clicking the
  backdrop, or Escape. The game keeps running underneath while it's open;
  closing it refocuses the canvas the same way the fullscreen toggle
  already did, so keyboard input isn't left stuck on the button.

**Two real bugs found via testing** while wiring up the fullscreen button:

1. **`Cannot read properties of null (reading 'requestFullscreen')`.** The
   JS variable holding the container element was originally named
   `frameElement` - which collides with `window.frameElement`, a genuine
   built-in browser property (returns the containing `<iframe>` if the
   page is embedded, `null` otherwise). Assigning to it silently does
   nothing in non-strict mode (you can't overwrite a getter-only
   property), so the variable always read back as the *native* `null`
   rather than the intended element, and every click threw. Renamed the
   variable to `consoleFrame`.
2. **Blank canvas after exiting fullscreen** (and a related, not-yet-
   reported layout bug found in the process of fixing it): the button
   originally called Emscripten's `Module.requestFullscreen(false, true)`
   helper, whose `resizeCanvas` (`true`) argument changes the canvas
   element's actual backing-store resolution on fullscreen enter/exit -
   but `VideoOutput`'s `WindowWidth`/`WindowHeight` (which its GL viewport
   is based on) are only ever set once at startup, with no resize
   handling anywhere in `MainWeb.cpp`'s event loop to keep them in sync.
   Fixed by using the plain browser Fullscreen API directly on a wrapper
   element instead, never letting the canvas's actual resolution change
   at all (the existing CSS scaling already handles the visual size
   difference, in fullscreen exactly as it does normally). Testing that
   fix then surfaced the related layout bug: Chromium's fullscreen UA
   stylesheet forcibly overrides `max-width`/`max-height` on whatever
   element actually becomes the fullscreen target, which broke the 16:9
   letterboxing specifically while fullscreen was active. Fixed by adding
   an inner `#viewport` wrapper that holds the aspect-ratio/max-width/
   max-height math one level below the element that actually goes
   fullscreen, which the browser's override never touches.
3. **Arrow keys stopped reaching the emulated gamepad in Safari
   specifically**, reported after the "click to start" step (bug/feature
   above) was removed - regular mapped keys (A/X/etc.) kept working the
   whole time, only the D-pad broke. Two fix attempts that seemed
   reasonable didn't help at all when actually tested on Safari: an
   `autofocus` attribute on the canvas, and a `document`-level capture-
   phase `preventDefault()` for arrow keys specifically (the theory being
   that arrow keys, unlike letter keys, have a default browser action -
   scroll/history navigation - to compete with, which would explain why
   only they were affected; that part of the reasoning held up, but
   neither fix actually addressed it, so both were removed again rather
   than left as untested dead code). What actually worked, confirmed
   directly on Safari: intercepting `keydown`/`keyup` on the canvas for
   the four arrow keys specifically, fully stopping the original event
   (`preventDefault()` + `stopImmediatePropagation()`), and re-dispatching
   a brand new synthetic `KeyboardEvent` directly on `window` instead -
   provided by the user from a fix that had already worked for a
   different SDL2/Emscripten application hitting the same Safari-specific
   issue. Emscripten's SDL2 keyboard handling listens on `window`; whatever
   Safari does differently with arrow keys along the canvas→window bubble
   path, this sidesteps it entirely rather than depending on it not
   happening.

### `DesktopEmulator/build_web.sh` (new file)

A standalone build script — deliberately not integrated into the existing
`CMakeLists.txt`, which is wired for the full desktop dependency set
(ImGui, glad, PNG, SDL2-as-a-system-library). It compiles, directly via
`em++`:

- All of `ConsoleLogic/*.cpp` (the actual Vircon32 CPU/GPU/SPU emulation) —
  needed **zero changes**; it was already platform-independent C++ with no
  OS-specific or threading code.
- Three files from `DesktopInfrastructure/` (`FilePaths.cpp`, `Logger.cpp`,
  `StringFunctions.cpp`) — also needed no changes; their Windows-specific
  code paths are all behind a `WINDOWS_OS` macro that is simply never
  defined in this build, leaving the plain POSIX/`fopen`-style code paths,
  which Emscripten's virtual filesystem supports directly.
- Six existing files from `Emulator/` (`AudioOutput.cpp`,
  `EmulatorControl.cpp`, `GamepadsInput.cpp`, `Globals.cpp`,
  `StopWatch.cpp`, the patched `VideoOutput.cpp`), plus the new
  `MainWeb.cpp`.

Key build flags and why each is there:

| Flag | Reason |
|---|---|
| `-O2` | See "Performance: the electron_cat investigation" below - this one change was the actual fix for a real, severe (60fps → 11fps) slowdown under sustained heavy draw load. Originally left at `-O1` throughout development for fast rebuild iteration; there was no longer a reason to ship that once the port stabilized. |
| `-s USE_SDL=2` | Use Emscripten's built-in SDL2 port (maps to WebGL/Web Audio/keyboard events) instead of linking a system SDL2. |
| `-s MIN_WEBGL_VERSION=1 -s MAX_WEBGL_VERSION=1` | Force a WebGL1/GLES2 context - see the `VideoOutput.cpp` section above for why. |
| `-s ALLOW_MEMORY_GROWTH=1` | The console's actual memory needs aren't hardcoded/known up front from the build script's perspective. |
| `-s DISABLE_EXCEPTION_CATCHING=0` | Required for `MainWeb.cpp`'s own `try`/`catch` (and any `THROW()` inside the console logic) to actually catch anything. |
| `-s GL_ASSERTIONS=0` | Removed along with dropping `-s ASSERTIONS=1` (below) - per-GL-call validation overhead that isn't worth paying once the port is stable. Part of the same fix as `-O2`. |
| `-s FORCE_FILESYSTEM=1` | Keeps the full FS API linked in for `preload_cartridge.js`'s `FS.writeFile` call, now that the cartridge is no longer one of the things `--preload-file` itself forces the FS API in for. |
| `-lidbfs.js` | Links in Emscripten's IDBFS (IndexedDB-backed filesystem) for memory card persistence - see `persist_storage.js`'s own section above. |
| `--pre-js Emulator/preload_cartridge.js` | Runtime ROM fetch - see its own section above. |
| `--pre-js Emulator/persist_storage.js` | Persistent memory card storage - see its own section above. |
| `--shell-file Emulator/shell.html` | Custom page shell - see its own section above. |
| `--preload-file Data/Bios/StandardBios.v32@/data/Bios/StandardBios.v32` | Embeds the standard BIOS into the virtual filesystem at a fixed path. |

`-s ASSERTIONS=1` (readable error messages instead of opaque aborts) was
dropped for the same reason as `GL_ASSERTIONS` - real overhead on every
memory access/function call throughout the runtime, worth paying during
active debugging, not worth paying once things are stable. If you're
chasing a new bug, temporarily adding `-s ASSERTIONS=1` back is a
reasonable first step.

Output goes to `WebBuild/`: `Vircon32Web.html`, `.js`, `.wasm`, and `.data`
(the last one now holds just the BIOS - the cartridge is fetched at
runtime, see above). No `CARTRIDGE_PATH` or other ROM-related environment
variable is needed at build time any more.

## Performance: the electron_cat investigation

Reported: a specific ROM (a large one, ~6.8MB - much bigger than the other
test ROMs used throughout this log) ran at a solid 60fps for the first
several seconds, then dropped hard to ~10-11fps and stayed there, for the
rest of the session. The ask was specifically to look at the *emulator's*
performance characteristics, not the game's own logic.

**Getting real data instead of guessing.** Console logs showed nothing at
the exact moment of the drop, so this needed actual measurement:

1. A Chrome DevTools Protocol CPU profile (via Playwright's
   `newCDPSession`) taken across the fast→slow transition showed the
   overwhelming majority of time - well over 60% combined - in
   `getWasmTableEntry`, `wasm-to-js`, `js-to-wasm`, and `invoke_vi`: all
   Emscripten's JS↔WASM call-boundary machinery, not game logic and not
   GPU shader time. This makes sense structurally: WebGL only exists as a
   browser/JS API, so every single GL call this renderer makes
   (`glDrawElements`, `glBindTexture`, `glUniform4f`, ...) has to cross
   from compiled WASM into JS, and that crossing has real per-call
   overhead that native desktop GL never pays at all.
2. Temporary instrumentation (`EM_ASM` counters on `window`, read from the
   test harness, removed again once the investigation concluded) measured
   actual draw-call and quad counts per real second, during both the fast
   and slow periods. Converted to a per-emulated-frame basis (accounting
   for the FPS itself having dropped), this showed the game was issuing
   roughly **10x more quads per frame** during the slow period than the
   fast one (~300/frame → ~3000/frame) - a genuine, large increase in
   rendering workload, not a leak or a growing resource.

**Two reasonable-looking fixes that turned out not to be it**, kept
honest here rather than only reporting what worked:

- Raising `QUAD_QUEUE_SIZE` (see the `VideoOutput.hpp` section above) on
  the theory that hitting the old 20-quad cap mid-run was forcing extra
  flushes. Measured effect: essentially none (10fps → 11fps). Kept anyway
  since it's strictly harmless, but the real cause was elsewhere.
- Batching/sorting draw calls by texture to reduce how often
  `SelectTexture()`'s forced flush (see its own logic) fires. Considered,
  but rejected without being tried: this renderer is a simple
  painter's-algorithm 2D renderer with alpha blending, not a Z-buffered
  one, so anything drawn later relies on appearing on top of anything
  drawn earlier *regardless of texture* - reordering draws across texture
  boundaries risks silently breaking that layering for any game that
  depends on it (very common), for a fix that hadn't even been confirmed
  to address the actual bottleneck yet.

**The actual fix**: `build_web.sh` had been left at `-O1` with
`-s ASSERTIONS=1` the entire time this log covers - both were reasonable
during active development (faster rebuilds, readable error messages) but
both also add real, fixed overhead on top of an already call-heavy path,
with no reason left to keep paying for either once the port had
stabilized. Switched to `-O2`, dropped `-s ASSERTIONS=1`, and added
`-s GL_ASSERTIONS=0` (removes Emscripten's per-GL-call validation
overhead specifically). Re-measured against the same ROM, same workload
(~10 quads/frame higher than before, if anything): a clean, stable 60fps
for the full length of a 40-second test, no dip anywhere. The takeaway
generalizes beyond this one ROM: a busy scene that would run fine at
600%+ headroom on native desktop GL can be genuinely overwhelming under
WASM/WebGL's fixed per-call cost, and optimization/assertion flags that
seem like a minor build-hygiene detail during development turn out to
matter a lot once a game pushes real draw-call volume.

## Memory cards: per-deployment storage isolation

After memory card support was added (see the `MainWeb.cpp` and
`persist_storage.js` sections above), a direct question worth verifying
rather than assuming: if multiple different games are hosted under the
same domain but different subdirectories - a completely normal itch.io-
style hosting pattern, especially since every build here produces the
same generically-named `Vircon32Web.*`/`game.v32` regardless of which
game it actually is - are their memory cards guaranteed not to collide?

**The first version's answer was no.** IndexedDB storage is scoped per
*origin*, not per path. The first version mounted IDBFS at a fixed
`/persist` path regardless of deployment; since Emscripten's IDBFS
database name is tied directly to the mount path, every deployment under
one origin ended up sharing the exact same IndexedDB database. The only
thing preventing actual save-data collisions was the cartridge-title-
based memory card filename (see `MainWeb.cpp` above) - fine for genuinely
different games with different titles, but no protection at all against
the same game (or two games that happen to share a title) being deployed
at more than one subdirectory of the same domain.

**Verified empirically, not just reasoned about:** served two different
games (different cartridge titles) from `/gameA/` and `/gameB/` under one
origin. Before the fix: both showed up as a single `"/persist"` IndexedDB
database, and each game's page could see the *other* game's card file
sitting in the same directory listing. After the fix (below): two
separate databases, each game's page only ever seeing its own card, and
reloading either one still showed only its own card - no cross-
contamination.

**The fix:** `persist_storage.js` now derives the actual IDBFS mount path
from `location.pathname` (sanitized), rather than the fixed `/persist`,
and exposes it as `Module.persistMountPath`. `MainWeb.cpp` reads this at
startup via a small `EM_JS` bridge (`GetPersistMountPathJS()`, using
`stringToNewUTF8()` to hand the JS string over to C++) instead of a
compile-time constant, since the value depends on the browser's URL,
which isn't known at compile time. This gives each deployment a
genuinely separate IndexedDB database - real isolation at the storage
level, not just a naming convention layered on top of shared storage.
The cartridge-title-based filename is kept as a second, cheap layer on
top: harmless for the normal case (one game per deployment), and still
useful in the unusual case of two different games sharing one deployment/
mount path with different titles.

## How this was tested

Rather than just handing back untested files, every change in this log was
actually run in a browser, not just compiled:

1. A local HTTP server (`python3 -m http.server`) was started in
   `WebBuild/`, since the generated page fetches its `.wasm`/`.data`/ROM
   files and does not work when opened directly via `file://`.
2. `playwright-core` was installed (no bundled browser download) and
   pointed at the system-installed Google Chrome, driven headlessly.
3. Pages were loaded, given time to fetch/instantiate the WebAssembly
   module, and screenshotted at each meaningful step (initial load,
   mid-interaction, after a fullscreen toggle, etc.). Console messages,
   failed requests, and page errors were all captured every time.
4. This caught most of the bugs described in this log before they were
   called "done" - the audio crash and swallowed exception in
   `MainWeb.cpp`, the `frameElement` naming collision, the blank-canvas-
   after-fullscreen-exit bug and the letterboxing-breaks-in-fullscreen bug
   it led to, the upside-down rendering from the ES2 blit replacement's
   flipped texture coordinates, and the GL-version check rejecting the ES2
   context it had just been told to request. The ES2 dark-screen
   (MultiplyColor) bug was instead caught from a direct report against a
   specific ROM, then reproduced and confirmed fixed the same way.
5. Interaction was also verified, not just static rendering: clicking the
   canvas (for focus) and holding the key mapped to the console's "A"
   button correctly selected a game and showed that game's own title/
   instructions screen, confirming keyboard input reaches the emulated
   gamepad; toggling the fullscreen button was confirmed to both engage
   real browser fullscreen (`document.fullscreenElement`) and keep the
   game rendering correctly (not blank, not upside-down, still correctly
   letterboxed) throughout enter → active → exit.
6. The one console message that shows up in every run regardless
   (`Failed to load resource: 404`) was confirmed via a direct `curl` to
   be the browser's own automatic `favicon.ico` request — unrelated to
   the emulator.
7. Memory card persistence was verified across an actual page reload, not
   just inferred from the code, and not just for the blank card created on
   first run: a real gameplay session (Playwright driving Worm all the way
   to an actual game over, confirmed via screenshots - not just a title
   screen sitting idle, which an earlier, insufficiently-patient version of
   this same test had mistaken for a real play session) was used to write
   genuine save data, then the card file's bytes were read directly out of
   the virtual filesystem and compared before and after a full page reload
   (fresh JS/WASM state) - byte-for-byte identical, confirming the actual
   written data survives, not just the card's existence. This is what
   caught the "saves never reach IndexedDB" bug described above: the first
   version of this test only checked that a card file was present after
   reload, which passed even while the real bug was live, since the
   blank card from creation was still being persisted correctly - only the
   later, in-game modification wasn't. Per-deployment storage isolation
   (see its own section above) was verified the same way, across two
   different games served from different subdirectories of one origin:
   separate IndexedDB databases, no visibility into each other's card
   files, no cross-contamination after reloading either one.

**What this testing cannot confirm:** all of it was done in headless
Chrome (via `swiftshader` software rendering), the only browser available
in this environment. The ES2/WebGL1 switch was specifically motivated by
reported flickering/black-screen issues in WebKit-based browsers (Safari,
GNOME Web) - this log can confirm the ES2 code path builds and renders
correctly under Chrome, but *not* that it actually resolves those WebKit-
specific symptoms, since no WebKit browser was available to test against
directly. Likewise, the frame catch-up cap (bug #3 in the `MainWeb.cpp`
section) removes the specific unbounded mechanism that would cause the
reported hang, but the hang itself needed real sustained interactive play
to reproduce, which automated headless testing here didn't attempt - so
this log can confirm the mechanism is gone, not that the original report
is resolved end-to-end.

The one exception: the Safari arrow-key fix (bug #3 in the `shell.html`
section) *was* confirmed directly against real Safari, by the user - not
just in headless Chrome. Worth calling out precisely because it's the
exception here: it's also the reason the first two fix attempts for that
same bug could be identified as ineffective and removed again rather than
staying in as untested, unverified guesses.

## Building it yourself

See [BUILD.md](BUILD.md) for the full walkthrough (installing `emsdk` on
Windows/Linux, activating it, running the build, serving it locally). In
short, from `DesktopEmulator/`:

```bash
source /c/github/emsdk/emsdk_env.sh
bash build_web.sh
```

Then copy the `.v32` ROM you want into `WebBuild/` as `game.v32` (or any
name, passed via `?rom=` in the page URL - see `preload_cartridge.js`
above), serve `WebBuild/` over HTTP (e.g. `python3 -m http.server`, from
inside that folder), and open `Vircon32Web.html`.

## Known gaps (minimal-path scope, not yet done)

- **No in-page file picker or drag-and-drop** for choosing a ROM - it's
  still a fixed filename/query-parameter, just resolved at runtime now
  rather than needing a rebuild (see `preload_cartridge.js` above).
- **No settings or controls persistence** — matches the README's stated
  intent for the web build, just not yet exposed via any UI.
- **No gamepad/joystick support** — keyboard only (already true of
  `GamepadsInput.cpp` before this work, not something this port removed).
- **No mute/reset/power UI** — the desktop build's Ctrl+shortcuts for these
  were part of `Main.cpp`'s event loop and were not carried over to
  `MainWeb.cpp`.
- **No on-screen touch controls** — `shell.html`'s viewport meta tag makes
  the page itself mobile-friendly, but input is still keyboard-only.
