# WASM port — working notes & runbook

Goal: compile `basic.cpp` to WebAssembly with Emscripten so the GW-BASIC
interpreter + C64-style editor runs in a browser, with a **retro** look:
Commodore-64 blue background, a solid blinking block cursor, and screen borders.

This file is the living state of that work so the build can be driven from
**Windows CMD** (the Emscripten SDK on this machine is a Windows install and
cannot be invoked from WSL — see "Toolchain" below).

---

## STATUS (2026-06-18)

- [x] Investigated the codebase; the screen layer is cleanly abstracted behind a
      `Screen` virtual base class, so a WASM backend slots in alongside
      `CursesScreen` / `PlainScreen` without touching the interpreter core.
- [x] Confirmed the native build still uses ncurses; WASM build compiles out
      ncurses entirely.
- [x] **DONE:** `WasmScreen` backend in `basic.cpp` (mirrors `CursesScreen`'s
      shadow/attr/wrapped buffers; renders the 80×25 grid via `EM_JS js_present`;
      keys via a JS queue; blocking `waitKey` via `EM_ASYNC_JS` + Asyncify;
      `breakPending` throttles `emscripten_sleep(0)` to ~60 Hz).
- [x] **DONE:** `#ifndef __EMSCRIPTEN__` guards around `<ncurses.h>`,
      `CursesScreen`, the `using EditorScreen` alias, the `stmtShell` curses path
      (browser raises err 73), and `stmtColor`'s `dynamic_cast` (now to
      `EditorScreen`, which covers both backends).
- [x] **DONE:** `main()` `__EMSCRIPTEN__` branch builds `WasmScreen` + `Editor`,
      skips `signal()`.
- [x] **DONE:** `shell.html` — `<canvas>`, C64 palette/border, blinking block
      cursor, CP437→Unicode table mirroring `cp437[]`, `keydown`→packed-key queue,
      `basicPresent/basicPollKey/basicWaitKey/basicBeep/basicBreakFlag` hooks.
- [x] **DONE:** first successful `emcc` build (exit 0, no warnings) →
      `basic.html` + `basic.js` + `basic.wasm`. `STARS.BAS` / `SIEVE.BAS` /
      `GUESS.BAS` embedded into MEMFS.
- [x] **DONE:** added `-fexceptions` after a hang was found — see the big
      warning under the build command. Verified end-to-end with a scripted Node
      harness (type a line, `RUN`, get output + `Ok` prompt, editor stays
      responsive). This was the "hangs on RUN" bug.
- [ ] **TODO:** visual in-browser smoke test (open `basic.html`, `RUN` STARS.BAS,
      confirm blue screen / border / block cursor / colored glyphs / live keys).
      Interpreter behavior under Asyncify is already proven via the Node harness;
      this remaining check is purely the canvas/keyboard look-and-feel.

> First `emcc` run is slow only because Emscripten generates & caches its system
> libraries (libc/libc++/compiler-rt, ~100 s one-time). Re-builds are fast.

---

## Toolchain — why the build runs on Windows

- Emscripten SDK is installed on the **Windows** side: `C:\emsdk`
  (visible from WSL as `/mnt/c/emsdk`).
- Driving `emcc` from **WSL bash fails**:
  ```
  emcc: error: clang executable not found at `/mnt/c/emsdk/upstream/bin/clang`
  ```
  The Linux python wrapper looks for `clang`, but the Windows toolchain only
  ships `clang.exe`. Node is also not on the WSL PATH.
- Conclusion: **run `emcc` from Windows CMD/PowerShell**, where `emsdk_env`
  wires up node + clang.exe correctly.

The C++ source lives on the WSL filesystem at `/home/gdevic/tmp/basic`. From
Windows that path is:
```
\\wsl$\<distro>\home\gdevic\tmp\basic
```
(replace `<distro>` with your WSL distro name, e.g. `Ubuntu`). You can `cd` into
a UNC path in CMD via `pushd \\wsl$\Ubuntu\home\gdevic\tmp\basic`, or copy the
folder to a normal Windows path like `C:\basic` and build there.

