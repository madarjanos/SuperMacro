# uMacro — Simple Macro Runner

A lightweight Windows utility that loads a plain-text macro script and
executes it step by step, automating keyboard and mouse actions.

---

## Usage

```
macro.exe <macrofile.txt>
```

The program will display a warning and ask for confirmation before running
the script. Press **ESC** at any time during execution to abort.

---

## Macro File Format

Each line contains one command, optionally followed by up to two parameters
separated by spaces. Empty lines and comment lines (starting with `;` or `//`)
are ignored. Commands are case-insensitive.

```
WAIT 500
MOVE 960 540
CLICK 960 540
KEYPRESS 65
```

### Variable references

Parameters can be literal integers **or** references to memory variables.
A variable reference is written as `$N`, where N is the variable index (0–99).

```
SET $0 640          ; set var 0 to 640
MOVE $0 480         ; x from var 0, y literal 480
MOVE $0 $1          ; both coordinates from variables
```

The `$` prefix can be used wherever a numeric parameter is accepted,
unless stated otherwise in the command description below.

---

## Commands

### Mouse

| Command | Parameters | Description |
|---------|------------|-------------|
| `MOVE` | x y | Move the mouse cursor to pixel coordinates (x, y). |
| `LEFTDOWN` | — | Press and hold the left mouse button. |
| `LEFTUP` | — | Release the left mouse button. |
| `CLICK` | x y | Move to (x, y) and perform a left click (down + up). |
| `GETMOUSE` | $vx $vy | Store the current cursor position in variables vx (X) and vy (Y). Both parameters **must** be variable references. |

### Keyboard

| Command | Parameters | Description |
|---------|------------|-------------|
| `KEYDOWN` | vkey | Press and hold a key (Windows virtual key code). |
| `KEYUP` | vkey | Release a key (Windows virtual key code). |
| `KEYPRESS` | vkey | Press and release a key (KEYDOWN + KEYUP). |
| `KEYPRESS_CTRL` | vkey | Press and release a key while holding Left Ctrl. |

> Virtual key codes are numeric. Common examples: `13` = Enter, `27` = ESC,
> `32` = Space, `65`–`90` = A–Z, `112`–`123` = F1–F12.
> Full list: https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes

### Timing

| Command | Parameters | Description |
|---------|------------|-------------|
| `WAIT` | ms | Pause execution for the given number of milliseconds (max 10 000). |

### Memory Variables

Up to 100 integer memory variables are available, indexed `$0`–`$99`.

| Command | Parameters | Description |
|---------|------------|-------------|
| `SET` | $var value | Set variable `$var` to a literal value or copy from another variable (`SET $0 $1`). First parameter **must** be a variable reference. |
| `ADD` | $var value | Add a literal value or another variable to `$var`. First parameter **must** be a variable reference. |
| `READ` | $var | Read an integer typed by the user on the console and store it in `$var`. Parameter **must** be a variable reference. |

### Console Output

| Command | Parameters | Description |
|---------|------------|-------------|
| `WRITE` | text | Print text to the console (no newline). Everything after `WRITE ` is the string. |
| `WRITELN` | text | Same as `WRITE` but appends a newline. |
| `WRITECHAR` | value | Print a single character given by its ASCII code (literal or variable). E.g. `WRITECHAR 10` prints a newline. |
| `WRITEVAR` | $v1 [$v2] | Print the value of one or two variables separated by a space (no newline). |
| `WRITELNVAR` | $v1 [$v2] | Same as `WRITEVAR` but appends a newline. |

### Loops

Loops are stack-based and can be nested.

| Command | Parameters | Description |
|---------|------------|-------------|
| `LOOP` | count | Begin a loop, repeating `count` times (literal or variable). |
| `ENDLOOP` | — | End of the nearest open loop. |

> - A mismatched `ENDLOOP` (no open `LOOP`) or an unclosed `LOOP` (no matching `ENDLOOP`) is detected at load time and
reported as an error.
> - Loops can be nested until stack overflow.
> - Stack overflow will neither generate error nor halt the script.

```
LOOP 10
    WRITELN outer loop iteration
    LOOP 5
        WRITE .
    ENDLOOP
    WRITECHAR 13
ENDLOOP
```

### Subroutines

Subroutines are stack-based. They can call other subroutines.

| Command | Parameters | Description |
|---------|------------|-------------|
| `SUB` | id | Define the start of subroutine `id` (0–99). Skipped if not reached via `CALL`. |
| `ENDSUB` | — | End of the subroutine. Returns to after the `CALL` that invoked it. |
| `CALL` | id | Call subroutine `id`. Execution resumes after `CALL` when the sub returns. `id` may be a variable reference. |

> - Subroutines can call other subroutines (full call stack supported).
> - If two `SUB` blocks share the same id, the later one in the file wins.
> - A `SUB` without a matching `ENDSUB`, or an `ENDSUB` without an open `SUB`,
>   is detected at load time and reported as an error.
> - `CALL` to an undefined subroutine id is silently ignored at runtime.
> - Recursion is possible but it is a **very bad idea** because you cannot break it!
> - Stack overflow will neither generate error nor halt the script.

```
SUB 0
    WRITE Hello
    CALL 1
ENDSUB

SUB 1
    WRITELN  World!
ENDSUB

LOOP 3
    CALL 0
ENDLOOP
```

### Screen Capture

Output files are saved as PNG in the current working directory,
named `001.png`, `002.png`, etc.

| Command | Parameters | Description |
|---------|------------|-------------|
| `GRABX1Y1` | x y | Set the top-left corner of the capture region. |
| `GRABX2Y2` | x y | Set the bottom-right corner of the capture region. |
| `GRABBPP` | bpp | Set the colour depth: 1 or 24 (default 24). |
| `GRABSTARTIX` | n | Set the starting file name index (first file will be `00n.png`). |
| `SCRGRAB` | — | Capture the defined region and save it as a PNG file. Prints the saved path to the console. |

> - colour depth 1 does mean a black&white conversion with a built-in luminance cut value.
---

## Stopping Execution

Press **ESC** at any time. The macro will halt at the end of the current step.

---

## Load-time Error Checking

The following structural errors are caught when the script is loaded,
before any instruction is executed:

- `ENDLOOP` with no matching open `LOOP`
- One or more `LOOP` blocks with no matching `ENDLOOP`
- `ENDSUB` with no open `SUB`
- `SUB` opened without a matching `ENDSUB`
- Nested `SUB` definition (a `SUB` block inside another `SUB` block)

---

## Example Script

```
; Move the mouse, type "Hi", read a number from the user,
; click that many times, then take a screenshot.

; --- subroutine: perform one click at (500, 400) ---
SUB 0
    CLICK 500 400
    WAIT 150
ENDSUB

; --- setup screen capture ---
GRABX1Y1 0 0
GRABX2Y2 800 600
GRABBPP 24
GRABSTARTIX 1

; --- move and type ---
MOVE 500 400
WAIT 200
KEYPRESS 72   ; H
KEYPRESS 73   ; i

; --- ask user how many clicks ---
WRITELN How many clicks?
READ $0

; --- click $0 times ---
LOOP $0
    CALL 0
ENDLOOP

; --- screenshot ---
WAIT 300
SCRGRAB
```

---

## Building

**MSVC:**
```
cl main.c macro.c pngsave.c /link user32.lib gdi32.lib gdiplus.lib
```

**MinGW / GCC:**
```
gcc main.c macro.c pngsave.c -o macro.exe -luser32 -lgdi32 -lgdiplus
```
