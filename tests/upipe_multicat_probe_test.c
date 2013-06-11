/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short unit tests for multicat_probe pipe
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_multicat_probe.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define SYSTIMEINC      100
#define ROTATE          (SYSTIMEINC * 10)
#define UREFNB          (ROTATE * 5 + 1)

static unsigned int pipe_counter = 0, probe_counter = 0;

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
        case UPROBE_MULTICAT_PROBE_ROTATE:
            probe_counter++;
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_MULTICAT_PROBE_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref*);
            uint64_t index = va_arg(args, uint64_t);
            uint64_t systime = 0;
            uref_clock_get_systime(uref, &systime);
            assert(systime/ROTATE == index);
            assert(index == probe_counter);

            break;
    }
    return true;
}

/** helper phony pipe to test upipe_multicat_probe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_multicat_probe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump *upump)
{
    assert(uref != NULL);
    const char *def;
    if (uref_flow_get_def(uref, &def) || uref_flow_get_end(uref)) {
        uref_free(uref);
        return;
    }

    uref_free(uref);
    pipe_counter++;
}

/** helper phony pipe to test upipe_multicat_probe */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_multicat_probe */
static struct upipe_mgr test_mgr = {
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
};

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);

    struct upipe *upipe_sink = upipe_alloc(&test_mgr, log);
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_multicat_probe_mgr = upipe_multicat_probe_mgr_alloc();
    assert(upipe_multicat_probe_mgr != NULL);
    struct upipe *upipe_multicat_probe = upipe_alloc(upipe_multicat_probe_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "multicat_probe"));
    assert(upipe_multicat_probe != NULL);
    assert(upipe_multicat_probe_set_rotate(upipe_multicat_probe, ROTATE));
    assert(upipe_set_output(upipe_multicat_probe, upipe_sink));

    struct uref *uref;
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "internal."));
    upipe_input(upipe_multicat_probe, uref, NULL);

    int i;
    for (i=0; i < UREFNB; i++) {
        uref = uref_alloc(uref_mgr);
        assert(uref != NULL);
        uref_clock_set_systime(uref, SYSTIMEINC * i);
        upipe_input(upipe_multicat_probe, uref, NULL);
    }
    assert(pipe_counter == UREFNB);
    assert(probe_counter == (UREFNB *SYSTIMEINC / ROTATE));

    upipe_release(upipe_multicat_probe);
    upipe_mgr_release(upipe_multicat_probe_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}