---

## BUILD — Windows CMD runbook

```bat
:: 1. Activate the Emscripten environment (sets PATH for emcc/node/clang.exe)
call C:\emsdk\emsdk_env.bat

:: 2. Go to the source. Either a copied Windows folder...
pushd C:\basic
::    ...or straight into the WSL filesystem over UNC:
:: pushd \\wsl$\Ubuntu\home\gdevic\tmp\basic

:: 3. Compile to basic.html + basic.js + basic.wasm
::    (.BAS files are baked into MEMFS so SAVE/LOAD/FILES/RUN can see them)
emcc basic.cpp -std=c++17 -O2 ^
  -fexceptions ^
  -sASYNCIFY ^
  -sALLOW_MEMORY_GROWTH=1 ^
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap ^
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free ^
  --embed-file STARS.BAS --embed-file SIEVE.BAS --embed-file GUESS.BAS ^
  --shell-file shell.html ^
  -o basic.html
```

> **`-fexceptions` is REQUIRED — do not drop it.** Emscripten disables C++
> exception *catching* by default (`DISABLE_EXCEPTION_CATCHING=1`). The
> interpreter's whole control flow is exception-driven (`BasicError`,
> `EndSignal`, `BreakSignal`, `StopSignal`). Without `-fexceptions`, the first
> `throw` — e.g. the `EndSignal` raised when a program finishes — is never
> caught, the call aborts, the Asyncify suspend/resume chain breaks, and the
> editor's next `waitKey()` never resumes: the page **hangs the instant you
> `RUN` anything**. Symptom verified and fixed 2026-06-18. (`-fwasm-exceptions`
> is faster but was not validated against Asyncify here; stick with
> `-fexceptions`.)

> Note: on this machine `emcc` is also reachable from Git Bash
> (`/c/emsdk/upstream/emscripten/emcc`) and builds fine there — the earlier
> "clang not found" WSL failure does not apply to the Git Bash shell, which sees
> `clang.exe`/`node` on PATH. The build above was produced that way.

Notes on the flags:
- `-sASYNCIFY` — **essential.** The interpreter is written as a synchronous
  REPL with blocking `waitKey()` calls (editor loop, `INPUT`, `INPUT$`,
  `INKEY$`). A browser tab cannot block its main thread, so Asyncify is used to
  suspend/resume the WASM stack across an `await` on a JS key Promise. Without
  it the page freezes on the first keypress wait.
- `-sALLOW_MEMORY_GROWTH=1` — programs can DIM large arrays; let the heap grow.
- `--shell-file shell.html` — our C64-themed page template (TODO).
- `_malloc`/`_free` exported so JS can hand buffers in/out if needed (the
  renderer mostly reads the screen buffer straight out of `HEAPU8`).

### Serve & open (Windows)
WASM can't be loaded over `file://`; serve over HTTP. With emsdk's node:
```bat
emrun --no_browser --port 8000 .
:: then open http://localhost:8000/basic.html
```
or any static server (`python -m http.server 8000`).

---

## RUN / TEST from WSL (no WASM needed)

The native build is unaffected and is the fastest way to check interpreter
behavior while iterating on the port:
```sh
make            # ./basic  (ncurses editor)
./basic STARS.BAS
./basic -r SIEVE.BAS
bash tests/run.sh
```

---

## Architecture findings (line numbers are in `basic.cpp`, 4774 lines)

The whole terminal surface is behind one abstract class — this is what makes the
port small and self-contained.

### `struct Screen` — abstract base (lines ~685–731)
Pure-virtual interface every backend implements:
`rows() cols() cls() locate(r,c) row() col() setColor(fg,bg) putByte(b)
flush() charAt(r,c) attrAt(r,c) setViewPrint(t,b) setSoftKeys(labels)
pollKey() waitKey() beepNow() setCursorVisible(v) breakPending() newline()`.
Non-virtual helpers `write()/writeChar()` already translate BASIC control
characters (BEL, BS, TAB, CR/LF, CLS, cursor arrows) into the primitives — so a
backend only needs the primitives above.

