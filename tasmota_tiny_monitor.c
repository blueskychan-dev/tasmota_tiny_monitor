/*
 * Tasmota Tiny-Monitor
 * Listens on :7270, fetches http://192.168.1.124/?m=1, parses values,
 * responds with JSON.
 *
 * Build: gcc -O2 -Wall -o tasmota_tiny_monitor tasmota_tiny_monitor.c -lcurl
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>

#define LISTEN_PORT 7270
#define UPSTREAM_URL "http://192.168.1.124/?m=1"
#define NAME_STR "Tasmota Tiny-Monitor"

typedef struct {
    char *data;
    size_t len;
} buf_t;

static void buf_init(buf_t *b) { b->data = NULL; b->len = 0; }

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t n = size * nmemb;
    buf_t *b = (buf_t *)userdata;
    char *p = realloc(b->data, b->len + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

static int fetch_upstream(buf_t *out) {
    CURL *c = curl_easy_init();
    if (!c) return -1;
    curl_easy_setopt(c, CURLOPT_URL, UPSTREAM_URL);
    curl_easy_setopt(c, CURLOPT_USERAGENT, NAME_STR "/1.0");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    CURLcode rc = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || http_code >= 400) return -2;
    return 0;
}

/* Helper: after a label (e.g., "Voltage"), find the next "style='text-align:left'>" and read until '<' */
static bool parse_value_after(const char *hay, const char *label, char *out, size_t outsz) {
    const char *p = strstr(hay, label);
    if (!p) return false;
    const char *q = strstr(p, "style='text-align:left'>");
    if (!q) return false;
    q += strlen("style='text-align:left'>");
    const char *end = strchr(q, '<');
    if (!end || end <= q) return false;
    size_t n = (size_t)(end - q);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, q, n);
    out[n] = '\0';
    return true;
}

/* Parse ON/OFF big label: find \"font-size:62px'>...<\" */
static bool parse_state(const char *hay, char *out, size_t outsz) {
    const char *p = strstr(hay, "font-size:62px'>");
    if (!p) return false;
    p += strlen("font-size:62px'>");
    const char *end = strchr(p, '<');
    if (!end || end <= p) return false;
    size_t n = (size_t)(end - p);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

/* Trim spaces and NBSP */
static void trim(char *s) {
    if (!s) return;
    // left trim
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == 0xC2 || s[i] == 0xA0) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    // right trim
    size_t len = strlen(s);
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

/* Convert string like "233" or "0.170" to double safely; returns 0 on success */
static int to_double(const char *s, double *out) {
    if (!s) return -1;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) return -1;
    *out = v;
    return 0;
}

static int make_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 16) < 0) { close(fd); return -1; }
    return fd;
}

static void send_http(int cfd, int code, const char *status, const char *ctype, const char *body) {
    char hdr[1024];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "Cache-Control: no-store\r\n"
                     "\r\n",
                     code, status, ctype, body ? strlen(body) : 0UL);
    send(cfd, hdr, n, 0);
    if (body && *body) send(cfd, body, strlen(body), 0);
}

