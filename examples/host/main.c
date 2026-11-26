/* eccles_web on your PC: load a page, look at what it drew, tap a button, let it call C. */
#include <stdio.h>
#include <string.h>
#include "eccles_web.h"

static eccles_web_status_t dial(eccles_web_vm_t *vm, const eccles_web_val_t *args, uint32_t n, eccles_web_val_t *ret) {
    (void)vm; (void)ret;
    if (n < 1 || args[0].type != ECCLES_WEB_V_STR) return ECCLES_WEB_FAILED;
    printf("page called dial(\"%s\")\n", args[0].str);
    return ECCLES_WEB_SUCCESSFUL;
}

static const char page[] =
    "<title>Hello</title><style>h1{color:#06c} button{padding:12px}</style>"
    "<h1 id=h>Hello</h1><button id=b>Tap me</button>"
    "<script>let n=0;"
    "document.getElementById('b').addEventListener('click',()=>{"
    "document.getElementById('h').textContent=`Tapped ${++n}`; console.log('tap', n);});"
    "console.log('page up');</script>";

int main(void) {
    char log[512];
    eccles_web_sim_cmd_t cmd;
    int32_t bx = -1, by = -1;
    uint32_t i;

    eccles_web_register("dial", dial);
    eccles_web_set_viewport(320, 480);
    if (eccles_web_load_buffer(page, sizeof page - 1, NULL) != ECCLES_WEB_SUCCESSFUL) {
        printf("load failed: %s\n", eccles_web_error_name(eccles_web_last_error()));
        return 1;
    }
    eccles_web_render();
    for (i = 0; i < eccles_web_sim_cmd_count(); i++) {
        if (!eccles_web_sim_cmd(i, &cmd) || cmd.kind != 'T') continue;
        printf("  text (%d,%d) %upx \"%s\"\n", (int)cmd.x, (int)cmd.y, (unsigned)cmd.px, cmd.text);
        if (!strncmp(cmd.text, "Tap", 3)) { bx = cmd.x + 2; by = cmd.y + 2; }
    }
    if (bx < 0) return 1;

    eccles_web_touch(bx, by, ECCLES_WEB_TOUCH_DOWN);
    eccles_web_touch(bx, by, ECCLES_WEB_TOUCH_UP);
    eccles_web_eval("console.log('heading is now:', document.getElementById('h').textContent)");
    eccles_web_sim_log_take(log, sizeof log);
    fputs(log, stdout);

    /* host functions are refused until the owner grants them */
    eccles_web_set_hal_privilege(ECCLES_WEB_ALLOW_PAGE | ECCLES_WEB_PERM_HOST);
    eccles_web_eval("dial('112')");

    return eccles_web_script_error() == ECCLES_WEB_OK ? 0 : 1;
}
