#ifndef __CMD_ACK_H__
#define __CMD_ACK_H__

#include "common/tools/common_def.h"
#include "service/sys/sys_log.h"
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CMD_ACK_CTX_MAX_LEN = 96
};

static inline bool cmd_ack_is_ctx_char_safe(char c)
{
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
        return true;
    }
    switch (c) {
        case '_':
        case '-':
        case '.':
        case ':':
        case ';':
        case '=':
        case '/':
            return true;
        default:
            return false;
    }
}

static inline void cmd_ack_sanitize_ctx(const char *ctx, char out[CMD_ACK_CTX_MAX_LEN + 1])
{
    if (ctx == NULL || out == NULL) {
        return;
    }

    u32 j = 0;
    for (u32 i = 0; ctx[i] != '\0' && j < CMD_ACK_CTX_MAX_LEN; i++) {
        char c = ctx[i];
        out[j++] = cmd_ack_is_ctx_char_safe(c) ? c : '_';
    }
    out[j] = '\0';
}

static inline void cmd_ack_emit(i32 seq, const cmd_exec_result_t *result)
{
    if (result == NULL) {
        sys_log_text(terminal, "{cmd_ack} seq=%ld result=%s",
                     (long)seq,
                     error_code_name(EXIT_UNKNOWN));
        return;
    }

    if (result->ctx != NULL && result->ctx[0] != '\0') {
        char safe_ctx[CMD_ACK_CTX_MAX_LEN + 1] = {0};
        cmd_ack_sanitize_ctx(result->ctx, safe_ctx);
        if (safe_ctx[0] != '\0') {
            sys_log_text(terminal, "{cmd_ack} seq=%ld result=%s ctx=%s",
                         (long)seq,
                         error_code_name(result->code),
                         safe_ctx);
            return;
        }
    }

    sys_log_text(terminal, "{cmd_ack} seq=%ld result=%s",
                 (long)seq,
                 error_code_name(result->code));
}

#ifdef __cplusplus
}
#endif

#endif // __CMD_ACK_H__

