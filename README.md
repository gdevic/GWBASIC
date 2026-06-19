# GWBASIC — a GW-BASIC style interpreter with a C64-style screen editor

**▶ [Live demo](https://baltazarstudios.com/experiments/GWBASIC/basic.html)** &nbsp;·&nbsp; **[Source on GitHub](https://github.com/gdevic/GWBASIC)**

> New here? **[GUIDE.md](GUIDE.md)** is the full user's guide — chapters on
> the language, the editor, file I/O, and worked examples.

A single-file C++17 implementation of the classic Microsoft GW-BASIC / BASIC-80
dialect, with a full-screen ncurses editor that works the way the Commodore 64
did: the screen itself is the line buffer. Move the cursor anywhere, edit what
you see, press Enter — if the line starts with a number it goes into the
program, otherwise it executes immediately.

The same source file runs three ways: a native **ncurses** build, a headless
**`-r`** mode for scripting and tests, and — compiled to **WebAssembly** with
Emscripten — right in your browser. The Commodore-64-blue screen at the
**[live demo](https://baltazarstudios.com/experiments/GWBASIC/basic.html)** above
*is* that WASM build.

---

## Try it in your browser

No install, nothing to build: **https://baltazarstudios.com/experiments/GWBASIC/basic.html**

Click the screen and start typing (**Ctrl-C** = Break). The whole interpreter —
editor, dialect, file I/O, all of it — is compiled to WebAssembly and runs
**entirely client-side**: there is no backend, nothing is uploaded, and it keeps
working offline once the page has loaded. The demo programs `STARS.BAS`,
`SIEVE.BAS`, and `GUESS.BAS` are baked into the in-browser filesystem, so `FILES`
lists them and you can `LOAD "STARS.BAS"` then `RUN` straight away.

The look is deliberately retro — C64-blue background, a chunky border, and a
solid blinking block cursor — painted to a `<canvas>` by `shell.html`, with the
interpreter's CP437 box/block glyphs mapped through to Unicode so programs like
STARS render properly. As in the native build, pixel graphics and hardware
statements raise *Advanced Feature* (see *Intentional deviations*); everything
text-mode works.

To host it yourself, copy the three build artifacts — `basic.html`, `basic.js`,
and `basic.wasm` — to any static file server (WASM can't load over `file://`, so
it does need HTTP, not a local file open):

```sh
python -m http.server 8000      # then open http://localhost:8000/basic.html
```

Building those artifacts from source with Emscripten, and the architecture of the
port (the `WasmScreen` backend, Asyncify, the key bridge), are documented in
**[WASM.md](WASM.md)**.

---

## Build (native)

```sh
make            # produces ./basic  (needs g++ and ncursesw)
```

If `libncurses-dev` isn't installed and you have no root, fetch the headers
locally (the Makefile picks them up automatically):

```sh
mkdir -p .deps && cd .deps \
  && apt-get download libncurses-dev \
  && dpkg -x libncurses-dev_*.deb extracted
```

## Run

```sh
./basic                 # full-screen editor
./basic SIEVE.BAS       # editor with a program loaded (type RUN)
./basic -r SIEVE.BAS    # run headless (no curses) and exit
echo 'PRINT 2+2' | ./basic    # piped immediate mode (scripting/tests)
```

## Using the editor

```
10 PRINT "HELLO"        Enter — stored as program line 10
PRINT 2+2               Enter — executes immediately, prints 4, then Ok
LIST                    list the program; cursor-up onto a listed line,
                        edit it in place, press Enter to commit the change
10                      Enter — deletes line 10
RUN / STOP (Ctrl-C) / CONT / NEW / SAVE "F" / LOAD "F" / SYSTEM
```

Keys: arrows move the cursor anywhere (C64-style), Insert toggles
insert/overtype, Delete/Backspace edit, Esc erases the current line, Home
top-left, End jumps to the end of the line, Ctrl-L clears the screen,
Ctrl-C interrupts a running program (`Break in n`, resume with `CONT`).
F1–F10 are GW-style soft keys (`KEY n, "text"` to redefine, `KEY LIST` to
show).

`AUTO [start][,inc]` types line numbers for you; `EDIT n` lists line *n* and
parks the cursor on it; `RENUM [new][,old][,inc]` renumbers and fixes up
`GOTO/GOSUB/THEN/ELSE/ON…/RESTORE/RESUME` references.

## Dialect

Line-numbered GW-BASIC: `GOTO/GOSUB/RETURN`, `FOR/NEXT`, `WHILE/WEND`,
`IF/THEN/ELSE`, `ON n GOTO/GOSUB`, `ON TIMER(n) GOSUB` with `TIMER ON/OFF`,
`DATA/READ/RESTORE`, `DIM` (multi-dim, `OPTION BASE`, `ERASE`), `DEF FN`,
`DEFINT/SNG/DBL/STR`, `SWAP`,
`ON ERROR GOTO / RESUME / ERR / ERL / ERROR n`, `TRON/TROFF`, `CHAIN/COMMON/
MERGE`, `PEEK/POKE` (sandboxed 64 KB), and the full string/math function set
(`LEFT$ RIGHT$ MID$ INSTR STRING$ SPACE$ STR$ VAL CHR$ ASC HEX$ OCT$ SQR SIN
COS TAN ATN EXP LOG INT FIX CINT CDBL CSNG ABS SGN RND TIMER DATE$ TIME$ …`).

GW's arithmetic warning semantics are reproduced: division by zero and
floating overflow print their message, yield "machine infinity"
±1.701412E+38, and the program keeps running; 16-bit integer overflow
promotes to single precision. Exponentiation is left-associative
(`2^3^2` = 64), `CINT` rounds half to even, and `PRINT USING` rounds half
away from zero — all as in GW-BASIC 3.23.

Types: `%` int16, `!` single (7 digits), `#` double (16 digits), `$` strings
(255 chars max). Operators include `\` (integer division), `MOD`, `^`, and the
16-bit logical ops `NOT AND OR XOR EQV IMP`. Literals: `&H10`, `&O17`, `1D9`.

The tokenizer follows GW's rules: keywords are matched only when not followed
by a letter, `$` or `.` — so `TOTAL` is a variable, and `FOR I=1TO10:PRINT I:NEXT`
works without spaces. `?` is shorthand for `PRINT`.

I/O: `PRINT` (zones, `TAB`, `SPC`), full `PRINT USING`, `WRITE`, `INPUT`,
`LINE INPUT`, `INKEY$`, `INPUT$`; sequential files (`OPEN FOR
INPUT/OUTPUT/APPEND`, `PRINT#`, `WRITE#`, `INPUT#`, `LINE INPUT#`, `EOF LOC
LOF`) and random-access files (`FIELD`, `LSET/RSET`, `GET/PUT`,
`MKI$/MKS$/MKD$`, `CVI/CVS/CVD`); `KILL`, `NAME … AS`, `FILES`, `SHELL`.

Screen: `CLS`, `LOCATE`, `COLOR` (16 fg / 8 bg), `WIDTH 40/80`, `POS`,
`CSRLIN`, `SCREEN(row,col[,attr])`, `VIEW PRINT t TO b` (text viewport with
confined scrolling), `KEY ON/OFF` (soft-key row); output goes through a
CP437→Unicode mapping so `CHR$(176)`–`CHR$(223)` box/block characters render
properly.

### Intentional deviations

* Pixel graphics (`PSET`, `LINE`, `CIRCLE`, `DRAW`, `PAINT`, screen modes 1+)
  and hardware statements (`CALL`, `USR`, `WAIT`, `INP/OUT` ports, light pen,
  joystick) raise error 73 *Advanced Feature*. `BEEP`/`SOUND`/`PLAY` beep.
* `LPRINT`/`LLIST` append to `./printer.out` instead of a printer.
* Programs are stored and `SAVE`d as plain ASCII; tokenized `.BAS` binaries
  aren't read, and `LIST` shows lines exactly as you typed them rather than
  re-generating them from tokens.
* `MKS$`/`CVS` use IEEE floats, not Microsoft Binary Format.
* `PRINT#`/`WRITE#`/`INPUT#` are not allowed on RANDOM-mode files (GW lets
  them work through the FIELD buffer); use `FIELD`/`LSET`/`GET`/`PUT`.
* `LET` on a FIELDed string does not permanently detach it from the record
  buffer: the buffer keeps the `LSET` value (as in GW), but a later `GET`
  refreshes the variable again.
* No 64 KB ceiling; `FRE(x)` reports a nostalgic constant.

## Tests

`tests/run.sh` runs the regression suite: each `tests/*.bas` executes under
`basic -r` (with `X.in` as piped input when present), each `tests/*.repl`
pipes into immediate mode, and the output is compared against the golden
`X.out`. The suite covers expression semantics (GW's warning-style division
by zero and overflow, `^` associativity), control flow (the unified
GOSUB/FOR/WHILE stack), DATA edge cases, PRINT USING, the INPUT family,
sequential and random file I/O, and error trapping.

## Demos

* `GUESS.BAS` — number guessing game (`INPUT`, `RND`).
* `SIEVE.BAS` — primes with `PRINT USING` and nested `FOR`.
* `STARS.BAS` — colorful `LOCATE`/`COLOR`/`INKEY$` animation; press a key to
  stop. (Run this one in the editor, not `-r`.)
