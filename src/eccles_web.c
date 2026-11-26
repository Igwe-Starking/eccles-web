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

//eccles_web.c the implementation: names + arena, urls and the streaming reader, html parser, css, layout + painting, the script engine,
//dom bindings + events + timers, the public runtime api and the backends
//in this order: 1 state + arena + atoms, 2 urls + streaming reader + html parser, 3 css, 4 layout + painting + hit testing,
//5 script engine (compiler, vm, garbage collector, builtins), 6 dom bindings + events + timers, 7 public runtime api, 8 backends (ui, net, files, host simulator)
//TODO: split this file into smaller units, it is getting long

#if (defined(__unix__) || defined(__APPLE__)) && !defined(ESP_PLATFORM) && !defined(ARDUINO) \
    && !defined(__ZEPHYR__) && !defined(_POSIX_C_SOURCE)
  #define _POSIX_C_SOURCE 200809L
#endif

#include "eccles_web.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

//debug logging, if you define ECCLES_WEB_DEBUG it prints to the console
//same thing i do in eccles_wasm, you want to know WHY a page looks wrong
#if defined(ECCLES_WEB_DEBUG)
  #ifndef ECCLES_WEB_LOG_LINE
    #define ECCLES_WEB_LOG_LINE(msg) printf("[eccles_web] %s\n", (msg))
  #endif
#else
  #ifndef ECCLES_WEB_LOG_LINE
    #define ECCLES_WEB_LOG_LINE(msg) ((void)0)
  #endif
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define WB_ALIGN8 __attribute__((aligned(8)))
  #define WB_UNLIKELY(x) __builtin_expect(!!(x), 0)
  #define WB_LIKELY(x) __builtin_expect(!!(x), 1)
#else
  #define WB_ALIGN8
  #define WB_UNLIKELY(x) (x)
  #define WB_LIKELY(x) (x)
#endif

typedef eccles_web_status_t wb_st;
#define WB_OK  ECCLES_WEB_SUCCESSFUL
#define WB_BAD ECCLES_WEB_FAILED
#define WB_UNS ECCLES_WEB_UNSUPPORTED

#define WB_LINE_H(px) ((int32_t)((px) * ECCLES_WEB_LINE_HEIGHT_PCT / 100))
#define WB_NOCOL 0x01000000u  //bit 24 set in a color = "none" (transparent background / no border)

//1. state

//every name the engine knows (tags, attributes, css properties and keywords, script keywords and builtin names) is interned ONCE as a 16 bit atom
//the list below is generated from the A_xxx names used in this file, an underscore is a dash in the string (A_font_size = "font-size")
//atoms are what the dom, the style rules and the script engine compare, nobody compares strings in the hot paths
#define WB_ATOM_LIST(X) \
    X(Array, "Array") \
    X(Boolean, "Boolean") \
    X(DOMContentLoaded, "DOMContentLoaded") \
    X(Date, "Date") \
    X(E, "E") \
    X(EPSILON, "EPSILON") \
    X(Error, "Error") \
    X(Infinity, "Infinity") \
    X(JSON, "JSON") \
    X(LN2, "LN2") \
    X(MAX_SAFE_INTEGER, "MAX_SAFE_INTEGER") \
    X(MAX_VALUE, "MAX_VALUE") \
    X(MIN_SAFE_INTEGER, "MIN_SAFE_INTEGER") \
    X(MIN_VALUE, "MIN_VALUE") \
    X(Math, "Math") \
    X(NEGATIVE_INFINITY, "NEGATIVE_INFINITY") \
    X(NaN, "NaN") \
    X(Number, "Number") \
    X(Object, "Object") \
    X(OutOfMemory, "out of memory") \
    X(PI, "PI") \
    X(POSITIVE_INFINITY, "POSITIVE_INFINITY") \
    X(Promise, "Promise") \
    X(RangeError, "RangeError") \
    X(ReferenceError, "ReferenceError") \
    X(SQRT2, "SQRT2") \
    X(String, "String") \
    X(SyntaxError, "SyntaxError") \
    X(TypeError, "TypeError") \
    X(URL, "URL") \
    X(WebAssembly, "WebAssembly") \
    X(a, "a") \
    X(abbr, "abbr") \
    X(abs, "abs") \
    X(acos, "acos") \
    X(activeElement, "activeElement") \
    X(add, "add") \
    X(addEventListener, "addEventListener") \
    X(address, "address") \
    X(alert, "alert") \
    X(align_items, "align-items") \
    X(align_self, "align-self") \
    X(all, "all") \
    X(allSettled, "allSettled") \
    X(alt, "alt") \
    X(append, "append") \
    X(appendChild, "appendChild") \
    X(apply, "apply") \
    X(area, "area") \
    X(arrayBuffer, "arrayBuffer") \
    X(article, "article") \
    X(aside, "aside") \
    X(asin, "asin") \
    X(assign, "assign") \
    X(async, "async") \
    X(at, "at") \
    X(atan, "atan") \
    X(atan2, "atan2") \
    X(auto, "auto") \
    X(await, "await") \
    X(b, "b") \
    X(back, "back") \
    X(background, "background") \
    X(background_color, "background-color") \
    X(base, "base") \
    X(big, "big") \
    X(bind, "bind") \
    X(block, "block") \
    X(blockquote, "blockquote") \
    X(blur, "blur") \
    X(body, "body") \
    X(bodyText, "bodyText") \
    X(boolean, "boolean") \
    X(border, "border") \
    X(border_bottom, "border-bottom") \
    X(border_color, "border-color") \
    X(border_left, "border-left") \
    X(border_right, "border-right") \
    X(border_style, "border-style") \
    X(border_top, "border-top") \
    X(border_width, "border-width") \
    X(bottom, "bottom") \
    X(box_sizing, "box-sizing") \
    X(br, "br") \
    X(break, "break") \
    X(button, "button") \
    X(byteLength, "byteLength") \
    X(call, "call") \
    X(cancelAnimationFrame, "cancelAnimationFrame") \
    X(caption, "caption") \
    X(case, "case") \
    X(catch, "catch") \
    X(cbrt, "cbrt") \
    X(ceil, "ceil") \
    X(center, "center") \
    X(change, "change") \
    X(charAt, "charAt") \
    X(charCodeAt, "charCodeAt") \
    X(checked, "checked") \
    X(childElementCount, "childElementCount") \
    X(childNodes, "childNodes") \
    X(children, "children") \
    X(cite, "cite") \
    X(class, "class") \
    X(classList, "classList") \
    X(className, "className") \
    X(clear, "clear") \
    X(clearInterval, "clearInterval") \
    X(clearTimeout, "clearTimeout") \
    X(click, "click") \
    X(clientHeight, "clientHeight") \
    X(clientWidth, "clientWidth") \
    X(closest, "closest") \
    X(code, "code") \
    X(codePointAt, "codePointAt") \
    X(col, "col") \
    X(color, "color") \
    X(column, "column") \
    X(column_gap, "column-gap") \
    X(column_reverse, "column-reverse") \
    X(complete, "complete") \
    X(concat, "concat") \
    X(confirm, "confirm") \
    X(console, "console") \
    X(const, "const") \
    X(constructor, "constructor") \
    X(contains, "contains") \
    X(contents, "contents") \
    X(continue, "continue") \
    X(cookie, "cookie") \
    X(cos, "cos") \
    X(create, "create") \
    X(createElement, "createElement") \
    X(createTextNode, "createTextNode") \
    X(currentTarget, "currentTarget") \
    X(currentcolor, "currentcolor") \
    X(data, "data") \
    X(datalist, "datalist") \
    X(dataset, "dataset") \
    X(dd, "dd") \
    X(debug, "debug") \
    X(debugger, "debugger") \
    X(decodeURIComponent, "decodeURIComponent") \
    X(default, "default") \
    X(defaultPrevented, "defaultPrevented") \
    X(defaultView, "defaultView") \
    X(defineProperty, "defineProperty") \
    X(del, "del") \
    X(delete, "delete") \
    X(details, "details") \
    X(devicePixelRatio, "devicePixelRatio") \
    X(disabled, "disabled") \
    X(display, "display") \
    X(div, "div") \
    X(dl, "dl") \
    X(do, "do") \
    X(document, "document") \
    X(documentElement, "documentElement") \
    X(documentURI, "documentURI") \
    X(dt, "dt") \
    X(else, "else") \
    X(em, "em") \
    X(embed, "embed") \
    X(encodeURIComponent, "encodeURIComponent") \
    X(end, "end") \
    X(endsWith, "endsWith") \
    X(entries, "entries") \
    X(error, "error") \
    X(event, "event") \
    X(every, "every") \
    X(exp, "exp") \
    X(export, "export") \
    X(exports, "exports") \
    X(extends, "extends") \
    X(false, "false") \
    X(fetch, "fetch") \
    X(fieldset, "fieldset") \
    X(figcaption, "figcaption") \
    X(figure, "figure") \
    X(fill, "fill") \
    X(filter, "filter") \
    X(finally, "finally") \
    X(find, "find") \
    X(findIndex, "findIndex") \
    X(firstChild, "firstChild") \
    X(firstElementChild, "firstElementChild") \
    X(flex, "flex") \
    X(flex_basis, "flex-basis") \
    X(flex_direction, "flex-direction") \
    X(flex_end, "flex-end") \
    X(flex_grow, "flex-grow") \
    X(flex_start, "flex-start") \
    X(floor, "floor") \
    X(flow_root, "flow-root") \
    X(focus, "focus") \
    X(font, "font") \
    X(font_family, "font-family") \
    X(font_size, "font-size") \
    X(font_style, "font-style") \
    X(font_weight, "font-weight") \
    X(footer, "footer") \
    X(for, "for") \
    X(forEach, "forEach") \
    X(form, "form") \
    X(forward, "forward") \
    X(freeze, "freeze") \
    X(from, "from") \
    X(fromCharCode, "fromCharCode") \
    X(fromEntries, "fromEntries") \
    X(fulfilled, "fulfilled") \
    X(function, "function") \
    X(gap, "gap") \
    X(get, "get") \
    X(getAttribute, "getAttribute") \
    X(getBoundingClientRect, "getBoundingClientRect") \
    X(getComputedStyle, "getComputedStyle") \
    X(getDate, "getDate") \
    X(getDay, "getDay") \
    X(getElementById, "getElementById") \
    X(getElementsByClassName, "getElementsByClassName") \
    X(getElementsByTagName, "getElementsByTagName") \
    X(getFullYear, "getFullYear") \
    X(getHours, "getHours") \
    X(getItem, "getItem") \
    X(getMinutes, "getMinutes") \
    X(getMonth, "getMonth") \
    X(getPropertyValue, "getPropertyValue") \
    X(getSeconds, "getSeconds") \
    X(getTime, "getTime") \
    X(globalThis, "globalThis") \
    X(go, "go") \
    X(grid, "grid") \
    X(h1, "h1") \
    X(h2, "h2") \
    X(h3, "h3") \
    X(h4, "h4") \
    X(h5, "h5") \
    X(h6, "h6") \
    X(hasAttribute, "hasAttribute") \
    X(hasOwnProperty, "hasOwnProperty") \
    X(head, "head") \
    X(header, "header") \
    X(headers, "headers") \
    X(height, "height") \
    X(hidden, "hidden") \
    X(history, "history") \
    X(hr, "hr") \
    X(href, "href") \
    X(html, "html") \
    X(hypot, "hypot") \
    X(i, "i") \
    X(id, "id") \
    X(if, "if") \
    X(img, "img") \
    X(import, "import") \
    X(in, "in") \
    X(includes, "includes") \
    X(indexOf, "indexOf") \
    X(info, "info") \
    X(inline, "inline") \
    X(inline_block, "inline-block") \
    X(inline_flex, "inline-flex") \
    X(inline_table, "inline-table") \
    X(innerHTML, "innerHTML") \
    X(innerHeight, "innerHeight") \
    X(innerText, "innerText") \
    X(innerWidth, "innerWidth") \
    X(input, "input") \
    X(ins, "ins") \
    X(insertBefore, "insertBefore") \
    X(instance, "instance") \
    X(instanceof, "instanceof") \
    X(instantiate, "instantiate") \
    X(instantiateStreaming, "instantiateStreaming") \
    X(isArray, "isArray") \
    X(isConnected, "isConnected") \
    X(isFinite, "isFinite") \
    X(isInteger, "isInteger") \
    X(isNaN, "isNaN") \
    X(join, "join") \
    X(json, "json") \
    X(justify_content, "justify-content") \
    X(kbd, "kbd") \
    X(keys, "keys") \
    X(label, "label") \
    X(language, "language") \
    X(lastChild, "lastChild") \
    X(lastElementChild, "lastElementChild") \
    X(lastIndexOf, "lastIndexOf") \
    X(left, "left") \
    X(length, "length") \
    X(let, "let") \
    X(li, "li") \
    X(line_height, "line-height") \
    X(link, "link") \
    X(list_item, "list-item") \
    X(list_style, "list-style") \
    X(list_style_type, "list-style-type") \
    X(load, "load") \
    X(localStorage, "localStorage") \
    X(localeCompare, "localeCompare") \
    X(location, "location") \
    X(log, "log") \
    X(log10, "log10") \
    X(log2, "log2") \
    X(main, "main") \
    X(map, "map") \
    X(margin, "margin") \
    X(margin_bottom, "margin-bottom") \
    X(margin_left, "margin-left") \
    X(margin_right, "margin-right") \
    X(margin_top, "margin-top") \
    X(mark, "mark") \
    X(matches, "matches") \
    X(max, "max") \
    X(max_height, "max-height") \
    X(max_width, "max-width") \
    X(menu, "menu") \
    X(message, "message") \
    X(meta, "meta") \
    X(method, "method") \
    X(min, "min") \
    X(min_height, "min-height") \
    X(min_width, "min-width") \
    X(module, "module") \
    X(name, "name") \
    X(nav, "nav") \
    X(navigator, "navigator") \
    X(new, "new") \
    X(nextElementSibling, "nextElementSibling") \
    X(nextSibling, "nextSibling") \
    X(nodeName, "nodeName") \
    X(nodeType, "nodeType") \
    X(nodeValue, "nodeValue") \
    X(none, "none") \
    X(noscript, "noscript") \
    X(now, "now") \
    X(null, "null") \
    X(number, "number") \
    X(object, "object") \
    X(object_HTMLElement, "[object HTMLElement]") \
    X(object_Object, "[object Object]") \
    X(of, "of") \
    X(offsetHeight, "offsetHeight") \
    X(offsetLeft, "offsetLeft") \
    X(offsetTop, "offsetTop") \
    X(offsetWidth, "offsetWidth") \
    X(ok, "ok") \
    X(ol, "ol") \
    X(onLine, "onLine") \
    X(option, "option") \
    X(outerHTML, "outerHTML") \
    X(outerHeight, "outerHeight") \
    X(outerWidth, "outerWidth") \
    X(p, "p") \
    X(padEnd, "padEnd") \
    X(padStart, "padStart") \
    X(padding, "padding") \
    X(padding_bottom, "padding-bottom") \
    X(padding_left, "padding-left") \
    X(padding_right, "padding-right") \
    X(padding_top, "padding-top") \
    X(pageXOffset, "pageXOffset") \
    X(pageYOffset, "pageYOffset") \
    X(param, "param") \
    X(parent, "parent") \
    X(parentElement, "parentElement") \
    X(parentNode, "parentNode") \
    X(parse, "parse") \
    X(parseFloat, "parseFloat") \
    X(parseInt, "parseInt") \
    X(pathname, "pathname") \
    X(placeholder, "placeholder") \
    X(platform, "platform") \
    X(pop, "pop") \
    X(pow, "pow") \
    X(pre, "pre") \
    X(prepend, "prepend") \
    X(preventDefault, "preventDefault") \
    X(previousElementSibling, "previousElementSibling") \
    X(previousSibling, "previousSibling") \
    X(prompt, "prompt") \
    X(protocol, "protocol") \
    X(prototype, "prototype") \
    X(push, "push") \
    X(pushState, "pushState") \
    X(q, "q") \
    X(querySelector, "querySelector") \
    X(querySelectorAll, "querySelectorAll") \
    X(queueMicrotask, "queueMicrotask") \
    X(race, "race") \
    X(random, "random") \
    X(read, "read") \
    X(readyState, "readyState") \
    X(reason, "reason") \
    X(reduce, "reduce") \
    X(reject, "reject") \
    X(rejected, "rejected") \
    X(rel, "rel") \
    X(reload, "reload") \
    X(remove, "remove") \
    X(removeAttribute, "removeAttribute") \
    X(removeChild, "removeChild") \
    X(removeEventListener, "removeEventListener") \
    X(removeItem, "removeItem") \
    X(removeProperty, "removeProperty") \
    X(repeat, "repeat") \
    X(replace, "replace") \
    X(replaceAll, "replaceAll") \
    X(replaceState, "replaceState") \
    X(requestAnimationFrame, "requestAnimationFrame") \
    X(resolve, "resolve") \
    X(return, "return") \
    X(reverse, "reverse") \
    X(right, "right") \
    X(round, "round") \
    X(row, "row") \
    X(row_gap, "row-gap") \
    X(row_reverse, "row-reverse") \
    X(s, "s") \
    X(samp, "samp") \
    X(script, "script") \
    X(scrollBy, "scrollBy") \
    X(scrollIntoView, "scrollIntoView") \
    X(scrollTo, "scrollTo") \
    X(scrollTop, "scrollTop") \
    X(scrollX, "scrollX") \
    X(scrollY, "scrollY") \
    X(section, "section") \
    X(select, "select") \
    X(selected, "selected") \
    X(self, "self") \
    X(sessionStorage, "sessionStorage") \
    X(set, "set") \
    X(setAttribute, "setAttribute") \
    X(setInterval, "setInterval") \
    X(setItem, "setItem") \
    X(setProperty, "setProperty") \
    X(setTimeout, "setTimeout") \
    X(shift, "shift") \
    X(sign, "sign") \
    X(sin, "sin") \
    X(slice, "slice") \
    X(small, "small") \
    X(some, "some") \
    X(sort, "sort") \
    X(source, "source") \
    X(space, "space") \
    X(space_around, "space-around") \
    X(space_between, "space-between") \
    X(space_evenly, "space-evenly") \
    X(span, "span") \
    X(splice, "splice") \
    X(split, "split") \
    X(sqrt, "sqrt") \
    X(src, "src") \
    X(start, "start") \
    X(startsWith, "startsWith") \
    X(static, "static") \
    X(status, "status") \
    X(stopImmediatePropagation, "stopImmediatePropagation") \
    X(stopPropagation, "stopPropagation") \
    X(strike, "strike") \
    X(string, "string") \
    X(stringify, "stringify") \
    X(strong, "strong") \
    X(style, "style") \
    X(sub, "sub") \
    X(substr, "substr") \
    X(substring, "substring") \
    X(summary, "summary") \
    X(sup, "sup") \
    X(super, "super") \
    X(switch, "switch") \
    X(table, "table") \
    X(table_cell, "table-cell") \
    X(table_row, "table-row") \
    X(tagName, "tagName") \
    X(tan, "tan") \
    X(target, "target") \
    X(tbody, "tbody") \
    X(td, "td") \
    X(template, "template") \
    X(text, "text") \
    X(textContent, "textContent") \
    X(text_align, "text-align") \
    X(text_decoration, "text-decoration") \
    X(text_decoration_line, "text-decoration-line") \
    X(textarea, "textarea") \
    X(tfoot, "tfoot") \
    X(th, "th") \
    X(thead, "thead") \
    X(then, "then") \
    X(this, "this") \
    X(throw, "throw") \
    X(time, "time") \
    X(title, "title") \
    X(toFixed, "toFixed") \
    X(toISOString, "toISOString") \
    X(toLocaleString, "toLocaleString") \
    X(toLowerCase, "toLowerCase") \
    X(toString, "toString") \
    X(toUpperCase, "toUpperCase") \
    X(toggle, "toggle") \
    X(top, "top") \
    X(tr, "tr") \
    X(track, "track") \
    X(trim, "trim") \
    X(trimEnd, "trimEnd") \
    X(trimStart, "trimStart") \
    X(true, "true") \
    X(trunc, "trunc") \
    X(try, "try") \
    X(tt, "tt") \
    X(type, "type") \
    X(typeof, "typeof") \
    X(u, "u") \
    X(ul, "ul") \
    X(undefined, "undefined") \
    X(unshift, "unshift") \
    X(url, "url") \
    X(userAgent, "userAgent") \
    X(value, "value") \
    X(valueOf, "valueOf") \
    X(values, "values") \
    X(var, "var") \
    X(visibility, "visibility") \
    X(void, "void") \
    X(warn, "warn") \
    X(wasmSlot, "wasmSlot") \
    X(wbr, "wbr") \
    X(while, "while") \
    X(white_space, "white-space") \
    X(width, "width") \
    X(window, "window") \
    X(write, "write") \
    X(x, "x") \
    X(xxx, "xxx") \
    X(y, "y") \

enum {
    A_NONE = 0,
#define X(id, str) A_##id,
    WB_ATOM_LIST(X)
#undef X
    A_COUNT
};
static const char *const wb_atom_names[A_COUNT] = {
    "",
#define X(id, str) str,
    WB_ATOM_LIST(X)
#undef X
};

typedef struct { uint32_t off; uint16_t len, next; } wb_atom_t;

enum { WB_N_DOC = 0, WB_N_ELEM = 1, WB_N_TEXT = 2 };

//dom node, 20 bytes. index 0 is the document, so 0 also means "none" in every link
typedef struct {
    uint16_t parent, child, last, next;
    uint16_t tag;  //atom of the element name, 0 for text
    uint16_t attr;  //first attribute (index in attrs[]), 0 = none
    uint32_t text;  //text nodes: offset in the text pool
    uint16_t tlen;
    uint8_t  type, flags;
} wb_node_t;

//attribute, 12 bytes. value is in the text pool, NUL terminated, cap = room for in-place edits
typedef struct { uint16_t name, next; uint32_t off; uint16_t len, cap; } wb_attr_t;

//what layout leaves behind for every node. This is ALL that is kept of a laid out page (no render tree, no display list)
//element boxes are in page coordinates (not scrolled). inline elements only use the style fields, text nodes only use gy/gh
//gy/gh = where the inline group that STARTS at this node begins and how tall it is (painting wraps the text again, it needs the start)
typedef struct {
    int32_t  y; uint32_t h;
    int32_t  gy; uint32_t gh;
    int16_t  x; uint16_t w;
    int16_t  gx; uint16_t gw;
    uint32_t bg, fg, bc;  //0xRRGGBB, bit 24 = none
    uint8_t  px, fl, align, disp;
    uint8_t  bw[4];  //border widths t r b l
    uint16_t lh, lhp;  //line height px, or percent of the font size when lh is 0 (0 = default)
    int16_t  ml, mr;  //horizontal margins (only kept for inline-block / replaced elements, the line breaker needs them)
    uint8_t  vis, res, pad0, pad1;  //hidden, style stored in this layout pass
} wb_box_t;

enum { WB_D_INLINE = 0, WB_D_BLOCK, WB_D_NONE, WB_D_FLEX, WB_D_INLINEBLOCK, WB_D_LISTITEM };
//fl bits: the ui font bits (1 bold, 2 italic, 4 underline, 8 mono, 16 strike) + these
#define WB_FL_PRE   32u
#define WB_FL_NOWRAP 64u
#define WB_FL_UI_MASK 31u

//css selector: up to ECCLES_WEB_SEL_DEPTH compounds (right to left), each tag/id/2 classes. descendant and child combinators only
#define WB_SEL_DEPTH 3
typedef struct { uint16_t tag, id, cls[2]; uint8_t flags; } wb_comp_t;  //flags: 1 first-child, 2 last-child
#define WB_CF_FIRST 1
#define WB_CF_LAST  2
typedef struct {
    wb_comp_t c[WB_SEL_DEPTH];
    uint8_t   n;  //compounds used
    uint8_t   combs;  //2 bits per link: c[i] to c[i+1] is 1 descendant / 2 child
    uint8_t   ndecl;  //255 max, longer rules are cut
    uint8_t   author;  //0 built-in sheet, 1 page sheet
    uint32_t  spec;
    uint16_t  decl0;
} wb_rule_t;

//css declaration: property id + encoded value (see the css section)
typedef struct { uint8_t prop, imp; uint16_t pad; uint32_t val; } wb_decl_t;

//streaming reader, same idea as eccles_wasm: a memory buffer is read in place, anything else comes through a small chunk
typedef struct {
    eccles_web_read_fn fn;
    void *ctx;
    const uint8_t *p, *e;
    uint8_t  mem_src, eof;
    uint8_t  chunk[ECCLES_WEB_CHUNK_SIZE];
} wb_reader_t;

//an opened url (file or network)
typedef struct {
    void *h;
    uint8_t kind;  //1 stdio file, 2 fs hook, 3 net hook
} wb_src_t;

#if ECCLES_WEB_JS
//script values are 8 bytes (nan-boxed): a double is itself, everything else lives in the nan space (top 16 bits = tag)
typedef uint64_t wv;
typedef struct { uint32_t code; uint16_t nslots, nparams, name; uint8_t flags, pad; } wb_proto_t;  //code = offset in the code pool
typedef struct { const uint8_t *pc; uint32_t env; wv thisv; wv prom; uint32_t base; uint16_t proto; uint8_t ctor, boundary, is_async; } wb_jframe_t;  //boundary: a native called into script here, the run loop stops when this frame ends
typedef struct { const uint8_t *pc; uint32_t sp, env; uint8_t frame; } wb_jtry_t;
typedef struct { uint16_t node, type; wv fn; } wb_listener_t;
typedef struct { uint32_t due, interval; wv fn; uint16_t id; uint8_t used, repeat, h_raf, pad; } wb_timer_t;
#endif

//one complete, independent instance. Lives at the start of its own arena.
struct eccles_web_vm_s {
    //arena
    uint8_t *base, *end, *lo, *hi, *hi_init;
    uint8_t  inited, loaded, running, in_page;
    void    *heap_block;  //non-NULL: free() this on destroy
    void    *user;
    uint32_t perms;
    uint32_t denied; const char *last_denied; eccles_web_denied_fn denied_hook;
    //diagnostics
    eccles_web_error_t err; uint32_t err_info; char err_str[ECCLES_WEB_MAX_NAME + 1];
    eccles_web_error_t jserr; uint32_t jserr_info; char jserr_str[ECCLES_WEB_MAX_NAME + 1]; uint32_t jserr_count;
    //viewport + view state
    uint32_t vw, vh; int32_t scroll_y; uint32_t page_h;
    uint8_t  dirty, laid_out, in_layout;
    int32_t  touch_x0, touch_y0, touch_ly; uint8_t touching, touch_moved;
    uint16_t focus;  //focused form control (node), 0 none
    //page tables (carved out of the arena when a page starts)
    wb_node_t *nodes; uint16_t nnodes;
    wb_attr_t *attrs; uint16_t nattrs;
    wb_box_t  *boxes;
    uint8_t   *text; uint32_t text_len;
    wb_atom_t *atoms; uint16_t *abucket; uint8_t *apool; uint16_t natoms; uint32_t apool_len;
    wb_rule_t *rules; uint16_t nrules;
    wb_decl_t *decls; uint16_t ndecls;
    uint16_t title_node, body_node, free_node, free_attr;  //free_node / free_attr: lists of recycled dom nodes and attributes
    uint32_t budget;  //bytes the page tables took
    //urls
    char url[ECCLES_WEB_MAX_URL];
    char hist[ECCLES_WEB_MAX_HISTORY][ECCLES_WEB_MAX_URL]; uint8_t nhist;
    //host functions (per instance)
    struct { const char *name; eccles_web_host_fn fn; eccles_web_async_fn afn; } host[ECCLES_WEB_MAX_HOST_FUNCS]; uint8_t nhost;
    uint8_t  css_depth;
    char     nav_url[ECCLES_WEB_MAX_URL]; uint8_t nav_pending, nav_back;  //a page asked to go somewhere: done when the call that ran its script returns
#if ECCLES_WEB_JS
    uint8_t  *jheap; uint32_t jheap_size, jfree, jalloc_since, jgc_runs;
    uint8_t  *jcode; uint32_t jcode_len;
    wb_proto_t *protos; uint16_t nprotos;
    wv       *jstk; uint32_t jsp;
    wb_jframe_t *jframes; uint32_t jnf;
    wb_jtry_t jtry[ECCLES_WEB_JS_MAX_TRY]; uint8_t ntry;
    wv       jglobal, jexc;
    uint8_t  jexc_pending, jnative_depth, jscripting;
    uint32_t fuel_budget, fuel_left, jops;
    wb_listener_t *listeners; uint16_t nlisteners;
    wb_timer_t timers[ECCLES_WEB_MAX_TIMERS]; uint16_t timer_seq;
    wv       jroots[ECCLES_WEB_JS_MAX_ROOTS]; uint8_t njroots;
    uint8_t  event_stop, event_prevent, jtrap, new_call;
    uint32_t last_tick;
    wv       jbd1;  //second data word of the bound native being called
    struct { wv a, b, c; uint8_t kind, flag; } mt[ECCLES_WEB_JS_MAX_MICROTASKS]; uint8_t mt_head, mt_len, mt_draining;
    struct { uint32_t id; wv prom; uint8_t kind; } fetches[ECCLES_WEB_MAX_FETCH]; uint32_t fetch_seq;  //pending requests (fetch + async host calls), kind 1 = fetch
    struct { void *inst; wv imports; uint8_t used; } wasm[ECCLES_WEB_MAX_WASM];
    uint8_t  clock_valid;
#endif
};

#if ECCLES_WEB_USE_HEAP
#else
static uint8_t g_arena[ECCLES_WEB_ARENA_SIZE] WB_ALIGN8;
#endif

//chip-level state shared by every instance (set once at start-up)
static eccles_web_ui_t g_ui;  //the active ui backend (defaults + overrides)
static eccles_web_fs_open_fn  g_fs_open;
static eccles_web_fs_read_fn  g_fs_read;
static eccles_web_fs_close_fn g_fs_close;
static eccles_web_net_open_fn  g_net_open;
static eccles_web_net_read_fn  g_net_read;
static eccles_web_net_close_fn g_net_close;
static eccles_web_vm_t *g_default_vm;

//records the FIRST error only, later errors are ignored because they are usually just a result of the first one
static void wb_fail(eccles_web_vm_t *vm, eccles_web_error_t e, uint32_t info) {
    if (vm->err == ECCLES_WEB_OK) { vm->err = e; vm->err_info = info; }
}
static void wb_fail_str(eccles_web_vm_t *vm, eccles_web_error_t e, uint32_t info, const char *s) {
    if (vm->err == ECCLES_WEB_OK) {
        size_t n = strlen(s); if (n > ECCLES_WEB_MAX_NAME) n = ECCLES_WEB_MAX_NAME;
        vm->err = e; vm->err_info = info; memcpy(vm->err_str, s, n); vm->err_str[n] = 0;
    }
}

//arena: bump from both ends, same as eccles_wasm
//low allocator, grows up from the start of the arena. the page tables live here
//the overflow check (p + size < p) is for 32bit targets where the pointer can wrap around
//nothing is freed one by one, the whole page is dropped at once by wb_page_reset()
static void *wb_alo(eccles_web_vm_t *vm, size_t size, size_t align) {
    uintptr_t p = ((uintptr_t)vm->lo + (align - 1)) & ~(uintptr_t)(align - 1);
    if (vm->err != ECCLES_WEB_OK) return NULL;
    if (p + size > (uintptr_t)vm->hi || p + size < p) {
        wb_fail(vm, ECCLES_WEB_ERR_NO_MEMORY, (uint32_t)(p + size - (uintptr_t)vm->lo)); return NULL;
    }
    vm->lo = (uint8_t *)(p + size);
    return (void *)p;
}
//high allocator, grows down from the end of the arena. the script heap + stacks live here (and scratch space while a script is read)
static void *wb_ahi(eccles_web_vm_t *vm, size_t size, size_t align) {
    uintptr_t p;
    if (vm->err != ECCLES_WEB_OK) return NULL;
    if (size > (size_t)(vm->hi - vm->lo)) { wb_fail(vm, ECCLES_WEB_ERR_NO_MEMORY, (uint32_t)size); return NULL; }
    p = ((uintptr_t)vm->hi - size) & ~(uintptr_t)(align - 1);
    if (p < (uintptr_t)vm->lo) { wb_fail(vm, ECCLES_WEB_ERR_NO_MEMORY, (uint32_t)size); return NULL; }
    vm->hi = (uint8_t *)p;
    return (void *)p;
}

//atoms

#if (ECCLES_WEB_MAX_ATOMS) <= 1024
  #define WB_ABUCKETS 1024
#else
  #define WB_ABUCKETS 4096
#endif

static const char *wb_astr(const eccles_web_vm_t *vm, uint16_t a);
static uint32_t wb_hash(const char *s, uint32_t n) {
    uint32_t h = 2166136261u;
    while (n--) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h ^ (h >> 15);
}

//finds an atom without creating it (0 = not known). css uses this so a page cant fill the pool with property names it makes up
static uint16_t wb_atom_find(const eccles_web_vm_t *vm, const char *s, uint32_t n) {
    uint16_t i = vm->abucket[wb_hash(s, n) & (WB_ABUCKETS - 1)];
    while (i) {
        const wb_atom_t *a = &vm->atoms[i];
        if (a->len == n && memcmp(wb_astr(vm, i), s, n) == 0) return i;
        i = a->next;
    }
    return 0;
}
static uint16_t wb_atom(eccles_web_vm_t *vm, const char *s, uint32_t n) {
    uint16_t i = wb_atom_find(vm, s, n); uint32_t b;
    wb_atom_t *a;
    if (i) return i;
    if (vm->natoms >= ECCLES_WEB_MAX_ATOMS || n > 0xFFFFu || vm->apool_len + n + 1 > ECCLES_WEB_ATOM_POOL) {
        wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 4); return 0;  //4 = atoms
    }
    a = &vm->atoms[vm->natoms]; b = wb_hash(s, n) & (WB_ABUCKETS - 1);
    a->off = vm->apool_len; a->len = (uint16_t)n; a->next = vm->abucket[b];
    memcpy(vm->apool + vm->apool_len, s, n); vm->apool[vm->apool_len + n] = 0; vm->apool_len += n + 1;
    vm->abucket[b] = vm->natoms;
    return vm->natoms++;
}
//builtin atoms (id < A_COUNT) point at the static name table in flash, only names a page makes up (classes, ids, identifiers) use the pool
static const char *wb_astr(const eccles_web_vm_t *vm, uint16_t a) { return a < A_COUNT ? wb_atom_names[a] : (const char *)vm->apool + vm->atoms[a].off; }
static uint32_t wb_alen(const eccles_web_vm_t *vm, uint16_t a) { return vm->atoms[a].len; }

//text pool: text nodes and attribute values, every string NUL terminated. offset 0 is the empty string
//the pool only grows while strings are replaced (innerHTML, setAttribute) so when it is full the live strings are slid together and the dead ones disappear
typedef struct { uint32_t off, len; uint16_t idx; uint8_t kind; } wb_pe_t;
static int wb_pool_compact(eccles_web_vm_t *vm) {
    uint32_t cap, n = 0, i, max, gap, dst = 1; wb_pe_t *e = (wb_pe_t *)((uintptr_t)(vm->lo + 7) & ~(uintptr_t)7);
    uint16_t k, at;
    if (!vm->text) return 0;
    cap = (uint32_t)(vm->hi - (uint8_t *)e); max = cap / (uint32_t)sizeof(wb_pe_t);
    for (k = 1; k < vm->nnodes; k++) {
        if (vm->nodes[k].type == 3) continue;  //(recycled node)
        if (vm->nodes[k].type == WB_N_TEXT && vm->nodes[k].text) { if (n >= max) return 0; e[n].off = vm->nodes[k].text; e[n].len = (uint32_t)vm->nodes[k].tlen + 1; e[n].idx = k; e[n].kind = 0; n++; }
        for (at = vm->nodes[k].attr; at; at = vm->attrs[at].next) { if (!vm->attrs[at].off) continue; if (n >= max) return 0; e[n].off = vm->attrs[at].off; e[n].len = (uint32_t)vm->attrs[at].cap + 1; e[n].idx = at; e[n].kind = 1; n++; }
    }
    for (gap = n / 2; gap > 0; gap /= 2)  //shell sort by offset
        for (i = gap; i < n; i++) { wb_pe_t t = e[i]; uint32_t j = i; while (j >= gap && e[j - gap].off > t.off) { e[j] = e[j - gap]; j -= gap; } e[j] = t; }
    for (i = 0; i < n; i++) {
        if (e[i].off != dst) memmove(vm->text + dst, vm->text + e[i].off, e[i].len);
        if (e[i].kind == 0) vm->nodes[e[i].idx].text = dst; else vm->attrs[e[i].idx].off = dst;
        dst += e[i].len;
    }
    if (dst >= vm->text_len) return 0;  //nothing was gained
    vm->text_len = dst;
    return 1;
}
static uint32_t wb_pool_alloc(eccles_web_vm_t *vm, uint32_t n) {  //n = bytes WITHOUT the NUL
    uint32_t off;
    if (vm->text_len + n + 1 > ECCLES_WEB_TEXT_POOL) wb_pool_compact(vm);
    if (vm->text_len + n + 1 > ECCLES_WEB_TEXT_POOL) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 2); return 0xFFFFFFFFu; }  //2 = text pool
    off = vm->text_len; vm->text_len += n + 1; vm->text[off + n] = 0;
    return off;
}

//attribute entries are recycled too
static uint16_t wb_attr_alloc(eccles_web_vm_t *vm) {
    uint16_t i;
    if (vm->free_attr) { i = vm->free_attr; vm->free_attr = vm->attrs[i].next; return i; }
    if (vm->nattrs >= ECCLES_WEB_MAX_ATTRS) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 3); return 0; }  //3 = attrs
    return vm->nattrs++;
}
static void wb_attr_free(eccles_web_vm_t *vm, uint16_t i) { vm->attrs[i].off = 0; vm->attrs[i].len = vm->attrs[i].cap = 0; vm->attrs[i].next = vm->free_attr; vm->free_attr = i; }

//dom

//raw node allocation: recycled nodes first. when the table is full a collection runs once (it recycles detached nodes nobody refers to any more)
#define WB_N_FREE 3
static void wb_gc_for_nodes(eccles_web_vm_t *vm);  //script section
static wb_node_t *wb_node_alloc(eccles_web_vm_t *vm, uint8_t type, uint16_t tag) {
    wb_node_t *n; uint16_t idx;
    if (!vm->free_node && vm->nnodes >= ECCLES_WEB_MAX_NODES) wb_gc_for_nodes(vm);
    if (vm->free_node) { idx = vm->free_node; vm->free_node = vm->nodes[idx].next; }
    else if (vm->nnodes < ECCLES_WEB_MAX_NODES) idx = vm->nnodes++;
    else { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 1); return NULL; }  //1 = nodes
    n = &vm->nodes[idx]; memset(n, 0, sizeof *n);
    n->type = type; n->tag = tag;
    vm->dirty = 1;
    return n;
}
static wb_node_t *wb_new_node(eccles_web_vm_t *vm, uint8_t type, uint16_t tag, uint16_t parent) {
    wb_node_t *n = wb_node_alloc(vm, type, tag), *p; uint16_t idx;
    if (!n) return NULL;
    idx = (uint16_t)(n - vm->nodes);
    n->parent = parent;
    p = &vm->nodes[parent];
    if (p->last) vm->nodes[p->last].next = idx; else p->child = idx;
    p->last = idx;
    return n;
}
static uint16_t wb_idx(const eccles_web_vm_t *vm, const wb_node_t *n) { return (uint16_t)(n - vm->nodes); }

static wb_attr_t *wb_attr_find(const eccles_web_vm_t *vm, uint16_t node, uint16_t name) {
    uint16_t a = vm->nodes[node].attr;
    while (a) { if (vm->attrs[a].name == name) return &vm->attrs[a]; a = vm->attrs[a].next; }
    return NULL;
}
static const char *wb_attr_get(const eccles_web_vm_t *vm, uint16_t node, uint16_t name, uint32_t *len) {
    wb_attr_t *a = wb_attr_find(vm, node, name);
    if (!a) { if (len) *len = 0; return NULL; }
    if (len) *len = a->len;
    return (const char *)vm->text + a->off;
}
//sets an attribute, in place when the new value fits the old room (typing in an input, toggling a class), else a new pool string
//cap_hint: room to reserve for edits (an <input> value gets more)
static int wb_attr_set(eccles_web_vm_t *vm, uint16_t node, uint16_t name, const char *s, uint32_t n, uint32_t cap_hint) {
    wb_attr_t *a = wb_attr_find(vm, node, name); uint32_t cap = n > cap_hint ? n : cap_hint, off;
    if (a && a->cap >= n) { memmove(vm->text + a->off, s, n); vm->text[a->off + n] = 0; a->len = (uint16_t)n; vm->dirty = 1; return 1; }
    if (cap > 0xFFFFu) cap = n;
    off = wb_pool_alloc(vm, cap); if (off == 0xFFFFFFFFu) return 0;
    memmove(vm->text + off, s, n); vm->text[off + n] = 0;
    if (a) { a->off = off; a->len = (uint16_t)n; a->cap = (uint16_t)cap; vm->dirty = 1; return 1; }
    { uint16_t ai = wb_attr_alloc(vm); if (!ai) return 0;
      a = &vm->attrs[ai]; a->name = name; a->off = off; a->len = (uint16_t)n; a->cap = (uint16_t)cap;
      a->next = vm->nodes[node].attr; vm->nodes[node].attr = ai; }
    vm->dirty = 1;
    return 1;
}
static void wb_attr_remove(eccles_web_vm_t *vm, uint16_t node, uint16_t name) {
    uint16_t *pa = &vm->nodes[node].attr;
    while (*pa) { if (vm->attrs[*pa].name == name) { uint16_t dead = *pa; *pa = vm->attrs[dead].next; wb_attr_free(vm, dead); vm->dirty = 1; return; } pa = &vm->attrs[*pa].next; }
}

//does a class list ("a b c") contain this class
static int wb_has_class(const eccles_web_vm_t *vm, uint16_t node, uint16_t cls) {
    uint32_t n, cl = wb_alen(vm, cls); const char *s = wb_attr_get(vm, node, A_class, &n), *c = wb_astr(vm, cls);
    uint32_t i = 0;
    if (!s) return 0;
    while (i < n) {
        uint32_t j;
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
        j = i; while (j < n && s[j] != ' ' && s[j] != '\t' && s[j] != '\n') j++;
        if (j - i == cl && memcmp(s + i, c, cl) == 0) return 1;
        i = j;
    }
    return 0;
}

//what a node's text is. never NULL
static const char *wb_text_of(const eccles_web_vm_t *vm, const wb_node_t *n) { return (const char *)vm->text + n->text; }

//the page: everything a page needs is carved out of the arena in one go when it starts, nothing is allocated while it runs
//a page that needs more than the arena has fails with ECCLES_WEB_ERR_NO_MEMORY and info = the bytes it needed
static void wb_page_reset(eccles_web_vm_t *vm) {
    vm->lo = vm->base; vm->hi = vm->hi_init;
    vm->nodes = NULL; vm->nnodes = 0; vm->attrs = NULL; vm->nattrs = 0; vm->boxes = NULL;
    vm->text = NULL; vm->text_len = 0; vm->atoms = NULL; vm->abucket = NULL; vm->apool = NULL; vm->natoms = 0; vm->apool_len = 0;
    vm->rules = NULL; vm->nrules = 0; vm->decls = NULL; vm->ndecls = 0; vm->title_node = vm->body_node = vm->free_node = vm->free_attr = 0;
    vm->loaded = 0; vm->in_page = 0; vm->laid_out = 0; vm->dirty = 0; vm->scroll_y = 0; vm->page_h = 0; vm->focus = 0;
    vm->touching = 0; vm->budget = 0; vm->css_depth = 0;
    vm->jserr = ECCLES_WEB_OK; vm->jserr_info = 0; vm->jserr_str[0] = 0; vm->jserr_count = 0;
#if ECCLES_WEB_JS
    vm->jheap = NULL; vm->jcode = NULL; vm->jcode_len = 0; vm->protos = NULL; vm->nprotos = 0; vm->jstk = NULL; vm->jframes = NULL;
    vm->jsp = vm->jnf = 0; vm->ntry = 0; vm->jexc_pending = 0; vm->jnative_depth = 0; vm->jscripting = 0; vm->listeners = NULL; vm->nlisteners = 0;
    vm->njroots = 0; vm->jops = 0; vm->jgc_runs = 0; vm->jalloc_since = 0;
    memset(vm->timers, 0, sizeof vm->timers); vm->timer_seq = 0;
#endif
}

static void wb_log(eccles_web_vm_t *vm, const char *s, uint32_t n);  //backends section
static void wb_style_init_builtin(eccles_web_vm_t *vm);  //css section: parses the built-in user agent sheet
#if ECCLES_WEB_JS
static void wb_js_page_init(eccles_web_vm_t *vm);  //script section: globals, builtins
#endif

static int wb_page_begin(eccles_web_vm_t *vm) {
    size_t need = 0; size_t room;
    int i;
    wb_page_reset(vm);
    need += ECCLES_WEB_MAX_NODES * sizeof(wb_node_t) + (ECCLES_WEB_MAX_ATTRS + 1) * sizeof(wb_attr_t) + ECCLES_WEB_MAX_NODES * sizeof(wb_box_t);
    need += ECCLES_WEB_TEXT_POOL + ECCLES_WEB_MAX_ATOMS * sizeof(wb_atom_t) + WB_ABUCKETS * sizeof(uint16_t) + ECCLES_WEB_ATOM_POOL;
    need += ECCLES_WEB_MAX_RULES * sizeof(wb_rule_t) + ECCLES_WEB_MAX_DECLS * sizeof(wb_decl_t);
#if ECCLES_WEB_JS
    need += ECCLES_WEB_JS_HEAP_SIZE + ECCLES_WEB_JS_CODE_SIZE + ECCLES_WEB_JS_MAX_FUNCS * sizeof(wb_proto_t)
          + ECCLES_WEB_JS_STACK_SLOTS * sizeof(wv) + ECCLES_WEB_JS_CALL_DEPTH * sizeof(wb_jframe_t) + ECCLES_WEB_MAX_LISTENERS * sizeof(wb_listener_t);
#endif
    need += 256;  //alignment slack
    room = (size_t)(vm->hi - vm->lo);
    if (need > room) { wb_fail(vm, ECCLES_WEB_ERR_NO_MEMORY, (uint32_t)need); return 0; }
    vm->nodes = (wb_node_t *)wb_alo(vm, ECCLES_WEB_MAX_NODES * sizeof(wb_node_t), 8);
    vm->attrs = (wb_attr_t *)wb_alo(vm, (ECCLES_WEB_MAX_ATTRS + 1) * sizeof(wb_attr_t), 4);
    vm->boxes = (wb_box_t *)wb_alo(vm, ECCLES_WEB_MAX_NODES * sizeof(wb_box_t), 8);
    vm->text = (uint8_t *)wb_alo(vm, ECCLES_WEB_TEXT_POOL, 4);
    vm->atoms = (wb_atom_t *)wb_alo(vm, ECCLES_WEB_MAX_ATOMS * sizeof(wb_atom_t), 4);
    vm->abucket = (uint16_t *)wb_alo(vm, WB_ABUCKETS * sizeof(uint16_t), 2);
    vm->apool = (uint8_t *)wb_alo(vm, ECCLES_WEB_ATOM_POOL, 4);
    vm->rules = (wb_rule_t *)wb_alo(vm, ECCLES_WEB_MAX_RULES * sizeof(wb_rule_t), 4);
    vm->decls = (wb_decl_t *)wb_alo(vm, ECCLES_WEB_MAX_DECLS * sizeof(wb_decl_t), 4);
#if ECCLES_WEB_JS
    vm->jcode = (uint8_t *)wb_alo(vm, ECCLES_WEB_JS_CODE_SIZE, 4);
    vm->protos = (wb_proto_t *)wb_alo(vm, ECCLES_WEB_JS_MAX_FUNCS * sizeof(wb_proto_t), 4);
    vm->listeners = (wb_listener_t *)wb_alo(vm, ECCLES_WEB_MAX_LISTENERS * sizeof(wb_listener_t), 8);
    vm->jheap = (uint8_t *)wb_ahi(vm, ECCLES_WEB_JS_HEAP_SIZE, 8);
    vm->jstk = (wv *)wb_ahi(vm, ECCLES_WEB_JS_STACK_SLOTS * sizeof(wv), 8);
    vm->jframes = (wb_jframe_t *)wb_ahi(vm, ECCLES_WEB_JS_CALL_DEPTH * sizeof(wb_jframe_t), 8);
    vm->jheap_size = ECCLES_WEB_JS_HEAP_SIZE;
#endif
    if (vm->err) return 0;
    vm->budget = (uint32_t)((vm->lo - vm->base) + (vm->hi_init - vm->hi));
    //document node, reserved attribute 0, the empty string at text offset 0, the built-in atoms
    memset(&vm->nodes[0], 0, sizeof vm->nodes[0]); vm->nnodes = 1; vm->nodes[0].type = WB_N_DOC;
    memset(&vm->attrs[0], 0, sizeof vm->attrs[0]); vm->nattrs = 1;
    vm->text[0] = 0; vm->text_len = 1;
    memset(vm->abucket, 0, WB_ABUCKETS * sizeof(uint16_t));
    memset(&vm->atoms[0], 0, sizeof vm->atoms[0]); vm->natoms = 1; vm->apool_len = 0;
    if (A_COUNT >= ECCLES_WEB_MAX_ATOMS) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 4); return 0; }
    for (i = 1; i < A_COUNT; i++) {  //the builtin names: only the table entry + hash chain, the text stays in flash
        wb_atom_t *a = &vm->atoms[i]; uint32_t len = (uint32_t)strlen(wb_atom_names[i]), b = wb_hash(wb_atom_names[i], len) & (WB_ABUCKETS - 1);
        a->off = 0; a->len = (uint16_t)len; a->next = vm->abucket[b]; vm->abucket[b] = (uint16_t)i;
    }
    vm->natoms = A_COUNT;
    vm->in_page = 1;
    wb_style_init_builtin(vm);
#if ECCLES_WEB_JS
    wb_js_page_init(vm);
#endif
    return vm->err == ECCLES_WEB_OK;
}

static int isalnum_c(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

//number of code points in a utf-8 string
static uint32_t wb_utf8_len(const char *s, uint32_t n) { uint32_t c = 0, i; for (i = 0; i < n; i++) if (((uint8_t)s[i] & 0xC0) != 0x80) c++; return c; }

#define WB_INPUT_CAP 64  //room reserved for typing into an <input> (bytes)

//2. urls + streaming reader + html parser

//refused page calls: not an error for the page, the counter goes up and the owner can watch (eccles_web_vm_set_denied_hook)
static void wb_deny(eccles_web_vm_t *vm, const char *name) {
    vm->denied++; vm->last_denied = name;
    if (vm->denied_hook) vm->denied_hook(vm, name);
}

//kind of an url: 1 = file system, 2 = network, 0 = something we dont know (data:, mailto:, ...)
static int wb_url_kind(const char *u) {
    const char *p = u;
    if (!strncmp(u, "http://", 7) || !strncmp(u, "https://", 8)) return 2;
    if (!strncmp(u, "file://", 7)) return 1;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) p++;
    if (*p == ':' && p - u > 1) return 0;  //a scheme (a one letter "scheme" is a windows drive)
    return 1;
}

//removes ./ and ../ from a path (in place). a leading slash stays, a trailing slash stays
static void wb_path_norm(char *p) {
    char *w = p, *r = p; int abs = (*p == '/');
    if (abs) { w++; r++; }
    while (*r) {
        char *s = r; size_t n; int slash;
        while (*r && *r != '/') r++;
        n = (size_t)(r - s); slash = (*r == '/'); if (slash) r++;
        if (n == 0 || (n == 1 && s[0] == '.')) continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {  //drop the segment before
            char *b = w;
            if (b > p + abs) { b--; while (b > p + abs && b[-1] != '/') b--; w = b; }
            continue;
        }
        memmove(w, s, n); w += n; if (slash) *w++ = '/';
    }
    *w = 0;
}

//resolve rel against base into out. handles absolute urls, //host, /root, #frag, ?query, relative paths with ./ and ../
static void wb_url_resolve(const char *base, const char *rel, char *out, size_t cap) {
    size_t blen = base ? strlen(base) : 0, pstart = 0, k, e;
    const char *q; char tmp[ECCLES_WEB_MAX_URL + 16], tail[ECCLES_WEB_MAX_URL + 16]; char *cut;
    if (!cap) return;
    if (!base) base = "";
    while (*rel == ' ') rel++;
    for (q = rel; (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'); q++) {}
    if (*q == ':' && q - rel > 1) { size_t rl = strlen(rel); if (rl >= cap) rl = cap - 1; memcpy(out, rel, rl); out[rl] = 0; return; }  //has a scheme: already absolute
    if (!strncmp(base, "http://", 7) || !strncmp(base, "https://", 8) || !strncmp(base, "file://", 7)) {
        pstart = (size_t)(strstr(base, "://") - base) + 3;  //then skip the host
        while (pstart < blen && base[pstart] != '/') pstart++;
    }
    if (rel[0] == '/' && rel[1] == '/') {
        const char *sc = strstr(base, "://"); size_t sl = sc ? (size_t)(sc - base) + 1 : 0;
        snprintf(tmp, sizeof tmp, "%.*s%s", (int)sl, base, rel);
    } else if (rel[0] == '/') snprintf(tmp, sizeof tmp, "%.*s%s", (int)pstart, base, rel);
    else if (rel[0] == '#' || rel[0] == 0) {
        e = blen; for (k = 0; k < blen; k++) if (base[k] == '#') { e = k; break; }
        snprintf(tmp, sizeof tmp, "%.*s%s", (int)e, base, rel);
    } else if (rel[0] == '?') {
        e = blen; for (k = 0; k < blen; k++) if (base[k] == '?' || base[k] == '#') { e = k; break; }
        snprintf(tmp, sizeof tmp, "%.*s%s", (int)e, base, rel);
    } else {
        e = blen; for (k = pstart; k < blen; k++) if (base[k] == '?' || base[k] == '#') { e = k; break; }
        while (e > pstart && base[e - 1] != '/') e--;  //directory of the base path
        if (e == pstart && pstart > 0) snprintf(tmp, sizeof tmp, "%.*s/%s", (int)pstart, base, rel);
        else snprintf(tmp, sizeof tmp, "%.*s%s", (int)e, base, rel);
    }
    //normalise the path part, keep ?query#fragment as it is
    cut = tmp + pstart; while (*cut && *cut != '?' && *cut != '#') cut++;
    snprintf(tail, sizeof tail, "%s", cut); *cut = 0;
    wb_path_norm(tmp + pstart);
    snprintf(out, cap, "%.*s%s", (int)(cap > 1 ? cap - 1 : 0), tmp, "");
    { size_t used = strlen(out), tl = strlen(tail); if (used + tl + 1 > cap) tl = cap > used + 1 ? cap - used - 1 : 0; memcpy(out + used, tail, tl); out[used + tl] = 0; }
}

//opens an url for reading. file urls go through the filesystem hook / stdio, http(s) through the net backend
static int wb_open(eccles_web_vm_t *vm, const char *url, wb_src_t *s, int sub) {
    int k = wb_url_kind(url);
    memset(s, 0, sizeof *s);
    if (k == 0) { wb_fail(vm, ECCLES_WEB_ERR_BAD_URL, 0); return 0; }
    if (k == 2) {
        if (sub && !(vm->perms & ECCLES_WEB_PERM_NET)) { wb_deny(vm, "net"); wb_fail(vm, ECCLES_WEB_ERR_NO_NET, 0); return 0; }
        if (!g_net_open || !g_net_read) { wb_fail(vm, ECCLES_WEB_ERR_NO_NET, 0); return 0; }
        s->h = g_net_open(url); s->kind = 3;
        if (!s->h) { wb_fail(vm, ECCLES_WEB_ERR_IO, 0); return 0; }
        return 1;
    }
    if (!strncmp(url, "file://", 7)) url += 7;
    if (g_fs_open && g_fs_read) {
        s->h = g_fs_open(url); s->kind = 2;
        if (!s->h) { wb_fail(vm, ECCLES_WEB_ERR_IO, 0); return 0; }
        return 1;
    }
#if ECCLES_WEB_FS == ECCLES_WEB_FS_STDIO
    s->h = fopen(url, "rb"); s->kind = 1;
    if (!s->h) { wb_fail(vm, ECCLES_WEB_ERR_IO, 0); return 0; }
    return 1;
#else
    (void)sub; wb_fail(vm, ECCLES_WEB_ERR_NO_FS, 0); return 0;
#endif
}
static int wb_src_read(void *ctx, uint8_t *buf, size_t max) {
    wb_src_t *s = (wb_src_t *)ctx;
    if (s->kind == 3) return g_net_read(s->h, buf, max);
    if (s->kind == 2) return g_fs_read(s->h, buf, max);
#if ECCLES_WEB_FS == ECCLES_WEB_FS_STDIO
    if (s->kind == 1) return (int)fread(buf, 1, max, (FILE *)s->h);
#endif
    return -1;
}
static void wb_src_close(wb_src_t *s) {
    if (!s->h) return;
    if (s->kind == 3 && g_net_close) g_net_close(s->h);
    else if (s->kind == 2 && g_fs_close) g_fs_close(s->h);
#if ECCLES_WEB_FS == ECCLES_WEB_FS_STDIO
    else if (s->kind == 1) fclose((FILE *)s->h);
#endif
    s->h = NULL;
}

//streaming reader
static void wb_rd_mem(wb_reader_t *r, const void *p, size_t n) {
    r->mem_src = 1; r->fn = NULL; r->ctx = NULL; r->eof = 0; r->p = (const uint8_t *)p; r->e = r->p + n;
}
static void wb_rd_stream(wb_reader_t *r, eccles_web_read_fn fn, void *ctx) {
    r->mem_src = 0; r->fn = fn; r->ctx = ctx; r->eof = 0; r->p = r->e = r->chunk;
}
//refills the chunk buffer from the stream, returns 0 when there is nothing more to read
static int wb_rd_fill(eccles_web_vm_t *vm, wb_reader_t *r) {
    int n;
    if (r->mem_src || !r->fn || r->eof) return 0;
    n = r->fn(r->ctx, r->chunk, sizeof r->chunk);
    if (n < 0 || n > (int)sizeof r->chunk) { wb_fail(vm, ECCLES_WEB_ERR_IO, 0); n = 0; }
    if (n == 0) r->eof = 1;
    r->p = r->chunk; r->e = r->chunk + n;
    return n > 0;
}
static int wb_rd_get(eccles_web_vm_t *vm, wb_reader_t *r) {
    if (r->p >= r->e && !wb_rd_fill(vm, r)) return -1;
    return *r->p++;
}
static int wb_rd_peek(eccles_web_vm_t *vm, wb_reader_t *r) {
    if (r->p >= r->e && !wb_rd_fill(vm, r)) return -1;
    return *r->p;
}

//the free space between the two ends of the arena. a script or a style sheet is read here while it is compiled/parsed, then it is gone
//(nothing else allocates from the arena while that happens, page tables are all allocated up front)
static uint8_t *wb_scratch(eccles_web_vm_t *vm, uint32_t *cap) {
    *cap = (uint32_t)(vm->hi - vm->lo); return vm->lo;
}

//css + script entry points of the other sections
static void wb_css_parse(eccles_web_vm_t *vm, wb_reader_t *r, int author);
static void wb_css_load_sheet(eccles_web_vm_t *vm, const char *href);
static void wb_run_script(eccles_web_vm_t *vm, const char *src, uint32_t len);
static void wb_load_script_url(eccles_web_vm_t *vm, const char *href);
static void wb_fire_load_events(eccles_web_vm_t *vm);

//html tag classes
static int wb_is_void(uint16_t t) {
    switch (t) {
        case A_area: case A_base: case A_br: case A_col: case A_embed: case A_hr: case A_img: case A_input:
        case A_link: case A_meta: case A_param: case A_source: case A_track: case A_wbr: return 1;
        default: return 0;
    }
}
//inline level tags: a space between two of them is a real space. (display from css wins for layout, this is only for the parser's whitespace rule)
static int wb_is_inline_tag(uint16_t t) {
    switch (t) {
        case A_a: case A_abbr: case A_b: case A_big: case A_button: case A_cite: case A_code: case A_del: case A_em: case A_font:
        case A_i: case A_img: case A_input: case A_ins: case A_kbd: case A_label: case A_mark: case A_q: case A_s: case A_samp:
        case A_select: case A_small: case A_span: case A_strike: case A_strong: case A_sub: case A_sup: case A_textarea:
        case A_time: case A_tt: case A_u: case A_var: return 1;
        default: return 0;
    }
}
//tags that end an open <p>
static int wb_closes_p(uint16_t t) {
    switch (t) {
        case A_address: case A_article: case A_aside: case A_blockquote: case A_div: case A_dl: case A_fieldset: case A_figure:
        case A_footer: case A_form: case A_h1: case A_h2: case A_h3: case A_h4: case A_h5: case A_h6: case A_header: case A_hr:
        case A_main: case A_nav: case A_ol: case A_p: case A_pre: case A_section: case A_table: case A_ul: return 1;
        default: return 0;
    }
}
static int wb_is_head_tag(uint16_t t) {
    switch (t) { case A_title: case A_meta: case A_link: case A_style: case A_script: case A_base: case A_noscript: case A_head: case A_html: return 1; default: return 0; }
}

//html parser state
typedef struct {
    eccles_web_vm_t *vm;
    wb_reader_t *r;
    uint16_t stack[ECCLES_WEB_MAX_DEPTH];
    int      sp;
    uint16_t tn;  //text node being filled (0 = none)
    uint8_t  tsp, wsp;  //last text char was a space / whitespace seen before any text started
    uint8_t  pre;  //inside <pre>/<textarea>: keep whitespace
    uint8_t  stop, nojs;
    int      base;  //lowest stack index that may be popped (2 when parsing a fragment into an element)
} wb_hp_t;

static uint16_t wb_hp_top(const wb_hp_t *h) { return h->stack[h->sp - 1]; }

//closes the text node (puts the NUL after it)
static void wb_hp_text_end(wb_hp_t *h) {
    eccles_web_vm_t *vm = h->vm;
    if (h->tn) {
        wb_node_t *n = &vm->nodes[h->tn];
        vm->text[vm->text_len++] = 0;
        n->tlen = (uint16_t)(vm->text_len - 1 - n->text);
        h->tn = 0;
    }
    h->tsp = 0;
}
//is the thing before the next node of this parent inline (so a space between is real)
static int wb_hp_prev_inline(const wb_hp_t *h) {
    const eccles_web_vm_t *vm = h->vm; uint16_t last = vm->nodes[wb_hp_top(h)].last;
    if (!last) return 0;
    return vm->nodes[last].type == WB_N_TEXT || wb_is_inline_tag(vm->nodes[last].tag);
}
static void wb_hp_put(wb_hp_t *h, uint32_t ch) {  //append one utf-8 encoded char to the open text node (creates it first)
    eccles_web_vm_t *vm = h->vm; uint8_t b[4]; int n, i;
    if (ch < 0x80) { b[0] = (uint8_t)ch; n = 1; }
    else if (ch < 0x800) { b[0] = (uint8_t)(0xC0 | (ch >> 6)); b[1] = (uint8_t)(0x80 | (ch & 63)); n = 2; }
    else if (ch < 0x10000) { b[0] = (uint8_t)(0xE0 | (ch >> 12)); b[1] = (uint8_t)(0x80 | ((ch >> 6) & 63)); b[2] = (uint8_t)(0x80 | (ch & 63)); n = 3; }
    else { b[0] = (uint8_t)(0xF0 | (ch >> 18)); b[1] = (uint8_t)(0x80 | ((ch >> 12) & 63)); b[2] = (uint8_t)(0x80 | ((ch >> 6) & 63)); b[3] = (uint8_t)(0x80 | (ch & 63)); n = 4; }
    if (!h->tn) {
        wb_node_t *t;
        if (vm->text_len + 2 > ECCLES_WEB_TEXT_POOL) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 2); return; }
        t = wb_new_node(vm, WB_N_TEXT, 0, wb_hp_top(h)); if (!t) return;
        t->text = vm->text_len; h->tn = wb_idx(vm, t);
    }
    if (vm->text_len + (uint32_t)n + 1 > ECCLES_WEB_TEXT_POOL || vm->nodes[h->tn].tlen == 0xFFFFu) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 2); return; }
    for (i = 0; i < n; i++) vm->text[vm->text_len++] = b[i];
}

//entities. name table is small on purpose, numeric ones (&#169; &#xA9;) cover the rest
static uint32_t wb_entity_lookup(const char *s, uint32_t n) {
    static const struct { const char *n; uint16_t c; } t[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE},
        {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"bull", 0x2022},
        {"middot", 0xB7}, {"times", 0xD7}, {"euro", 0x20AC}, {"trade", 0x2122}, {"larr", 0x2190}, {"rarr", 0x2192},
        {"uarr", 0x2191}, {"darr", 0x2193}, {"deg", 0xB0}, {"plusmn", 0xB1}, {"pound", 0xA3}, {"yen", 0xA5}, {"cent", 0xA2},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"check", 0x2713}, {"hearts", 0x2665}
    };
    uint32_t i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) if (strlen(t[i].n) == n && !memcmp(t[i].n, s, n)) return t[i].c;
    return 0;
}
//called after a '&' was read. returns the code point (or 0 and the raw text in raw[]) 
static uint32_t wb_entity(eccles_web_vm_t *vm, wb_reader_t *r, char *raw, int *rawn) {
    char b[12]; int n = 0, c; uint32_t cp = 0;
    while (n < 10) {
        c = wb_rd_peek(vm, r);
        if (c < 0 || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '#' && n == 0))) break;
        b[n++] = (char)wb_rd_get(vm, r);
    }
    if (n > 0 && wb_rd_peek(vm, r) == ';') {
        if (b[0] == '#') {
            int i = 1, hex = (n > 1 && (b[1] == 'x' || b[1] == 'X')); uint32_t v = 0; int ok = 0;
            if (hex) i = 2;
            for (; i < n; i++) {
                int d = (b[i] >= '0' && b[i] <= '9') ? b[i] - '0' : (b[i] >= 'a' && b[i] <= 'f') ? b[i] - 'a' + 10 : (b[i] >= 'A' && b[i] <= 'F') ? b[i] - 'A' + 10 : -1;
                if (d < 0 || (!hex && d > 9)) { ok = 0; break; }
                v = v * (hex ? 16u : 10u) + (uint32_t)d; ok = 1;
                if (v > 0x10FFFFu) { ok = 0; break; }
            }
            if (ok) { cp = v; }
        } else cp = wb_entity_lookup(b, (uint32_t)n);
        if (cp) { wb_rd_get(vm, r); *rawn = 0; return cp; }
    }
    raw[0] = '&'; memcpy(raw + 1, b, (size_t)n); *rawn = n + 1;
    return 0;
}

//a plain character of the page (after entity decoding) goes into the current text node, whitespace is collapsed here (not in pre) so the text stays small
static void wb_hp_char(wb_hp_t *h, uint32_t ch) {
    int sp = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f');
    if (h->pre) { if (ch == '\r') return; wb_hp_put(h, ch); return; }
    if (sp) {
        if (!h->tn) { h->wsp = 1; return; }  //leading whitespace: decided when the first real char (or the next tag) shows up
        if (!h->tsp) { wb_hp_put(h, ' '); h->tsp = 1; }
        return;
    }
    if (!h->tn && h->wsp && wb_hp_prev_inline(h)) { wb_hp_put(h, ' '); }  //space between inline things stays
    h->wsp = 0; h->tsp = 0;
    wb_hp_put(h, ch);
}

//pops the open elements down to (and including) stack index i
static void wb_hp_pop_to(wb_hp_t *h, int i) { if (i < h->base) i = h->base; if (h->sp > i) h->sp = i; }

//the parts of html's "implied end tags" that real pages need: <p> closed by a block, <li>, <dt>/<dd>, table cells and rows, <option>, <head>
static void wb_hp_implied(wb_hp_t *h, uint16_t tag) {
    eccles_web_vm_t *vm = h->vm; int i;
    uint16_t top = wb_hp_top(h) ? vm->nodes[wb_hp_top(h)].tag : 0;
    if (top == A_head && !wb_is_head_tag(tag)) { wb_hp_pop_to(h, h->sp - 1); top = vm->nodes[wb_hp_top(h)].tag; }
    if (top == A_head && tag == A_body) wb_hp_pop_to(h, h->sp - 1);
    if (wb_closes_p(tag)) {
        for (i = h->sp - 1; i >= h->base; i--) {
            uint16_t t = vm->nodes[h->stack[i]].tag;
            if (t == A_p) { wb_hp_pop_to(h, i); break; }
            if (!wb_is_inline_tag(t)) break;
        }
    }
    if (tag == A_li) {
        for (i = h->sp - 1; i >= h->base; i--) {
            uint16_t t = vm->nodes[h->stack[i]].tag;
            if (t == A_li) { wb_hp_pop_to(h, i); break; }
            if (t == A_ul || t == A_ol) break;
        }
    } else if (tag == A_dt || tag == A_dd) {
        for (i = h->sp - 1; i >= h->base; i--) {
            uint16_t t = vm->nodes[h->stack[i]].tag;
            if (t == A_dt || t == A_dd) { wb_hp_pop_to(h, i); break; }
            if (t == A_dl) break;
        }
    } else if (tag == A_td || tag == A_th) {
        for (i = h->sp - 1; i >= h->base; i--) {
            uint16_t t = vm->nodes[h->stack[i]].tag;
            if (t == A_td || t == A_th) { wb_hp_pop_to(h, i); break; }
            if (t == A_tr || t == A_table) break;
        }
    } else if (tag == A_tr) {
        for (i = h->sp - 1; i >= h->base; i--) {
            uint16_t t = vm->nodes[h->stack[i]].tag;
            if (t == A_tr) { wb_hp_pop_to(h, i); break; }
            if (t == A_table || t == A_tbody || t == A_thead || t == A_tfoot) break;
        }
    } else if (tag == A_tbody || tag == A_thead || tag == A_tfoot) {
        for (i = h->sp - 1; i >= h->base; i--) {
            uint16_t t = vm->nodes[h->stack[i]].tag;
            if (t == A_tbody || t == A_thead || t == A_tfoot) { wb_hp_pop_to(h, i); break; }
            if (t == A_table) break;
        }
    } else if (tag == A_option) { if (top == A_option) wb_hp_pop_to(h, h->sp - 1); }
}

//reads raw text (script, style, textarea, title) up to </name into buf. returns the length (cut to cap, the rest is skipped)
static uint32_t wb_hp_raw(wb_hp_t *h, const char *name, uint8_t *buf, uint32_t cap, int decode) {
    eccles_web_vm_t *vm = h->vm; wb_reader_t *r = h->r;
    char pat[16]; uint32_t L, len = 0, m = 0; int c;
    pat[0] = '<'; pat[1] = '/'; strncpy(pat + 2, name, 12); pat[13] = 0; L = (uint32_t)strlen(pat);
    for (;;) {
        c = wb_rd_get(vm, r);
        if (c < 0) break;
        if (decode && c == '&' && m == 0) {
            char raw[12]; int rn, k; uint32_t cp = wb_entity(vm, r, raw, &rn);
            if (cp) {
                uint8_t u[4]; int un;
                if (cp < 0x80) { u[0] = (uint8_t)cp; un = 1; } else if (cp < 0x800) { u[0] = (uint8_t)(0xC0 | (cp >> 6)); u[1] = (uint8_t)(0x80 | (cp & 63)); un = 2; }
                else if (cp < 0x10000) { u[0] = (uint8_t)(0xE0 | (cp >> 12)); u[1] = (uint8_t)(0x80 | ((cp >> 6) & 63)); u[2] = (uint8_t)(0x80 | (cp & 63)); un = 3; }
                else { u[0] = (uint8_t)(0xF0 | (cp >> 18)); u[1] = (uint8_t)(0x80 | ((cp >> 12) & 63)); u[2] = (uint8_t)(0x80 | ((cp >> 6) & 63)); u[3] = (uint8_t)(0x80 | (cp & 63)); un = 4; }
                for (k = 0; k < un; k++) if (len < cap) buf[len++] = u[k];
            } else for (k = 0; k < rn; k++) if (len < cap) buf[len++] = (uint8_t)raw[k];
            continue;
        }
        if (len < cap) buf[len++] = (uint8_t)c;
        { int lc = (c >= 'A' && c <= 'Z') ? c + 32 : c;
          if (m < L && lc == pat[m]) m++; else m = (c == '<') ? 1 : 0; }
        if (m == L) {
            int nx = wb_rd_peek(vm, r);
            if (nx == '>' || nx == ' ' || nx == '\t' || nx == '\n' || nx == '\r' || nx == '/') {
                if (len >= L) len -= L; else len = 0;
                while ((c = wb_rd_get(vm, r)) >= 0 && c != '>') {}
                return len;
            }
            m = 0;
        }
    }
    return len;
}

//attributes of the start tag that was just read (the element node already exists), up to the '>'. returns 1 when it ended with />
static int wb_hp_attrs(wb_hp_t *h, uint16_t node) {
    eccles_web_vm_t *vm = h->vm; wb_reader_t *r = h->r; int c;
    for (;;) {
        char nm[ECCLES_WEB_MAX_NAME + 1]; uint32_t nn = 0; uint16_t atom;
        while ((c = wb_rd_peek(vm, r)) == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') wb_rd_get(vm, r);
        if (c < 0) return 0;
        if (c == '>') { wb_rd_get(vm, r); return 0; }
        if (c == '/') { wb_rd_get(vm, r); if (wb_rd_peek(vm, r) == '>') { wb_rd_get(vm, r); return 1; } continue; }
        while ((c = wb_rd_peek(vm, r)) >= 0 && c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '=' && c != '>' && c != '/') {
            wb_rd_get(vm, r); if (nn < ECCLES_WEB_MAX_NAME) nm[nn++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
        }
        nm[nn] = 0;
        while ((c = wb_rd_peek(vm, r)) == ' ' || c == '\t' || c == '\n' || c == '\r') wb_rd_get(vm, r);
        atom = nn ? wb_atom(vm, nm, nn) : 0;
        if (c == '=') {
            int q; uint32_t off, start; uint32_t len = 0;
            wb_rd_get(vm, r);
            while ((c = wb_rd_peek(vm, r)) == ' ' || c == '\t' || c == '\n' || c == '\r') wb_rd_get(vm, r);
            q = (c == '"' || c == '\'') ? c : 0; if (q) wb_rd_get(vm, r);
            //the value goes straight into the pool (nothing else writes to it while a tag is being read)
            start = vm->text_len;
            if (vm->text_len + 1 > ECCLES_WEB_TEXT_POOL) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 2); return 0; }
            for (;;) {
                uint8_t tmp[4]; int tn2 = 1, k; uint32_t cp;
                c = wb_rd_peek(vm, r);
                if (c < 0) break;
                if (q ? c == q : (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '>')) { if (q) wb_rd_get(vm, r); break; }
                wb_rd_get(vm, r);
                if (c == '&') {
                    char raw[12]; int rn; cp = wb_entity(vm, r, raw, &rn);
                    if (cp) {
                        if (cp < 0x80) tmp[0] = (uint8_t)cp, tn2 = 1;
                        else if (cp < 0x800) { tmp[0] = (uint8_t)(0xC0 | (cp >> 6)); tmp[1] = (uint8_t)(0x80 | (cp & 63)); tn2 = 2; }
                        else if (cp < 0x10000) { tmp[0] = (uint8_t)(0xE0 | (cp >> 12)); tmp[1] = (uint8_t)(0x80 | ((cp >> 6) & 63)); tmp[2] = (uint8_t)(0x80 | (cp & 63)); tn2 = 3; }
                        else { tmp[0] = (uint8_t)(0xF0 | (cp >> 18)); tmp[1] = (uint8_t)(0x80 | ((cp >> 12) & 63)); tmp[2] = (uint8_t)(0x80 | ((cp >> 6) & 63)); tmp[3] = (uint8_t)(0x80 | (cp & 63)); tn2 = 4; }
                    } else { for (k = 0; k < rn; k++) { if (vm->text_len + 2 <= ECCLES_WEB_TEXT_POOL && len < 0xFFF0u) { vm->text[vm->text_len++] = (uint8_t)raw[k]; len++; } } continue; }
                } else tmp[0] = (uint8_t)c;
                for (k = 0; k < tn2; k++) {
                    if (vm->text_len + 2 > ECCLES_WEB_TEXT_POOL || len >= 0xFFF0u) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 2); continue; }
                    vm->text[vm->text_len++] = tmp[k]; len++;
                }
            }
            vm->text[vm->text_len++] = 0;
            off = start;
            if (atom && !wb_attr_find(vm, node, atom)) {
                uint16_t ai = wb_attr_alloc(vm);
                if (ai) { wb_attr_t *a = &vm->attrs[ai]; a->name = atom; a->off = off; a->len = (uint16_t)len; a->cap = (uint16_t)len; a->next = vm->nodes[node].attr; vm->nodes[node].attr = ai; }
            }
        } else if (atom && !wb_attr_find(vm, node, atom)) {  //boolean attribute: name only
            uint16_t ai = wb_attr_alloc(vm);
            if (ai) { wb_attr_t *a = &vm->attrs[ai]; a->name = atom; a->off = 0; a->len = 0; a->cap = 0; a->next = vm->nodes[node].attr; vm->nodes[node].attr = ai; }
        }
        if (vm->err) return 0;
    }
}

static int wb_is_stylesheet_link(const eccles_web_vm_t *vm, uint16_t n) {
    uint32_t l; const char *rel = wb_attr_get(vm, n, A_rel, &l);
    if (!rel) return 0;
    return l >= 10 && !strncmp(rel, "stylesheet", 10);
}

static void wb_hp_start(wb_hp_t *h) {
    eccles_web_vm_t *vm = h->vm; wb_reader_t *r = h->r;
    char nm[ECCLES_WEB_MAX_NAME + 1]; uint32_t nn = 0; int c; uint16_t tag; wb_node_t *n; uint16_t ni; int selfclose;
    while ((c = wb_rd_peek(vm, r)) >= 0 && c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '>' && c != '/') {
        wb_rd_get(vm, r); if (nn < ECCLES_WEB_MAX_NAME) nm[nn++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    nm[nn] = 0; tag = wb_atom(vm, nm, nn);
    if (!tag) return;
    //whitespace that was waiting before this tag stays when it sits between inline things
    if (!h->tn && h->wsp && !h->pre && wb_hp_prev_inline(h) && wb_is_inline_tag(tag)) { wb_hp_put(h, ' '); }
    wb_hp_text_end(h); h->wsp = 0;
    wb_hp_implied(h, tag);
    if (h->sp >= ECCLES_WEB_MAX_DEPTH) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 5); return; }  //5 = depth
    n = wb_new_node(vm, WB_N_ELEM, tag, wb_hp_top(h)); if (!n) return;
    ni = wb_idx(vm, n);
    selfclose = wb_hp_attrs(h, ni);
    if (vm->err) return;
    if (tag == A_body && !vm->body_node) vm->body_node = ni;
    if (wb_is_void(tag) || (selfclose && !(tag == A_script || tag == A_style || tag == A_title || tag == A_textarea))) {
        if (tag == A_link && wb_is_stylesheet_link(vm, ni)) {
            uint32_t l; const char *href = wb_attr_get(vm, ni, A_href, &l);
            if (href && l) wb_css_load_sheet(vm, href);
        }
        return;  //not pushed
    }
    h->stack[h->sp++] = ni;
    if (tag == A_pre || tag == A_textarea) {
        h->pre++;
        if (wb_rd_peek(vm, r) == '\n') wb_rd_get(vm, r);  //the first newline of a <pre> does not count
        else if (wb_rd_peek(vm, r) == '\r') { wb_rd_get(vm, r); if (wb_rd_peek(vm, r) == '\n') wb_rd_get(vm, r); }
    }
    if (tag == A_title || tag == A_textarea) {
        //text that may hold entities but no tags: kept as a text node (the engine uses it for the page title and as the textarea's value)
        uint32_t cap = (uint32_t)(ECCLES_WEB_TEXT_POOL - vm->text_len), len; uint8_t *dst = vm->text + vm->text_len;
        if (cap > 2) cap -= 2; else cap = 0;
        len = wb_hp_raw(h, tag == A_title ? "title" : "textarea", dst, cap > 0xFFF0u ? 0xFFF0u : cap, 1);
        if (tag == A_title && vm->title_node == 0) vm->title_node = ni;
        if (len || tag == A_textarea) {
            wb_node_t *t = wb_new_node(vm, WB_N_TEXT, 0, ni);
            if (t) { t->text = vm->text_len; t->tlen = (uint16_t)len; vm->text_len += len; vm->text[vm->text_len++] = 0; }
        }
        if (tag == A_textarea && h->pre) h->pre--;
        h->sp--;
    } else if (tag == A_script || tag == A_style) {
        uint32_t cap, len; uint8_t *buf = wb_scratch(vm, &cap);
        uint32_t sl; const char *src = (tag == A_script) ? wb_attr_get(vm, ni, A_src, &sl) : NULL;
        len = wb_hp_raw(h, tag == A_script ? "script" : "style", buf, cap, 0);
        h->sp--;
        if (tag == A_style) {
            wb_reader_t cr; wb_rd_mem(&cr, buf, len); wb_css_parse(vm, &cr, 1);
        } else if (h->nojs) {
            /* scripts in innerHTML never run */
        } else if (src && sl) {
            wb_load_script_url(vm, src);
        } else if (len) {
            wb_run_script(vm, (const char *)buf, len);
        }
    }
}

static void wb_hp_end(wb_hp_t *h) {
    eccles_web_vm_t *vm = h->vm; wb_reader_t *r = h->r;
    char nm[ECCLES_WEB_MAX_NAME + 1]; uint32_t nn = 0; int c, i; uint16_t tag;
    while ((c = wb_rd_peek(vm, r)) >= 0 && c != '>' && c != ' ' && c != '\t' && c != '\n' && c != '\r') {
        wb_rd_get(vm, r); if (nn < ECCLES_WEB_MAX_NAME) nm[nn++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    while ((c = wb_rd_get(vm, r)) >= 0 && c != '>') {}
    nm[nn] = 0; tag = wb_atom_find(vm, nm, nn);
    wb_hp_text_end(h); h->wsp = 0;
    if (!tag || wb_is_void(tag)) return;
    for (i = h->sp - 1; i >= h->base; i--) {
        if (vm->nodes[h->stack[i]].tag == tag) {
            int k; for (k = i; k < h->sp; k++) if (vm->nodes[h->stack[k]].tag == A_pre) { if (h->pre) h->pre--; }
            h->sp = i; return;
        }
    }
}

//parent = 0 parses a document, else the content is added as children of that element (innerHTML), where scripts do not run
static void wb_html_run_in(eccles_web_vm_t *vm, wb_reader_t *r, uint16_t parent) {
    wb_hp_t h; int c;
    memset(&h, 0, sizeof h); h.vm = vm; h.r = r; h.sp = 1; h.stack[0] = 0; h.base = 1;
    if (parent) { h.stack[1] = parent; h.sp = 2; h.base = 2; h.nojs = 1; }
    while (!vm->err && (c = wb_rd_get(vm, r)) >= 0) {
        if (c == '<') {
            int n = wb_rd_peek(vm, r);
            if (n == '!') {
                wb_rd_get(vm, r);
                if (wb_rd_peek(vm, r) == '-') {  //comment: skip to -->
                    int d1 = 0, d2 = 0;
                    while ((c = wb_rd_get(vm, r)) >= 0) {
                        if (c == '>' && d1 >= 2) break;
                        if (c == '-') d1++; else d1 = 0;
                        (void)d2;
                    }
                } else { while ((c = wb_rd_get(vm, r)) >= 0 && c != '>') {} }  //doctype, <![CDATA[
            } else if (n == '?') { while ((c = wb_rd_get(vm, r)) >= 0 && c != '>') {} }
            else if (n == '/') { wb_rd_get(vm, r); wb_hp_end(&h); }
            else if ((n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z')) wb_hp_start(&h);
            else wb_hp_char(&h, '<');
        } else if (c == '&') {
            char raw[12]; int rn, k; uint32_t cp = wb_entity(vm, r, raw, &rn);
            if (cp) wb_hp_char(&h, cp);  //a non breaking space is a normal char here, it is not collapsed
            else for (k = 0; k < rn; k++) wb_hp_char(&h, (uint8_t)raw[k]);
        } else wb_hp_char(&h, (uint32_t)c);
    }
    wb_hp_text_end(&h);
}

static void wb_html_run(eccles_web_vm_t *vm, wb_reader_t *r) { wb_html_run_in(vm, r, 0); }

//3. css

//a declaration value is one u32: the low 4 bits are the unit, the rest the number in 1/16 steps (or an atom / a 0xRRGGBB color)
enum { U_UNSET = 0, U_PX, U_EM, U_PCT, U_NUM, U_KW, U_COLOR };
#define WB_ENC(u, v) ((uint32_t)((((int32_t)(v)) * 16 & ~15) | (u)))  //v is already in 1/16 steps
#define WB_ENCK(u, v) ((uint32_t)(((uint32_t)(v) << 4) | (u)))  //for atoms and colors (no sign)
#define WB_UNIT(v) ((v) & 15u)
#define WB_NUM(v) ((int32_t)(v) >> 4)  //1/16 steps
#define WB_KWOF(v) ((uint16_t)((v) >> 4))
#define WB_COLOROF(v) (((v) >> 4) & 0x1FFFFFFu)  //keeps bit 24 (= none)
static uint32_t wb_enc(unsigned unit, int32_t q4) { return (uint32_t)(((uint32_t)q4 << 4) | unit); }

//properties the engine knows. everything else in a style sheet is skipped
enum {
    P_NONE = 0, P_DISPLAY, P_COLOR, P_BG, P_FONT_SIZE, P_FONT_WEIGHT, P_FONT_STYLE, P_FONT_FAMILY, P_TEXT_ALIGN, P_TEXT_DECO, P_LINE_HEIGHT,
    P_MT, P_MR, P_MB, P_ML, P_PT, P_PR, P_PB, P_PL, P_BWT, P_BWR, P_BWB, P_BWL, P_BCOLOR,
    P_WIDTH, P_HEIGHT, P_MAXW, P_MINW, P_MINH, P_MAXH,
    P_FLEX_DIR, P_JUSTIFY, P_ALIGN_ITEMS, P_GROW, P_BASIS, P_GAP, P_VISIBILITY, P_WHITE_SPACE, P_BOXSIZING, P_LIST_STYLE, P_ALIGN_SELF,
    P_COUNT
};

typedef void (*wb_emit_fn)(void *ctx, uint8_t prop, uint32_t val, uint8_t imp);
typedef struct { const char *p; uint32_t n; } wb_tok_t;

static int wb_lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int wb_is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int wb_ieq(const char *a, uint32_t an, const char *b) {  //case insensitive compare with a C string
    uint32_t i;
    if (strlen(b) != an) return 0;
    for (i = 0; i < an; i++) if (wb_lc((uint8_t)a[i]) != wb_lc((uint8_t)b[i])) return 0;
    return 1;
}
//an atom from a css keyword (lower cased, only if the engine knows it)
static uint16_t wb_kw(const eccles_web_vm_t *vm, const char *s, uint32_t n) {
    char b[24]; uint32_t i;
    if (n == 0 || n >= sizeof b) return 0;
    for (i = 0; i < n; i++) b[i] = (char)wb_lc((uint8_t)s[i]);
    return wb_atom_find(vm, b, n);
}

//splits a value on whitespace (not inside () or quotes)
static int wb_css_tokens(const char *s, uint32_t n, wb_tok_t *t, int max) {
    uint32_t i = 0; int k = 0;
    while (i < n && k < max) {
        uint32_t st; int depth = 0, q = 0;
        while (i < n && (wb_is_space(s[i]) || s[i] == ',')) i++;
        if (i >= n) break;
        st = i;
        while (i < n) {
            char c = s[i];
            if (q) { if (c == q) q = 0; }
            else if (c == '"' || c == '\'') q = c;
            else if (c == '(') depth++;
            else if (c == ')') { if (depth) depth--; }
            else if (depth == 0 && (wb_is_space(c) || c == ',')) break;
            i++;
        }
        t[k].p = s + st; t[k].n = i - st; k++;
    }
    return k;
}

//number at the start of s: returns chars used (0 = not a number), value in 1/16 steps
static uint32_t wb_css_number(const char *s, uint32_t n, int32_t *q4) {
    uint32_t i = 0, ip = 0, f = 0, d = 0, pw = 1, k; int neg = 0, any = 0;
    if (i < n && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
    while (i < n && s[i] >= '0' && s[i] <= '9') { if (ip < 1000000u) ip = ip * 10 + (uint32_t)(s[i] - '0'); i++; any = 1; }
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') { if (d < 4) { f = f * 10 + (uint32_t)(s[i] - '0'); d++; } i++; any = 1; }
    }
    if (!any) return 0;
    for (k = 0; k < d; k++) pw *= 10;
    { int32_t v = (int32_t)(ip * 16u + (d ? (f * 16u + pw / 2) / pw : 0)); *q4 = neg ? -v : v; }
    return i;
}

//a length / number / keyword. returns the encoded value, 0 when it is not valid
static uint32_t wb_css_value1(const eccles_web_vm_t *vm, const char *s, uint32_t n, int allow_kw) {
    int32_t q; uint32_t u = wb_css_number(s, n, &q), rest;
    if (!u) {
        uint16_t a;
        if (!allow_kw) return 0;
        a = wb_kw(vm, s, n);
        return a ? WB_ENCK(U_KW, a) : 0;
    }
    s += u; rest = n - u;
    if (rest == 0) return wb_enc(U_NUM, q);
    if (wb_ieq(s, rest, "px")) return wb_enc(U_PX, q);
    if (wb_ieq(s, rest, "em")) return wb_enc(U_EM, q);
    if (wb_ieq(s, rest, "rem")) return wb_enc(U_PX, (int32_t)((int64_t)q * ECCLES_WEB_DEFAULT_FONT_PX));
    if (wb_ieq(s, rest, "%")) return wb_enc(U_PCT, q);
    if (wb_ieq(s, rest, "pt")) return wb_enc(U_PX, q * 4 / 3);
    if (wb_ieq(s, rest, "vw")) return wb_enc(U_PX, (int32_t)((int64_t)q * (int32_t)vm->vw / 100));
    if (wb_ieq(s, rest, "vh")) return wb_enc(U_PX, (int32_t)((int64_t)q * (int32_t)vm->vh / 100));
    return 0;
}

//colors
static const struct { const char *n; uint32_t c; } wb_colors[] = {
    {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000}, {"blue", 0x0000FF}, {"yellow", 0xFFFF00},
    {"orange", 0xFFA500}, {"purple", 0x800080}, {"pink", 0xFFC0CB}, {"gray", 0x808080}, {"grey", 0x808080}, {"silver", 0xC0C0C0},
    {"maroon", 0x800000}, {"navy", 0x000080}, {"teal", 0x008080}, {"lime", 0x00FF00}, {"aqua", 0x00FFFF}, {"cyan", 0x00FFFF},
    {"fuchsia", 0xFF00FF}, {"magenta", 0xFF00FF}, {"olive", 0x808000}, {"brown", 0xA52A2A}, {"gold", 0xFFD700}, {"indigo", 0x4B0082},
    {"violet", 0xEE82EE}, {"coral", 0xFF7F50}, {"crimson", 0xDC143C}, {"tomato", 0xFF6347}, {"salmon", 0xFA8072}, {"khaki", 0xF0E68C},
    {"tan", 0xD2B48C}, {"beige", 0xF5F5DC}, {"ivory", 0xFFFFF0}, {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3}, {"darkgray", 0xA9A9A9},
    {"darkgrey", 0xA9A9A9}, {"lightblue", 0xADD8E6}, {"darkblue", 0x00008B}, {"darkgreen", 0x006400}, {"darkred", 0x8B0000},
    {"lightgreen", 0x90EE90}, {"whitesmoke", 0xF5F5F5}, {"gainsboro", 0xDCDCDC}, {"dimgray", 0x696969}, {"dimgrey", 0x696969},
    {"skyblue", 0x87CEEB}, {"steelblue", 0x4682B4}, {"royalblue", 0x4169E1}, {"slategray", 0x708090}, {"slategrey", 0x708090},
    {"orangered", 0xFF4500}, {"firebrick", 0xB22222}, {"forestgreen", 0x228B22}, {"seagreen", 0x2E8B57}, {"turquoise", 0x40E0D0},
    {"chocolate", 0xD2691E}, {"peru", 0xCD853F}, {"lavender", 0xE6E6FA}, {"snow", 0xFFFAFA}, {"azure", 0xF0FFFF}, {"aliceblue", 0xF0F8FF}
};
static int wb_hex(int c) { return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; }

static uint32_t wb_hsl(int32_t h, int32_t s, int32_t l) {  //h 0..359, s,l 0..100
    int32_t c, x, m, r1, g1, b1, hh;
    c = (100 - (l * 2 > 100 ? l * 2 - 100 : 100 - l * 2)) * s / 100;  //chroma in 0..100 (scaled)
    hh = (h % 360) * 100 / 60;  //0..599
    x = c * (100 - ((hh % 200) > 100 ? (hh % 200) - 100 : 100 - (hh % 200))) / 100;
    m = l - c / 2;
    switch ((hh / 100) % 6) { case 0: r1 = c; g1 = x; b1 = 0; break; case 1: r1 = x; g1 = c; b1 = 0; break; case 2: r1 = 0; g1 = c; b1 = x; break;
                              case 3: r1 = 0; g1 = x; b1 = c; break; case 4: r1 = x; g1 = 0; b1 = c; break; default: r1 = c; g1 = 0; b1 = x; break; }
    r1 = (r1 + m) * 255 / 100; g1 = (g1 + m) * 255 / 100; b1 = (b1 + m) * 255 / 100;
    if (r1 < 0) r1 = 0;
    if (g1 < 0) g1 = 0;
    if (b1 < 0) b1 = 0;
    if (r1 > 255) r1 = 255;
    if (g1 > 255) g1 = 255;
    if (b1 > 255) b1 = 255;
    return ((uint32_t)r1 << 16) | ((uint32_t)g1 << 8) | (uint32_t)b1;
}

//returns 0 = not a color, 1 = color in *rgb, 2 = transparent, 3 = currentcolor
static int wb_css_color(const char *s, uint32_t n, uint32_t *rgb) {
    uint32_t i;
    if (n && s[0] == '#') {
        int d[8]; uint32_t k;
        if (n != 4 && n != 5 && n != 7 && n != 9) return 0;
        for (k = 1; k < n; k++) { d[k - 1] = wb_hex((uint8_t)s[k]); if (d[k - 1] < 0) return 0; }
        if (n <= 5) {
            if (n == 5 && d[3] == 0) return 2;
            *rgb = (uint32_t)(d[0] * 17) << 16 | (uint32_t)(d[1] * 17) << 8 | (uint32_t)(d[2] * 17);
        } else {
            if (n == 9 && d[6] == 0 && d[7] == 0) return 2;
            *rgb = (uint32_t)(d[0] * 16 + d[1]) << 16 | (uint32_t)(d[2] * 16 + d[3]) << 8 | (uint32_t)(d[4] * 16 + d[5]);
        }
        return 1;
    }
    if (n > 4 && (wb_lc((uint8_t)s[0]) == 'r' || wb_lc((uint8_t)s[0]) == 'h') && wb_lc((uint8_t)s[1]) != 'e') {
        int is_hsl = wb_lc((uint8_t)s[0]) == 'h'; int32_t v[4]; int nv = 0; uint32_t p = 0;
        while (p < n && s[p] != '(') p++;
        if (p >= n) return 0;
        p++;
        while (p < n && nv < 4) {
            int32_t q; uint32_t u;
            while (p < n && (wb_is_space(s[p]) || s[p] == ',' || s[p] == '/')) p++;
            u = wb_css_number(s + p, n - p, &q);
            if (!u) break;
            p += u;
            if (p < n && s[p] == '%') { if (!is_hsl || nv == 3) q = q * 255 / 100; p++; }
            else if (nv == 3) q = q * 255;  //alpha 0..1 -> 0..255 (q is in 1/16)
            v[nv] = (nv == 3) ? q : q / 16;
            nv++;
        }
        if (nv < 3) return 0;
        if (nv == 4 && v[3] / 16 <= 0 && v[3] < 8) return 2;
        if (is_hsl) *rgb = wb_hsl(v[0], v[1], v[2]);
        else {
            int k; for (k = 0; k < 3; k++) { if (v[k] < 0) v[k] = 0; if (v[k] > 255) v[k] = 255; }
            *rgb = (uint32_t)v[0] << 16 | (uint32_t)v[1] << 8 | (uint32_t)v[2];
        }
        return 1;
    }
    if (wb_ieq(s, n, "transparent")) return 2;
    if (wb_ieq(s, n, "currentcolor")) return 3;
    for (i = 0; i < sizeof wb_colors / sizeof wb_colors[0]; i++) if (wb_ieq(s, n, wb_colors[i].n)) { *rgb = wb_colors[i].c; return 1; }
    return 0;
}

static void wb_emit4(wb_emit_fn emit, void *ctx, uint8_t p0, uint32_t v[4], uint8_t imp) {
    emit(ctx, p0, v[0], imp); emit(ctx, (uint8_t)(p0 + 1), v[1], imp); emit(ctx, (uint8_t)(p0 + 2), v[2], imp); emit(ctx, (uint8_t)(p0 + 3), v[3], imp);
}
//top right bottom left from 1..4 values
static void wb_box4(const uint32_t *in, int n, uint32_t out[4]) {
    out[0] = in[0]; out[1] = n > 1 ? in[1] : in[0]; out[2] = n > 2 ? in[2] : in[0]; out[3] = n > 3 ? in[3] : (n > 1 ? in[1] : in[0]);
}
static uint32_t wb_border_width_tok(const eccles_web_vm_t *vm, wb_tok_t t) {
    if (wb_ieq(t.p, t.n, "thin")) return WB_ENC(U_PX, 16);
    if (wb_ieq(t.p, t.n, "medium")) return WB_ENC(U_PX, 48);
    if (wb_ieq(t.p, t.n, "thick")) return WB_ENC(U_PX, 80);
    { uint32_t v = wb_css_value1(vm, t.p, t.n, 0); return (WB_UNIT(v) == U_PX || WB_UNIT(v) == U_NUM) ? v : 0; }
}
static int wb_is_border_style(const char *s, uint32_t n) {
    return wb_ieq(s, n, "solid") || wb_ieq(s, n, "dashed") || wb_ieq(s, n, "dotted") || wb_ieq(s, n, "double") || wb_ieq(s, n, "groove")
        || wb_ieq(s, n, "ridge") || wb_ieq(s, n, "inset") || wb_ieq(s, n, "outset");
}
static uint32_t wb_color_val(int k, uint32_t rgb) { return k == 2 ? WB_ENCK(U_COLOR, WB_NOCOL) : k == 3 ? WB_ENCK(U_KW, A_currentcolor) : WB_ENCK(U_COLOR, rgb); }

//which side (0 top, 1 right, 2 bottom, 3 left) a margin-* / padding-* property is. (atoms are in alphabetical order, so no arithmetic on them)
static int wb_side_of(uint16_t prop) {
    switch (prop) {
        case A_margin_top: case A_padding_top: return 0;
        case A_margin_right: case A_padding_right: return 1;
        case A_margin_bottom: case A_padding_bottom: return 2;
        default: return 3;
    }
}

//one declaration: property name + value text -> emit() once per longhand it sets
static void wb_css_decl(const eccles_web_vm_t *vm, const char *name, uint32_t nlen, const char *val, uint32_t vlen, uint8_t imp, wb_emit_fn emit, void *ctx) {
    wb_tok_t t[8]; int nt; uint16_t prop; uint32_t v, c; int k, i;
    char lname[40]; uint32_t i2;
    if (nlen == 0 || nlen >= sizeof lname) return;
    for (i2 = 0; i2 < nlen; i2++) lname[i2] = (char)wb_lc((uint8_t)name[i2]);
    prop = wb_atom_find(vm, lname, nlen);
    if (!prop) return;
    nt = wb_css_tokens(val, vlen, t, 8);
    if (nt == 0) return;
    if (nt == 1 && (wb_ieq(t[0].p, t[0].n, "inherit") || wb_ieq(t[0].p, t[0].n, "initial") || wb_ieq(t[0].p, t[0].n, "unset"))) {
        uint16_t kw = wb_kw(vm, t[0].p, t[0].n); uint32_t kv = WB_ENCK(U_KW, kw);
        switch (prop) {
            case A_color: emit(ctx, P_COLOR, kv, imp); break;
            case A_font_size: emit(ctx, P_FONT_SIZE, kv, imp); break;
            case A_font_weight: emit(ctx, P_FONT_WEIGHT, kv, imp); break;
            case A_text_align: emit(ctx, P_TEXT_ALIGN, kv, imp); break;
            case A_background_color: case A_background: emit(ctx, P_BG, kv, imp); break;
            default: break;
        }
        return;
    }
    switch (prop) {
        case A_display: {
            uint16_t a = wb_kw(vm, t[0].p, t[0].n); uint16_t d = 0;
            switch (a) {
                case A_none: d = A_none; break; case A_block: case A_table: case A_grid: case A_table_cell: case A_contents: case A_flow_root: d = A_block; break;
                case A_inline: case A_inline_table: d = A_inline; break; case A_inline_block: d = A_inline_block; break;
                case A_flex: case A_inline_flex: case A_table_row: d = A_flex; break; case A_list_item: d = A_list_item; break;
                default: break;
            }
            if (d) emit(ctx, P_DISPLAY, WB_ENCK(U_KW, d), imp);
            return;
        }
        case A_color: k = wb_css_color(t[0].p, t[0].n, &c); if (k) emit(ctx, P_COLOR, k == 2 ? WB_ENCK(U_COLOR, 0) : wb_color_val(k, c), imp); return;
        case A_background_color: k = wb_css_color(t[0].p, t[0].n, &c); if (k) emit(ctx, P_BG, wb_color_val(k, c), imp); return;
        case A_background:
            for (i = 0; i < nt; i++) { k = wb_css_color(t[i].p, t[i].n, &c); if (k) { emit(ctx, P_BG, wb_color_val(k, c), imp); return; } }
            if (wb_ieq(t[0].p, t[0].n, "none")) emit(ctx, P_BG, WB_ENCK(U_COLOR, WB_NOCOL), imp);
            return;
        case A_font_size: {
            static const struct { const char *n; int px; } kws[] = { {"xx-small", 9}, {"x-small", 10}, {"small", 13}, {"medium", 16}, {"large", 18}, {"x-large", 24}, {"xx-large", 32} };
            for (i = 0; i < 7; i++) if (wb_ieq(t[0].p, t[0].n, kws[i].n)) { emit(ctx, P_FONT_SIZE, wb_enc(U_PX, kws[i].px * 16), imp); return; }
            if (wb_ieq(t[0].p, t[0].n, "smaller")) { emit(ctx, P_FONT_SIZE, wb_enc(U_EM, 13), imp); return; }
            if (wb_ieq(t[0].p, t[0].n, "larger")) { emit(ctx, P_FONT_SIZE, wb_enc(U_EM, 19), imp); return; }
            v = wb_css_value1(vm, t[0].p, t[0].n, 0); if (v && WB_UNIT(v) != U_NUM) emit(ctx, P_FONT_SIZE, v, imp); return;
        }
        case A_font_weight: {
            if (wb_ieq(t[0].p, t[0].n, "bold") || wb_ieq(t[0].p, t[0].n, "bolder")) v = wb_enc(U_NUM, 700 * 16);
            else if (wb_ieq(t[0].p, t[0].n, "normal") || wb_ieq(t[0].p, t[0].n, "lighter")) v = wb_enc(U_NUM, 400 * 16);
            else { v = wb_css_value1(vm, t[0].p, t[0].n, 0); if (WB_UNIT(v) != U_NUM) return; }
            emit(ctx, P_FONT_WEIGHT, v, imp); return;
        }
        case A_font_style: emit(ctx, P_FONT_STYLE, wb_ieq(t[0].p, t[0].n, "italic") || wb_ieq(t[0].p, t[0].n, "oblique") ? 1u : 0u, imp); return;
        case A_font_family: {
            int mono = 0;
            for (i = 0; i < nt; i++) if (wb_ieq(t[i].p, t[i].n, "monospace") || wb_ieq(t[i].p, t[i].n, "courier") || wb_ieq(t[i].p, t[i].n, "consolas")
                                        || wb_ieq(t[i].p, t[i].n, "menlo") || wb_ieq(t[i].p, t[i].n, "\"courier new\"")) mono = 1;
            emit(ctx, P_FONT_FAMILY, mono ? 1u : 0u, imp); return;
        }
        case A_text_align: { uint16_t a = wb_kw(vm, t[0].p, t[0].n); if (a == A_left || a == A_center || a == A_right) emit(ctx, P_TEXT_ALIGN, WB_ENCK(U_KW, a), imp); else if (a == A_start) emit(ctx, P_TEXT_ALIGN, WB_ENCK(U_KW, A_left), imp); else if (a == A_end) emit(ctx, P_TEXT_ALIGN, WB_ENCK(U_KW, A_right), imp); return; }
        case A_text_decoration: case A_text_decoration_line: {
            uint32_t m = 0;
            for (i = 0; i < nt; i++) { if (wb_ieq(t[i].p, t[i].n, "underline")) m |= 1; else if (wb_ieq(t[i].p, t[i].n, "line-through")) m |= 2; }
            emit(ctx, P_TEXT_DECO, m, imp); return;
        }
        case A_line_height:
            if (wb_ieq(t[0].p, t[0].n, "normal")) { emit(ctx, P_LINE_HEIGHT, 0, imp); return; }
            v = wb_css_value1(vm, t[0].p, t[0].n, 0); if (v) emit(ctx, P_LINE_HEIGHT, v, imp); return;
        case A_margin: case A_padding: {
            uint32_t in[4], out[4]; int n = 0;
            for (i = 0; i < nt && n < 4; i++) {
                v = wb_css_value1(vm, t[i].p, t[i].n, prop == A_margin);
                if (!v || (WB_UNIT(v) == U_KW && WB_KWOF(v) != A_auto)) return;
                in[n++] = v;
            }
            wb_box4(in, n, out); wb_emit4(emit, ctx, prop == A_margin ? P_MT : P_PT, out, imp); return;
        }
        case A_margin_top: case A_margin_right: case A_margin_bottom: case A_margin_left:
            v = wb_css_value1(vm, t[0].p, t[0].n, 1); if (v && (WB_UNIT(v) != U_KW || WB_KWOF(v) == A_auto)) emit(ctx, (uint8_t)(P_MT + wb_side_of(prop)), v, imp); return;
        case A_padding_top: case A_padding_right: case A_padding_bottom: case A_padding_left:
            v = wb_css_value1(vm, t[0].p, t[0].n, 0); if (v) emit(ctx, (uint8_t)(P_PT + wb_side_of(prop)), v, imp); return;
        case A_border: case A_border_top: case A_border_right: case A_border_bottom: case A_border_left: {
            uint32_t w = 0, bc = 0; int hascolor = 0, none = 0, style = 0;
            for (i = 0; i < nt; i++) {
                if (wb_ieq(t[i].p, t[i].n, "none") || wb_ieq(t[i].p, t[i].n, "hidden")) { none = 1; continue; }
                if (wb_is_border_style(t[i].p, t[i].n)) { style = 1; continue; }
                v = wb_border_width_tok(vm, t[i]); if (v) { w = v; continue; }
                k = wb_css_color(t[i].p, t[i].n, &c); if (k) { bc = wb_color_val(k, c); hascolor = 1; }
            }
            if (none) w = 0; else if (!w && (style || hascolor)) w = wb_enc(U_PX, 16);
            if (prop == A_border) { uint32_t ws[4]; ws[0] = ws[1] = ws[2] = ws[3] = w; wb_emit4(emit, ctx, P_BWT, ws, imp); }
            else emit(ctx, (uint8_t)(P_BWT + (prop == A_border_top ? 0 : prop == A_border_right ? 1 : prop == A_border_bottom ? 2 : 3)), w, imp);
            if (hascolor) emit(ctx, P_BCOLOR, bc, imp);
            return;
        }
        case A_border_width: {
            uint32_t in[4], out[4]; int n = 0;
            for (i = 0; i < nt && n < 4; i++) { v = wb_border_width_tok(vm, t[i]); if (!v && !(t[i].n == 1 && t[i].p[0] == '0')) return; in[n++] = v; }
            wb_box4(in, n, out); wb_emit4(emit, ctx, P_BWT, out, imp); return;
        }
        case A_border_color: k = wb_css_color(t[0].p, t[0].n, &c); if (k) emit(ctx, P_BCOLOR, wb_color_val(k, c), imp); return;
        case A_border_style: if (wb_ieq(t[0].p, t[0].n, "none") || wb_ieq(t[0].p, t[0].n, "hidden")) { uint32_t z[4] = {0, 0, 0, 0}; wb_emit4(emit, ctx, P_BWT, z, imp); } return;
        case A_width: case A_height: case A_max_width: case A_min_width: case A_min_height: case A_max_height: {
            uint8_t pid = prop == A_width ? P_WIDTH : prop == A_height ? P_HEIGHT : prop == A_max_width ? P_MAXW : prop == A_min_width ? P_MINW : prop == A_min_height ? P_MINH : P_MAXH;
            if (wb_ieq(t[0].p, t[0].n, "none") || wb_ieq(t[0].p, t[0].n, "auto")) { emit(ctx, pid, 0, imp); return; }
            v = wb_css_value1(vm, t[0].p, t[0].n, 0); if (v) emit(ctx, pid, v, imp); return;
        }
        case A_flex_direction: { uint16_t a = wb_kw(vm, t[0].p, t[0].n); if (a == A_row || a == A_row_reverse) emit(ctx, P_FLEX_DIR, 0, imp); else if (a == A_column || a == A_column_reverse) emit(ctx, P_FLEX_DIR, 1, imp); return; }
        case A_justify_content: {
            uint16_t a = wb_kw(vm, t[0].p, t[0].n);
            uint32_t j = (a == A_center) ? 1 : (a == A_flex_end || a == A_end || a == A_right) ? 2 : (a == A_space_between) ? 3 : (a == A_space_around || a == A_space_evenly) ? 4 : 0;
            emit(ctx, P_JUSTIFY, j, imp); return;
        }
        case A_align_items: case A_align_self: {
            uint16_t a = wb_kw(vm, t[0].p, t[0].n);
            uint32_t j = (a == A_center) ? 1 : (a == A_flex_end || a == A_end) ? 2 : (a == A_flex_start || a == A_start) ? 3 : 0;  //0 stretch, 3 start
            emit(ctx, prop == A_align_items ? P_ALIGN_ITEMS : P_ALIGN_SELF, j + 1, imp); return;  //stored +1 so 0 = not set
        }
        case A_flex_grow: v = wb_css_value1(vm, t[0].p, t[0].n, 0); if (WB_UNIT(v) == U_NUM) emit(ctx, P_GROW, v, imp); return;
        case A_flex_basis:
            if (wb_ieq(t[0].p, t[0].n, "auto")) { emit(ctx, P_BASIS, 0, imp); return; }
            v = wb_css_value1(vm, t[0].p, t[0].n, 0); if (v && WB_UNIT(v) != U_NUM) emit(ctx, P_BASIS, v, imp); return;
        case A_flex: {
            uint32_t grow = wb_enc(U_NUM, 0), basis = 0; int sawnum = 0, basisset = 0;
            if (wb_ieq(t[0].p, t[0].n, "none")) { emit(ctx, P_GROW, grow, imp); emit(ctx, P_BASIS, 0, imp); return; }
            if (wb_ieq(t[0].p, t[0].n, "auto")) { emit(ctx, P_GROW, wb_enc(U_NUM, 16), imp); emit(ctx, P_BASIS, 0, imp); return; }
            if (wb_ieq(t[0].p, t[0].n, "initial")) { emit(ctx, P_GROW, grow, imp); emit(ctx, P_BASIS, 0, imp); return; }
            for (i = 0; i < nt; i++) {
                v = wb_css_value1(vm, t[i].p, t[i].n, 1); if (!v) return;
                if (WB_UNIT(v) == U_NUM && !sawnum) { grow = v; sawnum = 1; }
                else if (WB_UNIT(v) == U_NUM) { /* shrink: not used */ }
                else { basis = (WB_UNIT(v) == U_KW) ? 0 : v; basisset = 1; }
            }
            if (!basisset && sawnum) basis = wb_enc(U_PCT, 0);  //"flex: 1" means basis 0
            emit(ctx, P_GROW, grow, imp); emit(ctx, P_BASIS, basis ? basis : wb_enc(U_PCT, -1), imp);  //-1% = auto marker
            return;
        }
        case A_gap: case A_row_gap: case A_column_gap: v = wb_css_value1(vm, t[0].p, t[0].n, 0); if (v) emit(ctx, P_GAP, v, imp); return;
        case A_visibility: emit(ctx, P_VISIBILITY, wb_ieq(t[0].p, t[0].n, "hidden") || wb_ieq(t[0].p, t[0].n, "collapse") ? 1u : 0u, imp); return;
        case A_white_space: {
            uint32_t m = wb_ieq(t[0].p, t[0].n, "pre") || wb_ieq(t[0].p, t[0].n, "pre-wrap") || wb_ieq(t[0].p, t[0].n, "pre-line") ? 1u : wb_ieq(t[0].p, t[0].n, "nowrap") ? 2u : 0u;
            emit(ctx, P_WHITE_SPACE, m, imp); return;
        }
        case A_box_sizing: emit(ctx, P_BOXSIZING, wb_ieq(t[0].p, t[0].n, "border-box") ? 1u : 0u, imp); return;
        case A_list_style: case A_list_style_type: emit(ctx, P_LIST_STYLE, wb_ieq(t[0].p, t[0].n, "none") ? 1u : 0u, imp); return;
        default: return;
    }
}

//a declaration block text ("a:b;c:d !important") -> emit()
static void wb_css_decls(const eccles_web_vm_t *vm, const char *s, uint32_t n, wb_emit_fn emit, void *ctx) {
    uint32_t i = 0;
    while (i < n) {
        uint32_t ns, ne, vs, ve; int q = 0, depth = 0; uint8_t imp = 0;
        while (i < n && (wb_is_space(s[i]) || s[i] == ';')) i++;
        ns = i; while (i < n && s[i] != ':' && s[i] != ';') i++;
        if (i >= n || s[i] == ';') continue;  //no colon
        ne = i; while (ne > ns && wb_is_space(s[ne - 1])) ne--;
        i++; vs = i;
        while (i < n) {
            char c = s[i];
            if (q) { if (c == q) q = 0; } else if (c == '"' || c == '\'') q = c; else if (c == '(') depth++; else if (c == ')') { if (depth) depth--; }
            else if (c == ';' && depth == 0) break;
            i++;
        }
        ve = i;
        while (ve > vs && wb_is_space(s[ve - 1])) ve--;
        if (ve - vs >= 10 && wb_ieq(s + ve - 10, 10, "!important")) { imp = 1; ve -= 10; while (ve > vs && wb_is_space(s[ve - 1])) ve--; }
        else if (ve - vs >= 11 && s[ve - 11] == '!' ) { /* "! important" */
            uint32_t k = ve - 9; if (wb_ieq(s + k, 9, "important")) { imp = 1; ve = ve - 9; while (ve > vs && (wb_is_space(s[ve - 1]) || s[ve - 1] == '!')) ve--; }
        }
        wb_css_decl(vm, s + ns, ne - ns, s + vs, ve - vs, imp, emit, ctx);
    }
}

//selectors

//parses one compound (tag / * / #id / .class / :first-child ...) at s[*pi], returns 0 when it uses something we dont support
static int wb_sel_compound(eccles_web_vm_t *vm, const char *s, uint32_t n, uint32_t *pi, wb_comp_t *c, uint32_t *spec) {
    uint32_t i = *pi; int any = 0, ncls = 0;
    memset(c, 0, sizeof *c);
    while (i < n) {
        char ch = s[i]; uint32_t st;
        if (wb_is_space(ch) || ch == '>' || ch == '+' || ch == '~' || ch == ',') break;
        if (ch == '*') { i++; any = 1; continue; }
        if (ch == '#' || ch == '.') {
            uint16_t a; i++; st = i;
            while (i < n && (isalnum_c(s[i]) || s[i] == '-' || s[i] == '_' || (uint8_t)s[i] >= 0x80)) i++;
            if (i == st) return 0;
            a = wb_atom(vm, s + st, i - st); if (!a) return 0;
            if (ch == '#') { if (c->id) return 0; c->id = a; *spec += 0x10000u; }
            else { if (ncls >= 2) return 0; c->cls[ncls++] = a; *spec += 0x100u; }
            any = 1; continue;
        }
        if (ch == ':') {
            st = ++i;
            if (i < n && s[i] == ':') return 0;  //::before etc
            while (i < n && (isalnum_c(s[i]) || s[i] == '-')) i++;
            if (wb_ieq(s + st, i - st, "first-child")) { c->flags |= WB_CF_FIRST; *spec += 0x100u; }
            else if (wb_ieq(s + st, i - st, "last-child")) { c->flags |= WB_CF_LAST; *spec += 0x100u; }
            else if (wb_ieq(s + st, i - st, "root")) { c->tag = A_html; *spec += 0x100u; }
            else if (wb_ieq(s + st, i - st, "link") || wb_ieq(s + st, i - st, "visited")) { *spec += 0x100u; }
            else return 0;  //:hover, :nth-child(...), :not(...) ...
            any = 1; continue;
        }
        if (ch == '[') return 0;
        if (isalnum_c(ch) || ch == '-' || ch == '_') {
            char b[ECCLES_WEB_MAX_NAME + 1]; uint32_t k = 0;
            while (i < n && (isalnum_c(s[i]) || s[i] == '-' || s[i] == '_') && k < ECCLES_WEB_MAX_NAME) b[k++] = (char)wb_lc((uint8_t)s[i++]);
            if (c->tag || any) return 0;
            c->tag = wb_atom(vm, b, k); if (!c->tag) return 0;
            *spec += 1u; any = 1; continue;
        }
        return 0;
    }
    *pi = i;
    return any;
}

//parses one selector (no commas) into a rule skeleton. returns 0 when it uses something unsupported (the whole rule part is then dropped, like a browser does)
static int wb_sel_parse(eccles_web_vm_t *vm, const char *s, uint32_t n, wb_rule_t *r) {
    wb_comp_t L[WB_SEL_DEPTH]; uint8_t comb[WB_SEL_DEPTH]; int nc = 0, i; uint32_t pos = 0, spec = 0;
    memset(r, 0, sizeof *r);
    for (;;) {
        uint8_t cb = 1;
        while (pos < n && wb_is_space(s[pos])) pos++;
        if (pos >= n) break;
        if (s[pos] == '>') { cb = 2; pos++; while (pos < n && wb_is_space(s[pos])) pos++; }
        else if (s[pos] == '+' || s[pos] == '~') return 0;
        if (nc >= WB_SEL_DEPTH) return 0;
        if (!wb_sel_compound(vm, s, n, &pos, &L[nc], &spec)) return 0;
        comb[nc] = cb; nc++;
        if (pos < n && !wb_is_space(s[pos]) && s[pos] != '>') return 0;
    }
    if (nc == 0) return 0;
    r->n = (uint8_t)nc;
    for (i = 0; i < nc; i++) r->c[i] = L[nc - 1 - i];
    for (i = 0; i + 1 < nc; i++) r->combs |= (uint8_t)(comb[nc - 1 - i] << (2 * i));  //link between c[i] and c[i+1] is written before c[i] (comb[nc-1-i])
    r->spec = spec > 0xFFFFFFu ? 0xFFFFFFu : spec;
    return 1;
}

//@media: only the features a phone has an answer for. unknown features count as true so that a page is shown rather than dropped
static int wb_media_part(const eccles_web_vm_t *vm, const char *s, uint32_t n) {
    uint32_t i = 0, ns, ne, vs, ve; int32_t q; uint32_t u;
    while (i < n && wb_is_space(s[i])) i++;
    while (n > i && wb_is_space(s[n - 1])) n--;
    if (i >= n) return 1;
    if (s[i] != '(') {
        if (wb_ieq(s + i, n - i, "print") || wb_ieq(s + i, n - i, "speech")) return 0;
        return 1;  //all, screen, only screen
    }
    i++; ns = i; while (i < n && s[i] != ':' && s[i] != ')') i++;
    ne = i; while (ne > ns && wb_is_space(s[ne - 1])) ne--;
    if (i >= n || s[i] == ')') return 1;  //(color), (hover) ...
    i++; vs = i; while (i < n && s[i] != ')') i++;
    ve = i; while (vs < ve && wb_is_space(s[vs])) vs++;
    while (ve > vs && wb_is_space(s[ve - 1])) ve--;
    if (wb_ieq(s + ns, ne - ns, "orientation")) return wb_ieq(s + vs, ve - vs, "portrait") ? vm->vh >= vm->vw : vm->vh < vm->vw;
    if (wb_ieq(s + ns, ne - ns, "prefers-color-scheme")) return !wb_ieq(s + vs, ve - vs, "dark");
    if (wb_ieq(s + ns, ne - ns, "prefers-reduced-motion")) return wb_ieq(s + vs, ve - vs, "no-preference");
    if (wb_ieq(s + ns, ne - ns, "hover")) return wb_ieq(s + vs, ve - vs, "none");
    if (wb_ieq(s + ns, ne - ns, "pointer")) return wb_ieq(s + vs, ve - vs, "coarse");
    if (wb_ieq(s + ns, ne - ns, "min-width") || wb_ieq(s + ns, ne - ns, "max-width") || wb_ieq(s + ns, ne - ns, "min-height") || wb_ieq(s + ns, ne - ns, "max-height")) {
        int32_t px, lim; int ismin = wb_ieq(s + ns, 3, "min");
        u = wb_css_number(s + vs, ve - vs, &q);
        if (!u) return 1;
        px = q / 16; if (ve - vs > u && wb_lc((uint8_t)s[vs + u]) == 'e') px = px * ECCLES_WEB_DEFAULT_FONT_PX;  //em
        lim = (wb_ieq(s + ns, ne - ns, "min-width") || wb_ieq(s + ns, ne - ns, "max-width")) ? (int32_t)vm->vw : (int32_t)vm->vh;
        return ismin ? lim >= px : lim <= px;
    }
    return 1;
}
static int wb_media_eval(const eccles_web_vm_t *vm, const char *s, uint32_t n) {
    uint32_t i = 0, qs = 0;
    for (;;) {  //comma = or
        uint32_t qe = qs; int ok = 1; int neg = 0; uint32_t ps;
        while (qe < n && s[qe] != ',') qe++;
        ps = qs; while (ps < qe && wb_is_space(s[ps])) ps++;
        if (qe - ps > 4 && wb_ieq(s + ps, 4, "not ")) { neg = 1; ps += 4; }
        i = ps;
        while (i <= qe) {  //parts joined by " and "
            uint32_t pe = i; int depth = 0;
            while (pe < qe) { if (s[pe] == '(') depth++; else if (s[pe] == ')') depth--; else if (!depth && pe + 5 <= qe && s[pe] == ' ' && wb_ieq(s + pe, 5, " and ")) break; pe++; }
            if (!wb_media_part(vm, s + i, pe - i)) ok = 0;
            if (pe >= qe) break;
            i = pe + 5;
        }
        if (neg) ok = !ok;
        if (ok) return 1;
        if (qe >= n) return 0;
        qs = qe + 1;
    }
}

//the style sheet parser: streams from a reader, keeps no copy of the text
typedef struct { eccles_web_vm_t *vm; wb_reader_t *r; int pb; int author; } wb_cp_t;
typedef struct { eccles_web_vm_t *vm; uint16_t d0, cnt; } wb_pool_ctx_t;

static int wb_cp_get(wb_cp_t *p) {  //next char, comments are whitespace
    int c;
    if (p->pb >= 0) { c = p->pb; p->pb = -1; return c; }
    c = wb_rd_get(p->vm, p->r);
    if (c == '/' && wb_rd_peek(p->vm, p->r) == '*') {
        int prev = 0; wb_rd_get(p->vm, p->r);
        while ((c = wb_rd_get(p->vm, p->r)) >= 0) { if (prev == '*' && c == '/') break; prev = c; }
        return ' ';
    }
    return c;
}
static int wb_cp_nonws(wb_cp_t *p) { int c; do { c = wb_cp_get(p); } while (c >= 0 && wb_is_space(c)); return c; }

static void wb_pool_emit(void *ctx, uint8_t prop, uint32_t val, uint8_t imp) {
    wb_pool_ctx_t *c = (wb_pool_ctx_t *)ctx; eccles_web_vm_t *vm = c->vm;
    if (vm->ndecls >= ECCLES_WEB_MAX_DECLS || c->cnt >= 255) return;  //too many: the rest is ignored
    vm->decls[vm->ndecls].prop = prop; vm->decls[vm->ndecls].imp = imp; vm->decls[vm->ndecls].val = val; vm->ndecls++; c->cnt++;
}

//skips up to the matching } (the { is already read)
static void wb_cp_skip_block(wb_cp_t *p) {
    int depth = 1, c, q = 0;
    while (depth > 0 && (c = wb_cp_get(p)) >= 0) {
        if (q) { if (c == q) q = 0; else if (c == '\\') wb_cp_get(p); }
        else if (c == '"' || c == '\'') q = c;
        else if (c == '{') depth++;
        else if (c == '}') depth--;
    }
}

//the declarations of a rule up to the closing }
static void wb_cp_decls(wb_cp_t *p, wb_pool_ctx_t *pc) {
    char nm[40], val[160]; uint32_t nn, vn; int c;
    for (;;) {
        nn = 0; vn = 0;
        c = wb_cp_nonws(p);
        if (c < 0 || c == '}') return;
        if (c == ';') continue;
        while (c >= 0 && c != ':' && c != ';' && c != '}' && c != '{') { if (nn < sizeof nm - 1) nm[nn++] = (char)c; c = wb_cp_get(p); }
        if (c == '{') { wb_cp_skip_block(p); continue; }  //nested rule (css nesting): not supported, skip
        if (c == '}') return;
        if (c == ';' || c < 0) continue;
        while (nn && wb_is_space(nm[nn - 1])) nn--;
        { int depth = 0, q = 0;
          for (;;) {
              c = wb_cp_get(p);
              if (c < 0) break;
              if (q) { if (c == q) q = 0; }
              else if (c == '"' || c == '\'') q = c;
              else if (c == '(') depth++;
              else if (c == ')') { if (depth) depth--; }
              else if (depth == 0 && (c == ';' || c == '}')) break;
              if (vn < sizeof val - 1) val[vn++] = (char)c;
          }
        }
        { uint32_t vs = 0, ve = vn; uint8_t imp = 0;
          while (vs < ve && wb_is_space(val[vs])) vs++;
          while (ve > vs && wb_is_space(val[ve - 1])) ve--;
          if (ve - vs >= 10 && wb_ieq(val + ve - 10, 10, "!important")) { imp = 1; ve -= 10; while (ve > vs && wb_is_space(val[ve - 1])) ve--; }
          wb_css_decl(p->vm, nm, nn, val + vs, ve - vs, imp, wb_pool_emit, pc); }
        if (c == '}' || c < 0) return;
    }
}

static void wb_cp_rules(wb_cp_t *p, int nested);

//a qualified rule: the prelude text is already in pre
static void wb_cp_rule(wb_cp_t *p, const char *pre, uint32_t pn) {
    eccles_web_vm_t *vm = p->vm; wb_rule_t sel[8]; int ns = 0, i; uint32_t st = 0, k; wb_pool_ctx_t pc;
    for (k = 0; k <= pn && ns < 8; k++) {
        if (k == pn || pre[k] == ',') {
            if (wb_sel_parse(vm, pre + st, k - st, &sel[ns])) ns++;
            st = k + 1;
        }
    }
    pc.vm = vm; pc.d0 = vm->ndecls; pc.cnt = 0;
    wb_cp_decls(p, &pc);
    if (ns == 0 || pc.cnt == 0) { vm->ndecls = pc.d0; return; }
    for (i = 0; i < ns; i++) {
        wb_rule_t *r;
        if (vm->nrules >= ECCLES_WEB_MAX_RULES) break;  //too many rules: the rest is ignored
        r = &vm->rules[vm->nrules++]; *r = sel[i]; r->decl0 = pc.d0; r->ndecl = (uint8_t)pc.cnt; r->author = (uint8_t)(p->author ? 1 : 0);
    }
}

static void wb_cp_at(wb_cp_t *p) {
    eccles_web_vm_t *vm = p->vm; char name[16], pre[192]; uint32_t nn = 0, pn = 0; int c, q = 0;
    while ((c = wb_cp_get(p)) >= 0 && (isalnum_c(c) || c == '-')) { if (nn < sizeof name - 1) name[nn++] = (char)c; }
    name[nn] = 0;
    while (c >= 0 && c != '{' && c != ';') {  //prelude
        if (q) { if (c == q) q = 0; } else if (c == '"' || c == '\'') q = c;
        if (pn < sizeof pre - 1) pre[pn++] = (char)c;
        c = wb_cp_get(p);
    }
    if (c == '{') {
        if (wb_ieq(name, nn, "media") && p->vm->css_depth < 3 && wb_media_eval(vm, pre, pn)) { vm->css_depth++; wb_cp_rules(p, 1); vm->css_depth--; }
        else wb_cp_skip_block(p);  //@font-face, @keyframes, @supports, a media block that does not apply
    }
}

static void wb_cp_rules(wb_cp_t *p, int nested) {
    char pre[192]; uint32_t pn; int c;
    for (;;) {
        if (p->vm->err) return;
        c = wb_cp_nonws(p);
        if (c < 0) return;
        if (c == '}') { if (nested) return; continue; }
        if (c == '<') { continue; }  //<!-- --> around old inline sheets
        if (c == '@') { wb_cp_at(p); continue; }
        pn = 0;
        while (c >= 0 && c != '{' && c != ';' && c != '}') { if (pn < sizeof pre - 1) pre[pn++] = (char)c; c = wb_cp_get(p); }
        if (c == '{') wb_cp_rule(p, pre, pn);
        else if (c == '}' && nested) return;
    }
}

static void wb_css_parse(eccles_web_vm_t *vm, wb_reader_t *r, int author) {
    wb_cp_t p; p.vm = vm; p.r = r; p.pb = -1; p.author = author;
    wb_cp_rules(&p, 0);
}

static void wb_css_load_sheet(eccles_web_vm_t *vm, const char *href) {
    char abs[ECCLES_WEB_MAX_URL]; wb_src_t s; wb_reader_t r;
    wb_url_resolve(vm->url, href, abs, sizeof abs);
    if (wb_url_kind(abs) == 2 && !(vm->perms & ECCLES_WEB_PERM_NET)) { wb_deny(vm, "net"); return; }
    if (!wb_open(vm, abs, &s, 1)) { vm->err = ECCLES_WEB_OK; return; }  //a missing style sheet does not fail the page
    wb_rd_stream(&r, wb_src_read, &s);
    wb_css_parse(vm, &r, 1);
    wb_src_close(&s);
}

//built-in user agent sheet. element display is decided in code by tag (wb_default_display), this only has what needs numbers
static const char wb_ua_css[] =
    "body{margin:8px}"
    "h1{font-size:2em;font-weight:bold;margin:.67em 0}h2{font-size:1.5em;font-weight:bold;margin:.83em 0}"
    "h3{font-size:1.17em;font-weight:bold;margin:1em 0}h4{font-weight:bold;margin:1.33em 0}"
    "h5{font-size:.83em;font-weight:bold;margin:1.67em 0}h6{font-size:.67em;font-weight:bold;margin:2.33em 0}"
    "p,dl,pre,ul,ol{margin:1em 0}blockquote{margin:1em 40px}figure{margin:1em 40px}"
    "ul,ol{padding-left:24px}ul ul,ul ol,ol ul,ol ol{margin:0}dd{margin-left:24px}"
    "b,strong,th{font-weight:bold}i,em,cite,var,dfn,address{font-style:italic}u,ins{text-decoration:underline}"
    "s,del,strike{text-decoration:line-through}a{color:#0645ad;text-decoration:underline}"
    "small,sub,sup{font-size:.83em}big{font-size:1.17em}"
    "code,kbd,samp,tt,pre{font-family:monospace}pre{white-space:pre}mark{background-color:#ff0}"
    "center{text-align:center}th{text-align:center}"
    "hr{border-top:1px solid #888;margin:8px 0}"
    "td,th{flex:1;padding:2px 4px}table{margin:0}"
    "button{padding:6px 12px;border:1px solid #888;background-color:#e8e8e8;text-align:center}"
    "input{padding:4px 6px;border:1px solid #888;background-color:#fff}"
    "textarea{padding:4px;border:1px solid #888;background-color:#fff;width:200px;height:80px;font-family:monospace}"
    "select{padding:4px 6px;border:1px solid #888;background-color:#fff}"
    "fieldset{border:1px solid #888;padding:6px;margin:0 2px}";

static void wb_style_init_builtin(eccles_web_vm_t *vm) {
    wb_reader_t r; wb_rd_mem(&r, wb_ua_css, sizeof wb_ua_css - 1);
    wb_css_parse(vm, &r, 0);
}

//matching

static int wb_is_elem(const eccles_web_vm_t *vm, uint16_t n) { return n && vm->nodes[n].type == WB_N_ELEM; }

static int wb_match_comp(const eccles_web_vm_t *vm, uint16_t n, const wb_comp_t *c) {
    const wb_node_t *nd = &vm->nodes[n];
    if (c->tag && nd->tag != c->tag) return 0;
    if (c->id) {
        uint32_t l; const char *v = wb_attr_get(vm, n, A_id, &l);
        if (!v || l != wb_alen(vm, c->id) || memcmp(v, wb_astr(vm, c->id), l)) return 0;
    }
    if (c->cls[0] && !wb_has_class(vm, n, c->cls[0])) return 0;
    if (c->cls[1] && !wb_has_class(vm, n, c->cls[1])) return 0;
    if (c->flags & WB_CF_FIRST) {
        uint16_t s = vm->nodes[nd->parent].child;
        while (s && !wb_is_elem(vm, s)) s = vm->nodes[s].next;
        if (s != n) return 0;
    }
    if (c->flags & WB_CF_LAST) {
        uint16_t s = nd->next;
        while (s && !wb_is_elem(vm, s)) s = vm->nodes[s].next;
        if (s) return 0;
    }
    return 1;
}
static int wb_match_rule(const eccles_web_vm_t *vm, uint16_t n, const wb_rule_t *r) {
    int i; uint16_t cur = n;
    if (!wb_match_comp(vm, n, &r->c[0])) return 0;
    for (i = 0; i + 1 < r->n; i++) {
        uint8_t cb = (uint8_t)((r->combs >> (2 * i)) & 3);
        uint16_t a = vm->nodes[cur].parent;
        if (cb == 2) { if (!wb_is_elem(vm, a) || !wb_match_comp(vm, a, &r->c[i + 1])) return 0; cur = a; }
        else {
            while (wb_is_elem(vm, a) && !wb_match_comp(vm, a, &r->c[i + 1])) a = vm->nodes[a].parent;
            if (!wb_is_elem(vm, a)) return 0;
            cur = a;
        }
    }
    return 1;
}

//cascade: for every property the declaration with the highest (important, inline, author, specificity, source order) wins
typedef struct { uint32_t key[P_COUNT]; uint32_t val[P_COUNT]; } wb_cs_t;
typedef struct { wb_cs_t *cs; uint32_t base; } wb_csx_t;
static void wb_cs_emit(void *ctx, uint8_t prop, uint32_t val, uint8_t imp) {
    wb_csx_t *x = (wb_csx_t *)ctx; uint32_t key = x->base | (imp ? 0x80000000u : 0u);
    if (prop < P_COUNT && key >= x->cs->key[prop]) { x->cs->key[prop] = key; x->cs->val[prop] = val; }
}
static void wb_cascade(const eccles_web_vm_t *vm, uint16_t n, wb_cs_t *cs) {
    uint32_t i, sl; const char *st; wb_csx_t x;
    memset(cs, 0, sizeof *cs); x.cs = cs;
    for (i = 0; i < vm->nrules; i++) {
        const wb_rule_t *r = &vm->rules[i]; uint32_t d;
        if (!wb_match_rule(vm, n, r)) continue;
        x.base = 0x08000000u | (r->author ? 0x02000000u : 0u) | r->spec;
        for (d = 0; d < r->ndecl; d++) { const wb_decl_t *dc = &vm->decls[r->decl0 + d]; wb_cs_emit(&x, dc->prop, dc->val, dc->imp); }
    }
    st = wb_attr_get(vm, n, A_style, &sl);
    if (st && sl) { x.base = 0x08000000u | 0x04000000u | 0x02000000u; wb_css_decls(vm, st, sl, wb_cs_emit, &x); }
}

//4. layout, painting, hit testing
//there is NO render tree. layout stores one wb_box_t per node (see the struct) and nothing else. text is wrapped again while painting and while hit testing
//(same code, `mode` tells what to do with a finished line) so the lines never have to be stored. styles are computed once per layout pass.

//ui backend calls. a backend without text_width gets a rough estimate so layout still works
static uint32_t wb_text_w(uint32_t px, uint32_t fl, const char *s, uint32_t n) {
    uint32_t w = 0;
    if (g_ui.text_width && g_ui.text_width(px, fl & WB_FL_UI_MASK, s, n, &w) == WB_OK) return w;
    return n * px * 11 / 20;
}
static void wb_ui_rect(const eccles_web_vm_t *vm, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    int32_t y1;
    if (w <= 0 || h <= 0 || (rgb & WB_NOCOL) || !g_ui.fill_rect) return;
    y1 = y + h; if (y < 0) y = 0; if (y1 > (int32_t)vm->vh) y1 = (int32_t)vm->vh;
    if (y1 <= y) return;
    g_ui.fill_rect(x, y, (uint32_t)w, (uint32_t)(y1 - y), rgb);
}
static void wb_ui_text(int32_t x, int32_t y, uint32_t px, uint32_t fl, uint32_t rgb, const char *s, uint32_t n) {
    if (n && g_ui.draw_text) g_ui.draw_text(x, y, px, fl & WB_FL_UI_MASK, rgb, s, n);
}

static int32_t wb_line_h(const wb_box_t *b) {
    if (b->lh) return b->lh;
    return (int32_t)b->px * (b->lhp ? b->lhp : ECCLES_WEB_LINE_HEIGHT_PCT) / 100;
}

//inherited style bundle (what a child gets from its parent)
typedef struct { uint32_t fg; uint8_t px, fl, align, vis; uint16_t lh, lhp; } wb_inh_t;
static void wb_inh_default(wb_inh_t *i) { memset(i, 0, sizeof *i); i->px = ECCLES_WEB_DEFAULT_FONT_PX; }
static void wb_inh_from_box(wb_inh_t *i, const wb_box_t *b) { i->fg = b->fg; i->px = b->px; i->fl = b->fl; i->align = b->align; i->vis = b->vis; i->lh = b->lh; i->lhp = b->lhp; }

//computed style of one element, lives on the c stack
typedef struct {
    wb_inh_t in;
    uint8_t  disp, bsz, fdir, justify, aitems, aself, leaf, nomark;
    uint32_t bg, bc;
    int32_t  m[4], p[4]; uint8_t mauto[4]; uint8_t bw[4];  //t r b l
    uint32_t w, h, maxw, minw, minh, maxh;  //raw encoded (0 = auto / none)
    uint16_t grow; uint32_t basis; int32_t gap;
} wb_style_t;

enum { WB_LEAF_NONE = 0, WB_LEAF_IMG, WB_LEAF_TEXTIN, WB_LEAF_BTNIN, WB_LEAF_CHECK, WB_LEAF_SELECT };

static uint8_t wb_default_display(uint16_t t) {
    switch (t) {
        case A_html: case A_body: case A_div: case A_p: case A_section: case A_article: case A_aside: case A_header: case A_footer: case A_nav:
        case A_main: case A_form: case A_fieldset: case A_figure: case A_figcaption: case A_blockquote: case A_address: case A_dl: case A_dt: case A_dd:
        case A_ul: case A_ol: case A_pre: case A_hr: case A_table: case A_thead: case A_tbody: case A_tfoot: case A_center: case A_details: case A_summary:
        case A_h1: case A_h2: case A_h3: case A_h4: case A_h5: case A_h6: case A_caption: case A_td: case A_th: case A_menu: return WB_D_BLOCK;
        case A_tr: return WB_D_FLEX;
        case A_li: return WB_D_LISTITEM;
        case A_button: case A_input: case A_select: case A_textarea: case A_img: return WB_D_INLINEBLOCK;
        case A_head: case A_script: case A_style: case A_title: case A_meta: case A_link: case A_base: case A_noscript: case A_template:
        case A_area: case A_option: case A_datalist: case A_param: case A_source: case A_track: return WB_D_NONE;
        default: return WB_D_INLINE;
    }
}

static int wb_attr_is(const eccles_web_vm_t *vm, uint16_t n, uint16_t name, const char *val) {
    uint32_t l; const char *v = wb_attr_get(vm, n, name, &l);
    return v && wb_ieq(v, l, val);
}
static uint8_t wb_leaf_kind(const eccles_web_vm_t *vm, uint16_t n) {
    switch (vm->nodes[n].tag) {
        case A_img: return WB_LEAF_IMG;
        case A_select: return WB_LEAF_SELECT;
        case A_input:
            if (wb_attr_is(vm, n, A_type, "checkbox") || wb_attr_is(vm, n, A_type, "radio")) return WB_LEAF_CHECK;
            if (wb_attr_is(vm, n, A_type, "submit") || wb_attr_is(vm, n, A_type, "button") || wb_attr_is(vm, n, A_type, "reset")) return WB_LEAF_BTNIN;
            return WB_LEAF_TEXTIN;
        default: return WB_LEAF_NONE;
    }
}

static int32_t wb_len(uint32_t v, int32_t em_px, int32_t pct_base) {
    switch (WB_UNIT(v)) {
        case U_PX: case U_NUM: return WB_NUM(v) / 16;
        case U_EM: return (int32_t)((int64_t)WB_NUM(v) * em_px / 16);
        case U_PCT: return (int32_t)((int64_t)WB_NUM(v) * pct_base / 1600);
        default: return 0;
    }
}
static int wb_is_len(uint32_t v) { uint32_t u = WB_UNIT(v); return u == U_PX || u == U_EM || u == U_PCT || u == U_NUM; }

//cascade + inheritance -> one computed style. pin = what the parent passes down, cw = width percentages refer to
static void wb_style(const eccles_web_vm_t *vm, uint16_t n, const wb_inh_t *pin, int32_t cw, wb_style_t *st) {
    wb_cs_t cs; uint16_t tag = vm->nodes[n].tag; int i; uint32_t v; int32_t px;
    wb_cascade(vm, n, &cs);
    memset(st, 0, sizeof *st);
    st->in = *pin;  //(text-decoration is passed down to the children too, like a browser paints it over them)
    st->disp = wb_default_display(tag);
    if (tag == A_input && wb_attr_is(vm, n, A_type, "hidden")) st->disp = WB_D_NONE;
    if (cs.key[P_DISPLAY]) {
        switch (WB_KWOF(cs.val[P_DISPLAY])) {
            case A_none: st->disp = WB_D_NONE; break; case A_block: st->disp = WB_D_BLOCK; break; case A_inline: st->disp = WB_D_INLINE; break;
            case A_inline_block: st->disp = WB_D_INLINEBLOCK; break; case A_flex: st->disp = WB_D_FLEX; break; case A_list_item: st->disp = WB_D_LISTITEM; break;
            default: break;
        }
    }
    st->leaf = wb_leaf_kind(vm, n);
    //font size first, em units of the other properties use it
    px = pin->px;
    if (cs.key[P_FONT_SIZE]) {
        v = cs.val[P_FONT_SIZE];
        switch (WB_UNIT(v)) {
            case U_PX: px = WB_NUM(v) / 16; break;
            case U_EM: px = (int32_t)((int64_t)WB_NUM(v) * pin->px / 16); break;
            case U_PCT: px = (int32_t)((int64_t)WB_NUM(v) * pin->px / 1600); break;
            default: break;
        }
        if (px < 6) px = 6;
        if (px > 200) px = 200;
    }
    st->in.px = (uint8_t)px;
    if (cs.key[P_COLOR]) { v = cs.val[P_COLOR]; if (WB_UNIT(v) == U_COLOR) st->in.fg = WB_COLOROF(v) & 0xFFFFFFu; }
    if (cs.key[P_FONT_WEIGHT]) { v = cs.val[P_FONT_WEIGHT]; if (WB_UNIT(v) == U_NUM) { if (WB_NUM(v) >= 600 * 16) st->in.fl |= ECCLES_WEB_FONT_BOLD; else st->in.fl &= (uint8_t)~ECCLES_WEB_FONT_BOLD; } }
    if (cs.key[P_FONT_STYLE]) { if (cs.val[P_FONT_STYLE]) st->in.fl |= ECCLES_WEB_FONT_ITALIC; else st->in.fl &= (uint8_t)~ECCLES_WEB_FONT_ITALIC; }
    if (cs.key[P_FONT_FAMILY]) { if (cs.val[P_FONT_FAMILY]) st->in.fl |= ECCLES_WEB_FONT_MONO; else st->in.fl &= (uint8_t)~ECCLES_WEB_FONT_MONO; }
    if (cs.key[P_TEXT_DECO]) {
        st->in.fl &= (uint8_t)~(ECCLES_WEB_FONT_UNDERLINE | ECCLES_WEB_FONT_STRIKE);
        if (cs.val[P_TEXT_DECO] & 1) st->in.fl |= ECCLES_WEB_FONT_UNDERLINE;
        if (cs.val[P_TEXT_DECO] & 2) st->in.fl |= ECCLES_WEB_FONT_STRIKE;
    }
    if (cs.key[P_WHITE_SPACE]) { st->in.fl &= (uint8_t)~(WB_FL_PRE | WB_FL_NOWRAP); if (cs.val[P_WHITE_SPACE] == 1) st->in.fl |= WB_FL_PRE; else if (cs.val[P_WHITE_SPACE] == 2) st->in.fl |= WB_FL_NOWRAP; }
    if (cs.key[P_LIST_STYLE]) { if (cs.val[P_LIST_STYLE]) st->in.fl |= 128u; else st->in.fl &= (uint8_t)~128u; }
    if (cs.key[P_TEXT_ALIGN]) { uint16_t a = WB_KWOF(cs.val[P_TEXT_ALIGN]); st->in.align = a == A_center ? 1 : a == A_right ? 2 : 0; }
    if (cs.key[P_VISIBILITY]) st->in.vis = (uint8_t)cs.val[P_VISIBILITY];
    if (cs.key[P_LINE_HEIGHT]) {
        v = cs.val[P_LINE_HEIGHT]; st->in.lh = 0; st->in.lhp = 0;
        if (v) {
            if (WB_UNIT(v) == U_NUM) st->in.lhp = (uint16_t)((int64_t)WB_NUM(v) * 100 / 16);
            else if (WB_UNIT(v) == U_PCT) st->in.lhp = (uint16_t)(WB_NUM(v) / 16);
            else { int32_t l = wb_len(v, px, px); st->in.lh = (uint16_t)(l < 0 ? 0 : l > 4000 ? 4000 : l); }
        }
    }
    //colors
    st->bg = WB_NOCOL; st->bc = st->in.fg;
    if (cs.key[P_BG]) { v = cs.val[P_BG]; if (WB_UNIT(v) == U_COLOR) st->bg = WB_COLOROF(v); }
    if (cs.key[P_BCOLOR]) { v = cs.val[P_BCOLOR]; if (WB_UNIT(v) == U_COLOR) st->bc = WB_COLOROF(v); }
    //box model
    for (i = 0; i < 4; i++) {
        v = cs.val[P_MT + i]; if (cs.key[P_MT + i]) { if (WB_UNIT(v) == U_KW) st->mauto[i] = 1; else st->m[i] = wb_len(v, px, cw); }
        v = cs.val[P_PT + i]; if (cs.key[P_PT + i]) { st->p[i] = wb_len(v, px, cw); if (st->p[i] < 0) st->p[i] = 0; }
        v = cs.val[P_BWT + i];
        if (cs.key[P_BWT + i]) { int32_t b = wb_len(v, px, cw); if (b == 0 && v != 0 && WB_NUM(v) > 0) b = 1; st->bw[i] = (uint8_t)(b < 0 ? 0 : b > 40 ? 40 : b); }
    }
    st->w = cs.val[P_WIDTH]; st->h = cs.val[P_HEIGHT]; st->maxw = cs.val[P_MAXW]; st->minw = cs.val[P_MINW]; st->minh = cs.val[P_MINH]; st->maxh = cs.val[P_MAXH];
    st->bsz = (uint8_t)cs.val[P_BOXSIZING];
    st->fdir = (uint8_t)cs.val[P_FLEX_DIR]; st->justify = (uint8_t)cs.val[P_JUSTIFY];
    st->aitems = (uint8_t)cs.val[P_ALIGN_ITEMS]; st->aself = (uint8_t)cs.val[P_ALIGN_SELF];
    if (cs.key[P_GROW]) st->grow = (uint16_t)(WB_NUM(cs.val[P_GROW]) < 0 ? 0 : WB_NUM(cs.val[P_GROW]));
    st->basis = cs.val[P_BASIS];
    if (cs.key[P_GAP]) st->gap = wb_len(cs.val[P_GAP], px, cw);
    st->nomark = (st->in.fl & 128u) ? 1 : 0;
    if (st->leaf == WB_LEAF_BTNIN && !(cs.key[P_BG] & 0x02000000u)) { st->bg = 0xE8E8E8u; st->in.align = 1; }  //<input type=submit> looks like a <button>
    //checkboxes / radios are a fixed little square
    if (st->leaf == WB_LEAF_CHECK) { for (i = 0; i < 4; i++) { st->p[i] = 0; st->bw[i] = 1; } st->w = st->h = 0; }
}

//writes the computed style into the node's box (this is what survives the layout)
static void wb_store(wb_box_t *b, const wb_style_t *st) {
    b->fg = st->in.fg; b->px = st->in.px; b->fl = st->in.fl; b->align = st->in.align; b->vis = st->in.vis; b->lh = st->in.lh; b->lhp = st->in.lhp;
    b->bg = st->bg; b->bc = st->bc; b->disp = st->disp; b->res = 1;
    memcpy(b->bw, st->bw, 4);
}

//flow = the line breaker, used by layout (measure), painting and hit testing. A flow runs over a group of inline level siblings
enum { WB_M_MEASURE = 0, WB_M_MAXC, WB_M_PAINT, WB_M_HIT };
typedef struct {
    const char *p; uint16_t len, node; int32_t x; uint16_t w, h; int16_t ml; uint8_t atomic, px, fl, vis; uint32_t fg; uint16_t lh;
} wb_run_t;
typedef struct {
    eccles_web_vm_t *vm; uint8_t mode, sp, align; uint16_t cont;  //cont = the container element (block) whose inline content this is
    int32_t x0, w, y, lx, maxw; int32_t lh0;
    uint8_t spw_px, spw_fl; int32_t spw; uint8_t spw_ok;
    int32_t hx, hy; uint16_t hit;
    int depth;
    int nruns;
    wb_run_t runs[ECCLES_WEB_LINE_RUNS];  //keep this LAST: wb_fl_init clears everything in front of it
} wb_fl_t;

static int32_t wb_layout_elem(eccles_web_vm_t *vm, uint16_t n, const wb_style_t *st, int32_t x, int32_t y, int32_t cw, int32_t forced_w);
static int32_t wb_maxc(eccles_web_vm_t *vm, uint16_t n, const wb_style_t *st, int depth);
static int32_t wb_border_box_maxc(eccles_web_vm_t *vm, uint16_t c, const wb_style_t *st, int32_t cw, int depth);
static void wb_paint_elem(eccles_web_vm_t *vm, uint16_t n);
static uint16_t wb_fl_nodes(wb_fl_t *f, uint16_t first, int top);

static int32_t wb_fl_spw(wb_fl_t *f, uint8_t px, uint8_t fl) {
    if (!f->spw_ok || f->spw_px != px || f->spw_fl != (fl & WB_FL_UI_MASK)) {
        f->spw = (int32_t)wb_text_w(px, fl, " ", 1); f->spw_px = px; f->spw_fl = (uint8_t)(fl & WB_FL_UI_MASK); f->spw_ok = 1;
    }
    return f->spw;
}

//moves a laid out subtree (every box in it)
static void wb_shift(eccles_web_vm_t *vm, uint16_t root, int32_t dx, int32_t dy) {
    uint16_t n = root;
    if (!dx && !dy) return;
    for (;;) {
        wb_box_t *b = &vm->boxes[n];
        b->x = (int16_t)(b->x + dx); b->gx = (int16_t)(b->gx + dx); b->y += dy; b->gy += dy;
        if (vm->nodes[n].child) n = vm->nodes[n].child;
        else {
            while (n != root && !vm->nodes[n].next) n = vm->nodes[n].parent;
            if (n == root) break;
            n = vm->nodes[n].next;
        }
    }
}

//the line is complete: apply alignment, then paint / hit test / place the inline-blocks that are on it
static void wb_fl_flush(wb_fl_t *f, int force) {
    eccles_web_vm_t *vm = f->vm; int i; int32_t lw = 0, lh = 0, off = 0;
    if (f->nruns == 0) { if (force) { f->y += f->lh0; } f->lx = 0; f->sp = 0; return; }
    for (i = 0; i < f->nruns; i++) {
        const wb_run_t *r = &f->runs[i]; int32_t rh = r->atomic ? (int32_t)r->h : (int32_t)r->lh;
        if (rh > lh) lh = rh;
        if (r->x + (int32_t)r->w > lw) lw = r->x + (int32_t)r->w;
    }
    if (f->align == 1) off = (f->w - lw) / 2; else if (f->align == 2) off = f->w - lw;
    if (off < 0) off = 0;
    if (lw > f->maxw) f->maxw = lw;
    if (f->mode != WB_M_MAXC) {
        int32_t top = f->y, sy = f->y - vm->scroll_y;
        for (i = 0; i < f->nruns; i++) {
            wb_run_t *r = &f->runs[i];
            if (f->mode == WB_M_PAINT) {
                if (sy + lh < 0 || sy > (int32_t)vm->vh) continue;  //line is not on screen
                if (r->atomic) { wb_paint_elem(vm, r->node); continue; }
                if (!r->vis) wb_ui_text(f->x0 + off + r->x, sy + (lh - (int32_t)r->lh) + ((int32_t)r->lh - (int32_t)r->px) / 2, r->px, r->fl, r->fg, r->p, r->len);
            } else if (f->mode == WB_M_HIT) {
                if (f->hy >= top && f->hy < top + lh && f->hx >= f->x0 + off + r->x && f->hx < f->x0 + off + r->x + (int32_t)r->w && !f->hit) f->hit = r->node;
            } else if (r->atomic) {  //MEASURE: now the final position of the inline-block is known
                wb_box_t *b = &vm->boxes[r->node];
                wb_shift(vm, r->node, (f->x0 + off + r->x + r->ml) - b->x, (top + (lh - (int32_t)r->h)) - b->y);
            }
        }
    }
    f->y += lh; f->lx = 0; f->nruns = 0; f->sp = 0;
}

static int wb_fl_add(wb_fl_t *f, const wb_run_t *r) {
    if (f->nruns >= ECCLES_WEB_LINE_RUNS) wb_fl_flush(f, 0);
    f->runs[f->nruns++] = *r;
    return 1;
}

static void wb_fl_text(wb_fl_t *f, uint16_t tn) {
    eccles_web_vm_t *vm = f->vm; const wb_node_t *t = &vm->nodes[tn];
    const char *s = wb_text_of(vm, t); uint32_t n = t->tlen, i = 0; uint16_t par = t->parent; const wb_box_t *pb = &vm->boxes[par];
    uint8_t px = pb->px, fl = pb->fl; int32_t lhp = wb_line_h(pb); const char *prev_end = NULL;
    if (par == 0) { pb = &vm->boxes[0]; }
    if (fl & WB_FL_PRE) {
        while (i <= n) {
            uint32_t j = i; wb_run_t r;
            while (j < n && s[j] != '\n') j++;
            if (j > i) {
                memset(&r, 0, sizeof r);
                r.p = s + i; r.len = (uint16_t)(j - i); r.node = par; r.x = f->lx; r.w = (uint16_t)wb_text_w(px, fl, s + i, j - i);
                r.px = px; r.fl = fl; r.fg = pb->fg; r.lh = (uint16_t)lhp; r.vis = pb->vis;
                wb_fl_add(f, &r); f->lx += r.w;
            }
            if (j < n) wb_fl_flush(f, 1); else break;
            i = j + 1;
        }
        return;
    }
    if (n && s[0] == ' ') f->sp = 1;
    while (i < n) {
        uint32_t j; int32_t ww, spw, gap, xpos; wb_run_t *last;
        if (s[i] == ' ') { f->sp = 1; i++; continue; }
        j = i; while (j < n && s[j] != ' ') j++;
        ww = (int32_t)wb_text_w(px, fl, s + i, j - i);
        spw = wb_fl_spw(f, px, fl);
        gap = (f->lx > 0 && f->sp) ? spw : 0;
        if (!(fl & WB_FL_NOWRAP) && f->lx > 0 && f->lx + gap + ww > f->w) { wb_fl_flush(f, 0); gap = 0; }
        if (f->lx == 0) gap = 0;
        xpos = f->lx + gap;
        last = f->nruns ? &f->runs[f->nruns - 1] : NULL;
        if (last && !last->atomic && last->node == par && prev_end && last->p + last->len == prev_end && last->px == px && last->fl == fl) {
            last->len = (uint16_t)((s + j) - last->p); last->w = (uint16_t)(xpos + ww - last->x);
        } else {
            wb_run_t r; memset(&r, 0, sizeof r);
            r.p = s + i; r.len = (uint16_t)(j - i); r.node = par; r.x = xpos; r.w = (uint16_t)ww; r.px = px; r.fl = fl; r.fg = pb->fg; r.lh = (uint16_t)lhp; r.vis = pb->vis;
            if (f->nruns >= ECCLES_WEB_LINE_RUNS) { wb_fl_flush(f, 0); r.x = 0; xpos = 0; }
            wb_fl_add(f, &r);
        }
        f->lx = xpos + ww; f->sp = 0; prev_end = s + j; i = j;
    }
    if (n && s[n - 1] == ' ') f->sp = 1;
}

//size of replaced / form leaf content (no padding, no border) when css gives none
static void wb_leaf_intrinsic(const eccles_web_vm_t *vm, uint16_t n, const wb_style_t *st, int32_t *w, int32_t *h) {
    uint32_t l; const char *a; int32_t lh = WB_LINE_H(st->in.px);
    *w = 0; *h = lh;
    switch (st->leaf) {
        case WB_LEAF_IMG: {
            int32_t aw = 0, ah = 0; uint32_t nw = 0, nh = 0; const char *src;
            a = wb_attr_get(vm, n, A_width, &l); if (a) aw = atoi(a);
            a = wb_attr_get(vm, n, A_height, &l); if (a) ah = atoi(a);
            src = wb_attr_get(vm, n, A_src, &l);
            if ((!aw || !ah) && src && g_ui.image_size && g_ui.image_size(src, &nw, &nh) == WB_OK && nw && nh) {
                if (!aw && !ah) { aw = (int32_t)nw; ah = (int32_t)nh; }
                else if (!aw) aw = ah * (int32_t)nw / (int32_t)nh; else ah = aw * (int32_t)nh / (int32_t)nw;
            }
            if (!aw) aw = ah ? ah : 64;
            if (!ah) ah = aw;
            *w = aw; *h = ah; break;
        }
        case WB_LEAF_TEXTIN: *w = 160; break;
        case WB_LEAF_BTNIN: a = wb_attr_get(vm, n, A_value, &l); *w = a ? (int32_t)wb_text_w(st->in.px, st->in.fl, a, l) : 0; break;
        case WB_LEAF_CHECK: *w = 14; *h = 14; break;
        case WB_LEAF_SELECT: *w = 80; break;
        default: break;
    }
}

//resolves what an atomic inline item (inline-block, image, input) is: border box size, margins. MEASURE lays it out at 0,0
static void wb_fl_atomic(wb_fl_t *f, uint16_t c, int block_in_inline) {
    eccles_web_vm_t *vm = f->vm; wb_style_t st; wb_inh_t pin; wb_box_t *b = &vm->boxes[c]; int32_t aw, ah, ml, mr; wb_run_t r;
    if (f->mode == WB_M_MEASURE || f->mode == WB_M_MAXC) {
        wb_inh_from_box(&pin, &vm->boxes[vm->nodes[c].parent]);
        wb_style(vm, c, &pin, f->w, &st);
        if (block_in_inline) st.disp = st.disp == WB_D_FLEX ? WB_D_FLEX : WB_D_BLOCK;
        wb_store(b, &st);
        ml = st.mauto[3] ? 0 : st.m[3]; mr = st.mauto[1] ? 0 : st.m[1];
        b->ml = (int16_t)ml; b->mr = (int16_t)mr;
        if (f->mode == WB_M_MAXC) {
            int32_t mc = wb_border_box_maxc(vm, c, &st, f->w, f->depth + 1);
            aw = mc; ah = 0;
        } else {
            int32_t avail = f->w - ml - mr; int32_t fw;
            if (block_in_inline) fw = avail;
            else if (st.w && wb_is_len(st.w)) fw = -1;  //css width: layout_elem resolves it
            else { int32_t mc = wb_border_box_maxc(vm, c, &st, f->w, f->depth + 1); fw = mc < avail ? mc : avail; if (fw < 0) fw = 0; }
            if (block_in_inline) { wb_fl_flush(f, 0); ah = wb_layout_elem(vm, c, &st, f->x0, f->y, f->w, -1); f->y += ah; return; }
            ah = wb_layout_elem(vm, c, &st, 0, 0, f->w, fw);
            aw = b->w;
        }
    } else {
        ml = b->ml; mr = b->mr; aw = b->w; ah = (int32_t)b->h;
        if (block_in_inline) { wb_fl_flush(f, 0); if (f->mode == WB_M_PAINT) wb_paint_elem(vm, c); f->y = b->y + (int32_t)b->h; return; }
    }
    if (block_in_inline) {  //MAXC
        if (aw > f->maxw) f->maxw = aw;
        return;
    }
    memset(&r, 0, sizeof r);
    r.atomic = 1; r.node = c; r.w = (uint16_t)(aw + ml + mr); r.h = (uint16_t)ah; r.ml = (int16_t)ml;
    if (f->lx > 0 && f->lx + (int32_t)r.w > f->w && f->mode != WB_M_MAXC) wb_fl_flush(f, 0);
    if (f->lx > 0 && f->sp) f->lx += wb_fl_spw(f, vm->boxes[vm->nodes[c].parent].px, vm->boxes[vm->nodes[c].parent].fl);
    r.x = f->lx; f->lx += r.w; f->sp = 0;
    wb_fl_add(f, &r);
}

//walks inline level siblings starting at `first`. top = 1: this is the group of a block container, stop at the first block level element and return it.
//top = 0: we are inside an inline element, block level children are laid out as full width blocks
static uint16_t wb_fl_nodes(wb_fl_t *f, uint16_t first, int top) {
    eccles_web_vm_t *vm = f->vm; uint16_t c = first;
    int cont_flex = vm->boxes[f->cont].disp == WB_D_FLEX;
    while (c) {
        const wb_node_t *nd = &vm->nodes[c]; wb_box_t *b = &vm->boxes[c];
        if (nd->type == WB_N_TEXT) { wb_fl_text(f, c); c = nd->next; continue; }
        if (nd->type == WB_N_ELEM) {
            uint8_t d;
            if (f->mode == WB_M_MEASURE || f->mode == WB_M_MAXC) {
                if (!b->res) { wb_style_t st; wb_inh_t pin; wb_inh_from_box(&pin, &vm->boxes[nd->parent]); wb_style(vm, c, &pin, f->w, &st); wb_store(b, &st); }
            }
            d = b->disp;
            if (d == WB_D_NONE) { c = nd->next; continue; }
            if (top && !cont_flex && (d == WB_D_BLOCK || d == WB_D_FLEX || d == WB_D_LISTITEM)) return c;
            if (top && cont_flex) { /* every element child of a flex container is an item, the caller does not send them here */ return c; }
            if (nd->tag == A_br) { wb_fl_flush(f, 1); c = nd->next; continue; }
            if (d == WB_D_INLINEBLOCK) { wb_fl_atomic(f, c, 0); c = nd->next; continue; }
            if (d == WB_D_BLOCK || d == WB_D_FLEX || d == WB_D_LISTITEM) { wb_fl_atomic(f, c, 1); c = nd->next; continue; }
            if (nd->child && f->depth < ECCLES_WEB_MAX_DEPTH) { f->depth++; wb_fl_nodes(f, nd->child, 0); f->depth--; }
        }
        c = nd->next;
    }
    return 0;
}

static void wb_fl_init(wb_fl_t *f, eccles_web_vm_t *vm, int mode, uint16_t cont, int32_t x0, int32_t w, int32_t y) {
    memset(f, 0, offsetof(wb_fl_t, runs));
    f->vm = vm; f->mode = (uint8_t)mode; f->cont = cont; f->x0 = x0; f->w = w < 1 ? 1 : w; f->y = y; f->align = vm->boxes[cont].align;
    f->lh0 = wb_line_h(&vm->boxes[cont]);
    if (mode == WB_M_MAXC) f->w = 1 << 24;
}

#if defined(__GNUC__) || defined(__clang__)
  #define WB_NOINLINE __attribute__((noinline))
#else
  #define WB_NOINLINE
#endif

static int32_t wb_layout_blocks(eccles_web_vm_t *vm, uint16_t n, int32_t cx, int32_t cy, int32_t cw);
static int32_t wb_layout_flex(eccles_web_vm_t *vm, uint16_t n, const wb_style_t *st, int32_t cx, int32_t cy, int32_t cw);

//where an inline group that starts at c ends (the first block level sibling, or 0). same classification as the line breaker uses
static uint16_t wb_group_end(const eccles_web_vm_t *vm, uint16_t cont, uint16_t c) {
    int flex = vm->boxes[cont].disp == WB_D_FLEX;
    while (c) {
        const wb_node_t *nd = &vm->nodes[c];
        if (nd->type == WB_N_ELEM) {
            uint8_t d = vm->boxes[c].disp;
            if (flex) return c;
            if (d == WB_D_BLOCK || d == WB_D_FLEX || d == WB_D_LISTITEM) return c;
        }
        c = nd->next;
    }
    return 0;
}
//is this child of cont a block level item (laid out as a box of its own) rather than the start of an inline group
static int wb_is_item(const eccles_web_vm_t *vm, uint16_t cont, uint16_t c) {
    uint8_t d; int flex = vm->boxes[cont].disp == WB_D_FLEX;
    if (vm->nodes[c].type != WB_N_ELEM) return 0;
    d = vm->boxes[c].disp;
    if (d == WB_D_NONE) return 0;
    return flex || d == WB_D_BLOCK || d == WB_D_FLEX || d == WB_D_LISTITEM;
}

//flows over one group: returns the height, or (maxc) the widest line
static WB_NOINLINE int32_t wb_group_run(eccles_web_vm_t *vm, uint16_t cont, uint16_t c, int mode, int32_t x, int32_t y, int32_t w, uint16_t *next, int depth) {
    wb_fl_t f; int32_t r;
    wb_fl_init(&f, vm, mode, cont, x, w, y); f.depth = depth;
    *next = wb_fl_nodes(&f, c, 1);
    wb_fl_flush(&f, 0);
    r = (mode == WB_M_MAXC) ? f.maxw : f.y - y;
    return r;
}
static WB_NOINLINE uint16_t wb_group_hit(eccles_web_vm_t *vm, uint16_t cont, uint16_t c, int32_t x, int32_t y, uint16_t *next) {
    wb_fl_t f; const wb_box_t *b = &vm->boxes[c];
    wb_fl_init(&f, vm, WB_M_HIT, cont, b->gx, b->gw, b->gy); f.hx = x; f.hy = y;
    *next = wb_fl_nodes(&f, c, 1);
    wb_fl_flush(&f, 0);
    return f.hit;
}

//max-content width of an element's CONTENT box (shrink-to-fit and flex items use it)
static int32_t wb_border_box_maxc(eccles_web_vm_t *vm, uint16_t c, const wb_style_t *st, int32_t cw, int depth) {
    int32_t padb = st->p[1] + st->p[3] + st->bw[1] + st->bw[3], w;
    if (st->w && wb_is_len(st->w) && WB_UNIT(st->w) != U_PCT) { w = wb_len(st->w, st->in.px, cw); return st->bsz ? w : w + padb; }
    return wb_maxc(vm, c, st, depth) + padb;
}
static int32_t wb_maxc(eccles_web_vm_t *vm, uint16_t n, const wb_style_t *st, int depth) {
    wb_box_t *b = &vm->boxes[n]; uint16_t c; int32_t best = 0, sum = 0; int row;
    if (depth > ECCLES_WEB_MAX_DEPTH) return 0;
    wb_store(b, st);
    if (st->leaf) { int32_t w, h; wb_leaf_intrinsic(vm, n, st, &w, &h); return w; }
    row = st->disp == WB_D_FLEX && !st->fdir;
    c = vm->nodes[n].child;
    while (c) {
        const wb_node_t *nd = &vm->nodes[c];
        if (nd->type == WB_N_ELEM) {
            wb_style_t cst; wb_inh_t pin; int32_t cwid;
            wb_inh_from_box(&pin, b); wb_style(vm, c, &pin, 0, &cst); wb_store(&vm->boxes[c], &cst);
            if (cst.disp == WB_D_NONE) { c = nd->next; continue; }
            if (st->disp == WB_D_FLEX || cst.disp == WB_D_BLOCK || cst.disp == WB_D_FLEX || cst.disp == WB_D_LISTITEM) {
                cwid = wb_border_box_maxc(vm, c, &cst, 0, depth + 1) + (cst.mauto[1] ? 0 : cst.m[1]) + (cst.mauto[3] ? 0 : cst.m[3]);
                if (row) sum += cwid + (sum ? st->gap : 0); else if (cwid > best) best = cwid;
                c = nd->next; continue;
            }
        }
        { uint16_t next = 0; int32_t gw = wb_group_run(vm, n, c, WB_M_MAXC, 0, 0, 1 << 24, &next, depth + 1);
          if (row) sum += gw + (sum ? st->gap : 0); else if (gw > best) best = gw;
          c = next; if (!c) break; continue; }
    }
    return row ? sum : best;
}

static int32_t wb_gap_of(int32_t a, int32_t b) { return (a >= 0 && b >= 0) ? (a > b ? a : b) : a + b; }

//block flow: children are stacked, runs of inline level children become lines. returns the content height
static int32_t wb_layout_blocks(eccles_web_vm_t *vm, uint16_t n, int32_t cx, int32_t cy, int32_t cw) {
    uint16_t c = vm->nodes[n].child; int32_t y = cy, prev_mb = 0; int first = 1; wb_inh_t pin;
    wb_inh_from_box(&pin, &vm->boxes[n]);
    while (c) {
        const wb_node_t *nd = &vm->nodes[c]; wb_box_t *b = &vm->boxes[c];
        if (nd->type == WB_N_ELEM) {
            wb_style_t st; int32_t ml, mr, bbw, x, h;
            wb_style(vm, c, &pin, cw, &st); wb_store(b, &st);
            if (st.disp == WB_D_NONE) { c = nd->next; continue; }
            if (st.disp == WB_D_BLOCK || st.disp == WB_D_FLEX || st.disp == WB_D_LISTITEM) {
                ml = st.mauto[3] ? 0 : st.m[3]; mr = st.mauto[1] ? 0 : st.m[1];
                y += first ? st.m[0] : wb_gap_of(prev_mb, st.m[0]);
                x = cx + ml;
                if (st.mauto[3] || st.mauto[1]) {  //auto margins: center / right align a box with a width
                    int32_t padb = st.p[1] + st.p[3] + st.bw[1] + st.bw[3];
                    if (st.w && wb_is_len(st.w)) { bbw = wb_len(st.w, st.in.px, cw); if (!st.bsz) bbw += padb; }
                    else bbw = cw - ml - mr;
                    if (st.maxw && wb_is_len(st.maxw)) { int32_t m = wb_len(st.maxw, st.in.px, cw); if (!st.bsz) m += padb; if (bbw > m) bbw = m; }
                    if (st.mauto[3] && st.mauto[1]) x = cx + (cw - bbw) / 2; else if (st.mauto[3]) x = cx + cw - bbw - mr;
                }
                h = wb_layout_elem(vm, c, &st, x, y, cw, -1);
                y += h; prev_mb = st.m[2]; first = 0; c = nd->next; continue;
            }
        }
        {   //an inline group starts here
            uint16_t next = 0; int32_t h = wb_group_run(vm, n, c, WB_M_MEASURE, cx, y, cw, &next, 0);
            if (h > 0 && prev_mb > 0 && !first) { y += prev_mb; h = wb_group_run(vm, n, c, WB_M_MEASURE, cx, y, cw, &next, 0); prev_mb = 0; }
            else if (h > 0) prev_mb = 0;
            vm->boxes[c].gx = (int16_t)cx; vm->boxes[c].gw = (uint16_t)cw; vm->boxes[c].gy = y; vm->boxes[c].gh = (uint32_t)h;
            if (h > 0) first = 0;
            y += h; c = next;
        }
    }
    return (y + prev_mb) - cy;
}

typedef struct { uint16_t node; uint8_t anon, align; int32_t base, size, mt, mb, ml, mr; uint16_t grow; } wb_fi_t;

//flex: one line, row or column. sizes: flex-basis, else width, else max-content. then free space goes to flex-grow, or to justify-content
static int32_t wb_layout_flex(eccles_web_vm_t *vm, uint16_t n, const wb_style_t *st, int32_t cx, int32_t cy, int32_t cw) {
    wb_fi_t it[ECCLES_WEB_MAX_FLEX_ITEMS]; int ni = 0, i, col = st->fdir ? 1 : 0; uint16_t c = vm->nodes[n].child; wb_inh_t pin;
    int32_t total = 0, free_sp, sumgrow = 0, x, rowh = 0, y, gapn = 0;
    wb_inh_from_box(&pin, &vm->boxes[n]);
    while (c && ni < ECCLES_WEB_MAX_FLEX_ITEMS) {
        const wb_node_t *nd = &vm->nodes[c]; wb_fi_t *f = &it[ni];
        memset(f, 0, sizeof *f);
        if (nd->type == WB_N_ELEM) {
            wb_style_t cs; int32_t padb;
            wb_style(vm, c, &pin, cw, &cs); wb_store(&vm->boxes[c], &cs);
            if (cs.disp == WB_D_NONE) { c = nd->next; continue; }
            if (cs.disp == WB_D_INLINE || cs.disp == WB_D_INLINEBLOCK) { cs.disp = WB_D_BLOCK; vm->boxes[c].disp = WB_D_BLOCK; }  //flex items are blockified
            f->node = c; f->ml = cs.mauto[3] ? 0 : cs.m[3]; f->mr = cs.mauto[1] ? 0 : cs.m[1]; f->mt = cs.m[0]; f->mb = cs.m[2]; f->grow = cs.grow; f->align = cs.aself;
            padb = cs.p[1] + cs.p[3] + cs.bw[1] + cs.bw[3];
            if (cs.basis && !(WB_UNIT(cs.basis) == U_PCT && WB_NUM(cs.basis) == -1) && wb_is_len(cs.basis)) { f->base = wb_len(cs.basis, cs.in.px, cw); if (!cs.bsz) f->base += padb; }
            else f->base = wb_border_box_maxc(vm, c, &cs, cw, 1);
            if (!col) total += f->base + f->ml + f->mr;
            c = nd->next;
        } else {
            uint16_t next = 0; int32_t gw = wb_group_run(vm, n, c, WB_M_MAXC, 0, 0, 1 << 24, &next, 1);
            if (gw > 0) { f->node = c; f->anon = 1; f->base = gw; if (!col) total += gw; ni++; sumgrow += 0; }
            c = next; continue;
        }
        sumgrow += f->grow; ni++;
    }
    if (ni == 0) return 0;
    gapn = ni > 1 ? st->gap * (ni - 1) : 0;
    free_sp = cw - total - gapn;
    x = cx; y = cy;
    if (!col) {
        int32_t lead = 0, between = 0;
        for (i = 0; i < ni; i++) it[i].size = it[i].base;
        if (free_sp > 0 && sumgrow > 0) {
            int32_t left = free_sp;
            for (i = 0; i < ni; i++) if (it[i].grow) { int32_t add = (int32_t)((int64_t)free_sp * it[i].grow / sumgrow); it[i].size += add; left -= add; }
            for (i = ni - 1; i >= 0 && left > 0; i--) if (it[i].grow) { it[i].size += left; left = 0; }
        } else if (free_sp > 0) {
            switch (st->justify) {
                case 1: lead = free_sp / 2; break; case 2: lead = free_sp; break;
                case 3: if (ni > 1) between = free_sp / (ni - 1); break;
                case 4: between = free_sp / ni; lead = between / 2; break;
                default: break;
            }
        } else if (free_sp < 0) {  //overflow: shrink in proportion to the size
            int32_t sumb = 0, over = -free_sp; for (i = 0; i < ni; i++) sumb += it[i].base;
            if (sumb > 0) for (i = 0; i < ni; i++) { int32_t sh = (int32_t)((int64_t)over * it[i].base / sumb); it[i].size = it[i].base - sh; if (it[i].size < 0) it[i].size = 0; }
        }
        x += lead;
        for (i = 0; i < ni; i++) {
            wb_fi_t *f = &it[i]; int32_t h;
            if (f->anon) {
                uint16_t next; h = wb_group_run(vm, n, f->node, WB_M_MEASURE, x, y, f->size, &next, 1);
                vm->boxes[f->node].gx = (int16_t)x; vm->boxes[f->node].gw = (uint16_t)f->size; vm->boxes[f->node].gy = y; vm->boxes[f->node].gh = (uint32_t)h;
                x += f->size + st->gap + between;
            } else {
                wb_style_t cs; wb_style(vm, f->node, &pin, cw, &cs); if (cs.disp == WB_D_INLINE || cs.disp == WB_D_INLINEBLOCK) cs.disp = WB_D_BLOCK;
                h = wb_layout_elem(vm, f->node, &cs, x + f->ml, y + f->mt, cw, f->size);
                h += f->mt + f->mb;
                x += f->size + f->ml + f->mr + st->gap + between;
            }
            f->base = h;  //reuse: the height of the item with its margins
            if (h > rowh) rowh = h;
        }
        for (i = 0; i < ni; i++) {  //cross axis alignment
            wb_fi_t *f = &it[i]; uint8_t a = f->align ? f->align : st->aitems; int32_t dy = 0;
            if (f->anon) continue;
            if (a == 2) dy = (rowh - f->base) / 2; else if (a == 3) dy = rowh - f->base;
            else if (a == 0 || a == 1) { wb_box_t *b = &vm->boxes[f->node]; wb_style_t cs; wb_style(vm, f->node, &pin, cw, &cs); if (!cs.h) b->h = (uint32_t)(rowh - f->mt - f->mb > 0 ? rowh - f->mt - f->mb : 0); }
            if (dy) wb_shift(vm, f->node, 0, dy);
        }
        return rowh;
    }
    //column
    for (i = 0; i < ni; i++) {
        wb_fi_t *f = &it[i]; int32_t h, avail = cw - f->ml - f->mr, w = avail, xo = 0; uint8_t a;
        if (f->anon) {
            uint16_t next; h = wb_group_run(vm, n, f->node, WB_M_MEASURE, cx, y, cw, &next, 1);
            vm->boxes[f->node].gx = (int16_t)cx; vm->boxes[f->node].gw = (uint16_t)cw; vm->boxes[f->node].gy = y; vm->boxes[f->node].gh = (uint32_t)h;
            y += h + st->gap; continue;
        }
        {
            wb_style_t cs; wb_style(vm, f->node, &pin, cw, &cs); if (cs.disp == WB_D_INLINE || cs.disp == WB_D_INLINEBLOCK) cs.disp = WB_D_BLOCK;
            a = f->align ? f->align : st->aitems;
            if (a >= 2 && !(cs.w && wb_is_len(cs.w))) { w = f->base < avail ? f->base : avail; xo = a == 2 ? (avail - w) / 2 : a == 3 ? avail - w : 0; }
            else if (cs.w && wb_is_len(cs.w)) w = -1;
            y += f->mt;
            h = wb_layout_elem(vm, f->node, &cs, cx + f->ml + xo, y, cw, w);
            y += h + f->mb + st->gap;
        }
    }
    return ni ? y - cy - st->gap : 0;
}

//lays out one element and everything inside it. x,y = top left of its border box, cw = width of the containing block, forced_w = border box width or -1
static int32_t wb_layout_elem(eccles_web_vm_t *vm, uint16_t n, const wb_style_t *st, int32_t x, int32_t y, int32_t cw, int32_t forced_w) {
    wb_box_t *b = &vm->boxes[n]; int32_t pl = st->p[3], pr = st->p[1], pt = st->p[0], pb = st->p[2], bl = st->bw[3], br = st->bw[1], bt = st->bw[0], bb = st->bw[2];
    int32_t padw = pl + pr + bl + br, padh = pt + pb + bt + bb, bbw, cwid, ch, hh, ml = st->mauto[3] ? 0 : st->m[3], mr = st->mauto[1] ? 0 : st->m[1];
    int32_t lw = 0, lhh = 0; int hasw = st->w && wb_is_len(st->w);
    wb_store(b, st);
    if (st->leaf) wb_leaf_intrinsic(vm, n, st, &lw, &lhh);
    if (forced_w >= 0) bbw = forced_w;
    else if (hasw) { int32_t w = wb_len(st->w, st->in.px, cw); bbw = st->bsz ? w : w + padw; }
    else if (st->leaf) bbw = lw + padw;
    else bbw = cw - ml - mr;
    if (st->maxw && wb_is_len(st->maxw)) { int32_t m = wb_len(st->maxw, st->in.px, cw); if (!st->bsz) m += padw; if (bbw > m) bbw = m; }
    if (st->minw && wb_is_len(st->minw)) { int32_t m = wb_len(st->minw, st->in.px, cw); if (!st->bsz) m += padw; if (bbw < m) bbw = m; }
    if (bbw < padw) bbw = padw;
    cwid = bbw - padw;
    b->x = (int16_t)x; b->y = y; b->w = (uint16_t)(bbw > 65535 ? 65535 : bbw);
    if (st->leaf) ch = lhh;
    else if (st->disp == WB_D_FLEX) ch = wb_layout_flex(vm, n, st, x + bl + pl, y + bt + pt, cwid);
    else ch = wb_layout_blocks(vm, n, x + bl + pl, y + bt + pt, cwid);
    if (st->h && wb_is_len(st->h) && WB_UNIT(st->h) != U_PCT) { int32_t hv = wb_len(st->h, st->in.px, 0); hh = st->bsz ? hv : hv + padh; }
    else hh = ch + padh;
    if (st->minh && wb_is_len(st->minh) && WB_UNIT(st->minh) != U_PCT) { int32_t m = wb_len(st->minh, st->in.px, 0); if (!st->bsz) m += padh; if (hh < m) hh = m; }
    if (st->maxh && wb_is_len(st->maxh) && WB_UNIT(st->maxh) != U_PCT) { int32_t m = wb_len(st->maxh, st->in.px, 0); if (!st->bsz) m += padh; if (hh > m) hh = m; }
    if (hh < padh) hh = padh;
    b->h = (uint32_t)hh;
    return hh;
}

static void wb_layout_page(eccles_web_vm_t *vm) {
    wb_box_t *b0; uint32_t w = vm->vw ? vm->vw : 320; int32_t h;
    memset(vm->boxes, 0, (size_t)vm->nnodes * sizeof(wb_box_t));
    b0 = &vm->boxes[0];
    b0->px = ECCLES_WEB_DEFAULT_FONT_PX; b0->disp = WB_D_BLOCK; b0->bg = WB_NOCOL; b0->bc = WB_NOCOL; b0->res = 1; b0->w = (uint16_t)w;
    h = wb_layout_blocks(vm, 0, 0, 0, (int32_t)w);
    b0->h = (uint32_t)h; vm->page_h = (uint32_t)(h > 0 ? h : 0);
    vm->laid_out = 1;
    { int32_t maxs = (int32_t)vm->page_h - (int32_t)vm->vh; if (maxs < 0) maxs = 0; if (vm->scroll_y > maxs) vm->scroll_y = maxs; if (vm->scroll_y < 0) vm->scroll_y = 0; }
}

//painting

static const char *wb_input_text(const eccles_web_vm_t *vm, uint16_t n, uint32_t *len, int *placeholder) {
    const char *v = wb_attr_get(vm, n, A_value, len); *placeholder = 0;
    if (v && *len) return v;
    v = wb_attr_get(vm, n, A_placeholder, len);
    if (v && *len) { *placeholder = 1; return v; }
    *len = 0; return "";
}

static void wb_paint_children(eccles_web_vm_t *vm, uint16_t n);

static void wb_paint_elem(eccles_web_vm_t *vm, uint16_t n) {
    const wb_box_t *b = &vm->boxes[n]; int32_t sy, x, w, h;
    if (b->disp == WB_D_NONE || !b->res) return;
    sy = b->y - vm->scroll_y; x = b->x; w = b->w; h = (int32_t)b->h;
    if (sy + h < 0 || sy > (int32_t)vm->vh) return;
    if (!b->vis) {
        wb_ui_rect(vm, x, sy, w, h, b->bg);
        if (b->bw[0]) wb_ui_rect(vm, x, sy, w, b->bw[0], b->bc);
        if (b->bw[2]) wb_ui_rect(vm, x, sy + h - b->bw[2], w, b->bw[2], b->bc);
        if (b->bw[3]) wb_ui_rect(vm, x, sy + b->bw[0], b->bw[3], h - b->bw[0] - b->bw[2], b->bc);
        if (b->bw[1]) wb_ui_rect(vm, x + w - b->bw[1], sy + b->bw[0], b->bw[1], h - b->bw[0] - b->bw[2], b->bc);
        switch (wb_leaf_kind(vm, n)) {
            case WB_LEAF_IMG: {
                uint32_t l; const char *src = wb_attr_get(vm, n, A_src, &l);
                if (src && g_ui.draw_image) g_ui.draw_image(src, x + b->bw[3], sy + b->bw[0], (uint32_t)(w - b->bw[3] - b->bw[1] > 0 ? w - b->bw[3] - b->bw[1] : 0), (uint32_t)(h - b->bw[0] - b->bw[2] > 0 ? h - b->bw[0] - b->bw[2] : 0));
                break;
            }
            case WB_LEAF_CHECK:
                if (wb_attr_find(vm, n, A_checked) && w > 6 && h > 6) wb_ui_rect(vm, x + 3, sy + 3, w - 6, h - 6, 0x2255CC);
                break;
            case WB_LEAF_TEXTIN: case WB_LEAF_BTNIN: case WB_LEAF_SELECT: {
                wb_style_t st; wb_inh_t pin; uint32_t l = 0; int ph = 0; const char *t; int32_t tw, tx, ty;
                wb_inh_from_box(&pin, &vm->boxes[vm->nodes[n].parent]); wb_style(vm, n, &pin, w, &st);
                if (st.leaf == WB_LEAF_SELECT) { uint16_t o = vm->nodes[n].child, sel = 0;
                    while (o) { if (vm->nodes[o].tag == A_option) { if (!sel) sel = o; if (wb_attr_find(vm, o, A_selected)) { sel = o; break; } } o = vm->nodes[o].next; }
                    t = ""; if (sel && vm->nodes[sel].child) { const wb_node_t *tn = &vm->nodes[vm->nodes[sel].child]; if (tn->type == WB_N_TEXT) { t = wb_text_of(vm, tn); l = tn->tlen; } }
                } else t = wb_input_text(vm, n, &l, &ph);
                tw = (int32_t)wb_text_w(b->px, b->fl, t, l);
                tx = x + b->bw[3] + st.p[3]; ty = sy + b->bw[0] + st.p[0];
                if (st.leaf == WB_LEAF_BTNIN) tx = x + (w - tw) / 2;
                wb_ui_text(tx, ty + (WB_LINE_H(b->px) - b->px) / 2, b->px, b->fl, ph ? 0x999999u : b->fg, t, l);
                if (st.leaf == WB_LEAF_TEXTIN && vm->focus == n) wb_ui_rect(vm, tx + tw + 1, ty + 1, 1, WB_LINE_H(b->px) - 2, b->fg);
                if (st.leaf == WB_LEAF_SELECT) wb_ui_text(x + w - b->bw[1] - st.p[1] - b->px, ty + (WB_LINE_H(b->px) - b->px) / 2, b->px, b->fl, b->fg, "\xE2\x96\xBE", 3);
                break;
            }
            default: break;
        }
        if (b->disp == WB_D_LISTITEM && !(b->fl & 128u)) {
            char mk[16]; uint16_t par = vm->nodes[n].parent; int len;
            if (vm->nodes[par].tag == A_ol) {
                int k = 1; uint16_t s = vm->nodes[par].child; while (s && s != n) { if (vm->nodes[s].tag == A_li) k++; s = vm->nodes[s].next; }
                len = snprintf(mk, sizeof mk, "%d.", k);
            } else { memcpy(mk, "\xE2\x80\xA2", 3); len = 3; }
            wb_ui_text(x - (int32_t)wb_text_w(b->px, b->fl, mk, (uint32_t)len) - 6, sy + b->bw[0] + (WB_LINE_H(b->px) - b->px) / 2, b->px, b->fl, b->fg, mk, (uint32_t)len);
        }
    }
    if (!wb_leaf_kind(vm, n)) wb_paint_children(vm, n);
}

static WB_NOINLINE void wb_group_paint(eccles_web_vm_t *vm, uint16_t cont, uint16_t c) {
    wb_fl_t f; const wb_box_t *b = &vm->boxes[c]; uint16_t next;
    wb_fl_init(&f, vm, WB_M_PAINT, cont, b->gx, b->gw, b->gy);
    next = wb_fl_nodes(&f, c, 1); (void)next;
    wb_fl_flush(&f, 0);
}

static void wb_paint_children(eccles_web_vm_t *vm, uint16_t n) {
    uint16_t c = vm->nodes[n].child;
    while (c) {
        const wb_node_t *nd = &vm->nodes[c];
        if (nd->type == WB_N_ELEM && vm->boxes[c].disp == WB_D_NONE) { c = nd->next; continue; }
        if (wb_is_item(vm, n, c)) { wb_paint_elem(vm, c); c = nd->next; continue; }
        {
            const wb_box_t *gb = &vm->boxes[c]; uint16_t next = wb_group_end(vm, n, c);
            if (gb->gh && gb->gy + (int32_t)gb->gh > vm->scroll_y && gb->gy < vm->scroll_y + (int32_t)vm->vh) wb_group_paint(vm, n, c);
            c = next;
        }
    }
}

static uint32_t wb_canvas_color(const eccles_web_vm_t *vm) {
    uint16_t h = vm->nodes[0].child;
    while (h && vm->nodes[h].tag != A_html) h = vm->nodes[h].next;
    if (h && !(vm->boxes[h].bg & WB_NOCOL)) return vm->boxes[h].bg;
    if (vm->body_node && !(vm->boxes[vm->body_node].bg & WB_NOCOL)) return vm->boxes[vm->body_node].bg;
    return 0xFFFFFF;
}

static void wb_paint_page(eccles_web_vm_t *vm) {
    if (g_ui.begin) g_ui.begin(vm->vw, vm->vh);
    wb_ui_rect(vm, 0, 0, (int32_t)vm->vw, (int32_t)vm->vh, wb_canvas_color(vm));
    wb_paint_children(vm, 0);
    if (g_ui.end) g_ui.end();
}

//hit testing: the deepest element under (x, y) (page coordinates). 0 = nothing
static uint16_t wb_hit_node(eccles_web_vm_t *vm, uint16_t n, int32_t x, int32_t y) {
    const wb_box_t *b = &vm->boxes[n]; uint16_t c;
    if (n) {
        if (b->disp == WB_D_NONE || !b->res) return 0;
        if (x < b->x || x >= b->x + (int32_t)b->w || y < b->y || y >= b->y + (int32_t)b->h) return 0;
    }
    c = vm->nodes[n].child;
    while (c) {
        const wb_node_t *nd = &vm->nodes[c];
        if (nd->type == WB_N_ELEM && vm->boxes[c].disp == WB_D_NONE) { c = nd->next; continue; }
        if (wb_is_item(vm, n, c)) { uint16_t r = wb_hit_node(vm, c, x, y); if (r) return r; c = nd->next; continue; }
        {
            const wb_box_t *gb = &vm->boxes[c]; uint16_t next = wb_group_end(vm, n, c);
            if (gb->gh && y >= gb->gy && y < gb->gy + (int32_t)gb->gh) {
                uint16_t nx, h = wb_group_hit(vm, n, c, x, y, &nx);
                if (h) { uint16_t r = (h != n && vm->boxes[h].disp == WB_D_INLINEBLOCK) ? wb_hit_node(vm, h, x, y) : 0; return r ? r : h; }  //an inline-block is searched inside, text runs belong to their element
            }
            c = next;
        }
    }
    return n;
}

//5. script engine
#if !ECCLES_WEB_JS
//compiled out: pages still load, scripts are skipped
static void wb_gc_for_nodes(eccles_web_vm_t *vm) { (void)vm; }
static void wb_run_script(eccles_web_vm_t *vm, const char *src, uint32_t len) { (void)vm; (void)src; (void)len; }
static void wb_load_script_url(eccles_web_vm_t *vm, const char *href) { (void)vm; (void)href; }
static void wb_fire_load_events(eccles_web_vm_t *vm) { (void)vm; }
static int  wb_js_dispatch(eccles_web_vm_t *vm, uint16_t node, uint16_t type) { (void)vm; (void)node; (void)type; return 0; }
static void wb_js_tick(eccles_web_vm_t *vm, uint32_t now) { (void)vm; (void)now; }
static int  wb_js_next_timer(const eccles_web_vm_t *vm, uint32_t now, uint32_t *wait) { (void)vm; (void)now; (void)wait; return 0; }
static void wb_js_eval(eccles_web_vm_t *vm, const char *src) { (void)vm; (void)src; wb_fail(vm, ECCLES_WEB_ERR_UNSUPPORTED_FEATURE, 0); }
void eccles_web_set_fetch(eccles_web_fetch_fn fn) { (void)fn; }
void eccles_web_set_wasm(const eccles_web_wasm_t *ops) { (void)ops; }
eccles_web_status_t eccles_web_vm_register_async(eccles_web_vm_t *vm, const char *name, eccles_web_async_fn fn) { (void)vm; (void)name; (void)fn; return ECCLES_WEB_UNSUPPORTED; }
eccles_web_status_t eccles_web_vm_async_resolve(eccles_web_vm_t *vm, uint32_t id, const eccles_web_val_t *v) { (void)vm; (void)id; (void)v; return ECCLES_WEB_UNSUPPORTED; }
eccles_web_status_t eccles_web_vm_async_reject(eccles_web_vm_t *vm, uint32_t id, const char *m) { (void)vm; (void)id; (void)m; return ECCLES_WEB_UNSUPPORTED; }
eccles_web_status_t eccles_web_vm_fetch_respond(eccles_web_vm_t *vm, uint32_t id, uint32_t st, const char *h, const void *b, size_t l) { (void)vm; (void)id; (void)st; (void)h; (void)b; (void)l; return ECCLES_WEB_UNSUPPORTED; }
eccles_web_status_t eccles_web_vm_fetch_fail(eccles_web_vm_t *vm, uint32_t id, const char *m) { (void)vm; (void)id; (void)m; return ECCLES_WEB_UNSUPPORTED; }
eccles_web_status_t eccles_web_vm_wasm_call_import(eccles_web_vm_t *vm, void *i, const char *m, const char *n, const eccles_web_val_t *a, uint32_t na, eccles_web_val_t *r) { (void)vm; (void)i; (void)m; (void)n; (void)a; (void)na; (void)r; return ECCLES_WEB_UNSUPPORTED; }
#else
#include <time.h>

//values: a double is itself, everything else is a nan with a tag in the top 16 bits (all real nans are made canonical so they cant collide)
#define WV_MK(tag, pay) (((uint64_t)(tag) << 48) | (uint64_t)(uint32_t)(pay))
#define WV_T_SPEC 0xFFF9u  //undefined null false true
#define WV_T_ATOM 0xFFFAu  //an interned string, the payload is the atom
#define WV_T_OBJ  0xFFFBu  //a heap object, the payload is its byte offset in the script heap
#define WV_T_NAT  0xFFFCu  //a native function: index in the builtin table, or 0x10000 + host function index
#define WV_T_ELEM 0xFFFDu  //a dom element: the payload is the node index (no heap object needed)
#define WV_T_DOMX 0xFFFEu  //dom helpers (style / classList / document / window ...): kind<<16 | node
#define WV_UNDEF WV_MK(WV_T_SPEC, 0)
#define WV_NULL  WV_MK(WV_T_SPEC, 1)
#define WV_FALSE WV_MK(WV_T_SPEC, 2)
#define WV_TRUE  WV_MK(WV_T_SPEC, 3)
#define WV_TAGOF(v) ((uint32_t)((v) >> 48))
#define WV_PAY(v) ((uint32_t)(v))
#define WV_ISNUM(v) (WV_TAGOF(v) < WV_T_SPEC)
static wv wv_num(double d) { uint64_t b; if (d != d) return 0x7FF8000000000000ull; memcpy(&b, &d, 8); return b; }
static double wv_dbl(wv v) { double d; memcpy(&d, &v, 8); return d; }
static wv wv_bool(int b) { return b ? WV_TRUE : WV_FALSE; }
static wv wv_atom(uint16_t a) { return WV_MK(WV_T_ATOM, a); }
static wv wv_obj(uint32_t off) { return WV_MK(WV_T_OBJ, off); }
static wv wv_elem(uint16_t n) { return WV_MK(WV_T_ELEM, n); }

//heap objects. 8 byte aligned blocks with an 8 byte header, addressed by byte offset (so the heap can live in psram, and a reference is 4 bytes)
enum { H_FREE = 0, H_STR, H_OBJ, H_ARR, H_FUNC, H_ENV, H_BLOCK, H_PROMISE, H_CORO, H_BOUND };
typedef struct { uint32_t units; uint8_t type, mark; uint16_t aux; } wb_hh_t;
typedef struct { wb_hh_t h; uint32_t len; char s[1]; } wb_hstr_t;
typedef struct { wb_hh_t h; uint32_t proto, n, cap, tab; } wb_hobj_t;  //tab: offset of a BLOCK of {key, pad, value} entries
typedef struct { wb_hh_t h; uint32_t len, cap, elems, pad; } wb_harr_t;  //elems: offset of a BLOCK of values
typedef struct { wb_hh_t h; uint32_t proto, env; wv thisv; uint32_t pobj, props; } wb_hfunc_t;  //aux = proto flags (arrow, async..), props = object with static members / expando properties
typedef struct { wb_hh_t h; uint32_t parent, n; wv slots[1]; } wb_henv_t;
typedef struct { wb_hh_t h; uint32_t nat, pad; wv d0, d1; } wb_hbound_t;
typedef struct { wb_hh_t h; uint32_t state, handled; wv value; uint32_t reactions, pad; } wb_hprom_t;  //reactions: offset of an ARR of triples [onFulfilled, onRejected, child]
//a suspended async function: everything needed to continue it after an await
typedef struct { wb_hh_t h; uint32_t proto, env; wv thisv; uint32_t pc, nstack, prom, ntry; } wb_hcoro_t;  //followed by ntry * 3 u32 {pc, sp, env}, then nstack values

#define HHP(vm, off) ((wb_hh_t *)((vm)->jheap + (off)))
#define WB_JHEAP_MIN 64

static uint32_t wb_units(uint32_t payload) { return (8u + payload + 7u) / 8u; }

static void wb_heap_init(eccles_web_vm_t *vm) {
    wb_hh_t *f; uint32_t *next;
    memset(vm->jheap, 0, 16);
    f = HHP(vm, 8); f->units = (vm->jheap_size - 8) / 8; f->type = H_FREE; f->mark = 0;
    next = (uint32_t *)(vm->jheap + 16); *next = 0;
    vm->jfree = 8; vm->jalloc_since = 0; vm->jgc_runs = 0;
}

static void wb_gc(eccles_web_vm_t *vm);

//allocates a zeroed block, runs a collection when the heap is full. returns 0 (and throws) when there is really no room
static uint32_t wb_throw_oom(eccles_web_vm_t *vm);
static uint32_t wb_galloc(eccles_web_vm_t *vm, uint8_t type, uint32_t payload) {
    uint32_t need = wb_units(payload), pass;
    for (pass = 0; pass < 2; pass++) {
        uint32_t cur = vm->jfree, prev = 0;
        while (cur) {
            wb_hh_t *b = HHP(vm, cur); uint32_t *nx = (uint32_t *)(vm->jheap + cur + 8);
            if (b->units >= need) {
                uint32_t rem = b->units - need, nextf = *nx;
                if (rem >= 2) {  //split: the front is ours, the rest stays free
                    uint32_t ro = cur + need * 8; wb_hh_t *r = HHP(vm, ro);
                    r->units = rem; r->type = H_FREE; r->mark = 0; r->aux = 0; *(uint32_t *)(vm->jheap + ro + 8) = nextf;
                    nextf = ro; b->units = need;
                }
                if (prev) *(uint32_t *)(vm->jheap + prev + 8) = nextf; else vm->jfree = nextf;
                b->type = type; b->mark = 0; b->aux = 0;
                memset(vm->jheap + cur + 8, 0, (size_t)b->units * 8 - 8);
                vm->jalloc_since += b->units * 8;
                return cur;
            }
            prev = cur; cur = *nx;
        }
        if (pass == 0) wb_gc(vm);
    }
    return wb_throw_oom(vm);
}

//strings
static int wb_is_str(const eccles_web_vm_t *vm, wv v) {
    if (WV_TAGOF(v) == WV_T_ATOM) return 1;
    return WV_TAGOF(v) == WV_T_OBJ && HHP(vm, WV_PAY(v))->type == H_STR;
}
static const char *wb_str_ptr(const eccles_web_vm_t *vm, wv v, uint32_t *len) {
    if (WV_TAGOF(v) == WV_T_ATOM) { *len = wb_alen(vm, (uint16_t)WV_PAY(v)); return wb_astr(vm, (uint16_t)WV_PAY(v)); }
    { const wb_hstr_t *s = (const wb_hstr_t *)(vm->jheap + WV_PAY(v)); *len = s->len; return s->s; }
}
static wv wb_str_new(eccles_web_vm_t *vm, const char *p, uint32_t n) {
    uint32_t off;
    if (n == 0) return wv_atom(0);  //the empty string is atom 0
    off = wb_galloc(vm, H_STR, 4 + n + 1);
    if (!off) return WV_UNDEF;
    { wb_hstr_t *s = (wb_hstr_t *)(vm->jheap + off); s->len = n; memcpy(s->s, p, n); s->s[n] = 0; }
    return wv_obj(off);
}
static wv wb_str_cstr(eccles_web_vm_t *vm, const char *c) { return wb_str_new(vm, c, (uint32_t)strlen(c)); }

//roots for natives that allocate more than once (the value stack and the globals are always roots)
static void wb_root(eccles_web_vm_t *vm, wv v) {
    if (vm->njroots < sizeof vm->jroots / sizeof vm->jroots[0]) vm->jroots[vm->njroots++] = v;
    else { if (!vm->jtrap) vm->jtrap = (uint8_t)ECCLES_WEB_TRAP_JS_STACK; vm->jroots[vm->njroots - 1] = v; }  //too many: the call is trapped (the page keeps running)
}
static void wb_unroot(eccles_web_vm_t *vm, uint8_t n) { vm->njroots = (uint8_t)(vm->njroots >= n ? vm->njroots - n : 0); }

//exceptions: a thrown value is kept in vm->jexc until a handler takes it
static void wb_throw(eccles_web_vm_t *vm, wv v) { vm->jexc = v; vm->jexc_pending = 1; }
static wv wb_obj_new(eccles_web_vm_t *vm, uint32_t proto);
static void wb_obj_set(eccles_web_vm_t *vm, uint32_t obj, uint16_t key, wv val);
static void wb_throw_err(eccles_web_vm_t *vm, const char *type, const char *msg) {
    wv o, m;
    if (vm->jexc_pending) return;
    o = wb_obj_new(vm, 0);
    if (vm->jexc_pending) return;  //out of memory while making it: that error is already pending
    wb_root(vm, o);
    m = wb_str_cstr(vm, msg);
    if (!vm->jexc_pending) {
        wb_obj_set(vm, WV_PAY(o), A_message, m);
        { uint16_t a = wb_atom(vm, type, (uint32_t)strlen(type)); if (a) wb_obj_set(vm, WV_PAY(o), A_name, wv_atom(a)); }
        wb_throw(vm, o);
    }
    wb_unroot(vm, 1);
}
static uint32_t wb_throw_oom(eccles_web_vm_t *vm) {
    if (!vm->jexc_pending) { vm->jexc = wv_atom(A_OutOfMemory); vm->jexc_pending = 1; }  //a script exception: the page goes on, it is not a failed load
    return 0;
}

//objects: a list of {key atom, value} in a block. objects are small, a linear search beats a hash table for ram and speed here
static wv wb_obj_new(eccles_web_vm_t *vm, uint32_t proto) {
    uint32_t off = wb_galloc(vm, H_OBJ, sizeof(wb_hobj_t) - sizeof(wb_hh_t));
    if (!off) return WV_UNDEF;
    ((wb_hobj_t *)(vm->jheap + off))->proto = proto;
    return wv_obj(off);
}
static int wb_obj_find(const eccles_web_vm_t *vm, uint32_t obj, uint16_t key) {
    const wb_hobj_t *o = (const wb_hobj_t *)(vm->jheap + obj); uint32_t i; const uint8_t *tab;
    if (!o->tab) return -1;
    tab = vm->jheap + o->tab + 8;
    for (i = 0; i < o->n; i++) if (*(const uint32_t *)(tab + (size_t)i * 16) == key) return (int)i;
    return -1;
}
static wv *wb_obj_slot(eccles_web_vm_t *vm, uint32_t obj, int i) {
    wb_hobj_t *o = (wb_hobj_t *)(vm->jheap + obj);
    return (wv *)(vm->jheap + o->tab + 8 + (size_t)i * 16 + 8);
}
static void wb_obj_set(eccles_web_vm_t *vm, uint32_t obj, uint16_t key, wv val) {
    wb_hobj_t *o; int i = wb_obj_find(vm, obj, key);
    if (i >= 0) { *wb_obj_slot(vm, obj, i) = val; return; }
    o = (wb_hobj_t *)(vm->jheap + obj);
    if (o->n >= o->cap) {  //grow the property block
        uint32_t ncap = o->cap ? o->cap * 2 : 4, nt, old;
        wb_root(vm, wv_obj(obj)); wb_root(vm, val);
        nt = wb_galloc(vm, H_BLOCK, ncap * 16);
        val = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 2);
        if (!nt) return;
        o = (wb_hobj_t *)(vm->jheap + obj); old = o->tab;
        if (old) memcpy(vm->jheap + nt + 8, vm->jheap + old + 8, (size_t)o->n * 16);
        o->tab = nt; o->cap = ncap;
    }
    { uint8_t *e = vm->jheap + o->tab + 8 + (size_t)o->n * 16; *(uint32_t *)e = key; *(wv *)(e + 8) = val; o->n++; }
}

//arrays
static wv wb_arr_new(eccles_web_vm_t *vm, uint32_t cap) {
    uint32_t off = wb_galloc(vm, H_ARR, sizeof(wb_harr_t) - sizeof(wb_hh_t));
    if (!off) return WV_UNDEF;
    if (cap) {
        uint32_t el; wb_root(vm, wv_obj(off)); el = wb_galloc(vm, H_BLOCK, cap * 8); wb_unroot(vm, 1);
        if (!el) return WV_UNDEF;
        { wb_harr_t *a = (wb_harr_t *)(vm->jheap + off); a->elems = el; a->cap = cap; }
    }
    return wv_obj(off);
}
static int wb_is_arr(const eccles_web_vm_t *vm, wv v) { return WV_TAGOF(v) == WV_T_OBJ && HHP(vm, WV_PAY(v))->type == H_ARR; }
static wb_harr_t *wb_arr(const eccles_web_vm_t *vm, wv v) { return (wb_harr_t *)(vm->jheap + WV_PAY(v)); }
static wv *wb_arr_el(const eccles_web_vm_t *vm, wv a) { const wb_harr_t *r = wb_arr(vm, a); return r->elems ? (wv *)(vm->jheap + r->elems + 8) : NULL; }
static int wb_arr_push(eccles_web_vm_t *vm, wv a, wv v) {
    wb_harr_t *r = wb_arr(vm, a);
    if (r->len >= r->cap) {
        uint32_t ncap = r->cap ? r->cap * 2 : 4, nb;
        wb_root(vm, a); wb_root(vm, v);
        nb = wb_galloc(vm, H_BLOCK, ncap * 8);
        v = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 2);
        if (!nb) return 0;
        r = wb_arr(vm, a);
        if (r->elems) memcpy(vm->jheap + nb + 8, vm->jheap + r->elems + 8, (size_t)r->len * 8);
        r->elems = nb; r->cap = ncap;
    }
    ((wv *)(vm->jheap + r->elems + 8))[r->len++] = v;
    return 1;
}

//the collector: mark and sweep, non moving. marks use a small explicit stack, when it overflows the heap is rescanned for gray blocks
#define WB_MARKSTACK 64
typedef struct { uint32_t st[WB_MARKSTACK]; uint32_t n; uint8_t overflow; uint8_t ebits[(ECCLES_WEB_MAX_NODES + 7) / 8]; } wb_gcs_t;  //ebits: dom nodes the script can still reach
static void wb_gc_mark_off(eccles_web_vm_t *vm, wb_gcs_t *g, uint32_t off) {
    wb_hh_t *h;
    if (!off) return;
    h = HHP(vm, off);
    if (h->mark) return;
    h->mark = 1;
    if (h->type == H_STR || h->type == H_BLOCK) { h->mark = 2; return; }  //leaves (a block is scanned by its owner)
    if (g->n < WB_MARKSTACK) g->st[g->n++] = off; else g->overflow = 1;
}
static void wb_gc_mark_v(eccles_web_vm_t *vm, wb_gcs_t *g, wv v) {
    uint32_t t = WV_TAGOF(v);
    if (t == WV_T_OBJ) wb_gc_mark_off(vm, g, WV_PAY(v));
    else if (t == WV_T_ELEM || t == WV_T_DOMX) { uint32_t nd = WV_PAY(v) & 0xFFFFu; if (nd < ECCLES_WEB_MAX_NODES) g->ebits[nd >> 3] |= (uint8_t)(1u << (nd & 7)); }
}
static void wb_gc_scan(eccles_web_vm_t *vm, wb_gcs_t *g, uint32_t off) {
    wb_hh_t *h = HHP(vm, off); uint32_t i;
    h->mark = 2;
    switch (h->type) {
        case H_OBJ: { wb_hobj_t *o = (wb_hobj_t *)h; wb_gc_mark_off(vm, g, o->proto);
            if (o->tab) { HHP(vm, o->tab)->mark = 2; for (i = 0; i < o->n; i++) wb_gc_mark_v(vm, g, *wb_obj_slot(vm, off, (int)i)); } break; }
        case H_ARR: { wb_harr_t *a = (wb_harr_t *)h;
            if (a->elems) { HHP(vm, a->elems)->mark = 2; for (i = 0; i < a->len; i++) wb_gc_mark_v(vm, g, ((wv *)(vm->jheap + a->elems + 8))[i]); } break; }
        case H_FUNC: { wb_hfunc_t *f = (wb_hfunc_t *)h; wb_gc_mark_off(vm, g, f->env); wb_gc_mark_v(vm, g, f->thisv); wb_gc_mark_off(vm, g, f->pobj); wb_gc_mark_off(vm, g, f->props); break; }
        case H_ENV: { wb_henv_t *e = (wb_henv_t *)h; wb_gc_mark_off(vm, g, e->parent); for (i = 0; i < e->n; i++) wb_gc_mark_v(vm, g, e->slots[i]); break; }
        case H_BOUND: { wb_hbound_t *b = (wb_hbound_t *)h; wb_gc_mark_v(vm, g, b->d0); wb_gc_mark_v(vm, g, b->d1); break; }
        case H_PROMISE: { wb_hprom_t *p = (wb_hprom_t *)h; wb_gc_mark_v(vm, g, p->value); wb_gc_mark_off(vm, g, p->reactions); break; }
        case H_CORO: { wb_hcoro_t *c = (wb_hcoro_t *)h; uint32_t *t = (uint32_t *)(c + 1); wv *st;
            wb_gc_mark_off(vm, g, c->env); wb_gc_mark_v(vm, g, c->thisv); wb_gc_mark_off(vm, g, c->prom);
            for (i = 0; i < c->ntry; i++) wb_gc_mark_off(vm, g, t[i * 3 + 2]);
            st = (wv *)(t + c->ntry * 3);
            for (i = 0; i < c->nstack; i++) wb_gc_mark_v(vm, g, st[i]);
            break; }
        default: break;
    }
}
static void wb_gc_drain(eccles_web_vm_t *vm, wb_gcs_t *g) {
    for (;;) {
        while (g->n) wb_gc_scan(vm, g, g->st[--g->n]);
        if (!g->overflow) break;
        g->overflow = 0;  //rescan the heap for gray blocks (marked but not scanned yet)
        { uint32_t off = 8;
          while (off < vm->jheap_size) { wb_hh_t *h = HHP(vm, off); if (h->mark == 1 && h->type != H_FREE) { if (g->n < WB_MARKSTACK) g->st[g->n++] = off; else g->overflow = 1; } off += h->units * 8; } }
    }
}

static void wb_gc_roots(eccles_web_vm_t *vm, wb_gcs_t *g);  //dom / timers / promises: section 6
static void wb_dom_reclaim(eccles_web_vm_t *vm, const uint8_t *ebits);

static void wb_gc(eccles_web_vm_t *vm) {
    wb_gcs_t g; uint32_t off, prev = 0; uint32_t i;
    g.n = 0; g.overflow = 0; memset(g.ebits, 0, sizeof g.ebits);
    wb_gc_mark_v(vm, &g, vm->jglobal); wb_gc_mark_v(vm, &g, vm->jexc);
    for (i = 0; i < vm->jsp; i++) wb_gc_mark_v(vm, &g, vm->jstk[i]);
    for (i = 0; i < vm->jnf; i++) { wb_gc_mark_off(vm, &g, vm->jframes[i].env); wb_gc_mark_v(vm, &g, vm->jframes[i].thisv); wb_gc_mark_v(vm, &g, vm->jframes[i].prom); }
    for (i = 0; i < vm->ntry; i++) wb_gc_mark_off(vm, &g, vm->jtry[i].env);
    for (i = 0; i < vm->njroots; i++) wb_gc_mark_v(vm, &g, vm->jroots[i]);
    wb_gc_roots(vm, &g);
    wb_gc_drain(vm, &g);
    wb_dom_reclaim(vm, g.ebits);
    //sweep: free what is unmarked, merge neighbours, rebuild the free list in address order
    vm->jfree = 0;
    off = 8;
    while (off < vm->jheap_size) {
        wb_hh_t *h = HHP(vm, off);
        if (h->type == H_FREE || h->mark == 0) {
            uint32_t run = h->units, nx = off + h->units * 8;
            while (nx < vm->jheap_size) { wb_hh_t *n = HHP(vm, nx); if (n->type == H_FREE || n->mark == 0) { run += n->units; nx += n->units * 8; } else break; }
            h->units = run; h->type = H_FREE; h->mark = 0; h->aux = 0;
            *(uint32_t *)(vm->jheap + off + 8) = 0;
            if (prev) *(uint32_t *)(vm->jheap + prev + 8) = off; else vm->jfree = off;
            prev = off; off = nx;
        } else { h->mark = 0; off += h->units * 8; }
    }
    vm->jgc_runs++; vm->jalloc_since = 0;
}

//script engine: lexer + single pass compiler to bytecode (no ast)
#include <setjmp.h>

enum {
    OP_UNDEF = 0, OP_NULL, OP_TRUE, OP_FALSE, OP_THIS, OP_NUM8, OP_NUM16, OP_NUMF, OP_ATOM,
    OP_POP, OP_DUP, OP_DUP2, OP_SWAP,
    OP_GETL, OP_SETL, OP_GETU, OP_SETU, OP_GETG, OP_SETG, OP_TYPEOFG, OP_VARG,
    OP_GETF, OP_SETF, OP_GETM, OP_GETI, OP_SETI, OP_GETIM,
    OP_CALL, OP_CALLA, OP_NEW, OP_NEWA, OP_RET, OP_RETU,
    OP_JMP, OP_JF, OP_JT, OP_JFK, OP_JTK, OP_JNN, OP_OPTCHK,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_EXP, OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR, OP_USHR,
    OP_EQ, OP_NE, OP_SEQ, OP_SNE, OP_LT, OP_LE, OP_GT, OP_GE, OP_NEG, OP_POS, OP_NOT, OP_BNOT, OP_TYPEOF, OP_VOID, OP_INSTANCEOF, OP_IN, OP_TONUM,
    OP_NEWOBJ, OP_INITF, OP_INITI, OP_NEWARR, OP_PUSHA, OP_SPREADA, OP_CLOSURE,
    OP_TRY, OP_ENDTRY, OP_THROW, OP_KEYS, OP_ITER, OP_FORNEXT, OP_DELF, OP_DELI, OP_AWAIT, OP_TOSTR, OP_OBJSPREAD, OP_REST, OP_SETPROTO
};

enum {
    T_EOF = 0, T_NUM = 256, T_STR, T_TPL, T_ID,
    T_EQ, T_SEQ, T_NE, T_SNE, T_LE, T_GE, T_AND, T_OR, T_NULLISH, T_INC, T_DEC,
    T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ, T_MODEQ, T_BANDEQ, T_BOREQ, T_BXOREQ, T_SHL, T_SHR, T_USHR, T_SHLEQ, T_SHREQ, T_USHREQ,
    T_ARROW, T_ELLIPSIS, T_QDOT, T_EXP, T_EXPEQ, T_ANDEQ, T_OREQ, T_NULLEQ
};

typedef struct { int t; double num; uint16_t atom; uint32_t pos, end; uint8_t nl; } wb_tk_t;
typedef struct { uint32_t pos; wb_tk_t tk; } wb_ls_t;

#define WB_FN_ARROW 1
#define WB_FN_ASYNC 2
#define WB_FN_REST  4
#define WB_FN_CONCISE 8  //arrow with an expression body
#define WB_FN_PENDING 128

#define WB_MAX_LOOPS 6
#define WB_MAX_PATCH 8
#define WB_MAX_HOIST 16

typedef struct { uint32_t brk[WB_MAX_PATCH]; uint32_t cont[WB_MAX_PATCH]; uint8_t nbrk, ncont, try_depth, is_switch; } wb_loop_t;

typedef struct wb_fc_s {
    struct wb_fc_s *parent;
    uint16_t proto, super_atom;  //super_atom: the hidden variable that holds the superclass inside the methods of a class
    uint16_t locals[ECCLES_WEB_JS_MAX_LOCALS]; uint8_t nlocals;  //atoms, 0 = a hidden temp
    uint8_t  nparams, toplevel, is_async, is_arrow;
    int16_t  tmp;  //hidden temp slot, -1 none
    wb_loop_t loops[WB_MAX_LOOPS]; uint8_t nloops, try_depth;
    struct { uint16_t name, proto; } hoist[WB_MAX_HOIST]; uint8_t nhoist;
    uint16_t vars[WB_MAX_HOIST]; uint8_t nvars;  //toplevel var names (defined as undefined in the prologue)
} wb_fc_t;

typedef struct {
    eccles_web_vm_t *vm; const char *s; uint32_t n, pos;
    wb_tk_t tk; wb_fc_t *fc; jmp_buf jb;
    char msg[ECCLES_WEB_MAX_NAME + 1]; uint32_t errpos;
    uint8_t noin, last_kind; uint32_t last_pos; uint16_t class_seq;  //last_kind: 1 local 2 upvalue 3 global 4 field 5 index (for turning a load into a store)
} wb_cc_t;

static void wb_syntax(wb_cc_t *cc, const char *msg) {
    snprintf(cc->msg, sizeof cc->msg, "%s", msg); cc->errpos = cc->tk.pos;
    longjmp(cc->jb, 1);
}

static int wb_idstart(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || (unsigned)c >= 0x80; }
static int wb_idpart(int c) { return wb_idstart(c) || (c >= '0' && c <= '9'); }

static void wb_utf8_put(char *buf, uint32_t *n, uint32_t cp) {
    if (cp < 0x80) buf[(*n)++] = (char)cp;
    else if (cp < 0x800) { buf[(*n)++] = (char)(0xC0 | (cp >> 6)); buf[(*n)++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { buf[(*n)++] = (char)(0xE0 | (cp >> 12)); buf[(*n)++] = (char)(0x80 | ((cp >> 6) & 63)); buf[(*n)++] = (char)(0x80 | (cp & 63)); }
    else { buf[(*n)++] = (char)(0xF0 | (cp >> 18)); buf[(*n)++] = (char)(0x80 | ((cp >> 12) & 63)); buf[(*n)++] = (char)(0x80 | ((cp >> 6) & 63)); buf[(*n)++] = (char)(0x80 | (cp & 63)); }
}

//reads a string literal body (cc->pos is after the quote) into a temp buffer on the stack, interns it
#define WB_STRBUF 256
static uint16_t wb_lex_string(wb_cc_t *cc, char q) {
    char buf[WB_STRBUF]; uint32_t n = 0; uint16_t a;
    for (;;) {
        int c;
        if (cc->pos >= cc->n) wb_syntax(cc, "unterminated string");
        c = (uint8_t)cc->s[cc->pos++];
        if (c == q) break;
        if (c == '\n') wb_syntax(cc, "unterminated string");
        if (c == '\\') {
            if (cc->pos >= cc->n) wb_syntax(cc, "bad escape");
            c = (uint8_t)cc->s[cc->pos++];
            switch (c) {
                case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break; case 'b': c = '\b'; break; case 'f': c = '\f'; break; case 'v': c = '\v'; break; case '0': c = 0; break;
                case '\n': continue;
                case 'x': { int h1 = wb_hex((uint8_t)cc->s[cc->pos]), h2 = wb_hex((uint8_t)cc->s[cc->pos + 1]); if (h1 < 0 || h2 < 0) wb_syntax(cc, "bad \\x escape"); c = h1 * 16 + h2; cc->pos += 2; if (c >= 0x80) { if (n + 4 < WB_STRBUF) wb_utf8_put(buf, &n, (uint32_t)c); continue; } break; }
                case 'u': {
                    uint32_t cp = 0; int k;
                    if (cc->s[cc->pos] == '{') { cc->pos++; while (cc->pos < cc->n && cc->s[cc->pos] != '}') { int h = wb_hex((uint8_t)cc->s[cc->pos++]); if (h < 0) wb_syntax(cc, "bad \\u escape"); cp = cp * 16 + (uint32_t)h; } cc->pos++; }
                    else { for (k = 0; k < 4; k++) { int h = wb_hex((uint8_t)cc->s[cc->pos++]); if (h < 0) wb_syntax(cc, "bad \\u escape"); cp = cp * 16 + (uint32_t)h; } }
                    if (n + 4 < WB_STRBUF) wb_utf8_put(buf, &n, cp);
                    continue;
                }
                default: break;
            }
        }
        if (n + 1 >= WB_STRBUF) wb_syntax(cc, "string literal too long (see ECCLES_WEB docs)");
        buf[n++] = (char)c;
    }
    a = n ? wb_atom(cc->vm, buf, n) : 0;
    if (n && !a) wb_syntax(cc, "too many names (ECCLES_WEB_MAX_ATOMS)");
    return a;
}

//a template literal token is the whole `...${...}...` span (nested braces, strings and templates are skipped over)
static void wb_skip_template(wb_cc_t *cc) {
    for (;;) {
        int c;
        if (cc->pos >= cc->n) wb_syntax(cc, "unterminated template");
        c = (uint8_t)cc->s[cc->pos++];
        if (c == '`') return;
        if (c == '\\') { cc->pos++; continue; }
        if (c == '$' && cc->pos < cc->n && cc->s[cc->pos] == '{') {
            int depth = 1; cc->pos++;
            while (depth > 0) {
                if (cc->pos >= cc->n) wb_syntax(cc, "unterminated template");
                c = (uint8_t)cc->s[cc->pos++];
                if (c == '{') depth++; else if (c == '}') depth--;
                else if (c == '"' || c == '\'') { while (cc->pos < cc->n && (uint8_t)cc->s[cc->pos] != c) { if (cc->s[cc->pos] == '\\') cc->pos++; cc->pos++; } cc->pos++; }
                else if (c == '`') { cc->pos--; cc->pos++; wb_skip_template(cc); }
            }
        }
    }
}

static void wb_next(wb_cc_t *cc) {
    const char *s = cc->s; uint32_t n = cc->n, p; wb_tk_t *t = &cc->tk; int c;
    t->nl = 0;
    for (;;) {  //whitespace and comments
        while (cc->pos < n && (s[cc->pos] == ' ' || s[cc->pos] == '\t' || s[cc->pos] == '\r' || s[cc->pos] == '\n' || s[cc->pos] == '\f' || s[cc->pos] == '\v')) { if (s[cc->pos] == '\n') t->nl = 1; cc->pos++; }
        if (cc->pos + 1 < n && s[cc->pos] == '/' && s[cc->pos + 1] == '/') { while (cc->pos < n && s[cc->pos] != '\n') cc->pos++; continue; }
        if (cc->pos + 1 < n && s[cc->pos] == '/' && s[cc->pos + 1] == '*') { cc->pos += 2; while (cc->pos + 1 < n && !(s[cc->pos] == '*' && s[cc->pos + 1] == '/')) { if (s[cc->pos] == '\n') t->nl = 1; cc->pos++; } cc->pos += 2; continue; }
        if (cc->pos + 3 < n && s[cc->pos] == '<' && !strncmp(s + cc->pos, "<!--", 4)) { while (cc->pos < n && s[cc->pos] != '\n') cc->pos++; continue; }  //old school html comment in scripts
        break;
    }
    t->pos = cc->pos; t->atom = 0;
    if (cc->pos >= n) { t->t = T_EOF; t->end = cc->pos; return; }
    c = (uint8_t)s[cc->pos]; p = cc->pos;
    if (wb_idstart(c)) {
        while (cc->pos < n && wb_idpart((uint8_t)s[cc->pos])) cc->pos++;
        t->atom = wb_atom(cc->vm, s + p, cc->pos - p);
        if (!t->atom) wb_syntax(cc, "too many names (ECCLES_WEB_MAX_ATOMS)");
        t->t = T_ID; t->end = cc->pos; return;
    }
    if ((c >= '0' && c <= '9') || (c == '.' && cc->pos + 1 < n && s[cc->pos + 1] >= '0' && s[cc->pos + 1] <= '9')) {
        char tmp[40]; uint32_t k = 0; double d;
        if (c == '0' && cc->pos + 1 < n && (s[cc->pos + 1] == 'x' || s[cc->pos + 1] == 'X' || s[cc->pos + 1] == 'b' || s[cc->pos + 1] == 'B' || s[cc->pos + 1] == 'o' || s[cc->pos + 1] == 'O')) {
            int base = (s[cc->pos + 1] | 32) == 'x' ? 16 : (s[cc->pos + 1] | 32) == 'b' ? 2 : 8; d = 0; cc->pos += 2;
            while (cc->pos < n) { int h = wb_hex((uint8_t)s[cc->pos]); if (s[cc->pos] == '_') { cc->pos++; continue; } if (h < 0 || h >= base) break; d = d * base + h; cc->pos++; }
        } else {
            while (cc->pos < n && ((s[cc->pos] >= '0' && s[cc->pos] <= '9') || s[cc->pos] == '.' || s[cc->pos] == '_')) { if (s[cc->pos] != '_' && k < 38) tmp[k++] = s[cc->pos]; cc->pos++; }
            if (cc->pos < n && (s[cc->pos] == 'e' || s[cc->pos] == 'E')) {
                tmp[k++] = 'e'; cc->pos++;
                if (cc->pos < n && (s[cc->pos] == '+' || s[cc->pos] == '-')) tmp[k++] = s[cc->pos++];
                while (cc->pos < n && s[cc->pos] >= '0' && s[cc->pos] <= '9' && k < 38) tmp[k++] = s[cc->pos++];
            }
            tmp[k] = 0; d = strtod(tmp, NULL);
        }
        t->t = T_NUM; t->num = d; t->end = cc->pos; return;
    }
    if (c == '"' || c == '\'') { cc->pos++; t->atom = wb_lex_string(cc, (char)c); t->t = T_STR; t->end = cc->pos; return; }
    if (c == '`') { cc->pos++; wb_skip_template(cc); t->t = T_TPL; t->end = cc->pos; return; }
    cc->pos++;
    switch (c) {
        case '=': if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_SEQ; } else t->t = T_EQ; t->end = cc->pos; return; }
                  if (cc->pos < n && s[cc->pos] == '>') { cc->pos++; t->t = T_ARROW; t->end = cc->pos; return; } break;
        case '!': if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_SNE; } else t->t = T_NE; t->end = cc->pos; return; } break;
        case '<': if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_LE; t->end = cc->pos; return; }
                  if (cc->pos < n && s[cc->pos] == '<') { cc->pos++; if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_SHLEQ; } else t->t = T_SHL; t->end = cc->pos; return; } break;
        case '>': if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_GE; t->end = cc->pos; return; }
                  if (cc->pos < n && s[cc->pos] == '>') { cc->pos++;
                      if (cc->pos < n && s[cc->pos] == '>') { cc->pos++; if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_USHREQ; } else t->t = T_USHR; }
                      else if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_SHREQ; } else t->t = T_SHR;
                      t->end = cc->pos; return; } break;
        case '&': if (cc->pos < n && s[cc->pos] == '&') { cc->pos++; if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_ANDEQ; } else t->t = T_AND; t->end = cc->pos; return; }
                  if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_BANDEQ; t->end = cc->pos; return; } break;
        case '|': if (cc->pos < n && s[cc->pos] == '|') { cc->pos++; if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_OREQ; } else t->t = T_OR; t->end = cc->pos; return; }
                  if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_BOREQ; t->end = cc->pos; return; } break;
        case '?': if (cc->pos < n && s[cc->pos] == '?') { cc->pos++; if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_NULLEQ; } else t->t = T_NULLISH; t->end = cc->pos; return; }
                  if (cc->pos + 1 < n && s[cc->pos] == '.' && !(s[cc->pos + 1] >= '0' && s[cc->pos + 1] <= '9')) { cc->pos++; t->t = T_QDOT; t->end = cc->pos; return; } break;
        case '+': if (cc->pos < n && s[cc->pos] == '+') { cc->pos++; t->t = T_INC; t->end = cc->pos; return; } if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_ADDEQ; t->end = cc->pos; return; } break;
        case '-': if (cc->pos < n && s[cc->pos] == '-') { cc->pos++; t->t = T_DEC; t->end = cc->pos; return; } if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_SUBEQ; t->end = cc->pos; return; } break;
        case '*': if (cc->pos < n && s[cc->pos] == '*') { cc->pos++; if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_EXPEQ; } else t->t = T_EXP; t->end = cc->pos; return; }
                  if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_MULEQ; t->end = cc->pos; return; } break;
        case '/': if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_DIVEQ; t->end = cc->pos; return; } break;
        case '%': if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_MODEQ; t->end = cc->pos; return; } break;
        case '^': if (cc->pos < n && s[cc->pos] == '=') { cc->pos++; t->t = T_BXOREQ; t->end = cc->pos; return; } break;
        case '.': if (cc->pos + 1 < n && s[cc->pos] == '.' && s[cc->pos + 1] == '.') { cc->pos += 2; t->t = T_ELLIPSIS; t->end = cc->pos; return; } break;
        default: break;
    }
    t->t = c; t->end = cc->pos;
}

static void wb_ls_save(const wb_cc_t *cc, wb_ls_t *ls) { ls->pos = cc->pos; ls->tk = cc->tk; }
static void wb_ls_restore(wb_cc_t *cc, const wb_ls_t *ls) { cc->pos = ls->pos; cc->tk = ls->tk; }
static int wb_isid(const wb_cc_t *cc, uint16_t atom) { return cc->tk.t == T_ID && cc->tk.atom == atom; }

//code emission
static void wb_e8(wb_cc_t *cc, uint8_t b) {
    eccles_web_vm_t *vm = cc->vm;
    if (vm->jcode_len >= ECCLES_WEB_JS_CODE_SIZE) wb_syntax(cc, "script too big (ECCLES_WEB_JS_CODE_SIZE)");
    vm->jcode[vm->jcode_len++] = b;
}
static void wb_op(wb_cc_t *cc, uint8_t op) { wb_e8(cc, op); cc->last_kind = 0; }
static void wb_e16(wb_cc_t *cc, uint16_t v) { wb_e8(cc, (uint8_t)v); wb_e8(cc, (uint8_t)(v >> 8)); }
static void wb_op16(wb_cc_t *cc, uint8_t op, uint16_t v) { wb_op(cc, op); wb_e16(cc, v); }
static uint32_t wb_cur(const wb_cc_t *cc) { return cc->vm->jcode_len; }
static uint32_t wb_jmp(wb_cc_t *cc, uint8_t op) { wb_op(cc, op); wb_e16(cc, 0); return wb_cur(cc) - 2; }  //returns the operand position to patch
static void wb_patch_to(wb_cc_t *cc, uint32_t at, uint32_t target) {
    int32_t off = (int32_t)target - (int32_t)(at + 2);
    if (off > 32767 || off < -32768) wb_syntax(cc, "jump too far (function too big)");
    cc->vm->jcode[at] = (uint8_t)off; cc->vm->jcode[at + 1] = (uint8_t)((uint16_t)off >> 8);
}
static void wb_patch(wb_cc_t *cc, uint32_t at) { wb_patch_to(cc, at, wb_cur(cc)); }
static void wb_jmp_back(wb_cc_t *cc, uint8_t op, uint32_t target) { uint32_t at = wb_jmp(cc, op); wb_patch_to(cc, at, target); }

static void wb_emit_num(wb_cc_t *cc, double d) {
    if (d == (double)(int8_t)d && !(d == 0 && 1.0 / d < 0)) { wb_op(cc, OP_NUM8); wb_e8(cc, (uint8_t)(int8_t)d); }
    else if (d == (double)(int16_t)d && !(d == 0 && 1.0 / d < 0)) { wb_op(cc, OP_NUM16); wb_e16(cc, (uint16_t)(int16_t)d); }
    else { uint8_t b[8]; int i; memcpy(b, &d, 8); wb_op(cc, OP_NUMF); for (i = 0; i < 8; i++) wb_e8(cc, b[i]); }
}

//scopes
static int wb_find_local(const wb_fc_t *f, uint16_t atom) { int i; for (i = f->nlocals - 1; i >= 0; i--) if (f->locals[i] == atom) return i; return -1; }
static int wb_add_local(wb_cc_t *cc, uint16_t atom) {
    wb_fc_t *f = cc->fc;
    if (f->nlocals >= ECCLES_WEB_JS_MAX_LOCALS) wb_syntax(cc, "too many variables in one function (ECCLES_WEB_JS_MAX_LOCALS)");
    f->locals[f->nlocals] = atom; return f->nlocals++;
}
//declares a variable of the current function. returns the slot, -1 when it is a global (toplevel code)
static int wb_declare(wb_cc_t *cc, uint16_t atom) {
    wb_fc_t *f = cc->fc; int i;
    if (f->toplevel) {
        for (i = 0; i < f->nvars; i++) if (f->vars[i] == atom) return -1;
        if (f->nvars < WB_MAX_HOIST) f->vars[f->nvars++] = atom;
        return -1;
    }
    i = wb_find_local(f, atom);
    return i >= 0 ? i : wb_add_local(cc, atom);
}
static int wb_tmp(wb_cc_t *cc) { if (cc->fc->tmp < 0) cc->fc->tmp = (int16_t)wb_add_local(cc, 0); return cc->fc->tmp; }
static int wb_resolve(const wb_cc_t *cc, uint16_t atom, uint8_t *hops, uint8_t *slot) {
    const wb_fc_t *f = cc->fc; uint8_t h = 0;
    while (f) { int i = wb_find_local(f, atom); if (i >= 0) { *hops = h; *slot = (uint8_t)i; return 1; } f = f->parent; h++; }
    return 0;
}

static void wb_emit_load(wb_cc_t *cc, uint16_t atom) {
    uint8_t h, s;
    cc->last_pos = wb_cur(cc);
    if (wb_resolve(cc, atom, &h, &s)) {
        if (h == 0) { wb_op(cc, OP_GETL); wb_e8(cc, s); cc->last_kind = 1; }
        else { wb_op(cc, OP_GETU); wb_e8(cc, h); wb_e8(cc, s); cc->last_kind = 2; }
    } else { wb_op16(cc, OP_GETG, atom); cc->last_kind = 3; }
}
//assignment to a name: value on the stack stays there (it is the value of the expression)
static void wb_emit_store(wb_cc_t *cc, uint16_t atom) {
    uint8_t h, s;
    if (wb_resolve(cc, atom, &h, &s)) {
        if (h == 0) { wb_op(cc, OP_SETL); wb_e8(cc, s); } else { wb_op(cc, OP_SETU); wb_e8(cc, h); wb_e8(cc, s); }
    } else wb_op16(cc, OP_SETG, atom);
}

//the lexer helpers
static void wb_expect(wb_cc_t *cc, int t, const char *msg) { if (cc->tk.t != t) wb_syntax(cc, msg); wb_next(cc); }
static int wb_eat(wb_cc_t *cc, int t) { if (cc->tk.t == t) { wb_next(cc); return 1; } return 0; }
static void wb_semi(wb_cc_t *cc) {
    if (cc->tk.t == ';') { wb_next(cc); return; }
    if (cc->tk.t == '}' || cc->tk.t == T_EOF || cc->tk.nl) return;
    wb_syntax(cc, "expected ;");
}

static void wb_expr(wb_cc_t *cc);
static void wb_assign(wb_cc_t *cc);
static void wb_stmt(wb_cc_t *cc);
static void wb_block_body(wb_cc_t *cc);
static void wb_compile_proto(wb_cc_t *cc, uint16_t idx, wb_fc_t *parent);
static void wb_class(wb_cc_t *cc, int is_decl);

//skips one bracketed group: the current token is ( [ or {, afterwards the token after the closing bracket is current
static void wb_skip_group(wb_cc_t *cc) {
    int depth = 0;
    do {
        if (cc->tk.t == T_EOF) wb_syntax(cc, "unbalanced brackets");
        if (cc->tk.t == '(' || cc->tk.t == '[' || cc->tk.t == '{') depth++;
        else if (cc->tk.t == ')' || cc->tk.t == ']' || cc->tk.t == '}') depth--;
        wb_next(cc);
    } while (depth > 0);
}
//skips a concise arrow body (an expression)
static void wb_skip_expr(wb_cc_t *cc) {
    int depth = 0;
    while (cc->tk.t != T_EOF) {
        int t = cc->tk.t;
        if (depth == 0 && (t == ',' || t == ')' || t == ']' || t == '}' || t == ';')) break;
        if (depth == 0 && cc->tk.nl && t == T_ID) {
            uint16_t a = cc->tk.atom;
            if (a == A_const || a == A_let || a == A_var || a == A_function || a == A_if || a == A_for || a == A_while || a == A_return || a == A_class || a == A_async) break;
        }
        if (t == '(' || t == '[' || t == '{') depth++; else if (t == ')' || t == ']' || t == '}') depth--;
        wb_next(cc);
    }
}

//a function expression / arrow / method: it is NOT compiled now. Its source position is remembered, the body is compiled after the enclosing function
//is finished (that way every variable of the outer function is already known and the code of each function stays contiguous)
static void wb_make_func(wb_cc_t *cc, uint8_t flags, int arrow) {
    eccles_web_vm_t *vm = cc->vm; uint16_t idx;
    if (vm->nprotos >= ECCLES_WEB_JS_MAX_FUNCS) wb_syntax(cc, "too many functions (ECCLES_WEB_JS_MAX_FUNCS)");
    idx = vm->nprotos++;
    memset(&vm->protos[idx], 0, sizeof vm->protos[idx]);
    vm->protos[idx].code = cc->tk.pos;  //the source position, until it is compiled
    if (arrow) {
        if (cc->tk.t == T_ID) wb_next(cc); else wb_skip_group(cc);
        wb_expect(cc, T_ARROW, "expected =>");
        if (cc->tk.t == '{') wb_skip_group(cc); else { flags |= WB_FN_CONCISE; wb_skip_expr(cc); }
        flags |= WB_FN_ARROW;
    } else {
        wb_skip_group(cc);  //(params)
        if (cc->tk.t != '{') wb_syntax(cc, "expected {");
        wb_skip_group(cc);
    }
    vm->protos[idx].flags = (uint8_t)(flags | WB_FN_PENDING);
    wb_op16(cc, OP_CLOSURE, idx);
}

//looks ahead: is the ( at the current token the start of an arrow function's parameter list
static int wb_arrow_ahead(wb_cc_t *cc) {
    wb_ls_t ls; int depth = 0, r = 0;
    wb_ls_save(cc, &ls);
    do {
        if (cc->tk.t == T_EOF) break;
        if (cc->tk.t == '(') depth++; else if (cc->tk.t == ')') depth--;
        wb_next(cc);
    } while (depth > 0);
    if (depth == 0 && cc->tk.t == T_ARROW) r = 1;
    wb_ls_restore(cc, &ls);
    return r;
}

//template literals
static uint32_t wb_tpl_expr_end(const wb_cc_t *cc, uint32_t p) {  //p is after "${": returns the position of the matching }
    int depth = 1;
    while (p < cc->n) {
        char c = cc->s[p];
        if (c == '{') depth++;
        else if (c == '}') { if (--depth == 0) return p; }
        else if (c == '"' || c == '\'') { p++; while (p < cc->n && cc->s[p] != c) { if (cc->s[p] == '\\') p++; p++; } }
        else if (c == '`') { p++; while (p < cc->n && cc->s[p] != '`') { if (cc->s[p] == '\\') p++; else if (cc->s[p] == '$' && p + 1 < cc->n && cc->s[p + 1] == '{') { p = wb_tpl_expr_end(cc, p + 2); } p++; } }
        p++;
    }
    return cc->n;
}
static void wb_template(wb_cc_t *cc) {
    uint32_t p = cc->tk.pos + 1, e = cc->tk.end - 1; char buf[WB_STRBUF]; uint32_t n; uint16_t a; int first = 1;
    wb_tk_t savetk = cc->tk; uint32_t savepos = cc->pos, saven = cc->n;
    for (;;) {
        n = 0;
        while (p < e && !(cc->s[p] == '$' && p + 1 < e && cc->s[p + 1] == '{')) {
            char c = cc->s[p++];
            if (c == '\\' && p < e) { c = cc->s[p++]; switch (c) { case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break; case '0': c = 0; break; default: break; } }
            if (n + 1 >= WB_STRBUF) wb_syntax(cc, "template text too long");
            buf[n++] = c;
        }
        a = n ? wb_atom(cc->vm, buf, n) : 0;
        if (n && !a) wb_syntax(cc, "too many names (ECCLES_WEB_MAX_ATOMS)");
        if (first || a) { wb_op16(cc, OP_ATOM, a); if (!first) wb_op(cc, OP_ADD); }
        first = 0;
        if (p >= e) break;
        {   //${ expression }
            uint32_t xs = p + 2, xe = wb_tpl_expr_end(cc, xs);
            cc->n = xe; cc->pos = xs; wb_next(cc);
            wb_expr(cc);
            if (cc->tk.t != T_EOF) wb_syntax(cc, "bad template expression");
            cc->n = saven; p = xe + 1;
            wb_op(cc, OP_ADD);
        }
    }
    cc->tk = savetk; cc->pos = savepos; cc->n = saven;
    wb_next(cc);
}

static void wb_object_literal(wb_cc_t *cc) {
    wb_next(cc);  //{
    wb_op(cc, OP_NEWOBJ);
    while (cc->tk.t != '}') {
        uint16_t key = 0; int computed = 0, isasync = 0;
        if (cc->tk.t == T_ELLIPSIS) { wb_next(cc); wb_assign(cc); wb_op(cc, OP_OBJSPREAD); if (cc->tk.t == ',') wb_next(cc); continue; }
        if (wb_isid(cc, A_async)) { wb_ls_t ls; wb_ls_save(cc, &ls); wb_next(cc); if (cc->tk.t == ':' || cc->tk.t == ',' || cc->tk.t == '(' || cc->tk.t == '}') wb_ls_restore(cc, &ls); else isasync = 1; }
        if (cc->tk.t == '[') { wb_next(cc); wb_assign(cc); wb_expect(cc, ']', "expected ]"); computed = 1; }
        else if (cc->tk.t == T_ID || cc->tk.t == T_STR) { key = cc->tk.atom; wb_next(cc); }
        else if (cc->tk.t == T_NUM) { char b[40]; int l = snprintf(b, sizeof b, "%.15g", cc->tk.num); key = wb_atom(cc->vm, b, (uint32_t)l); wb_next(cc); }
        else wb_syntax(cc, "bad object key");
        if (cc->tk.t == ':') {
            wb_next(cc); wb_assign(cc);
            if (computed) wb_op(cc, OP_INITI); else wb_op16(cc, OP_INITF, key);
        } else if (cc->tk.t == '(') {  //method shorthand
            if (computed) wb_syntax(cc, "computed methods are not supported");
            wb_make_func(cc, (uint8_t)(isasync ? WB_FN_ASYNC : 0), 0); wb_op16(cc, OP_INITF, key);
        } else {  //shorthand { a }
            if (computed || !key) wb_syntax(cc, "bad object literal");
            wb_emit_load(cc, key); wb_op16(cc, OP_INITF, key);
        }
        if (cc->tk.t == ',') wb_next(cc); else if (cc->tk.t != '}') wb_syntax(cc, "expected , or }");
    }
    wb_next(cc);
}

static void wb_array_literal(wb_cc_t *cc) {
    wb_next(cc);  //[
    wb_op(cc, OP_NEWARR);
    while (cc->tk.t != ']') {
        if (cc->tk.t == ',') { wb_op(cc, OP_UNDEF); wb_op(cc, OP_PUSHA); wb_next(cc); continue; }  //hole
        if (cc->tk.t == T_ELLIPSIS) { wb_next(cc); wb_assign(cc); wb_op(cc, OP_SPREADA); }
        else { wb_assign(cc); wb_op(cc, OP_PUSHA); }
        if (cc->tk.t == ',') wb_next(cc); else if (cc->tk.t != ']') wb_syntax(cc, "expected , or ]");
    }
    wb_next(cc);
}

//call arguments. returns 1 when a spread made it an array call (OP_CALLA / OP_NEWA), else the count in *n
static int wb_args(wb_cc_t *cc, int *n) {
    uint32_t start = wb_cur(cc); int count = 0, spread = 0; wb_ls_t ls;
    wb_next(cc);  //(
    wb_ls_save(cc, &ls);
    //look for a spread first (cheap scan of the argument list) so that the right code is emitted from the start
    { int depth = 1; while (cc->tk.t != T_EOF && depth > 0) { if (cc->tk.t == '(' || cc->tk.t == '[' || cc->tk.t == '{') depth++; else if (cc->tk.t == ')' || cc->tk.t == ']' || cc->tk.t == '}') depth--; else if (depth == 1 && cc->tk.t == T_ELLIPSIS) spread = 1; wb_next(cc); } }
    wb_ls_restore(cc, &ls);
    (void)start;
    if (spread) wb_op(cc, OP_NEWARR);
    while (cc->tk.t != ')') {
        if (spread) { if (cc->tk.t == T_ELLIPSIS) { wb_next(cc); wb_assign(cc); wb_op(cc, OP_SPREADA); } else { wb_assign(cc); wb_op(cc, OP_PUSHA); } }
        else { wb_assign(cc); if (++count > ECCLES_WEB_JS_MAX_ARGS * 4) wb_syntax(cc, "too many arguments"); }
        if (cc->tk.t == ',') wb_next(cc); else if (cc->tk.t != ')') wb_syntax(cc, "expected , or )");
    }
    wb_next(cc);
    *n = count;
    return spread;
}

static void wb_primary(wb_cc_t *cc) {
    wb_tk_t t = cc->tk;
    switch (t.t) {
        case T_NUM: wb_emit_num(cc, t.num); wb_next(cc); return;
        case T_STR: wb_op16(cc, OP_ATOM, t.atom); wb_next(cc); return;
        case T_TPL: wb_template(cc); return;
        case '(':
            if (wb_arrow_ahead(cc)) { wb_make_func(cc, 0, 1); return; }
            wb_next(cc); wb_expr(cc); wb_expect(cc, ')', "expected )"); cc->last_kind = 0; return;
        case '[': wb_array_literal(cc); return;
        case '{': wb_object_literal(cc); return;
        case '/': wb_syntax(cc, "regular expressions are not supported");  /* (never returns) */
        /* fallthrough */
        case T_ID: {
            uint16_t a = t.atom;
            if (a == A_function) {
                wb_next(cc);
                if (cc->tk.t == '*') wb_syntax(cc, "generators are not supported");
                if (cc->tk.t == T_ID) wb_next(cc);  //name of a function expression (not bound inside)
                wb_make_func(cc, 0, 0); return;
            }
            if (a == A_async) {
                wb_ls_t ls; wb_ls_save(cc, &ls); wb_next(cc);
                if (wb_isid(cc, A_function)) { wb_next(cc); if (cc->tk.t == T_ID) wb_next(cc); wb_make_func(cc, WB_FN_ASYNC, 0); return; }
                if (!cc->tk.nl && ((cc->tk.t == '(' && wb_arrow_ahead(cc)) || cc->tk.t == T_ID)) {
                    if (cc->tk.t == T_ID) { wb_ls_t l2; wb_ls_save(cc, &l2); wb_next(cc); if (cc->tk.t == T_ARROW) { wb_ls_restore(cc, &l2); wb_make_func(cc, WB_FN_ASYNC, 1); return; } }
                    else { wb_make_func(cc, WB_FN_ASYNC, 1); return; }
                }
                wb_ls_restore(cc, &ls);
            }
            if (a == A_this) { wb_op(cc, OP_THIS); wb_next(cc); return; }
            if (a == A_null) { wb_op(cc, OP_NULL); wb_next(cc); return; }
            if (a == A_true) { wb_op(cc, OP_TRUE); wb_next(cc); return; }
            if (a == A_false) { wb_op(cc, OP_FALSE); wb_next(cc); return; }
            if (a == A_undefined) { wb_op(cc, OP_UNDEF); wb_next(cc); return; }
            if (a == A_class) { wb_class(cc, 0); return; }
            if (a == A_super) {  //super(...) and super.method(...) inside the methods of a class
                uint16_t sa = cc->fc->super_atom; int n = 0, spread;
                if (!sa) wb_syntax(cc, "super is only valid inside a class method");
                wb_next(cc);
                if (cc->tk.t == '(') {
                    wb_emit_load(cc, sa); wb_op(cc, OP_THIS);
                    spread = wb_args(cc, &n); if (spread) wb_op(cc, OP_CALLA); else { wb_op(cc, OP_CALL); wb_e8(cc, (uint8_t)n); }
                    cc->last_kind = 0; return;
                }
                if (cc->tk.t == '.') {
                    uint16_t m; wb_next(cc); if (cc->tk.t != T_ID) wb_syntax(cc, "expected a method name");
                    m = cc->tk.atom; wb_next(cc);
                    wb_emit_load(cc, sa); wb_op16(cc, OP_GETF, A_prototype); wb_op16(cc, OP_GETF, m);
                    if (cc->tk.t == '(') {
                        wb_op(cc, OP_THIS);
                        spread = wb_args(cc, &n); if (spread) wb_op(cc, OP_CALLA); else { wb_op(cc, OP_CALL); wb_e8(cc, (uint8_t)n); }
                    }
                    cc->last_kind = 0; return;
                }
                wb_syntax(cc, "bad use of super");
            }
            if (a == A_new) {
                int n = 0, spread;
                wb_next(cc);
                if (wb_isid(cc, A_class)) wb_syntax(cc, "bad new");
                wb_primary(cc);  //the constructor: a name, then .x and [x] but no calls
                while (cc->tk.t == '.' || cc->tk.t == '[') {
                    if (cc->tk.t == '.') { wb_next(cc); if (cc->tk.t != T_ID) wb_syntax(cc, "expected a name"); wb_op16(cc, OP_GETF, cc->tk.atom); wb_next(cc); }
                    else { wb_next(cc); wb_expr(cc); wb_expect(cc, ']', "expected ]"); wb_op(cc, OP_GETI); }
                }
                if (cc->tk.t == '(') { spread = wb_args(cc, &n); wb_op(cc, spread ? OP_NEWA : OP_NEW); if (!spread) wb_e8(cc, (uint8_t)n); }
                else { wb_op(cc, OP_NEW); wb_e8(cc, 0); }
                cc->last_kind = 0; return;
            }
            if (a == A_typeof || a == A_void || a == A_delete) wb_syntax(cc, "unexpected keyword");
            if (a == A_var || a == A_let || a == A_const || a == A_if || a == A_for || a == A_while || a == A_return || a == A_else || a == A_in || a == A_of) {
                if (!(a == A_of || a == A_let)) wb_syntax(cc, "unexpected keyword");
            }
            //a name, or the single parameter of an arrow function
            { wb_ls_t ls; wb_ls_save(cc, &ls); wb_next(cc);
              if (cc->tk.t == T_ARROW && !cc->tk.nl) { wb_ls_restore(cc, &ls); wb_make_func(cc, 0, 1); return; }
              wb_ls_restore(cc, &ls); }
            wb_next(cc);
            wb_emit_load(cc, a);
            return;
        }
        default: break;
    }
    wb_syntax(cc, "unexpected token");
}

//member access, calls, ++ -- (postfix)
static void wb_postfix(wb_cc_t *cc) {
    uint32_t chain_end[4]; int nce = 0;
    wb_primary(cc);
    for (;;) {
        int t = cc->tk.t;
        if (t == '.' || t == T_QDOT) {
            int opt = (t == T_QDOT);
            wb_next(cc);
            if (opt) {
                if (cc->tk.t == '(') { /* a?.() */ if (nce < 4) chain_end[nce++] = 0; goto call_opt; }
                if (cc->tk.t == '[') { if (nce < 4) { wb_op(cc, OP_OPTCHK); wb_e16(cc, 0); chain_end[nce++] = wb_cur(cc) - 2; } wb_next(cc); wb_expr(cc); wb_expect(cc, ']', "expected ]"); cc->last_pos = wb_cur(cc); wb_op(cc, OP_GETI); cc->last_kind = 5; continue; }
                if (nce < 4) { wb_op(cc, OP_OPTCHK); wb_e16(cc, 0); chain_end[nce++] = wb_cur(cc) - 2; }
            }
            if (cc->tk.t != T_ID) wb_syntax(cc, "expected a property name");
            cc->last_pos = wb_cur(cc); wb_op16(cc, OP_GETF, cc->tk.atom); cc->last_kind = 4; wb_next(cc);
        } else if (t == '[') {
            wb_next(cc); wb_expr(cc); wb_expect(cc, ']', "expected ]");
            cc->last_pos = wb_cur(cc); wb_op(cc, OP_GETI); cc->last_kind = 5;
        } else if (t == '(') {
        call_opt: {
            int n = 0, spread;
            if (cc->last_kind == 4 && cc->last_pos + 3 == wb_cur(cc)) cc->vm->jcode[cc->last_pos] = OP_GETM;  //a.b(...) keeps a as this
            else if (cc->last_kind == 5 && cc->last_pos + 1 == wb_cur(cc)) cc->vm->jcode[cc->last_pos] = OP_GETIM;
            else wb_op(cc, OP_UNDEF);
            spread = wb_args(cc, &n);
            if (spread) wb_op(cc, OP_CALLA); else { wb_op(cc, OP_CALL); wb_e8(cc, (uint8_t)n); }
            cc->last_kind = 0;
          }
        } else if ((t == T_INC || t == T_DEC) && !cc->tk.nl && cc->last_kind) {
            uint8_t kind = cc->last_kind; uint32_t pos = cc->last_pos; uint8_t *code = cc->vm->jcode; uint16_t a = 0; uint8_t h = 0, s = 0; int tmp;
            int inc = (t == T_INC);
            wb_next(cc);
            if (kind == 1) s = code[pos + 1]; else if (kind == 2) { h = code[pos + 1]; s = code[pos + 2]; } else if (kind == 3 || kind == 4) a = (uint16_t)(code[pos + 1] | (code[pos + 2] << 8));
            tmp = wb_tmp(cc);
            //old value is kept in the temp, the new value is stored, the old value is the result
            wb_op(cc, OP_TONUM);
            if (kind == 4) { cc->vm->jcode_len = pos; wb_op(cc, OP_DUP); wb_op16(cc, OP_GETF, a); wb_op(cc, OP_TONUM); wb_op(cc, OP_SETL); wb_e8(cc, (uint8_t)tmp); wb_emit_num(cc, inc ? 1 : -1); wb_op(cc, OP_ADD); wb_op16(cc, OP_SETF, a); }
            else if (kind == 5) { cc->vm->jcode_len = pos; wb_op(cc, OP_DUP2); wb_op(cc, OP_GETI); wb_op(cc, OP_TONUM); wb_op(cc, OP_SETL); wb_e8(cc, (uint8_t)tmp); wb_emit_num(cc, inc ? 1 : -1); wb_op(cc, OP_ADD); wb_op(cc, OP_SETI); }
            else {
                wb_op(cc, OP_SETL); wb_e8(cc, (uint8_t)tmp); wb_emit_num(cc, inc ? 1 : -1); wb_op(cc, OP_ADD);
                if (kind == 1) { wb_op(cc, OP_SETL); wb_e8(cc, s); } else if (kind == 2) { wb_op(cc, OP_SETU); wb_e8(cc, h); wb_e8(cc, s); } else wb_op16(cc, OP_SETG, a);
            }
            wb_op(cc, OP_POP); wb_op(cc, OP_GETL); wb_e8(cc, (uint8_t)tmp);
            cc->last_kind = 0;
        } else break;
    }
    while (nce--) { if (chain_end[nce]) wb_patch(cc, chain_end[nce]); }
}

static void wb_unary(wb_cc_t *cc) {
    int t = cc->tk.t;
    if (t == '!' || t == '-' || t == '+' || t == '~') { wb_next(cc); wb_unary(cc); wb_op(cc, t == '!' ? OP_NOT : t == '-' ? OP_NEG : t == '+' ? OP_POS : OP_BNOT); return; }
    if (t == T_INC || t == T_DEC) {
        uint8_t kind; uint32_t pos; uint8_t *code; uint16_t a = 0; uint8_t h = 0, s = 0;
        wb_next(cc); wb_unary(cc);
        kind = cc->last_kind; pos = cc->last_pos; code = cc->vm->jcode;
        if (!kind) wb_syntax(cc, "invalid ++/-- target");
        if (kind == 1) s = code[pos + 1]; else if (kind == 2) { h = code[pos + 1]; s = code[pos + 2]; } else if (kind == 3 || kind == 4) a = (uint16_t)(code[pos + 1] | (code[pos + 2] << 8));
        if (kind == 4) { cc->vm->jcode_len = pos; wb_op(cc, OP_DUP); wb_op16(cc, OP_GETF, a); }
        else if (kind == 5) { cc->vm->jcode_len = pos; wb_op(cc, OP_DUP2); wb_op(cc, OP_GETI); }
        wb_op(cc, OP_TONUM); wb_emit_num(cc, t == T_INC ? 1 : -1); wb_op(cc, OP_ADD);
        if (kind == 1) { wb_op(cc, OP_SETL); wb_e8(cc, s); } else if (kind == 2) { wb_op(cc, OP_SETU); wb_e8(cc, h); wb_e8(cc, s); }
        else if (kind == 3) wb_op16(cc, OP_SETG, a); else if (kind == 4) wb_op16(cc, OP_SETF, a); else wb_op(cc, OP_SETI);
        cc->last_kind = 0; return;
    }
    if (t == T_ID) {
        uint16_t a = cc->tk.atom;
        if (a == A_typeof) {
            wb_next(cc);
            if (cc->tk.t == T_ID) {  //typeof undeclaredName must not throw
                wb_ls_t ls; wb_ls_save(cc, &ls); { uint16_t nm = cc->tk.atom; uint8_t h, s; wb_next(cc);
                  if ((cc->tk.t == ')' || cc->tk.t == ';' || cc->tk.t == T_EQ || cc->tk.t == T_SEQ || cc->tk.t == T_NE || cc->tk.t == T_SNE || cc->tk.t == '}' || cc->tk.t == ',' || cc->tk.t == '?' || cc->tk.t == T_AND || cc->tk.t == T_OR) && !wb_resolve(cc, nm, &h, &s)) { wb_op16(cc, OP_TYPEOFG, nm); return; } }
                wb_ls_restore(cc, &ls);
            }
            wb_unary(cc); wb_op(cc, OP_TYPEOF); return;
        }
        if (a == A_void) { wb_next(cc); wb_unary(cc); wb_op(cc, OP_VOID); return; }
        if (a == A_delete) {
            wb_next(cc); wb_unary(cc);
            if (cc->last_kind == 4) { uint16_t at = (uint16_t)(cc->vm->jcode[cc->last_pos + 1] | (cc->vm->jcode[cc->last_pos + 2] << 8)); cc->vm->jcode_len = cc->last_pos; wb_op16(cc, OP_DELF, at); }
            else if (cc->last_kind == 5) { cc->vm->jcode_len = cc->last_pos; wb_op(cc, OP_DELI); }
            else { wb_op(cc, OP_POP); wb_op(cc, OP_TRUE); }
            return;
        }
        if (a == A_await && cc->fc->is_async) { wb_next(cc); wb_unary(cc); wb_op(cc, OP_AWAIT); return; }
    }
    wb_postfix(cc);
}

static int wb_binprec(int t, uint16_t atom, int noin) {
    switch (t) {
        case T_NULLISH: return 1; case T_OR: return 2; case T_AND: return 3; case '|': return 4; case '^': return 5; case '&': return 6;
        case T_EQ: case T_NE: case T_SEQ: case T_SNE: return 7;
        case '<': case '>': case T_LE: case T_GE: return 8;
        case T_SHL: case T_SHR: case T_USHR: return 9;
        case '+': case '-': return 10; case '*': case '/': case '%': return 11; case T_EXP: return 12;
        case T_ID: if (atom == A_instanceof) return 8; if (atom == A_in && !noin) return 8; return 0;
        default: return 0;
    }
}
static void wb_binary(wb_cc_t *cc, int minprec) {
    wb_unary(cc);
    for (;;) {
        int t = cc->tk.t, prec = wb_binprec(t, cc->tk.atom, cc->noin); uint16_t atom = cc->tk.atom; uint8_t op;
        if (!prec || prec < minprec) return;
        wb_next(cc);
        if (t == T_AND || t == T_OR || t == T_NULLISH) {
            uint32_t j = wb_jmp(cc, t == T_AND ? OP_JFK : t == T_OR ? OP_JTK : OP_JNN);
            wb_binary(cc, prec + 1); wb_patch(cc, j); cc->last_kind = 0; continue;
        }
        wb_binary(cc, t == T_EXP ? prec : prec + 1);
        switch (t) {
            case '+': op = OP_ADD; break; case '-': op = OP_SUB; break; case '*': op = OP_MUL; break; case '/': op = OP_DIV; break; case '%': op = OP_MOD; break; case T_EXP: op = OP_EXP; break;
            case '&': op = OP_BAND; break; case '|': op = OP_BOR; break; case '^': op = OP_BXOR; break; case T_SHL: op = OP_SHL; break; case T_SHR: op = OP_SHR; break; case T_USHR: op = OP_USHR; break;
            case T_EQ: op = OP_EQ; break; case T_NE: op = OP_NE; break; case T_SEQ: op = OP_SEQ; break; case T_SNE: op = OP_SNE; break;
            case '<': op = OP_LT; break; case '>': op = OP_GT; break; case T_LE: op = OP_LE; break; case T_GE: op = OP_GE; break;
            default: op = atom == A_instanceof ? OP_INSTANCEOF : OP_IN; break;
        }
        wb_op(cc, op);
    }
}

static int wb_assign_op(int t) {  //the binary op of a compound assignment, 0 = not one
    switch (t) {
        case T_ADDEQ: return OP_ADD; case T_SUBEQ: return OP_SUB; case T_MULEQ: return OP_MUL; case T_DIVEQ: return OP_DIV; case T_MODEQ: return OP_MOD; case T_EXPEQ: return OP_EXP;
        case T_BANDEQ: return OP_BAND; case T_BOREQ: return OP_BOR; case T_BXOREQ: return OP_BXOR; case T_SHLEQ: return OP_SHL; case T_SHREQ: return OP_SHR; case T_USHREQ: return OP_USHR;
        default: return 0;
    }
}

static void wb_assign(wb_cc_t *cc) {
    uint8_t kind, h = 0, s = 0; uint32_t pos; uint16_t a = 0; int t;
    //arrow functions with a parenthesised or single name parameter are caught in wb_primary
    wb_binary(cc, 1);
    t = cc->tk.t;
    if (t == '?') {  //ternary
        uint32_t jf, je;
        wb_next(cc);
        jf = wb_jmp(cc, OP_JF);
        { uint8_t save = cc->noin; cc->noin = 0; wb_assign(cc); cc->noin = save; }
        je = wb_jmp(cc, OP_JMP);
        wb_patch(cc, jf);
        wb_expect(cc, ':', "expected :");
        wb_assign(cc);
        wb_patch(cc, je); cc->last_kind = 0;
        return;
    }
    if (t != '=' && !wb_assign_op(t) && t != T_ANDEQ && t != T_OREQ && t != T_NULLEQ) return;
    kind = cc->last_kind; pos = cc->last_pos;
    if (!kind) wb_syntax(cc, "invalid assignment target");
    { uint8_t *code = cc->vm->jcode;
      if (kind == 1) s = code[pos + 1]; else if (kind == 2) { h = code[pos + 1]; s = code[pos + 2]; } else if (kind == 3 || kind == 4) a = (uint16_t)(code[pos + 1] | (code[pos + 2] << 8)); }
    wb_next(cc);
    if (t == '=') {
        if (kind == 4 || kind == 5 || 1) cc->vm->jcode_len = pos;  //take the load back: the object (and key) stay on the stack
        wb_assign(cc);
        if (kind == 1) { wb_op(cc, OP_SETL); wb_e8(cc, s); } else if (kind == 2) { wb_op(cc, OP_SETU); wb_e8(cc, h); wb_e8(cc, s); }
        else if (kind == 3) wb_op16(cc, OP_SETG, a); else if (kind == 4) wb_op16(cc, OP_SETF, a); else wb_op(cc, OP_SETI);
        cc->last_kind = 0; return;
    }
    if (t == T_ANDEQ || t == T_OREQ || t == T_NULLEQ) {  //a ||= b  =  a || (a = b)
        uint32_t j; uint8_t jop = t == T_ANDEQ ? OP_JFK : t == T_OREQ ? OP_JTK : OP_JNN;
        if (kind == 4 || kind == 5) wb_syntax(cc, "logical assignment on properties is not supported");
        j = wb_jmp(cc, jop);
        wb_assign(cc);
        if (kind == 1) { wb_op(cc, OP_SETL); wb_e8(cc, s); } else if (kind == 2) { wb_op(cc, OP_SETU); wb_e8(cc, h); wb_e8(cc, s); } else wb_op16(cc, OP_SETG, a);
        wb_patch(cc, j); cc->last_kind = 0; return;
    }
    //compound: a += b  =  a = a + b
    if (kind == 4) { cc->vm->jcode_len = pos; wb_op(cc, OP_DUP); wb_op16(cc, OP_GETF, a); }
    else if (kind == 5) { cc->vm->jcode_len = pos; wb_op(cc, OP_DUP2); wb_op(cc, OP_GETI); }
    wb_assign(cc);
    wb_op(cc, (uint8_t)wb_assign_op(t));
    if (kind == 1) { wb_op(cc, OP_SETL); wb_e8(cc, s); } else if (kind == 2) { wb_op(cc, OP_SETU); wb_e8(cc, h); wb_e8(cc, s); }
    else if (kind == 3) wb_op16(cc, OP_SETG, a); else if (kind == 4) wb_op16(cc, OP_SETF, a); else wb_op(cc, OP_SETI);
    cc->last_kind = 0;
}

static void wb_expr(wb_cc_t *cc) {
    wb_assign(cc);
    while (cc->tk.t == ',') { wb_next(cc); wb_op(cc, OP_POP); wb_assign(cc); }
}

//statements

static wb_loop_t *wb_loop_push(wb_cc_t *cc, int is_switch) {
    wb_fc_t *f = cc->fc; wb_loop_t *l;
    if (f->nloops >= WB_MAX_LOOPS) wb_syntax(cc, "loops nested too deep");
    l = &f->loops[f->nloops++]; memset(l, 0, sizeof *l); l->try_depth = f->try_depth; l->is_switch = (uint8_t)is_switch;
    return l;
}
static void wb_loop_pop(wb_cc_t *cc) {  //breaks land here
    wb_loop_t *l = &cc->fc->loops[--cc->fc->nloops]; int i;
    for (i = 0; i < l->nbrk; i++) wb_patch(cc, l->brk[i]);
}
static void wb_loop_cont_here(wb_cc_t *cc, wb_loop_t *l) { int i; for (i = 0; i < l->ncont; i++) wb_patch(cc, l->cont[i]); l->ncont = 0; }
static void wb_loop_cont_to(wb_cc_t *cc, wb_loop_t *l, uint32_t target) { int i; for (i = 0; i < l->ncont; i++) wb_patch_to(cc, l->cont[i], target); l->ncont = 0; }

//binds the value on top of the stack to a pattern or a name (consumes the value)
static void wb_bind(wb_cc_t *cc, int is_decl);
static void wb_bind_default(wb_cc_t *cc) {  //[v] -> [v or the default]
    if (cc->tk.t == '=') {
        uint32_t j;
        wb_next(cc);
        wb_op(cc, OP_DUP); wb_op(cc, OP_UNDEF); wb_op(cc, OP_SEQ); j = wb_jmp(cc, OP_JF);
        wb_op(cc, OP_POP); wb_assign(cc);
        wb_patch(cc, j);
    }
}
static void wb_bind(wb_cc_t *cc, int is_decl) {
    if (cc->tk.t == '[') {
        int idx = 0;
        wb_next(cc);
        while (cc->tk.t != ']') {
            if (cc->tk.t == ',') { idx++; wb_next(cc); continue; }
            if (cc->tk.t == T_ELLIPSIS) wb_syntax(cc, "rest in array patterns is not supported");
            wb_op(cc, OP_DUP); wb_emit_num(cc, idx); wb_op(cc, OP_GETI);
            { wb_ls_t ls; wb_ls_save(cc, &ls); (void)ls; }
            wb_bind_default(cc);  //(for [a = 1] the default follows the name, handled in the recursive call below)
            wb_bind(cc, is_decl);
            idx++;
            if (cc->tk.t == ',') wb_next(cc); else if (cc->tk.t != ']') wb_syntax(cc, "expected , or ]");
        }
        wb_next(cc); wb_op(cc, OP_POP);
    } else if (cc->tk.t == '{') {
        wb_next(cc);
        while (cc->tk.t != '}') {
            uint16_t key;
            if (cc->tk.t == T_ELLIPSIS) wb_syntax(cc, "rest in object patterns is not supported");
            if (cc->tk.t != T_ID && cc->tk.t != T_STR) wb_syntax(cc, "bad object pattern");
            key = cc->tk.atom; wb_next(cc);
            wb_op(cc, OP_DUP); wb_op16(cc, OP_GETF, key);
            if (cc->tk.t == ':') { wb_next(cc); wb_bind_default(cc); wb_bind(cc, is_decl); }
            else {  //{ a } or { a = 1 }
                int slot;
                wb_bind_default(cc);
                if (is_decl) slot = wb_declare(cc, key);
                (void)slot;
                wb_emit_store(cc, key); wb_op(cc, OP_POP);
            }
            if (cc->tk.t == ',') wb_next(cc); else if (cc->tk.t != '}') wb_syntax(cc, "expected , or }");
        }
        wb_next(cc); wb_op(cc, OP_POP);
    } else if (cc->tk.t == T_ID) {
        uint16_t a = cc->tk.atom;
        wb_next(cc);
        wb_bind_default(cc);
        if (is_decl) wb_declare(cc, a);
        wb_emit_store(cc, a); wb_op(cc, OP_POP);
    } else wb_syntax(cc, "expected a name or a pattern");
}

//var / let / const declarations (const is not enforced, let is function scoped)
static void wb_var_decl(wb_cc_t *cc, int is_let) {
    do {
        if (cc->tk.t == '[' || cc->tk.t == '{') {
            wb_ls_t ls; wb_ls_save(cc, &ls); wb_skip_group(cc);
            if (cc->tk.t != '=') wb_syntax(cc, "a pattern needs a value");
            wb_next(cc); wb_assign(cc);
            { wb_ls_t after; wb_ls_save(cc, &after); wb_ls_restore(cc, &ls); wb_bind(cc, 1); wb_ls_restore(cc, &after); }
        } else {
            uint16_t a;
            if (cc->tk.t != T_ID) wb_syntax(cc, "expected a variable name");
            a = cc->tk.atom; wb_next(cc);
            wb_declare(cc, a);
            if (cc->tk.t == '=') { wb_next(cc); wb_assign(cc); wb_emit_store(cc, a); wb_op(cc, OP_POP); }
            else if (is_let && !cc->fc->toplevel) { wb_op(cc, OP_UNDEF); wb_emit_store(cc, a); wb_op(cc, OP_POP); }
        }
    } while (wb_eat(cc, ','));
}

static void wb_paren_expr(wb_cc_t *cc) { wb_expect(cc, '(', "expected ("); wb_expr(cc); wb_expect(cc, ')', "expected )"); }

static void wb_for(wb_cc_t *cc) {
    wb_ls_t head; int foreach = 0, is_of = 0, decl = 0, pat = 0;
    wb_next(cc);  //for
    if (wb_isid(cc, A_await)) wb_syntax(cc, "for await is not supported");
    wb_expect(cc, '(', "expected (");
    wb_ls_save(cc, &head);
    {   //is it for (x of y) / for (x in y)?
        wb_ls_t ls; wb_ls_save(cc, &ls);
        if (cc->tk.t == T_ID && (cc->tk.atom == A_var || cc->tk.atom == A_let || cc->tk.atom == A_const)) { decl = 1; wb_next(cc); }
        if (cc->tk.t == '[' || cc->tk.t == '{') { pat = 1; wb_skip_group(cc); } else if (cc->tk.t == T_ID) wb_next(cc); else decl = decl;
        if (cc->tk.t == T_ID && (cc->tk.atom == A_of || cc->tk.atom == A_in)) { foreach = 1; is_of = (cc->tk.atom == A_of); }
        wb_ls_restore(cc, &ls);
    }
    if (foreach) {
        wb_ls_t bind, after; wb_loop_t *l; uint32_t loop, exit_j;
        if (decl) wb_next(cc);
        wb_ls_save(cc, &bind);
        if (pat) wb_skip_group(cc); else wb_next(cc);
        wb_next(cc);  //of / in
        wb_expr(cc);
        wb_expect(cc, ')', "expected )");
        wb_op(cc, is_of ? OP_ITER : OP_KEYS);  //(the op leaves [list, 0] on the stack)
        wb_ls_save(cc, &after);
        l = wb_loop_push(cc, 0);
        loop = wb_cur(cc);
        exit_j = wb_jmp(cc, OP_FORNEXT);
        wb_ls_restore(cc, &bind);
        wb_bind(cc, decl);  //takes the element
        wb_ls_restore(cc, &after);
        wb_stmt(cc);
        wb_loop_cont_to(cc, l, loop);
        wb_jmp_back(cc, OP_JMP, loop);
        wb_patch(cc, exit_j);
        wb_loop_pop(cc);
        wb_op(cc, OP_POP); wb_op(cc, OP_POP);
        return;
    }
    {   //for (init; test; update) body
        wb_loop_t *l; uint32_t test_at, exit_j = 0; int has_test = 0; wb_ls_t upd, after; int has_upd = 0;
        if (cc->tk.t == ';') wb_next(cc);
        else {
            cc->noin = 1;
            if (cc->tk.t == T_ID && (cc->tk.atom == A_var || cc->tk.atom == A_let || cc->tk.atom == A_const)) { int is_let = cc->tk.atom != A_var; wb_next(cc); wb_var_decl(cc, is_let); }
            else { wb_expr(cc); wb_op(cc, OP_POP); }
            cc->noin = 0;
            wb_expect(cc, ';', "expected ;");
        }
        test_at = wb_cur(cc);
        if (cc->tk.t != ';') { wb_expr(cc); exit_j = wb_jmp(cc, OP_JF); has_test = 1; }
        wb_expect(cc, ';', "expected ;");
        wb_ls_save(cc, &upd);
        if (cc->tk.t != ')') { int depth = 0; has_upd = 1; while (cc->tk.t != T_EOF && !(depth == 0 && cc->tk.t == ')')) { if (cc->tk.t == '(' || cc->tk.t == '[' || cc->tk.t == '{') depth++; else if (cc->tk.t == ')' || cc->tk.t == ']' || cc->tk.t == '}') depth--; wb_next(cc); } }
        wb_expect(cc, ')', "expected )");
        l = wb_loop_push(cc, 0);
        wb_stmt(cc);
        wb_ls_save(cc, &after);
        wb_loop_cont_here(cc, l);
        if (has_upd) { wb_ls_restore(cc, &upd); wb_expr(cc); wb_op(cc, OP_POP); wb_ls_restore(cc, &after); }
        wb_jmp_back(cc, OP_JMP, test_at);
        if (has_test) wb_patch(cc, exit_j);
        wb_loop_pop(cc);
    }
}

static void wb_try(wb_cc_t *cc) {
    wb_fc_t *f = cc->fc; uint32_t try_all = 0, try_catch = 0, jend = 0; int has_catch = 0, has_finally = 0; int exc_tmp = -1;
    wb_ls_t fin_start, fin_after;
    wb_next(cc);  //try
    if (cc->tk.t != '{') wb_syntax(cc, "expected {");
    //we need to know if there is a finally before generating the code: scan ahead over the blocks
    {
        wb_ls_t ls; wb_ls_save(cc, &ls);
        wb_skip_group(cc);
        if (wb_isid(cc, A_catch)) { wb_next(cc); if (cc->tk.t == '(') wb_skip_group(cc); if (cc->tk.t == '{') wb_skip_group(cc); has_catch = 1; }
        if (wb_isid(cc, A_finally)) has_finally = 1;
        wb_ls_restore(cc, &ls);
    }
    if (!has_catch && !has_finally) wb_syntax(cc, "try needs catch or finally");
    if (has_finally) { exc_tmp = wb_add_local(cc, 0); try_all = wb_jmp(cc, OP_TRY); f->try_depth++; }
    if (has_catch) { try_catch = wb_jmp(cc, OP_TRY); f->try_depth++; }
    wb_block_body(cc);
    if (has_catch) {
        uint32_t jover;
        wb_op(cc, OP_ENDTRY); f->try_depth--;
        jover = wb_jmp(cc, OP_JMP);
        wb_patch(cc, try_catch);
        wb_next(cc);  //catch
        if (cc->tk.t == '(') {
            wb_next(cc);
            if (cc->tk.t == '[' || cc->tk.t == '{') wb_bind(cc, 1); else { uint16_t a = cc->tk.atom; if (cc->tk.t != T_ID) wb_syntax(cc, "expected a name"); wb_next(cc); wb_declare(cc, a); wb_emit_store(cc, a); wb_op(cc, OP_POP); }
            wb_expect(cc, ')', "expected )");
        } else wb_op(cc, OP_POP);
        wb_block_body(cc);
        wb_patch(cc, jover);
    }
    if (has_finally) {
        wb_op(cc, OP_ENDTRY); f->try_depth--;
        wb_next(cc);  //finally
        wb_ls_save(cc, &fin_start);
        wb_block_body(cc);  //copy for the normal path
        wb_ls_save(cc, &fin_after);
        jend = wb_jmp(cc, OP_JMP);
        wb_patch(cc, try_all);
        wb_op(cc, OP_SETL); wb_e8(cc, (uint8_t)exc_tmp); wb_op(cc, OP_POP);
        wb_ls_restore(cc, &fin_start);
        wb_block_body(cc);  //copy for the exception path, then rethrow
        wb_op(cc, OP_GETL); wb_e8(cc, (uint8_t)exc_tmp); wb_op(cc, OP_THROW);
        wb_patch(cc, jend);
        wb_ls_restore(cc, &fin_after);
    }
}

static void wb_switch(wb_cc_t *cc) {
    int tmp = wb_add_local(cc, 0); uint32_t jtests, labels[24]; wb_ls_t cases[24]; int ncase = 0, dlabel = -1, i; uint32_t jend_default;
    wb_next(cc);  //switch
    wb_paren_expr(cc);
    wb_op(cc, OP_SETL); wb_e8(cc, (uint8_t)tmp); wb_op(cc, OP_POP);
    wb_expect(cc, '{', "expected {");
    wb_loop_push(cc, 1);
    jtests = wb_jmp(cc, OP_JMP);
    while (cc->tk.t != '}') {
        if (wb_isid(cc, A_case)) {
            int depth = 0, q = 0;
            if (ncase >= 24) wb_syntax(cc, "too many cases (24 max)");
            wb_next(cc);
            wb_ls_save(cc, &cases[ncase]);
            while (cc->tk.t != T_EOF) {  //skip the case expression up to its colon
                if (cc->tk.t == '(' || cc->tk.t == '[' || cc->tk.t == '{') depth++; else if (cc->tk.t == ')' || cc->tk.t == ']' || cc->tk.t == '}') depth--;
                else if (cc->tk.t == '?') q++;
                else if (cc->tk.t == ':' && depth == 0) { if (q) q--; else break; }
                wb_next(cc);
            }
            wb_expect(cc, ':', "expected :");
            labels[ncase++] = wb_cur(cc);
        } else if (wb_isid(cc, A_default)) {
            wb_next(cc); wb_expect(cc, ':', "expected :");
            dlabel = (int)wb_cur(cc);
        } else wb_syntax(cc, "expected case or default");
        while (cc->tk.t != '}' && !wb_isid(cc, A_case) && !wb_isid(cc, A_default)) wb_stmt(cc);
    }
    {   //after the last body: skip the tests
        wb_ls_t after; uint32_t jover = wb_jmp(cc, OP_JMP);
        wb_ls_save(cc, &after);
        wb_patch(cc, jtests);
        for (i = 0; i < ncase; i++) {
            uint32_t jt;
            wb_ls_restore(cc, &cases[i]);
            wb_op(cc, OP_GETL); wb_e8(cc, (uint8_t)tmp);
            wb_expr(cc);
            wb_op(cc, OP_SEQ);
            jt = wb_jmp(cc, OP_JT); wb_patch_to(cc, jt, labels[i]);
        }
        jend_default = wb_jmp(cc, OP_JMP);
        if (dlabel >= 0) wb_patch_to(cc, jend_default, (uint32_t)dlabel); else wb_patch(cc, jend_default);
        wb_patch(cc, jover);
        wb_ls_restore(cc, &after);
    }
    wb_loop_pop(cc);
    wb_expect(cc, '}', "expected }");
}

static void wb_block_body(wb_cc_t *cc) {
    wb_expect(cc, '{', "expected {");
    while (cc->tk.t != '}') { if (cc->tk.t == T_EOF) wb_syntax(cc, "missing }"); wb_stmt(cc); }
    wb_next(cc);
}

static void wb_func_decl(wb_cc_t *cc, uint8_t flags) {
    wb_fc_t *f = cc->fc; uint16_t name, idx;
    if (cc->tk.t == '*') wb_syntax(cc, "generators are not supported");
    if (cc->tk.t != T_ID) wb_syntax(cc, "expected a function name");
    name = cc->tk.atom; wb_next(cc);
    wb_declare(cc, name);
    wb_make_func(cc, flags, 0);
    cc->vm->jcode_len -= 3;  //hoisted: the closure is created in the prologue instead
    idx = (uint16_t)(cc->vm->nprotos - 1);
    if (f->nhoist >= WB_MAX_HOIST) wb_syntax(cc, "too many function declarations in one function");
    f->hoist[f->nhoist].name = name; f->hoist[f->nhoist].proto = idx; f->nhoist++;
    cc->last_kind = 0;
}


//classes are desugared: a constructor function, its prototype object with the methods, static members as properties of the function.
//extends keeps the superclass in a hidden variable, the prototype chain is linked, super(...) calls it with the same this
static const char *const wb_snips[] = { "(){}", "(...a){super(...a)}" };  //constructors of classes that do not write one
static void wb_class(wb_cc_t *cc, int is_decl) {
    eccles_web_vm_t *vm = cc->vm; uint16_t name = 0, sup = 0; char nm[24]; wb_ls_t body, ctor_at; int has_ctor = 0, depth = 0;
    wb_next(cc);  //class
    if (cc->tk.t == T_ID && !wb_isid(cc, A_extends)) { name = cc->tk.atom; wb_next(cc); }
    if (is_decl && !name) wb_syntax(cc, "a class declaration needs a name");
    if (wb_isid(cc, A_extends)) {
        int l;
        wb_next(cc);
        l = snprintf(nm, sizeof nm, "$super%u", (unsigned)cc->class_seq++); sup = wb_atom(vm, nm, (uint32_t)l);
        if (!sup) wb_syntax(cc, "too many names (ECCLES_WEB_MAX_ATOMS)");
        wb_postfix(cc);
        wb_declare(cc, sup); wb_emit_store(cc, sup); wb_op(cc, OP_POP);
    }
    if (cc->tk.t != '{') wb_syntax(cc, "expected {");
    wb_ls_save(cc, &body);
    {   //find the constructor first: its function has to exist before the methods are attached
        wb_ls_t ls; wb_ls_save(cc, &ls);
        wb_next(cc);
        while (cc->tk.t != T_EOF) {
            if (cc->tk.t == '{' || cc->tk.t == '(' || cc->tk.t == '[') depth++;
            else if (cc->tk.t == '}' || cc->tk.t == ')' || cc->tk.t == ']') { if (--depth < 0) break; }
            else if (depth == 0 && wb_isid(cc, A_constructor)) { wb_ls_t here; wb_ls_save(cc, &here); wb_next(cc); if (cc->tk.t == '(') { ctor_at = here; has_ctor = 1; break; } wb_ls_restore(cc, &here); }
            wb_next(cc);
        }
        wb_ls_restore(cc, &ls);
    }
    if (has_ctor) {
        wb_ls_t keep; wb_ls_save(cc, &keep);
        wb_ls_restore(cc, &ctor_at); wb_next(cc);  //the ( of the constructor
        wb_make_func(cc, 0, 0);
        vm->protos[vm->nprotos - 1].name = sup;
        wb_ls_restore(cc, &keep);
    } else {
        uint16_t idx;
        if (vm->nprotos >= ECCLES_WEB_JS_MAX_FUNCS) wb_syntax(cc, "too many functions (ECCLES_WEB_JS_MAX_FUNCS)");
        idx = vm->nprotos++; memset(&vm->protos[idx], 0, sizeof vm->protos[idx]);
        vm->protos[idx].code = 0x80000000u | (sup ? 1u : 0u); vm->protos[idx].flags = WB_FN_PENDING; vm->protos[idx].name = sup;
        wb_op16(cc, OP_CLOSURE, idx);
    }
    wb_op(cc, OP_DUP); wb_op16(cc, OP_GETF, A_prototype);  //[F, P]
    if (sup) { wb_emit_load(cc, sup); wb_op16(cc, OP_GETF, A_prototype); wb_op(cc, OP_SETPROTO); }
    wb_next(cc);  //{
    while (cc->tk.t != '}') {
        int is_static = 0, is_async = 0; uint16_t key = 0;
        if (cc->tk.t == ';') { wb_next(cc); continue; }
        if (cc->tk.t == T_EOF) wb_syntax(cc, "missing } of the class");
        if (wb_isid(cc, A_static)) { wb_ls_t ls; wb_ls_save(cc, &ls); wb_next(cc); if (cc->tk.t == '(' || cc->tk.t == '=') wb_ls_restore(cc, &ls); else is_static = 1; }
        if (wb_isid(cc, A_async)) { wb_ls_t ls; wb_ls_save(cc, &ls); wb_next(cc); if (cc->tk.t == '(' || cc->tk.t == '=') wb_ls_restore(cc, &ls); else is_async = 1; }
        if (cc->tk.t == '*') wb_syntax(cc, "generators are not supported");
        if (wb_isid(cc, A_get) || wb_isid(cc, A_set)) { wb_ls_t ls; wb_ls_save(cc, &ls); wb_next(cc); if (cc->tk.t == T_ID || cc->tk.t == T_STR) wb_syntax(cc, "getters and setters are not supported"); wb_ls_restore(cc, &ls); }
        if (cc->tk.t == T_ID || cc->tk.t == T_STR) { key = cc->tk.atom; wb_next(cc); }
        else if (cc->tk.t == T_NUM) { char b[40]; int l = snprintf(b, sizeof b, "%.15g", cc->tk.num); key = wb_atom(vm, b, (uint32_t)l); wb_next(cc); }
        else wb_syntax(cc, "bad class member");
        if (cc->tk.t != '(') wb_syntax(cc, "class fields are not supported, assign them in the constructor");
        if (key == A_constructor && !is_static) { wb_skip_group(cc); if (cc->tk.t != '{') wb_syntax(cc, "expected {"); wb_skip_group(cc); continue; }
        if (is_static) { wb_op(cc, OP_SWAP); wb_op(cc, OP_DUP); }
        wb_make_func(cc, (uint8_t)(is_async ? WB_FN_ASYNC : 0), 0);
        vm->protos[vm->nprotos - 1].name = sup;
        if (is_static) { wb_op16(cc, OP_SETF, key); wb_op(cc, OP_POP); wb_op(cc, OP_SWAP); }
        else wb_op16(cc, OP_INITF, key);
    }
    wb_next(cc);  //}
    wb_op(cc, OP_POP);  //the prototype, the constructor stays
    if (is_decl) { wb_declare(cc, name); wb_emit_store(cc, name); wb_op(cc, OP_POP); }
    cc->last_kind = 0;
}

static void wb_stmt(wb_cc_t *cc) {
    int t = cc->tk.t;
    wb_fc_t *f = cc->fc;
    if (t == ';') { wb_next(cc); return; }
    if (t == '{') { wb_block_body(cc); return; }
    if (t == T_ID) {
        uint16_t a = cc->tk.atom;
        if (a == A_var || a == A_let || a == A_const) {
            wb_ls_t ls; int ok = 1;
            wb_ls_save(cc, &ls); wb_next(cc);
            if (a == A_let && cc->tk.t != T_ID && cc->tk.t != '[' && cc->tk.t != '{') ok = 0;  //"let" used as a name
            wb_ls_restore(cc, &ls);
            if (ok) { wb_next(cc); wb_var_decl(cc, a != A_var); wb_semi(cc); return; }
        }
        if (a == A_function) { wb_next(cc); wb_func_decl(cc, 0); return; }
        if (a == A_async) { wb_ls_t ls; wb_ls_save(cc, &ls); wb_next(cc); if (wb_isid(cc, A_function) && !cc->tk.nl) { wb_next(cc); wb_func_decl(cc, WB_FN_ASYNC); return; } wb_ls_restore(cc, &ls); }
        if (a == A_class) { wb_class(cc, 1); return; }
        if (a == A_import || a == A_export) wb_syntax(cc, "modules are not supported");
        if (a == A_if) {
            uint32_t jf;
            wb_next(cc); wb_paren_expr(cc);
            jf = wb_jmp(cc, OP_JF);
            wb_stmt(cc);
            if (wb_isid(cc, A_else)) { uint32_t je = wb_jmp(cc, OP_JMP); wb_patch(cc, jf); wb_next(cc); wb_stmt(cc); wb_patch(cc, je); }
            else wb_patch(cc, jf);
            return;
        }
        if (a == A_for) { wb_for(cc); return; }
        if (a == A_while) {
            wb_loop_t *l; uint32_t top = wb_cur(cc), jf;
            wb_next(cc); wb_paren_expr(cc);
            jf = wb_jmp(cc, OP_JF);
            l = wb_loop_push(cc, 0);
            wb_stmt(cc);
            wb_loop_cont_to(cc, l, top);
            wb_jmp_back(cc, OP_JMP, top);
            wb_patch(cc, jf);
            wb_loop_pop(cc);
            return;
        }
        if (a == A_do) {
            wb_loop_t *l; uint32_t top = wb_cur(cc);
            wb_next(cc);
            l = wb_loop_push(cc, 0);
            wb_stmt(cc);
            wb_loop_cont_here(cc, l);
            if (!wb_isid(cc, A_while)) wb_syntax(cc, "expected while");
            wb_next(cc); wb_paren_expr(cc);
            { uint32_t j = wb_jmp(cc, OP_JT); wb_patch_to(cc, j, top); }
            wb_loop_pop(cc);
            if (cc->tk.t == ';') wb_next(cc);
            return;
        }
        if (a == A_return) {
            wb_next(cc);
            if (cc->tk.t == ';' || cc->tk.t == '}' || cc->tk.t == T_EOF || cc->tk.nl) wb_op(cc, OP_RETU);
            else { wb_expr(cc); wb_op(cc, OP_RET); }
            wb_semi(cc); return;
        }
        if (a == A_break || a == A_continue) {
            int i, is_break = (a == A_break); wb_loop_t *l = NULL;
            wb_next(cc);
            if (cc->tk.t == T_ID && !cc->tk.nl) wb_syntax(cc, "labels are not supported");
            for (i = f->nloops - 1; i >= 0; i--) if (is_break || !f->loops[i].is_switch) { l = &f->loops[i]; break; }
            if (!l) wb_syntax(cc, is_break ? "break outside a loop" : "continue outside a loop");
            for (i = f->try_depth - l->try_depth; i > 0; i--) wb_op(cc, OP_ENDTRY);
            if (is_break) { if (l->nbrk >= WB_MAX_PATCH) wb_syntax(cc, "too many breaks in one loop"); l->brk[l->nbrk++] = wb_jmp(cc, OP_JMP); }
            else { if (l->ncont >= WB_MAX_PATCH) wb_syntax(cc, "too many continues in one loop"); l->cont[l->ncont++] = wb_jmp(cc, OP_JMP); }
            wb_semi(cc); return;
        }
        if (a == A_throw) { wb_next(cc); wb_expr(cc); wb_op(cc, OP_THROW); wb_semi(cc); return; }
        if (a == A_try) { wb_try(cc); return; }
        if (a == A_switch) { wb_switch(cc); return; }
        if (a == A_debugger) { wb_next(cc); wb_semi(cc); return; }
    }
    wb_expr(cc); wb_op(cc, OP_POP); wb_semi(cc);
}

//functions and scripts

//compiles the function whose source position was remembered in protos[idx].code. children are compiled right after, while this scope is alive
static void wb_compile_proto(wb_cc_t *cc, uint16_t idx, wb_fc_t *parent) {
    eccles_web_vm_t *vm = cc->vm; wb_fc_t fc, *saved = cc->fc; uint16_t n0, i, end; uint32_t src, body_start, jpro; uint8_t flags;
    int i2, snip = 0; const char *save_s = cc->s; uint32_t save_n = cc->n;
    memset(&fc, 0, sizeof fc);
    fc.parent = parent; fc.proto = idx; fc.tmp = -1;
    flags = vm->protos[idx].flags; src = vm->protos[idx].code;
    fc.super_atom = (flags & WB_FN_ARROW) && parent ? parent->super_atom : vm->protos[idx].name;
    if (src & 0x80000000u) { const char *sn = wb_snips[src & 0xFFu]; cc->s = sn; cc->n = (uint32_t)strlen(sn); src = 0; snip = 1; }
    fc.is_arrow = (flags & WB_FN_ARROW) != 0; fc.is_async = (flags & WB_FN_ASYNC) != 0;
    vm->protos[idx].flags = (uint8_t)(flags & ~WB_FN_PENDING);
    vm->protos[idx].code = vm->jcode_len;
    cc->fc = &fc; cc->last_kind = 0;
    cc->pos = src; wb_next(cc);
    n0 = vm->nprotos;
    jpro = wb_jmp(cc, OP_JMP);
    body_start = wb_cur(cc);
    //parameters
    if (fc.is_arrow && cc->tk.t == T_ID) { wb_add_local(cc, cc->tk.atom); fc.nparams = 1; wb_next(cc); }
    else {
        wb_expect(cc, '(', "expected (");
        while (cc->tk.t != ')') {
            int slot; uint16_t a;
            if (cc->tk.t == T_ELLIPSIS) {
                wb_next(cc); if (cc->tk.t != T_ID) wb_syntax(cc, "expected a name");
                wb_add_local(cc, cc->tk.atom); fc.nparams++; flags |= WB_FN_REST; wb_next(cc);
                if (cc->tk.t != ')') wb_syntax(cc, "rest parameter must be last");
                break;
            }
            if (cc->tk.t != T_ID) wb_syntax(cc, "destructuring parameters are not supported");
            a = cc->tk.atom; slot = wb_add_local(cc, a); fc.nparams++; wb_next(cc);
            if (cc->tk.t == '=') {  //default value: if (p === undefined) p = default
                uint32_t j;
                wb_next(cc);
                wb_op(cc, OP_GETL); wb_e8(cc, (uint8_t)slot); wb_op(cc, OP_UNDEF); wb_op(cc, OP_SEQ); j = wb_jmp(cc, OP_JF);
                wb_assign(cc); wb_op(cc, OP_SETL); wb_e8(cc, (uint8_t)slot); wb_op(cc, OP_POP);
                wb_patch(cc, j);
            }
            if (cc->tk.t == ',') wb_next(cc); else if (cc->tk.t != ')') wb_syntax(cc, "expected , or )");
        }
        wb_next(cc);
    }
    if (fc.is_arrow) wb_expect(cc, T_ARROW, "expected =>");
    if (fc.is_arrow && (flags & WB_FN_CONCISE)) { wb_assign(cc); wb_op(cc, OP_RET); }
    else {
        wb_expect(cc, '{', "expected {");
        while (cc->tk.t != '}') { if (cc->tk.t == T_EOF) wb_syntax(cc, "missing }"); wb_stmt(cc); }
        wb_op(cc, OP_RETU);
    }
    //prologue: hoisted function declarations get their closures, then back to the start of the body
    wb_patch(cc, jpro);
    for (i2 = 0; i2 < fc.nhoist; i2++) { wb_op16(cc, OP_CLOSURE, fc.hoist[i2].proto); wb_emit_store(cc, fc.hoist[i2].name); wb_op(cc, OP_POP); }
    wb_jmp_back(cc, OP_JMP, body_start);
    vm->protos[idx].nslots = fc.nlocals; vm->protos[idx].nparams = fc.nparams; vm->protos[idx].flags = (uint8_t)(flags & ~WB_FN_PENDING);
    end = vm->nprotos;
    for (i = n0; i < end; i++) if (vm->protos[i].flags & WB_FN_PENDING) wb_compile_proto(cc, i, &fc);
    cc->fc = saved;
    if (snip) { cc->s = save_s; cc->n = save_n; }
}

//compiles a script (toplevel code). returns the proto index, or -1 with a syntax error recorded
static int wb_compile_script(eccles_web_vm_t *vm, const char *src, uint32_t len) {
    wb_cc_t ccs, *cc = &ccs; wb_fc_t fc; uint16_t idx, n0, i, end; uint32_t jpro, body_start; int i2;
    uint32_t save_len = vm->jcode_len; uint16_t save_np = vm->nprotos; volatile int ok = 0;
    memset(cc, 0, sizeof *cc); cc->vm = vm; cc->s = src; cc->n = len;
    if (vm->nprotos >= ECCLES_WEB_JS_MAX_FUNCS) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 8); return -1; }
    idx = vm->nprotos++; memset(&vm->protos[idx], 0, sizeof vm->protos[idx]);
    memset(&fc, 0, sizeof fc); fc.toplevel = 1; fc.tmp = -1; fc.proto = idx;
    cc->fc = &fc;
    if (setjmp(cc->jb) == 0) {
        vm->protos[idx].code = vm->jcode_len;
        cc->pos = 0; wb_next(cc);
        n0 = vm->nprotos;
        jpro = wb_jmp(cc, OP_JMP);
        body_start = wb_cur(cc);
        while (cc->tk.t != T_EOF) wb_stmt(cc);
        wb_op(cc, OP_RETU);
        wb_patch(cc, jpro);
        for (i2 = 0; i2 < fc.nvars; i2++) wb_op16(cc, OP_VARG, fc.vars[i2]);
        for (i2 = 0; i2 < fc.nhoist; i2++) { wb_op16(cc, OP_CLOSURE, fc.hoist[i2].proto); wb_op16(cc, OP_SETG, fc.hoist[i2].name); wb_op(cc, OP_POP); }
        wb_jmp_back(cc, OP_JMP, body_start);
        vm->protos[idx].nslots = fc.nlocals;
        end = vm->nprotos;
        for (i = n0; i < end; i++) if (vm->protos[i].flags & WB_FN_PENDING) wb_compile_proto(cc, i, &fc);
        ok = 1;
    }
    if (!ok) {  //syntax error: throw the half compiled script away
        vm->jcode_len = save_len; vm->nprotos = save_np;
        if (vm->jserr_count++ == 0) { vm->jserr = ECCLES_WEB_ERR_JS_SYNTAX; vm->jserr_info = cc->errpos; snprintf(vm->jserr_str, sizeof vm->jserr_str, "%.90s", cc->msg); }
        { char lb[ECCLES_WEB_MAX_NAME + 40]; int ll = snprintf(lb, sizeof lb, "SyntaxError: %s (at byte %u)", cc->msg, (unsigned)cc->errpos); wb_log(vm, lb, (uint32_t)(ll > 0 ? ll : 0)); }
        return -1;
    }
    return (int)idx;
}

//script vm: conversions, properties, calls, promises, the run loop
typedef wv (*wb_nat_fn)(eccles_web_vm_t *vm, wv self, const wv *a, int n);
#define WV_NONE (~(uint64_t)0)

//builtin tables are in the builtins section
static wv wb_nat_value(uint8_t kind, uint16_t atom);  //WV_NONE when there is no such builtin
static wv wb_call_native(eccles_web_vm_t *vm, uint32_t id, wv self, const wv *a, int n);
static uint8_t wb_nat_statics(uint32_t id);
static wv wb_dom_get(eccles_web_vm_t *vm, wv obj, uint16_t atom);
static void wb_dom_set(eccles_web_vm_t *vm, wv obj, uint16_t atom, wv val);
static wv wb_host_call(eccles_web_vm_t *vm, uint32_t idx, const wv *a, int n);
enum { K_NONE = 0, K_OBJ, K_ARR, K_STR, K_NUM, K_FUNC, K_PROMISE, K_ELEM, K_STYLE, K_CLASSLIST, K_DOCUMENT, K_WINDOW, K_RESPONSE, K_WASMINST, K_WASMMEM,
       K_MATH, K_JSON, K_CONSOLE, K_GLOBAL, K_OBJECT_S, K_ARRAY_S, K_STRING_S, K_NUMBER_S, K_PROMISE_S, K_DATE_S, K_WASM, K_STORAGE, K_LOCATION, K_HISTORY, K_EVENT, K_COUNT };

static int wb_is_obj_type(const eccles_web_vm_t *vm, wv v, uint8_t type) { return WV_TAGOF(v) == WV_T_OBJ && HHP(vm, WV_PAY(v))->type == type; }
static int wb_is_callable(const eccles_web_vm_t *vm, wv v) {
    if (WV_TAGOF(v) == WV_T_NAT) return 1;
    return WV_TAGOF(v) == WV_T_OBJ && (HHP(vm, WV_PAY(v))->type == H_FUNC || HHP(vm, WV_PAY(v))->type == H_BOUND);
}
static int wb_is_nullish(wv v) { return v == WV_UNDEF || v == WV_NULL; }

//conversions
static uint32_t wb_num_str(double d, char *buf) {  //buf >= 40 bytes
    int p; double back;
    if (d != d) { memcpy(buf, "NaN", 4); return 3; }
    if (d == HUGE_VAL) { memcpy(buf, "Infinity", 9); return 8; }
    if (d == -HUGE_VAL) { memcpy(buf, "-Infinity", 10); return 9; }
    if (d == 0) { buf[0] = '0'; buf[1] = 0; return 1; }
    if (d == floor(d) && fabs(d) < 1e15) return (uint32_t)snprintf(buf, 40, "%.0f", d);
    for (p = 1; p <= 17; p++) { snprintf(buf, 40, "%.*g", p, d); back = strtod(buf, NULL); if (back == d) break; }
    { char *e = strchr(buf, 'e');  //JS writes 1e-7, C writes 1e-07
      if (e) { char *x = e + 2; while (x[0] == '0' && x[1]) memmove(x, x + 1, strlen(x)); } }
    return (uint32_t)strlen(buf);
}
static double wb_str_num(const char *s, uint32_t n) {
    char tmp[48]; uint32_t i = 0, e = n; char *end;
    while (i < e && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) e--;
    if (i == e) return 0;
    if (e - i >= sizeof tmp) return NAN;
    memcpy(tmp, s + i, e - i); tmp[e - i] = 0;
    if (tmp[0] == '0' && (tmp[1] == 'x' || tmp[1] == 'X')) { double d = 0; uint32_t k; for (k = 2; tmp[k]; k++) { int h = wb_hex((uint8_t)tmp[k]); if (h < 0) return NAN; d = d * 16 + h; } return k > 2 ? d : NAN; }
    if (!strcmp(tmp, "Infinity") || !strcmp(tmp, "+Infinity")) return HUGE_VAL;
    if (!strcmp(tmp, "-Infinity")) return -HUGE_VAL;
    { uint32_t k; for (k = 0; tmp[k]; k++) { char c = tmp[k]; if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) return NAN; } }
    { double d = strtod(tmp, &end); return *end ? NAN : d; }
}

static wv wb_tostr(eccles_web_vm_t *vm, wv v);
static double wb_tonum(eccles_web_vm_t *vm, wv v) {
    uint32_t t = WV_TAGOF(v);
    if (WV_ISNUM(v)) return wv_dbl(v);
    if (v == WV_UNDEF) return NAN;
    if (v == WV_NULL || v == WV_FALSE) return 0;
    if (v == WV_TRUE) return 1;
    if (t == WV_T_ATOM || wb_is_obj_type(vm, v, H_STR)) { uint32_t n; const char *s = wb_str_ptr(vm, v, &n); return wb_str_num(s, n); }
    if (wb_is_arr(vm, v)) { wb_harr_t *a = wb_arr(vm, v); if (a->len == 0) return 0; if (a->len == 1) return wb_tonum(vm, wb_arr_el(vm, v)[0]); }
    return NAN;
}
static int wb_truthy(const eccles_web_vm_t *vm, wv v) {
    uint32_t t = WV_TAGOF(v);
    if (WV_ISNUM(v)) { double d = wv_dbl(v); return d != 0 && d == d; }
    if (v == WV_UNDEF || v == WV_NULL || v == WV_FALSE) return 0;
    if (v == WV_TRUE) return 1;
    if (t == WV_T_ATOM) return wb_alen(vm, (uint16_t)WV_PAY(v)) != 0;
    return 1;
}
static int32_t wb_toi32(double d) {
    if (d != d || d == HUGE_VAL || d == -HUGE_VAL) return 0;
    d = fmod(trunc(d), 4294967296.0);
    if (d < 0) d += 4294967296.0;
    return (int32_t)(uint32_t)d;
}
static uint32_t wb_cstr_of(eccles_web_vm_t *vm, wv v, char *buf, uint32_t cap);  //ToString into a C buffer (cut to cap)

//strings built by the engine
static wv wb_str_concat(eccles_web_vm_t *vm, wv a, wv b) {
    uint32_t la, lb, off; const char *pa = wb_str_ptr(vm, a, &la); const char *pb; wb_hstr_t *s;
    pb = wb_str_ptr(vm, b, &lb);
    if (la == 0) return b;
    if (lb == 0) return a;
    off = wb_galloc(vm, H_STR, 4 + la + lb + 1);
    if (!off) return WV_UNDEF;
    s = (wb_hstr_t *)(vm->jheap + off);
    pa = wb_str_ptr(vm, a, &la); pb = wb_str_ptr(vm, b, &lb);  //(the heap does not move, but be safe)
    s->len = la + lb; memcpy(s->s, pa, la); memcpy(s->s + la, pb, lb); s->s[la + lb] = 0;
    return wv_obj(off);
}
//join of several C strings, used by natives
static wv wb_typeof_atom(const eccles_web_vm_t *vm, wv v) {
    uint32_t t = WV_TAGOF(v);
    if (WV_ISNUM(v)) return wv_atom(A_number);
    if (v == WV_UNDEF) return wv_atom(A_undefined);
    if (v == WV_NULL) return wv_atom(A_object);
    if (v == WV_TRUE || v == WV_FALSE) return wv_atom(A_boolean);
    if (t == WV_T_ATOM) return wv_atom(A_string);
    if (t == WV_T_NAT) return wv_atom(A_function);
    if (t == WV_T_OBJ) { uint8_t ty = HHP(vm, WV_PAY(v))->type; if (ty == H_STR) return wv_atom(A_string); if (ty == H_FUNC || ty == H_BOUND) return wv_atom(A_function); }
    return wv_atom(A_object);
}

static wv wb_arr_join(eccles_web_vm_t *vm, wv arr, const char *sep, uint32_t seplen);
static wv wb_tostr(eccles_web_vm_t *vm, wv v) {
    char buf[48]; uint32_t t = WV_TAGOF(v);
    if (t == WV_T_ATOM) return v;
    if (WV_ISNUM(v)) { uint32_t n = wb_num_str(wv_dbl(v), buf); return wb_str_new(vm, buf, n); }
    if (v == WV_UNDEF) return wv_atom(A_undefined);
    if (v == WV_NULL) return wv_atom(A_null);
    if (v == WV_TRUE) return wv_atom(A_true);
    if (v == WV_FALSE) return wv_atom(A_false);
    if (t == WV_T_OBJ) {
        uint8_t ty = HHP(vm, WV_PAY(v))->type;
        if (ty == H_STR) return v;
        if (ty == H_ARR) return wb_arr_join(vm, v, ",", 1);
        if (ty == H_FUNC || ty == H_BOUND) return wb_str_cstr(vm, "function () { [native code] }");
        if (ty == H_PROMISE) return wb_str_cstr(vm, "[object Promise]");
        if (ty == H_OBJ) {  //errors print as "Name: message"
            int im = wb_obj_find(vm, WV_PAY(v), A_message);
            if (im >= 0) {
                int in = wb_obj_find(vm, WV_PAY(v), A_name); wv name = in >= 0 ? *wb_obj_slot(vm, WV_PAY(v), in) : wv_atom(A_Error), msg = *wb_obj_slot(vm, WV_PAY(v), im), r;
                wb_root(vm, v);
                r = wb_str_concat(vm, wb_tostr(vm, name), wb_str_cstr(vm, ": "));
                if (!vm->jexc_pending) { wb_root(vm, r); r = wb_str_concat(vm, r, wb_tostr(vm, msg)); wb_unroot(vm, 1); }
                wb_unroot(vm, 1);
                return r;
            }
            return wv_atom(A_object_Object);
        }
    }
    if (t == WV_T_ELEM) return wv_atom(A_object_HTMLElement);
    if (t == WV_T_NAT) return wb_str_cstr(vm, "function () { [native code] }");
    return wv_atom(A_object_Object);
}
static uint32_t wb_cstr_of(eccles_web_vm_t *vm, wv v, char *buf, uint32_t cap) {
    wv s = wb_tostr(vm, v); uint32_t n; const char *p;
    if (vm->jexc_pending || !cap) { if (cap) buf[0] = 0; return 0; }
    p = wb_str_ptr(vm, s, &n);
    if (n >= cap) n = cap - 1;
    memcpy(buf, p, n); buf[n] = 0;
    return n;
}
//a property key from any value (interned, so keys compare as numbers)
static uint16_t wb_key_atom(eccles_web_vm_t *vm, wv k) {
    uint32_t n; const char *p; char buf[48]; uint16_t a;
    if (WV_TAGOF(k) == WV_T_ATOM) return (uint16_t)WV_PAY(k);
    if (WV_ISNUM(k)) { n = wb_num_str(wv_dbl(k), buf); p = buf; }
    else if (wb_is_str(vm, k)) p = wb_str_ptr(vm, k, &n);
    else { wv s = wb_tostr(vm, k); if (vm->jexc_pending) return 0; p = wb_str_ptr(vm, s, &n); }
    a = wb_atom_find(vm, p, n);
    if (!a && n == 0) return 0;
    if (!a) { a = wb_atom(vm, p, n); if (!a) { vm->err = ECCLES_WEB_OK; wb_throw_err(vm, "RangeError", "too many property names (ECCLES_WEB_MAX_ATOMS)"); } }
    return a;
}

//equality
static int wb_strict_eq(const eccles_web_vm_t *vm, wv a, wv b) {
    if (WV_ISNUM(a) && WV_ISNUM(b)) return wv_dbl(a) == wv_dbl(b);
    if (a == b) return 1;
    if (wb_is_str(vm, a) && wb_is_str(vm, b)) { uint32_t la, lb; const char *pa = wb_str_ptr(vm, a, &la), *pb = wb_str_ptr(vm, b, &lb); return la == lb && memcmp(pa, pb, la) == 0; }
    return 0;
}
static int wb_loose_eq(eccles_web_vm_t *vm, wv a, wv b) {
    if (wb_is_nullish(a) || wb_is_nullish(b)) return wb_is_nullish(a) && wb_is_nullish(b);
    if (wb_strict_eq(vm, a, b)) return 1;
    if ((WV_ISNUM(a) || a == WV_TRUE || a == WV_FALSE || wb_is_str(vm, a)) && (WV_ISNUM(b) || b == WV_TRUE || b == WV_FALSE || wb_is_str(vm, b))) {
        if (wb_is_str(vm, a) && wb_is_str(vm, b)) return 0;
        return wb_tonum(vm, a) == wb_tonum(vm, b);
    }
    if ((wb_is_str(vm, a) || WV_ISNUM(a)) && wb_is_arr(vm, b)) { wv s = wb_tostr(vm, b); return wb_loose_eq(vm, a, s); }
    return 0;
}

//property access on any value
static wv wb_obj_get(const eccles_web_vm_t *vm, uint32_t obj, uint16_t key, int *found) {
    uint32_t cur = obj; int depth = 0;
    while (cur && depth++ < 32) {
        int i = wb_obj_find(vm, cur, key);
        if (i >= 0) { *found = 1; return *(wv *)(vm->jheap + ((wb_hobj_t *)(vm->jheap + cur))->tab + 8 + (size_t)i * 16 + 8); }
        cur = ((wb_hobj_t *)(vm->jheap + cur))->proto;
    }
    *found = 0; return WV_UNDEF;
}
static wv wb_func_prototype(eccles_web_vm_t *vm, wv f) {  //the .prototype object of a function, made on first use
    wb_hfunc_t *fn = (wb_hfunc_t *)(vm->jheap + WV_PAY(f));
    if (!fn->pobj) {
        wv o; wb_root(vm, f); o = wb_obj_new(vm, 0); wb_unroot(vm, 1);
        if (vm->jexc_pending) return WV_UNDEF;
        fn = (wb_hfunc_t *)(vm->jheap + WV_PAY(f)); fn->pobj = WV_PAY(o);
    }
    return wv_obj(fn->pobj);
}
static void wb_type_error(eccles_web_vm_t *vm, const char *msg) { wb_throw_err(vm, "TypeError", msg); }

static wv wb_getprop(eccles_web_vm_t *vm, wv o, uint16_t atom) {
    uint32_t t = WV_TAGOF(o); wv r;
    if (t == WV_T_OBJ) {
        uint8_t ty = HHP(vm, WV_PAY(o))->type; int found;
        switch (ty) {
            case H_OBJ: { wv v = wb_obj_get(vm, WV_PAY(o), atom, &found); if (found) return v; r = wb_nat_value(K_OBJ, atom); if (r != WV_NONE) return r;
                          { uint8_t k = (uint8_t)((HHP(vm, WV_PAY(o))->aux >> 8)); if (k) { r = wb_nat_value(k, atom); if (r != WV_NONE) return r; } }  //objects tagged with a builtin kind (Response, wasm instance...)
                          return WV_UNDEF; }
            case H_ARR: if (atom == A_length) return wv_num((double)wb_arr(vm, o)->len); r = wb_nat_value(K_ARR, atom); return r != WV_NONE ? r : WV_UNDEF;
            case H_STR: if (atom == A_length) { uint32_t n; wb_str_ptr(vm, o, &n); return wv_num((double)wb_utf8_len(wb_str_ptr(vm, o, &n), n)); } r = wb_nat_value(K_STR, atom); return r != WV_NONE ? r : WV_UNDEF;
            case H_FUNC: { uint32_t pr = ((wb_hfunc_t *)(vm->jheap + WV_PAY(o)))->props; if (pr) { wv v = wb_obj_get(vm, pr, atom, &found); if (found) return v; } }
                         if (atom == A_prototype) return wb_func_prototype(vm, o); /* fallthrough */
            case H_BOUND: r = wb_nat_value(K_FUNC, atom); return r != WV_NONE ? r : WV_UNDEF;
            case H_PROMISE: r = wb_nat_value(K_PROMISE, atom); return r != WV_NONE ? r : WV_UNDEF;
            default: return WV_UNDEF;
        }
    }
    if (t == WV_T_ATOM) { if (atom == A_length) { uint32_t n; const char *p = wb_str_ptr(vm, o, &n); return wv_num((double)wb_utf8_len(p, n)); } r = wb_nat_value(K_STR, atom); return r != WV_NONE ? r : WV_UNDEF; }
    if (WV_ISNUM(o)) { r = wb_nat_value(K_NUM, atom); return r != WV_NONE ? r : WV_UNDEF; }
    if (t == WV_T_ELEM || t == WV_T_DOMX) return wb_dom_get(vm, o, atom);
    if (t == WV_T_NAT) {
        uint8_t k = wb_nat_statics(WV_PAY(o));
        if (k) { r = wb_nat_value(k, atom); if (r != WV_NONE) return r; }
        r = wb_nat_value(K_FUNC, atom); return r != WV_NONE ? r : WV_UNDEF;
    }
    if (o == WV_UNDEF || o == WV_NULL) { char m[ECCLES_WEB_MAX_NAME + 40]; snprintf(m, sizeof m, "Cannot read properties of %s (reading '%.24s')", o == WV_UNDEF ? "undefined" : "null", wb_astr(vm, atom)); wb_type_error(vm, m); }
    return WV_UNDEF;
}
//converts an array index key ("3", or the number 3) to an index, -1 if it is not one
static int64_t wb_index_of(const eccles_web_vm_t *vm, wv k) {
    if (WV_ISNUM(k)) { double d = wv_dbl(k); if (d >= 0 && d < 4294967295.0 && d == floor(d)) return (int64_t)d; return -1; }
    (void)vm; return -1;
}
static wv wb_utf8_char_at(eccles_web_vm_t *vm, wv s, uint32_t idx) {  //idx counts code points
    uint32_t n, i = 0, c = 0; const char *p = wb_str_ptr(vm, s, &n);
    while (i < n) {
        uint32_t l = 1; uint8_t b = (uint8_t)p[i];
        if (b >= 0xF0) l = 4; else if (b >= 0xE0) l = 3; else if (b >= 0xC0) l = 2;
        if (c == idx) return wb_str_new(vm, p + i, i + l <= n ? l : n - i);
        i += l; c++;
    }
    return WV_UNDEF;
}
static wv wb_getidx(eccles_web_vm_t *vm, wv o, wv k) {
    int64_t ix = wb_index_of(vm, k);
    if (wb_is_arr(vm, o)) { if (ix >= 0) { wb_harr_t *a = wb_arr(vm, o); return (uint64_t)ix < a->len ? wb_arr_el(vm, o)[ix] : WV_UNDEF; } }
    else if (wb_is_str(vm, o)) { if (ix >= 0) return wb_utf8_char_at(vm, o, (uint32_t)ix); }
    else if (o == WV_UNDEF || o == WV_NULL) { wb_type_error(vm, "Cannot read properties of undefined or null"); return WV_UNDEF; }
    { uint16_t a = wb_key_atom(vm, k); if (vm->jexc_pending) return WV_UNDEF; return wb_getprop(vm, o, a); }
}
static void wb_arr_set_len(eccles_web_vm_t *vm, wv a, uint32_t len);
static void wb_setprop(eccles_web_vm_t *vm, wv o, uint16_t atom, wv v) {
    uint32_t t = WV_TAGOF(o);
    if (t == WV_T_OBJ) {
        uint8_t ty = HHP(vm, WV_PAY(o))->type;
        if (ty == H_OBJ) wb_obj_set(vm, WV_PAY(o), atom, v);
        else if (ty == H_ARR && atom == A_length) wb_arr_set_len(vm, o, (uint32_t)wb_tonum(vm, v));
        else if (ty == H_FUNC && atom == A_prototype && WV_TAGOF(v) == WV_T_OBJ) ((wb_hfunc_t *)(vm->jheap + WV_PAY(o)))->pobj = WV_PAY(v);
        else if (ty == H_FUNC) {  //static members and expando properties of a function
            wb_hfunc_t *f = (wb_hfunc_t *)(vm->jheap + WV_PAY(o));
            if (!f->props) { wv po; wb_root(vm, o); wb_root(vm, v); po = wb_obj_new(vm, 0); v = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 2); if (vm->jexc_pending) return; f = (wb_hfunc_t *)(vm->jheap + WV_PAY(o)); f->props = WV_PAY(po); }
            wb_obj_set(vm, f->props, atom, v);
        }
    } else if (t == WV_T_ELEM || t == WV_T_DOMX) wb_dom_set(vm, o, atom, v);
    else if (o == WV_UNDEF || o == WV_NULL) { char m[ECCLES_WEB_MAX_NAME + 40]; snprintf(m, sizeof m, "Cannot set properties of %s (setting '%.24s')", o == WV_UNDEF ? "undefined" : "null", wb_astr(vm, atom)); wb_type_error(vm, m); }
}
static void wb_arr_set_len(eccles_web_vm_t *vm, wv a, uint32_t len) {
    wb_harr_t *r = wb_arr(vm, a); uint32_t i;
    if (len > 100000u) { wb_throw_err(vm, "RangeError", "array too large"); return; }
    while (r->len < len) { if (!wb_arr_push(vm, a, WV_UNDEF)) return; r = wb_arr(vm, a); }
    if (len < r->len) { (void)i; r->len = len; }
}
static void wb_setidx(eccles_web_vm_t *vm, wv o, wv k, wv v) {
    int64_t ix = wb_index_of(vm, k);
    if (wb_is_arr(vm, o) && ix >= 0) {
        wb_harr_t *a;
        if (ix > 100000) { wb_throw_err(vm, "RangeError", "array index too large"); return; }
        a = wb_arr(vm, o);
        if ((uint64_t)ix >= a->len) { wb_root(vm, v); wb_arr_set_len(vm, o, (uint32_t)ix + 1); v = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1); if (vm->jexc_pending) return; }
        wb_arr_el(vm, o)[ix] = v; return;
    }
    { uint16_t a = wb_key_atom(vm, k); if (vm->jexc_pending) return; wb_setprop(vm, o, a, v); }
}
static int wb_delprop(eccles_web_vm_t *vm, wv o, uint16_t key) {
    int i;
    if (!wb_is_obj_type(vm, o, H_OBJ)) return 1;
    i = wb_obj_find(vm, WV_PAY(o), key);
    if (i >= 0) {
        wb_hobj_t *ob = (wb_hobj_t *)(vm->jheap + WV_PAY(o)); uint8_t *tab = vm->jheap + ob->tab + 8;
        memmove(tab + (size_t)i * 16, tab + (size_t)(i + 1) * 16, (size_t)(ob->n - (uint32_t)i - 1) * 16); ob->n--;
    }
    return 1;
}

static wv wb_arr_join(eccles_web_vm_t *vm, wv arr, const char *sep, uint32_t seplen) {
    uint32_t i, total = 0, n = wb_arr(vm, arr)->len; wv *parts; wv res; uint8_t rooted = 0; uint32_t off; char *dst;
    if (n == 0) return wv_atom(0);
    wb_root(vm, arr); rooted++;
    //first pass converts every element to a string (rooted in a temp array), second pass concatenates
    { wv tmp = wb_arr_new(vm, n); if (vm->jexc_pending) { wb_unroot(vm, rooted); return WV_UNDEF; } wb_root(vm, tmp); rooted++;
      for (i = 0; i < n; i++) {
          wv e = wb_arr_el(vm, arr)[i], s;
          if (wb_is_nullish(e)) s = wv_atom(0); else s = wb_tostr(vm, e);
          if (vm->jexc_pending) { wb_unroot(vm, rooted); return WV_UNDEF; }
          wb_arr_push(vm, tmp, s);
      }
      parts = wb_arr_el(vm, tmp);
      for (i = 0; i < n; i++) { uint32_t l; wb_str_ptr(vm, parts[i], &l); total += l; }
      total += seplen * (n - 1);
      off = wb_galloc(vm, H_STR, 4 + total + 1);
      if (!off) { wb_unroot(vm, rooted); return WV_UNDEF; }
      parts = wb_arr_el(vm, tmp);
      { wb_hstr_t *s = (wb_hstr_t *)(vm->jheap + off); s->len = total; dst = s->s; }
      for (i = 0; i < n; i++) { uint32_t l; const char *p = wb_str_ptr(vm, parts[i], &l); if (i) { memcpy(dst, sep, seplen); dst += seplen; } memcpy(dst, p, l); dst += l; }
      *dst = 0; }
    wb_unroot(vm, rooted);
    res = wv_obj(off);
    if (total == 0) return wv_atom(0);
    return res;
}

//promises
static wv wb_promise_new(eccles_web_vm_t *vm) {
    uint32_t off = wb_galloc(vm, H_PROMISE, sizeof(wb_hprom_t) - sizeof(wb_hh_t));
    if (!off) return WV_UNDEF;
    ((wb_hprom_t *)(vm->jheap + off))->value = WV_UNDEF;
    return wv_obj(off);
}
enum { MT_REACT = 1, MT_QUEUE = 2 };
static void wb_mt_push(eccles_web_vm_t *vm, uint8_t kind, uint8_t flag, wv a, wv b, wv c) {
    uint32_t i;
    if (vm->mt_len >= ECCLES_WEB_JS_MAX_MICROTASKS) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 9); if (!vm->jexc_pending) wb_throw_err(vm, "RangeError", "too many pending promise jobs"); return; }
    i = (uint32_t)(vm->mt_head + vm->mt_len) % ECCLES_WEB_JS_MAX_MICROTASKS;
    vm->mt[i].kind = kind; vm->mt[i].flag = flag; vm->mt[i].a = a; vm->mt[i].b = b; vm->mt[i].c = c; vm->mt_len++;
}
//settles a promise and queues everything that was waiting for it
static void wb_promise_settle(eccles_web_vm_t *vm, wv p, uint32_t state, wv value) {
    wb_hprom_t *pr = (wb_hprom_t *)(vm->jheap + WV_PAY(p)); uint32_t reactions;
    if (pr->state) return;
    pr->state = state; pr->value = value; reactions = pr->reactions; pr->reactions = 0;
    if (reactions) {
        wv ra = wv_obj(reactions); uint32_t i, n = wb_arr(vm, ra)->len;
        for (i = 0; i + 2 < n; i += 3) {
            wv *el = wb_arr_el(vm, ra);
            wb_mt_push(vm, MT_REACT, (uint8_t)(state == 2), state == 1 ? el[i] : el[i + 1], value, el[i + 2]);
            if (vm->jexc_pending) return;
        }
    }
}
static void wb_promise_resolve(eccles_web_vm_t *vm, wv p, wv v);
static void wb_promise_react(eccles_web_vm_t *vm, wv p, wv onf, wv onr, wv child) {
    wb_hprom_t *pr = (wb_hprom_t *)(vm->jheap + WV_PAY(p));
    pr->handled = 1;
    if (pr->state == 0) {
        wv ra;
        wb_root(vm, p); wb_root(vm, onf); wb_root(vm, onr); wb_root(vm, child);
        pr = (wb_hprom_t *)(vm->jheap + WV_PAY(p));
        if (!pr->reactions) { ra = wb_arr_new(vm, 6); if (vm->jexc_pending) { wb_unroot(vm, 4); return; } pr = (wb_hprom_t *)(vm->jheap + WV_PAY(p)); pr->reactions = WV_PAY(ra); }
        ra = wv_obj(pr->reactions);
        wb_arr_push(vm, ra, vm->jroots[vm->njroots - 3]); wb_arr_push(vm, ra, vm->jroots[vm->njroots - 2]); wb_arr_push(vm, ra, vm->jroots[vm->njroots - 1]);
        wb_unroot(vm, 4);
    } else {
        wb_mt_push(vm, MT_REACT, (uint8_t)(pr->state == 2), pr->state == 1 ? onf : onr, pr->value, child);
    }
}
static void wb_promise_resolve(eccles_web_vm_t *vm, wv p, wv v) {
    wb_hprom_t *pr = (wb_hprom_t *)(vm->jheap + WV_PAY(p));
    if (pr->state) return;
    if (v == p) { wv e; wb_throw_err(vm, "TypeError", "promise resolved with itself"); e = vm->jexc; vm->jexc_pending = 0; wb_promise_settle(vm, p, 2, e); return; }
    if (wb_is_obj_type(vm, v, H_PROMISE)) { wb_promise_react(vm, v, WV_UNDEF, WV_UNDEF, p); return; }  //adopt its result
    wb_promise_settle(vm, p, 1, v);
}
static void wb_promise_reject(eccles_web_vm_t *vm, wv p, wv v) { if (!((wb_hprom_t *)(vm->jheap + WV_PAY(p)))->state) wb_promise_settle(vm, p, 2, v); }

//function calls
static wv wb_run_nested(eccles_web_vm_t *vm, uint32_t base, int argc);
static wv wb_call(eccles_web_vm_t *vm, wv fn, wv thisv, const wv *args, int argc) {
    uint32_t base; int i;
    if (vm->jnative_depth >= 12) { wb_throw_err(vm, "RangeError", "call stack too deep"); return WV_UNDEF; }
    if (vm->jsp + (uint32_t)argc + 2 >= ECCLES_WEB_JS_STACK_SLOTS) { wb_fail(vm, ECCLES_WEB_TRAP_JS_STACK, 0); wb_throw_err(vm, "RangeError", "stack overflow"); return WV_UNDEF; }
    base = vm->jsp;
    vm->jstk[vm->jsp++] = fn; vm->jstk[vm->jsp++] = thisv;
    for (i = 0; i < argc; i++) vm->jstk[vm->jsp++] = args[i];
    return wb_run_nested(vm, base, argc);
}

//the run loop
#define WB_STK vm->jstk
#define WB_PUSH(v) do { if (WB_UNLIKELY(vm->jsp >= ECCLES_WEB_JS_STACK_SLOTS)) { wb_trap(vm, ECCLES_WEB_TRAP_JS_STACK); goto trapped; } WB_STK[vm->jsp++] = (v); } while (0)
#define WB_POP() (WB_STK[--vm->jsp])
#define WB_TOP() (WB_STK[vm->jsp - 1])
#define WB_ENVP(off) ((wb_henv_t *)(vm->jheap + (off)))

static int wb_nat_instanceof(eccles_web_vm_t *vm, wv a, uint32_t natid);

//a trap is not catchable by the script: the whole call is thrown away
static void wb_trap(eccles_web_vm_t *vm, eccles_web_error_t e) { if (!vm->jtrap) vm->jtrap = (uint8_t)e; }

static wv wb_call_bound_or_native(eccles_web_vm_t *vm, wv callee, uint32_t base, int argc) {
    const wv *args = &vm->jstk[base + 2]; wv self = vm->jstk[base + 1];
    if (WV_TAGOF(callee) == WV_T_NAT) {
        uint32_t id = WV_PAY(callee);
        if (id >= 0x10000u) return wb_host_call(vm, id - 0x10000u, args, argc);
        return wb_call_native(vm, id, self, args, argc);
    } else {
        wb_hbound_t *b = (wb_hbound_t *)(vm->jheap + WV_PAY(callee));
        vm->jbd1 = b->d1;
        return wb_call_native(vm, b->nat, b->d0, args, argc);
    }
}

//pushes the frame of a closure call. the stack holds [fn, this, args...] from `base`. returns 0 when it threw (or trapped)
static int wb_enter(eccles_web_vm_t *vm, uint32_t base, int argc, int ctor, int boundary) {
    wv fnv = vm->jstk[base]; wb_hfunc_t *f = (wb_hfunc_t *)(vm->jheap + WV_PAY(fnv)); const wb_proto_t *p = &vm->protos[f->proto];
    uint8_t flags = p->flags; uint32_t np = p->nparams, i, envoff; wv prom = WV_UNDEF, rest = WV_UNDEF; uint8_t rooted = 0; wb_henv_t *env; wb_jframe_t *fr;
    if (vm->jnf >= ECCLES_WEB_JS_CALL_DEPTH) { wb_throw_err(vm, "RangeError", "call stack too deep"); return 0; }
    if (flags & WB_FN_ASYNC) { prom = wb_promise_new(vm); if (vm->jexc_pending) return 0; wb_root(vm, prom); rooted++; }
    if (flags & WB_FN_REST) {
        uint32_t fixed = np ? np - 1 : 0; int k;
        rest = wb_arr_new(vm, argc > (int)fixed ? (uint32_t)argc - fixed : 0);
        if (vm->jexc_pending) { wb_unroot(vm, rooted); return 0; }
        wb_root(vm, rest); rooted++;
        for (k = (int)fixed; k < argc; k++) wb_arr_push(vm, rest, vm->jstk[base + 2 + (uint32_t)k]);
    }
    envoff = wb_galloc(vm, H_ENV, 8u + (uint32_t)p->nslots * 8u);
    if (!envoff) { wb_unroot(vm, rooted); return 0; }
    f = (wb_hfunc_t *)(vm->jheap + WV_PAY(fnv));
    env = WB_ENVP(envoff); env->parent = f->env; env->n = p->nslots;
    for (i = 0; i < p->nslots; i++) env->slots[i] = WV_UNDEF;
    for (i = 0; i < np && i < (uint32_t)argc; i++) env->slots[i] = vm->jstk[base + 2 + i];
    if ((flags & WB_FN_REST) && np) env->slots[np - 1] = rest;
    fr = &vm->jframes[vm->jnf++];
    fr->pc = vm->jcode + p->code; fr->env = envoff; fr->thisv = (flags & WB_FN_ARROW) ? f->thisv : vm->jstk[base + 1]; fr->prom = prom; fr->base = base;
    fr->proto = f->proto; fr->ctor = (uint8_t)ctor; fr->boundary = (uint8_t)boundary; fr->is_async = (flags & WB_FN_ASYNC) ? 1 : 0;
    vm->jsp = base;
    wb_unroot(vm, rooted);
    return 1;
}

//dispatches a call whose stack is [fn, this, args...] at base. 1 = a script frame was pushed, 0 = done (result pushed), -1 = threw
static int wb_dispatch(eccles_web_vm_t *vm, uint32_t base, int argc, int ctor, int boundary) {
    wv callee = vm->jstk[base]; wv r;
    if (WV_TAGOF(callee) == WV_T_OBJ && HHP(vm, WV_PAY(callee))->type == H_FUNC) return wb_enter(vm, base, argc, ctor, boundary) ? 1 : -1;
    if (!wb_is_callable(vm, callee)) { wb_type_error(vm, "value is not a function"); return -1; }
    if (vm->jnative_depth >= 12) { wb_throw_err(vm, "RangeError", "call stack too deep"); return -1; }
    r = wb_call_bound_or_native(vm, callee, base, argc);
    if (vm->jexc_pending || vm->jtrap) return -1;
    vm->jsp = base; vm->jstk[vm->jsp++] = r;
    return 0;
}

//exception unwinding. 1 = a handler took it, 2 = an async function absorbed it (execution goes on in its caller), 0 = it escapes the run loop
static int wb_unwind(eccles_web_vm_t *vm, uint32_t stop_nf) {
    while (vm->jnf > stop_nf) {
        wb_jframe_t *fr = &vm->jframes[vm->jnf - 1];
        if (vm->ntry && vm->jtry[vm->ntry - 1].frame == vm->jnf - 1) {
            wb_jtry_t h = vm->jtry[--vm->ntry];
            vm->jsp = h.sp; fr->env = h.env; fr->pc = h.pc;
            vm->jstk[vm->jsp++] = vm->jexc; vm->jexc_pending = 0;
            return 1;
        }
        if (fr->is_async) {  //the exception rejects the promise of the async function, its caller just gets the promise
            wv prom = fr->prom, e = vm->jexc;
            uint32_t base = fr->base; int boundary = fr->boundary;
            vm->jexc_pending = 0;
            wb_root(vm, e);
            wb_promise_reject(vm, prom, e);
            wb_unroot(vm, 1);
            vm->jnf--; vm->jsp = base; vm->jstk[vm->jsp++] = prom;
            (void)boundary;
            return 2;
        }
        vm->jsp = fr->base; vm->jnf--;
    }
    return 0;
}

//suspends the running async function: its frame and stack slice move into a heap coroutine that a promise reaction resumes later
static int wb_await(eccles_web_vm_t *vm, wv v, const uint8_t *pc_after) {
    wb_jframe_t *fr = &vm->jframes[vm->jnf - 1]; wv pr, cor; uint32_t n, k = 0, i, off; wb_hcoro_t *c; uint32_t *tp; wv *sp;
    wb_root(vm, v);
    if (wb_is_obj_type(vm, v, H_PROMISE)) pr = v;
    else {
        pr = wb_promise_new(vm); if (vm->jexc_pending) { wb_unroot(vm, 1); return 0; }
        wb_promise_settle(vm, pr, 1, v);
    }
    wb_root(vm, pr);
    fr = &vm->jframes[vm->jnf - 1];
    n = vm->jsp - fr->base;
    while (k < vm->ntry && vm->jtry[vm->ntry - 1 - k].frame == vm->jnf - 1) k++;
    off = wb_galloc(vm, H_CORO, (uint32_t)(sizeof(wb_hcoro_t) - sizeof(wb_hh_t)) + k * 12u + n * 8u);
    if (!off) { wb_unroot(vm, 2); return 0; }
    fr = &vm->jframes[vm->jnf - 1];
    c = (wb_hcoro_t *)(vm->jheap + off);
    c->proto = fr->proto; c->env = fr->env; c->thisv = fr->thisv; c->pc = (uint32_t)(pc_after - vm->jcode); c->nstack = n; c->prom = WV_PAY(fr->prom); c->ntry = k;
    tp = (uint32_t *)(c + 1);
    for (i = 0; i < k; i++) { wb_jtry_t *h = &vm->jtry[vm->ntry - k + i]; tp[i * 3] = (uint32_t)(h->pc - vm->jcode); tp[i * 3 + 1] = h->sp - fr->base; tp[i * 3 + 2] = h->env; }
    sp = (wv *)(tp + k * 3);
    memcpy(sp, &vm->jstk[fr->base], (size_t)n * 8);
    cor = wv_obj(off);
    wb_promise_react(vm, vm->jroots[vm->njroots - 1], cor, cor, WV_UNDEF);
    wb_unroot(vm, 2);
    if (vm->jexc_pending) return 0;
    fr = &vm->jframes[vm->jnf - 1];
    vm->ntry = (uint8_t)(vm->ntry - k);
    { wv prom = fr->prom; uint32_t base = fr->base; vm->jnf--; vm->jsp = base; vm->jstk[vm->jsp++] = prom; }
    return 1;
}

static void wb_report_exc(eccles_web_vm_t *vm);
static int wb_run(eccles_web_vm_t *vm, uint32_t stop_nf, int start_exc);

//runs a stack prepared as [fn, this, args...] at base to completion
static wv wb_run_nested(eccles_web_vm_t *vm, uint32_t base, int argc) {
    uint32_t nf0 = vm->jnf; wv callee = vm->jstk[base], res = WV_UNDEF; int rc;
    if (WV_TAGOF(callee) == WV_T_OBJ && HHP(vm, WV_PAY(callee))->type == H_FUNC) {
        if (!wb_enter(vm, base, argc, 0, 1)) { vm->jsp = base; return WV_UNDEF; }
        vm->jnative_depth++;
        rc = wb_run(vm, nf0, 0);
        vm->jnative_depth--;
        if (rc == 0) res = vm->jstk[base];
        vm->jsp = base;
        return res;
    }
    if (!wb_is_callable(vm, callee)) { wb_type_error(vm, "value is not a function"); vm->jsp = base; return WV_UNDEF; }
    vm->jnative_depth++;
    res = wb_call_bound_or_native(vm, callee, base, argc);
    vm->jnative_depth--;
    vm->jsp = base;
    return res;
}

static int wb_nat_in_obj(eccles_web_vm_t *vm, wv o, wv k) {
    if (wb_is_arr(vm, o)) { int64_t ix = wb_index_of(vm, k); if (ix >= 0) return (uint64_t)ix < wb_arr(vm, o)->len; }
    if (wb_is_obj_type(vm, o, H_OBJ)) { int f; uint16_t a = wb_key_atom(vm, k); if (vm->jexc_pending) return 0; wb_obj_get(vm, WV_PAY(o), a, &f); return f; }
    return 0;
}

static int wb_cmp_lt(eccles_web_vm_t *vm, wv a, wv b, int *undef) {  //a < b, *undef set for NaN
    *undef = 0;
    if (wb_is_str(vm, a) && wb_is_str(vm, b)) {
        uint32_t la, lb; const char *pa = wb_str_ptr(vm, a, &la), *pb = wb_str_ptr(vm, b, &lb); uint32_t m = la < lb ? la : lb; int c = memcmp(pa, pb, m);
        return c < 0 || (c == 0 && la < lb);
    }
    { double x = wb_tonum(vm, a), y = wb_tonum(vm, b); if (x != x || y != y) { *undef = 1; return 0; } return x < y; }
}

static wv wb_keys_of(eccles_web_vm_t *vm, wv o) {
    wv arr = wb_arr_new(vm, 0); uint32_t i;
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, arr);
    if (wb_is_obj_type(vm, o, H_OBJ)) {
        for (i = 0; i < ((wb_hobj_t *)(vm->jheap + WV_PAY(o)))->n; i++) {
            uint16_t key = (uint16_t)*(uint32_t *)(vm->jheap + ((wb_hobj_t *)(vm->jheap + WV_PAY(o)))->tab + 8 + (size_t)i * 16);
            wb_arr_push(vm, arr, wv_atom(key));
        }
    } else if (wb_is_arr(vm, o) || wb_is_str(vm, o)) {
        uint32_t n = wb_is_arr(vm, o) ? wb_arr(vm, o)->len : 0, l;
        if (!wb_is_arr(vm, o)) { const char *p = wb_str_ptr(vm, o, &l); n = wb_utf8_len(p, l); }
        for (i = 0; i < n; i++) { char b[16]; uint32_t k = (uint32_t)snprintf(b, sizeof b, "%u", i); wv s = wb_str_new(vm, b, k); if (vm->jexc_pending) break; wb_arr_push(vm, arr, s); }
    }
    wb_unroot(vm, 1);
    return arr;
}
static wv wb_iter_of(eccles_web_vm_t *vm, wv o) {
    if (wb_is_arr(vm, o)) return o;
    if (wb_is_str(vm, o)) {
        uint32_t n, i = 0; const char *p = wb_str_ptr(vm, o, &n); wv arr = wb_arr_new(vm, 0);
        if (vm->jexc_pending) return WV_UNDEF;
        wb_root(vm, o); wb_root(vm, arr);
        while (i < n && !vm->jexc_pending) {
            uint32_t l = 1; uint8_t b; p = wb_str_ptr(vm, o, &n); b = (uint8_t)p[i];
            if (b >= 0xF0) l = 4; else if (b >= 0xE0) l = 3; else if (b >= 0xC0) l = 2;
            { wv s = wb_str_new(vm, p + i, i + l <= n ? l : n - i); if (vm->jexc_pending) break; wb_arr_push(vm, arr, s); }
            i += l;
        }
        wb_unroot(vm, 2);
        return arr;
    }
    wb_type_error(vm, "value is not iterable");
    return WV_UNDEF;
}

#define WB_NUMBIN(expr) { wv bv = WB_POP(), av = WB_TOP(); double x = WV_ISNUM(av) ? wv_dbl(av) : wb_tonum(vm, av), y = WV_ISNUM(bv) ? wv_dbl(bv) : wb_tonum(vm, bv); WB_TOP() = wv_num(expr); }
#define WB_INTBIN(expr) { wv bv = WB_POP(), av = WB_TOP(); int32_t x = wb_toi32(wb_tonum(vm, av)), y = wb_toi32(wb_tonum(vm, bv)); WB_TOP() = wv_num((double)(expr)); }
#define WB_TICK_BACK(off) do { if ((off) < 0) { \
        if (vm->fuel_budget && --vm->fuel_left == 0) { wb_trap(vm, ECCLES_WEB_TRAP_JS_FUEL); goto trapped; } \
        if (ECCLES_WEB_YIELD_INTERVAL && !(++vm->jops & (ECCLES_WEB_YIELD_INTERVAL - 1))) ECCLES_WEB_YIELD(); \
        else vm->jops++; } } while (0)

static int wb_run(eccles_web_vm_t *vm, uint32_t stop_nf, int start_exc) {
    wb_jframe_t *fr; const uint8_t *pc; uint8_t op; uint32_t base; int argc, rc;
#define SAVE() (fr->pc = pc)
#define RELOAD() do { fr = &vm->jframes[vm->jnf - 1]; pc = fr->pc; } while (0)
    RELOAD();
    if (start_exc) goto handle_exc;
    for (;;) {
        op = *pc++;
        switch (op) {
            case OP_UNDEF: WB_PUSH(WV_UNDEF); break;
            case OP_NULL: WB_PUSH(WV_NULL); break;
            case OP_TRUE: WB_PUSH(WV_TRUE); break;
            case OP_FALSE: WB_PUSH(WV_FALSE); break;
            case OP_THIS: WB_PUSH(fr->thisv); break;
            case OP_NUM8: WB_PUSH(wv_num((double)(int8_t)*pc)); pc++; break;
            case OP_NUM16: { int16_t v; memcpy(&v, pc, 2); pc += 2; WB_PUSH(wv_num((double)v)); break; }
            case OP_NUMF: { double d; memcpy(&d, pc, 8); pc += 8; WB_PUSH(wv_num(d)); break; }
            case OP_ATOM: { uint16_t a; memcpy(&a, pc, 2); pc += 2; WB_PUSH(wv_atom(a)); break; }
            case OP_POP: vm->jsp--; break;
            case OP_DUP: { wv v = WB_TOP(); WB_PUSH(v); break; }
            case OP_DUP2: { wv a = WB_STK[vm->jsp - 2], b = WB_STK[vm->jsp - 1]; WB_PUSH(a); WB_PUSH(b); break; }
            case OP_SWAP: { wv t = WB_STK[vm->jsp - 1]; WB_STK[vm->jsp - 1] = WB_STK[vm->jsp - 2]; WB_STK[vm->jsp - 2] = t; break; }
            case OP_GETL: WB_PUSH(WB_ENVP(fr->env)->slots[*pc++]); break;
            case OP_SETL: WB_ENVP(fr->env)->slots[*pc++] = WB_TOP(); break;
            case OP_GETU: { uint32_t e = fr->env; uint8_t h = *pc++; while (h--) e = WB_ENVP(e)->parent; WB_PUSH(WB_ENVP(e)->slots[*pc++]); break; }
            case OP_SETU: { uint32_t e = fr->env; uint8_t h = *pc++; while (h--) e = WB_ENVP(e)->parent; WB_ENVP(e)->slots[*pc++] = WB_TOP(); break; }
            case OP_GETG: {
                uint16_t a; int found; wv v; memcpy(&a, pc, 2); pc += 2;
                v = wb_obj_get(vm, WV_PAY(vm->jglobal), a, &found);
                if (!found) { char m[ECCLES_WEB_MAX_NAME + 32]; snprintf(m, sizeof m, "%.40s is not defined", wb_astr(vm, a)); SAVE(); wb_throw_err(vm, "ReferenceError", m); goto handle_exc; }
                WB_PUSH(v); break;
            }
            case OP_SETG: { uint16_t a; memcpy(&a, pc, 2); pc += 2; SAVE(); wb_obj_set(vm, WV_PAY(vm->jglobal), a, WB_TOP()); if (vm->jexc_pending) goto handle_exc; break; }
            case OP_TYPEOFG: { uint16_t a; int found; wv v; memcpy(&a, pc, 2); pc += 2; v = wb_obj_get(vm, WV_PAY(vm->jglobal), a, &found); WB_PUSH(found ? wb_typeof_atom(vm, v) : wv_atom(A_undefined)); break; }
            case OP_VARG: { uint16_t a; int found; memcpy(&a, pc, 2); pc += 2; wb_obj_get(vm, WV_PAY(vm->jglobal), a, &found); if (!found) { SAVE(); wb_obj_set(vm, WV_PAY(vm->jglobal), a, WV_UNDEF); if (vm->jexc_pending) goto handle_exc; } break; }
            case OP_GETF: { uint16_t a; wv o, v; memcpy(&a, pc, 2); pc += 2; o = WB_TOP(); SAVE(); v = wb_getprop(vm, o, a); if (vm->jexc_pending) goto handle_exc; WB_TOP() = v; break; }
            case OP_GETM: { uint16_t a; wv o, v; memcpy(&a, pc, 2); pc += 2; o = WB_TOP(); SAVE(); v = wb_getprop(vm, o, a); if (vm->jexc_pending) goto handle_exc; WB_TOP() = v; WB_PUSH(o); break; }
            case OP_SETF: { uint16_t a; wv v = WB_STK[vm->jsp - 1], o = WB_STK[vm->jsp - 2]; memcpy(&a, pc, 2); pc += 2; SAVE(); wb_setprop(vm, o, a, v); if (vm->jexc_pending) goto handle_exc; vm->jsp--; WB_TOP() = v; break; }
            case OP_GETI: { wv k = WB_STK[vm->jsp - 1], o = WB_STK[vm->jsp - 2], v; SAVE(); v = wb_getidx(vm, o, k); if (vm->jexc_pending) goto handle_exc; vm->jsp--; WB_TOP() = v; break; }
            case OP_GETIM: { wv k = WB_STK[vm->jsp - 1], o = WB_STK[vm->jsp - 2], v; SAVE(); v = wb_getidx(vm, o, k); if (vm->jexc_pending) goto handle_exc; WB_STK[vm->jsp - 2] = v; WB_STK[vm->jsp - 1] = o; break; }
            case OP_SETI: { wv v = WB_STK[vm->jsp - 1], k = WB_STK[vm->jsp - 2], o = WB_STK[vm->jsp - 3]; SAVE(); wb_setidx(vm, o, k, v); if (vm->jexc_pending) goto handle_exc; vm->jsp -= 2; WB_TOP() = v; break; }
            case OP_CALL: case OP_NEW: {
                int isnew = (op == OP_NEW); argc = *pc++;
                base = vm->jsp - (uint32_t)argc - (isnew ? 1u : 2u);
                SAVE();
                if (vm->fuel_budget && --vm->fuel_left == 0) { wb_trap(vm, ECCLES_WEB_TRAP_JS_FUEL); goto trapped; }
                if (isnew) {  //make room for `this`
                    wv ctor = WB_STK[base];
                    if (vm->jsp >= ECCLES_WEB_JS_STACK_SLOTS) { wb_trap(vm, ECCLES_WEB_TRAP_JS_STACK); goto trapped; }
                    memmove(&WB_STK[base + 2], &WB_STK[base + 1], (size_t)argc * 8); vm->jsp++;
                    if (WV_TAGOF(ctor) == WV_T_OBJ && HHP(vm, WV_PAY(ctor))->type == H_FUNC) {
                        wv po = wb_func_prototype(vm, ctor), o;
                        if (vm->jexc_pending) goto handle_exc;
                        o = wb_obj_new(vm, WV_PAY(po)); if (vm->jexc_pending) goto handle_exc;
                        WB_STK[base + 1] = o;
                        rc = wb_dispatch(vm, base, argc, 1, 0);
                    } else {
                        WB_STK[base + 1] = WV_UNDEF; vm->new_call = 1;
                        rc = wb_dispatch(vm, base, argc, 0, 0);
                        vm->new_call = 0;
                    }
                } else rc = wb_dispatch(vm, base, argc, 0, 0);
                if (vm->jtrap) goto trapped;
                if (rc < 0) goto handle_exc;
                if (rc == 1) RELOAD();
                break;
            }
            case OP_CALLA: case OP_NEWA: {
                int isnew2 = (op == OP_NEWA); wv arr; uint32_t n, i;
                arr = WB_POP(); n = wb_arr(vm, arr)->len;
                if (vm->jsp + n + 2 >= ECCLES_WEB_JS_STACK_SLOTS) { wb_trap(vm, ECCLES_WEB_TRAP_JS_STACK); goto trapped; }
                for (i = 0; i < n; i++) WB_STK[vm->jsp++] = wb_arr_el(vm, arr)[i];
                argc = (int)n;
                if (isnew2) { vm->jsp++; WB_STK[vm->jsp - 1] = WV_UNDEF; vm->jsp--; }
                {   /* same path as OP_CALL / OP_NEW from here */
                    int isnew = isnew2;
                    base = vm->jsp - (uint32_t)argc - (isnew ? 1u : 2u);
                    SAVE();
                    if (isnew) {
                        wv ctor = WB_STK[base];
                        memmove(&WB_STK[base + 2], &WB_STK[base + 1], (size_t)argc * 8); vm->jsp++;
                        if (WV_TAGOF(ctor) == WV_T_OBJ && HHP(vm, WV_PAY(ctor))->type == H_FUNC) {
                            wv po = wb_func_prototype(vm, ctor), o;
                            if (vm->jexc_pending) goto handle_exc;
                            o = wb_obj_new(vm, WV_PAY(po)); if (vm->jexc_pending) goto handle_exc;
                            WB_STK[base + 1] = o; rc = wb_dispatch(vm, base, argc, 1, 0);
                        } else { WB_STK[base + 1] = WV_UNDEF; vm->new_call = 1; rc = wb_dispatch(vm, base, argc, 0, 0); vm->new_call = 0; }
                    } else rc = wb_dispatch(vm, base, argc, 0, 0);
                    if (vm->jtrap) goto trapped;
                    if (rc < 0) goto handle_exc;
                    if (rc == 1) RELOAD();
                }
                break;
            }
            case OP_RETU: WB_PUSH(WV_UNDEF); /* fallthrough */
            case OP_RET: {
                wv v = WB_POP(); uint32_t b;
                if (fr->is_async) { wb_root(vm, v); wb_promise_resolve(vm, fr->prom, v); wb_unroot(vm, 1); v = fr->prom; if (vm->jexc_pending) { SAVE(); goto handle_exc; } }
                else if (fr->ctor && !(WV_TAGOF(v) == WV_T_OBJ && HHP(vm, WV_PAY(v))->type != H_STR)) v = fr->thisv;
                while (vm->ntry && vm->jtry[vm->ntry - 1].frame == vm->jnf - 1) vm->ntry--;
                b = fr->base; vm->jnf--; vm->jsp = b; WB_STK[vm->jsp++] = v;
                if (vm->jnf <= stop_nf) return 0;
                RELOAD();
                break;
            }
            case OP_JMP: { int16_t off; memcpy(&off, pc, 2); pc += 2; WB_TICK_BACK(off); pc += off; break; }
            case OP_JF: { int16_t off; wv v = WB_POP(); memcpy(&off, pc, 2); pc += 2; if (!wb_truthy(vm, v)) { WB_TICK_BACK(off); pc += off; } break; }
            case OP_JT: { int16_t off; wv v = WB_POP(); memcpy(&off, pc, 2); pc += 2; if (wb_truthy(vm, v)) { WB_TICK_BACK(off); pc += off; } break; }
            case OP_JFK: { int16_t off; memcpy(&off, pc, 2); pc += 2; if (!wb_truthy(vm, WB_TOP())) pc += off; else vm->jsp--; break; }
            case OP_JTK: { int16_t off; memcpy(&off, pc, 2); pc += 2; if (wb_truthy(vm, WB_TOP())) pc += off; else vm->jsp--; break; }
            case OP_JNN: { int16_t off; memcpy(&off, pc, 2); pc += 2; if (!wb_is_nullish(WB_TOP())) pc += off; else vm->jsp--; break; }
            case OP_OPTCHK: { int16_t off; memcpy(&off, pc, 2); pc += 2; if (wb_is_nullish(WB_TOP())) { WB_TOP() = WV_UNDEF; pc += off; } break; }
            case OP_ADD: {
                wv bv = WB_STK[vm->jsp - 1], av = WB_STK[vm->jsp - 2];
                if (WV_ISNUM(av) && WV_ISNUM(bv)) { vm->jsp--; WB_TOP() = wv_num(wv_dbl(av) + wv_dbl(bv)); break; }
                SAVE();
                if (wb_is_str(vm, av) || wb_is_str(vm, bv) || WV_TAGOF(av) == WV_T_OBJ || WV_TAGOF(bv) == WV_T_OBJ || WV_TAGOF(av) == WV_T_ELEM || WV_TAGOF(bv) == WV_T_ELEM) {
                    wv sa = wb_tostr(vm, av), sb, r;
                    if (vm->jexc_pending) goto handle_exc;
                    wb_root(vm, sa); sb = wb_tostr(vm, bv);
                    if (vm->jexc_pending) { wb_unroot(vm, 1); goto handle_exc; }
                    wb_root(vm, sb);
                    r = wb_str_concat(vm, vm->jroots[vm->njroots - 2], vm->jroots[vm->njroots - 1]);
                    wb_unroot(vm, 2);
                    if (vm->jexc_pending) goto handle_exc;
                    vm->jsp--; WB_TOP() = r;
                } else { vm->jsp--; WB_TOP() = wv_num(wb_tonum(vm, av) + wb_tonum(vm, bv)); }
                break;
            }
            case OP_SUB: WB_NUMBIN(x - y); break;
            case OP_MUL: WB_NUMBIN(x * y); break;
            case OP_DIV: WB_NUMBIN(x / y); break;
            case OP_MOD: WB_NUMBIN(fmod(x, y)); break;
            case OP_EXP: WB_NUMBIN(pow(x, y)); break;
            case OP_BAND: WB_INTBIN(x & y); break;
            case OP_BOR: WB_INTBIN(x | y); break;
            case OP_BXOR: WB_INTBIN(x ^ y); break;
            case OP_SHL: { wv bv = WB_POP(), av = WB_TOP(); WB_TOP() = wv_num((double)(int32_t)((uint32_t)wb_toi32(wb_tonum(vm, av)) << (wb_toi32(wb_tonum(vm, bv)) & 31))); break; }
            case OP_SHR: { wv bv = WB_POP(), av = WB_TOP(); WB_TOP() = wv_num((double)(wb_toi32(wb_tonum(vm, av)) >> (wb_toi32(wb_tonum(vm, bv)) & 31))); break; }
            case OP_USHR: { wv bv = WB_POP(), av = WB_TOP(); WB_TOP() = wv_num((double)((uint32_t)wb_toi32(wb_tonum(vm, av)) >> (wb_toi32(wb_tonum(vm, bv)) & 31))); break; }
            case OP_EQ: { wv bv = WB_POP(), av = WB_TOP(); WB_TOP() = wv_bool(wb_loose_eq(vm, av, bv)); break; }
            case OP_NE: { wv bv = WB_POP(), av = WB_TOP(); WB_TOP() = wv_bool(!wb_loose_eq(vm, av, bv)); break; }
            case OP_SEQ: { wv bv = WB_POP(), av = WB_TOP(); WB_TOP() = wv_bool(wb_strict_eq(vm, av, bv)); break; }
            case OP_SNE: { wv bv = WB_POP(), av = WB_TOP(); WB_TOP() = wv_bool(!wb_strict_eq(vm, av, bv)); break; }
            case OP_LT: { wv bv = WB_POP(), av = WB_TOP(); int u; int r = wb_cmp_lt(vm, av, bv, &u); WB_TOP() = wv_bool(r); break; }
            case OP_GT: { wv bv = WB_POP(), av = WB_TOP(); int u; int r = wb_cmp_lt(vm, bv, av, &u); WB_TOP() = wv_bool(r); break; }
            case OP_LE: { wv bv = WB_POP(), av = WB_TOP(); int u; int r = wb_cmp_lt(vm, bv, av, &u); WB_TOP() = wv_bool(!r && !u); break; }
            case OP_GE: { wv bv = WB_POP(), av = WB_TOP(); int u; int r = wb_cmp_lt(vm, av, bv, &u); WB_TOP() = wv_bool(!r && !u); break; }
            case OP_NEG: WB_TOP() = wv_num(-wb_tonum(vm, WB_TOP())); break;
            case OP_POS: case OP_TONUM: WB_TOP() = wv_num(wb_tonum(vm, WB_TOP())); break;
            case OP_NOT: WB_TOP() = wv_bool(!wb_truthy(vm, WB_TOP())); break;
            case OP_BNOT: WB_TOP() = wv_num((double)(~wb_toi32(wb_tonum(vm, WB_TOP())))); break;
            case OP_TYPEOF: WB_TOP() = wb_typeof_atom(vm, WB_TOP()); break;
            case OP_VOID: WB_TOP() = WV_UNDEF; break;
            case OP_TOSTR: { wv s; SAVE(); s = wb_tostr(vm, WB_TOP()); if (vm->jexc_pending) goto handle_exc; WB_TOP() = s; break; }
            case OP_INSTANCEOF: {
                wv bv = WB_STK[vm->jsp - 1], av = WB_STK[vm->jsp - 2]; int r = 0;
                SAVE();
                if (WV_TAGOF(bv) == WV_T_NAT) r = wb_nat_instanceof(vm, av, WV_PAY(bv));
                else if (WV_TAGOF(bv) == WV_T_OBJ && HHP(vm, WV_PAY(bv))->type == H_FUNC) {
                    wv po = wb_func_prototype(vm, bv);
                    if (vm->jexc_pending) goto handle_exc;
                    if (wb_is_obj_type(vm, av, H_OBJ)) { uint32_t cur = ((wb_hobj_t *)(vm->jheap + WV_PAY(av)))->proto; int d = 0; while (cur && d++ < 32) { if (cur == WV_PAY(po)) { r = 1; break; } cur = ((wb_hobj_t *)(vm->jheap + cur))->proto; } }
                } else { wb_type_error(vm, "right side of instanceof is not callable"); goto handle_exc; }
                vm->jsp--; WB_TOP() = wv_bool(r); break;
            }
            case OP_IN: { wv ov = WB_POP(), kv = WB_TOP(); int r; SAVE(); r = wb_nat_in_obj(vm, ov, kv); if (vm->jexc_pending) goto handle_exc; WB_TOP() = wv_bool(r); break; }
            case OP_NEWOBJ: { wv o; SAVE(); o = wb_obj_new(vm, 0); if (vm->jexc_pending) goto handle_exc; WB_PUSH(o); break; }
            case OP_INITF: { uint16_t a; wv v = WB_POP(); memcpy(&a, pc, 2); pc += 2; SAVE(); wb_obj_set(vm, WV_PAY(WB_TOP()), a, v); if (vm->jexc_pending) goto handle_exc; break; }
            case OP_INITI: { wv v = WB_STK[vm->jsp - 1], k = WB_STK[vm->jsp - 2]; uint16_t a; SAVE(); a = wb_key_atom(vm, k); if (vm->jexc_pending) goto handle_exc; wb_obj_set(vm, WV_PAY(WB_STK[vm->jsp - 3]), a, v); if (vm->jexc_pending) goto handle_exc; vm->jsp -= 2; break; }
            case OP_OBJSPREAD: {
                wv src = WB_POP(); uint32_t i; SAVE();
                if (wb_is_obj_type(vm, src, H_OBJ)) {
                    for (i = 0; i < ((wb_hobj_t *)(vm->jheap + WV_PAY(src)))->n; i++) {
                        uint8_t *e = vm->jheap + ((wb_hobj_t *)(vm->jheap + WV_PAY(src)))->tab + 8 + (size_t)i * 16;
                        wb_obj_set(vm, WV_PAY(WB_TOP()), (uint16_t)*(uint32_t *)e, *(wv *)(e + 8)); if (vm->jexc_pending) goto handle_exc;
                    }
                }
                break;
            }
            case OP_NEWARR: { wv a; SAVE(); a = wb_arr_new(vm, 0); if (vm->jexc_pending) goto handle_exc; WB_PUSH(a); break; }
            case OP_PUSHA: { wv v = WB_POP(); SAVE(); if (!wb_arr_push(vm, WB_TOP(), v)) goto handle_exc; break; }
            case OP_SPREADA: {
                wv src = WB_POP(), it; uint32_t i; SAVE();
                wb_root(vm, src); it = wb_iter_of(vm, src); wb_unroot(vm, 1);
                if (vm->jexc_pending) goto handle_exc;
                for (i = 0; i < wb_arr(vm, it)->len; i++) { if (!wb_arr_push(vm, WB_TOP(), wb_arr_el(vm, it)[i])) goto handle_exc; }
                break;
            }
            case OP_CLOSURE: {
                uint16_t idx; uint32_t off; wb_hfunc_t *f; memcpy(&idx, pc, 2); pc += 2; SAVE();
                off = wb_galloc(vm, H_FUNC, sizeof(wb_hfunc_t) - sizeof(wb_hh_t)); if (!off) goto handle_exc;
                f = (wb_hfunc_t *)(vm->jheap + off); f->proto = idx; f->env = fr->env; f->thisv = fr->thisv; f->h.aux = vm->protos[idx].flags;
                WB_PUSH(wv_obj(off)); break;
            }
            case OP_TRY: {
                int16_t off; memcpy(&off, pc, 2); pc += 2;
                if (vm->ntry >= ECCLES_WEB_JS_MAX_TRY) { SAVE(); wb_throw_err(vm, "RangeError", "try blocks nested too deep"); goto handle_exc; }
                vm->jtry[vm->ntry].pc = pc + off; vm->jtry[vm->ntry].sp = vm->jsp; vm->jtry[vm->ntry].env = fr->env; vm->jtry[vm->ntry].frame = (uint8_t)(vm->jnf - 1); vm->ntry++;
                break;
            }
            case OP_ENDTRY: if (vm->ntry) vm->ntry--; break;
            case OP_THROW: SAVE(); wb_throw(vm, WB_POP()); goto handle_exc;
            case OP_KEYS: { wv k; SAVE(); k = wb_keys_of(vm, WB_TOP()); if (vm->jexc_pending) goto handle_exc; WB_TOP() = k; WB_PUSH(wv_num(0)); break; }
            case OP_ITER: { wv k; SAVE(); k = wb_iter_of(vm, WB_TOP()); if (vm->jexc_pending) goto handle_exc; WB_TOP() = k; WB_PUSH(wv_num(0)); break; }
            case OP_FORNEXT: {
                int16_t off; double idx = wv_dbl(WB_TOP()); wv arr = WB_STK[vm->jsp - 2]; memcpy(&off, pc, 2); pc += 2;
                if (idx >= (double)wb_arr(vm, arr)->len) pc += off;
                else { WB_TOP() = wv_num(idx + 1); WB_PUSH(wb_arr_el(vm, arr)[(uint32_t)idx]); }
                break;
            }
            case OP_SETPROTO: { wv sp = WB_POP(), po = WB_TOP(); if (wb_is_obj_type(vm, po, H_OBJ) && wb_is_obj_type(vm, sp, H_OBJ)) ((wb_hobj_t *)(vm->jheap + WV_PAY(po)))->proto = WV_PAY(sp); break; }
            case OP_DELF: { uint16_t a; memcpy(&a, pc, 2); pc += 2; wb_delprop(vm, WB_TOP(), a); WB_TOP() = WV_TRUE; break; }
            case OP_DELI: { wv k = WB_POP(), o = WB_TOP(); uint16_t a; SAVE(); a = wb_key_atom(vm, k); if (vm->jexc_pending) goto handle_exc; wb_delprop(vm, o, a); WB_TOP() = WV_TRUE; break; }
            case OP_AWAIT: {
                wv v = WB_POP(); SAVE();
                if (!fr->is_async) { wb_throw_err(vm, "SyntaxError", "await outside an async function"); goto handle_exc; }
                if (!wb_await(vm, v, pc)) goto handle_exc;
                if (vm->jnf <= stop_nf) return 0;
                RELOAD();
                break;
            }
            default: SAVE(); wb_throw_err(vm, "Error", "bad opcode"); goto handle_exc;
        }
        continue;
    handle_exc:
        if (vm->jtrap) goto trapped;
        rc = wb_unwind(vm, stop_nf);
        if (rc == 0) return 1;
        if (vm->jnf <= stop_nf) return 0;
        RELOAD();
        continue;
    trapped:
        while (vm->jnf > stop_nf) { vm->jsp = vm->jframes[vm->jnf - 1].base; vm->jnf--; }
        while (vm->ntry && vm->jtry[vm->ntry - 1].frame >= stop_nf) vm->ntry--;
        vm->jexc_pending = 0;
        return 2;
    }
#undef SAVE
#undef RELOAD
}

//builtins: the natives behind Array / String / Number / Math / JSON / Object / console / Function / Error / Date
#if defined(__GNUC__) || defined(__clang__)
  #define WB_UNUSED __attribute__((unused))
#else
  #define WB_UNUSED
#endif
#define NAT(name) static wv name(eccles_web_vm_t *vm WB_UNUSED, wv self WB_UNUSED, const wv *a WB_UNUSED, int n WB_UNUSED)
#define ARG(i) ((i) < n ? a[(i)] : WV_UNDEF)
#define THROWRET(type, msg) do { wb_throw_err(vm, type, msg); return WV_UNDEF; } while (0)

static int wb_num_arg(eccles_web_vm_t *vm, const wv *a, int n, int i, double dflt, double *out) { if (i >= n || a[i] == WV_UNDEF) { *out = dflt; return 0; } *out = wb_tonum(vm, a[i]); return 1; }
static int64_t wb_rel_index(double d, uint32_t len) {  //negative counts from the end, clamped to 0..len
    int64_t r;
    if (d != d) return 0;
    if (d == HUGE_VAL) return len;
    if (d == -HUGE_VAL) return 0;
    r = (int64_t)trunc(d);
    if (r < 0) { r += len; if (r < 0) r = 0; }
    if (r > (int64_t)len) r = len;
    return r;
}

//array methods
static wb_harr_t *wb_self_arr(eccles_web_vm_t *vm, wv self) { if (!wb_is_arr(vm, self)) { wb_type_error(vm, "not an array"); return NULL; } return wb_arr(vm, self); }
NAT(nat_arr_push) { int i; if (!wb_self_arr(vm, self)) return WV_UNDEF; for (i = 0; i < n; i++) if (!wb_arr_push(vm, self, a[i])) return WV_UNDEF; return wv_num((double)wb_arr(vm, self)->len); }
NAT(nat_arr_pop) { wb_harr_t *r = wb_self_arr(vm, self); if (!r) return WV_UNDEF; if (!r->len) return WV_UNDEF; return wb_arr_el(vm, self)[--r->len]; }
NAT(nat_arr_shift) { wb_harr_t *r = wb_self_arr(vm, self); wv v; if (!r) return WV_UNDEF; if (!r->len) return WV_UNDEF; v = wb_arr_el(vm, self)[0]; memmove(wb_arr_el(vm, self), wb_arr_el(vm, self) + 1, (size_t)(r->len - 1) * 8); r->len--; return v; }
NAT(nat_arr_unshift) {
    wb_harr_t *r = wb_self_arr(vm, self); int i; uint32_t old;
    if (!r) return WV_UNDEF;
    old = r->len;
    for (i = 0; i < n; i++) if (!wb_arr_push(vm, self, WV_UNDEF)) return WV_UNDEF;
    r = wb_arr(vm, self);
    if (old) memmove(wb_arr_el(vm, self) + n, wb_arr_el(vm, self), (size_t)old * 8);
    for (i = 0; i < n; i++) wb_arr_el(vm, self)[i] = a[i];
    return wv_num((double)r->len);
}
NAT(nat_arr_slice) {
    wb_harr_t *r = wb_self_arr(vm, self); double d; int64_t s, e, i; wv out;
    if (!r) return WV_UNDEF;
    wb_num_arg(vm, a, n, 0, 0, &d); s = wb_rel_index(d, r->len);
    wb_num_arg(vm, a, n, 1, (double)r->len, &d); e = wb_rel_index(d, r->len);
    out = wb_arr_new(vm, e > s ? (uint32_t)(e - s) : 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, out);
    for (i = s; i < e; i++) if (!wb_arr_push(vm, out, wb_arr_el(vm, self)[i])) break;
    wb_unroot(vm, 1);
    return out;
}
NAT(nat_arr_splice) {
    wb_harr_t *r = wb_self_arr(vm, self); double d; int64_t s, dc; wv removed; int i, ni = n > 2 ? n - 2 : 0; uint32_t len, tail, k;
    if (!r) return WV_UNDEF;
    len = r->len;
    wb_num_arg(vm, a, n, 0, 0, &d); s = wb_rel_index(d, len);
    if (n < 2) dc = (int64_t)len - s; else { double x = wb_tonum(vm, a[1]); dc = x != x || x < 0 ? 0 : (int64_t)x; if (dc > (int64_t)len - s) dc = (int64_t)len - s; }
    removed = wb_arr_new(vm, (uint32_t)dc); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, removed);
    for (k = 0; k < (uint32_t)dc; k++) wb_arr_push(vm, removed, wb_arr_el(vm, self)[s + k]);
    tail = len - (uint32_t)(s + dc);
    if (ni > (int)dc) { int grow = ni - (int)dc; for (i = 0; i < grow; i++) if (!wb_arr_push(vm, self, WV_UNDEF)) { wb_unroot(vm, 1); return WV_UNDEF; } }
    r = wb_arr(vm, self);
    memmove(wb_arr_el(vm, self) + s + ni, wb_arr_el(vm, self) + s + dc, (size_t)tail * 8);
    for (i = 0; i < ni; i++) wb_arr_el(vm, self)[s + i] = a[2 + i];
    r->len = len - (uint32_t)dc + (uint32_t)ni;
    wb_unroot(vm, 1);
    return removed;
}
NAT(nat_arr_indexOf) { wb_harr_t *r = wb_self_arr(vm, self); uint32_t i; if (!r) return WV_UNDEF; for (i = 0; i < r->len; i++) if (wb_strict_eq(vm, wb_arr_el(vm, self)[i], ARG(0))) return wv_num((double)i); return wv_num(-1); }
NAT(nat_arr_lastIndexOf) { wb_harr_t *r = wb_self_arr(vm, self); int64_t i; if (!r) return WV_UNDEF; for (i = (int64_t)r->len - 1; i >= 0; i--) if (wb_strict_eq(vm, wb_arr_el(vm, self)[i], ARG(0))) return wv_num((double)i); return wv_num(-1); }
NAT(nat_arr_includes) { wb_harr_t *r = wb_self_arr(vm, self); uint32_t i; if (!r) return WV_UNDEF; for (i = 0; i < r->len; i++) { wv e = wb_arr_el(vm, self)[i]; if (wb_strict_eq(vm, e, ARG(0)) || (WV_ISNUM(e) && WV_ISNUM(ARG(0)) && wv_dbl(e) != wv_dbl(e) && wv_dbl(ARG(0)) != wv_dbl(ARG(0)))) return WV_TRUE; } return WV_FALSE; }
NAT(nat_arr_join) {
    uint32_t sl = 1; const char *sep = ","; wv sv = ARG(0);
    if (!wb_is_arr(vm, self)) THROWRET("TypeError", "not an array");
    if (sv != WV_UNDEF) { sv = wb_tostr(vm, sv); if (vm->jexc_pending) return WV_UNDEF; sep = wb_str_ptr(vm, sv, &sl); }
    { char tmp[16]; if (sl >= sizeof tmp) sl = sizeof tmp - 1; memcpy(tmp, sep, sl); return wb_arr_join(vm, self, tmp, sl); }
}
NAT(nat_arr_concat) {
    wv out; int i; uint32_t k;
    if (!wb_is_arr(vm, self)) THROWRET("TypeError", "not an array");
    out = wb_arr_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, out);
    for (k = 0; k < wb_arr(vm, self)->len; k++) wb_arr_push(vm, out, wb_arr_el(vm, self)[k]);
    for (i = 0; i < n; i++) {
        if (wb_is_arr(vm, a[i])) { for (k = 0; k < wb_arr(vm, a[i])->len; k++) wb_arr_push(vm, out, wb_arr_el(vm, a[i])[k]); }
        else wb_arr_push(vm, out, a[i]);
    }
    wb_unroot(vm, 1);
    return out;
}
NAT(nat_arr_reverse) { wb_harr_t *r = wb_self_arr(vm, self); uint32_t i; wv *e; if (!r) return WV_UNDEF; e = wb_arr_el(vm, self); for (i = 0; i < r->len / 2; i++) { wv t = e[i]; e[i] = e[r->len - 1 - i]; e[r->len - 1 - i] = t; } return self; }
NAT(nat_arr_at) { wb_harr_t *r = wb_self_arr(vm, self); double d = wb_tonum(vm, ARG(0)); int64_t i; if (!r) return WV_UNDEF; i = (int64_t)d; if (i < 0) i += r->len; return i >= 0 && (uint64_t)i < r->len ? wb_arr_el(vm, self)[i] : WV_UNDEF; }
NAT(nat_arr_fill) { wb_harr_t *r = wb_self_arr(vm, self); double d; int64_t s, e, i; if (!r) return WV_UNDEF; wb_num_arg(vm, a, n, 1, 0, &d); s = wb_rel_index(d, r->len); wb_num_arg(vm, a, n, 2, (double)r->len, &d); e = wb_rel_index(d, r->len); for (i = s; i < e; i++) wb_arr_el(vm, self)[i] = ARG(0); return self; }

//callbacks: forEach / map / filter / find ...  mode: 0 forEach, 1 map, 2 filter, 3 find, 4 findIndex, 5 some, 6 every
static wv wb_arr_iterate(eccles_web_vm_t *vm, wv self, const wv *a, int n, int mode) {
    wv fn = ARG(0), thisArg = ARG(1), out = WV_UNDEF; uint32_t i;
    if (!wb_is_arr(vm, self)) THROWRET("TypeError", "not an array");
    if (!wb_is_callable(vm, fn)) THROWRET("TypeError", "callback is not a function");
    if (mode == 1 || mode == 2) { out = wb_arr_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF; wb_root(vm, out); }
    wb_root(vm, self);
    for (i = 0; i < wb_arr(vm, self)->len; i++) {
        wv args[3], r; wv el = wb_arr_el(vm, self)[i];
        args[0] = el; args[1] = wv_num((double)i); args[2] = self;
        r = wb_call(vm, fn, thisArg, args, 3);
        if (vm->jexc_pending || vm->jtrap) { wb_unroot(vm, mode == 1 || mode == 2 ? 2 : 1); return WV_UNDEF; }
        if (mode == 1) { wb_arr_push(vm, out, r); }
        else if (mode == 2) { if (wb_truthy(vm, r)) wb_arr_push(vm, out, el); }
        else if (mode == 3 && wb_truthy(vm, r)) { wb_unroot(vm, 1); return el; }
        else if (mode == 4 && wb_truthy(vm, r)) { wb_unroot(vm, 1); return wv_num((double)i); }
        else if (mode == 5 && wb_truthy(vm, r)) { wb_unroot(vm, 1); return WV_TRUE; }
        else if (mode == 6 && !wb_truthy(vm, r)) { wb_unroot(vm, 1); return WV_FALSE; }
    }
    wb_unroot(vm, mode == 1 || mode == 2 ? 2 : 1);
    return mode == 1 || mode == 2 ? out : mode == 4 ? wv_num(-1) : mode == 5 ? WV_FALSE : mode == 6 ? WV_TRUE : WV_UNDEF;
}
NAT(nat_arr_forEach) { return wb_arr_iterate(vm, self, a, n, 0); }
NAT(nat_arr_map) { return wb_arr_iterate(vm, self, a, n, 1); }
NAT(nat_arr_filter) { return wb_arr_iterate(vm, self, a, n, 2); }
NAT(nat_arr_find) { return wb_arr_iterate(vm, self, a, n, 3); }
NAT(nat_arr_findIndex) { return wb_arr_iterate(vm, self, a, n, 4); }
NAT(nat_arr_some) { return wb_arr_iterate(vm, self, a, n, 5); }
NAT(nat_arr_every) { return wb_arr_iterate(vm, self, a, n, 6); }
NAT(nat_arr_reduce) {
    wv fn = ARG(0), acc; uint32_t i = 0;
    if (!wb_is_arr(vm, self)) THROWRET("TypeError", "not an array");
    if (!wb_is_callable(vm, fn)) THROWRET("TypeError", "callback is not a function");
    if (n >= 2) acc = a[1]; else { if (!wb_arr(vm, self)->len) THROWRET("TypeError", "reduce of empty array with no initial value"); acc = wb_arr_el(vm, self)[0]; i = 1; }
    wb_root(vm, self); wb_root(vm, acc);
    for (; i < wb_arr(vm, self)->len; i++) {
        wv args[4];
        args[0] = vm->jroots[vm->njroots - 1]; args[1] = wb_arr_el(vm, self)[i]; args[2] = wv_num((double)i); args[3] = self;
        acc = wb_call(vm, fn, WV_UNDEF, args, 4);
        if (vm->jexc_pending || vm->jtrap) { wb_unroot(vm, 2); return WV_UNDEF; }
        vm->jroots[vm->njroots - 1] = acc;
    }
    wb_unroot(vm, 2);
    return acc;
}
NAT(nat_arr_sort) {  //stable insertion sort: fine for the array sizes a phone page has, no extra memory
    wb_harr_t *r = wb_self_arr(vm, self); wv cmp = ARG(0); uint32_t i, j;
    if (!r) return WV_UNDEF;
    if (cmp != WV_UNDEF && !wb_is_callable(vm, cmp)) THROWRET("TypeError", "comparator is not a function");
    wb_root(vm, self);
    for (i = 1; i < wb_arr(vm, self)->len; i++) {
        wv x = wb_arr_el(vm, self)[i]; wb_root(vm, x);
        for (j = i; j > 0; j--) {
            wv y = wb_arr_el(vm, self)[j - 1]; int gt;
            if (cmp != WV_UNDEF) { wv args[2], r2; args[0] = y; args[1] = vm->jroots[vm->njroots - 1]; r2 = wb_call(vm, cmp, WV_UNDEF, args, 2); if (vm->jexc_pending || vm->jtrap) { wb_unroot(vm, 2); return WV_UNDEF; } gt = wb_tonum(vm, r2) > 0; }
            else { wv sy = wb_tostr(vm, y), sx; int u; if (vm->jexc_pending) { wb_unroot(vm, 2); return WV_UNDEF; } wb_root(vm, sy); sx = wb_tostr(vm, vm->jroots[vm->njroots - 2]); wb_unroot(vm, 1); if (vm->jexc_pending) { wb_unroot(vm, 2); return WV_UNDEF; } gt = wb_cmp_lt(vm, sx, sy, &u); }
            if (!gt) break;
            wb_arr_el(vm, self)[j] = y;
        }
        wb_arr_el(vm, self)[j] = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1);
    }
    wb_unroot(vm, 1);
    return self;
}
NAT(nat_array_ctor) {
    wv arr = wb_arr_new(vm, 0); int i;
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, arr);
    if (n == 1 && WV_ISNUM(a[0])) { double d = wv_dbl(a[0]); if (d < 0 || d != floor(d) || d > 100000) { wb_unroot(vm, 1); THROWRET("RangeError", "invalid array length"); } wb_arr_set_len(vm, arr, (uint32_t)d); }
    else for (i = 0; i < n; i++) wb_arr_push(vm, arr, a[i]);
    wb_unroot(vm, 1);
    return arr;
}
NAT(nat_array_isArray) { return wv_bool(wb_is_arr(vm, ARG(0))); }
NAT(nat_array_of) { wv arr = wb_arr_new(vm, (uint32_t)n); int i; if (vm->jexc_pending) return WV_UNDEF; wb_root(vm, arr); for (i = 0; i < n; i++) wb_arr_push(vm, arr, a[i]); wb_unroot(vm, 1); return arr; }
NAT(nat_array_from) {
    wv src = ARG(0), fn = ARG(1), out; uint32_t i, len = 0; int isarr = 0;
    if (wb_is_arr(vm, src) || wb_is_str(vm, src)) { src = wb_iter_of(vm, src); if (vm->jexc_pending) return WV_UNDEF; len = wb_arr(vm, src)->len; isarr = 1; }
    else if (wb_is_obj_type(vm, src, H_OBJ)) { int f; wv l = wb_obj_get(vm, WV_PAY(src), A_length, &f); len = f ? (uint32_t)wb_tonum(vm, l) : 0; if (len > 100000) len = 100000; }
    out = wb_arr_new(vm, len); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, out); wb_root(vm, src);
    for (i = 0; i < len; i++) {
        wv el = isarr ? wb_arr_el(vm, src)[i] : WV_UNDEF;
        if (wb_is_callable(vm, fn)) { wv args[2]; args[0] = el; args[1] = wv_num((double)i); el = wb_call(vm, fn, WV_UNDEF, args, 2); if (vm->jexc_pending || vm->jtrap) { wb_unroot(vm, 2); return WV_UNDEF; } }
        wb_arr_push(vm, vm->jroots[vm->njroots - 2], el);
    }
    wb_unroot(vm, 2);
    return out;
}

//strings. indices are code points (utf-8 text), which is the same as js for ascii
static uint32_t wb_cp_byte(const char *p, uint32_t n, uint32_t cp) {  //byte offset of code point number cp (n if past the end)
    uint32_t i = 0, c = 0;
    while (i < n && c < cp) { uint8_t b = (uint8_t)p[i]; i += b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1; c++; }
    return i > n ? n : i;
}
static uint32_t wb_byte_cp(const char *p, uint32_t byte) { return wb_utf8_len(p, byte); }
static wv wb_self_str(eccles_web_vm_t *vm, wv self) { return wb_tostr(vm, self); }
static wv wb_substr_bytes(eccles_web_vm_t *vm, wv s, uint32_t b0, uint32_t b1) {
    uint32_t n; const char *p = wb_str_ptr(vm, s, &n);
    if (b1 <= b0) return wv_atom(0);
    if (b0 == 0 && b1 == n) return s;
    return wb_str_new(vm, p + b0, b1 - b0);
}
NAT(nat_str_charAt) { wv s = wb_self_str(vm, self); double d; wb_num_arg(vm, a, n, 0, 0, &d); if (vm->jexc_pending) return WV_UNDEF; if (d < 0) return wv_atom(0); { wv r = wb_utf8_char_at(vm, s, (uint32_t)d); return r == WV_UNDEF ? wv_atom(0) : r; } }
NAT(nat_str_charCodeAt) {
    wv s = wb_self_str(vm, self); double d; uint32_t len, b; const char *p; uint32_t cp;
    wb_num_arg(vm, a, n, 0, 0, &d); p = wb_str_ptr(vm, s, &len);
    if (d < 0) return wv_num(NAN);
    b = wb_cp_byte(p, len, (uint32_t)d); if (b >= len) return wv_num(NAN);
    { uint8_t c = (uint8_t)p[b]; if (c < 0x80) cp = c; else if (c >= 0xF0) cp = ((uint32_t)(c & 7) << 18) | ((uint32_t)(p[b + 1] & 63) << 12) | ((uint32_t)(p[b + 2] & 63) << 6) | (uint32_t)(p[b + 3] & 63);
      else if (c >= 0xE0) cp = ((uint32_t)(c & 15) << 12) | ((uint32_t)(p[b + 1] & 63) << 6) | (uint32_t)(p[b + 2] & 63); else cp = ((uint32_t)(c & 31) << 6) | (uint32_t)(p[b + 1] & 63); }
    return wv_num((double)cp);
}
static int wb_find(const char *h, uint32_t hl, const char *nd, uint32_t nl, uint32_t from, int last) {
    int64_t i;
    if (nl > hl) return -1;
    if (!last) { for (i = from; i + nl <= hl; i++) if (!memcmp(h + i, nd, nl)) return (int)i; }
    else { for (i = (int64_t)(hl - nl < from ? hl - nl : from); i >= 0; i--) if (!memcmp(h + i, nd, nl)) return (int)i; }
    return -1;
}
static wv wb_str_search(eccles_web_vm_t *vm, wv self, const wv *a, int n, int last) {
    wv s = wb_self_str(vm, self), t; uint32_t hl, nl; const char *h, *nd; double d; int r; uint32_t from;
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, s); t = wb_tostr(vm, ARG(0)); wb_unroot(vm, 1); if (vm->jexc_pending) return WV_UNDEF;
    h = wb_str_ptr(vm, s, &hl); nd = wb_str_ptr(vm, t, &nl);
    wb_num_arg(vm, a, n, 1, last ? (double)hl : 0, &d);
    from = d < 0 ? 0 : (uint32_t)wb_cp_byte(h, hl, (uint32_t)d);
    r = wb_find(h, hl, nd, nl, from, last);
    return wv_num(r < 0 ? -1 : (double)wb_byte_cp(h, (uint32_t)r));
}
NAT(nat_str_indexOf) { return wb_str_search(vm, self, a, n, 0); }
NAT(nat_str_lastIndexOf) { return wb_str_search(vm, self, a, n, 1); }
NAT(nat_str_includes) { wv r = wb_str_search(vm, self, a, n, 0); return wv_bool(WV_ISNUM(r) && wv_dbl(r) >= 0); }
NAT(nat_str_startsWith) { wv s = wb_self_str(vm, self), t = wb_tostr(vm, ARG(0)); uint32_t hl, nl; const char *h, *nd; if (vm->jexc_pending) return WV_UNDEF; h = wb_str_ptr(vm, s, &hl); nd = wb_str_ptr(vm, t, &nl); return wv_bool(nl <= hl && !memcmp(h, nd, nl)); }
NAT(nat_str_endsWith) { wv s = wb_self_str(vm, self), t = wb_tostr(vm, ARG(0)); uint32_t hl, nl; const char *h, *nd; if (vm->jexc_pending) return WV_UNDEF; h = wb_str_ptr(vm, s, &hl); nd = wb_str_ptr(vm, t, &nl); return wv_bool(nl <= hl && !memcmp(h + hl - nl, nd, nl)); }
NAT(nat_str_slice) {
    wv s = wb_self_str(vm, self); uint32_t len, cpn; const char *p; double d; int64_t b, e;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len); cpn = wb_utf8_len(p, len);
    wb_num_arg(vm, a, n, 0, 0, &d); b = wb_rel_index(d, cpn);
    wb_num_arg(vm, a, n, 1, (double)cpn, &d); e = wb_rel_index(d, cpn);
    return wb_substr_bytes(vm, s, wb_cp_byte(p, len, (uint32_t)b), wb_cp_byte(p, len, (uint32_t)(e > b ? e : b)));
}
NAT(nat_str_substring) {
    wv s = wb_self_str(vm, self); uint32_t len, cpn; const char *p; double d; int64_t b, e, t;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len); cpn = wb_utf8_len(p, len);
    wb_num_arg(vm, a, n, 0, 0, &d); b = d != d || d < 0 ? 0 : d > cpn ? cpn : (int64_t)d;
    wb_num_arg(vm, a, n, 1, (double)cpn, &d); e = d != d || d < 0 ? 0 : d > cpn ? cpn : (int64_t)d;
    if (b > e) { t = b; b = e; e = t; }
    return wb_substr_bytes(vm, s, wb_cp_byte(p, len, (uint32_t)b), wb_cp_byte(p, len, (uint32_t)e));
}
NAT(nat_str_substr) {
    wv s = wb_self_str(vm, self); uint32_t len, cpn; const char *p; double d; int64_t b, l;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len); cpn = wb_utf8_len(p, len);
    wb_num_arg(vm, a, n, 0, 0, &d); b = wb_rel_index(d, cpn);
    wb_num_arg(vm, a, n, 1, (double)cpn, &d); l = d != d || d < 0 ? 0 : (int64_t)d; if (b + l > cpn) l = cpn - b;
    return wb_substr_bytes(vm, s, wb_cp_byte(p, len, (uint32_t)b), wb_cp_byte(p, len, (uint32_t)(b + l)));
}
static wv wb_str_case(eccles_web_vm_t *vm, wv self, int up) {
    wv s = wb_self_str(vm, self), r; uint32_t len, i; const char *p; char *d;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len); if (!len) return s;
    r = wb_str_new(vm, p, len); if (vm->jexc_pending) return WV_UNDEF;
    d = ((wb_hstr_t *)(vm->jheap + WV_PAY(r)))->s;
    for (i = 0; i < len; i++) { if (up && d[i] >= 'a' && d[i] <= 'z') d[i] = (char)(d[i] - 32); else if (!up && d[i] >= 'A' && d[i] <= 'Z') d[i] = (char)(d[i] + 32); }
    return r;
}
NAT(nat_str_toUpperCase) { return wb_str_case(vm, self, 1); }
NAT(nat_str_toLowerCase) { return wb_str_case(vm, self, 0); }
static wv wb_str_trim(eccles_web_vm_t *vm, wv self, int l, int r) {
    wv s = wb_self_str(vm, self); uint32_t len, b = 0, e; const char *p;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len); e = len;
    if (l) while (b < e && wb_is_space((uint8_t)p[b])) b++;
    if (r) while (e > b && wb_is_space((uint8_t)p[e - 1])) e--;
    return wb_substr_bytes(vm, s, b, e);
}
NAT(nat_str_trim) { return wb_str_trim(vm, self, 1, 1); }
NAT(nat_str_trimStart) { return wb_str_trim(vm, self, 1, 0); }
NAT(nat_str_trimEnd) { return wb_str_trim(vm, self, 0, 1); }
NAT(nat_str_split) {
    wv s = wb_self_str(vm, self), sep = ARG(0), out; uint32_t len, sl, i = 0, st = 0; const char *p, *sp; double lim;
    if (vm->jexc_pending) return WV_UNDEF;
    wb_num_arg(vm, a, n, 1, 100000, &lim);
    out = wb_arr_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, out); wb_root(vm, s);
    if (sep == WV_UNDEF) { wb_arr_push(vm, out, s); wb_unroot(vm, 2); return out; }
    if (!wb_is_str(vm, sep)) { wb_unroot(vm, 2); THROWRET("TypeError", "split needs a string separator (regular expressions are not supported)"); }
    p = wb_str_ptr(vm, s, &len); sp = wb_str_ptr(vm, sep, &sl);
    if (sl == 0) {  //every character
        wv chars = wb_iter_of(vm, s); uint32_t k;
        if (vm->jexc_pending) { wb_unroot(vm, 2); return WV_UNDEF; }
        for (k = 0; k < wb_arr(vm, chars)->len && k < lim; k++) wb_arr_push(vm, out, wb_arr_el(vm, chars)[k]);
        wb_unroot(vm, 2); return out;
    }
    while (i + sl <= len && wb_arr(vm, out)->len < lim) {
        p = wb_str_ptr(vm, vm->jroots[vm->njroots - 1], &len);
        if (!memcmp(p + i, sp, sl)) { wv piece = wb_substr_bytes(vm, vm->jroots[vm->njroots - 1], st, i); if (vm->jexc_pending) break; wb_arr_push(vm, out, piece); i += sl; st = i; } else i++;
        sp = wb_str_ptr(vm, sep, &sl);
    }
    if (!vm->jexc_pending && wb_arr(vm, out)->len < lim) { wv piece = wb_substr_bytes(vm, vm->jroots[vm->njroots - 1], st, len); if (!vm->jexc_pending) wb_arr_push(vm, out, piece); }
    wb_unroot(vm, 2);
    return out;
}
static wv wb_str_replace(eccles_web_vm_t *vm, wv self, const wv *a, int n, int all) {
    wv s = wb_self_str(vm, self), pat = ARG(0), rep = ARG(1), out; uint32_t len, pl, i = 0, st = 0; const char *p, *pp; int replaced = 0;
    if (vm->jexc_pending) return WV_UNDEF;
    if (!wb_is_str(vm, pat)) THROWRET("TypeError", "replace needs a string pattern (regular expressions are not supported)");
    out = wv_atom(0);
    wb_root(vm, s); wb_root(vm, pat); wb_root(vm, rep); wb_root(vm, out);
#define R_S vm->jroots[vm->njroots - 4]
#define R_PAT vm->jroots[vm->njroots - 3]
#define R_REP vm->jroots[vm->njroots - 2]
#define R_OUT vm->jroots[vm->njroots - 1]
    p = wb_str_ptr(vm, R_S, &len); pp = wb_str_ptr(vm, R_PAT, &pl);
    while (i + pl <= len && (all || !replaced)) {
        p = wb_str_ptr(vm, R_S, &len); pp = wb_str_ptr(vm, R_PAT, &pl);
        if ((pl || i < len) && !memcmp(p + i, pp, pl)) {
            wv piece = wb_substr_bytes(vm, R_S, st, i), rv;
            if (vm->jexc_pending) break;
            R_OUT = wb_str_concat(vm, R_OUT, piece); if (vm->jexc_pending) break;
            if (wb_is_callable(vm, R_REP)) { wv args[3]; args[0] = R_PAT; args[1] = wv_num((double)wb_byte_cp(p, i)); args[2] = R_S; rv = wb_call(vm, R_REP, WV_UNDEF, args, 3); if (vm->jexc_pending || vm->jtrap) break; rv = wb_tostr(vm, rv); }
            else rv = wb_tostr(vm, R_REP);
            if (vm->jexc_pending) break;
            R_OUT = wb_str_concat(vm, R_OUT, rv); if (vm->jexc_pending) break;
            replaced = 1; i += pl ? pl : 1; st = i;
            if (!pl) break;
        } else i++;
    }
    if (!vm->jexc_pending) { p = wb_str_ptr(vm, R_S, &len); { wv tail = wb_substr_bytes(vm, R_S, st > len ? len : st, len); if (!vm->jexc_pending) R_OUT = wb_str_concat(vm, R_OUT, tail); } }
    out = R_OUT;
    wb_unroot(vm, 4);
#undef R_S
#undef R_PAT
#undef R_REP
#undef R_OUT
    return vm->jexc_pending ? WV_UNDEF : out;
}
NAT(nat_str_replace) { return wb_str_replace(vm, self, a, n, 0); }
NAT(nat_str_replaceAll) { return wb_str_replace(vm, self, a, n, 1); }
NAT(nat_str_repeat) {
    wv s = wb_self_str(vm, self); double d = wb_tonum(vm, ARG(0)); uint32_t len, cnt, i, off; const char *p; char *dst;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len);
    if (d != d || d < 0 || d > 100000) THROWRET("RangeError", "invalid repeat count");
    cnt = (uint32_t)d; if (!cnt || !len) return wv_atom(0);
    off = wb_galloc(vm, H_STR, 4 + len * cnt + 1); if (!off) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len); dst = ((wb_hstr_t *)(vm->jheap + off))->s; ((wb_hstr_t *)(vm->jheap + off))->len = len * cnt;
    for (i = 0; i < cnt; i++) memcpy(dst + (size_t)i * len, p, len);
    dst[len * cnt] = 0;
    return wv_obj(off);
}
static wv wb_str_pad(eccles_web_vm_t *vm, wv self, const wv *a, int n, int start) {
    wv s = wb_self_str(vm, self), pad = n > 1 && a[1] != WV_UNDEF ? wb_tostr(vm, a[1]) : wv_atom(A_space); double target = wb_tonum(vm, ARG(0));
    uint32_t len, pl, cpn, need, i, off, total; const char *p, *pp; char *dst;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len); pp = wb_str_ptr(vm, pad, &pl); cpn = wb_utf8_len(p, len);
    if (target != target || target <= cpn || pl == 0) return s;
    need = (uint32_t)target - cpn; if (need > 10000) need = 10000;
    total = len + need * pl;  //(pad is treated as ascii-wide per char, fine for the usual " " and "0")
    off = wb_galloc(vm, H_STR, 4 + total + 1); if (!off) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len); pp = wb_str_ptr(vm, pad, &pl);
    dst = ((wb_hstr_t *)(vm->jheap + off))->s; ((wb_hstr_t *)(vm->jheap + off))->len = total;
    if (start) { for (i = 0; i < need; i++) memcpy(dst + (size_t)i * pl, pp, pl); memcpy(dst + (size_t)need * pl, p, len); }
    else { memcpy(dst, p, len); for (i = 0; i < need; i++) memcpy(dst + len + (size_t)i * pl, pp, pl); }
    dst[total] = 0;
    return wv_obj(off);
}
NAT(nat_str_padStart) { return wb_str_pad(vm, self, a, n, 1); }
NAT(nat_str_padEnd) { return wb_str_pad(vm, self, a, n, 0); }
NAT(nat_str_concat) { wv r = wb_self_str(vm, self); int i; if (vm->jexc_pending) return WV_UNDEF; for (i = 0; i < n; i++) { wv t; wb_root(vm, r); t = wb_tostr(vm, a[i]); if (vm->jexc_pending) { wb_unroot(vm, 1); return WV_UNDEF; } r = wb_str_concat(vm, vm->jroots[vm->njroots - 1], t); wb_unroot(vm, 1); if (vm->jexc_pending) return WV_UNDEF; } return r; }
NAT(nat_str_at) { wv s = wb_self_str(vm, self); double d = wb_tonum(vm, ARG(0)); uint32_t len; const char *p; int64_t i; if (vm->jexc_pending) return WV_UNDEF; p = wb_str_ptr(vm, s, &len); i = (int64_t)d; if (i < 0) i += wb_utf8_len(p, len); return i >= 0 ? wb_utf8_char_at(vm, s, (uint32_t)i) : WV_UNDEF; }
NAT(nat_str_localeCompare) { wv s = wb_self_str(vm, self), t = wb_tostr(vm, ARG(0)); uint32_t l1, l2; const char *p1, *p2; int c; if (vm->jexc_pending) return WV_UNDEF; p1 = wb_str_ptr(vm, s, &l1); p2 = wb_str_ptr(vm, t, &l2); c = memcmp(p1, p2, l1 < l2 ? l1 : l2); if (!c) c = l1 < l2 ? -1 : l1 > l2 ? 1 : 0; return wv_num(c < 0 ? -1 : c > 0 ? 1 : 0); }
NAT(nat_str_toString) { return wb_self_str(vm, self); }
NAT(nat_string_ctor) { wv s = n ? wb_tostr(vm, a[0]) : wv_atom(0); return s; }
NAT(nat_string_fromCharCode) {
    char buf[64]; uint32_t len = 0; int i;
    for (i = 0; i < n && len + 4 < sizeof buf; i++) wb_utf8_put(buf, &len, (uint32_t)wb_tonum(vm, a[i]));
    return wb_str_new(vm, buf, len);
}

//numbers
NAT(nat_num_toFixed) {
    double d = WV_ISNUM(self) ? wv_dbl(self) : wb_tonum(vm, self), dg; char buf[64]; int digits;
    wb_num_arg(vm, a, n, 0, 0, &dg); digits = dg < 0 ? 0 : dg > 20 ? 20 : (int)dg;
    if (d != d || fabs(d) >= 1e21) { uint32_t l = wb_num_str(d, buf); return wb_str_new(vm, buf, l); }
    snprintf(buf, sizeof buf, "%.*f", digits, d);
    return wb_str_cstr(vm, buf);
}
NAT(nat_num_toString) {
    double d = WV_ISNUM(self) ? wv_dbl(self) : wb_tonum(vm, self), rd; char buf[72]; uint32_t l;
    wb_num_arg(vm, a, n, 0, 10, &rd);
    if (rd != 10 && rd >= 2 && rd <= 36 && d == floor(d) && fabs(d) < 9e15) {
        char tmp[70]; int k = 0, neg = d < 0; uint64_t u = (uint64_t)fabs(d); int r = (int)rd;
        if (!u) tmp[k++] = '0';
        while (u) { int dgt = (int)(u % (uint64_t)r); tmp[k++] = (char)(dgt < 10 ? '0' + dgt : 'a' + dgt - 10); u /= (uint64_t)r; }
        l = 0; if (neg) buf[l++] = '-';
        while (k) buf[l++] = tmp[--k];
        return wb_str_new(vm, buf, l);
    }
    l = wb_num_str(d, buf);
    return wb_str_new(vm, buf, l);
}
NAT(nat_number_ctor) { return wv_num(n ? wb_tonum(vm, a[0]) : 0); }
NAT(nat_boolean_ctor) { return wv_bool(n && wb_truthy(vm, a[0])); }
NAT(nat_parseFloat) {
    wv s = wb_tostr(vm, ARG(0)); uint32_t len; const char *p; char tmp[48], *end; uint32_t i = 0;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len);
    while (i < len && wb_is_space((uint8_t)p[i])) i++;
    if (len - i >= sizeof tmp) len = i + sizeof tmp - 1;
    memcpy(tmp, p + i, len - i); tmp[len - i] = 0;
    if (!strncmp(tmp, "Infinity", 8)) return wv_num(HUGE_VAL);
    if (!strncmp(tmp, "-Infinity", 9)) return wv_num(-HUGE_VAL);
    { double d = strtod(tmp, &end); if (end == tmp || (tmp[0] == '0' && (tmp[1] == 'x' || tmp[1] == 'X'))) { if (end != tmp) return wv_num(0); return wv_num(NAN); } return wv_num(d); }
}
NAT(nat_parseInt) {
    wv s = wb_tostr(vm, ARG(0)); uint32_t len, i = 0; const char *p; double rd, v = 0; int radix, neg = 0, any = 0;
    if (vm->jexc_pending) return WV_UNDEF;
    p = wb_str_ptr(vm, s, &len);
    wb_num_arg(vm, a, n, 1, 0, &rd); radix = (int)rd;
    while (i < len && wb_is_space((uint8_t)p[i])) i++;
    if (i < len && (p[i] == '-' || p[i] == '+')) { neg = p[i] == '-'; i++; }
    if ((radix == 0 || radix == 16) && i + 1 < len && p[i] == '0' && (p[i + 1] == 'x' || p[i + 1] == 'X')) { i += 2; radix = 16; }
    if (radix == 0) radix = 10;
    if (radix < 2 || radix > 36) return wv_num(NAN);
    for (; i < len; i++) {
        int d = p[i] >= '0' && p[i] <= '9' ? p[i] - '0' : p[i] >= 'a' && p[i] <= 'z' ? p[i] - 'a' + 10 : p[i] >= 'A' && p[i] <= 'Z' ? p[i] - 'A' + 10 : 99;
        if (d >= radix) break;
        v = v * radix + d; any = 1;
    }
    return any ? wv_num(neg ? -v : v) : wv_num(NAN);
}
NAT(nat_isNaN) { double d = wb_tonum(vm, ARG(0)); return wv_bool(d != d); }
NAT(nat_isFinite) { double d = wb_tonum(vm, ARG(0)); return wv_bool(d == d && d != HUGE_VAL && d != -HUGE_VAL); }
NAT(nat_number_isInteger) { return wv_bool(WV_ISNUM(ARG(0)) && wv_dbl(ARG(0)) == floor(wv_dbl(ARG(0))) && fabs(wv_dbl(ARG(0))) != HUGE_VAL); }
NAT(nat_number_isFinite) { return wv_bool(WV_ISNUM(ARG(0)) && wv_dbl(ARG(0)) == wv_dbl(ARG(0)) && fabs(wv_dbl(ARG(0))) != HUGE_VAL); }
NAT(nat_number_isNaN) { return wv_bool(WV_ISNUM(ARG(0)) && wv_dbl(ARG(0)) != wv_dbl(ARG(0))); }

//Math
#define MATH1(name, expr) NAT(name) { double x = wb_tonum(vm, ARG(0)); return wv_num(expr); }
MATH1(nat_math_floor, floor(x)) MATH1(nat_math_ceil, ceil(x)) MATH1(nat_math_abs, fabs(x)) MATH1(nat_math_sqrt, sqrt(x))
MATH1(nat_math_sin, sin(x)) MATH1(nat_math_cos, cos(x)) MATH1(nat_math_tan, tan(x)) MATH1(nat_math_atan, atan(x))
MATH1(nat_math_asin, asin(x)) MATH1(nat_math_acos, acos(x))
MATH1(nat_math_log, log(x)) MATH1(nat_math_log2, log2(x)) MATH1(nat_math_log10, log10(x)) MATH1(nat_math_exp, exp(x))
MATH1(nat_math_trunc, trunc(x)) MATH1(nat_math_cbrt, cbrt(x)) MATH1(nat_math_sign, x != x ? x : x > 0 ? 1 : x < 0 ? -1 : x)
MATH1(nat_math_round, floor(x + 0.5))
NAT(nat_math_pow) { return wv_num(pow(wb_tonum(vm, ARG(0)), wb_tonum(vm, ARG(1)))); }
NAT(nat_math_atan2) { return wv_num(atan2(wb_tonum(vm, ARG(0)), wb_tonum(vm, ARG(1)))); }
NAT(nat_math_min) { double r = HUGE_VAL; int i; for (i = 0; i < n; i++) { double d = wb_tonum(vm, a[i]); if (d != d) return wv_num(NAN); if (d < r) r = d; } return wv_num(r); }
NAT(nat_math_max) { double r = -HUGE_VAL; int i; for (i = 0; i < n; i++) { double d = wb_tonum(vm, a[i]); if (d != d) return wv_num(NAN); if (d > r) r = d; } return wv_num(r); }
NAT(nat_math_hypot) { double s = 0; int i; for (i = 0; i < n; i++) { double d = wb_tonum(vm, a[i]); s += d * d; } return wv_num(sqrt(s)); }
static uint32_t wb_rng = 2463534242u;
NAT(nat_math_random) { wb_rng ^= wb_rng << 13; wb_rng ^= wb_rng >> 17; wb_rng ^= wb_rng << 5; wb_rng += vm->jops; return wv_num((double)(wb_rng >> 8) / 16777216.0); }

//Object
NAT(nat_object_ctor) { return wb_is_obj_type(vm, ARG(0), H_OBJ) ? a[0] : wb_obj_new(vm, 0); }
static void wb_own_keys_values(eccles_web_vm_t *vm, wv o, wv out, int mode) {  //mode 0 keys, 1 values, 2 entries
    uint32_t i, cnt;
    if (wb_is_obj_type(vm, o, H_OBJ)) {
        cnt = ((wb_hobj_t *)(vm->jheap + WV_PAY(o)))->n;
        for (i = 0; i < cnt; i++) {
            uint8_t *e = vm->jheap + ((wb_hobj_t *)(vm->jheap + WV_PAY(o)))->tab + 8 + (size_t)i * 16; wv k = wv_atom((uint16_t)*(uint32_t *)e), v = *(wv *)(e + 8), item;
            if (mode == 0) item = k; else if (mode == 1) item = v;
            else { item = wb_arr_new(vm, 2); if (vm->jexc_pending) return; wb_root(vm, item); wb_arr_push(vm, item, k); wb_arr_push(vm, item, *(wv *)(vm->jheap + ((wb_hobj_t *)(vm->jheap + WV_PAY(o)))->tab + 8 + (size_t)i * 16 + 8)); wb_unroot(vm, 1); }
            if (!wb_arr_push(vm, out, item)) return;
        }
    } else if (wb_is_arr(vm, o)) {
        wv ks = wb_keys_of(vm, o); if (vm->jexc_pending) return;
        for (i = 0; i < wb_arr(vm, o)->len; i++) {
            wv item = mode == 0 ? wb_arr_el(vm, ks)[i] : wb_arr_el(vm, o)[i];
            if (mode == 2) { wb_root(vm, ks); item = wb_arr_new(vm, 2); if (vm->jexc_pending) { wb_unroot(vm, 1); return; } wb_root(vm, item); wb_arr_push(vm, item, wb_arr_el(vm, ks)[i]); wb_arr_push(vm, item, wb_arr_el(vm, o)[i]); wb_unroot(vm, 2); }
            if (!wb_arr_push(vm, out, item)) return;
        }
    }
}
static wv wb_object_list(eccles_web_vm_t *vm, wv o, int mode) {
    wv out = wb_arr_new(vm, 0);
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, out); wb_root(vm, o);
    wb_own_keys_values(vm, o, out, mode);
    wb_unroot(vm, 2);
    return out;
}
NAT(nat_object_keys) { return wb_object_list(vm, ARG(0), 0); }
NAT(nat_object_values) { return wb_object_list(vm, ARG(0), 1); }
NAT(nat_object_entries) { return wb_object_list(vm, ARG(0), 2); }
NAT(nat_object_assign) {
    int i; uint32_t k;
    if (!wb_is_obj_type(vm, ARG(0), H_OBJ)) THROWRET("TypeError", "Object.assign target must be an object");
    for (i = 1; i < n; i++) if (wb_is_obj_type(vm, a[i], H_OBJ)) {
        for (k = 0; k < ((wb_hobj_t *)(vm->jheap + WV_PAY(a[i])))->n; k++) {
            uint8_t *e = vm->jheap + ((wb_hobj_t *)(vm->jheap + WV_PAY(a[i])))->tab + 8 + (size_t)k * 16;
            wb_obj_set(vm, WV_PAY(a[0]), (uint16_t)*(uint32_t *)e, *(wv *)(e + 8)); if (vm->jexc_pending) return WV_UNDEF;
        }
    }
    return a[0];
}
NAT(nat_object_freeze) { return ARG(0); }
NAT(nat_object_create) { wv p = ARG(0); return wb_obj_new(vm, wb_is_obj_type(vm, p, H_OBJ) ? WV_PAY(p) : 0); }
NAT(nat_object_fromEntries) {
    wv src = ARG(0), o; uint32_t i;
    if (!wb_is_arr(vm, src)) THROWRET("TypeError", "Object.fromEntries needs an array");
    o = wb_obj_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, o);
    for (i = 0; i < wb_arr(vm, src)->len; i++) { wv e = wb_arr_el(vm, src)[i]; if (wb_is_arr(vm, e) && wb_arr(vm, e)->len >= 2) { uint16_t k = wb_key_atom(vm, wb_arr_el(vm, e)[0]); if (vm->jexc_pending) break; wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 1]), k, wb_arr_el(vm, e)[1]); } }
    wb_unroot(vm, 1);
    return o;
}
NAT(nat_object_defineProperty) {
    if (wb_is_obj_type(vm, ARG(0), H_OBJ) && wb_is_obj_type(vm, ARG(2), H_OBJ)) { int f; wv v = wb_obj_get(vm, WV_PAY(a[2]), A_value, &f); uint16_t k = wb_key_atom(vm, ARG(1)); if (f && !vm->jexc_pending) wb_obj_set(vm, WV_PAY(a[0]), k, v); }
    return ARG(0);
}
NAT(nat_obj_hasOwnProperty) {
    uint16_t k = wb_key_atom(vm, ARG(0));
    if (vm->jexc_pending) return WV_UNDEF;
    if (wb_is_obj_type(vm, self, H_OBJ)) return wv_bool(wb_obj_find(vm, WV_PAY(self), k) >= 0);
    return WV_FALSE;
}

//Function.prototype
NAT(nat_func_call) { return wb_call(vm, self, ARG(0), n > 1 ? a + 1 : a, n > 1 ? n - 1 : 0); }
NAT(nat_func_apply) {
    wv arr = ARG(1); wv args[24]; uint32_t i, cnt = 0;
    if (wb_is_arr(vm, arr)) { cnt = wb_arr(vm, arr)->len; if (cnt > 24) cnt = 24; for (i = 0; i < cnt; i++) args[i] = wb_arr_el(vm, arr)[i]; }
    return wb_call(vm, self, ARG(0), args, (int)cnt);
}
NAT(nat_bind_call) {  //the body of a bound function: self = target, jbd1 = [this, partial args...]
    wv arr = vm->jbd1, args[16]; uint32_t cnt = 0, i; int k;
    for (i = 1; i < wb_arr(vm, arr)->len && cnt < 12; i++) args[cnt++] = wb_arr_el(vm, arr)[i];
    for (k = 0; k < n && cnt < 16; k++) args[cnt++] = a[k];
    return wb_call(vm, self, wb_arr_el(vm, arr)[0], args, (int)cnt);
}
static uint32_t wb_nat_id(wb_nat_fn fn);
static wv wb_make_bound(eccles_web_vm_t *vm, wb_nat_fn fn, wv d0, wv d1) {
    uint32_t off; wb_hbound_t *b;
    wb_root(vm, d0); wb_root(vm, d1);
    off = wb_galloc(vm, H_BOUND, sizeof(wb_hbound_t) - sizeof(wb_hh_t));
    d1 = vm->jroots[vm->njroots - 1]; d0 = vm->jroots[vm->njroots - 2]; wb_unroot(vm, 2);
    if (!off) return WV_UNDEF;
    b = (wb_hbound_t *)(vm->jheap + off); b->nat = wb_nat_id(fn); b->d0 = d0; b->d1 = d1;
    return wv_obj(off);
}
NAT(nat_func_bind) {
    wv arr = wb_arr_new(vm, (uint32_t)n + 1); int i;
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, arr);
    wb_arr_push(vm, arr, ARG(0));
    for (i = 1; i < n; i++) wb_arr_push(vm, arr, a[i]);
    { wv r = wb_make_bound(vm, nat_bind_call, self, vm->jroots[vm->njroots - 1]); wb_unroot(vm, 1); return r; }
}

//errors: Error("msg") and new Error("msg")
static wv wb_make_error(eccles_web_vm_t *vm, uint16_t name, wv msg) {
    wv o = wb_obj_new(vm, 0);
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, o);
    wb_obj_set(vm, WV_PAY(o), A_name, wv_atom(name));
    wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 1]), A_message, msg == WV_UNDEF ? wv_atom(0) : wb_tostr(vm, msg));
    wb_unroot(vm, 1);
    return o;
}
NAT(nat_error_ctor) { return wb_make_error(vm, A_Error, ARG(0)); }
NAT(nat_typeerror_ctor) { return wb_make_error(vm, A_TypeError, ARG(0)); }
NAT(nat_rangeerror_ctor) { return wb_make_error(vm, A_RangeError, ARG(0)); }
NAT(nat_syntaxerror_ctor) { return wb_make_error(vm, A_SyntaxError, ARG(0)); }
NAT(nat_referenceerror_ctor) { return wb_make_error(vm, A_ReferenceError, ARG(0)); }

//JSON + console output share one writer that uses the free space of the arena (nothing else is using it while a native runs)
typedef struct { char *b; uint32_t cap, len; uint8_t over; uint8_t depth; } wb_jw_t;
static void wb_jw_put(wb_jw_t *w, const char *s, uint32_t n) { if (w->len + n > w->cap) { w->over = 1; return; } memcpy(w->b + w->len, s, n); w->len += n; }
static void wb_jw_c(wb_jw_t *w, char c) { wb_jw_put(w, &c, 1); }
static void wb_jw_str(wb_jw_t *w, const char *p, uint32_t n) {
    uint32_t i; wb_jw_c(w, '"');
    for (i = 0; i < n; i++) {
        uint8_t c = (uint8_t)p[i];
        if (c == '"' || c == '\\') { wb_jw_c(w, '\\'); wb_jw_c(w, (char)c); }
        else if (c == '\n') wb_jw_put(w, "\\n", 2); else if (c == '\r') wb_jw_put(w, "\\r", 2); else if (c == '\t') wb_jw_put(w, "\\t", 2);
        else if (c < 0x20) { char b[8]; int l = snprintf(b, sizeof b, "\\u%04x", c); wb_jw_put(w, b, (uint32_t)l); }
        else wb_jw_c(w, (char)c);
    }
    wb_jw_c(w, '"');
}
static int wb_json_write(eccles_web_vm_t *vm, wb_jw_t *w, wv v, int pretty) {  //returns 0 for values JSON cannot represent (undefined, functions)
    uint32_t t = WV_TAGOF(v), i;
    if (w->depth > 12) { wb_jw_put(w, "null", 4); return 1; }
    if (WV_ISNUM(v)) { char b[48]; double d = wv_dbl(v); if (d != d || fabs(d) == HUGE_VAL) wb_jw_put(w, "null", 4); else { uint32_t l = wb_num_str(d, b); wb_jw_put(w, b, l); } return 1; }
    if (v == WV_NULL) { wb_jw_put(w, "null", 4); return 1; }
    if (v == WV_TRUE) { wb_jw_put(w, "true", 4); return 1; }
    if (v == WV_FALSE) { wb_jw_put(w, "false", 5); return 1; }
    if (v == WV_UNDEF || wb_is_callable(vm, v)) return 0;
    if (wb_is_str(vm, v)) { uint32_t l; const char *p = wb_str_ptr(vm, v, &l); wb_jw_str(w, p, l); return 1; }
    if (wb_is_arr(vm, v)) {
        w->depth++; wb_jw_c(w, '[');
        for (i = 0; i < wb_arr(vm, v)->len; i++) { if (i) wb_jw_c(w, ','); if (!wb_json_write(vm, w, wb_arr_el(vm, v)[i], pretty)) wb_jw_put(w, "null", 4); }
        wb_jw_c(w, ']'); w->depth--; return 1;
    }
    if (wb_is_obj_type(vm, v, H_OBJ)) {
        uint32_t cnt = ((wb_hobj_t *)(vm->jheap + WV_PAY(v)))->n, emitted = 0;
        w->depth++; wb_jw_c(w, '{');
        for (i = 0; i < cnt; i++) {
            uint8_t *e = vm->jheap + ((wb_hobj_t *)(vm->jheap + WV_PAY(v)))->tab + 8 + (size_t)i * 16; uint16_t k = (uint16_t)*(uint32_t *)e; uint32_t before = w->len;
            if (emitted) wb_jw_c(w, ',');
            wb_jw_str(w, wb_astr(vm, k), wb_alen(vm, k)); wb_jw_c(w, ':');
            if (wb_json_write(vm, w, *(wv *)(e + 8), pretty)) emitted++; else w->len = before;
        }
        wb_jw_c(w, '}'); w->depth--; return 1;
    }
    (void)t; wb_jw_str(w, "[object]", 8); return 1;
}
NAT(nat_json_stringify) {
    wb_jw_t w; uint32_t cap; wv r;
    w.b = (char *)wb_scratch(vm, &cap); w.cap = cap > 60000 ? 60000 : cap; w.len = 0; w.over = 0; w.depth = 0;
    if (!wb_json_write(vm, &w, ARG(0), 0)) return WV_UNDEF;
    if (w.over) THROWRET("RangeError", "JSON.stringify result too large for the free arena space");
    r = wb_str_new(vm, w.b, w.len);
    return r;
}
typedef struct { eccles_web_vm_t *vm; const char *p; uint32_t n, i; uint8_t depth; uint8_t err; } wb_jp_t;
static void wb_jp_ws(wb_jp_t *j) { while (j->i < j->n && wb_is_space((uint8_t)j->p[j->i])) j->i++; }
static wv wb_jp_value(wb_jp_t *j);
static wv wb_jp_string(wb_jp_t *j) {
    char buf[WB_STRBUF]; uint32_t l = 0;
    j->i++;
    while (j->i < j->n && j->p[j->i] != '"') {
        char c = j->p[j->i++];
        if (c == '\\' && j->i < j->n) {
            c = j->p[j->i++];
            switch (c) { case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break; case 'b': c = '\b'; break; case 'f': c = '\f'; break;
                case 'u': { uint32_t cp = 0, k; for (k = 0; k < 4 && j->i < j->n; k++) { int h = wb_hex((uint8_t)j->p[j->i++]); if (h < 0) { j->err = 1; return WV_UNDEF; } cp = cp * 16 + (uint32_t)h; } if (l + 4 < sizeof buf) wb_utf8_put(buf, &l, cp); continue; }
                default: break; }
        }
        if (l + 1 < sizeof buf) buf[l++] = c; else { j->err = 1; return WV_UNDEF; }
    }
    if (j->i >= j->n) { j->err = 1; return WV_UNDEF; }
    j->i++;
    return wb_str_new(j->vm, buf, l);
}
static wv wb_jp_value(wb_jp_t *j) {
    eccles_web_vm_t *vm = j->vm; wv r;
    wb_jp_ws(j);
    if (j->i >= j->n || j->depth > 12) { j->err = 1; return WV_UNDEF; }
    if (j->p[j->i] == '"') return wb_jp_string(j);
    if (j->p[j->i] == '{') {
        j->i++; j->depth++;
        r = wb_obj_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
        wb_root(vm, r); wb_jp_ws(j);
        if (j->i < j->n && j->p[j->i] == '}') { j->i++; } else for (;;) {
            wv k, v; uint16_t ka; wb_jp_ws(j);
            if (j->i >= j->n || j->p[j->i] != '"') { j->err = 1; break; }
            k = wb_jp_string(j); if (j->err || vm->jexc_pending) break;
            ka = wb_key_atom(vm, k); if (vm->jexc_pending) break;
            wb_jp_ws(j); if (j->i >= j->n || j->p[j->i] != ':') { j->err = 1; break; } j->i++;
            v = wb_jp_value(j); if (j->err || vm->jexc_pending) break;
            wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 1]), ka, v); if (vm->jexc_pending) break;
            wb_jp_ws(j);
            if (j->i < j->n && j->p[j->i] == ',') { j->i++; continue; }
            if (j->i < j->n && j->p[j->i] == '}') { j->i++; break; }
            j->err = 1; break;
        }
        j->depth--; r = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1); return r;
    }
    if (j->p[j->i] == '[') {
        j->i++; j->depth++;
        r = wb_arr_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
        wb_root(vm, r); wb_jp_ws(j);
        if (j->i < j->n && j->p[j->i] == ']') { j->i++; } else for (;;) {
            wv v = wb_jp_value(j); if (j->err || vm->jexc_pending) break;
            wb_arr_push(vm, vm->jroots[vm->njroots - 1], v); if (vm->jexc_pending) break;
            wb_jp_ws(j);
            if (j->i < j->n && j->p[j->i] == ',') { j->i++; continue; }
            if (j->i < j->n && j->p[j->i] == ']') { j->i++; break; }
            j->err = 1; break;
        }
        j->depth--; r = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1); return r;
    }
    if (j->n - j->i >= 4 && !strncmp(j->p + j->i, "true", 4)) { j->i += 4; return WV_TRUE; }
    if (j->n - j->i >= 5 && !strncmp(j->p + j->i, "false", 5)) { j->i += 5; return WV_FALSE; }
    if (j->n - j->i >= 4 && !strncmp(j->p + j->i, "null", 4)) { j->i += 4; return WV_NULL; }
    { char tmp[40], *end; uint32_t k = 0; double d;
      while (j->i < j->n && k < 38 && ((j->p[j->i] >= '0' && j->p[j->i] <= '9') || j->p[j->i] == '-' || j->p[j->i] == '+' || j->p[j->i] == '.' || j->p[j->i] == 'e' || j->p[j->i] == 'E')) tmp[k++] = j->p[j->i++];
      tmp[k] = 0; if (!k) { j->err = 1; return WV_UNDEF; }
      d = strtod(tmp, &end); if (*end) { j->err = 1; return WV_UNDEF; } return wv_num(d); }
}
NAT(nat_json_parse) {
    wv s = wb_tostr(vm, ARG(0)), r; wb_jp_t j;
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, s);
    j.vm = vm; j.p = wb_str_ptr(vm, s, &j.n); j.i = 0; j.depth = 0; j.err = 0;
    r = wb_jp_value(&j);
    wb_jp_ws(&j);
    wb_unroot(vm, 1);
    if (vm->jexc_pending) return WV_UNDEF;
    if (j.err || j.i != j.n) THROWRET("SyntaxError", "invalid JSON");
    return r;
}

//console.log: same writer, values are printed the way the browser console prints them (strings plain, objects as json)
static void wb_console_line(eccles_web_vm_t *vm, const wv *a, int n, const char *prefix) {
    wb_jw_t w; uint32_t cap; int i;
    w.b = (char *)wb_scratch(vm, &cap); w.cap = cap > 4000 ? 4000 : cap; w.len = 0; w.over = 0; w.depth = 0;
    wb_jw_put(&w, prefix, (uint32_t)strlen(prefix));
    for (i = 0; i < n; i++) {
        if (i) wb_jw_c(&w, ' ');
        if (wb_is_str(vm, a[i])) { uint32_t l; const char *p = wb_str_ptr(vm, a[i], &l); wb_jw_put(&w, p, l); }
        else if (WV_TAGOF(a[i]) == WV_T_OBJ && (wb_is_arr(vm, a[i]) || wb_is_obj_type(vm, a[i], H_OBJ)) && wb_obj_find(vm, WV_PAY(a[i]), A_message) < 0 && 1) { if (!wb_json_write(vm, &w, a[i], 0)) wb_jw_put(&w, "undefined", 9); }
        else { char b[160]; uint32_t l = wb_cstr_of(vm, a[i], b, sizeof b); if (vm->jexc_pending) { vm->jexc_pending = 0; l = 0; } wb_jw_put(&w, b, l); }
    }
    wb_log(vm, w.b, w.len);
}
NAT(nat_console_log) { wb_console_line(vm, a, n, ""); return WV_UNDEF; }
NAT(nat_console_warn) { wb_console_line(vm, a, n, "warn: "); return WV_UNDEF; }
NAT(nat_console_error) { wb_console_line(vm, a, n, "error: "); return WV_UNDEF; }

//Date: a number of milliseconds since 1970 (utc), no time zones
#ifndef ECCLES_WEB_NOW_MS
  #define ECCLES_WEB_NOW_MS() ((double)time(NULL) * 1000.0)
#endif
NAT(nat_date_now) { return wv_num(ECCLES_WEB_NOW_MS()); }
NAT(nat_date_ctor) {
    wv o; double t = n > 0 ? wb_tonum(vm, a[0]) : ECCLES_WEB_NOW_MS();
    if (!vm->new_call) { char b[64]; time_t s = (time_t)(ECCLES_WEB_NOW_MS() / 1000); struct tm *tm = gmtime(&s); strftime(b, sizeof b, "%a %b %d %Y %H:%M:%S GMT", tm); return wb_str_cstr(vm, b); }
    o = wb_obj_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    ((wb_hh_t *)(vm->jheap + WV_PAY(o)))->aux = (uint16_t)(K_DATE_S << 8);  //kind tag: dispatches getTime() & co (see wb_getprop)
    wb_obj_set(vm, WV_PAY(o), A_time, wv_num(t));
    return o;
}
static double wb_date_t(eccles_web_vm_t *vm, wv self) { int f; wv v; if (!wb_is_obj_type(vm, self, H_OBJ)) return NAN; v = wb_obj_get(vm, WV_PAY(self), A_time, &f); return f ? wv_dbl(v) : NAN; }
static int wb_date_tm(eccles_web_vm_t *vm, wv self, struct tm *out) { double t = wb_date_t(vm, self); time_t s; struct tm *r; if (t != t) return 0; s = (time_t)floor(t / 1000.0); r = gmtime(&s); if (!r) return 0; *out = *r; return 1; }
NAT(nat_date_getTime) { return wv_num(wb_date_t(vm, self)); }
NAT(nat_date_getFullYear) { struct tm t; return wb_date_tm(vm, self, &t) ? wv_num(t.tm_year + 1900) : wv_num(NAN); }
NAT(nat_date_getMonth) { struct tm t; return wb_date_tm(vm, self, &t) ? wv_num(t.tm_mon) : wv_num(NAN); }
NAT(nat_date_getDate) { struct tm t; return wb_date_tm(vm, self, &t) ? wv_num(t.tm_mday) : wv_num(NAN); }
NAT(nat_date_getDay) { struct tm t; return wb_date_tm(vm, self, &t) ? wv_num(t.tm_wday) : wv_num(NAN); }
NAT(nat_date_getHours) { struct tm t; return wb_date_tm(vm, self, &t) ? wv_num(t.tm_hour) : wv_num(NAN); }
NAT(nat_date_getMinutes) { struct tm t; return wb_date_tm(vm, self, &t) ? wv_num(t.tm_min) : wv_num(NAN); }
NAT(nat_date_getSeconds) { struct tm t; return wb_date_tm(vm, self, &t) ? wv_num(t.tm_sec) : wv_num(NAN); }
NAT(nat_date_toISOString) { struct tm t; char b[48]; if (!wb_date_tm(vm, self, &t)) THROWRET("RangeError", "invalid date"); snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (int)fmod(wb_date_t(vm, self), 1000.0)); return wb_str_cstr(vm, b); }

//6. dom bindings, events, timers
#define DX(kind, node) WV_MK(WV_T_DOMX, ((uint32_t)(kind) << 16) | (uint32_t)(node))
enum { DX_DOC = 1, DX_WIN, DX_LOC, DX_HIST, DX_NAV, DX_STYLE, DX_CLASS, DX_DATA };
#define WB_WINDOW_NODE 0xFFFFu
#define WB_NF_DETACHED 2

static uint16_t wb_nodeof(wv v) { return (uint16_t)(WV_PAY(v) & 0xFFFFu); }
static uint8_t wb_dxkind(wv v) { return (uint8_t)(WV_PAY(v) >> 16); }
static int wb_node_ok(const eccles_web_vm_t *vm, uint16_t n) { return n < vm->nnodes && vm->nodes[n].type != WB_N_FREE; }
static wv wb_node_val(uint16_t n) { return n == 0 ? DX(DX_DOC, 0) : wv_elem(n); }
static uint16_t wb_target_node(wv v) {
    if (v == WV_UNDEF) return WB_WINDOW_NODE;  //addEventListener(...) called as a plain global function
    if (WV_TAGOF(v) == WV_T_ELEM) return wb_nodeof(v);
    if (WV_TAGOF(v) == WV_T_DOMX) { uint8_t k = wb_dxkind(v); if (k == DX_DOC) return 0; if (k == DX_WIN) return WB_WINDOW_NODE; }
    return 0xFFFEu;
}

static int wb_is_editable(const eccles_web_vm_t *vm, uint16_t n);
static void wb_scr_report(eccles_web_vm_t *vm);  //logs and counts an uncaught exception (clears it)
static void wb_drain(eccles_web_vm_t *vm);
static void wb_ensure_layout(eccles_web_vm_t *vm);
static void wb_request_nav(eccles_web_vm_t *vm, const char *url);

//tree surgery
static void wb_dom_detach(eccles_web_vm_t *vm, uint16_t n) {
    wb_node_t *nd = &vm->nodes[n]; wb_node_t *p;
    if (nd->flags & WB_NF_DETACHED) return;
    p = &vm->nodes[nd->parent];
    if (p->child == n) { p->child = nd->next; if (p->last == n) p->last = 0; }
    else { uint16_t c = p->child; while (c && vm->nodes[c].next != n) c = vm->nodes[c].next; if (c) { vm->nodes[c].next = nd->next; if (p->last == n) p->last = c; } }
    nd->parent = 0; nd->next = 0; nd->flags |= WB_NF_DETACHED; vm->dirty = 1;
    if (vm->focus == n) vm->focus = 0;
}
static int wb_dom_is_ancestor(const eccles_web_vm_t *vm, uint16_t anc, uint16_t n) { while (n) { if (n == anc) return 1; n = vm->nodes[n].parent; } return 0; }
//puts n under parent before ref (0 = at the end). returns 0 if that would make a cycle
static int wb_dom_insert(eccles_web_vm_t *vm, uint16_t parent, uint16_t n, uint16_t ref) {
    wb_node_t *p = &vm->nodes[parent], *nd = &vm->nodes[n];
    if (n == 0 || wb_dom_is_ancestor(vm, n, parent)) return 0;
    if (ref && vm->nodes[ref].parent != parent) ref = 0;
    wb_dom_detach(vm, n);
    nd->flags &= (uint8_t)~WB_NF_DETACHED; nd->parent = parent; nd->next = 0;
    if (!ref) { if (p->last) vm->nodes[p->last].next = n; else p->child = n; p->last = n; }
    else {
        if (p->child == ref) { nd->next = ref; p->child = n; }
        else { uint16_t c = p->child; while (c && vm->nodes[c].next != ref) c = vm->nodes[c].next; vm->nodes[c].next = n; nd->next = ref; }
    }
    vm->dirty = 1;
    return 1;
}
#if ECCLES_WEB_JS
static void wb_listeners_drop_node(eccles_web_vm_t *vm, uint16_t n) {
    uint16_t i = 0;
    while (i < vm->nlisteners) { if (vm->listeners[i].node == n) { vm->listeners[i] = vm->listeners[--vm->nlisteners]; } else i++; }
}
#endif
static void wb_dom_free_rec(eccles_web_vm_t *vm, uint16_t n, int depth) {
    uint16_t c = vm->nodes[n].child;
    if (depth < 200) while (c) { uint16_t nx = vm->nodes[c].next; wb_dom_free_rec(vm, c, depth + 1); c = nx; }
#if ECCLES_WEB_JS
    wb_listeners_drop_node(vm, n);
#endif
    if (vm->focus == n) vm->focus = 0;
    { uint16_t at = vm->nodes[n].attr; while (at) { uint16_t nx = vm->attrs[at].next; wb_attr_free(vm, at); at = nx; } }
    memset(&vm->nodes[n], 0, sizeof vm->nodes[n]);
    vm->nodes[n].type = WB_N_FREE; vm->nodes[n].next = vm->free_node; vm->free_node = n;
}
//frees the children of n (the content of an element that is being replaced)
static void wb_dom_clear(eccles_web_vm_t *vm, uint16_t n) {
    uint16_t c = vm->nodes[n].child;
    while (c) { uint16_t nx = vm->nodes[c].next; wb_dom_free_rec(vm, c, 0); c = nx; }
    vm->nodes[n].child = vm->nodes[n].last = 0; vm->dirty = 1;
}
//detached nodes that the script cannot reach any more are given back (called by the collector, which knows what the script still refers to)
static void wb_dom_reclaim(eccles_web_vm_t *vm, const uint8_t *ebits) {
    uint16_t i;
    if (!vm->nodes) return;
    for (i = 1; i < vm->nnodes; i++) {
        uint16_t n, root = i; int live = 0;
        if (vm->nodes[i].type == WB_N_FREE || !(vm->nodes[i].flags & WB_NF_DETACHED)) continue;
        n = root;
        for (;;) {  //is any node of this subtree still referenced
            if (ebits[n >> 3] & (1u << (n & 7))) { live = 1; break; }
            if (vm->nodes[n].child) n = vm->nodes[n].child;
            else { while (n != root && !vm->nodes[n].next) n = vm->nodes[n].parent; if (n == root) break; n = vm->nodes[n].next; }
        }
        if (!live) wb_dom_free_rec(vm, root, 0);
    }
}
static void wb_gc_for_nodes(eccles_web_vm_t *vm) { if (vm->jheap) wb_gc(vm); }

//creating nodes from script
static uint16_t wb_dom_new_text(eccles_web_vm_t *vm, const char *s, uint32_t n) {
    wb_node_t *t; uint32_t off;
    if (n > 0xFFFF) n = 0xFFFF;
    off = wb_pool_alloc(vm, n); if (off == 0xFFFFFFFFu) return 0;
    memcpy(vm->text + off, s, n);
    t = wb_node_alloc(vm, WB_N_TEXT, 0); if (!t) return 0;
    t->text = off; t->tlen = (uint16_t)n; t->flags = WB_NF_DETACHED;
    return wb_idx(vm, t);
}
static uint16_t wb_dom_new_elem(eccles_web_vm_t *vm, uint16_t tag) {
    wb_node_t *e = wb_node_alloc(vm, WB_N_ELEM, tag); if (!e) return 0;
    e->flags = WB_NF_DETACHED;
    return wb_idx(vm, e);
}

//selectors for querySelector & co: the same parser and matcher as the style sheets
static int wb_parse_selectors(eccles_web_vm_t *vm, const char *s, uint32_t n, wb_rule_t *out, int max) {
    int cnt = 0; uint32_t st = 0, k;
    for (k = 0; k <= n && cnt < max; k++) {
        if (k == n || s[k] == ',') { if (wb_sel_parse(vm, s + st, k - st, &out[cnt])) cnt++; st = k + 1; }
    }
    return cnt;
}
static int wb_match_any(const eccles_web_vm_t *vm, uint16_t n, const wb_rule_t *r, int nr) { int i; for (i = 0; i < nr; i++) if (wb_match_rule(vm, n, &r[i])) return 1; return 0; }
//collects the matching descendants of root (document order). first_only stops at the first one. out = an array value or WV_UNDEF
static uint16_t wb_query(eccles_web_vm_t *vm, uint16_t root, const wb_rule_t *r, int nr, wv out) {
    uint16_t n = vm->nodes[root].child;
    while (n) {
        if (vm->nodes[n].type == WB_N_ELEM && wb_match_any(vm, n, r, nr)) {
            if (out == WV_UNDEF) return n;
            if (!wb_arr_push(vm, out, wv_elem(n))) return 0;
        }
        if (vm->nodes[n].child) n = vm->nodes[n].child;
        else { while (n != root && !vm->nodes[n].next) n = vm->nodes[n].parent; if (n == root) break; n = vm->nodes[n].next; }
    }
    return 0;
}

//text and html of a subtree. They are built in the free space of the arena
static void wb_subtree_text(const eccles_web_vm_t *vm, uint16_t root, wb_jw_t *w) {
    uint16_t n = root;
    if (vm->nodes[root].type == WB_N_TEXT) { wb_jw_put(w, wb_text_of(vm, &vm->nodes[root]), vm->nodes[root].tlen); return; }
    n = vm->nodes[root].child;
    while (n) {
        if (vm->nodes[n].type == WB_N_TEXT) wb_jw_put(w, wb_text_of(vm, &vm->nodes[n]), vm->nodes[n].tlen);
        if (vm->nodes[n].child) n = vm->nodes[n].child;
        else { while (n != root && !vm->nodes[n].next) n = vm->nodes[n].parent; if (n == root) break; n = vm->nodes[n].next; }
    }
}
static void wb_jw_esc(wb_jw_t *w, const char *p, uint32_t n, int attr) {
    uint32_t i;
    for (i = 0; i < n; i++) { char c = p[i]; if (c == '&') wb_jw_put(w, "&amp;", 5); else if (c == '<') wb_jw_put(w, "&lt;", 4); else if (c == '>') wb_jw_put(w, "&gt;", 4); else if (attr && c == '"') wb_jw_put(w, "&quot;", 6); else wb_jw_c(w, c); }
}
static void wb_serialize(const eccles_web_vm_t *vm, uint16_t n, wb_jw_t *w, int depth) {
    const wb_node_t *nd = &vm->nodes[n]; uint16_t c;
    if (depth > 40 || w->over) return;
    if (nd->type == WB_N_TEXT) { wb_jw_esc(w, wb_text_of(vm, nd), nd->tlen, 0); return; }
    if (nd->type == WB_N_ELEM) {
        uint16_t at = nd->attr; uint16_t names[24]; int na = 0, i;
        wb_jw_c(w, '<'); wb_jw_put(w, wb_astr(vm, nd->tag), wb_alen(vm, nd->tag));
        while (at && na < 24) { names[na++] = at; at = vm->attrs[at].next; }
        for (i = na - 1; i >= 0; i--) {  //attributes are kept newest first, write them in source order
            const wb_attr_t *a = &vm->attrs[names[i]];
            wb_jw_c(w, ' '); wb_jw_put(w, wb_astr(vm, a->name), wb_alen(vm, a->name));
            if (a->len) { wb_jw_put(w, "=\"", 2); wb_jw_esc(w, (const char *)vm->text + a->off, a->len, 1); wb_jw_c(w, '"'); }
        }
        wb_jw_c(w, '>');
        if (wb_is_void(nd->tag)) return;
    }
    for (c = nd->child; c; c = vm->nodes[c].next) wb_serialize(vm, c, w, depth + 1);
    if (nd->type == WB_N_ELEM) { wb_jw_put(w, "</", 2); wb_jw_put(w, wb_astr(vm, nd->tag), wb_alen(vm, nd->tag)); wb_jw_c(w, '>'); }
}
static void wb_jw_open(eccles_web_vm_t *vm, wb_jw_t *w, uint32_t max) { uint32_t cap; w->b = (char *)wb_scratch(vm, &cap); w->cap = cap > max ? max : cap; w->len = 0; w->over = 0; w->depth = 0; }

static wv wb_dom_text_get(eccles_web_vm_t *vm, uint16_t n) { wb_jw_t w; wb_jw_open(vm, &w, 30000); wb_subtree_text(vm, n, &w); return wb_str_new(vm, w.b, w.len); }
static void wb_dom_text_set(eccles_web_vm_t *vm, uint16_t n, wv v) {
    wv s = wb_tostr(vm, v); uint32_t len; const char *p;
    if (vm->jexc_pending) return;
    p = wb_str_ptr(vm, s, &len);
    if (vm->nodes[n].type == WB_N_TEXT) {  //a text node: in place when it fits
        wb_node_t *t = &vm->nodes[n];
        if (len <= t->tlen) { memcpy(vm->text + t->text, p, len); vm->text[t->text + len] = 0; t->tlen = (uint16_t)len; }
        else { uint32_t off = wb_pool_alloc(vm, len > 0xFFFF ? 0xFFFF : len); if (off != 0xFFFFFFFFu) { memcpy(vm->text + off, p, len > 0xFFFF ? 0xFFFF : len); t->text = off; t->tlen = (uint16_t)(len > 0xFFFF ? 0xFFFF : len); } }
        vm->dirty = 1; return;
    }
    {   //an element with just one text child keeps it (a counter that changes every second does not eat the text pool)
        uint16_t c = vm->nodes[n].child;
        if (c && !vm->nodes[c].next && vm->nodes[c].type == WB_N_TEXT && len <= vm->nodes[c].tlen && len > 0) { wb_dom_text_set(vm, c, s); return; }
    }
    wb_dom_clear(vm, n);
    if (len) { uint16_t t = wb_dom_new_text(vm, p, len); if (t) wb_dom_insert(vm, n, t, 0); }
}
static void wb_dom_html_set(eccles_web_vm_t *vm, uint16_t n, wv v) {
    wv s = wb_tostr(vm, v); uint32_t len; const char *p; wb_reader_t r;
    if (vm->jexc_pending) return;
    p = wb_str_ptr(vm, s, &len);
    wb_dom_clear(vm, n);
    wb_rd_mem(&r, p, len);
    wb_html_run_in(vm, &r, n);
    if (vm->err) { wb_fail_str(vm, vm->err, vm->err_info, "innerHTML"); vm->err = ECCLES_WEB_OK; wb_throw_err(vm, "RangeError", "innerHTML: page limits reached (nodes / text pool)"); }
    vm->dirty = 1;
}

//attributes seen from script
static wv wb_attr_wv(const eccles_web_vm_t *vm, uint16_t n, uint16_t name, int null_if_missing) {
    uint32_t l; const char *p = wb_attr_get(vm, n, name, &l);
    if (!p) return null_if_missing ? WV_NULL : wv_atom(0);
    return wb_str_new((eccles_web_vm_t *)vm, p, l);
}
static void wb_attr_from_wv(eccles_web_vm_t *vm, uint16_t n, uint16_t name, wv v) {
    wv s = wb_tostr(vm, v); uint32_t l; const char *p;
    if (vm->jexc_pending) return;
    p = wb_str_ptr(vm, s, &l);
    if (!wb_attr_set(vm, n, name, p, l, 0)) { vm->err = ECCLES_WEB_OK; wb_throw_err(vm, "RangeError", "attribute table / text pool full"); }
}
static uint16_t wb_name_atom(eccles_web_vm_t *vm, wv v, int lower) {
    char b[ECCLES_WEB_MAX_NAME + 1]; uint32_t l = wb_cstr_of(vm, v, b, sizeof b), i; uint16_t a;
    if (vm->jexc_pending) return 0;
    if (lower) for (i = 0; i < l; i++) b[i] = (char)wb_lc((uint8_t)b[i]);
    a = wb_atom(vm, b, l);
    if (!a && !vm->jexc_pending) { vm->err = ECCLES_WEB_OK; wb_throw_err(vm, "RangeError", "too many names (ECCLES_WEB_MAX_ATOMS)"); }
    return a;
}

//classList: edits the class attribute string. op 0 add, 1 remove, 2 toggle. returns whether the class is there afterwards
static int wb_class_edit(eccles_web_vm_t *vm, uint16_t n, const char *cls, uint32_t cl, int op) {
    char buf[320]; uint32_t l, i = 0, o = 0; const char *s = wb_attr_get(vm, n, A_class, &l); int had = 0;
    if (!s) { s = ""; l = 0; }
    while (i < l) {
        uint32_t j; while (i < l && wb_is_space((uint8_t)s[i])) i++;
        j = i; while (j < l && !wb_is_space((uint8_t)s[j])) j++;
        if (j > i) {
            if (j - i == cl && !memcmp(s + i, cls, cl)) had = 1;
            if (!((j - i == cl && !memcmp(s + i, cls, cl)) && (op == 1 || op == 2 || op == 0))) { if (o + (j - i) + 2 < sizeof buf) { if (o) buf[o++] = ' '; memcpy(buf + o, s + i, j - i); o += j - i; } }
        }
        i = j;
    }
    if (op == 0 || (op == 2 && !had)) { if (o + cl + 2 < sizeof buf) { if (o) buf[o++] = ' '; memcpy(buf + o, cls, cl); o += cl; } wb_attr_set(vm, n, A_class, buf, o, o + 16); return 1; }
    wb_attr_set(vm, n, A_class, buf, o, o + 16);
    return 0;
}

//inline style from script: element.style.backgroundColor = "red"
static void wb_css_name_of(const char *js, char *out, uint32_t cap) {  //backgroundColor -> background-color
    uint32_t o = 0; const char *p;
    for (p = js; *p && o + 2 < cap; p++) { if (*p >= 'A' && *p <= 'Z') { out[o++] = '-'; out[o++] = (char)(*p + 32); } else out[o++] = *p; }
    out[o] = 0;
}
static void wb_style_put(eccles_web_vm_t *vm, uint16_t n, const char *name, const char *val, uint32_t vl) {
    char buf[480]; uint32_t l, o = 0, i = 0, nl = (uint32_t)strlen(name); const char *s = wb_attr_get(vm, n, A_style, &l);
    if (!s) { s = ""; l = 0; }
    while (i < l) {  //copy the declarations that are not the one being set
        uint32_t j = i, colon; while (j < l && s[j] != ';') j++;
        colon = i; while (colon < j && s[colon] != ':') colon++;
        { uint32_t a = i, b = colon; while (a < b && wb_is_space((uint8_t)s[a])) a++; while (b > a && wb_is_space((uint8_t)s[b - 1])) b--;
          if (j > i && !(b - a == nl && !strncmp(s + a, name, nl)) && o + (j - i) + 2 < sizeof buf) { memcpy(buf + o, s + i, j - i); o += j - i; buf[o++] = ';'; } }
        i = j + 1;
    }
    if (vl && o + nl + vl + 3 < sizeof buf) { memcpy(buf + o, name, nl); o += nl; buf[o++] = ':'; memcpy(buf + o, val, vl); o += vl; buf[o++] = ';'; }
    wb_attr_set(vm, n, A_style, buf, o, o + 48);
}
static wv wb_style_read(eccles_web_vm_t *vm, uint16_t n, const char *name) {
    uint32_t l, i = 0, nl = (uint32_t)strlen(name); const char *s = wb_attr_get(vm, n, A_style, &l);
    if (!s) return wv_atom(0);
    while (i < l) {
        uint32_t j = i, colon; while (j < l && s[j] != ';') j++;
        colon = i; while (colon < j && s[colon] != ':') colon++;
        { uint32_t a = i, b = colon; while (a < b && wb_is_space((uint8_t)s[a])) a++; while (b > a && wb_is_space((uint8_t)s[b - 1])) b--;
          if (b - a == nl && !strncmp(s + a, name, nl) && colon < j) { uint32_t vs = colon + 1, ve = j; while (vs < ve && wb_is_space((uint8_t)s[vs])) vs++; while (ve > vs && wb_is_space((uint8_t)s[ve - 1])) ve--; return wb_str_new(vm, s + vs, ve - vs); } }
        i = j + 1;
    }
    return wv_atom(0);
}
static void wb_style_set_js(eccles_web_vm_t *vm, uint16_t n, uint16_t jsprop, wv value) {
    char name[ECCLES_WEB_MAX_NAME + 8], val[160]; uint32_t vl;
    wb_css_name_of(wb_astr(vm, jsprop), name, sizeof name);
    vl = wb_cstr_of(vm, value, val, sizeof val);
    if (vm->jexc_pending) return;
    if (!strcmp(name, "css-float")) return;
    wb_style_put(vm, n, name, val, vl);
}

//dataset: el.dataset.userName <-> data-user-name
static void wb_data_attr(const char *js, char *out, uint32_t cap) { memcpy(out, "data-", 5); wb_css_name_of(js, out + 5, cap - 5); }

//reading and writing properties of dom values
static wv wb_children_arr(eccles_web_vm_t *vm, uint16_t n, int elements_only) {
    wv arr = wb_arr_new(vm, 0); uint16_t c;
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, arr);
    for (c = vm->nodes[n].child; c; c = vm->nodes[c].next) if (!elements_only || vm->nodes[c].type == WB_N_ELEM) if (!wb_arr_push(vm, arr, wv_elem(c))) break;
    wb_unroot(vm, 1);
    return arr;
}
static uint16_t wb_prev_sibling(const eccles_web_vm_t *vm, uint16_t n, int elements_only) {
    uint16_t p = vm->nodes[n].parent, c = vm->nodes[p].child, prev = 0;
    if (vm->nodes[n].flags & WB_NF_DETACHED) return 0;
    while (c && c != n) { if (!elements_only || vm->nodes[c].type == WB_N_ELEM) prev = c; c = vm->nodes[c].next; }
    return prev;
}
static wv wb_elem_or_null(uint16_t n) { return n ? wv_elem(n) : WV_NULL; }
static wv wb_first_elem_child(const eccles_web_vm_t *vm, uint16_t n, int last) {
    uint16_t c = vm->nodes[n].child, r = 0;
    while (c) { if (vm->nodes[c].type == WB_N_ELEM) { r = c; if (!last) break; } c = vm->nodes[c].next; }
    return wb_elem_or_null(r);
}
static wv wb_next_elem(const eccles_web_vm_t *vm, uint16_t n) { uint16_t c = vm->nodes[n].next; if (vm->nodes[n].flags & WB_NF_DETACHED) return WV_NULL; while (c && vm->nodes[c].type != WB_N_ELEM) c = vm->nodes[c].next; return wb_elem_or_null(c); }
static wv wb_metric(eccles_web_vm_t *vm, uint16_t n, int which) {
    const wb_box_t *b;
    wb_ensure_layout(vm);
    if (n >= vm->nnodes || !vm->boxes[n].res) return wv_num(0);
    b = &vm->boxes[n];
    switch (which) { case 0: return wv_num(b->w); case 1: return wv_num((double)b->h); case 2: return wv_num(b->x); default: return wv_num((double)b->y); }
}

static wv wb_dom_get(eccles_web_vm_t *vm, wv o, uint16_t atom) {
    wv r;
    if (WV_TAGOF(o) == WV_T_ELEM) {
        uint16_t n = wb_nodeof(o); const wb_node_t *nd;
        if (!wb_node_ok(vm, n)) return WV_UNDEF;
        nd = &vm->nodes[n];
        switch (atom) {
            case A_textContent: case A_innerText: case A_nodeValue: case A_data: return wb_dom_text_get(vm, n);
            case A_innerHTML: case A_outerHTML: { wb_jw_t w; wb_jw_open(vm, &w, 30000); if (atom == A_outerHTML) wb_serialize(vm, n, &w, 0); else { uint16_t c; for (c = nd->child; c; c = vm->nodes[c].next) wb_serialize(vm, c, &w, 0); } return wb_str_new(vm, w.b, w.len); }
            case A_nodeType: return wv_num(nd->type == WB_N_TEXT ? 3 : 1);
            case A_tagName: case A_nodeName: { char b[ECCLES_WEB_MAX_NAME + 1]; uint32_t i, l; if (nd->type != WB_N_ELEM) return WV_UNDEF; l = wb_alen(vm, nd->tag); if (l > ECCLES_WEB_MAX_NAME) l = ECCLES_WEB_MAX_NAME; for (i = 0; i < l; i++) { char c = wb_astr(vm, nd->tag)[i]; b[i] = (char)((c >= 'a' && c <= 'z') ? c - 32 : c); } return wb_str_new(vm, b, l); }
            case A_id: case A_href: case A_src: case A_type: case A_name: case A_placeholder: case A_alt: case A_title: return wb_attr_wv(vm, n, atom, 0);
            case A_className: return wb_attr_wv(vm, n, A_class, 0);
            case A_value: if (nd->tag == A_textarea) return wb_dom_text_get(vm, n); return wb_attr_wv(vm, n, A_value, 0);
            case A_checked: return wv_bool(wb_attr_find(vm, n, A_checked) != NULL);
            case A_disabled: return wv_bool(wb_attr_find(vm, n, A_disabled) != NULL);
            case A_hidden: return wv_bool(wb_attr_find(vm, n, A_hidden) != NULL);
            case A_parentNode: case A_parentElement: if (nd->flags & WB_NF_DETACHED) return WV_NULL; return atom == A_parentElement && nd->parent == 0 ? WV_NULL : wb_node_val(nd->parent);
            case A_firstChild: return wb_elem_or_null(nd->child);
            case A_lastChild: return wb_elem_or_null(nd->last);
            case A_nextSibling: return (nd->flags & WB_NF_DETACHED) ? WV_NULL : wb_elem_or_null(nd->next);
            case A_previousSibling: return wb_elem_or_null(wb_prev_sibling(vm, n, 0));
            case A_firstElementChild: return wb_first_elem_child(vm, n, 0);
            case A_lastElementChild: return wb_first_elem_child(vm, n, 1);
            case A_nextElementSibling: return wb_next_elem(vm, n);
            case A_previousElementSibling: return wb_elem_or_null(wb_prev_sibling(vm, n, 1));
            case A_children: return wb_children_arr(vm, n, 1);
            case A_childNodes: return wb_children_arr(vm, n, 0);
            case A_childElementCount: { uint16_t c; uint32_t k = 0; for (c = nd->child; c; c = vm->nodes[c].next) if (vm->nodes[c].type == WB_N_ELEM) k++; return wv_num(k); }
            case A_style: return DX(DX_STYLE, n);
            case A_classList: return DX(DX_CLASS, n);
            case A_dataset: return DX(DX_DATA, n);
            case A_offsetWidth: case A_clientWidth: return wb_metric(vm, n, 0);
            case A_offsetHeight: case A_clientHeight: return wb_metric(vm, n, 1);
            case A_offsetLeft: return wb_metric(vm, n, 2);
            case A_offsetTop: return wb_metric(vm, n, 3);
            case A_scrollTop: return wv_num(n == vm->body_node ? vm->scroll_y : 0);
            case A_isConnected: { uint16_t c = n; while (c && !(vm->nodes[c].flags & WB_NF_DETACHED)) c = vm->nodes[c].parent; return wv_bool(!(vm->nodes[n].flags & WB_NF_DETACHED) && !c); }
            default: break;
        }
        r = wb_nat_value(K_ELEM, atom);
        return r != WV_NONE ? r : WV_UNDEF;
    }
    switch (wb_dxkind(o)) {
        case DX_DOC:
            switch (atom) {
                case A_body: return wb_elem_or_null(vm->body_node);
                case A_documentElement: { uint16_t h = vm->nodes[0].child; while (h && vm->nodes[h].tag != A_html) h = vm->nodes[h].next; return wb_elem_or_null(h); }
                case A_head: { uint16_t h = vm->nodes[0].child, c = 0; while (h && vm->nodes[h].tag != A_html) h = vm->nodes[h].next; if (h) { c = vm->nodes[h].child; while (c && vm->nodes[c].tag != A_head) c = vm->nodes[c].next; } return wb_elem_or_null(c); }
                case A_title: { const char *t = eccles_web_vm_title(vm); return wb_str_cstr(vm, t); }
                case A_readyState: return wv_atom(A_complete);
                case A_cookie: return wv_atom(0);
                case A_URL: case A_documentURI: return wb_str_cstr(vm, vm->url);
                case A_location: return DX(DX_LOC, 0);
                case A_defaultView: return DX(DX_WIN, 0);
                case A_activeElement: return wb_elem_or_null(vm->focus ? vm->focus : vm->body_node);
                case A_nodeType: return wv_num(9);
                default: r = wb_nat_value(K_DOCUMENT, atom); return r != WV_NONE ? r : WV_UNDEF;
            }
        case DX_WIN:
            switch (atom) {
                case A_window: case A_self: case A_globalThis: case A_top: case A_parent: return DX(DX_WIN, 0);
                case A_document: return DX(DX_DOC, 0);
                case A_location: return DX(DX_LOC, 0);
                case A_history: return DX(DX_HIST, 0);
                case A_navigator: return DX(DX_NAV, 0);
                case A_innerWidth: case A_outerWidth: return wv_num(vm->vw);
                case A_innerHeight: case A_outerHeight: return wv_num(vm->vh);
                case A_scrollY: case A_pageYOffset: return wv_num(vm->scroll_y);
                case A_scrollX: case A_pageXOffset: return wv_num(0);
                case A_devicePixelRatio: return wv_num(1);
                default: { int found; wv v = wb_obj_get(vm, WV_PAY(vm->jglobal), atom, &found); if (found) return v; r = wb_nat_value(K_WINDOW, atom); return r != WV_NONE ? r : WV_UNDEF; }
            }
        case DX_LOC:
            switch (atom) {
                case A_href: return wb_str_cstr(vm, vm->url);
                case A_pathname: { const char *p = strstr(vm->url, "://"); p = p ? strchr(p + 3, '/') : vm->url; return wb_str_cstr(vm, p ? p : "/"); }
                case A_protocol: return wb_str_cstr(vm, !strncmp(vm->url, "https", 5) ? "https:" : !strncmp(vm->url, "http", 4) ? "http:" : "file:");
                default: r = wb_nat_value(K_LOCATION, atom); return r != WV_NONE ? r : WV_UNDEF;
            }
        case DX_HIST: if (atom == A_length) return wv_num(vm->nhist + 1); r = wb_nat_value(K_HISTORY, atom); return r != WV_NONE ? r : WV_UNDEF;
        case DX_NAV: if (atom == A_userAgent) return wb_str_cstr(vm, "Mozilla/5.0 (Eccles) EcclesWeb/" ECCLES_WEB_VERSION_STRING); if (atom == A_language) return wb_str_cstr(vm, "en"); if (atom == A_onLine) return WV_TRUE; if (atom == A_platform) return wb_str_cstr(vm, "ESP"); return WV_UNDEF;
        case DX_STYLE: {
            uint16_t n = wb_nodeof(o); char name[ECCLES_WEB_MAX_NAME + 8];
            r = wb_nat_value(K_STYLE, atom); if (r != WV_NONE) return r;
            wb_css_name_of(wb_astr(vm, atom), name, sizeof name);
            return wb_style_read(vm, n, name);
        }
        case DX_CLASS: {
            uint16_t n = wb_nodeof(o);
            if (atom == A_length) { uint32_t l, i = 0, k = 0; const char *s = wb_attr_get(vm, n, A_class, &l); if (s) while (i < l) { while (i < l && wb_is_space((uint8_t)s[i])) i++; if (i < l) { k++; while (i < l && !wb_is_space((uint8_t)s[i])) i++; } } return wv_num(k); }
            if (atom == A_value) return wb_attr_wv(vm, n, A_class, 0);
            r = wb_nat_value(K_CLASSLIST, atom); return r != WV_NONE ? r : WV_UNDEF;
        }
        case DX_DATA: { char name[ECCLES_WEB_MAX_NAME + 12]; uint16_t a; wb_data_attr(wb_astr(vm, atom), name, sizeof name); a = wb_atom_find(vm, name, (uint32_t)strlen(name)); return a ? wb_attr_wv(vm, wb_nodeof(o), a, 0) : WV_UNDEF; }
        default: return WV_UNDEF;
    }
}

static int wb_on_handler_atom(const eccles_web_vm_t *vm, uint16_t atom, uint16_t *evtype) {  //onclick -> click
    const char *s = wb_astr(vm, atom); uint32_t l = wb_alen(vm, atom); uint16_t a;
    if (l < 3 || s[0] != 'o' || s[1] != 'n') return 0;
    a = wb_atom_find(vm, s + 2, l - 2);
    if (!a) return 0;
    *evtype = a; return 1;
}
static int wb_listener_set(eccles_web_vm_t *vm, uint16_t node, uint16_t type, wv fn, int prop);
static void wb_dom_set(eccles_web_vm_t *vm, wv o, uint16_t atom, wv val) {
    if (WV_TAGOF(o) == WV_T_ELEM) {
        uint16_t n = wb_nodeof(o), ev;
        if (!wb_node_ok(vm, n)) return;
        switch (atom) {
            case A_textContent: case A_innerText: case A_nodeValue: case A_data: wb_dom_text_set(vm, n, val); return;
            case A_innerHTML: wb_dom_html_set(vm, n, val); return;
            case A_id: case A_href: case A_src: case A_type: case A_name: case A_placeholder: case A_alt: case A_title: wb_attr_from_wv(vm, n, atom, val); return;
            case A_className: wb_attr_from_wv(vm, n, A_class, val); return;
            case A_value:
                if (vm->nodes[n].tag == A_textarea) { wb_dom_text_set(vm, n, val); return; }
                { wv s = wb_tostr(vm, val); uint32_t l; const char *p; if (vm->jexc_pending) return; p = wb_str_ptr(vm, s, &l); wb_attr_set(vm, n, A_value, p, l, WB_INPUT_CAP); }
                return;
            case A_checked: case A_disabled: case A_hidden:
                if (wb_truthy(vm, val)) { if (!wb_attr_find(vm, n, atom)) wb_attr_set(vm, n, atom, "", 0, 0); } else wb_attr_remove(vm, n, atom);
                if (atom == A_hidden) wb_style_put(vm, n, "display", wb_truthy(vm, val) ? "none" : "", wb_truthy(vm, val) ? 4 : 0);
                vm->dirty = 1; return;
            case A_scrollTop: if (n == vm->body_node) { vm->scroll_y = (int32_t)wb_tonum(vm, val); wb_ensure_layout(vm); if (vm->scroll_y < 0) vm->scroll_y = 0; vm->dirty = 1; } return;
            default: break;
        }
        if (wb_on_handler_atom(vm, atom, &ev)) { wb_listener_set(vm, n, ev, val, 1); return; }
        return;  //expando properties on elements are not kept
    }
    switch (wb_dxkind(o)) {
        case DX_DOC: if (atom == A_title) { wv s = wb_tostr(vm, val); uint32_t l; const char *p; if (vm->jexc_pending) return; p = wb_str_ptr(vm, s, &l);
                        if (vm->title_node) { uint16_t t = vm->nodes[vm->title_node].child; if (t) wb_dom_text_set(vm, t, s); else { uint16_t tx = wb_dom_new_text(vm, p, l); if (tx) wb_dom_insert(vm, vm->title_node, tx, 0); } } return; }
                     { uint16_t ev; if (wb_on_handler_atom(vm, atom, &ev)) wb_listener_set(vm, 0, ev, val, 1); } return;
        case DX_WIN: { uint16_t ev; if (wb_on_handler_atom(vm, atom, &ev)) { wb_listener_set(vm, WB_WINDOW_NODE, ev, val, 1); return; } wb_obj_set(vm, WV_PAY(vm->jglobal), atom, val); return; }
        case DX_LOC: if (atom == A_href) { char u[ECCLES_WEB_MAX_URL]; wb_cstr_of(vm, val, u, sizeof u); if (vm->perms & ECCLES_WEB_PERM_NAVIGATE) wb_request_nav(vm, u); else wb_deny(vm, "navigate"); } return;
        case DX_STYLE: wb_style_set_js(vm, wb_nodeof(o), atom, val); return;
        case DX_CLASS: if (atom == A_value) wb_attr_from_wv(vm, wb_nodeof(o), A_class, val); return;
        case DX_DATA: { char name[ECCLES_WEB_MAX_NAME + 12]; uint16_t a; wb_data_attr(wb_astr(vm, atom), name, sizeof name); a = wb_atom(vm, name, (uint32_t)strlen(name)); if (a) wb_attr_from_wv(vm, wb_nodeof(o), a, val); return; }
        default: return;
    }
}

//methods of elements and the document
static uint16_t wb_self_node(eccles_web_vm_t *vm, wv self) {
    uint16_t n = wb_target_node(self);
    if (n == 0xFFFEu || n == WB_WINDOW_NODE || !wb_node_ok(vm, n)) { wb_type_error(vm, "not a dom node"); return 0xFFFEu; }
    return n;
}
static uint16_t wb_arg_node(eccles_web_vm_t *vm, wv v) {
    if (WV_TAGOF(v) == WV_T_ELEM && wb_node_ok(vm, wb_nodeof(v))) return wb_nodeof(v);
    wb_type_error(vm, "argument is not a dom node"); return 0xFFFEu;
}
NAT(nat_el_getAttribute) { uint16_t nd = wb_self_node(vm, self), at; if (nd == 0xFFFEu) return WV_UNDEF; at = wb_name_atom(vm, ARG(0), 1); if (!at) return WV_UNDEF; return wb_attr_wv(vm, nd, at, 1); }
NAT(nat_el_setAttribute) { uint16_t nd = wb_self_node(vm, self), at; if (nd == 0xFFFEu) return WV_UNDEF; at = wb_name_atom(vm, ARG(0), 1); if (!at) return WV_UNDEF; wb_attr_from_wv(vm, nd, at, ARG(1)); return WV_UNDEF; }
NAT(nat_el_removeAttribute) { uint16_t nd = wb_self_node(vm, self), at; if (nd == 0xFFFEu) return WV_UNDEF; at = wb_name_atom(vm, ARG(0), 1); if (at) wb_attr_remove(vm, nd, at); return WV_UNDEF; }
NAT(nat_el_hasAttribute) { uint16_t nd = wb_self_node(vm, self), at; if (nd == 0xFFFEu) return WV_UNDEF; at = wb_name_atom(vm, ARG(0), 1); return wv_bool(at && wb_attr_find(vm, nd, at)); }
NAT(nat_el_appendChild) { uint16_t p = wb_self_node(vm, self), c = p == 0xFFFEu ? 0xFFFEu : wb_arg_node(vm, ARG(0)); if (p == 0xFFFEu || c == 0xFFFEu) return WV_UNDEF; if (!wb_dom_insert(vm, p, c, 0)) THROWRET("HierarchyRequestError", "cannot insert a node into itself"); return a[0]; }
NAT(nat_el_insertBefore) { uint16_t p = wb_self_node(vm, self), c = p == 0xFFFEu ? 0xFFFEu : wb_arg_node(vm, ARG(0)), ref = 0; if (p == 0xFFFEu || c == 0xFFFEu) return WV_UNDEF; if (ARG(1) != WV_NULL && ARG(1) != WV_UNDEF) { ref = wb_arg_node(vm, a[1]); if (ref == 0xFFFEu) return WV_UNDEF; } if (!wb_dom_insert(vm, p, c, ref)) THROWRET("HierarchyRequestError", "cannot insert a node into itself"); return a[0]; }
NAT(nat_el_removeChild) { uint16_t p = wb_self_node(vm, self), c = p == 0xFFFEu ? 0xFFFEu : wb_arg_node(vm, ARG(0)); if (p == 0xFFFEu || c == 0xFFFEu) return WV_UNDEF; if (vm->nodes[c].parent != p || (vm->nodes[c].flags & WB_NF_DETACHED)) THROWRET("NotFoundError", "the node is not a child of this node"); wb_dom_detach(vm, c); return a[0]; }
NAT(nat_el_remove) { uint16_t nd = wb_self_node(vm, self); if (nd == 0xFFFEu) return WV_UNDEF; wb_dom_detach(vm, nd); return WV_UNDEF; }
static void wb_append_args(eccles_web_vm_t *vm, uint16_t p, const wv *a, int n, int prepend) {
    int i; uint16_t first = prepend ? vm->nodes[p].child : 0;
    for (i = 0; i < n; i++) {
        uint16_t c;
        if (WV_TAGOF(a[i]) == WV_T_ELEM) c = wb_nodeof(a[i]);
        else { wv s = wb_tostr(vm, a[i]); uint32_t l; const char *t; if (vm->jexc_pending) return; t = wb_str_ptr(vm, s, &l); c = wb_dom_new_text(vm, t, l); if (!c) return; }
        wb_dom_insert(vm, p, c, first);
    }
}
NAT(nat_el_append) { uint16_t p = wb_self_node(vm, self); if (p == 0xFFFEu) return WV_UNDEF; wb_append_args(vm, p, a, n, 0); return WV_UNDEF; }
NAT(nat_el_prepend) { uint16_t p = wb_self_node(vm, self); if (p == 0xFFFEu) return WV_UNDEF; wb_append_args(vm, p, a, n, 1); return WV_UNDEF; }
NAT(nat_el_contains) { uint16_t p = wb_self_node(vm, self), c = p == 0xFFFEu ? 0xFFFEu : wb_arg_node(vm, ARG(0)); if (p == 0xFFFEu || c == 0xFFFEu) return WV_UNDEF; return wv_bool(wb_dom_is_ancestor(vm, p, c)); }
static int wb_sel_arg(eccles_web_vm_t *vm, wv v, wb_rule_t *rules, int max) {
    char b[160]; uint32_t l = wb_cstr_of(vm, v, b, sizeof b); int k;
    if (vm->jexc_pending) return -1;
    k = wb_parse_selectors(vm, b, l, rules, max);
    if (vm->err) { vm->err = ECCLES_WEB_OK; wb_throw_err(vm, "RangeError", "selector: too many names"); return -1; }
    return k;
}
NAT(nat_el_querySelector) { wb_rule_t sel[6]; uint16_t root = wb_target_node(self), r; int k; if (root == 0xFFFEu || !wb_node_ok(vm, root)) THROWRET("TypeError", "not a dom node"); k = wb_sel_arg(vm, ARG(0), sel, 6); if (k < 0) return WV_UNDEF; r = wb_query(vm, root, sel, k, WV_UNDEF); return wb_elem_or_null(r); }
NAT(nat_el_querySelectorAll) {
    wb_rule_t sel[6]; uint16_t root = wb_target_node(self); int k; wv out;
    if (root == 0xFFFEu || !wb_node_ok(vm, root)) THROWRET("TypeError", "not a dom node");
    k = wb_sel_arg(vm, ARG(0), sel, 6); if (k < 0) return WV_UNDEF;
    out = wb_arr_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, out); wb_query(vm, root, sel, k, out); wb_unroot(vm, 1);
    return out;
}
NAT(nat_el_matches) { wb_rule_t sel[6]; uint16_t nd = wb_self_node(vm, self); int k; if (nd == 0xFFFEu) return WV_UNDEF; k = wb_sel_arg(vm, ARG(0), sel, 6); if (k < 0) return WV_UNDEF; return wv_bool(vm->nodes[nd].type == WB_N_ELEM && wb_match_any(vm, nd, sel, k)); }
NAT(nat_el_closest) { wb_rule_t sel[6]; uint16_t nd = wb_self_node(vm, self); int k; if (nd == 0xFFFEu) return WV_UNDEF; k = wb_sel_arg(vm, ARG(0), sel, 6); if (k < 0) return WV_UNDEF; while (nd && vm->nodes[nd].type == WB_N_ELEM) { if (wb_match_any(vm, nd, sel, k)) return wv_elem(nd); nd = vm->nodes[nd].parent; } return WV_NULL; }
static wv wb_by_tag_or_class(eccles_web_vm_t *vm, wv self, wv arg, int by_class) {
    wb_rule_t r; uint16_t root = wb_target_node(self); wv out; char b[ECCLES_WEB_MAX_NAME + 4]; uint32_t l; uint16_t at;
    if (root == 0xFFFEu || !wb_node_ok(vm, root)) THROWRET("TypeError", "not a dom node");
    l = wb_cstr_of(vm, arg, b, sizeof b); if (vm->jexc_pending) return WV_UNDEF;
    out = wb_arr_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, out);
    memset(&r, 0, sizeof r); r.n = 1;
    if (by_class) { at = wb_atom(vm, b, l); if (!at) { vm->err = ECCLES_WEB_OK; wb_unroot(vm, 1); return out; } r.c[0].cls[0] = at; }
    else { uint32_t i; for (i = 0; i < l; i++) b[i] = (char)wb_lc((uint8_t)b[i]); if (!(l == 1 && b[0] == '*')) { at = wb_atom_find(vm, b, l); if (!at) { wb_unroot(vm, 1); return out; } r.c[0].tag = at; } }
    wb_query(vm, root, &r, 1, out);
    wb_unroot(vm, 1);
    return out;
}
NAT(nat_el_getElementsByTagName) { return wb_by_tag_or_class(vm, self, ARG(0), 0); }
NAT(nat_el_getElementsByClassName) { return wb_by_tag_or_class(vm, self, ARG(0), 1); }
NAT(nat_doc_getElementById) {
    char b[96]; uint32_t l = wb_cstr_of(vm, ARG(0), b, sizeof b), i;
    if (vm->jexc_pending) return WV_UNDEF;
    for (i = 1; i < vm->nnodes; i++) {
        uint32_t al; const char *v;
        if (vm->nodes[i].type != WB_N_ELEM || (vm->nodes[i].flags & WB_NF_DETACHED)) continue;
        v = wb_attr_get(vm, (uint16_t)i, A_id, &al);
        if (v && al == l && !memcmp(v, b, l)) { uint16_t c = (uint16_t)i; while (c && !(vm->nodes[c].flags & WB_NF_DETACHED)) { if (!vm->nodes[c].parent) break; c = vm->nodes[c].parent; } return wv_elem((uint16_t)i); }
    }
    return WV_NULL;
}
NAT(nat_doc_createElement) { uint16_t at = wb_name_atom(vm, ARG(0), 1), e; if (!at) return WV_UNDEF; e = wb_dom_new_elem(vm, at); if (!e) { if (!vm->jexc_pending) { vm->err = ECCLES_WEB_OK; wb_throw_err(vm, "RangeError", "too many dom nodes (ECCLES_WEB_MAX_NODES)"); } return WV_UNDEF; } return wv_elem(e); }
NAT(nat_doc_createTextNode) { wv s = wb_tostr(vm, ARG(0)); uint32_t l; const char *p; uint16_t t; if (vm->jexc_pending) return WV_UNDEF; p = wb_str_ptr(vm, s, &l); t = wb_dom_new_text(vm, p, l); if (!t) { vm->err = ECCLES_WEB_OK; wb_throw_err(vm, "RangeError", "dom node or text pool limit reached"); return WV_UNDEF; } return wv_elem(t); }
NAT(nat_el_focus) { uint16_t nd = wb_self_node(vm, self); if (nd != 0xFFFEu && wb_is_editable(vm, nd)) { vm->focus = nd; vm->dirty = 1; } return WV_UNDEF; }
NAT(nat_el_blur) { vm->focus = 0; vm->dirty = 1; return WV_UNDEF; }
NAT(nat_el_scrollIntoView) { uint16_t nd = wb_self_node(vm, self); if (nd == 0xFFFEu) return WV_UNDEF; wb_ensure_layout(vm); if (vm->boxes[nd].res) { vm->scroll_y = vm->boxes[nd].y; if (vm->scroll_y < 0) vm->scroll_y = 0; vm->dirty = 1; } return WV_UNDEF; }
NAT(nat_el_getBoundingClientRect) {
    uint16_t nd = wb_self_node(vm, self); wv o; const wb_box_t *b; double x = 0, y = 0, w = 0, h = 0;
    if (nd == 0xFFFEu) return WV_UNDEF;
    wb_ensure_layout(vm);
    if (vm->boxes[nd].res) { b = &vm->boxes[nd]; x = b->x; y = (double)(b->y - vm->scroll_y); w = b->w; h = (double)b->h; }
    o = wb_obj_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, o);
    wb_obj_set(vm, WV_PAY(o), A_x, wv_num(x)); wb_obj_set(vm, WV_PAY(o), A_y, wv_num(y)); wb_obj_set(vm, WV_PAY(o), A_left, wv_num(x)); wb_obj_set(vm, WV_PAY(o), A_top, wv_num(y));
    wb_obj_set(vm, WV_PAY(o), A_width, wv_num(w)); wb_obj_set(vm, WV_PAY(o), A_height, wv_num(h)); wb_obj_set(vm, WV_PAY(o), A_right, wv_num(x + w)); wb_obj_set(vm, WV_PAY(o), A_bottom, wv_num(y + h));
    wb_unroot(vm, 1);
    return o;
}
NAT(nat_win_scrollTo) { double y = n > 1 ? wb_tonum(vm, a[1]) : wb_tonum(vm, ARG(0)); if (WV_TAGOF(ARG(0)) == WV_T_OBJ) { int f; wv t = wb_obj_get(vm, WV_PAY(a[0]), A_top, &f); y = f ? wb_tonum(vm, t) : 0; } eccles_web_vm_scroll_to(vm, (int32_t)y); return WV_UNDEF; }
NAT(nat_win_scrollBy) { double y = n > 1 ? wb_tonum(vm, a[1]) : wb_tonum(vm, ARG(0)); eccles_web_vm_scroll_by(vm, (int32_t)y); return WV_UNDEF; }
NAT(nat_win_getComputedStyle) { uint16_t nd = wb_arg_node(vm, ARG(0)); return nd == 0xFFFEu ? WV_UNDEF : DX(DX_STYLE, nd); }
//classList / style methods
NAT(nat_cl_add) { uint16_t nd = wb_nodeof(self); int i; char b[64]; for (i = 0; i < n; i++) { uint32_t l = wb_cstr_of(vm, a[i], b, sizeof b); wb_class_edit(vm, nd, b, l, 0); } return WV_UNDEF; }
NAT(nat_cl_remove) { uint16_t nd = wb_nodeof(self); int i; char b[64]; for (i = 0; i < n; i++) { uint32_t l = wb_cstr_of(vm, a[i], b, sizeof b); wb_class_edit(vm, nd, b, l, 1); } return WV_UNDEF; }
NAT(nat_cl_toggle) { uint16_t nd = wb_nodeof(self); char b[64]; uint32_t l = wb_cstr_of(vm, ARG(0), b, sizeof b); int had, r; { uint32_t al; const char *s = wb_attr_get(vm, nd, A_class, &al); had = s && wb_has_class(vm, nd, wb_atom(vm, b, l)); }
    if (n > 1 && a[1] != WV_UNDEF) { int force = wb_truthy(vm, a[1]); if (force && !had) wb_class_edit(vm, nd, b, l, 0); else if (!force && had) wb_class_edit(vm, nd, b, l, 1); return wv_bool(force); }
    r = wb_class_edit(vm, nd, b, l, 2); return wv_bool(r); }
NAT(nat_cl_contains) { uint16_t nd = wb_nodeof(self); char b[64]; uint32_t l = wb_cstr_of(vm, ARG(0), b, sizeof b); uint16_t at = wb_atom_find(vm, b, l); return wv_bool(at && wb_has_class(vm, nd, at)); }
NAT(nat_style_setProperty) { uint16_t nd = wb_nodeof(self); char nm[ECCLES_WEB_MAX_NAME + 4], val[160]; uint32_t vl; wb_cstr_of(vm, ARG(0), nm, sizeof nm); vl = wb_cstr_of(vm, ARG(1), val, sizeof val); wb_style_put(vm, nd, nm, val, vl); return WV_UNDEF; }
NAT(nat_style_getPropertyValue) { char nm[ECCLES_WEB_MAX_NAME + 4]; wb_cstr_of(vm, ARG(0), nm, sizeof nm); return wb_style_read(vm, wb_nodeof(self), nm); }
NAT(nat_style_removeProperty) { char nm[ECCLES_WEB_MAX_NAME + 4]; wb_cstr_of(vm, ARG(0), nm, sizeof nm); wb_style_put(vm, wb_nodeof(self), nm, "", 0); return WV_UNDEF; }
//location / history / storage
NAT(nat_loc_assign) { char u[ECCLES_WEB_MAX_URL]; wb_cstr_of(vm, ARG(0), u, sizeof u); if (vm->perms & ECCLES_WEB_PERM_NAVIGATE) wb_request_nav(vm, u); else wb_deny(vm, "navigate"); return WV_UNDEF; }
NAT(nat_loc_reload) { if (vm->perms & ECCLES_WEB_PERM_NAVIGATE) wb_request_nav(vm, vm->url); else wb_deny(vm, "navigate"); return WV_UNDEF; }
NAT(nat_hist_back) { if (!(vm->perms & ECCLES_WEB_PERM_NAVIGATE)) { wb_deny(vm, "navigate"); return WV_UNDEF; } if (vm->nhist) { snprintf(vm->nav_url, sizeof vm->nav_url, "%s", vm->hist[vm->nhist - 1]); vm->nhist--; vm->nav_pending = 1; vm->nav_back = 1; } return WV_UNDEF; }
NAT(nat_hist_noop) { return WV_UNDEF; }
NAT(nat_store_getItem) { int f; uint16_t k = wb_key_atom(vm, ARG(0)); wv v; if (vm->jexc_pending) return WV_UNDEF; v = wb_obj_get(vm, WV_PAY(self), k, &f); return f ? v : WV_NULL; }
NAT(nat_store_setItem) { uint16_t k = wb_key_atom(vm, ARG(0)); wv s = wb_tostr(vm, ARG(1)); if (vm->jexc_pending) return WV_UNDEF; wb_obj_set(vm, WV_PAY(self), k, s); return WV_UNDEF; }
NAT(nat_store_removeItem) { uint16_t k = wb_key_atom(vm, ARG(0)); if (!vm->jexc_pending) wb_delprop(vm, self, k); return WV_UNDEF; }
NAT(nat_store_clear) { ((wb_hobj_t *)(vm->jheap + WV_PAY(self)))->n = 0; return WV_UNDEF; }

//events

static wv wb_node_this(uint16_t node) { return node == WB_WINDOW_NODE ? DX(DX_WIN, 0) : wb_node_val(node); }

//adds (or, with prop = 1, replaces the onclick style handler of) a listener
static int wb_listener_set(eccles_web_vm_t *vm, uint16_t node, uint16_t type, wv fn, int prop) {
    uint16_t i, key = (uint16_t)(type | (prop ? 0x8000u : 0u));
    for (i = 0; i < vm->nlisteners; i++) {
        if (vm->listeners[i].node == node && vm->listeners[i].type == key && (prop || wb_strict_eq(vm, vm->listeners[i].fn, fn))) {
            if (prop) { if (wb_is_callable(vm, fn)) vm->listeners[i].fn = fn; else vm->listeners[i] = vm->listeners[--vm->nlisteners]; }
            return 1;
        }
    }
    if (!wb_is_callable(vm, fn)) return 1;
    if (vm->nlisteners >= ECCLES_WEB_MAX_LISTENERS) { wb_throw_err(vm, "RangeError", "too many event listeners (ECCLES_WEB_MAX_LISTENERS)"); return 0; }
    vm->listeners[vm->nlisteners].node = node; vm->listeners[vm->nlisteners].type = key; vm->listeners[vm->nlisteners].fn = fn; vm->nlisteners++;
    return 1;
}
NAT(nat_addEventListener) {
    uint16_t node = wb_target_node(self), type;
    if (node == 0xFFFEu) THROWRET("TypeError", "not an event target");
    type = wb_name_atom(vm, ARG(0), 1); if (!type) return WV_UNDEF;
    wb_listener_set(vm, node, type, ARG(1), 0);
    return WV_UNDEF;
}
NAT(nat_removeEventListener) {
    uint16_t node = wb_target_node(self), type, i;
    if (node == 0xFFFEu) return WV_UNDEF;
    type = wb_name_atom(vm, ARG(0), 1); if (!type) return WV_UNDEF;
    for (i = 0; i < vm->nlisteners; i++) if (vm->listeners[i].node == node && vm->listeners[i].type == type && wb_strict_eq(vm, vm->listeners[i].fn, ARG(1))) { vm->listeners[i] = vm->listeners[--vm->nlisteners]; break; }
    return WV_UNDEF;
}
NAT(nat_event_preventDefault) { vm->event_prevent = 1; if (wb_is_obj_type(vm, self, H_OBJ)) wb_obj_set(vm, WV_PAY(self), A_defaultPrevented, WV_TRUE); return WV_UNDEF; }
NAT(nat_event_stopPropagation) { vm->event_stop = 1; return WV_UNDEF; }

static void wb_js_begin(eccles_web_vm_t *vm) { vm->jscripting++; vm->fuel_left = vm->fuel_budget; }
static void wb_js_end(eccles_web_vm_t *vm) { if (vm->jscripting) vm->jscripting--; }

//calls a script function as the top level of a turn (event, timer): fuel reset, errors reported
static wv wb_turn_call(eccles_web_vm_t *vm, wv fn, wv thisv, const wv *args, int argc) {
    wv r;
    vm->fuel_left = vm->fuel_budget;
    r = wb_call(vm, fn, thisv, args, argc);
    if (vm->jtrap || vm->jexc_pending) wb_scr_report(vm);
    return r;
}

//an onclick="..." attribute is compiled the first time it is needed, the function is then kept like any other listener
static wv wb_attr_handler(eccles_web_vm_t *vm, uint16_t node, uint16_t type) {
    char nm[ECCLES_WEB_MAX_NAME + 4]; uint16_t at; uint32_t l; const char *src;
    if (!(vm->perms & ECCLES_WEB_PERM_JS) || vm->nodes[node].type != WB_N_ELEM) return WV_UNDEF;
    snprintf(nm, sizeof nm, "on%s", wb_astr(vm, type));
    at = wb_atom_find(vm, nm, (uint32_t)strlen(nm)); if (!at) return WV_UNDEF;
    src = wb_attr_get(vm, node, at, &l); if (!src || !l) return WV_UNDEF;
    {
        int idx = wb_compile_script(vm, src, l); uint32_t off; wb_hfunc_t *f;
        if (idx < 0) return WV_UNDEF;
        off = wb_galloc(vm, H_FUNC, sizeof(wb_hfunc_t) - sizeof(wb_hh_t)); if (!off) { vm->jexc_pending = 0; return WV_UNDEF; }
        f = (wb_hfunc_t *)(vm->jheap + off); f->proto = (uint32_t)idx; f->thisv = DX(DX_WIN, 0); f->h.aux = vm->protos[idx].flags;
        return wv_obj(off);
    }
}

static void wb_call_listeners(eccles_web_vm_t *vm, uint16_t node, uint16_t type, wv ev) {
    uint16_t i, cnt = vm->nlisteners; int had_attr = 0;
    wb_obj_set(vm, WV_PAY(ev), A_currentTarget, wb_node_this(node));
    for (i = 0; i < cnt && i < vm->nlisteners; i++) {
        wb_listener_t l = vm->listeners[i];
        if (l.node != node || (l.type & 0x3FFFu) != type) continue;
        if (l.type & 0x4000u) had_attr = 1;
        if (!wb_is_callable(vm, l.fn)) continue;
        wb_root(vm, l.fn);
        wb_turn_call(vm, l.fn, wb_node_this(node), &ev, 1);
        wb_unroot(vm, 1);
        if (vm->event_stop && 0) break;
    }
    if (!had_attr && node != WB_WINDOW_NODE) {
        wv fn = wb_attr_handler(vm, node, type);
        if (vm->jexc_pending) { vm->jexc_pending = 0; fn = WV_UNDEF; }
        if (fn != WV_UNDEF && vm->nlisteners < ECCLES_WEB_MAX_LISTENERS) {
            vm->listeners[vm->nlisteners].node = node; vm->listeners[vm->nlisteners].type = (uint16_t)(type | 0x4000u); vm->listeners[vm->nlisteners].fn = fn; vm->nlisteners++;
            wb_root(vm, fn);
            { wv r = wb_turn_call(vm, fn, wb_node_this(node), &ev, 1); if (r == WV_FALSE) vm->event_prevent = 1; }
            wb_unroot(vm, 1);
        }
    }
}

//sends an event to node and lets it bubble up to the document and the window. returns 1 when a listener called preventDefault()
static int wb_js_dispatch(eccles_web_vm_t *vm, uint16_t node, uint16_t type) {
    wv ev; uint16_t cur; int depth = 0;
    if (!(vm->perms & ECCLES_WEB_PERM_JS) || !vm->jheap) return 0;
    wb_js_begin(vm);
    ev = wb_obj_new(vm, 0);
    if (vm->jexc_pending) { vm->jexc_pending = 0; wb_js_end(vm); return 0; }
    ((wb_hh_t *)(vm->jheap + WV_PAY(ev)))->aux = (uint16_t)(K_EVENT << 8);
    wb_root(vm, ev);
    wb_obj_set(vm, WV_PAY(ev), A_type, wv_atom(type));
    wb_obj_set(vm, WV_PAY(ev), A_target, wb_node_this(node));
    wb_obj_set(vm, WV_PAY(ev), A_defaultPrevented, WV_FALSE);
    vm->event_stop = vm->event_prevent = 0;
    { int found; wv old = wb_obj_get(vm, WV_PAY(vm->jglobal), A_event, &found); wb_obj_set(vm, WV_PAY(vm->jglobal), A_event, ev);
      for (cur = node; depth++ < ECCLES_WEB_MAX_DEPTH * 4; ) {
          wb_call_listeners(vm, cur, type, ev);
          if (vm->event_stop || cur == 0) break;
          cur = vm->nodes[cur].parent;
          if (vm->nodes[cur].flags & WB_NF_DETACHED && cur) break;
      }
      if (!vm->event_stop && type != A_load && type != A_DOMContentLoaded) wb_call_listeners(vm, WB_WINDOW_NODE, type, ev);
      else if (type == A_load) wb_call_listeners(vm, WB_WINDOW_NODE, type, ev);
      wb_obj_set(vm, WV_PAY(vm->jglobal), A_event, found ? old : WV_UNDEF); }
    wb_unroot(vm, 1);
    wb_js_end(vm);
    wb_drain(vm);
    return vm->event_prevent;
}
static void wb_fire_load_events(eccles_web_vm_t *vm) {
    if (!(vm->perms & ECCLES_WEB_PERM_JS)) return;
    wb_js_dispatch(vm, 0, A_DOMContentLoaded);
    wb_js_dispatch(vm, vm->body_node ? vm->body_node : 0, A_load);
}

//timers: setTimeout / setInterval / requestAnimationFrame. Times are relative to the clock the host feeds to eccles_web_tick()
static wv wb_timer_add(eccles_web_vm_t *vm, wv fn, double delay, int repeat, int raf) {
    int i;
    if (!(vm->perms & ECCLES_WEB_PERM_TIMERS)) { wb_deny(vm, "timers"); return wv_num(0); }
    if (!wb_is_callable(vm, fn)) { wb_type_error(vm, "timer callback is not a function"); return WV_UNDEF; }
    if (delay != delay || delay < 0) delay = 0;
    if (delay > 2147483647.0) delay = 2147483647.0;
    if (raf) delay = 16;
    for (i = 0; i < ECCLES_WEB_MAX_TIMERS; i++) if (!vm->timers[i].used) {
        wb_timer_t *t = &vm->timers[i];
        t->used = 1; t->repeat = (uint8_t)repeat; t->fn = fn; t->interval = (uint32_t)delay; t->id = ++vm->timer_seq; if (!t->id) t->id = ++vm->timer_seq;
        t->due = vm->clock_valid ? vm->last_tick + (uint32_t)delay : (uint32_t)delay;  //before the first tick the delay is kept and made absolute then
        t->h_raf = (uint8_t)raf;
        return wv_num(t->id);
    }
    wb_throw_err(vm, "RangeError", "too many timers (ECCLES_WEB_MAX_TIMERS)");
    return WV_UNDEF;
}
NAT(nat_setTimeout) { return wb_timer_add(vm, ARG(0), n > 1 ? wb_tonum(vm, a[1]) : 0, 0, 0); }
NAT(nat_setInterval) { double d = n > 1 ? wb_tonum(vm, a[1]) : 0; return wb_timer_add(vm, ARG(0), d < 1 ? 1 : d, 1, 0); }
NAT(nat_requestAnimationFrame) { return wb_timer_add(vm, ARG(0), 16, 0, 1); }
NAT(nat_clearTimeout) { uint32_t id = (uint32_t)wb_tonum(vm, ARG(0)); int i; for (i = 0; i < ECCLES_WEB_MAX_TIMERS; i++) if (vm->timers[i].used && vm->timers[i].id == id) vm->timers[i].used = 0; return WV_UNDEF; }
NAT(nat_queueMicrotask) { if (!wb_is_callable(vm, ARG(0))) THROWRET("TypeError", "callback is not a function"); wb_mt_push(vm, MT_QUEUE, 0, a[0], WV_UNDEF, WV_UNDEF); return WV_UNDEF; }

static void wb_js_tick(eccles_web_vm_t *vm, uint32_t now) {
    int i;
    if (!(vm->perms & ECCLES_WEB_PERM_JS) || !vm->jheap) return;
    if (!vm->clock_valid) { for (i = 0; i < ECCLES_WEB_MAX_TIMERS; i++) if (vm->timers[i].used) vm->timers[i].due += now; vm->clock_valid = 1; }
    vm->last_tick = now;
    for (;;) {  //run what is due, oldest first (a 10 ms timer fires before a 80 ms one even when both are overdue)
        wb_timer_t *t = NULL; wv fn, arg; int guard;
        for (i = 0; i < ECCLES_WEB_MAX_TIMERS; i++) {
            wb_timer_t *c = &vm->timers[i];
            if (!c->used || (int32_t)(now - c->due) < 0) continue;
            if (!t || (int32_t)(c->due - t->due) < 0) t = c;
        }
        if (!t) break;
        fn = t->fn; arg = t->h_raf ? wv_num((double)now) : WV_UNDEF;
        if (t->repeat) { uint32_t iv = t->interval ? t->interval : 1; t->due += iv; if ((int32_t)(now - t->due) >= 0) t->due = now + iv; } else t->used = 0;
        wb_js_begin(vm); wb_root(vm, fn);
        wb_turn_call(vm, fn, DX(DX_WIN, 0), &arg, t->h_raf ? 1 : 0);
        wb_unroot(vm, 1); wb_js_end(vm);
        wb_drain(vm);
        guard = 0; (void)guard;
    }
    wb_drain(vm);
}
static int wb_js_next_timer(const eccles_web_vm_t *vm, uint32_t now, uint32_t *wait) {
    int i, any = 0; uint32_t best = 0;
    for (i = 0; i < ECCLES_WEB_MAX_TIMERS; i++) {
        const wb_timer_t *t = &vm->timers[i]; uint32_t w;
        if (!t->used) continue;
        w = vm->clock_valid ? ((int32_t)(t->due - now) <= 0 ? 0u : t->due - now) : t->due;
        if (!any || w < best) best = w;
        any = 1;
    }
    if (any && wait) *wait = best;
    return any;
}

//promises, fetch, async host calls, webassembly
static eccles_web_fetch_fn g_fetch;
static const eccles_web_wasm_t *g_wasm;

NAT(nat_prom_resolve_fn) { wb_promise_resolve(vm, self, ARG(0)); return WV_UNDEF; }
NAT(nat_prom_reject_fn) { wb_promise_reject(vm, self, ARG(0)); return WV_UNDEF; }
NAT(nat_promise_ctor) {
    wv p, res, rej, ex = ARG(0), args[2]; int is_new = vm->new_call;
    if (!is_new) THROWRET("TypeError", "Promise constructor cannot be invoked without 'new'");
    if (!wb_is_callable(vm, ex)) THROWRET("TypeError", "Promise resolver is not a function");
    p = wb_promise_new(vm); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, p);
    res = wb_make_bound(vm, nat_prom_resolve_fn, p, WV_UNDEF); if (vm->jexc_pending) { wb_unroot(vm, 1); return WV_UNDEF; }
    wb_root(vm, res);
    rej = wb_make_bound(vm, nat_prom_reject_fn, p, WV_UNDEF); if (vm->jexc_pending) { wb_unroot(vm, 2); return WV_UNDEF; }
    args[0] = res; args[1] = rej; wb_root(vm, rej);
    vm->new_call = 0;
    wb_call(vm, ex, WV_UNDEF, args, 2);
    if (vm->jexc_pending && !vm->jtrap) { wv e = vm->jexc; vm->jexc_pending = 0; wb_promise_reject(vm, vm->jroots[vm->njroots - 3], e); }
    p = vm->jroots[vm->njroots - 3]; wb_unroot(vm, 3);
    return p;
}
static wv wb_promise_then(eccles_web_vm_t *vm, wv p, wv onf, wv onr) {
    wv child;
    if (!wb_is_obj_type(vm, p, H_PROMISE)) THROWRET("TypeError", "not a promise");
    wb_root(vm, p); wb_root(vm, onf); wb_root(vm, onr);
    child = wb_promise_new(vm);
    if (!vm->jexc_pending) { wb_root(vm, child); wb_promise_react(vm, vm->jroots[vm->njroots - 4], vm->jroots[vm->njroots - 3], vm->jroots[vm->njroots - 2], child); wb_unroot(vm, 1); }
    wb_unroot(vm, 3);
    return vm->jexc_pending ? WV_UNDEF : child;
}
NAT(nat_promise_then) { return wb_promise_then(vm, self, ARG(0), ARG(1)); }
NAT(nat_promise_catch) { return wb_promise_then(vm, self, WV_UNDEF, ARG(0)); }
NAT(nat_finally_ok) { wb_call(vm, self, WV_UNDEF, NULL, 0); return ARG(0); }
NAT(nat_finally_fail) { wb_call(vm, self, WV_UNDEF, NULL, 0); if (!vm->jexc_pending) wb_throw(vm, ARG(0)); return WV_UNDEF; }
NAT(nat_promise_finally) {
    wv f = ARG(0), onf, onr;
    if (!wb_is_callable(vm, f)) return wb_promise_then(vm, self, f, f);
    wb_root(vm, self); wb_root(vm, f);
    onf = wb_make_bound(vm, nat_finally_ok, f, WV_UNDEF); if (vm->jexc_pending) { wb_unroot(vm, 2); return WV_UNDEF; }
    wb_root(vm, onf);
    onr = wb_make_bound(vm, nat_finally_fail, f, WV_UNDEF); if (vm->jexc_pending) { wb_unroot(vm, 3); return WV_UNDEF; }
    wb_root(vm, onr);
    { wv r = wb_promise_then(vm, self, onf, onr); wb_unroot(vm, 4); return r; }
}
static wv wb_promise_of(eccles_web_vm_t *vm, wv v) {
    wv p;
    if (wb_is_obj_type(vm, v, H_PROMISE)) return v;
    wb_root(vm, v); p = wb_promise_new(vm); wb_unroot(vm, 1);
    if (vm->jexc_pending) return WV_UNDEF;
    wb_promise_settle(vm, p, 1, v);
    return p;
}
NAT(nat_promise_resolve) { return wb_promise_of(vm, ARG(0)); }
NAT(nat_promise_reject) { wv p = wb_promise_new(vm); if (vm->jexc_pending) return WV_UNDEF; wb_promise_settle(vm, p, 2, ARG(0)); return p; }
//Promise.all / race / allSettled: agg = [results, remaining, promise], each element gets bound handlers carrying agg and its index
NAT(nat_all_ok) {
    wv agg = self, results = wb_arr_el(vm, agg)[0]; uint32_t idx = (uint32_t)wv_dbl(vm->jbd1); double rem;
    wb_arr_el(vm, results)[idx] = ARG(0);
    rem = wv_dbl(wb_arr_el(vm, agg)[1]) - 1; wb_arr_el(vm, agg)[1] = wv_num(rem);
    if (rem <= 0) wb_promise_resolve(vm, wb_arr_el(vm, agg)[2], results);
    return WV_UNDEF;
}
NAT(nat_all_fail) { wb_promise_reject(vm, wb_arr_el(vm, self)[2], ARG(0)); return WV_UNDEF; }
NAT(nat_race_ok) { wb_promise_resolve(vm, wb_arr_el(vm, self)[2], ARG(0)); return WV_UNDEF; }
static wv wb_settled_obj(eccles_web_vm_t *vm, int ok, wv v) {
    wv o = wb_obj_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, o); wb_obj_set(vm, WV_PAY(o), A_status, wv_atom(ok ? A_fulfilled : A_rejected)); wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 1]), ok ? A_value : A_reason, v); wb_unroot(vm, 1);
    return o;
}
NAT(nat_settled_ok) { wv results = wb_arr_el(vm, self)[0]; uint32_t idx = (uint32_t)wv_dbl(vm->jbd1); double rem; wv o = wb_settled_obj(vm, 1, ARG(0)); if (vm->jexc_pending) return WV_UNDEF; wb_arr_el(vm, results)[idx] = o; rem = wv_dbl(wb_arr_el(vm, self)[1]) - 1; wb_arr_el(vm, self)[1] = wv_num(rem); if (rem <= 0) wb_promise_resolve(vm, wb_arr_el(vm, self)[2], results); return WV_UNDEF; }
NAT(nat_settled_fail) { wv results = wb_arr_el(vm, self)[0]; uint32_t idx = (uint32_t)wv_dbl(vm->jbd1); double rem; wv o = wb_settled_obj(vm, 0, ARG(0)); if (vm->jexc_pending) return WV_UNDEF; wb_arr_el(vm, results)[idx] = o; rem = wv_dbl(wb_arr_el(vm, self)[1]) - 1; wb_arr_el(vm, self)[1] = wv_num(rem); if (rem <= 0) wb_promise_resolve(vm, wb_arr_el(vm, self)[2], results); return WV_UNDEF; }
static wv wb_promise_combine(eccles_web_vm_t *vm, wv list, int mode) {
    wv p, agg, results; uint32_t i, cnt; uint8_t rooted = 0;
    if (!wb_is_arr(vm, list)) THROWRET("TypeError", "argument is not iterable");
    cnt = wb_arr(vm, list)->len;
    p = wb_promise_new(vm); if (vm->jexc_pending) return WV_UNDEF; wb_root(vm, p); rooted++;
    agg = wb_arr_new(vm, 3); if (vm->jexc_pending) { wb_unroot(vm, rooted); return WV_UNDEF; } wb_root(vm, agg); rooted++;
    results = wb_arr_new(vm, cnt); if (vm->jexc_pending) { wb_unroot(vm, rooted); return WV_UNDEF; } wb_root(vm, results); rooted++;
    for (i = 0; i < cnt; i++) wb_arr_push(vm, results, WV_UNDEF);
    wb_arr_push(vm, agg, results); wb_arr_push(vm, agg, wv_num((double)cnt)); wb_arr_push(vm, agg, p);
    if (cnt == 0 && mode != 1) wb_promise_resolve(vm, p, results);
    for (i = 0; i < cnt && !vm->jexc_pending; i++) {
        wv el = wb_promise_of(vm, wb_arr_el(vm, list)[i]), onf, onr;
        if (vm->jexc_pending) break;
        wb_root(vm, el);
        onf = wb_make_bound(vm, mode == 0 ? nat_all_ok : mode == 1 ? nat_race_ok : nat_settled_ok, agg, wv_num((double)i)); if (vm->jexc_pending) { wb_unroot(vm, 1); break; }
        wb_root(vm, onf);
        onr = wb_make_bound(vm, mode == 2 ? nat_settled_fail : nat_all_fail, agg, wv_num((double)i)); if (vm->jexc_pending) { wb_unroot(vm, 2); break; }
        wb_promise_react(vm, el, onf, onr, WV_UNDEF);
        wb_unroot(vm, 2);
    }
    wb_unroot(vm, rooted);
    return vm->jexc_pending ? WV_UNDEF : p;
}
NAT(nat_promise_all) { return wb_promise_combine(vm, ARG(0), 0); }
NAT(nat_promise_race) { return wb_promise_combine(vm, ARG(0), 1); }
NAT(nat_promise_allSettled) { return wb_promise_combine(vm, ARG(0), 2); }

//pending requests (fetch and async host calls)
static int wb_pending_add(eccles_web_vm_t *vm, wv prom, uint8_t kind, uint32_t *id) {
    int i;
    for (i = 0; i < ECCLES_WEB_MAX_FETCH; i++) if (!vm->fetches[i].id) { *id = ++vm->fetch_seq; if (!*id) *id = ++vm->fetch_seq; vm->fetches[i].id = *id; vm->fetches[i].prom = prom; vm->fetches[i].kind = kind; return i; }
    return -1;
}
static int wb_pending_find(const eccles_web_vm_t *vm, uint32_t id, uint8_t kind) { int i; if (!id) return -1; for (i = 0; i < ECCLES_WEB_MAX_FETCH; i++) if (vm->fetches[i].id == id && vm->fetches[i].kind == kind) return i; return -1; }
static wv wb_error_wv(eccles_web_vm_t *vm, const char *type, const char *msg) {
    wv m = wb_str_cstr(vm, msg), e; uint16_t t = wb_atom(vm, type, (uint32_t)strlen(type));
    if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, m); e = wb_make_error(vm, t ? t : A_Error, vm->jroots[vm->njroots - 1]); wb_unroot(vm, 1);
    return e;
}
static void wb_reject_msg(eccles_web_vm_t *vm, wv p, const char *type, const char *msg) {
    wv e;
    wb_root(vm, p); e = wb_error_wv(vm, type, msg);
    if (vm->jexc_pending) { vm->jexc_pending = 0; e = wv_atom(A_OutOfMemory); }
    wb_promise_reject(vm, vm->jroots[vm->njroots - 1], e); wb_unroot(vm, 1);
}

//responses: a plain object tagged as a Response, the body is one script string (binary safe)
static wv wb_make_response(eccles_web_vm_t *vm, uint32_t status, const void *body, size_t len, const char *url) {
    wv o, b;
    if (len > 0x7FFFFFu) len = 0x7FFFFFu;
    o = wb_obj_new(vm, 0); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, o);
    ((wb_hh_t *)(vm->jheap + WV_PAY(o)))->aux = (uint16_t)(K_RESPONSE << 8);
    b = len ? wb_str_new(vm, (const char *)body, (uint32_t)len) : wv_atom(0);
    if (vm->jexc_pending) { wb_unroot(vm, 1); return WV_UNDEF; }
    wb_root(vm, b);
    wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 2]), A_bodyText, b);
    wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 2]), A_status, wv_num(status));
    wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 2]), A_ok, wv_bool(status >= 200 && status < 300));
    { wv u = wb_str_cstr(vm, url ? url : ""); if (!vm->jexc_pending) wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 2]), A_url, u); }
    o = vm->jroots[vm->njroots - 2]; wb_unroot(vm, 2);
    return o;
}
NAT(nat_resp_text) {
    int f; wv body = wb_is_obj_type(vm, self, H_OBJ) ? wb_obj_get(vm, WV_PAY(self), A_bodyText, &f) : WV_UNDEF, p; (void)f;
    wb_root(vm, body); p = wb_promise_new(vm); body = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1);
    if (vm->jexc_pending) return WV_UNDEF;
    wb_promise_settle(vm, p, 1, body);
    return p;
}
NAT(nat_resp_json) {
    int f; wv body = wb_is_obj_type(vm, self, H_OBJ) ? wb_obj_get(vm, WV_PAY(self), A_bodyText, &f) : WV_UNDEF, p, r; (void)f;
    p = wb_promise_new(vm); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, p);
    r = nat_json_parse(vm, WV_UNDEF, &body, 1);
    if (vm->jexc_pending && !vm->jtrap) { wv e = vm->jexc; vm->jexc_pending = 0; wb_promise_reject(vm, vm->jroots[vm->njroots - 1], e); }
    else wb_promise_settle(vm, vm->jroots[vm->njroots - 1], 1, r);
    p = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1);
    return p;
}

//the page's fetch(): needs ECCLES_WEB_PERM_NET
static wv wb_fetch_fallback(eccles_web_vm_t *vm, wv p, const char *url) {  //no async backend: use the (blocking) net backend, body capped by the free arena space
    wb_src_t s; uint32_t cap, got = 0; uint8_t *buf = wb_scratch(vm, &cap); int n; wv resp;
    if (cap > 24000) cap = 24000;
    if (!wb_open(vm, url, &s, 1)) { vm->err = ECCLES_WEB_OK; wb_reject_msg(vm, p, "TypeError", "Failed to fetch"); return p; }
    while (got < cap && (n = wb_src_read(&s, buf + got, cap - got)) > 0) got += (uint32_t)n;
    wb_src_close(&s);
    resp = wb_make_response(vm, 200, buf, got, url);
    if (vm->jexc_pending) { vm->jexc_pending = 0; wb_reject_msg(vm, p, "RangeError", "response too large for the script heap"); return p; }
    wb_promise_resolve(vm, p, resp);
    return p;
}
NAT(nat_fetch) {
    wv p, opts = ARG(1); char url[ECCLES_WEB_MAX_URL], abs[ECCLES_WEB_MAX_URL], method[16] = "GET", hdrs[256]; uint32_t id; int slot; eccles_web_request_t rq; const uint8_t *body = NULL; uint32_t blen = 0;
    p = wb_promise_new(vm); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, p);
    if (!(vm->perms & ECCLES_WEB_PERM_NET)) { wb_deny(vm, "net"); wb_reject_msg(vm, p, "TypeError", "Failed to fetch: network access is not allowed"); p = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1); return p; }
    wb_cstr_of(vm, ARG(0), url, sizeof url);
    wb_url_resolve(vm->url, url, abs, sizeof abs);
    hdrs[0] = 0;
    if (wb_is_obj_type(vm, opts, H_OBJ)) {
        int f; wv m = wb_obj_get(vm, WV_PAY(opts), A_method, &f), b, h;
        if (f) { uint32_t i; wb_cstr_of(vm, m, method, sizeof method); for (i = 0; method[i]; i++) if (method[i] >= 'a' && method[i] <= 'z') method[i] = (char)(method[i] - 32); }
        b = wb_obj_get(vm, WV_PAY(opts), A_body, &f); if (f && wb_is_str(vm, b)) body = (const uint8_t *)wb_str_ptr(vm, b, &blen);
        h = wb_obj_get(vm, WV_PAY(opts), A_headers, &f);
        if (f && wb_is_obj_type(vm, h, H_OBJ)) { uint32_t k, o = 0; for (k = 0; k < ((wb_hobj_t *)(vm->jheap + WV_PAY(h)))->n; k++) { uint8_t *e = vm->jheap + ((wb_hobj_t *)(vm->jheap + WV_PAY(h)))->tab + 8 + (size_t)k * 16; char vb[80]; wb_cstr_of(vm, *(wv *)(e + 8), vb, sizeof vb); o += (uint32_t)snprintf(hdrs + o, sizeof hdrs - o, "%s: %s\r\n", wb_astr(vm, (uint16_t)*(uint32_t *)e), vb); if (o >= sizeof hdrs - 2) break; } }
    }
    vm->jexc_pending = 0;
    if (!g_fetch) { wv r = wb_fetch_fallback(vm, vm->jroots[vm->njroots - 1], abs); wb_unroot(vm, 1); return r; }
    slot = wb_pending_add(vm, vm->jroots[vm->njroots - 1], 1, &id);
    if (slot < 0) { wb_reject_msg(vm, vm->jroots[vm->njroots - 1], "RangeError", "too many requests in flight (ECCLES_WEB_MAX_FETCH)"); p = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1); return p; }
    rq.url = abs; rq.method = method; rq.headers = hdrs; rq.body = body; rq.body_len = blen;
    if (g_fetch(vm, id, &rq) != WB_OK) { vm->fetches[slot].id = 0; wb_reject_msg(vm, vm->jroots[vm->njroots - 1], "TypeError", "Failed to fetch"); }
    p = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1);
    return p;
}

//host functions called by the page (names given to eccles_web_register)
static wv wb_from_val(eccles_web_vm_t *vm, const eccles_web_val_t *v) {
    switch (v->type) {
        case ECCLES_WEB_V_NUM: return wv_num(v->num);
        case ECCLES_WEB_V_BOOL: return wv_bool(v->num != 0);
        case ECCLES_WEB_V_NULL: return WV_NULL;
        case ECCLES_WEB_V_STR: return v->str ? wb_str_cstr(vm, v->str) : wv_atom(0);
        default: return WV_UNDEF;
    }
}
static void wb_to_val(eccles_web_vm_t *vm, wv v, eccles_web_val_t *o) {
    o->num = 0; o->str = NULL;
    if (WV_ISNUM(v)) { o->type = ECCLES_WEB_V_NUM; o->num = wv_dbl(v); }
    else if (v == WV_TRUE || v == WV_FALSE) { o->type = ECCLES_WEB_V_BOOL; o->num = v == WV_TRUE; }
    else if (v == WV_NULL) o->type = ECCLES_WEB_V_NULL;
    else if (v == WV_UNDEF) o->type = ECCLES_WEB_V_UNDEF;
    else if (wb_is_str(vm, v)) { uint32_t l; o->type = ECCLES_WEB_V_STR; o->str = wb_str_ptr(vm, v, &l); }
    else o->type = ECCLES_WEB_V_OTHER;
}
static wv wb_host_call(eccles_web_vm_t *vm, uint32_t idx, const wv *a, int n) {
    eccles_web_val_t args[ECCLES_WEB_JS_MAX_ARGS], ret; int i, cnt = n > ECCLES_WEB_JS_MAX_ARGS ? ECCLES_WEB_JS_MAX_ARGS : n; wb_st st;
    if (idx >= vm->nhost) return WV_UNDEF;
    if (!(vm->perms & ECCLES_WEB_PERM_HOST)) { wb_deny(vm, vm->host[idx].name); return WV_UNDEF; }
    for (i = 0; i < cnt; i++) wb_to_val(vm, a[i], &args[i]);
    memset(&ret, 0, sizeof ret);
    if (vm->host[idx].afn) {
        wv p = wb_promise_new(vm); uint32_t id; int slot;
        if (vm->jexc_pending) return WV_UNDEF;
        wb_root(vm, p);
        slot = wb_pending_add(vm, p, 0, &id);
        if (slot < 0) { wb_reject_msg(vm, vm->jroots[vm->njroots - 1], "RangeError", "too many host calls in flight"); p = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1); return p; }
        st = vm->host[idx].afn(vm, id, args, (uint32_t)cnt);
        if (st != WB_OK) { vm->fetches[slot].id = 0; wb_reject_msg(vm, vm->jroots[vm->njroots - 1], "Error", "host call failed"); }
        p = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1);
        return p;
    }
    st = vm->host[idx].fn(vm, args, (uint32_t)cnt, &ret);
    if (st != WB_OK) { wb_fail(vm, ECCLES_WEB_TRAP_HOST, idx); wb_throw_err(vm, "Error", "host function failed"); return WV_UNDEF; }
    return wb_from_val(vm, &ret);
}

//webassembly through the host's backend (eccles_wasm)
static void wb_wasm_release_all(eccles_web_vm_t *vm) {
    int i;
    for (i = 0; i < ECCLES_WEB_MAX_WASM; i++) { if (vm->wasm[i].used && g_wasm && g_wasm->destroy) g_wasm->destroy(vm->wasm[i].inst); vm->wasm[i].used = 0; vm->wasm[i].inst = NULL; vm->wasm[i].imports = WV_UNDEF; }
}
NAT(nat_wasm_call) {  //self = slot number, jbd1 = export name atom
    int slot = (int)wv_dbl(self); eccles_web_val_t args[ECCLES_WEB_JS_MAX_ARGS], ret; int i, cnt = n > ECCLES_WEB_JS_MAX_ARGS ? ECCLES_WEB_JS_MAX_ARGS : n;
    if (slot < 0 || slot >= ECCLES_WEB_MAX_WASM || !vm->wasm[slot].used || !g_wasm || !g_wasm->call) THROWRET("Error", "WebAssembly instance is gone");
    for (i = 0; i < cnt; i++) { args[i].type = ECCLES_WEB_V_NUM; args[i].num = wb_tonum(vm, a[i]); args[i].str = NULL; }
    memset(&ret, 0, sizeof ret);
    if (g_wasm->call(vm->wasm[slot].inst, wb_astr(vm, (uint16_t)wv_dbl(vm->jbd1)), args, (uint32_t)cnt, &ret) != WB_OK) { wb_fail(vm, ECCLES_WEB_TRAP_HOST, 0); THROWRET("RuntimeError", "WebAssembly trap"); }
    return ret.type == ECCLES_WEB_V_NUM || ret.type == ECCLES_WEB_V_BOOL ? wv_num(ret.num) : WV_UNDEF;
}
static int wb_memslot(eccles_web_vm_t *vm, wv self) { int f; wv s = wb_is_obj_type(vm, self, H_OBJ) ? wb_obj_get(vm, WV_PAY(self), A_wasmSlot, &f) : WV_UNDEF; int slot = WV_ISNUM(s) ? (int)wv_dbl(s) : -1; (void)f; return slot >= 0 && slot < ECCLES_WEB_MAX_WASM && vm->wasm[slot].used ? slot : -1; }
NAT(nat_wasmmem_read) {
    int slot = wb_memslot(vm, self); double ad = wb_tonum(vm, ARG(0)), ln = wb_tonum(vm, ARG(1)); uint32_t len, off; wv s;
    if (slot < 0 || !g_wasm || !g_wasm->mem_read) THROWRET("Error", "no WebAssembly memory");
    if (ad < 0 || ln < 0 || ln > 60000) THROWRET("RangeError", "bad memory range");
    len = (uint32_t)ln; if (!len) return wv_atom(0);
    off = wb_galloc(vm, H_STR, 4 + len + 1); if (!off) return WV_UNDEF;
    ((wb_hstr_t *)(vm->jheap + off))->len = len; ((wb_hstr_t *)(vm->jheap + off))->s[len] = 0;
    if (g_wasm->mem_read(vm->wasm[slot].inst, (uint32_t)ad, ((wb_hstr_t *)(vm->jheap + off))->s, len) != WB_OK) THROWRET("RangeError", "memory access out of bounds");
    s = wv_obj(off); return s;
}
NAT(nat_wasmmem_write) {
    int slot = wb_memslot(vm, self); double ad = wb_tonum(vm, ARG(0)); uint32_t len; const char *p;
    if (slot < 0 || !g_wasm || !g_wasm->mem_write) THROWRET("Error", "no WebAssembly memory");
    if (!wb_is_str(vm, ARG(1))) THROWRET("TypeError", "write needs a string");
    p = wb_str_ptr(vm, a[1], &len);
    if (ad < 0 || g_wasm->mem_write(vm->wasm[slot].inst, (uint32_t)ad, p, len) != WB_OK) THROWRET("RangeError", "memory access out of bounds");
    return wv_num(len);
}
static void wb_wasm_do_instantiate(eccles_web_vm_t *vm, wv prom, wv bytes, wv imports) {
    int slot, i; void *inst; char err[ECCLES_WEB_MAX_NAME + 1]; uint32_t len, nexp; const uint8_t *p; wv result, inst_o, exports, m;
    wb_root(vm, prom); wb_root(vm, bytes); wb_root(vm, imports);
#define W_P vm->jroots[vm->njroots - 3]
#define W_B vm->jroots[vm->njroots - 2]
#define W_I vm->jroots[vm->njroots - 1]
    if (!g_wasm || !g_wasm->instantiate || !g_wasm->call) { wb_reject_msg(vm, W_P, "TypeError", "WebAssembly is not available on this device"); goto done; }
    if (!(vm->perms & ECCLES_WEB_PERM_HOST)) { wb_deny(vm, "WebAssembly"); wb_reject_msg(vm, W_P, "TypeError", "WebAssembly is not allowed for this page"); goto done; }
    if (!wb_is_str(vm, W_B)) { wb_reject_msg(vm, W_P, "TypeError", "WebAssembly.instantiate needs the module bytes"); goto done; }
    for (slot = 0; slot < ECCLES_WEB_MAX_WASM && vm->wasm[slot].used; slot++) {}
    if (slot >= ECCLES_WEB_MAX_WASM) { wb_reject_msg(vm, W_P, "RangeError", "too many WebAssembly instances (ECCLES_WEB_MAX_WASM)"); goto done; }
    p = (const uint8_t *)wb_str_ptr(vm, W_B, &len); err[0] = 0;
    inst = g_wasm->instantiate(vm, p, len, err, sizeof err);
    if (!inst) { wb_reject_msg(vm, W_P, "CompileError", err[0] ? err : "WebAssembly.instantiate failed"); goto done; }
    vm->wasm[slot].used = 1; vm->wasm[slot].inst = inst; vm->wasm[slot].imports = W_I;
    exports = wb_obj_new(vm, 0); if (vm->jexc_pending) goto fail_oom;
    wb_root(vm, exports);
    nexp = g_wasm->export_count ? g_wasm->export_count(inst) : 0;
    for (i = 0; i < (int)nexp && !vm->jexc_pending; i++) {
        int kind = 0; const char *nm = g_wasm->export_name ? g_wasm->export_name(inst, (uint32_t)i, &kind) : NULL; uint16_t at; wv f;
        if (!nm) continue;
        at = wb_atom(vm, nm, (uint32_t)strlen(nm)); if (!at) { vm->err = ECCLES_WEB_OK; continue; }
        if (kind == 1) {
            f = wb_obj_new(vm, 0); if (vm->jexc_pending) break;
            ((wb_hh_t *)(vm->jheap + WV_PAY(f)))->aux = (uint16_t)(K_WASMMEM << 8);
            wb_root(vm, f);
            wb_obj_set(vm, WV_PAY(f), A_wasmSlot, wv_num(slot));
            wb_obj_set(vm, WV_PAY(f), A_byteLength, wv_num(g_wasm->mem_size ? g_wasm->mem_size(inst) : 0));
            wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 2]), at, vm->jroots[vm->njroots - 1]);
            wb_unroot(vm, 1);
        } else {
            f = wb_make_bound(vm, nat_wasm_call, wv_num(slot), wv_num(at)); if (vm->jexc_pending) break;
            wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 1]), at, f);
        }
    }
    if (vm->jexc_pending) { wb_unroot(vm, 1); goto fail_oom; }
    inst_o = wb_obj_new(vm, 0); if (vm->jexc_pending) { wb_unroot(vm, 1); goto fail_oom; }
    wb_root(vm, inst_o);
    wb_obj_set(vm, WV_PAY(inst_o), A_exports, vm->jroots[vm->njroots - 2]);
    result = wb_obj_new(vm, 0); if (vm->jexc_pending) { wb_unroot(vm, 2); goto fail_oom; }
    wb_root(vm, result);
    wb_obj_set(vm, WV_PAY(result), A_instance, vm->jroots[vm->njroots - 2]);
    m = wb_obj_new(vm, 0); if (!vm->jexc_pending) wb_obj_set(vm, WV_PAY(vm->jroots[vm->njroots - 1]), A_module, m);
    wb_promise_resolve(vm, vm->jroots[vm->njroots - 6], vm->jroots[vm->njroots - 1]);
    wb_unroot(vm, 3);
    goto done;
fail_oom:
    vm->jexc_pending = 0; wb_reject_msg(vm, W_P, "RangeError", "out of script heap while creating the instance");
done:
    wb_unroot(vm, 3);
#undef W_P
#undef W_B
#undef W_I
}
NAT(nat_wasm_from_response) {  //self = result promise, jbd1 = imports, a[0] = the Response
    int f; wv body = wb_is_obj_type(vm, ARG(0), H_OBJ) ? wb_obj_get(vm, WV_PAY(a[0]), A_bodyText, &f) : WV_UNDEF; (void)f;
    wb_wasm_do_instantiate(vm, self, body, vm->jbd1);
    return WV_UNDEF;
}
NAT(nat_wasm_fail) { wb_promise_reject(vm, self, ARG(0)); return WV_UNDEF; }
NAT(nat_wasm_instantiate) {
    wv src = ARG(0), imports = ARG(1), p;
    p = wb_promise_new(vm); if (vm->jexc_pending) return WV_UNDEF;
    wb_root(vm, p);
    if (wb_is_obj_type(vm, src, H_PROMISE)) {  //instantiateStreaming(fetch(...)): wait for the response first
        wv onf, onr;
        wb_root(vm, src); wb_root(vm, imports);
        onf = wb_make_bound(vm, nat_wasm_from_response, vm->jroots[vm->njroots - 3], vm->jroots[vm->njroots - 1]);
        if (!vm->jexc_pending) { wb_root(vm, onf); onr = wb_make_bound(vm, nat_wasm_fail, vm->jroots[vm->njroots - 4], WV_UNDEF);
            if (!vm->jexc_pending) wb_promise_react(vm, vm->jroots[vm->njroots - 4], onf, onr, WV_UNDEF);
            wb_unroot(vm, 1); }
        wb_unroot(vm, 2);
    } else {
        wv bytes = src;
        if (wb_is_obj_type(vm, src, H_OBJ)) { int f; bytes = wb_obj_get(vm, WV_PAY(src), A_bodyText, &f); }  //a Response was passed
        wb_wasm_do_instantiate(vm, p, bytes, imports);
    }
    p = vm->jroots[vm->njroots - 1]; wb_unroot(vm, 1);
    return vm->jexc_pending ? WV_UNDEF : p;
}

//running scripts, the microtask queue, async functions
static void wb_scr_report(eccles_web_vm_t *vm) {
    char msg[ECCLES_WEB_MAX_NAME + 1]; eccles_web_error_t code; char line[ECCLES_WEB_MAX_NAME + 16]; int ll;
    if (vm->jtrap) { code = (eccles_web_error_t)vm->jtrap; vm->jtrap = 0; snprintf(msg, sizeof msg, "%s", eccles_web_error_name(code)); vm->jexc_pending = 0; }
    else if (vm->jexc_pending) {
        wv e = vm->jexc; vm->jexc_pending = 0; wb_root(vm, e);
        wb_cstr_of(vm, e, msg, sizeof msg); if (vm->jexc_pending) { vm->jexc_pending = 0; snprintf(msg, sizeof msg, "error"); }
        wb_unroot(vm, 1); code = ECCLES_WEB_ERR_JS_THROW;
    } else return;
    if (vm->jserr_count++ == 0) { vm->jserr = code; vm->jserr_info = 0; snprintf(vm->jserr_str, sizeof vm->jserr_str, "%s", msg); }
    ll = snprintf(line, sizeof line, "Uncaught %s", msg); wb_log(vm, line, (uint32_t)(ll > 0 ? ll : 0));
}

static void wb_coro_resume(eccles_web_vm_t *vm, wv cor, wv value, int is_throw) {
    wb_hcoro_t *c = (wb_hcoro_t *)(vm->jheap + WV_PAY(cor)); uint32_t *tp = (uint32_t *)(c + 1); wv *sp = (wv *)(tp + c->ntry * 3); uint32_t base, i, nf0; wb_jframe_t *fr; int rc;
    if (vm->jnf >= ECCLES_WEB_JS_CALL_DEPTH || vm->jsp + c->nstack + 2 >= ECCLES_WEB_JS_STACK_SLOTS || vm->ntry + c->ntry > ECCLES_WEB_JS_MAX_TRY) {
        wb_reject_msg(vm, wv_obj(c->prom), "RangeError", "cannot resume the async function (stack limits)"); return;
    }
    base = vm->jsp;
    fr = &vm->jframes[vm->jnf++];
    fr->pc = vm->jcode + c->pc; fr->env = c->env; fr->thisv = c->thisv; fr->prom = wv_obj(c->prom); fr->base = base; fr->proto = (uint16_t)c->proto; fr->ctor = 0; fr->boundary = 1; fr->is_async = 1;
    memcpy(&vm->jstk[base], sp, (size_t)c->nstack * 8); vm->jsp = base + c->nstack;
    for (i = 0; i < c->ntry; i++) { wb_jtry_t *h = &vm->jtry[vm->ntry++]; h->pc = vm->jcode + tp[i * 3]; h->sp = base + tp[i * 3 + 1]; h->env = tp[i * 3 + 2]; h->frame = (uint8_t)(vm->jnf - 1); }
    nf0 = vm->jnf - 1;
    vm->jnative_depth++;
    if (is_throw) { wb_throw(vm, value); rc = wb_run(vm, nf0, 1); }
    else { vm->jstk[vm->jsp++] = value; rc = wb_run(vm, nf0, 0); }
    vm->jnative_depth--;
    vm->jsp = base;
    (void)rc;
    if (vm->jtrap || vm->jexc_pending) wb_scr_report(vm);
}

static void wb_run_job(eccles_web_vm_t *vm, uint8_t kind, uint8_t flag, wv a, wv b, wv c) {
    if (kind == MT_QUEUE) { wb_turn_call(vm, a, WV_UNDEF, NULL, 0); return; }
    if (wb_is_obj_type(vm, a, H_CORO)) { wb_coro_resume(vm, a, b, flag); return; }
    if (wb_is_callable(vm, a)) {
        wv r = wb_call(vm, a, WV_UNDEF, &b, 1);
        if (vm->jtrap) { wb_scr_report(vm); return; }
        if (vm->jexc_pending) { wv e = vm->jexc; vm->jexc_pending = 0; if (c != WV_UNDEF) wb_promise_reject(vm, c, e); else { vm->jexc = e; vm->jexc_pending = 1; wb_scr_report(vm); } return; }
        if (c != WV_UNDEF) wb_promise_resolve(vm, c, r);
    } else if (c != WV_UNDEF) wb_promise_settle(vm, c, flag ? 2 : 1, b);  //no handler: the result passes through
}
static void wb_drain(eccles_web_vm_t *vm) {
    uint32_t guard = 20000;
    if (vm->mt_draining || vm->jscripting || vm->jnative_depth || !vm->jheap) return;
    vm->mt_draining = 1;
    while (vm->mt_len && guard--) {
        uint32_t i = vm->mt_head; uint8_t kind = vm->mt[i].kind, flag = vm->mt[i].flag; wv a = vm->mt[i].a, b = vm->mt[i].b, c = vm->mt[i].c;
        vm->mt_head = (uint8_t)((vm->mt_head + 1) % ECCLES_WEB_JS_MAX_MICROTASKS); vm->mt_len--;
        wb_root(vm, a); wb_root(vm, b); wb_root(vm, c);
        vm->fuel_left = vm->fuel_budget;
        wb_run_job(vm, kind, flag, a, b, c);
        wb_unroot(vm, 3);
        if (vm->jtrap || vm->jexc_pending) wb_scr_report(vm);
    }
    vm->mt_draining = 0;
}

static void wb_gc_roots(eccles_web_vm_t *vm, wb_gcs_t *g) {
    uint32_t i;
    for (i = 0; i < ECCLES_WEB_MAX_TIMERS; i++) if (vm->timers[i].used) wb_gc_mark_v(vm, g, vm->timers[i].fn);
    for (i = 0; i < vm->nlisteners; i++) wb_gc_mark_v(vm, g, vm->listeners[i].fn);
    for (i = 0; i < vm->mt_len; i++) { uint32_t k = (vm->mt_head + i) % ECCLES_WEB_JS_MAX_MICROTASKS; wb_gc_mark_v(vm, g, vm->mt[k].a); wb_gc_mark_v(vm, g, vm->mt[k].b); wb_gc_mark_v(vm, g, vm->mt[k].c); }
    for (i = 0; i < ECCLES_WEB_MAX_FETCH; i++) if (vm->fetches[i].id) wb_gc_mark_v(vm, g, vm->fetches[i].prom);
    for (i = 0; i < ECCLES_WEB_MAX_WASM; i++) if (vm->wasm[i].used) wb_gc_mark_v(vm, g, vm->wasm[i].imports);
    wb_gc_mark_v(vm, g, vm->jbd1);
}

//scripts
static void wb_exec_proto(eccles_web_vm_t *vm, int idx) {
    uint32_t off, base; wb_hfunc_t *f;
    off = wb_galloc(vm, H_FUNC, sizeof(wb_hfunc_t) - sizeof(wb_hh_t));
    if (!off) { wb_scr_report(vm); return; }
    f = (wb_hfunc_t *)(vm->jheap + off); f->proto = (uint32_t)idx; f->thisv = DX(DX_WIN, 0); f->h.aux = vm->protos[idx].flags;
    base = vm->jsp; vm->jstk[vm->jsp++] = wv_obj(off); vm->jstk[vm->jsp++] = DX(DX_WIN, 0);
    wb_run_nested(vm, base, 0);
    if (vm->jtrap || vm->jexc_pending) wb_scr_report(vm);
}
static void wb_run_script(eccles_web_vm_t *vm, const char *src, uint32_t len) {
    int idx;
    if (!(vm->perms & ECCLES_WEB_PERM_JS)) { wb_deny(vm, "script"); return; }
    if (!vm->jheap) return;
    idx = wb_compile_script(vm, src, len);
    if (idx < 0) return;
    wb_js_begin(vm);
    wb_exec_proto(vm, idx);
    wb_js_end(vm);
    wb_drain(vm);
}
static void wb_load_script_url(eccles_web_vm_t *vm, const char *href) {
    char abs[ECCLES_WEB_MAX_URL]; wb_src_t s; uint32_t cap, got = 0; uint8_t *buf; int n;
    if (!(vm->perms & ECCLES_WEB_PERM_JS)) { wb_deny(vm, "script"); return; }
    wb_url_resolve(vm->url, href, abs, sizeof abs);
    if (wb_url_kind(abs) == 2 && !(vm->perms & ECCLES_WEB_PERM_NET)) { wb_deny(vm, "net"); return; }
    if (!wb_open(vm, abs, &s, 1)) { vm->err = ECCLES_WEB_OK; if (vm->jserr_count++ == 0) { vm->jserr = ECCLES_WEB_ERR_IO; snprintf(vm->jserr_str, sizeof vm->jserr_str, "script not loaded"); } return; }
    buf = wb_scratch(vm, &cap);
    while (got < cap && (n = wb_src_read(&s, buf + got, cap - got)) > 0) got += (uint32_t)n;
    wb_src_close(&s);
    if (got >= cap) { if (vm->jserr_count++ == 0) { vm->jserr = ECCLES_WEB_ERR_NO_MEMORY; snprintf(vm->jserr_str, sizeof vm->jserr_str, "script larger than free arena space"); } return; }
    wb_run_script(vm, (const char *)buf, got);
}
static void wb_js_eval(eccles_web_vm_t *vm, const char *src) { wb_run_script(vm, src, (uint32_t)strlen(src)); }

//the api the host uses to answer async calls
static void wb_after_host(eccles_web_vm_t *vm) { if (!vm->jscripting) wb_drain(vm); }
eccles_web_status_t eccles_web_vm_async_resolve(eccles_web_vm_t *vm, uint32_t call_id, const eccles_web_val_t *value) {
    int slot; wv v, p;
    if (!vm || !vm->jheap) return WB_BAD;
    slot = wb_pending_find(vm, call_id, 0); if (slot < 0) return WB_BAD;
    p = vm->fetches[slot].prom; vm->fetches[slot].id = 0; wb_root(vm, p);
    v = value ? wb_from_val(vm, value) : WV_UNDEF;
    if (vm->jexc_pending) { vm->jexc_pending = 0; v = WV_UNDEF; }
    wb_promise_resolve(vm, vm->jroots[vm->njroots - 1], v); wb_unroot(vm, 1);
    wb_after_host(vm);
    return WB_OK;
}
eccles_web_status_t eccles_web_vm_async_reject(eccles_web_vm_t *vm, uint32_t call_id, const char *message) {
    int slot;
    if (!vm || !vm->jheap) return WB_BAD;
    slot = wb_pending_find(vm, call_id, 0); if (slot < 0) return WB_BAD;
    { wv p = vm->fetches[slot].prom; vm->fetches[slot].id = 0; wb_reject_msg(vm, p, "Error", message ? message : "host call failed"); }
    wb_after_host(vm);
    return WB_OK;
}
eccles_web_status_t eccles_web_vm_fetch_respond(eccles_web_vm_t *vm, uint32_t id, uint32_t status, const char *headers, const void *body, size_t len) {
    int slot; wv p, resp;
    (void)headers;
    if (!vm || !vm->jheap) return WB_BAD;
    slot = wb_pending_find(vm, id, 1); if (slot < 0) return WB_BAD;
    p = vm->fetches[slot].prom; vm->fetches[slot].id = 0; wb_root(vm, p);
    resp = wb_make_response(vm, status, body, len, "");
    if (vm->jexc_pending) { vm->jexc_pending = 0; wb_reject_msg(vm, vm->jroots[vm->njroots - 1], "RangeError", "response too large for the script heap (ECCLES_WEB_JS_HEAP_SIZE)"); }
    else wb_promise_resolve(vm, vm->jroots[vm->njroots - 1], resp);
    wb_unroot(vm, 1);
    wb_after_host(vm);
    return WB_OK;
}
eccles_web_status_t eccles_web_vm_fetch_fail(eccles_web_vm_t *vm, uint32_t id, const char *message) {
    int slot;
    if (!vm || !vm->jheap) return WB_BAD;
    slot = wb_pending_find(vm, id, 1); if (slot < 0) return WB_BAD;
    { wv p = vm->fetches[slot].prom; vm->fetches[slot].id = 0; wb_reject_msg(vm, p, "TypeError", message && message[0] ? message : "Failed to fetch"); }
    wb_after_host(vm);
    return WB_OK;
}
eccles_web_status_t eccles_web_vm_wasm_call_import(eccles_web_vm_t *vm, void *inst, const char *module, const char *name, const eccles_web_val_t *args, uint32_t nargs, eccles_web_val_t *ret) {
    int slot, found; wv imp, m, f, av[ECCLES_WEB_JS_MAX_ARGS], r; uint32_t i; uint16_t am, an;
    if (!vm || !vm->jheap || !module || !name) return WB_BAD;
    for (slot = 0; slot < ECCLES_WEB_MAX_WASM && !(vm->wasm[slot].used && vm->wasm[slot].inst == inst); slot++) {}
    if (slot >= ECCLES_WEB_MAX_WASM) return WB_BAD;
    imp = vm->wasm[slot].imports;
    if (!wb_is_obj_type(vm, imp, H_OBJ)) return WB_BAD;
    am = wb_atom_find(vm, module, (uint32_t)strlen(module)); an = wb_atom_find(vm, name, (uint32_t)strlen(name));
    if (!am || !an) return WB_BAD;
    m = wb_obj_get(vm, WV_PAY(imp), am, &found); if (!found || !wb_is_obj_type(vm, m, H_OBJ)) return WB_BAD;
    f = wb_obj_get(vm, WV_PAY(m), an, &found); if (!found || !wb_is_callable(vm, f)) return WB_BAD;
    if (nargs > ECCLES_WEB_JS_MAX_ARGS) nargs = ECCLES_WEB_JS_MAX_ARGS;
    for (i = 0; i < nargs; i++) av[i] = wb_from_val(vm, &args[i]);
    wb_root(vm, f);
    r = wb_call(vm, vm->jroots[vm->njroots - 1], WV_UNDEF, av, (int)nargs);
    wb_unroot(vm, 1);
    if (vm->jexc_pending || vm->jtrap) { wb_scr_report(vm); return WB_BAD; }
    if (ret) wb_to_val(vm, r, ret);
    return WB_OK;
}

//more global functions
NAT(nat_alert) { wb_console_line(vm, a, n < 1 ? 0 : 1, "alert: "); return WV_UNDEF; }
NAT(nat_confirm) { wb_console_line(vm, a, n < 1 ? 0 : 1, "confirm: "); return WV_FALSE; }
NAT(nat_prompt) { wb_console_line(vm, a, n < 1 ? 0 : 1, "prompt: "); return WV_NULL; }
NAT(nat_el_click) { uint16_t nd = wb_self_node(vm, self); if (nd != 0xFFFEu) wb_js_dispatch(vm, nd, A_click); return WV_UNDEF; }
NAT(nat_encodeURIComponent) {
    char in[256], out[780]; uint32_t l = wb_cstr_of(vm, ARG(0), in, sizeof in), i, o = 0;
    if (vm->jexc_pending) return WV_UNDEF;
    for (i = 0; i < l && o + 4 < sizeof out; i++) {
        uint8_t c = (uint8_t)in[i];
        if (isalnum_c(c) || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')') out[o++] = (char)c;
        else { static const char hx[] = "0123456789ABCDEF"; out[o++] = '%'; out[o++] = hx[c >> 4]; out[o++] = hx[c & 15]; }
    }
    return wb_str_new(vm, out, o);
}
NAT(nat_decodeURIComponent) {
    char in[780], out[260]; uint32_t l = wb_cstr_of(vm, ARG(0), in, sizeof in), i, o = 0;
    if (vm->jexc_pending) return WV_UNDEF;
    for (i = 0; i < l && o + 1 < sizeof out; i++) {
        if (in[i] == '%' && i + 2 < l + 0 + 1 && wb_hex((uint8_t)in[i + 1]) >= 0 && wb_hex((uint8_t)in[i + 2]) >= 0) { out[o++] = (char)(wb_hex((uint8_t)in[i + 1]) * 16 + wb_hex((uint8_t)in[i + 2])); i += 2; }
        else out[o++] = in[i];
    }
    return wb_str_new(vm, out, o);
}

//the builtin table. entries of one kind are kept together (the lookup scans only the range of its kind). fn == NULL means a constant
typedef struct { uint16_t atom; uint8_t kind; wb_nat_fn fn; double c; } wb_nat_t;
#define T(kind, name, fn) { name, kind, fn, 0 },
#define C(kind, name, val) { name, kind, NULL, val },
static const wb_nat_t wb_nats[] = {
    //internal functions that bound function objects point at (not reachable by name)
    T(K_NONE, A_NONE, nat_prom_resolve_fn) T(K_NONE, A_NONE, nat_prom_reject_fn) T(K_NONE, A_NONE, nat_finally_ok) T(K_NONE, A_NONE, nat_finally_fail)
    T(K_NONE, A_NONE, nat_all_ok) T(K_NONE, A_NONE, nat_all_fail) T(K_NONE, A_NONE, nat_race_ok) T(K_NONE, A_NONE, nat_settled_ok) T(K_NONE, A_NONE, nat_settled_fail)
    T(K_NONE, A_NONE, nat_wasm_from_response) T(K_NONE, A_NONE, nat_wasm_fail) T(K_NONE, A_NONE, nat_wasm_call) T(K_NONE, A_NONE, nat_bind_call)
    //globals
    T(K_GLOBAL, A_parseInt, nat_parseInt) T(K_GLOBAL, A_parseFloat, nat_parseFloat) T(K_GLOBAL, A_isNaN, nat_isNaN) T(K_GLOBAL, A_isFinite, nat_isFinite)
    T(K_GLOBAL, A_String, nat_string_ctor) T(K_GLOBAL, A_Number, nat_number_ctor) T(K_GLOBAL, A_Boolean, nat_boolean_ctor) T(K_GLOBAL, A_Object, nat_object_ctor)
    T(K_GLOBAL, A_Array, nat_array_ctor) T(K_GLOBAL, A_Error, nat_error_ctor) T(K_GLOBAL, A_TypeError, nat_typeerror_ctor) T(K_GLOBAL, A_RangeError, nat_rangeerror_ctor)
    T(K_GLOBAL, A_SyntaxError, nat_syntaxerror_ctor) T(K_GLOBAL, A_ReferenceError, nat_referenceerror_ctor) T(K_GLOBAL, A_Promise, nat_promise_ctor) T(K_GLOBAL, A_Date, nat_date_ctor)
    T(K_GLOBAL, A_setTimeout, nat_setTimeout) T(K_GLOBAL, A_setInterval, nat_setInterval) T(K_GLOBAL, A_clearTimeout, nat_clearTimeout) T(K_GLOBAL, A_clearInterval, nat_clearTimeout)
    T(K_GLOBAL, A_requestAnimationFrame, nat_requestAnimationFrame) T(K_GLOBAL, A_cancelAnimationFrame, nat_clearTimeout) T(K_GLOBAL, A_queueMicrotask, nat_queueMicrotask)
    T(K_GLOBAL, A_fetch, nat_fetch) T(K_GLOBAL, A_alert, nat_alert) T(K_GLOBAL, A_confirm, nat_confirm) T(K_GLOBAL, A_prompt, nat_prompt)
    T(K_GLOBAL, A_addEventListener, nat_addEventListener) T(K_GLOBAL, A_removeEventListener, nat_removeEventListener) T(K_GLOBAL, A_getComputedStyle, nat_win_getComputedStyle)
    T(K_GLOBAL, A_scrollTo, nat_win_scrollTo) T(K_GLOBAL, A_scrollBy, nat_win_scrollBy) T(K_GLOBAL, A_encodeURIComponent, nat_encodeURIComponent) T(K_GLOBAL, A_decodeURIComponent, nat_decodeURIComponent)
    //objects with members
    T(K_MATH, A_floor, nat_math_floor) T(K_MATH, A_ceil, nat_math_ceil) T(K_MATH, A_round, nat_math_round) T(K_MATH, A_abs, nat_math_abs) T(K_MATH, A_min, nat_math_min) T(K_MATH, A_max, nat_math_max)
    T(K_MATH, A_random, nat_math_random) T(K_MATH, A_sqrt, nat_math_sqrt) T(K_MATH, A_pow, nat_math_pow) T(K_MATH, A_sin, nat_math_sin) T(K_MATH, A_cos, nat_math_cos) T(K_MATH, A_tan, nat_math_tan)
    T(K_MATH, A_atan, nat_math_atan) T(K_MATH, A_asin, nat_math_asin) T(K_MATH, A_acos, nat_math_acos) T(K_MATH, A_atan2, nat_math_atan2) T(K_MATH, A_log, nat_math_log) T(K_MATH, A_log2, nat_math_log2)
    T(K_MATH, A_log10, nat_math_log10) T(K_MATH, A_exp, nat_math_exp) T(K_MATH, A_trunc, nat_math_trunc) T(K_MATH, A_sign, nat_math_sign) T(K_MATH, A_cbrt, nat_math_cbrt) T(K_MATH, A_hypot, nat_math_hypot)
    C(K_MATH, A_PI, 3.14159265358979323846) C(K_MATH, A_E, 2.71828182845904523536) C(K_MATH, A_LN2, 0.69314718055994530942) C(K_MATH, A_SQRT2, 1.41421356237309504880)
    T(K_JSON, A_stringify, nat_json_stringify) T(K_JSON, A_parse, nat_json_parse)
    T(K_CONSOLE, A_log, nat_console_log) T(K_CONSOLE, A_info, nat_console_log) T(K_CONSOLE, A_debug, nat_console_log) T(K_CONSOLE, A_warn, nat_console_warn) T(K_CONSOLE, A_error, nat_console_error)
    T(K_WASM, A_instantiate, nat_wasm_instantiate) T(K_WASM, A_instantiateStreaming, nat_wasm_instantiate)
    T(K_STORAGE, A_getItem, nat_store_getItem) T(K_STORAGE, A_setItem, nat_store_setItem) T(K_STORAGE, A_removeItem, nat_store_removeItem) T(K_STORAGE, A_clear, nat_store_clear)
    //statics of the constructors
    T(K_OBJECT_S, A_keys, nat_object_keys) T(K_OBJECT_S, A_values, nat_object_values) T(K_OBJECT_S, A_entries, nat_object_entries) T(K_OBJECT_S, A_assign, nat_object_assign)
    T(K_OBJECT_S, A_freeze, nat_object_freeze) T(K_OBJECT_S, A_create, nat_object_create) T(K_OBJECT_S, A_fromEntries, nat_object_fromEntries) T(K_OBJECT_S, A_defineProperty, nat_object_defineProperty)
    T(K_ARRAY_S, A_isArray, nat_array_isArray) T(K_ARRAY_S, A_from, nat_array_from) T(K_ARRAY_S, A_of, nat_array_of)
    T(K_STRING_S, A_fromCharCode, nat_string_fromCharCode)
    T(K_NUMBER_S, A_isInteger, nat_number_isInteger) T(K_NUMBER_S, A_isFinite, nat_number_isFinite) T(K_NUMBER_S, A_isNaN, nat_number_isNaN) T(K_NUMBER_S, A_parseFloat, nat_parseFloat) T(K_NUMBER_S, A_parseInt, nat_parseInt)
    C(K_NUMBER_S, A_MAX_SAFE_INTEGER, 9007199254740991.0) C(K_NUMBER_S, A_MIN_SAFE_INTEGER, -9007199254740991.0) C(K_NUMBER_S, A_EPSILON, 2.220446049250313e-16)
    C(K_NUMBER_S, A_MAX_VALUE, 1.7976931348623157e308) C(K_NUMBER_S, A_MIN_VALUE, 5e-324) C(K_NUMBER_S, A_POSITIVE_INFINITY, HUGE_VAL) C(K_NUMBER_S, A_NEGATIVE_INFINITY, -HUGE_VAL)
    T(K_PROMISE_S, A_resolve, nat_promise_resolve) T(K_PROMISE_S, A_reject, nat_promise_reject) T(K_PROMISE_S, A_all, nat_promise_all) T(K_PROMISE_S, A_race, nat_promise_race) T(K_PROMISE_S, A_allSettled, nat_promise_allSettled)
    T(K_DATE_S, A_now, nat_date_now) T(K_DATE_S, A_getTime, nat_date_getTime) T(K_DATE_S, A_valueOf, nat_date_getTime) T(K_DATE_S, A_getFullYear, nat_date_getFullYear) T(K_DATE_S, A_getMonth, nat_date_getMonth)
    T(K_DATE_S, A_getDate, nat_date_getDate) T(K_DATE_S, A_getDay, nat_date_getDay) T(K_DATE_S, A_getHours, nat_date_getHours) T(K_DATE_S, A_getMinutes, nat_date_getMinutes) T(K_DATE_S, A_getSeconds, nat_date_getSeconds)
    T(K_DATE_S, A_toISOString, nat_date_toISOString)
    //methods of values
    T(K_OBJ, A_hasOwnProperty, nat_obj_hasOwnProperty)
    T(K_FUNC, A_call, nat_func_call) T(K_FUNC, A_apply, nat_func_apply) T(K_FUNC, A_bind, nat_func_bind)
    T(K_PROMISE, A_then, nat_promise_then) T(K_PROMISE, A_catch, nat_promise_catch) T(K_PROMISE, A_finally, nat_promise_finally)
    T(K_RESPONSE, A_text, nat_resp_text) T(K_RESPONSE, A_json, nat_resp_json) T(K_RESPONSE, A_arrayBuffer, nat_resp_text)
    T(K_EVENT, A_preventDefault, nat_event_preventDefault) T(K_EVENT, A_stopPropagation, nat_event_stopPropagation) T(K_EVENT, A_stopImmediatePropagation, nat_event_stopPropagation)
    T(K_WASMMEM, A_read, nat_wasmmem_read) T(K_WASMMEM, A_write, nat_wasmmem_write)
    T(K_ARR, A_push, nat_arr_push) T(K_ARR, A_pop, nat_arr_pop) T(K_ARR, A_shift, nat_arr_shift) T(K_ARR, A_unshift, nat_arr_unshift) T(K_ARR, A_slice, nat_arr_slice) T(K_ARR, A_splice, nat_arr_splice)
    T(K_ARR, A_indexOf, nat_arr_indexOf) T(K_ARR, A_lastIndexOf, nat_arr_lastIndexOf) T(K_ARR, A_includes, nat_arr_includes) T(K_ARR, A_join, nat_arr_join) T(K_ARR, A_concat, nat_arr_concat)
    T(K_ARR, A_forEach, nat_arr_forEach) T(K_ARR, A_map, nat_arr_map) T(K_ARR, A_filter, nat_arr_filter) T(K_ARR, A_reduce, nat_arr_reduce) T(K_ARR, A_reverse, nat_arr_reverse) T(K_ARR, A_sort, nat_arr_sort)
    T(K_ARR, A_find, nat_arr_find) T(K_ARR, A_findIndex, nat_arr_findIndex) T(K_ARR, A_some, nat_arr_some) T(K_ARR, A_every, nat_arr_every) T(K_ARR, A_fill, nat_arr_fill) T(K_ARR, A_at, nat_arr_at) T(K_ARR, A_toString, nat_arr_join)
    T(K_STR, A_charAt, nat_str_charAt) T(K_STR, A_charCodeAt, nat_str_charCodeAt) T(K_STR, A_codePointAt, nat_str_charCodeAt) T(K_STR, A_indexOf, nat_str_indexOf) T(K_STR, A_lastIndexOf, nat_str_lastIndexOf)
    T(K_STR, A_includes, nat_str_includes) T(K_STR, A_startsWith, nat_str_startsWith) T(K_STR, A_endsWith, nat_str_endsWith) T(K_STR, A_slice, nat_str_slice) T(K_STR, A_substring, nat_str_substring)
    T(K_STR, A_substr, nat_str_substr) T(K_STR, A_toUpperCase, nat_str_toUpperCase) T(K_STR, A_toLowerCase, nat_str_toLowerCase) T(K_STR, A_trim, nat_str_trim) T(K_STR, A_trimStart, nat_str_trimStart)
    T(K_STR, A_trimEnd, nat_str_trimEnd) T(K_STR, A_split, nat_str_split) T(K_STR, A_replace, nat_str_replace) T(K_STR, A_replaceAll, nat_str_replaceAll) T(K_STR, A_repeat, nat_str_repeat)
    T(K_STR, A_padStart, nat_str_padStart) T(K_STR, A_padEnd, nat_str_padEnd) T(K_STR, A_concat, nat_str_concat) T(K_STR, A_at, nat_str_at) T(K_STR, A_localeCompare, nat_str_localeCompare)
    T(K_STR, A_toString, nat_str_toString) T(K_STR, A_valueOf, nat_str_toString)
    T(K_NUM, A_toFixed, nat_num_toFixed) T(K_NUM, A_toString, nat_num_toString) T(K_NUM, A_toLocaleString, nat_num_toString)
    T(K_ELEM, A_getAttribute, nat_el_getAttribute) T(K_ELEM, A_setAttribute, nat_el_setAttribute) T(K_ELEM, A_removeAttribute, nat_el_removeAttribute) T(K_ELEM, A_hasAttribute, nat_el_hasAttribute)
    T(K_ELEM, A_appendChild, nat_el_appendChild) T(K_ELEM, A_insertBefore, nat_el_insertBefore) T(K_ELEM, A_removeChild, nat_el_removeChild) T(K_ELEM, A_remove, nat_el_remove)
    T(K_ELEM, A_append, nat_el_append) T(K_ELEM, A_prepend, nat_el_prepend) T(K_ELEM, A_contains, nat_el_contains) T(K_ELEM, A_querySelector, nat_el_querySelector) T(K_ELEM, A_querySelectorAll, nat_el_querySelectorAll)
    T(K_ELEM, A_matches, nat_el_matches) T(K_ELEM, A_closest, nat_el_closest) T(K_ELEM, A_getElementsByTagName, nat_el_getElementsByTagName) T(K_ELEM, A_getElementsByClassName, nat_el_getElementsByClassName)
    T(K_ELEM, A_addEventListener, nat_addEventListener) T(K_ELEM, A_removeEventListener, nat_removeEventListener) T(K_ELEM, A_focus, nat_el_focus) T(K_ELEM, A_blur, nat_el_blur) T(K_ELEM, A_click, nat_el_click)
    T(K_ELEM, A_scrollIntoView, nat_el_scrollIntoView) T(K_ELEM, A_getBoundingClientRect, nat_el_getBoundingClientRect)
    T(K_DOCUMENT, A_getElementById, nat_doc_getElementById) T(K_DOCUMENT, A_querySelector, nat_el_querySelector) T(K_DOCUMENT, A_querySelectorAll, nat_el_querySelectorAll)
    T(K_DOCUMENT, A_createElement, nat_doc_createElement) T(K_DOCUMENT, A_createTextNode, nat_doc_createTextNode) T(K_DOCUMENT, A_addEventListener, nat_addEventListener)
    T(K_DOCUMENT, A_removeEventListener, nat_removeEventListener) T(K_DOCUMENT, A_getElementsByTagName, nat_el_getElementsByTagName) T(K_DOCUMENT, A_getElementsByClassName, nat_el_getElementsByClassName)
    T(K_WINDOW, A_addEventListener, nat_addEventListener) T(K_WINDOW, A_removeEventListener, nat_removeEventListener) T(K_WINDOW, A_scrollTo, nat_win_scrollTo) T(K_WINDOW, A_scrollBy, nat_win_scrollBy)
    T(K_WINDOW, A_getComputedStyle, nat_win_getComputedStyle)
    T(K_STYLE, A_setProperty, nat_style_setProperty) T(K_STYLE, A_getPropertyValue, nat_style_getPropertyValue) T(K_STYLE, A_removeProperty, nat_style_removeProperty)
    T(K_CLASSLIST, A_add, nat_cl_add) T(K_CLASSLIST, A_remove, nat_cl_remove) T(K_CLASSLIST, A_toggle, nat_cl_toggle) T(K_CLASSLIST, A_contains, nat_cl_contains)
    T(K_LOCATION, A_assign, nat_loc_assign) T(K_LOCATION, A_replace, nat_loc_assign) T(K_LOCATION, A_reload, nat_loc_reload)
    T(K_HISTORY, A_back, nat_hist_back) T(K_HISTORY, A_forward, nat_hist_noop) T(K_HISTORY, A_pushState, nat_hist_noop) T(K_HISTORY, A_replaceState, nat_hist_noop) T(K_HISTORY, A_go, nat_hist_noop)
};
#undef T
#undef C
#define WB_NNATS ((uint32_t)(sizeof wb_nats / sizeof wb_nats[0]))

static uint16_t wb_kfirst[K_COUNT], wb_kend[K_COUNT]; static uint8_t wb_kinit;
static void wb_nat_index(void) {
    uint32_t i; uint8_t k;
    if (wb_kinit) return;
    for (k = 0; k < K_COUNT; k++) { wb_kfirst[k] = 0; wb_kend[k] = 0; }
    for (i = 0; i < WB_NNATS; i++) { k = wb_nats[i].kind; if (wb_kend[k] == 0) wb_kfirst[k] = (uint16_t)i; wb_kend[k] = (uint16_t)(i + 1); }
    wb_kinit = 1;
}
static wv wb_nat_value(uint8_t kind, uint16_t atom) {
    uint32_t i;
    wb_nat_index();
    if (kind >= K_COUNT) return WV_NONE;
    for (i = wb_kfirst[kind]; i < wb_kend[kind]; i++) if (wb_nats[i].atom == atom) return wb_nats[i].fn ? WV_MK(WV_T_NAT, i) : wv_num(wb_nats[i].c);
    return WV_NONE;
}
static wv wb_call_native(eccles_web_vm_t *vm, uint32_t id, wv self, const wv *a, int n) {
    if (id >= WB_NNATS || !wb_nats[id].fn) { wb_type_error(vm, "not a function"); return WV_UNDEF; }
    return wb_nats[id].fn(vm, self, a, n);
}
static uint32_t wb_nat_id(wb_nat_fn fn) { uint32_t i; for (i = 0; i < WB_NNATS; i++) if (wb_nats[i].fn == fn) return i; return 0xFFFFFFFFu; }
//the constructors that have members (Array.isArray, Object.keys ...): the native function value stands for the constructor
static uint8_t wb_nat_statics(uint32_t id) {
    wb_nat_fn f; if (id >= WB_NNATS) return 0;
    f = wb_nats[id].fn;
    if (f == nat_object_ctor) return K_OBJECT_S;
    if (f == nat_array_ctor) return K_ARRAY_S;
    if (f == nat_string_ctor) return K_STRING_S;
    if (f == nat_number_ctor) return K_NUMBER_S;
    if (f == nat_promise_ctor) return K_PROMISE_S;
    if (f == nat_date_ctor) return K_DATE_S;
    return 0;
}
static int wb_nat_instanceof(eccles_web_vm_t *vm, wv a, uint32_t natid) {
    wb_nat_fn f; if (natid >= WB_NNATS) return 0;
    f = wb_nats[natid].fn;
    if (f == nat_array_ctor) return wb_is_arr(vm, a);
    if (f == nat_object_ctor) return WV_TAGOF(a) == WV_T_OBJ && !wb_is_str(vm, a);
    if (f == nat_promise_ctor) return wb_is_obj_type(vm, a, H_PROMISE);
    if (f == nat_string_ctor || f == nat_number_ctor || f == nat_boolean_ctor) return 0;
    if (f == nat_date_ctor) return wb_is_obj_type(vm, a, H_OBJ) && (((wb_hh_t *)(vm->jheap + WV_PAY(a)))->aux >> 8) == K_DATE_S;
    if (f == nat_error_ctor || f == nat_typeerror_ctor || f == nat_rangeerror_ctor || f == nat_syntaxerror_ctor || f == nat_referenceerror_ctor) {
        int found; wv nm;
        if (!wb_is_obj_type(vm, a, H_OBJ) || wb_obj_find(vm, WV_PAY(a), A_message) < 0) return 0;
        if (f == nat_error_ctor) return 1;
        nm = wb_obj_get(vm, WV_PAY(a), A_name, &found);
        return found && WV_TAGOF(nm) == WV_T_ATOM && wb_astr(vm, (uint16_t)WV_PAY(nm)) == wb_astr(vm, wb_nats[natid].atom);
    }
    return 0;
}

//page start: the global object and everything on it
static wv wb_new_kind_obj(eccles_web_vm_t *vm, uint8_t kind) {
    wv o = wb_obj_new(vm, 0); uint32_t i;
    if (vm->jexc_pending) return WV_UNDEF;
    wb_nat_index();
    ((wb_hh_t *)(vm->jheap + WV_PAY(o)))->aux = (uint16_t)(kind << 8);
    for (i = wb_kfirst[kind]; i < wb_kend[kind]; i++) if (wb_kend[kind]) wb_obj_set(vm, WV_PAY(o), wb_nats[i].atom, wb_nats[i].fn ? WV_MK(WV_T_NAT, i) : wv_num(wb_nats[i].c));
    return o;
}
static void wb_js_page_init(eccles_web_vm_t *vm) {
    uint32_t i; wv g, o; uint8_t k;
    wb_wasm_release_all(vm);
    memset(vm->fetches, 0, sizeof vm->fetches); vm->mt_head = vm->mt_len = vm->mt_draining = 0; vm->jtrap = 0; vm->jbd1 = WV_UNDEF; vm->event_stop = vm->event_prevent = 0; vm->new_call = 0;
    vm->fuel_left = vm->fuel_budget; vm->jglobal = WV_UNDEF; vm->jexc = WV_UNDEF;
    wb_heap_init(vm);
    g = wb_obj_new(vm, 0); if (vm->jexc_pending) { vm->jexc_pending = 0; return; }
    vm->jglobal = g;
    wb_nat_index();
    for (i = wb_kfirst[K_GLOBAL]; i < wb_kend[K_GLOBAL]; i++) wb_obj_set(vm, WV_PAY(g), wb_nats[i].atom, WV_MK(WV_T_NAT, i));
    wb_obj_set(vm, WV_PAY(g), A_NaN, wv_num(NAN)); wb_obj_set(vm, WV_PAY(g), A_Infinity, wv_num(HUGE_VAL));
    { static const struct { uint16_t name; uint8_t kind; } objs[] = { {A_Math, K_MATH}, {A_JSON, K_JSON}, {A_console, K_CONSOLE}, {A_WebAssembly, K_WASM}, {A_localStorage, K_STORAGE}, {A_sessionStorage, K_STORAGE} };
      for (k = 0; k < sizeof objs / sizeof objs[0]; k++) { o = wb_new_kind_obj(vm, objs[k].kind); if (vm->jexc_pending) { vm->jexc_pending = 0; return; } wb_obj_set(vm, WV_PAY(vm->jglobal), objs[k].name, o); } }
    wb_obj_set(vm, WV_PAY(g), A_window, DX(DX_WIN, 0)); wb_obj_set(vm, WV_PAY(g), A_self, DX(DX_WIN, 0)); wb_obj_set(vm, WV_PAY(g), A_globalThis, DX(DX_WIN, 0));
    wb_obj_set(vm, WV_PAY(g), A_document, DX(DX_DOC, 0)); wb_obj_set(vm, WV_PAY(g), A_location, DX(DX_LOC, 0)); wb_obj_set(vm, WV_PAY(g), A_history, DX(DX_HIST, 0)); wb_obj_set(vm, WV_PAY(g), A_navigator, DX(DX_NAV, 0));
    for (i = 0; i < vm->nhost; i++) { uint16_t at = wb_atom(vm, vm->host[i].name, (uint32_t)strlen(vm->host[i].name)); if (at) wb_obj_set(vm, WV_PAY(g), at, WV_MK(WV_T_NAT, 0x10000u + i)); }
    vm->jexc_pending = 0;
}

void eccles_web_set_fetch(eccles_web_fetch_fn fn) { g_fetch = fn; }
void eccles_web_set_wasm(const eccles_web_wasm_t *ops) { g_wasm = ops; }
eccles_web_status_t eccles_web_vm_register_async(eccles_web_vm_t *vm, const char *name, eccles_web_async_fn fn) {
    uint8_t i;
    if (!vm || !name || !fn || !name[0]) return WB_BAD;
    for (i = 0; i < vm->nhost; i++) if (!strcmp(vm->host[i].name, name)) { vm->host[i].fn = NULL; vm->host[i].afn = fn; return WB_OK; }
    if (vm->nhost >= ECCLES_WEB_MAX_HOST_FUNCS) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 7); return WB_BAD; }
    vm->host[vm->nhost].name = name; vm->host[vm->nhost].fn = NULL; vm->host[vm->nhost].afn = fn; vm->nhost++;
    return WB_OK;
}

#endif  //ECCLES_WEB_JS

//7. public runtime api

#if ECCLES_WEB_JS
static int  wb_js_dispatch(eccles_web_vm_t *vm, uint16_t node, uint16_t type);
static void wb_js_tick(eccles_web_vm_t *vm, uint32_t now);
static int  wb_js_next_timer(const eccles_web_vm_t *vm, uint32_t now, uint32_t *wait);
static void wb_js_eval(eccles_web_vm_t *vm, const char *src);
static void wb_wasm_release_all(eccles_web_vm_t *vm);
#endif

static void wb_err_clear(eccles_web_vm_t *vm) { vm->err = ECCLES_WEB_OK; vm->err_info = 0; vm->err_str[0] = 0; }
static wb_st wb_result(const eccles_web_vm_t *vm) { return vm->err == ECCLES_WEB_OK ? WB_OK : WB_BAD; }

//instance creation (same layout trick as eccles_wasm: the struct lives at the start of the memory it manages)
eccles_web_vm_t *eccles_web_vm_create(void *mem, size_t size) {
    uintptr_t a = ((uintptr_t)mem + 7u) & ~(uintptr_t)7u; eccles_web_vm_t *vm; size_t adj;
    if (!mem) return NULL;
    adj = (size_t)(a - (uintptr_t)mem);
    if (size < adj + sizeof(eccles_web_vm_t) + 4096u) return NULL;
    size -= adj;
    vm = (eccles_web_vm_t *)a; memset(vm, 0, sizeof *vm);
    vm->base = (uint8_t *)((a + sizeof *vm + 7u) & ~(uintptr_t)7u);
    vm->end = (uint8_t *)((a + size) & ~(uintptr_t)7u);
    vm->lo = vm->base; vm->hi = vm->hi_init = vm->end;
    vm->perms = ECCLES_WEB_DEFAULT_PRIVILEGE; vm->vw = 320; vm->vh = 480;
#if ECCLES_WEB_JS
    vm->fuel_budget = ECCLES_WEB_JS_FUEL;
#endif
    vm->inited = 1;
    return vm;
}
#if ECCLES_WEB_USE_HEAP
eccles_web_vm_t *eccles_web_vm_create_heap(size_t size) {
    void *m = malloc(size); eccles_web_vm_t *vm;
    if (!m) return NULL;
    vm = eccles_web_vm_create(m, size);
    if (!vm) { free(m); return NULL; }
    vm->heap_block = m;
    return vm;
}
#endif
void eccles_web_vm_destroy(eccles_web_vm_t *vm) {
    if (!vm) return;
    if (vm == g_default_vm) g_default_vm = NULL;
    if (vm->heap_block) { void *m = vm->heap_block; vm->heap_block = NULL; vm->inited = 0; free(m); }
    else vm->inited = 0;
}
void  eccles_web_vm_set_user(eccles_web_vm_t *vm, void *user) { if (vm) vm->user = user; }
void *eccles_web_vm_user(const eccles_web_vm_t *vm) { return vm ? vm->user : NULL; }

static void wb_ui_init(void);  //backends section
static eccles_web_vm_t *wb_def(void) {
    if (!g_default_vm) {
        wb_ui_init();
#if ECCLES_WEB_USE_HEAP
        g_default_vm = eccles_web_vm_create_heap(ECCLES_WEB_ARENA_SIZE);
#else
        g_default_vm = eccles_web_vm_create(g_arena, sizeof g_arena);
#endif
    }
    return g_default_vm;
}
eccles_web_vm_t *eccles_web_default_vm(void) { return wb_def(); }
eccles_web_status_t eccles_web_init(void) { return wb_def() ? WB_OK : WB_BAD; }
eccles_web_status_t eccles_web_init_with(void *mem, size_t size) {
    eccles_web_vm_t *old = g_default_vm, *vm;
    wb_ui_init();
    vm = eccles_web_vm_create(mem, size);
    if (!vm) return WB_BAD;
    if (old) { old->inited = 0; if (old->heap_block) { void *m = old->heap_block; old->heap_block = NULL; free(m); } }
    g_default_vm = vm;
    return WB_OK;
}

//loading
static void wb_hist_push(eccles_web_vm_t *vm) {
    if (!vm->loaded || !vm->url[0]) return;
    if (vm->nhist == ECCLES_WEB_MAX_HISTORY) { memmove(vm->hist[0], vm->hist[1], (size_t)(ECCLES_WEB_MAX_HISTORY - 1) * ECCLES_WEB_MAX_URL); vm->nhist--; }
    snprintf(vm->hist[vm->nhist++], ECCLES_WEB_MAX_URL, "%s", vm->url);
}

//the heart of every load: a fresh page, parse it from the reader, scripts and sheets run while it is read
static wb_st wb_load_reader(eccles_web_vm_t *vm, wb_reader_t *r, const char *url) {
    snprintf(vm->url, sizeof vm->url, "%s", url ? url : "");
    vm->running = 1;
    if (wb_page_begin(vm)) {
        wb_html_run(vm, r);
        if (vm->err == ECCLES_WEB_OK) { vm->loaded = 1; vm->dirty = 1; wb_fire_load_events(vm); }
    }
    vm->running = 0;
    if (vm->err != ECCLES_WEB_OK) { vm->loaded = 0; ECCLES_WEB_LOG_LINE("page load failed"); }
    return wb_result(vm);
}
static wb_st wb_load_url(eccles_web_vm_t *vm, const char *abs, int sub) {
    wb_src_t s; wb_reader_t r; wb_st st;  //the reader (a chunk buffer) lives on the c stack for the time of the load
    if (!wb_open(vm, abs, &s, sub)) return WB_BAD;
    wb_rd_stream(&r, wb_src_read, &s);
    st = wb_load_reader(vm, &r, abs);
    wb_src_close(&s);
    return st;
}

eccles_web_status_t eccles_web_vm_load(eccles_web_vm_t *vm, const char *url) {
    char abs[ECCLES_WEB_MAX_URL];
    if (!vm || !url) return WB_BAD;
    if (vm->running) { wb_fail(vm, ECCLES_WEB_ERR_BUSY, 0); return WB_BAD; }
    wb_err_clear(vm);
    wb_url_resolve(NULL, url, abs, sizeof abs);
    wb_hist_push(vm);
    return wb_load_url(vm, abs, 0);
}
eccles_web_status_t eccles_web_vm_load_buffer(eccles_web_vm_t *vm, const void *html, size_t len, const char *base_url) {
    wb_reader_t r;
    if (!vm || (!html && len)) return WB_BAD;
    if (vm->running) { wb_fail(vm, ECCLES_WEB_ERR_BUSY, 0); return WB_BAD; }
    wb_err_clear(vm); wb_hist_push(vm);
    wb_rd_mem(&r, html, len);
    return wb_load_reader(vm, &r, base_url);
}
eccles_web_status_t eccles_web_vm_load_stream(eccles_web_vm_t *vm, eccles_web_read_fn read, void *ctx, const char *base_url) {
    wb_reader_t r;
    if (!vm || !read) return WB_BAD;
    if (vm->running) { wb_fail(vm, ECCLES_WEB_ERR_BUSY, 0); return WB_BAD; }
    wb_err_clear(vm); wb_hist_push(vm);
    wb_rd_stream(&r, read, ctx);
    return wb_load_reader(vm, &r, base_url);
}
eccles_web_status_t eccles_web_vm_unload(eccles_web_vm_t *vm) {
    if (!vm) return WB_BAD;
    if (vm->running) { wb_fail(vm, ECCLES_WEB_ERR_BUSY, 0); return WB_BAD; }
    wb_page_reset(vm); vm->url[0] = 0; vm->nhist = 0;
#if ECCLES_WEB_JS
    wb_wasm_release_all(vm);
#endif
    return WB_OK;
}

//a page asked to navigate (link, location.href): done when the outermost call returns, never while its own script is on the stack
static void wb_request_nav(eccles_web_vm_t *vm, const char *url) {
    if (!(vm->perms & ECCLES_WEB_PERM_NAVIGATE)) { wb_deny(vm, "navigate"); return; }
    if (url[0] == '#') return;  //in page anchors: nothing to reload
    wb_url_resolve(vm->url, url, vm->nav_url, sizeof vm->nav_url);
    if (wb_url_kind(vm->nav_url) == 0) return;  //mailto:, tel:, javascript: ...
    vm->nav_pending = 1; vm->nav_back = 0;
}
static void wb_do_nav(eccles_web_vm_t *vm) {
    char u[ECCLES_WEB_MAX_URL];
    int guard = 4;
    while (vm->nav_pending && guard--) {  //a page that redirects at once is followed, but not forever
        vm->nav_pending = 0; snprintf(u, sizeof u, "%s", vm->nav_url);
        if (vm->nav_back) { vm->nav_back = 0; } else wb_hist_push(vm);
        wb_err_clear(vm);
        wb_load_url(vm, u, 0);
    }
    vm->nav_pending = 0;
}

eccles_web_status_t eccles_web_vm_back(eccles_web_vm_t *vm) {
    if (!vm) return WB_BAD;
    if (!(vm->perms & ECCLES_WEB_PERM_NAVIGATE) && vm->running) { wb_deny(vm, "back"); return WB_BAD; }
    if (vm->nhist == 0) return WB_BAD;
    wb_err_clear(vm);
    snprintf(vm->nav_url, sizeof vm->nav_url, "%s", vm->hist[--vm->nhist]);
    vm->nav_pending = 1; vm->nav_back = 1;
    wb_do_nav(vm);
    return wb_result(vm);
}
bool eccles_web_vm_can_go_back(const eccles_web_vm_t *vm) { return vm && vm->nhist > 0; }

const char *eccles_web_vm_title(const eccles_web_vm_t *vm) {
    uint16_t t;
    if (!vm || !vm->loaded || !vm->title_node) return "";
    t = vm->nodes[vm->title_node].child;
    return t && vm->nodes[t].type == WB_N_TEXT ? wb_text_of(vm, &vm->nodes[t]) : "";
}
const char *eccles_web_vm_url(const eccles_web_vm_t *vm) { return vm ? vm->url : ""; }

//view
eccles_web_status_t eccles_web_vm_set_viewport(eccles_web_vm_t *vm, uint32_t w, uint32_t h) {
    if (!vm || w < 16 || h < 16 || w > 16000 || h > 16000) return WB_BAD;
    if (w != vm->vw || h != vm->vh) { vm->vw = w; vm->vh = h; vm->dirty = 1; vm->laid_out = 0; }
    return WB_OK;
}
static void wb_ensure_layout(eccles_web_vm_t *vm) {
    if (vm->loaded && (!vm->laid_out || vm->dirty)) {
        if (vm->in_layout) return;
        vm->in_layout = 1; wb_layout_page(vm); vm->in_layout = 0;
    }
}
eccles_web_status_t eccles_web_vm_render(eccles_web_vm_t *vm) {
    if (!vm) return WB_BAD;
    wb_err_clear(vm);
    if (!vm->loaded) { wb_fail(vm, ECCLES_WEB_ERR_NOT_LOADED, 0); return WB_BAD; }
    wb_ensure_layout(vm);
    wb_paint_page(vm);
    vm->dirty = 0;
    return WB_OK;
}
eccles_web_status_t eccles_web_vm_scroll_to(eccles_web_vm_t *vm, int32_t y) {
    int32_t mx;
    if (!vm) return WB_BAD;
    if (!vm->loaded) { wb_fail(vm, ECCLES_WEB_ERR_NOT_LOADED, 0); return WB_BAD; }
    wb_ensure_layout(vm);
    mx = (int32_t)vm->page_h - (int32_t)vm->vh; if (mx < 0) mx = 0;
    if (y < 0) y = 0;
    if (y > mx) y = mx;
    if (y != vm->scroll_y) { vm->scroll_y = y; vm->dirty = 1; }
    return WB_OK;
}
eccles_web_status_t eccles_web_vm_scroll_by(eccles_web_vm_t *vm, int32_t dy) { return vm ? eccles_web_vm_scroll_to(vm, vm->scroll_y + dy) : WB_BAD; }
uint32_t eccles_web_vm_scroll_y(const eccles_web_vm_t *vm) { return vm ? (uint32_t)vm->scroll_y : 0; }
uint32_t eccles_web_vm_page_height(const eccles_web_vm_t *vm) { return vm ? vm->page_h : 0; }
bool eccles_web_vm_dirty(const eccles_web_vm_t *vm) { return vm && vm->dirty; }

//input

static int wb_is_editable(const eccles_web_vm_t *vm, uint16_t n) {
    if (vm->nodes[n].tag == A_textarea) return 1;
    return vm->nodes[n].tag == A_input && wb_leaf_kind(vm, n) == WB_LEAF_TEXTIN;
}

//edits the value of the focused input / textarea. in place when it fits (no pool growth per key stroke)
static void wb_edit(eccles_web_vm_t *vm, const char *s, uint32_t n) {
    uint16_t f = vm->focus; char buf[WB_INPUT_CAP * 2 + 8]; uint32_t cur = 0; const char *old;
    if (!f || !wb_is_editable(vm, f)) return;
    if (vm->nodes[f].tag == A_textarea) {
        uint16_t t = vm->nodes[f].child; wb_node_t *tn;
        if (!t) { tn = wb_new_node(vm, WB_N_TEXT, 0, f); if (!tn) return; tn->text = 0; t = wb_idx(vm, tn); }
        tn = &vm->nodes[t]; old = wb_text_of(vm, tn); cur = tn->tlen;
        if (cur + n + 1 > sizeof buf) return;
        memcpy(buf, old, cur);
        if (n == 1 && s[0] == '\b') { if (cur) { cur--; while (cur && ((uint8_t)buf[cur] & 0xC0) == 0x80) cur--; } }
        else { memcpy(buf + cur, s, n); cur += n; }
        if (tn->text != 0 && cur <= tn->tlen) { memcpy(vm->text + tn->text, buf, cur); vm->text[tn->text + cur] = 0; tn->tlen = (uint16_t)cur; }
        else { uint32_t off = wb_pool_alloc(vm, cur + 32); if (off == 0xFFFFFFFFu) return; memcpy(vm->text + off, buf, cur); vm->text[off + cur] = 0; tn->text = off; tn->tlen = (uint16_t)cur; }
        vm->dirty = 1;
    } else {
        uint32_t l; old = wb_attr_get(vm, f, A_value, &l); cur = l;
        if (cur + n + 1 > sizeof buf) return;
        if (old) memcpy(buf, old, cur);
        if (n == 1 && s[0] == '\b') { if (cur) { cur--; while (cur && ((uint8_t)buf[cur] & 0xC0) == 0x80) cur--; } }
        else { if (n > WB_INPUT_CAP) return; memcpy(buf + cur, s, n); cur += n; }
        if (cur > WB_INPUT_CAP * 2) return;
        wb_attr_set(vm, f, A_value, buf, cur, WB_INPUT_CAP);
    }
}

eccles_web_status_t eccles_web_vm_key(eccles_web_vm_t *vm, const char *utf8, uint32_t len) {
#ifdef ECCLES_WEB_NO_FORMS
    (void)vm; (void)utf8; (void)len; return WB_UNS;
#else
    if (!vm || !utf8 || !len) return WB_BAD;
    wb_err_clear(vm);
    if (!vm->loaded) { wb_fail(vm, ECCLES_WEB_ERR_NOT_LOADED, 0); return WB_BAD; }
    if (!vm->focus) return WB_BAD;
    if (len == 1 && utf8[0] == '\n' && vm->nodes[vm->focus].tag == A_input) {
#if ECCLES_WEB_JS
        wb_js_dispatch(vm, vm->focus, A_change);
#endif
    } else {
        wb_edit(vm, utf8, len);
#if ECCLES_WEB_JS
        wb_js_dispatch(vm, vm->focus, A_input);
#endif
    }
    wb_do_nav(vm);
    return wb_result(vm);
#endif
}

//a tap: focus, click handlers, then the default action (follow a link, toggle a checkbox)
static void wb_tap(eccles_web_vm_t *vm, int32_t x, int32_t y) {
    uint16_t n, f, a; int prevented = 0;
    wb_ensure_layout(vm);
    n = wb_hit_node(vm, 0, x, y + vm->scroll_y);
    f = n; while (f && !wb_is_editable(vm, f)) f = vm->nodes[f].parent;
    if (vm->focus != f) { vm->focus = f; vm->dirty = 1; }
    if (!n) return;
#if ECCLES_WEB_JS
    prevented = wb_js_dispatch(vm, n, A_click);
#endif
    if (prevented) return;
    a = n;
    while (a) {
        if (vm->nodes[a].type == WB_N_ELEM) {
            if (vm->nodes[a].tag == A_a) { uint32_t l; const char *h = wb_attr_get(vm, a, A_href, &l); if (h && l) { wb_request_nav(vm, h); return; } }
            if (vm->nodes[a].tag == A_input && wb_leaf_kind(vm, a) == WB_LEAF_CHECK) {
                if (wb_attr_find(vm, a, A_checked)) wb_attr_remove(vm, a, A_checked);
                else wb_attr_set(vm, a, A_checked, "", 0, 0);
                vm->dirty = 1;
#if ECCLES_WEB_JS
                wb_js_dispatch(vm, a, A_change);
#endif
                return;
            }
        }
        a = vm->nodes[a].parent;
    }
}

eccles_web_status_t eccles_web_vm_touch(eccles_web_vm_t *vm, int32_t x, int32_t y, eccles_web_touch_t kind) {
    if (!vm) return WB_BAD;
    wb_err_clear(vm);
    if (!vm->loaded) { wb_fail(vm, ECCLES_WEB_ERR_NOT_LOADED, 0); return WB_BAD; }
    if (kind == ECCLES_WEB_TOUCH_DOWN) { vm->touching = 1; vm->touch_moved = 0; vm->touch_x0 = x; vm->touch_y0 = y; vm->touch_ly = y; return WB_OK; }
    if (!vm->touching) return WB_OK;
    if (kind == ECCLES_WEB_TOUCH_MOVE) {
        int32_t dy = y - vm->touch_ly, tot = y - vm->touch_y0, dx = x - vm->touch_x0;
        if (!vm->touch_moved && (tot > ECCLES_WEB_TAP_SLOP_PX || tot < -ECCLES_WEB_TAP_SLOP_PX || dx > ECCLES_WEB_TAP_SLOP_PX || dx < -ECCLES_WEB_TAP_SLOP_PX)) vm->touch_moved = 1;
        if (vm->touch_moved) { eccles_web_vm_scroll_by(vm, -dy); vm->touch_ly = y; if (vm->dirty) eccles_web_vm_render(vm); }
        return WB_OK;
    }
    vm->touching = 0;  //UP
    if (!vm->touch_moved) {
        wb_tap(vm, x, y);
        wb_do_nav(vm);
        if (vm->loaded && vm->dirty) eccles_web_vm_render(vm);
    }
    return wb_result(vm);
}

eccles_web_status_t eccles_web_vm_tick(eccles_web_vm_t *vm, uint32_t now_ms) {
    if (!vm) return WB_BAD;
    wb_err_clear(vm);
    if (!vm->loaded) return WB_OK;
#if ECCLES_WEB_JS
    wb_js_tick(vm, now_ms);
#else
    (void)now_ms;
#endif
    wb_do_nav(vm);
    return wb_result(vm);
}
bool eccles_web_vm_next_timer(const eccles_web_vm_t *vm, uint32_t now_ms, uint32_t *wait_ms) {
#if ECCLES_WEB_JS
    return vm && vm->loaded && wb_js_next_timer(vm, now_ms, wait_ms);
#else
    (void)vm; (void)now_ms; (void)wait_ms; return false;
#endif
}

//scripts
eccles_web_status_t eccles_web_vm_eval(eccles_web_vm_t *vm, const char *js) {
    if (!vm || !js) return WB_BAD;
    if (vm->running) { wb_fail(vm, ECCLES_WEB_ERR_BUSY, 0); return WB_BAD; }
    wb_err_clear(vm);
    if (!vm->loaded) { wb_fail(vm, ECCLES_WEB_ERR_NOT_LOADED, 0); return WB_BAD; }
    if (!(vm->perms & ECCLES_WEB_PERM_JS)) { wb_deny(vm, "eval"); return WB_BAD; }
#if ECCLES_WEB_JS
    wb_js_eval(vm, js);
    wb_do_nav(vm);
    return wb_result(vm);
#else
    wb_fail(vm, ECCLES_WEB_ERR_UNSUPPORTED_FEATURE, 0); return WB_UNS;
#endif
}
void eccles_web_vm_set_fuel(eccles_web_vm_t *vm, uint32_t budget) {
#if ECCLES_WEB_JS
    if (vm) vm->fuel_budget = budget;
#else
    (void)vm; (void)budget;
#endif
}

//diagnostics
eccles_web_error_t eccles_web_vm_last_error(const eccles_web_vm_t *vm) { return vm ? vm->err : ECCLES_WEB_ERR_NOT_INIT; }
uint32_t    eccles_web_vm_last_error_info(const eccles_web_vm_t *vm) { return vm ? vm->err_info : 0; }
const char *eccles_web_vm_last_error_str(const eccles_web_vm_t *vm) { return vm ? vm->err_str : ""; }
eccles_web_error_t eccles_web_vm_script_error(const eccles_web_vm_t *vm) { return vm ? vm->jserr : ECCLES_WEB_ERR_NOT_INIT; }
uint32_t    eccles_web_vm_script_error_info(const eccles_web_vm_t *vm) { return vm ? vm->jserr_info : 0; }
const char *eccles_web_vm_script_error_str(const eccles_web_vm_t *vm) { return vm ? vm->jserr_str : ""; }
uint32_t    eccles_web_vm_script_error_count(const eccles_web_vm_t *vm) { return vm ? vm->jserr_count : 0; }

const char *eccles_web_error_name(eccles_web_error_t e) {
    switch (e) {
        case ECCLES_WEB_OK: return "OK"; case ECCLES_WEB_ERR_NOT_INIT: return "NOT_INIT"; case ECCLES_WEB_ERR_NOT_LOADED: return "NOT_LOADED";
        case ECCLES_WEB_ERR_BUSY: return "BUSY"; case ECCLES_WEB_ERR_NO_MEMORY: return "NO_MEMORY"; case ECCLES_WEB_ERR_LIMIT: return "LIMIT";
        case ECCLES_WEB_ERR_IO: return "IO"; case ECCLES_WEB_ERR_NO_FS: return "NO_FS"; case ECCLES_WEB_ERR_NO_NET: return "NO_NET";
        case ECCLES_WEB_ERR_BAD_URL: return "BAD_URL"; case ECCLES_WEB_ERR_UNSUPPORTED_FEATURE: return "UNSUPPORTED_FEATURE";
        case ECCLES_WEB_ERR_BAD_ARG: return "BAD_ARG"; case ECCLES_WEB_ERR_NO_EXPORT: return "NO_EXPORT";
        case ECCLES_WEB_ERR_JS_SYNTAX: return "JS_SYNTAX"; case ECCLES_WEB_ERR_JS_THROW: return "JS_THROW";
        case ECCLES_WEB_TRAP_JS_FUEL: return "TRAP_JS_FUEL"; case ECCLES_WEB_TRAP_JS_STACK: return "TRAP_JS_STACK";
        case ECCLES_WEB_TRAP_JS_CALL_DEPTH: return "TRAP_JS_CALL_DEPTH"; case ECCLES_WEB_TRAP_JS_HEAP: return "TRAP_JS_HEAP";
        case ECCLES_WEB_TRAP_HOST: return "TRAP_HOST";
        default: return "UNKNOWN";
    }
}

eccles_web_stats_t eccles_web_vm_get_stats(const eccles_web_vm_t *vm) {
    eccles_web_stats_t s; memset(&s, 0, sizeof s);
    if (!vm) return s;
    s.arena_size = (uint32_t)(vm->end - vm->base);
    s.arena_used = (uint32_t)((vm->lo - vm->base) + (vm->hi_init - vm->hi));
    s.loaded = vm->loaded; s.nodes = vm->nnodes; s.nodes_max = ECCLES_WEB_MAX_NODES; s.attrs = vm->nattrs;
    s.text_bytes = vm->text_len; s.text_max = ECCLES_WEB_TEXT_POOL; s.atoms = vm->natoms; s.atoms_max = ECCLES_WEB_MAX_ATOMS;
    s.rules = vm->nrules; s.decls = vm->ndecls; s.page_height = vm->page_h;
#if ECCLES_WEB_JS
    s.js_code_bytes = vm->jcode_len; s.js_heap_size = vm->jheap_size; s.js_heap_used = vm->jalloc_since; s.js_gc_runs = vm->jgc_runs;
    s.js_ops = vm->jops; s.functions = vm->nprotos;
#endif
    return s;
}

//privilege
eccles_web_status_t eccles_web_vm_set_hal_privilege(eccles_web_vm_t *vm, uint32_t perms) { if (!vm) return WB_BAD; vm->perms = perms & ECCLES_WEB_ALLOW_ALL; return WB_OK; }
uint32_t    eccles_web_vm_get_privilege(const eccles_web_vm_t *vm) { return vm ? vm->perms : 0; }
uint32_t    eccles_web_vm_denied_count(const eccles_web_vm_t *vm) { return vm ? vm->denied : 0; }
const char *eccles_web_vm_last_denied(const eccles_web_vm_t *vm) { return vm && vm->last_denied ? vm->last_denied : ""; }
void        eccles_web_vm_set_denied_hook(eccles_web_vm_t *vm, eccles_web_denied_fn hook) { if (vm) vm->denied_hook = hook; }

eccles_web_status_t eccles_web_vm_register(eccles_web_vm_t *vm, const char *name, eccles_web_host_fn fn) {
    uint8_t i;
    if (!vm || !name || !fn || !name[0]) return WB_BAD;
    for (i = 0; i < vm->nhost; i++) if (!strcmp(vm->host[i].name, name)) { vm->host[i].fn = fn; return WB_OK; }
    if (vm->nhost >= ECCLES_WEB_MAX_HOST_FUNCS) { wb_fail(vm, ECCLES_WEB_ERR_LIMIT, 7); return WB_BAD; }
    vm->host[vm->nhost].name = name; vm->host[vm->nhost].fn = fn; vm->nhost++;
    return WB_OK;
}
void eccles_web_vm_unregister_all(eccles_web_vm_t *vm) { if (vm) vm->nhost = 0; }

//handle-free wrappers = the default instance
#define WB_D(call) eccles_web_vm_t *vm = wb_def(); if (!vm) return WB_BAD; return call
eccles_web_status_t eccles_web_load(const char *url) { WB_D(eccles_web_vm_load(vm, url)); }
eccles_web_status_t eccles_web_load_buffer(const void *h, size_t n, const char *b) { WB_D(eccles_web_vm_load_buffer(vm, h, n, b)); }
eccles_web_status_t eccles_web_load_stream(eccles_web_read_fn r, void *c, const char *b) { WB_D(eccles_web_vm_load_stream(vm, r, c, b)); }
eccles_web_status_t eccles_web_unload(void) { WB_D(eccles_web_vm_unload(vm)); }
eccles_web_status_t eccles_web_set_viewport(uint32_t w, uint32_t h) { WB_D(eccles_web_vm_set_viewport(vm, w, h)); }
eccles_web_status_t eccles_web_render(void) { WB_D(eccles_web_vm_render(vm)); }
eccles_web_status_t eccles_web_scroll_by(int32_t dy) { WB_D(eccles_web_vm_scroll_by(vm, dy)); }
eccles_web_status_t eccles_web_scroll_to(int32_t y) { WB_D(eccles_web_vm_scroll_to(vm, y)); }
uint32_t eccles_web_scroll_y(void) { return eccles_web_vm_scroll_y(wb_def()); }
uint32_t eccles_web_page_height(void) { return eccles_web_vm_page_height(wb_def()); }
eccles_web_status_t eccles_web_touch(int32_t x, int32_t y, eccles_web_touch_t k) { WB_D(eccles_web_vm_touch(vm, x, y, k)); }
eccles_web_status_t eccles_web_key(const char *s, uint32_t n) { WB_D(eccles_web_vm_key(vm, s, n)); }
eccles_web_status_t eccles_web_tick(uint32_t now) { WB_D(eccles_web_vm_tick(vm, now)); }
bool eccles_web_next_timer(uint32_t now, uint32_t *w) { return eccles_web_vm_next_timer(wb_def(), now, w); }
bool eccles_web_dirty(void) { return eccles_web_vm_dirty(wb_def()); }
eccles_web_status_t eccles_web_back(void) { WB_D(eccles_web_vm_back(vm)); }
bool eccles_web_can_go_back(void) { return eccles_web_vm_can_go_back(wb_def()); }
const char *eccles_web_title(void) { return eccles_web_vm_title(wb_def()); }
const char *eccles_web_url(void) { return eccles_web_vm_url(wb_def()); }
eccles_web_status_t eccles_web_eval(const char *js) { WB_D(eccles_web_vm_eval(vm, js)); }
void eccles_web_set_fuel(uint32_t b) { eccles_web_vm_set_fuel(wb_def(), b); }
eccles_web_error_t eccles_web_last_error(void) { return eccles_web_vm_last_error(wb_def()); }
uint32_t eccles_web_last_error_info(void) { return eccles_web_vm_last_error_info(wb_def()); }
const char *eccles_web_last_error_str(void) { return eccles_web_vm_last_error_str(wb_def()); }
eccles_web_error_t eccles_web_script_error(void) { return eccles_web_vm_script_error(wb_def()); }
uint32_t eccles_web_script_error_info(void) { return eccles_web_vm_script_error_info(wb_def()); }
const char *eccles_web_script_error_str(void) { return eccles_web_vm_script_error_str(wb_def()); }
uint32_t eccles_web_script_error_count(void) { return eccles_web_vm_script_error_count(wb_def()); }
eccles_web_stats_t eccles_web_get_stats(void) { return eccles_web_vm_get_stats(wb_def()); }
eccles_web_status_t eccles_web_set_hal_privilege(uint32_t p) { WB_D(eccles_web_vm_set_hal_privilege(vm, p)); }
uint32_t eccles_web_get_privilege(void) { return eccles_web_vm_get_privilege(wb_def()); }
uint32_t eccles_web_denied_count(void) { return eccles_web_vm_denied_count(wb_def()); }
const char *eccles_web_last_denied(void) { return eccles_web_vm_last_denied(wb_def()); }
eccles_web_status_t eccles_web_register(const char *n, eccles_web_host_fn f) { WB_D(eccles_web_vm_register(vm, n, f)); }
eccles_web_status_t eccles_web_register_async(const char *n, eccles_web_async_fn f) { WB_D(eccles_web_vm_register_async(vm, n, f)); }
eccles_web_status_t eccles_web_async_resolve(uint32_t id, const eccles_web_val_t *v) { WB_D(eccles_web_vm_async_resolve(vm, id, v)); }
eccles_web_status_t eccles_web_async_reject(uint32_t id, const char *m) { WB_D(eccles_web_vm_async_reject(vm, id, m)); }
eccles_web_status_t eccles_web_fetch_respond(uint32_t id, uint32_t st, const char *h, const void *b, size_t l) { WB_D(eccles_web_vm_fetch_respond(vm, id, st, h, b, l)); }
eccles_web_status_t eccles_web_fetch_fail(uint32_t id, const char *m) { WB_D(eccles_web_vm_fetch_fail(vm, id, m)); }
void eccles_web_unregister_all(void) { eccles_web_vm_unregister_all(wb_def()); }

//8. backends: ui, net, files, host simulator

//console output of a page (console.log / alert). needs ECCLES_WEB_PERM_LOG
static void wb_log(eccles_web_vm_t *vm, const char *s, uint32_t n) {
    if (!(vm->perms & ECCLES_WEB_PERM_LOG)) { wb_deny(vm, "log"); return; }
    if (g_ui.log) g_ui.log((const uint8_t *)s, n);
}


//the default ui backend. on a chip it draws nothing (the text metrics are an estimate), on the host it records every call (see eccles_web_sim_*)
#if ECCLES_WEB_PLATFORM == ECCLES_WEB_PLAT_HOST
#define WB_SIM_MAX 4096
static eccles_web_sim_cmd_t g_sim[WB_SIM_MAX]; static uint32_t g_sim_n;
static char g_sim_log[4096]; static uint32_t g_sim_logn;
static uint32_t g_sim_num = 11, g_sim_den = 20;
static eccles_web_sim_cmd_t *sim_add(char kind) {
    eccles_web_sim_cmd_t *c;
    if (g_sim_n >= WB_SIM_MAX) return NULL;
    c = &g_sim[g_sim_n++]; memset(c, 0, sizeof *c); c->kind = kind; return c;
}
static wb_st sim_begin(uint32_t w, uint32_t h) { eccles_web_sim_cmd_t *c = sim_add('B'); if (c) { c->w = w; c->h = h; } return WB_OK; }
static wb_st sim_end(void) { sim_add('E'); return WB_OK; }
static wb_st sim_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t rgb) { eccles_web_sim_cmd_t *c = sim_add('R'); if (c) { c->x = x; c->y = y; c->w = w; c->h = h; c->rgb = rgb; } return WB_OK; }
static wb_st sim_text(int32_t x, int32_t y, uint32_t px, uint32_t st, uint32_t rgb, const char *s, uint32_t n) {
    eccles_web_sim_cmd_t *c = sim_add('T'); uint32_t k = n < sizeof c->text - 1 ? n : (uint32_t)sizeof c->text - 1;
    if (c) { c->x = x; c->y = y; c->px = px; c->style = st; c->rgb = rgb; memcpy(c->text, s, k); c->text[k] = 0; }
    return WB_OK;
}
static wb_st sim_width(uint32_t px, uint32_t st, const char *s, uint32_t n, uint32_t *w) { (void)st; *w = wb_utf8_len(s, n) * px * g_sim_num / g_sim_den; return WB_OK; }
static wb_st sim_image(const char *src, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    eccles_web_sim_cmd_t *c = sim_add('I');
    if (c) { c->x = x; c->y = y; c->w = w; c->h = h; snprintf(c->text, sizeof c->text, "%s", src); }
    return WB_OK;
}
static wb_st sim_log(const uint8_t *b, uint32_t n) {
    uint32_t k = n; if (g_sim_logn + k + 2 > sizeof g_sim_log) k = (uint32_t)sizeof g_sim_log - g_sim_logn - 2;
    memcpy(g_sim_log + g_sim_logn, b, k); g_sim_logn += k; g_sim_log[g_sim_logn++] = '\n'; g_sim_log[g_sim_logn] = 0;
    return WB_OK;
}
void eccles_web_sim_reset(void) { g_sim_n = 0; g_sim_logn = 0; g_sim_log[0] = 0; }
uint32_t eccles_web_sim_cmd_count(void) { return g_sim_n; }
bool eccles_web_sim_cmd(uint32_t i, eccles_web_sim_cmd_t *out) { if (i >= g_sim_n || !out) return false; *out = g_sim[i]; return true; }
uint32_t eccles_web_sim_log_take(char *out, uint32_t max) {
    uint32_t n = g_sim_logn < max - 1 ? g_sim_logn : max - 1;
    if (!max) return 0;
    memcpy(out, g_sim_log, n); out[n] = 0; g_sim_logn = 0; g_sim_log[0] = 0; return n;
}
void eccles_web_sim_set_text_metrics(uint32_t num, uint32_t den) { if (num && den) { g_sim_num = num; g_sim_den = den; } }
#endif

static eccles_web_ui_t g_ui_default; static uint8_t g_ui_ready;
static wb_st stub_width(uint32_t px, uint32_t st, const char *s, uint32_t n, uint32_t *w) { (void)st; *w = wb_utf8_len(s, n) * px * 11 / 20; return WB_OK; }
static void wb_ui_init(void) {
    if (g_ui_ready) return;
    g_ui_ready = 1;
    memset(&g_ui_default, 0, sizeof g_ui_default);
#if ECCLES_WEB_PLATFORM == ECCLES_WEB_PLAT_HOST
    g_ui_default.begin = sim_begin; g_ui_default.end = sim_end; g_ui_default.fill_rect = sim_rect; g_ui_default.draw_text = sim_text;
    g_ui_default.text_width = sim_width; g_ui_default.draw_image = sim_image; g_ui_default.log = sim_log;
#else
    g_ui_default.text_width = stub_width;  //nothing is drawn, nothing is logged, but pages still lay out
#endif
    (void)stub_width;
    g_ui = g_ui_default;
}
void eccles_web_ui_override(const eccles_web_ui_t *ops) {
    wb_ui_init();
    if (!ops) return;
    if (ops->begin) g_ui.begin = ops->begin;
    if (ops->end) g_ui.end = ops->end;
    if (ops->fill_rect) g_ui.fill_rect = ops->fill_rect;
    if (ops->draw_text) g_ui.draw_text = ops->draw_text;
    if (ops->text_width) g_ui.text_width = ops->text_width;
    if (ops->image_size) g_ui.image_size = ops->image_size;
    if (ops->draw_image) g_ui.draw_image = ops->draw_image;
    if (ops->log) g_ui.log = ops->log;
}
void eccles_web_ui_reset(void) { wb_ui_init(); g_ui = g_ui_default; }

void eccles_web_set_net(eccles_web_net_open_fn open, eccles_web_net_read_fn read, eccles_web_net_close_fn close) { g_net_open = open; g_net_read = read; g_net_close = close; }
void eccles_web_set_fs(eccles_web_fs_open_fn open, eccles_web_fs_read_fn read, eccles_web_fs_close_fn close) { g_fs_open = open; g_fs_read = read; g_fs_close = close; }