static void handle_client(int cfd) {
    char req[2048];
    ssize_t r = recv(cfd, req, sizeof(req) - 1, 0);
    if (r <= 0) {
        close(cfd);
        return;
    }
    req[r] = '\0';

    // Very basic method/path check (always return same JSON for GET /)
    if (strncmp(req, "GET ", 4) != 0) {
        const char *msg = "{\"error\":\"method not allowed\"}";
        send_http(cfd, 405, "Method Not Allowed", "application/json", msg);
        close(cfd);
        return;
    }

    buf_t upstream; buf_init(&upstream);
    if (fetch_upstream(&upstream) != 0 || !upstream.data || upstream.len == 0) {
        const char *msg = "{\"error\":\"bad gateway\",\"detail\":\"fetch failed\"}";
        send_http(cfd, 502, "Bad Gateway", "application/json", msg);
        free(upstream.data);
        close(cfd);
        return;
    }

    // Parse fields
    char s_voltage[64] = {0}, s_current[64] = {0}, s_active[64] = {0}, s_apparent[64] = {0};
    char s_reactive[64] = {0}, s_pf[64] = {0}, s_etoday[64] = {0}, s_eyest[64] = {0}, s_etotal[64] = {0}, s_state[32]={0};

    bool ok = true;
    ok &= parse_value_after(upstream.data, "Voltage", s_voltage, sizeof(s_voltage));
    ok &= parse_value_after(upstream.data, "Current", s_current, sizeof(s_current));
    ok &= parse_value_after(upstream.data, "Active Power", s_active, sizeof(s_active));
    ok &= parse_value_after(upstream.data, "Apparent Power", s_apparent, sizeof(s_apparent));
    ok &= parse_value_after(upstream.data, "Reactive Power", s_reactive, sizeof(s_reactive));
    ok &= parse_value_after(upstream.data, "Power Factor", s_pf, sizeof(s_pf));
    ok &= parse_value_after(upstream.data, "Energy Today", s_etoday, sizeof(s_etoday));
    ok &= parse_value_after(upstream.data, "Energy Yesterday", s_eyest, sizeof(s_eyest));
    ok &= parse_value_after(upstream.data, "Energy Total", s_etotal, sizeof(s_etotal));
    parse_state(upstream.data, s_state, sizeof(s_state)); // state is optional

    trim(s_voltage); trim(s_current); trim(s_active); trim(s_apparent);
    trim(s_reactive); trim(s_pf); trim(s_etoday); trim(s_eyest); trim(s_etotal); trim(s_state);

    double voltage=0, current=0, active=0, apparent=0, reactive=0, pf=0, etoday=0, eyest=0, etotal=0;

    if (!ok ||
        to_double(s_voltage, &voltage) ||
        to_double(s_current, &current) ||
        to_double(s_active, &active) ||
        to_double(s_apparent, &apparent) ||
        to_double(s_reactive, &reactive) ||
        to_double(s_pf, &pf) ||
        to_double(s_etoday, &etoday) ||
        to_double(s_eyest, &eyest) ||
        to_double(s_etotal, &etotal)) {
        const char *msg = "{\"error\":\"parse failure\"}";
        send_http(cfd, 500, "Internal Server Error", "application/json", msg);
        free(upstream.data);
        close(cfd);
        return;
    }

    // Compose JSON
    char body[1024];
    int n = snprintf(
        body, sizeof(body),
        "{"
          "\"name\":\"%s\","
          "\"voltage\":%.3f,"
          "\"current\":%.3f,"
          "\"active_power\":%.3f,"
          "\"apparent_power\":%.3f,"
          "\"reactive_power\":%.3f,"
          "\"power_factor\":%.3f,"
          "\"energy_today_kwh\":%.3f,"
          "\"energy_yesterday_kwh\":%.3f,"
          "\"energy_total_kwh\":%.3f,"
          "\"state\":\"%s\","
          "\"source\":\"%s\""
        "}",
        NAME_STR,
        voltage, current, active, apparent, reactive, pf,
        etoday, eyest, etotal,
        (s_state[0] ? s_state : "UNKNOWN"),
        UPSTREAM_URL
    );
    if (n < 0 || (size_t)n >= sizeof(body)) {
        const char *msg = "{\"error\":\"serialization failure\"}";
        send_http(cfd, 500, "Internal Server Error", "application/json", msg);
    } else {
        send_http(cfd, 200, "OK", "application/json", body);
    }

    free(upstream.data);
    close(cfd);
}

static volatile sig_atomic_t keep_running = 1;
static void on_sigint(int sig) { (void)sig; keep_running = 0; }

int main(void) {
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "curl_global_init failed\n");
        return 1;
    }

    int sfd = make_server_socket(LISTEN_PORT);
    if (sfd < 0) {
        perror("listen socket");
        curl_global_cleanup();
        return 1;
    }
    printf("%s listening on :%d\n", NAME_STR, LISTEN_PORT);

    while (keep_running) {
        struct sockaddr_in cli; socklen_t clilen = sizeof(cli);
        int cfd = accept(sfd, (struct sockaddr *)&cli, &clilen);
        if (cfd < 0) {
            if (errno == EINTR) break;
            perror("accept");
            continue;
        }
        // simple, synchronous handling
        handle_client(cfd);
    }

    close(sfd);
    curl_global_cleanup();
    printf("bye\n");
    return 0;
}
