
//eccles_web_config.h config template for eccles_web
//every setting below is commented out, that means the defaults from eccles_web.h are used, uncomment what you want to change
//eccles_web.c includes eccles_web.h too so your app code and the library .c file must see the SAME macros
//this file is picked up automatically (through __has_include) by every file that includes eccles_web.h
//put it next to eccles_web.h/.c or anywhere on the include path. when you dont need to change anything just delete it
#ifndef ECCLES_WEB_CONFIG_H
#define ECCLES_WEB_CONFIG_H

//memory

//your chip's real RAM in bytes (datasheet). Arena = min(RAM/2, 24KB+RAM/4).
// #define ECCLES_WEB_TOTAL_RAM_SIZE    262144ul

//bytes in the arena (dom + styles + boxes + text + script code + script heap + stacks). Overrides the line above.
//the built-in per-platform default is a GUESS - set this for anything you ship, eccles_web_get_stats() tells you what a page really used.
// #define ECCLES_WEB_ARENA_SIZE        (128ul * 1024ul)

//malloc() the arena in eccles_web_init() instead of a static array. (put the arena in PSRAM with eccles_web_init_with())
// #define ECCLES_WEB_USE_HEAP          1

//page budgets. they are carved out of the arena when a page starts and given back when the next one loads,
//nothing is allocated while a page runs, a page that needs more fails with ECCLES_WEB_ERR_LIMIT / NO_MEMORY, never with a crash.
// #define ECCLES_WEB_MAX_NODES         384     /* elements + text nodes, max 65535 */
// #define ECCLES_WEB_MAX_ATTRS         384     /* attributes of all elements */
// #define ECCLES_WEB_TEXT_POOL         (16ul * 1024ul)   /* bytes for all text nodes and attribute values */
// #define ECCLES_WEB_MAX_ATOMS         640     /* interned names: tags, classes, ids, css/js identifiers */
// #define ECCLES_WEB_ATOM_POOL         (6ul * 1024ul)
// #define ECCLES_WEB_MAX_RULES         96      /* css rules (the built-in user agent sheet uses about 40) */
// #define ECCLES_WEB_MAX_DECLS         384     /* css declarations of all rules */
// #define ECCLES_WEB_MAX_DEPTH         32      /* dom nesting, also the layout recursion limit */
// #define ECCLES_WEB_LINE_RUNS         32      /* text runs buffered for one line while wrapping */
// #define ECCLES_WEB_MAX_FLEX_ITEMS    16

//loading

//bytes per refill when streaming a page from a file / network. 64 on tiny parts.
// #define ECCLES_WEB_CHUNK_SIZE        256

//force the file backend: ECCLES_WEB_FS_{NONE,STDIO}. Auto-detected otherwise (stdio on host/ESP). for anything else use eccles_web_set_fs().
// #define ECCLES_WEB_FS                ECCLES_WEB_FS_STDIO

//pages remembered for eccles_web_back(), each one costs ECCLES_WEB_MAX_URL bytes
// #define ECCLES_WEB_MAX_HISTORY       4
// #define ECCLES_WEB_MAX_URL           160

//javascript

//0 compiles the whole script engine out (html + css only, scripts are skipped, ~40% less code)
// #define ECCLES_WEB_JS                1

//garbage collected heap of the script engine, bytecode, stacks
// #define ECCLES_WEB_JS_HEAP_SIZE      (24ul * 1024ul)
// #define ECCLES_WEB_JS_CODE_SIZE      (12ul * 1024ul)   /* bytecode of all functions of the page */
// #define ECCLES_WEB_JS_MAX_FUNCS      64      /* functions of the page (every function expression counts) */
// #define ECCLES_WEB_JS_STACK_SLOTS    256     /* 8 byte value slots */
// #define ECCLES_WEB_JS_CALL_DEPTH     24
// #define ECCLES_WEB_JS_MAX_LOCALS     48      /* variables + parameters of ONE function */
// #define ECCLES_WEB_JS_MAX_TRY        8       /* try blocks active at the same time */
// #define ECCLES_WEB_JS_MAX_ARGS       8       /* arguments passed to a native / host function */
// #define ECCLES_WEB_JS_MAX_ROOTS      48      /* values the builtins keep alive while they allocate */
// #define ECCLES_WEB_JS_MAX_MICROTASKS 48      /* queued promise jobs */
// #define ECCLES_WEB_MAX_FETCH         8       /* fetch() / async host calls waiting for the host at the same time */
// #define ECCLES_WEB_MAX_WASM          2       /* WebAssembly instances per page */

//instruction budget for ONE script / event handler / timer callback (eccles_web_set_fuel). 0 means unlimited.
//with a budget an endless loop in a page traps with ECCLES_WEB_TRAP_JS_FUEL instead of freezing the phone
// #define ECCLES_WEB_JS_FUEL           2000000ul

//every N instructions (power of two) run ECCLES_WEB_YIELD() - feed a watchdog, let the scheduler breathe.
// #define ECCLES_WEB_YIELD_INTERVAL    4096
// #define ECCLES_WEB_YIELD()           vTaskDelay(0)

//timers (setTimeout / setInterval) and event listeners (addEventListener / onclick) alive at the same time
// #define ECCLES_WEB_MAX_TIMERS        8
// #define ECCLES_WEB_MAX_LISTENERS     32

//functions added with eccles_web_register(), and the longest name matched / reported
// #define ECCLES_WEB_MAX_HOST_FUNCS    16
// #define ECCLES_WEB_MAX_NAME          48

//rendering

//default font size in px and the line height as a percentage of the font size
// #define ECCLES_WEB_DEFAULT_FONT_PX   16
// #define ECCLES_WEB_LINE_HEIGHT_PCT   130

//a page is scrolled by dragging, a touch that moves less than this many px is a tap
// #define ECCLES_WEB_TAP_SLOP_PX       10

//skip <input>/<button>/<textarea> editing support (they are still drawn)
// #define ECCLES_WEB_NO_FORMS

//privilege

//what a freshly created instance may do. The built-in default lets a page run (scripts, timers, console, sub resources, links)
//but NOT call functions you registered with eccles_web_register(), those can touch your phone so the host grants them explicitly
//(eccles_web_set_hal_privilege / ECCLES_WEB_PERM_*).
// #define ECCLES_WEB_DEFAULT_PRIVILEGE ECCLES_WEB_ALLOW_PAGE

//hardware backend

//force a platform: ECCLES_WEB_PLAT_{NONE,HOST,ARDUINO,ESP_IDF,PICO,STM32_HAL,ZEPHYR}
// #define ECCLES_WEB_PLATFORM          ECCLES_WEB_PLAT_NONE

//debug

//log load/parse/script failures (never anything on the success path).
// #define ECCLES_WEB_DEBUG
// #define ECCLES_WEB_LOG_LINE(msg)     Serial.println(msg)

#endif  //ECCLES_WEB_CONFIG_H
