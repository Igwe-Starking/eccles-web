/*
 * MIT License
 *
 * Copyright (c) 2025-2026 Nwobodo Ecclesiastes Chidera
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

//eccles_web, a tiny web engine for microcontrollers (plain C99): html + css + javascript
//one header, one .c file and one config header, no heap needed
//usage is load a page (file, url, buffer or stream, it is parsed in small chunks), render it, feed it touches and ticks, see the README
//
//the engine does NOT own a screen or a network stack. it draws through a ui backend (eccles_web_ui_t) and fetches through a net backend
//(eccles_web_set_net), both are plain function pointers, same idea as eccles_wasm_hal_t. the ui backend is where YOUR ui engine plugs in
//every backend call returns SUCCESSFUL / FAILED / UNSUPPORTED, same values as the c api below (+ DENIED for privilege checks)
//the default backends are stubs: nothing is fetched and (on the host build) every draw command is recorded so you can test without a screen
//
//how it keeps the ram low: there is NO render tree and NO display list. the dom, the style rules and one small box per element are all that is
//stored, text is wrapped again while painting (costs cpu, not ram). styles are computed once per layout, not stored per element.
//names (tags, classes, ids, css/js identifiers) are interned once as 16 bit atoms. script code is compiled to a compact bytecode in a single pass (no ast)
//and values are 8 bytes (nan-boxed), numbers never allocate
//
//html: streaming tokenizer + forgiving tree builder (void elements, implied end tags, entities, raw text for script/style/textarea/title)
//css: tag, .class, #id, *, compound, descendant and child selectors, comma lists, :first-child/:last-child, inline style="", <style>, <link rel=stylesheet>, @media (width/height/orientation),
//specificity + source order + !important, inheritance. display (block, inline, inline-block, flex, list-item, none), box model (margin/padding/border/width/min/max, border-box, auto margins),
//colors (names, #rgb, rgb(), hsl()), fonts (size, weight, style, mono), text-align, line-height, white-space, flex row/column (grow, basis, justify-content, align-items, gap), tables (as flex rows)
//js (es2017 style core): var/let/const (function scoped), functions, arrows, closures, classes (extends, super, static methods), destructuring (declarations, for-of), spread/rest, default parameters,
//template literals, for/for-in/for-of (arrays + strings), switch, try/catch/finally, optional chaining, ??, **, async/await, Promise (then/catch/finally/all/race/allSettled), setTimeout/setInterval/requestAnimationFrame,
//JSON, Math, Date (utc), String/Array/Object/Number methods, console, fetch + WebAssembly + your own functions (sync or async) through host backends
//dom: getElementById, querySelector(All) (same selectors as css), createElement/TextNode, appendChild/insertBefore/removeChild/append/prepend, textContent, innerHTML, attributes, style.x, classList, dataset,
//getBoundingClientRect, events (click/input/change/load/DOMContentLoaded + your own, onclick="" attributes too) with bubbling, location, history, localStorage (in ram), navigator
//NOT supported (yet): float, position:absolute/fixed, grid, transforms, animations, web fonts, canvas, svg, iframes, rounded corners, background images (the ui backend could add them),
//regex literals, generators, getters/setters, class fields, labels, with, Symbol, Map/Set, typed arrays (so emscripten/wasm-bindgen glue does not run, hand written wasm usage does),
//proxies, modules, tagged templates, destructuring of parameters/assignments, setTimeout string arguments. return/break inside try skips finally. let is function scoped (the classic loop closure trap)
//anything it does not understand is skipped (css) or reported with a script_error (js), a page is never aborted because of it
//
//a script error never fails the load, like a browser. look at eccles_web_script_error() for the first one
//scripts run to completion with an instruction budget (eccles_web_set_fuel), a page cant freeze the chip
//
//all the config macros are described in eccles_web_config.h (or pass them with -D), every file must see the same ones

#ifndef ECCLES_WEB_H
#define ECCLES_WEB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

//configuration file - same mechanism as eccles_wasm_config.h.
#if defined(__has_include)
  #if __has_include("eccles_web_config.h")
    #include "eccles_web_config.h"
  #endif
#endif

#define ECCLES_WEB_VERSION_MAJOR 1
#define ECCLES_WEB_VERSION_MINOR 0
#define ECCLES_WEB_VERSION_PATCH 0
#define ECCLES_WEB_VERSION_STRING "1.0.0"

#ifdef __cplusplus
extern "C" {
#endif

//platform + filesystem detection

#define ECCLES_WEB_PLAT_NONE      0
#define ECCLES_WEB_PLAT_HOST      1
#define ECCLES_WEB_PLAT_ARDUINO   2
#define ECCLES_WEB_PLAT_ESP_IDF   3
#define ECCLES_WEB_PLAT_PICO      4
#define ECCLES_WEB_PLAT_STM32_HAL 5
#define ECCLES_WEB_PLAT_ZEPHYR    6

#define ECCLES_WEB_FS_NONE        0
#define ECCLES_WEB_FS_STDIO       1

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  #define ECCLES_WEB_IS_ESP 1
#else
  #define ECCLES_WEB_IS_ESP 0
#endif

#ifndef ECCLES_WEB_PLATFORM
  #if defined(ARDUINO)
    #define ECCLES_WEB_PLATFORM ECCLES_WEB_PLAT_ARDUINO
  #elif defined(ESP_PLATFORM)
    #define ECCLES_WEB_PLATFORM ECCLES_WEB_PLAT_ESP_IDF
  #elif defined(PICO_BUILD) || defined(PICO_ON_DEVICE)
    #define ECCLES_WEB_PLATFORM ECCLES_WEB_PLAT_PICO
  #elif defined(__ZEPHYR__)
    #define ECCLES_WEB_PLATFORM ECCLES_WEB_PLAT_ZEPHYR
  #elif defined(USE_HAL_DRIVER)
    #define ECCLES_WEB_PLATFORM ECCLES_WEB_PLAT_STM32_HAL
  #elif defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    #define ECCLES_WEB_PLATFORM ECCLES_WEB_PLAT_HOST
  #else
    #define ECCLES_WEB_PLATFORM ECCLES_WEB_PLAT_NONE
  #endif
#endif

#ifndef ECCLES_WEB_FS
  #if ECCLES_WEB_PLATFORM == ECCLES_WEB_PLAT_HOST || ECCLES_WEB_IS_ESP || defined(__MBED__)
    #define ECCLES_WEB_FS ECCLES_WEB_FS_STDIO
  #else
    #define ECCLES_WEB_FS ECCLES_WEB_FS_NONE
  #endif
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  #define ECCLES_WEB_BIG_ENDIAN 1
#else
  #define ECCLES_WEB_BIG_ENDIAN 0
#endif

//tunables (see the CONFIGURATION notes above)

#ifndef ECCLES_WEB_USE_HEAP
  #if ECCLES_WEB_PLATFORM == ECCLES_WEB_PLAT_HOST || ECCLES_WEB_IS_ESP
    #define ECCLES_WEB_USE_HEAP 1
  #endif
#endif

#ifndef ECCLES_WEB_ARENA_SIZE
  #if defined(ECCLES_WEB_TOTAL_RAM_SIZE)
    #define ECCLES_WEB__HALF   ((ECCLES_WEB_TOTAL_RAM_SIZE) / 2ul)
    #define ECCLES_WEB__QUART  (24ul * 1024ul + (ECCLES_WEB_TOTAL_RAM_SIZE) / 4ul)
    #if ECCLES_WEB__HALF < ECCLES_WEB__QUART
      #define ECCLES_WEB_ARENA_SIZE ECCLES_WEB__HALF
    #else
      #define ECCLES_WEB_ARENA_SIZE ECCLES_WEB__QUART
    #endif
  #elif defined(__AVR__) || defined(__MSP430__) || defined(__STM8__) || defined(__C51__) \
     || defined(__CX51__) || defined(_PIC12) || defined(_PIC16) || defined(_PIC18) \
     || (defined(__SDCC) && defined(__SDCC_mcs51))
    #error "eccles_web: 8/16-bit MCU - a page needs about 100 KB. Set ECCLES_WEB_ARENA_SIZE (e.g. external RAM) or ECCLES_WEB_TOTAL_RAM_SIZE explicitly."
  #elif ECCLES_WEB_PLATFORM == ECCLES_WEB_PLAT_HOST
    #define ECCLES_WEB_ARENA_SIZE (2048ul * 1024ul)
  #elif ECCLES_WEB_IS_ESP || ECCLES_WEB_PLATFORM == ECCLES_WEB_PLAT_PICO
    #define ECCLES_WEB_ARENA_SIZE (128ul * 1024ul)  //GUESS - set it for production
  #else
    #define ECCLES_WEB_ARENA_SIZE (96ul * 1024ul)  //GUESS - set it for production
  #endif
#endif
#if (ECCLES_WEB_ARENA_SIZE) < 8192ul
  #error "eccles_web: ECCLES_WEB_ARENA_SIZE is too small to hold even a trivial page (need >= 8192)."
#endif

#if ECCLES_WEB_PLATFORM == ECCLES_WEB_PLAT_HOST
  #define ECCLES_WEB__BIG 1
#else
  #define ECCLES_WEB__BIG 0
#endif

#ifndef ECCLES_WEB_MAX_NODES
  #define ECCLES_WEB_MAX_NODES (ECCLES_WEB__BIG ? 2048 : 384)
#endif
#ifndef ECCLES_WEB_MAX_ATTRS
  #define ECCLES_WEB_MAX_ATTRS ECCLES_WEB_MAX_NODES
#endif
#ifndef ECCLES_WEB_TEXT_POOL
  #define ECCLES_WEB_TEXT_POOL (ECCLES_WEB__BIG ? 256ul * 1024ul : 16ul * 1024ul)
#endif
#ifndef ECCLES_WEB_MAX_ATOMS
  #define ECCLES_WEB_MAX_ATOMS (ECCLES_WEB__BIG ? 2048 : 768)
#endif
#ifndef ECCLES_WEB_ATOM_POOL
  #define ECCLES_WEB_ATOM_POOL (ECCLES_WEB__BIG ? 32ul * 1024ul : 6ul * 1024ul)
#endif
#ifndef ECCLES_WEB_MAX_RULES
  #define ECCLES_WEB_MAX_RULES (ECCLES_WEB__BIG ? 512 : 96)
#endif
#ifndef ECCLES_WEB_MAX_DECLS
  #define ECCLES_WEB_MAX_DECLS (ECCLES_WEB__BIG ? 2048 : 384)
#endif
#ifndef ECCLES_WEB_MAX_DEPTH
  #define ECCLES_WEB_MAX_DEPTH 32
#endif
#ifndef ECCLES_WEB_LINE_RUNS
  #define ECCLES_WEB_LINE_RUNS 32
#endif
#ifndef ECCLES_WEB_MAX_FLEX_ITEMS
  #define ECCLES_WEB_MAX_FLEX_ITEMS 16
#endif
#ifndef ECCLES_WEB_CHUNK_SIZE
  #define ECCLES_WEB_CHUNK_SIZE 256
#endif
#ifndef ECCLES_WEB_MAX_HISTORY
  #define ECCLES_WEB_MAX_HISTORY 4
#endif
#ifndef ECCLES_WEB_MAX_URL
  #define ECCLES_WEB_MAX_URL 160
#endif
#ifndef ECCLES_WEB_JS
  #define ECCLES_WEB_JS 1
#endif
#ifndef ECCLES_WEB_JS_HEAP_SIZE
  #define ECCLES_WEB_JS_HEAP_SIZE (ECCLES_WEB__BIG ? 256ul * 1024ul : 24ul * 1024ul)
#endif
#ifndef ECCLES_WEB_JS_CODE_SIZE
  #define ECCLES_WEB_JS_CODE_SIZE (ECCLES_WEB__BIG ? 64ul * 1024ul : 12ul * 1024ul)
#endif
#ifndef ECCLES_WEB_JS_MAX_FUNCS
  #define ECCLES_WEB_JS_MAX_FUNCS (ECCLES_WEB__BIG ? 256 : 64)
#endif
#ifndef ECCLES_WEB_JS_STACK_SLOTS
  #define ECCLES_WEB_JS_STACK_SLOTS 256
#endif
#ifndef ECCLES_WEB_JS_CALL_DEPTH
  #define ECCLES_WEB_JS_CALL_DEPTH 24
#endif
#ifndef ECCLES_WEB_JS_MAX_LOCALS
  #define ECCLES_WEB_JS_MAX_LOCALS 48
#endif
#ifndef ECCLES_WEB_JS_MAX_TRY
  #define ECCLES_WEB_JS_MAX_TRY 8
#endif
#ifndef ECCLES_WEB_JS_MAX_ARGS
  #define ECCLES_WEB_JS_MAX_ARGS 8
#endif
#ifndef ECCLES_WEB_JS_FUEL
  #define ECCLES_WEB_JS_FUEL 2000000ul
#endif
#ifndef ECCLES_WEB_YIELD_INTERVAL
  #define ECCLES_WEB_YIELD_INTERVAL 0
#endif
#ifndef ECCLES_WEB_MAX_TIMERS
  #define ECCLES_WEB_MAX_TIMERS 8
#endif
#ifndef ECCLES_WEB_MAX_LISTENERS
  #define ECCLES_WEB_MAX_LISTENERS 32
#endif
#ifndef ECCLES_WEB_MAX_HOST_FUNCS
  #define ECCLES_WEB_MAX_HOST_FUNCS 16
#endif
#ifndef ECCLES_WEB_JS_MAX_ROOTS
  #define ECCLES_WEB_JS_MAX_ROOTS 48  //values natives keep alive across allocations (a debug build traps when it is exceeded)
#endif
#ifndef ECCLES_WEB_JS_MAX_MICROTASKS
  #define ECCLES_WEB_JS_MAX_MICROTASKS 48
#endif
#ifndef ECCLES_WEB_MAX_FETCH
  #define ECCLES_WEB_MAX_FETCH 8  //requests waiting for the host at the same time (fetch + async host calls)
#endif
#ifndef ECCLES_WEB_MAX_WASM
  #define ECCLES_WEB_MAX_WASM 2  //webassembly instances a page may have
#endif
#ifndef ECCLES_WEB_MAX_NAME
  #define ECCLES_WEB_MAX_NAME 96
#endif
#ifndef ECCLES_WEB_DEFAULT_FONT_PX
  #define ECCLES_WEB_DEFAULT_FONT_PX 16
#endif
#ifndef ECCLES_WEB_LINE_HEIGHT_PCT
  #define ECCLES_WEB_LINE_HEIGHT_PCT 130
#endif
#ifndef ECCLES_WEB_TAP_SLOP_PX
  #define ECCLES_WEB_TAP_SLOP_PX 10
#endif
#ifndef ECCLES_WEB_DEFAULT_PRIVILEGE
  #define ECCLES_WEB_DEFAULT_PRIVILEGE ECCLES_WEB_ALLOW_PAGE  //a page can run, it can NOT call host functions until the owner grants it
#endif

#if (ECCLES_WEB_MAX_NODES) < 16 || (ECCLES_WEB_MAX_NODES) > 65535
  #error "eccles_web: ECCLES_WEB_MAX_NODES must be in 16..65535."
#endif
#if (ECCLES_WEB_MAX_ATOMS) < 512 || (ECCLES_WEB_MAX_ATOMS) > 65535
  #error "eccles_web: ECCLES_WEB_MAX_ATOMS must be in 512..65535 (the built-in names alone take about 400)."
#endif
#if (ECCLES_WEB_CHUNK_SIZE) < 16 || (ECCLES_WEB_CHUNK_SIZE) > 65535
  #error "eccles_web: ECCLES_WEB_CHUNK_SIZE must be in 16..65535."
#endif
#if (ECCLES_WEB_JS_STACK_SLOTS) < 32 || (ECCLES_WEB_JS_STACK_SLOTS) > 65535
  #error "eccles_web: ECCLES_WEB_JS_STACK_SLOTS must be in 32..65535."
#endif
#if (ECCLES_WEB_YIELD_INTERVAL) != 0 && (((ECCLES_WEB_YIELD_INTERVAL) & ((ECCLES_WEB_YIELD_INTERVAL) - 1)) != 0)
  #error "eccles_web: ECCLES_WEB_YIELD_INTERVAL must be 0 or a power of two."
#endif
#ifndef ECCLES_WEB_YIELD
  #define ECCLES_WEB_YIELD() ((void)0)
#endif

//status / error types

//what every operation (and backend call) returns.
typedef enum {
    ECCLES_WEB_SUCCESSFUL = 0,  //did it
    ECCLES_WEB_FAILED     = 1,  //tried and failed (io error, out of memory, bad page, ...)
    ECCLES_WEB_UNSUPPORTED = 2,  //this backend / build cant do that
    ECCLES_WEB_DENIED   = 3  //the instance's privilege forbids it
} eccles_web_status_t;

//detailed reason behind the last FAILED/UNSUPPORTED - eccles_web_last_error(). The js ones are also what eccles_web_script_error() reports
typedef enum {
    ECCLES_WEB_OK = 0,
    ECCLES_WEB_ERR_NOT_INIT,  //(reserved - load/init auto-initialise)
    ECCLES_WEB_ERR_NOT_LOADED,  //no page loaded
    ECCLES_WEB_ERR_BUSY,  //re-entrant call from a host function
    ECCLES_WEB_ERR_NO_MEMORY,  //arena too small; info = bytes that were needed
    ECCLES_WEB_ERR_LIMIT,  //page exceeds a configured limit (nodes, attrs, text pool, rules, atoms, ...); info = which one
    ECCLES_WEB_ERR_IO,  //file/stream read failed or file not found
    ECCLES_WEB_ERR_NO_FS,  //no filesystem backend for eccles_web_load(path)
    ECCLES_WEB_ERR_NO_NET,  //no net backend for an http(s) url
    ECCLES_WEB_ERR_BAD_URL,
    ECCLES_WEB_ERR_UNSUPPORTED_FEATURE,  //valid but uses something we dont run; info = feature id
    ECCLES_WEB_ERR_BAD_ARG,  //bad argument to the API
    ECCLES_WEB_ERR_NO_EXPORT,  //eccles_web_call(): no such global function
    //javascript
    ECCLES_WEB_ERR_JS_SYNTAX,  //info = byte offset in the script, str = what was expected
    ECCLES_WEB_ERR_JS_THROW,  //uncaught exception, str = its message
    //run-time traps
    ECCLES_WEB_TRAP_JS_FUEL,  //instruction budget exhausted
    ECCLES_WEB_TRAP_JS_STACK,  //value stack exhausted
    ECCLES_WEB_TRAP_JS_CALL_DEPTH,  //call stack exhausted
    ECCLES_WEB_TRAP_JS_HEAP,  //script heap exhausted even after a collection
    ECCLES_WEB_TRAP_HOST  //a host function reported failure
} eccles_web_error_t;

typedef enum {
    ECCLES_WEB_TOUCH_DOWN = 0,
    ECCLES_WEB_TOUCH_MOVE = 1,
    ECCLES_WEB_TOUCH_UP = 2
} eccles_web_touch_t;

//runtime: init / load / render

//read callback for eccles_web_load_stream(): fill `buf` with up to `max`
//bytes, return how many you wrote; 0 = end of data; negative = error.
typedef int (*eccles_web_read_fn)(void *ctx, uint8_t *buf, size_t max);

//set up the arena (static array or malloc()). Optional: eccles_web_load*() calls it for you. Calling it again is a safe no-op.
eccles_web_status_t eccles_web_init(void);

//same, but over memory you provide (PSRAM, external SRAM, a linker section).
//`mem` must be 8-byte aligned and stay valid. Replaces any earlier arena.
eccles_web_status_t eccles_web_init_with(void *mem, size_t size);

//loads a page. `url` is a file path ("/spiffs/index.html", "file:///x.html") or an http(s) url when a net backend is installed
//the page is read in small chunks (ECCLES_WEB_CHUNK_SIZE), scripts and style sheets run/apply WHILE it is parsed, like a browser
//any page loaded before is thrown away. relative urls inside the page are resolved against this one
eccles_web_status_t eccles_web_load(const char *url);

//...from an html string in flash/RAM (zero-copy: nothing is buffered). base_url may be NULL
eccles_web_status_t eccles_web_load_buffer(const void *html, size_t len, const char *base_url);

//...from anything you can read bytes from: SD card, flash driver, UART, a socket.
eccles_web_status_t eccles_web_load_stream(eccles_web_read_fn read, void *ctx, const char *base_url);

//drop the page and give its arena space back.
eccles_web_status_t eccles_web_unload(void);

//size of the area the page is drawn in, in px. Call it before the first render and again when the area changes (rotation, keyboard). It re-lays out the page.
eccles_web_status_t eccles_web_set_viewport(uint32_t width, uint32_t height);

//paints the visible part of the page (from the scroll position) through the ui backend. runs layout first when the page changed
eccles_web_status_t eccles_web_render(void);

//scrolling: the position is clamped to the page. These do not paint, call eccles_web_render() after (eccles_web_touch does it for you)
eccles_web_status_t eccles_web_scroll_by(int32_t dy);
eccles_web_status_t eccles_web_scroll_to(int32_t y);
uint32_t eccles_web_scroll_y(void);
uint32_t eccles_web_page_height(void);

//touch input in viewport px. DOWN + MOVEs drag-scroll the page and repaint it, DOWN + UP without moving (ECCLES_WEB_TAP_SLOP_PX) is a tap:
//it is hit-tested, click handlers run (onclick / addEventListener), and when nobody called preventDefault() a link is followed (needs ECCLES_WEB_PERM_NAVIGATE)
eccles_web_status_t eccles_web_touch(int32_t x, int32_t y, eccles_web_touch_t kind);

//keyboard: utf-8 text for the focused <input>/<textarea> (tap one to focus it). "\b" (len 1) is backspace, "\n" in an <input> fires its form "change"
eccles_web_status_t eccles_web_key(const char *utf8, uint32_t len);

//runs due timers (setTimeout / setInterval / requestAnimationFrame). Call it from your loop with a millisecond clock (wraps at 2^32 is fine)
//returns SUCCESSFUL, and when something changed on screen the next eccles_web_render() repaints. eccles_web_next_timer() tells you how long you may sleep
eccles_web_status_t eccles_web_tick(uint32_t now_ms);
bool eccles_web_next_timer(uint32_t now_ms, uint32_t *wait_ms);  //false: no timers pending

//true after something changed the page since the last render (script, touch, load) - skip the repaint when it is false
bool eccles_web_dirty(void);

//history: go to the page before this one (needs ECCLES_WEB_PERM_NAVIGATE). FAILED when there is none
eccles_web_status_t eccles_web_back(void);
bool eccles_web_can_go_back(void);

//title of the page (from <title>, "" when there is none) and its url. valid until the next load
const char *eccles_web_title(void);
const char *eccles_web_url(void);

//scripts

//runs a piece of script in the loaded page, like the console of a browser. needs ECCLES_WEB_PERM_JS
eccles_web_status_t eccles_web_eval(const char *js);

//instruction budget for ONE script / handler / timer callback, calls and backward jumps cost 1 each. 0 means unlimited (default ECCLES_WEB_JS_FUEL)
void eccles_web_set_fuel(uint32_t budget);

//why the last operation failed, plus one extra number (byte offset, bytes needed, depends on the error) and for some errors a text
eccles_web_error_t eccles_web_last_error(void);
uint32_t          eccles_web_last_error_info(void);
const char       *eccles_web_last_error_str(void);
const char       *eccles_web_error_name(eccles_web_error_t e);

//the FIRST script error of the loaded page (a page keeps working after one, so load() still returns SUCCESSFUL) and how many there were
//ECCLES_WEB_OK when none. info = byte offset for syntax errors, str = message / what was expected
eccles_web_error_t eccles_web_script_error(void);
uint32_t           eccles_web_script_error_info(void);
const char        *eccles_web_script_error_str(void);
uint32_t           eccles_web_script_error_count(void);

typedef struct {
    uint32_t arena_size;  //total bytes the engine may use
    uint32_t arena_used;  //bytes in use right now (both ends)
    uint32_t nodes, nodes_max;  //dom nodes in use / allowed
    uint32_t attrs;
    uint32_t text_bytes, text_max;  //text pool
    uint32_t atoms, atoms_max;
    uint32_t rules, decls;
    uint32_t page_height;
    uint32_t js_code_bytes;  //bytecode in use
    uint32_t js_heap_used, js_heap_size;  //after the last collection + what was allocated since
    uint32_t js_gc_runs;
    uint32_t js_ops;  //instructions run for the page so far
    uint16_t functions;
    uint8_t  loaded;
} eccles_web_stats_t;
eccles_web_stats_t eccles_web_get_stats(void);

//privilege = what a PAGE is allowed to do
//every instance has a policy, it only restricts the page (its scripts + links), host c code calling eccles_web_load() etc is trusted
//the default is ECCLES_WEB_DEFAULT_PRIVILEGE: scripts, timers, console and links are fine, host functions are NOT, they usually touch the phone
//a refused call is not an error for the page, the script just gets `undefined` / nothing happens and the denied counter goes up
#define ECCLES_WEB_PERM_JS        (1u << 0)  //run scripts: <script>, inline handlers, eccles_web_eval
#define ECCLES_WEB_PERM_NET       (1u << 1)  //fetch sub resources over the net backend: <link rel=stylesheet>, <script src>, <img src>
#define ECCLES_WEB_PERM_NAVIGATE  (1u << 2)  //follow links, location.href, history
#define ECCLES_WEB_PERM_TIMERS    (1u << 3)  //setTimeout / setInterval / requestAnimationFrame
#define ECCLES_WEB_PERM_LOG       (1u << 4)  //console.log / alert
#define ECCLES_WEB_PERM_HOST      (1u << 5)  //call functions the host added with eccles_web_register()

#define ECCLES_WEB_ALLOW_NONE     0u  //html + css only
#define ECCLES_WEB_ALLOW_STATIC   (ECCLES_WEB_PERM_NET | ECCLES_WEB_PERM_NAVIGATE)
#define ECCLES_WEB_ALLOW_PAGE     (ECCLES_WEB_PERM_JS | ECCLES_WEB_PERM_NET | ECCLES_WEB_PERM_NAVIGATE | ECCLES_WEB_PERM_TIMERS | ECCLES_WEB_PERM_LOG)
#define ECCLES_WEB_ALLOW_ALL      0x3Fu

eccles_web_status_t eccles_web_set_hal_privilege(uint32_t perms);  //e.g. ECCLES_WEB_ALLOW_PAGE | ECCLES_WEB_PERM_HOST
#define eccles_web_set_hal_priviledge eccles_web_set_hal_privilege  //spelling alias, same as eccles_wasm
uint32_t    eccles_web_get_privilege(void);
uint32_t    eccles_web_denied_count(void);  //refused page calls so far
const char *eccles_web_last_denied(void);  //name of the last refused call, or ""

//host functions (your own functions callable from the page) 

//a value going in or out of a host function. strings are NUL terminated and only valid during the call (copy them)
typedef enum { ECCLES_WEB_V_UNDEF = 0, ECCLES_WEB_V_NULL, ECCLES_WEB_V_BOOL, ECCLES_WEB_V_NUM, ECCLES_WEB_V_STR, ECCLES_WEB_V_OTHER } eccles_web_vtype_t;
typedef struct { uint8_t type; double num; const char *str; } eccles_web_val_t;  //BOOL uses num (0/1), STR uses str, OTHER (object/function/element) is passed as undefined-like

//host function: the arguments come in args[0..nargs), put the result in *ret (it starts as undefined). Return SUCCESSFUL,
//anything else makes the script throw an error (ECCLES_WEB_TRAP_HOST)
typedef struct eccles_web_vm_s eccles_web_vm_t;
typedef eccles_web_status_t (*eccles_web_host_fn)(eccles_web_vm_t *vm, const eccles_web_val_t *args, uint32_t nargs, eccles_web_val_t *ret);

//async host function: the page gets a Promise. The engine hands you `call_id`, you return SUCCESSFUL (= accepted) and later, from your own loop or
//task (never from inside another script callback), call eccles_web_async_resolve() / eccles_web_async_reject() with that id. FAILED rejects at once
typedef eccles_web_status_t (*eccles_web_async_fn)(eccles_web_vm_t *vm, uint32_t call_id, const eccles_web_val_t *args, uint32_t nargs);
eccles_web_status_t eccles_web_register_async(const char *name, eccles_web_async_fn fn);
eccles_web_status_t eccles_web_vm_register_async(eccles_web_vm_t *vm, const char *name, eccles_web_async_fn fn);
eccles_web_status_t eccles_web_vm_async_resolve(eccles_web_vm_t *vm, uint32_t call_id, const eccles_web_val_t *value);
eccles_web_status_t eccles_web_vm_async_reject(eccles_web_vm_t *vm, uint32_t call_id, const char *message);

//fetch(): the page's fetch() calls this backend. Return SUCCESSFUL to accept (the answer comes later through eccles_web_vm_fetch_respond / _fail with
//the same id, from your loop or task), FAILED rejects the promise at once. Without a backend fetch() falls back to the (blocking) net backend of eccles_web_set_net()
//needs ECCLES_WEB_PERM_NET. headers is "Name: value\r\n" lines, body may be NULL
typedef struct { const char *url, *method, *headers; const uint8_t *body; uint32_t body_len; } eccles_web_request_t;
typedef eccles_web_status_t (*eccles_web_fetch_fn)(eccles_web_vm_t *vm, uint32_t id, const eccles_web_request_t *req);
void eccles_web_set_fetch(eccles_web_fetch_fn fn);
eccles_web_status_t eccles_web_vm_fetch_respond(eccles_web_vm_t *vm, uint32_t id, uint32_t status, const char *headers, const void *body, size_t len);
eccles_web_status_t eccles_web_vm_fetch_fail(eccles_web_vm_t *vm, uint32_t id, const char *message);

//WebAssembly: when a page calls WebAssembly.instantiate() the bytes go to this backend, connect it to eccles_wasm. Every function is optional except
//instantiate + call. A returned instance handle is yours (the engine only passes it back). bytes are only valid during the instantiate call, copy them if you need them
//instance.exports of the page has one function per export (numbers in, number out), and a memory object with read(addr, len) / write(addr, string)
typedef struct {
    void *(*instantiate)(eccles_web_vm_t *vm, const uint8_t *bytes, uint32_t len, char *err, uint32_t errcap);  //NULL = failed, put the reason in err
    uint32_t (*export_count)(void *inst);
    const char *(*export_name)(void *inst, uint32_t index, int *kind);  //kind: 0 function, 1 memory
    eccles_web_status_t (*call)(void *inst, const char *name, const eccles_web_val_t *args, uint32_t nargs, eccles_web_val_t *ret);
    uint32_t (*mem_size)(void *inst);
    eccles_web_status_t (*mem_read)(void *inst, uint32_t addr, void *dst, uint32_t len);
    eccles_web_status_t (*mem_write)(void *inst, uint32_t addr, const void *src, uint32_t len);
    void (*destroy)(void *inst);
} eccles_web_wasm_t;
void eccles_web_set_wasm(const eccles_web_wasm_t *ops);
//call this from inside your wasm import handler: runs imports[module][name] of the page (a script function) and gives back its result
eccles_web_status_t eccles_web_vm_wasm_call_import(eccles_web_vm_t *vm, void *inst, const char *module, const char *name, const eccles_web_val_t *args, uint32_t nargs, eccles_web_val_t *ret);

//makes fn callable from the page as a global function `name` (a "phone dial" api for your own pages). Needs ECCLES_WEB_PERM_HOST to be called.
//call this BEFORE loading, globals are created when the page starts. name must stay valid (string literals are ok)
eccles_web_status_t eccles_web_register(const char *name, eccles_web_host_fn fn);
void                eccles_web_unregister_all(void);

//the handle-free versions of the async calls, for the default instance
eccles_web_status_t eccles_web_async_resolve(uint32_t call_id, const eccles_web_val_t *value);
eccles_web_status_t eccles_web_async_reject(uint32_t call_id, const char *message);
eccles_web_status_t eccles_web_fetch_respond(uint32_t id, uint32_t status, const char *headers, const void *body, size_t len);
eccles_web_status_t eccles_web_fetch_fail(uint32_t id, const char *message);

//multiple instances: everything above works on ONE default instance, for more pages (or one per rtos task) make the instances yourself
//every instance has its own arena, state, host function table and error state, nothing is shared
//so different instances can run in parallel on different tasks, but one instance must only be driven by one task at a time
//the ui / net / fs backends below are chip-level and shared, make them thread safe if you run more than one page
//the instance struct lives at the start of the memory you pass in so there is no separate allocation
eccles_web_vm_t *eccles_web_vm_create(void *mem, size_t size);  //NULL if too small
#if ECCLES_WEB_USE_HEAP
eccles_web_vm_t *eccles_web_vm_create_heap(size_t size);  //malloc()s the arena
#endif
void  eccles_web_vm_destroy(eccles_web_vm_t *vm);  //frees a heap arena; the instance is unusable afterwards
eccles_web_vm_t *eccles_web_default_vm(void);  //the instance behind the handle-free API
void  eccles_web_vm_set_user(eccles_web_vm_t *vm, void *user);  //your pointer, readable from host functions
void *eccles_web_vm_user(const eccles_web_vm_t *vm);

eccles_web_status_t eccles_web_vm_load(eccles_web_vm_t *vm, const char *url);
eccles_web_status_t eccles_web_vm_load_buffer(eccles_web_vm_t *vm, const void *html, size_t len, const char *base_url);
eccles_web_status_t eccles_web_vm_load_stream(eccles_web_vm_t *vm, eccles_web_read_fn read, void *ctx, const char *base_url);
eccles_web_status_t eccles_web_vm_unload(eccles_web_vm_t *vm);
eccles_web_status_t eccles_web_vm_set_viewport(eccles_web_vm_t *vm, uint32_t width, uint32_t height);
eccles_web_status_t eccles_web_vm_render(eccles_web_vm_t *vm);
eccles_web_status_t eccles_web_vm_scroll_by(eccles_web_vm_t *vm, int32_t dy);
eccles_web_status_t eccles_web_vm_scroll_to(eccles_web_vm_t *vm, int32_t y);
uint32_t eccles_web_vm_scroll_y(const eccles_web_vm_t *vm);
uint32_t eccles_web_vm_page_height(const eccles_web_vm_t *vm);
eccles_web_status_t eccles_web_vm_touch(eccles_web_vm_t *vm, int32_t x, int32_t y, eccles_web_touch_t kind);
eccles_web_status_t eccles_web_vm_key(eccles_web_vm_t *vm, const char *utf8, uint32_t len);
eccles_web_status_t eccles_web_vm_tick(eccles_web_vm_t *vm, uint32_t now_ms);
bool eccles_web_vm_next_timer(const eccles_web_vm_t *vm, uint32_t now_ms, uint32_t *wait_ms);
bool eccles_web_vm_dirty(const eccles_web_vm_t *vm);
eccles_web_status_t eccles_web_vm_back(eccles_web_vm_t *vm);
bool eccles_web_vm_can_go_back(const eccles_web_vm_t *vm);
const char *eccles_web_vm_title(const eccles_web_vm_t *vm);
const char *eccles_web_vm_url(const eccles_web_vm_t *vm);
eccles_web_status_t eccles_web_vm_eval(eccles_web_vm_t *vm, const char *js);
void     eccles_web_vm_set_fuel(eccles_web_vm_t *vm, uint32_t budget);
eccles_web_error_t eccles_web_vm_last_error(const eccles_web_vm_t *vm);
uint32_t eccles_web_vm_last_error_info(const eccles_web_vm_t *vm);
const char *eccles_web_vm_last_error_str(const eccles_web_vm_t *vm);
eccles_web_error_t eccles_web_vm_script_error(const eccles_web_vm_t *vm);
uint32_t eccles_web_vm_script_error_info(const eccles_web_vm_t *vm);
const char *eccles_web_vm_script_error_str(const eccles_web_vm_t *vm);
uint32_t eccles_web_vm_script_error_count(const eccles_web_vm_t *vm);
eccles_web_stats_t eccles_web_vm_get_stats(const eccles_web_vm_t *vm);
eccles_web_status_t eccles_web_vm_set_hal_privilege(eccles_web_vm_t *vm, uint32_t perms);
uint32_t eccles_web_vm_get_privilege(const eccles_web_vm_t *vm);
uint32_t eccles_web_vm_denied_count(const eccles_web_vm_t *vm);
const char *eccles_web_vm_last_denied(const eccles_web_vm_t *vm);
typedef void (*eccles_web_denied_fn)(eccles_web_vm_t *vm, const char *name);
void     eccles_web_vm_set_denied_hook(eccles_web_vm_t *vm, eccles_web_denied_fn hook);  //audit: called for every refused call
eccles_web_status_t eccles_web_vm_register(eccles_web_vm_t *vm, const char *name, eccles_web_host_fn fn);
void     eccles_web_vm_unregister_all(eccles_web_vm_t *vm);

//backends - the ui, the network, the files. Same style as eccles_wasm_hal_t.

//ui backend: THIS is where your ui engine plugs in. Coordinates are viewport px (0,0 = top left of the area you gave to set_viewport), already scrolled,
//the engine only draws what is inside the viewport. colors are 0xRRGGBB. `style` is a mix of ECCLES_WEB_FONT_* bits.
//a ui backend call that fails does not stop the paint, the engine just keeps going
#define ECCLES_WEB_FONT_BOLD      1u
#define ECCLES_WEB_FONT_ITALIC    2u
#define ECCLES_WEB_FONT_UNDERLINE 4u
#define ECCLES_WEB_FONT_MONO      8u
#define ECCLES_WEB_FONT_STRIKE    16u

typedef struct eccles_web_ui_s {
    eccles_web_status_t (*begin)(uint32_t width, uint32_t height);  //start of a frame, clear the area (optional)
    eccles_web_status_t (*end)(void);  //end of a frame, push it to the panel (optional)
    eccles_web_status_t (*fill_rect)(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t rgb);
    //draw `len` bytes of utf-8 with the top left corner of the text at (x,y). px = font size (the line is px * ECCLES_WEB_LINE_HEIGHT_PCT / 100 high)
    eccles_web_status_t (*draw_text)(int32_t x, int32_t y, uint32_t px, uint32_t style, uint32_t rgb, const char *s, uint32_t len);
    //how wide the text would be, MUST agree with draw_text. The engine calls this a lot (once per word) so keep it cheap
    eccles_web_status_t (*text_width)(uint32_t px, uint32_t style, const char *s, uint32_t len, uint32_t *w);
    //natural size of an image when the page did not give width/height (optional, UNSUPPORTED = use 64x64)
    eccles_web_status_t (*image_size)(const char *src, uint32_t *w, uint32_t *h);
    eccles_web_status_t (*draw_image)(const char *src, int32_t x, int32_t y, uint32_t w, uint32_t h);
    eccles_web_status_t (*log)(const uint8_t *buf, uint32_t len);  //console.log / alert output
} eccles_web_ui_t;

//copies every non NULL pointer of ops over the backend ones, later calls stack up and NULL leaves the previous choice alone
//eccles_web_ui_reset() goes back to the backend defaults (the stub / the host recorder)
void eccles_web_ui_override(const eccles_web_ui_t *ops);
void eccles_web_ui_reset(void);

//net backend: how http(s) urls are fetched. open returns a handle (NULL on failure), read gives the body, same contract as the file hooks.
//the engine streams the body, it never has to be in ram. do TLS, redirects and chunked decoding on your side (esp-tls / esp_http_client)
typedef void *(*eccles_web_net_open_fn)(const char *url);  //NULL on failure
typedef int   (*eccles_web_net_read_fn)(void *conn, uint8_t *buf, size_t max);  //bytes, 0 = EOF, <0 error
typedef void  (*eccles_web_net_close_fn)(void *conn);
void eccles_web_set_net(eccles_web_net_open_fn open, eccles_web_net_read_fn read, eccles_web_net_close_fn close);

//filesystem hook: use ANY file system for eccles_web_load(path) and for relative sub resources
typedef void *(*eccles_web_fs_open_fn)(const char *path);  //NULL on failure
typedef int   (*eccles_web_fs_read_fn)(void *file, uint8_t *buf, size_t max);  //bytes, 0 = EOF, <0 error
typedef void  (*eccles_web_fs_close_fn)(void *file);
void eccles_web_set_fs(eccles_web_fs_open_fn open, eccles_web_fs_read_fn read, eccles_web_fs_close_fn close);

//host build simulator (ECCLES_WEB_PLATFORM == HOST), the default ui backend records every call so tests can look at what a page drew
#if ECCLES_WEB_PLATFORM == ECCLES_WEB_PLAT_HOST
typedef struct {
    char     kind;  //'R' rect, 'T' text, 'I' image, 'B' begin, 'E' end
    int32_t  x, y;
    uint32_t w, h, rgb, px, style;
    char     text[96];  //text / image src (cut to fit)
} eccles_web_sim_cmd_t;
void     eccles_web_sim_reset(void);  //forget recorded commands and console output
uint32_t eccles_web_sim_cmd_count(void);
bool     eccles_web_sim_cmd(uint32_t index, eccles_web_sim_cmd_t *out);
uint32_t eccles_web_sim_log_take(char *out, uint32_t max);  //console.log / alert output since the last take (NUL terminated)
void     eccles_web_sim_set_text_metrics(uint32_t num, uint32_t den);  //width of a char = px * num / den (default 11/20)
#endif

#ifdef __cplusplus
}  //extern "C"
#endif

#endif  //ECCLES_WEB_H
