<div align="center">

# 🌐 ECCLES WEB

### A web engine for microcontrollers. HTML, CSS and JavaScript on your own screen.

**One header · one `.c` · one config header · plain C99 · no heap required**

[![CI](https://github.com/igwe-starking/eccles-web/actions/workflows/ci.yml/badge.svg)](https://github.com/igwe-starking/eccles-web/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-v1.0.0-blue.svg)](CHANGELOG.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C99](https://img.shields.io/badge/C-99-orange.svg)](#)
[![Code size](https://img.shields.io/badge/code-~185%20KB%20(x86--64%20-Os)-brightgreen.svg)](#-memory---the-honest-part)

</div>

```c
#include "eccles_web.h"

eccles_web_set_viewport(320, 480);
eccles_web_load("/spiffs/index.html");   // streamed in small chunks; scripts and styles run as it is parsed
eccles_web_render();                     // paints through YOUR ui backend
```

Build screens in HTML and change them by swapping a file. The engine owns neither the screen nor the
network: you plug in drawing, networking and files as plain function pointers, and decide what a page
is allowed to do.

---

## ✨ Why ECCLES WEB

| | |
|---|---|
| 🧩 **Drop-in** | Copy `eccles_web.h` and `eccles_web.c` into your project. Only the C standard library and libm. |
| 🪶 **No render tree** | The DOM, the style rules and one small box per element are stored. Text is wrapped again while painting. |
| 🌊 **Streaming loader** | Pages are read in small chunks (256 bytes by default) from a file, URL, buffer or your own stream. |
| 📜 **Real scripting** | An ES2017-style core with closures, classes, `async`/`await`, Promises, `fetch`, DOM events and timers. |
| 🔒 **Privileges** | Per-instance permissions. By default a page **cannot** call your C functions. |
| ⛽ **Fuel limits** | `while(true){}` traps instead of freezing the device. |
| 📦 **Fixed budgets** | Page memory is carved from one arena. A page that needs more fails with an error code. |

---

## 🚀 Quick start

No hardware needed. On a PC the default UI backend records every draw call:

```bash
make examples && build/example_host
```
```
  text (0,26) 32px "Hello"
  text (13,100) 16px "Tap me"
page up
tap 1
heading is now: Tapped 1
page called dial("112")
```

The full program is [`examples/host/main.c`](examples/host/main.c).

### On a device

```c
eccles_web_ui_t ui = {0};
ui.fill_rect  = my_fill_rect;
ui.draw_text  = my_draw_text;
ui.text_width = my_text_width;      // must agree with draw_text
eccles_web_ui_override(&ui);

eccles_web_set_viewport(320, 480);
if (eccles_web_load("/spiffs/index.html") != ECCLES_WEB_SUCCESSFUL)
    printf("%s\n", eccles_web_error_name(eccles_web_last_error()));

for (;;) {
    // when your touch driver reports something:
    //   eccles_web_touch(x, y, ECCLES_WEB_TOUCH_DOWN / _MOVE / _UP);
    eccles_web_tick(my_millis());                  // timers
    if (eccles_web_dirty()) eccles_web_render();
}
```

To use it in your own project, copy `src/eccles_web.h`, `src/eccles_web.c` (and optionally
`config/eccles_web_config.h`) into it, then set `ECCLES_WEB_ARENA_SIZE` for your chip
(read [Memory](#-memory---the-honest-part) first).

Also packaged as a **PlatformIO** library (`library.json`), an **Arduino** library (`library.properties`),
an **ESP-IDF** component (`idf_component.yml`) and a plain **CMake** target (`eccles::web`).

### Calling your own C from a page

```c
static eccles_web_status_t dial(eccles_web_vm_t *vm, const eccles_web_val_t *args, uint32_t n, eccles_web_val_t *ret) {
    if (n < 1 || args[0].type != ECCLES_WEB_V_STR) return ECCLES_WEB_FAILED;
    my_modem_dial(args[0].str);
    return ECCLES_WEB_SUCCESSFUL;
}

eccles_web_register("dial", dial);                                          // before loading
eccles_web_set_hal_privilege(ECCLES_WEB_ALLOW_PAGE | ECCLES_WEB_PERM_HOST); // grant it explicitly
```

---

## 🧭 How it works

```
 file / url / buffer ──► chunked reader ──► HTML parser ──► DOM + style rules
                                                │                  │
                                           <script>          layout (1 box per element)
                                                ▼                  ▼
                                   compiler ──► bytecode VM     paint ──► your ui backend
                                   (single pass, no AST)
```

Names (tags, classes, ids, identifiers) are interned once as 16-bit atoms. Script values are 8 bytes
(NaN-boxed), so numbers never allocate. Styles are computed once per layout, not stored per element.

---

## 📚 The API

| | |
|---|---|
| **Load** | `eccles_web_load(url)` · `_load_buffer(html, len, base)` · `_load_stream(read_fn, ctx, base)` · `_unload()` |
| **Show** | `_set_viewport(w, h)` · `_render()` · `_dirty()` · `_scroll_by(dy)` · `_scroll_to(y)` · `_page_height()` |
| **Input & time** | `_touch(x, y, kind)` · `_key(utf8, len)` · `_tick(ms)` · `_next_timer(ms, &wait)` |
| **Navigate** | `_back()` · `_can_go_back()` · `_title()` · `_url()` |
| **Scripts** | `_eval(js)` · `_set_fuel(n)` · `_script_error()` · `_script_error_str()` |
| **Safety** | `_set_hal_privilege(perms)` · `_denied_count()` · `_last_denied()` · `_last_error()` · `_error_name(e)` |
| **Your functions** | `_register(name, fn)` · `_register_async(name, fn)` · `_async_resolve(id, value)` · `_async_reject(id, msg)` |
| **Backends** | `_ui_override(&ops)` · `_set_net(open, read, close)` · `_set_fs(open, read, close)` · `_set_fetch(fn)` · `_set_wasm(&ops)` |
| **Instances** | `eccles_web_vm_create(mem, size)` + the `eccles_web_vm_*` calls: several pages, nothing shared but the backends |

Names starting with `_` are short for `eccles_web_...`. Operations return `eccles_web_status_t`:
**`ECCLES_WEB_SUCCESSFUL`**, **`FAILED`**, **`UNSUPPORTED`** or **`DENIED`**. The full documentation is in
[`src/eccles_web.h`](src/eccles_web.h).

A script error never fails a load, and `eccles_web_eval()` can report success while the script failed:
check `eccles_web_script_error()` (it keeps the **first** error of the page).

### Privileges

| Permission (`ECCLES_WEB_PERM_*`) | Lets a page... | In the default `ALLOW_PAGE` |
|---|---|---|
| `JS` | run scripts and inline handlers | ✓ |
| `NET` | fetch style sheets, scripts, images | ✓ |
| `NAVIGATE` | follow links, use `location` and history | ✓ |
| `TIMERS` | `setTimeout`, `setInterval`, `requestAnimationFrame` | ✓ |
| `LOG` | `console.log`, `alert` | ✓ |
| `HOST` | call functions you registered | – |

---

## 🔌 Where it runs

The engine has no hardware code. The platform macro only picks defaults:

| Platform | Default arena | Memory | Files | UI backend |
|---|---|---|---|---|
| Host (Linux/macOS/Windows) | 2 MB | `malloc` | stdio | draw-call recorder |
| ESP-IDF / Arduino-ESP32 | 128 KB | `malloc` | stdio (VFS) | yours |
| Pico SDK | 128 KB | static | `eccles_web_set_fs()` | yours |
| STM32 HAL, Zephyr, other Arduino | 96 KB ⚠️ | static | `eccles_web_set_fs()` * | yours |

\* mbed targets get stdio. ⚠️ **96 KB is too small for the default budgets with JavaScript on**, see below.

**Network:** none built in. `eccles_web_set_net()` streams pages and sub-resources (TLS, redirects and
chunked decoding are yours); `eccles_web_set_fetch()` serves `fetch()` asynchronously.
**WebAssembly:** `eccles_web_set_wasm()` forwards `WebAssembly.instantiate()` to a runtime such as
[`eccles-wasm`](https://github.com/igwe-starking/eccles-wasm). You write the adapter.

---

## 🧠 Memory - the honest part

All page budgets are reserved when a page starts, so even a tiny page needs the full amount. With the
default embedded budgets, computed for 32-bit pointers:

| Build | Page budgets + instance |
|---|---|
| JavaScript on | ≈ **114 KB** (116,884 bytes) |
| `ECCLES_WEB_JS=0` | ≈ **72 KB** (73,616 bytes) |

On a 64-bit host a small page loaded in a 116 KB arena (76 KB without JavaScript).

So on STM32, Zephyr and non-ESP32 Arduino, either set `ECCLES_WEB_ARENA_SIZE` to at least about
`120 * 1024` or shrink the budgets, otherwise loads fail with `ECCLES_WEB_ERR_NO_MEMORY`.
`ECCLES_WEB_TOTAL_RAM_SIZE=262144` yields an 88 KB arena, which is also too small with JavaScript on.

| Main budgets (embedded default) | |
|---|---|
| `ECCLES_WEB_MAX_NODES` / `_MAX_ATTRS` | 384 / 384 |
| `ECCLES_WEB_TEXT_POOL` | 16 KB |
| `ECCLES_WEB_MAX_ATOMS` / `_ATOM_POOL` | 768 (minimum 512) / 6 KB |
| `ECCLES_WEB_MAX_RULES` / `_MAX_DECLS` | 96 / 384 |
| `ECCLES_WEB_JS_HEAP_SIZE` / `_JS_CODE_SIZE` | 24 KB / 12 KB |
| `ECCLES_WEB_JS_FUEL` | 2,000,000 |

`eccles_web_get_stats()` reports what a page really used. Every macro is listed in
[`config/eccles_web_config.h`](config/eccles_web_config.h).

Code at `-Os` on x86-64 (`make size`): about 185 KB, or 59 KB with `ECCLES_WEB_JS=0`. Largest single
stack frame is about 1.4 KB, and layout recurses up to `ECCLES_WEB_MAX_DEPTH` (32), so give the UI task a
generous stack and measure it.

---

## ✅ What it supports

**HTML:** forgiving streaming parser, void and implied end tags, entities, forms (`input`, `textarea`, `button`).

**CSS:** tag, class, id, `*`, compound, descendant and child selectors, `:first-child` / `:last-child`,
`<style>`, linked sheets, inline `style`, `@media` (width, height, orientation), specificity and
`!important`. Block, inline, inline-block, flex (row/column) and list layout, box model, tables as flex rows.

**JavaScript:** functions, arrows, closures, classes, destructuring in declarations, spread, default
parameters, template literals, `try`/`catch`/`finally`, optional chaining, `??`, `**`, `async`/`await`,
Promises, JSON, Math, Date (UTC), DOM queries and mutation, events with bubbling, `location`, `history`,
`localStorage` (in RAM).

**Not supported:**

- CSS/HTML: floats, absolute/fixed positioning, grid, transforms, animations, web fonts, canvas, SVG,
  iframes, rounded corners, background images.
- JavaScript: regex literals, generators, getters/setters, class fields, labels, `with`, Symbol, Map/Set,
  typed arrays (so Emscripten and wasm-bindgen glue does not run), proxies, modules, tagged templates,
  rest in object patterns, destructuring of parameters or assignments, string arguments to `setTimeout`.
- Behaviour differences: `let` is function scoped, and `return`/`break` inside `try` skips `finally`.

Unknown CSS is skipped. Unsupported script is reported through `eccles_web_script_error()`.

## 🔬 How it was tested

For v1.0.0, on x86-64 Linux:

- Compiles with GCC and Clang (`-std=c99`).
- The host example runs under AddressSanitizer and UBSan (`make san`).
- Script features, privileges, the fuel trap and arena sizing were spot-checked on the host.

> **Be aware:** this repository has no unit test suite or fuzzer yet, and it has not been run on real
> hardware for this release. Treat pages from outside your control with care.

---

## 🗂 Repository layout

```
src/eccles_web.h             API + documentation + configuration surface
src/eccles_web.c             parser, CSS, layout, script engine, DOM, backends
config/eccles_web_config.h   commented template (picked up automatically via __has_include)
examples/host/               runs on your PC
```

## 🛠 Development

```bash
make examples     # build the host example
make san          # run it under ASan + UBSan
make size         # code size, with and without JavaScript
```

Contributions are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md). Found a security issue? See
[SECURITY.md](SECURITY.md).

## 🧩 The EcclesRTLib family

| Library | What it gives your firmware |
|---|---|
| [`eccles-wasm`](https://github.com/igwe-starking/eccles-wasm) | Sandboxed WebAssembly |
| **`eccles-web`** (this one) | HTML, CSS and JavaScript pages |
| [`eccles-gui`](https://github.com/igwe-starking/eccles-gui) | 2D widgets and 3D without a framebuffer |
| [`eccles-media`](https://github.com/igwe-starking/eccles-media) | Images, audio and video |
| [`eccles-rtmem`](https://github.com/igwe-starking/eccles-rtmem) | The rest of your firmware's allocations |

## 📄 License

[MIT](LICENSE) © Nwobodo Ecclesiastes Chidera
