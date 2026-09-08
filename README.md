# M++ Language Documentation
"
WARNING :THIS WAS MADE BY AI "Claude" MAY HAVE BUGS 

i dont think of myself as a programer or coder i know a bit of python and may be able to recreate this as human slop and not ai slop

make fun of me all you want, its fine.

"

> **M++ — "Scratch for Writers"**
> A simple programming language that compiles to C++.
> Describe what should happen — M++ handles the rest.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Project Structure](#project-structure)
3. [Building a Project](#building-a-project)
4. [info.txt — The Bootstrap File](#infotxt--the-bootstrap-file)
5. [.m Files — Your Program](#m-files--your-program)
6. [Comments](#comments)
7. [Instructions](#instructions)
   - [say](#say)
   - [load](#load)
   - [fast load](#fast-load)
   - [clear](#clear)
   - [wait](#wait)
   - [set](#set)
   - [whats](#whats)
   - [v: — Math Operations](#v--math-operations)
   - [Blocks and reload](#blocks-and-reload)
   - [plum](#plum)
   - [lua](#lua)
8. [Variables in say](#variables-in-say)
9. [Lua Bridge](#lua-bridge)
10. [info.txt Rules](#infotxt-rules)
11. [Example Programs](#example-programs)

---

## Getting Started

M++ reads simple `.m` and `info.txt` files and turns them into a real
program your computer can run.

**What will you need:**

You will need to get

chafa and lua and gcc installed on your system.

You can get these by doing a simple google search.

**Build the compiler once:**
"NOTE: If you build something with M++ you need to have MPlusPlus folder or the app and mpp wont work."
```bash
g++ MPlusPlus/compiler.cpp -std=c++17 -o ./mpp
```

**Then build and run any M++ project:**

```bash
./mpp make
./mpp_program
```

---

## Project Structure

Every M++ project looks like this:

```
MyProject/
├── info.txt          ← required, the bootstrap file
├── main.m            ← your main program
├── helper.m          ← other .m files (optional)
├── images/
│   ├── 00.png
│   └── 01.png
└── plugins/
    ├── myplugin.h    ← plugin pointer file
    └── myplugin/
        └── info.txt  ← plugin project
```

And the M++ compiler folder:

```
MPlusPlus/
├── compiler.cpp
└── runtime/
    ├── runtime.hpp
    ├── runtime.cpp
    └── mpp.lua       ← Lua bridge module
```

`make.info` sits next to the compiler and tells it where your project is:

```
MyProject
```

---

## Building a Project

```bash
cd /path/to/your/mpp/compiler
./mpp make
./mpp_program
```

M++ will:
1. Read `make.info` to find your project folder
2. Check `info.txt` for the 500-line limit
3. Generate `generated.cpp`
4. Compile it with `g++`
5. Output `mpp_program`

---

## info.txt — The Bootstrap File

`info.txt` is the **bios** of your M++ project. Without it, nothing loads.
M++ always looks for `info.txt` in the root of your project first.

It is intentionally limited — it is a **pointer file**, not a program.

**What info.txt can do:**
- `load` a `.m`, `.h`, or `.png` file
- `fast load` a `.png`
- `plum` a plugin (`.h`) or Lua file (`.lua`)
- `wait`

**What info.txt cannot do:**
- `say`, `clear`, `set`, `whats`, `v:` operations
- Define blocks or use `reload`
- Load `.lua` files directly (use `plum`)
- Load more than **50 `.m` files**

**500-line limit:** If `info.txt` grows past 500 lines, M++ will
automatically move its contents into `main.m` and replace `info.txt`
with `load main.m`.

**Typical info.txt:**

```
load main.m
```

Or with a splash image:

```
fast load logo.png
wait 1
load main.m
```

---

## .m Files — Your Program

`.m` files are where your actual program lives. They have no line limit
and support the full M++ instruction set.

```
? main.m — the main program ?

say welcome to my game!
set score = 0

whats name
say hello v:name!

load levels/level1.m
```

`.m` files can load other `.m` files:

```
load helper.m
load dialogue.m
```

---

## Comments

Wrap anything in `?` to make it a comment. Comments must start at the
beginning of a line.

```
? this is a comment ?

? this comment
  spans multiple lines ?

say hello  ? this ? is NOT a comment — ? mid-line is literal
```

---

## Instructions

### say

Print text to the terminal.

```
say hello world!
say "hello world!"
```

Quotes are optional and stripped if present. See [Variables in say](#variables-in-say)
for printing variable values.

---

### load

Load and run another M++ file, or display a PNG image.

```
load main.m
load helper.m
load images/00.png
```

- `.m` files are inlined at that point in your program
- `.png` files are displayed in the terminal using `chafa`
- Cannot load `.lua` files — use `plum` for that

---

### fast load

Clear the current display and immediately show a new PNG.
Used for simple image animation.

```
fast load frame00.png
fast load frame01.png
fast load frame02.png
```

Each `fast load` clears the screen then shows the new image,
creating a frame-by-frame effect.

---

### clear

Clear the terminal screen.

```
clear
```

Not allowed in `info.txt`.

---

### wait

Pause execution for a number of seconds. Decimals are fine.

```
wait 2
wait 0.5
wait 0.1
```

Allowed in `info.txt` (useful for timed splash screens).

---

### set

Declare and assign a variable.

```
? declare only (starts empty) ?
set score

? declare and assign ?
set score = 0
set name = user
set message = hello world

? declare and compute in one line ?
set double && v:score * 2
set total && v:other_score + 100
```

Variables hold anything — numbers or text.
Not allowed in `info.txt`.

---

### whats

Ask the user to type something and store it in a variable.

```
say what is your name?
whats name
say hello v:name!

say how old are you?
whats age
v:age + 1
say next year you will be v:age
```

Not allowed in `info.txt`.

---

### v: — Math Operations

Perform math on a variable in place.

```
v:score + 1       ? score = score + 1 ?
v:score - 5       ? score = score - 5 ?
v:score * 2       ? score = score * 2 ?

? right side can also be a variable ?
v:total + v:bonus
v:a * v:b
```

Supported operators: `+`, `-`, `*`

If a variable is empty or non-numeric, it is treated as `0`.
Not allowed in `info.txt`.

---

### Blocks and reload

A **block** is a named chunk of code you can run again with `reload`.

```
loop(
say hello!
)

reload loop 3 times
say done
```

Output:
```
hello!
hello!
hello!
done
```

**Unlimited loop:**

```
tick(
say tick...
wait 1
)

reload tick
```

This loops forever.

**Syntax rules:**

```
? quoted name, plural ?
reload "loop" 5 times

? unquoted name, singular ?
reload loop 5 time

? both work ?
```

**How blocks work:**
- Blocks are defined anywhere in a `.m` file
- Their body only runs when `reload` calls them
- After the block finishes it returns to right after the `reload` line
- `N times` means the block runs exactly N times total

Blocks and `reload` are not allowed in `info.txt`.

---

### plum

Load a plugin or run a Lua file.

```
? run a Lua file ?
plum effects.lua
plum scripts/animation.lua

? load a plugin via its .h pointer file ?
plum myplugin.h
```

**Plugin `.h` file format:**

```
plugin: myplugin/info.txt
```

M++ reads the `.h`, finds the plugin's `info.txt`, and inlines
the plugin's top-level code at that point in your program.

Allowed in both `info.txt` and `.m` files.

---

### lua

Call Lua directly from M++.

```
? run any Lua expression ?
lua os.execute("clear")
lua print("hello from lua")

? call a specific function in a Lua file ?
lua call effects.lua shake
lua call effects.lua flash v:color
lua call game.lua add_score v:points v:multiplier
```

`lua call` passes M++ variables as string arguments to the Lua function.
The function can read and write M++ variables via `mpp.get` / `mpp.set`.

Not allowed in `info.txt`.

---

## Variables in say

Use `v:name` anywhere inside a `say` to print a variable's value.

```
set name = user
set score = 100

say hello v:name!
say your score is v:score points
say v:name scored v:score today
```

Output:
```
hello user!
your score is 100 points
user scored 100 today
```

Punctuation immediately after `v:name` (like `!`, `,`, `.`) is
treated as a literal character, not part of the variable name.

---

## Lua Bridge

M++ and Lua can share variables in both directions.

**In your Lua file:**

```lua
local mpp = require("mpp")

-- read an M++ variable
local score = tonumber(mpp.get("score")) or 0

-- write an M++ variable (visible to M++ after Lua returns)
mpp.set("score", score + 10)
mpp.set("status", "level complete")

-- define a function M++ can call
function double_score(val)
    local n = tonumber(val) or 0
    mpp.set("score", n * 2)
end
```

**In your M++ file:**

```
set score = 50
say score before: v:score

? run the whole lua file ?
plum game.lua

say score after: v:score

? call a specific function, passing a variable ?
lua call game.lua double_score v:score

say score doubled: v:score
```

**How it works:**
- Before calling Lua, M++ writes all variables to a temp bridge file
- Lua reads the bridge file when it calls `require("mpp")`
- `mpp.set()` in Lua writes back to the bridge file immediately
- After Lua returns, M++ re-reads the bridge file
- All changes made by Lua are now visible in your M++ variables

---

## info.txt Rules — Quick Reference

| Instruction     | info.txt | .m file |
|-----------------|----------|---------|
| `load .m`       | ✅        | ✅       |
| `load .png`     | ✅        | ✅       |
| `fast load .png`| ✅        | ✅       |
| `plum .h`       | ✅        | ✅       |
| `plum .lua`     | ✅        | ✅       |
| `wait`          | ✅        | ✅       |
| `say`           | ❌        | ✅       |
| `clear`         | ❌        | ✅       |
| `set`           | ❌        | ✅       |
| `whats`         | ❌        | ✅       |
| `v:` operations | ❌        | ✅       |
| `reload`        | ❌        | ✅       |
| Blocks          | ❌        | ✅       |
| `lua`           | ❌        | ✅       |
| `load .lua`     | ❌        | ❌ (use plum) |
| Max .m files    | 50        | unlimited |
| Max lines       | 500       | unlimited |

---

## Example Programs

### Hello World

**info.txt**
```
load main.m
```

**main.m**
```
say hello world!
```

---

### Ask the User's Name

**main.m**
```
say what is your name?
whats name
say nice to meet you v:name!
```

---

### Simple Counter

**main.m**
```
set count = 0

counter(
v:count + 1
say count: v:count
wait 0.5
)

reload counter 10 times
say finished counting to v:count
```

---

### Image Animation

**main.m**
```
say loading animation...
wait 0.5
fast load frames/00.png
wait 0.1
fast load frames/01.png
wait 0.1
fast load frames/02.png
wait 0.1
fast load frames/03.png
say animation done
```

---

### Splash Screen then Program

**info.txt**
```
fast load logo.png
wait 2
load main.m
```

**main.m**
```
clear
say welcome to my program!
say what would you like to do?
whats choice
say you chose: v:choice
```

---

### Using a Plugin

**myplugin.h**
```
plugin: plugins/myplugin/info.txt
```

**plugins/myplugin/info.txt**
```
load plugin_main.m
```

**plugins/myplugin/plugin_main.m**
```
say plugin loaded!
set plugin_version = 1
```

**main.m**
```
plum myplugin.h
say plugin version: v:plugin_version
```

---

### Lua Integration

**main.m**
```
set score = 0
say enter your starting score:
whats score

? lua will double it ?
lua call scorer.lua double v:score
say your doubled score is v:score
```

**scorer.lua**
```lua
local mpp = require("mpp")

function double(val)
    local n = tonumber(val) or 0
    mpp.set("score", n * 2)
end
```

---

*M++ is a work in progress. More features coming Soon.*
# M-PLUS-PLUS
