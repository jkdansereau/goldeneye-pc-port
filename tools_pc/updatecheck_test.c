/*
 * Offline test harness for the update-check parsing helpers
 * (port/include/updatecheck_parse.h). No network, no SDL: it exercises
 * versionNewer() and extractTag() on a table of cases and prints PASS/FAIL
 * per case.
 *
 * Build and run:
 *   gcc -O2 -Wall -o <temp>/uctest tools_pc/updatecheck_test.c
 *   <temp>/uctest
 *
 * Exits nonzero if any case fails.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../port/include/updatecheck_parse.h"

static int g_fail = 0;

static void checkVer(const char *tag, const char *running, int want)
{
    int got = versionNewer(tag, running);
    if (got == want) {
        printf("PASS versionNewer(\"%s\", \"%s\") = %d\n", tag, running, got);
    } else {
        printf("FAIL versionNewer(\"%s\", \"%s\") = %d, expected %d\n",
               tag, running, got, want);
        g_fail++;
    }
}

static void checkTag(const char *body, size_t outN, int wantOk, const char *want)
{
    char out[64];
    memset(out, 0x5a, sizeof(out));
    int got = extractTag(body, out, outN);
    if (wantOk && got && strcmp(out, want) == 0) {
        printf("PASS extractTag -> \"%s\"\n", out);
    } else if (!wantOk && got == 0) {
        printf("PASS extractTag -> fail\n");
    } else {
        printf("FAIL extractTag: returned %d (out \"%s\"), expected %s\n",
               got, got ? out : "(unwritten)", wantOk ? want : "fail");
        g_fail++;
    }
}

int main(void)
{
    /* versionNewer(tag, running) */
    checkVer("v0.5.1", "0.5.0", 1);
    checkVer("v0.5.0", "0.5.0", 0);
    checkVer("v0.4.9", "0.5.0", 0);
    checkVer("v0.10.0", "0.9.9", 1);
    checkVer("v1.0", "0.9.9", 1);
    checkVer("v1.0.0", "1.0", 0);
    checkVer("V0.6.0", "0.5.1", 1);
    checkVer("v0.5.1-rc1", "0.5.1", 0);
    checkVer("garbage", "0.5.0", 0);
    checkVer("v0.5.1", "", 0);

    /* extractTag(body) */
    checkTag("{\"tag_name\":\"v0.5.1\"}", 64, 1, "v0.5.1");
    checkTag("{\"tag_name\" : \"v0.5.1\"}", 64, 1, "v0.5.1");
    checkTag("{\"tag_name\":\n  \"v0.5.1\"}", 64, 1, "v0.5.1");
    checkTag("{\"name\":\"x\"}", 64, 0, NULL);
    checkTag("{\"tag_name\":\"v0.5.1\x1b[31m\"}", 64, 0, NULL);
    checkTag("{\"tag_name\":\"\"}", 64, 0, NULL);
    checkTag("{\"tag_name\":\"v0.5.1", 64, 0, NULL);

    /* 40-char tag, out size 32: must fail and must not overflow. */
    {
        char body[80];
        snprintf(body, sizeof(body), "{\"tag_name\":\"%040d\"}", 1);
        char out[64];
        memset(out, 0x5a, sizeof(out));
        int got = extractTag(body, out, 32);
        if (got == 0 && out[32] == (char)0x5a && out[63] == (char)0x5a) {
            printf("PASS extractTag 40-char tag, out size 32 -> fail, no overflow\n");
        } else {
            printf("FAIL extractTag 40-char tag, out size 32: returned %d "
                   "(out[32]=%02x out[63]=%02x)\n",
                   got, (unsigned char)out[32], (unsigned char)out[63]);
            g_fail++;
        }
    }

    /* A ~300 KB body: "tag_name" in the first ~24 bytes, 300 KB of padding
     * after it. extractTag must still find the tag (and never needs the
     * tail, which is what the fetch path may have truncated). */
    {
        static const char pre[] = "{\"tag_name\":\"v0.5.1\",\"notes\":\"";
        static const char post[] = "\"}";
        enum { PAD = 300000 };
        char *body = malloc(strlen(pre) + PAD + strlen(post) + 1);
        if (!body) {
            printf("FAIL out of memory\n");
            return 1;
        }
        strcpy(body, pre);
        memset(body + strlen(pre), 'x', PAD);
        strcpy(body + strlen(pre) + PAD, post);
        checkTag(body, 64, 1, "v0.5.1");
        free(body);
    }

    if (g_fail) {
        printf("%d case(s) FAILED\n", g_fail);
        return 1;
    }
    printf("all cases passed\n");
    return 0;
}