### `struct KeyEvent` (lines ~676–680)
```cpp
struct KeyEvent { int ch = -1; int scan = 0; bool none() const { return ch < 0; } };
```
`ch` = ASCII byte, or `0` for an extended key whose `scan` is a PC scan code.
Extended keys surface to BASIC as `CHR$(0)+CHR$(scan)` (GW convention). The
`WasmScreen` must emit the **same scan codes** as `CursesScreen::translate`
(lines ~947–972):
arrows 72/80/75/77, Home 71, End 79, PgUp 73, PgDn 81, Insert 82, Delete 83,
F1–F10 = 59..68, Backspace→ch 8, Enter→ch 13.

### `struct CursesScreen` (lines ~739–1016) — the model to mirror
Keeps the state the C64-style editor depends on:
- `shadow[H]` — W bytes/row of the original **CP437** glyph codes.
- `attrSh[H]` — parallel per-cell attribute `(fg & 15) | ((bg & 7) << 4)`.
- `wrapped[H]` — row continues the previous row's logical line (for stitching
  wrapped lines back together on Enter).
- `pending` — deque of keys read while polling for Ctrl-C break.
Default colors `fg=7, bg=0`. Width limited to ≤80 (`widthLimit`). `H,W` from the
terminal; for WASM fix this at **80×25** (border row handling aside).
`WasmScreen` should keep `shadow`/`attrSh`/`wrapped` identically and render from
them — the editor literally re-reads the screen via `charAt/attrAt`.

### `struct PlainScreen` (lines ~1023+)
Headless stdout/stdin fallback for `-r` and pipes. Not used in the browser, but
keep it for the native/test builds.

### Blocking input call sites (must work under Asyncify)
- `pollKey()` — line ~2057 (`INKEY$`, non-blocking).
- `waitKey()` — lines ~2089 (`INPUT$`), ~2595 (`INPUT`/`LINE INPUT`), ~4675
  (editor main loop).
- `breakPending()` — lines ~1479 (between every statement in `execLoop`), ~3719.
  This is the natural place to periodically yield to the browser event loop
  (`emscripten_sleep(0)`) so the canvas repaints and keydown events enqueue —
  throttle it (e.g. yield only every ~16 ms via `emscripten_get_now()`), not on
  every statement, or tight `FOR…NEXT` loops crawl.

### `main()` (lines ~4722–4774)
Chooses `PlainScreen` for `-r`/non-tty, else `CursesScreen` + `Editor`.
`signal(SIGINT, …)` at line 4733. Under `__EMSCRIPTEN__`: skip the signal, build
a `WasmScreen`, optionally `loadProgramFile`, then `Editor ed(scr,b); ed.run();`.

### OS-specific bits to guard for WASM
- `#include <ncurses.h>` (line 18) and the whole `CursesScreen` → wrap in
  `#ifndef __EMSCRIPTEN__`.
- `stmtShell` (lines ~3959–3975) calls `system()` and curses
  `def_prog_mode/endwin/reset_prog_mode/refresh` inside a
  `dynamic_cast<CursesScreen*>` guard. Under `__EMSCRIPTEN__` there is no
  `CursesScreen` and `system()` is unsupported — make `SHELL` a no-op or raise
  error 73 (*Advanced Feature*).
- `signal`/`SIGINT` (4733): browser has no Ctrl-C signal; map a browser key
  (e.g. Ctrl-C / Ctrl-Break / a dedicated "Stop" button) to set the break flag
  that `breakPending()` checks. `g_sigint` stays as the flag.
- File I/O (`SAVE/LOAD/OPEN/FILES/KILL/NAME`, `dirent.h`, `fnmatch.h`) → works
  against Emscripten **MEMFS**. Preload the demo `.BAS` files with
  `--preload-file STARS.BAS` etc., or `--embed-file`. `printer.out` (LPRINT)
  lands in MEMFS too.

