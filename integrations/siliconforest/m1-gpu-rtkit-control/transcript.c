/* Host-only deterministic fake mailbox transcript executable. No transport. */
#include "control.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int number(const char *s, int base, uint64_t *out) {
    if (!s || !*s || *s == '-' || *s == '+') return 0;
    char *end; errno = 0;
    unsigned long long n = strtoull(s, &end, base);
    if (errno || *end) return 0;
    *out = (uint64_t)n; return 1;
}
int main(void) {
    struct rtc_context c;
    memset(&c, 0, sizeof(c));
    char line[192]; unsigned lines = 0, ended = 0;
    struct rtc_result final = {0};
    while (fgets(line, sizeof(line), stdin)) {
        ++lines;
        if (ended || lines > RTC_STEP_LIMIT || !strchr(line, '\n')) goto invalid;
        char *fields[6]; unsigned n = 0;
        for (char *p = strtok(line, " \t\r\n"); p; p = strtok(NULL, " \t\r\n")) {
            if (n == 6) goto invalid;
            fields[n++] = p;
        }
        if (!n) goto invalid;
        uint64_t now = 0, a = 0, b = 0;
        if (n < 2 || !number(fields[1], 10, &now)) goto invalid;
        enum rtc_code r = RTC_ARGUMENT;
        if (!strcmp(fields[0], "INIT") && n == 3) {
            if (!number(fields[2], 10, &a)) goto invalid;
            r = rtc_init(&c, now, a);
        } else if (!strcmp(fields[0], "TX") && n == 2) {
            struct rtc_packet packet;
            r = rtc_tx_peek(&c, now, &packet);
            if (!r) printf("TX sequence=%" PRIu64 " endpoint=%u word=%016" PRIx64 "\n",
                           packet.ticket.sequence, packet.endpoint, packet.word);
        } else if (!strcmp(fields[0], "COMMIT") && n == 4) {
            if (!number(fields[2], 10, &a)) goto invalid;
            enum rtc_delivery delivery;
            if (!strcmp(fields[3], "ACCEPTED")) delivery = RTC_ACCEPTED;
            else if (!strcmp(fields[3], "WOULD_BLOCK")) delivery = RTC_WOULD_BLOCK;
            else if (!strcmp(fields[3], "FAILED")) delivery = RTC_DELIVERY_FAILED;
            else if (!strcmp(fields[3], "UNCERTAIN")) delivery = RTC_DELIVERY_UNCERTAIN;
            else goto invalid;
            r = rtc_tx_commit(&c, now, (struct rtc_ticket){&c, a}, delivery);
        } else if (!strcmp(fields[0], "RX") && n == 4) {
            if (!number(fields[2], 10, &a) || a > UINT32_MAX || !number(fields[3], 16, &b)) goto invalid;
            r = rtc_receive(&c, now, (uint32_t)a, b);
        } else if (!strcmp(fields[0], "POLL") && n == 2) r = rtc_poll(&c, now);
        else if (!strcmp(fields[0], "END") && n == 2) {
            r = rtc_finish(&c, now, &final); ended = !r;
        } else goto invalid;
        if (r) {
            fprintf(stderr, "PROTOCOL_FAILED line=%u error=%s\n", lines, rtc_code_name(r));
            struct rtc_result diagnostic;
            if (!rtc_inspect(&c, &diagnostic) && diagnostic.buffer.outstanding)
                fprintf(stderr, "UNRESOLVED_BUFFER endpoint=%u raw=%016" PRIx64 " common=%u bytes=%" PRIu64 " address_field=%" PRIx64 "\n",
                        diagnostic.buffer.endpoint, diagnostic.buffer.raw_word, diagnostic.buffer.common_layout,
                        diagnostic.buffer.requested_bytes, diagnostic.buffer.address_field);
            return 1;
        }
    }
    if (ferror(stdin) || !ended) goto invalid;
    printf("CONTROL_DISCOVERED_PROTOCOL_ONLY version=%u accepted_tx=%u received=%u system_endpoints=%08x\n",
           final.negotiated_version, final.accepted_tx, final.received, final.required_system_endpoints);
    return fflush(stdout) == 0 ? 0 : 2;
invalid:
    fprintf(stderr, "TRANSCRIPT_FAILED line=%u\n", lines);
    return 2;
}