---

## Port plan (the pending code)

### 1. `WasmScreen` backend (in `basic.cpp`, under `#ifdef __EMSCRIPTEN__`)
- Mirror `CursesScreen`'s `shadow/attrSh/wrapped` buffers and all the cursor /
  viewport / soft-key bookkeeping (much of it can be copied near-verbatim, minus
  the curses calls).
- Fixed 80×25 grid. Track cursor `r,c`, `fg,bg`, `viewTop/viewBot`, `keyRowOn`.
- Rendering: on `flush()` (and after `cls`, `locate`, etc.) call a single
  `EM_JS` function `js_present(charsPtr, attrsPtr, W, H, curR, curC, cursorVis)`
  that reads the two buffers out of `HEAPU8` and paints the `<canvas>`. Drawing
  the whole grid each flush is plenty fast for 80×25.
- Input via a JS-side key queue (filled by the page's `keydown` handler):
  - `pollKey()` → `EM_JS` returning the next packed key or `-1` (non-blocking).
  - `waitKey()` → `EM_ASYNC_JS` that `await`s a Promise resolved when a key is
    queued, returns the packed key. Pack as `ch | (scan << 8)`, `none` = `-1`.
  - `breakPending()` → check the JS break flag; throttled `emscripten_sleep(0)`
    to pump the event loop (see note above).
- `beepNow()` → tiny WebAudio blip (or ignore).
- `setCursorVisible()` → toggle the JS cursor-draw flag.

### 2. Guards
- Wrap `<ncurses.h>` + `CursesScreen` in `#ifndef __EMSCRIPTEN__`.
- Fix `stmtShell` `dynamic_cast<CursesScreen*>` path under the same guard.

### 3. `main()` `__EMSCRIPTEN__` branch
Build `WasmScreen scr; Basic b(scr,true); [loadProgramFile]; Editor ed(scr,b);
ed.run();`. No `signal()`.

### 4. `shell.html` (Emscripten shell template) — the retro look
- A `<canvas>` sized to the 80×25 grid times an 8×16 (or 8×8 doubled) font cell.
- **C64 palette** (Pepto): background **blue `#3E31A2`**, border **light-blue
  `#7C70DA`**, default text light-blue. Draw a chunky border rectangle around
  the text area = "screen borders".
- 16-color table mapped to the GW color indices (0..15) for `COLOR`; per-cell
  `attrSh` selects fg/bg.
- **Solid block cursor**: filled cell in the text color, blinking at ~C64 rate
  (toggle a flag on a `setInterval`, re-present).
- Font: a monospace web font or a CP437 bitmap; map glyph codes via the existing
  `cp437[]`/`utf8Of()` (codes ≥128 are box/block chars — important for STARS and
  friends). Simplest first cut: draw `String.fromCharCode(cp437[code])` in a
  monospace font; upgrade to a CP437 bitmap sheet for pixel-accurate blocks.
- `keydown` handler: translate browser keys → packed `{ch,scan}` and push to the
  key queue; resolve the waitKey Promise. Map Ctrl-C to the break flag.
  `preventDefault()` on arrows/Backspace/etc. so the page doesn't scroll.

### 5. Build & verify
Run the Windows CMD build above; serve; open `basic.html`; type `RUN` on
`STARS.BAS`; confirm blue screen, border, block cursor, colored glyphs, and that
keys/`INKEY$`/`INPUT` work.

---

## Retro look — quick spec
| Element       | Value |
|---------------|-------|
| Screen bg     | C64 blue `#3E31A2` |
| Border        | C64 light-blue `#7C70DA`, ~1–2 char-cells thick |
| Default text  | C64 light-blue |
| Cursor        | solid filled block, blinking ~0.5 s, text color |
| Grid          | 80×25 |
| Palette       | 16 C64-flavored colors indexed by GW `COLOR` numbers |
