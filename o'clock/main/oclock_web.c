#include "oclock_web.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "oclock_album.h"
#include "oclock_network.h"
#include "oclock_todo.h"
#include "oclock_weather.h"

static const char *TAG = "oclock_web";
static httpd_handle_t g_server = NULL;

static void wifi_apply_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK_WITHOUT_ABORT(oclock_network_apply_saved_wifi());
    vTaskDelete(NULL);
}

static void weather_apply_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_ERROR_CHECK_WITHOUT_ABORT(oclock_weather_request_refresh());
    vTaskDelete(NULL);
}

static void album_apply_task(void *arg)
{
    (void)arg;
    vTaskDelete(NULL);
}

static esp_err_t send_chunkf(httpd_req_t *req, const char *fmt, ...)
{
    va_list args;
    va_list args_copy;
    int needed;
    char *buffer;
    esp_err_t err;

    va_start(args, fmt);
    va_copy(args_copy, args);
    needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (needed < 0) {
        va_end(args);
        return ESP_FAIL;
    }

    buffer = (char *)malloc((size_t)needed + 1U);
    if (buffer == NULL) {
        va_end(args);
        return ESP_ERR_NO_MEM;
    }

    vsnprintf(buffer, (size_t)needed + 1U, fmt, args);
    va_end(args);
    err = httpd_resp_sendstr_chunk(req, buffer);
    free(buffer);
    return err;
}

static esp_err_t send_html_escaped_chunk(httpd_req_t *req, const char *text)
{
    const char *source = text != NULL ? text : "";
    const size_t source_len = strlen(source);
    const size_t max_len = (source_len * 6U) + 1U;
    char *escaped = (char *)malloc(max_len);
    char *dst = escaped;

    if (escaped == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < source_len; ++i) {
        const unsigned char ch = (unsigned char)source[i];
        const char *replacement = NULL;

        switch (ch) {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '"':
            replacement = "&quot;";
            break;
        case '\'':
            replacement = "&#39;";
            break;
        default:
            break;
        }

        if (replacement != NULL) {
            const size_t replacement_len = strlen(replacement);
            memcpy(dst, replacement, replacement_len);
            dst += replacement_len;
        } else if (ch >= 0x20U || ch == '\n' || ch == '\r' || ch == '\t') {
            *dst++ = (char)ch;
        }
    }

    *dst = '\0';
    esp_err_t err = httpd_resp_sendstr_chunk(req, escaped);
    free(escaped);
    return err;
}

static void json_escape_copy(const char *src, char *dst, size_t dst_len)
{
    const char *source = src != NULL ? src : "";
    size_t write_index = 0;

    if (dst_len == 0U) {
        return;
    }

    for (size_t i = 0; source[i] != '\0' && write_index + 1U < dst_len; ++i) {
        const unsigned char ch = (unsigned char)source[i];
        const char *replacement = NULL;

        switch (ch) {
        case '\\':
            replacement = "\\\\";
            break;
        case '"':
            replacement = "\\\"";
            break;
        case '\b':
            replacement = "\\b";
            break;
        case '\f':
            replacement = "\\f";
            break;
        case '\n':
            replacement = "\\n";
            break;
        case '\r':
            replacement = "\\r";
            break;
        case '\t':
            replacement = "\\t";
            break;
        default:
            break;
        }

        if (replacement != NULL) {
            const size_t replacement_len = strlen(replacement);
            if (write_index + replacement_len >= dst_len) {
                break;
            }
            memcpy(dst + write_index, replacement, replacement_len);
            write_index += replacement_len;
        } else if (ch >= 0x20U) {
            dst[write_index++] = (char)ch;
        }
    }

    dst[write_index] = '\0';
}

static const char *todo_priority_web_label(uint8_t priority)
{
    switch (priority) {
    case OCLOCK_TODO_PRIORITY_HIGH:
        return "&#39640;";
    case OCLOCK_TODO_PRIORITY_LOW:
        return "&#20302;";
    case OCLOCK_TODO_PRIORITY_MEDIUM:
    default:
        return "&#20013;";
    }
}

static esp_err_t send_message_page(httpd_req_t *req,
                                   const char *title_html,
                                   const char *message_html)
{
    esp_err_t err = ESP_OK;

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = send_chunkf(
        req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>%s</title>"
        "<style>"
        "body{margin:0;font-family:'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;background:#f3efe4;color:#183832;"
        "display:grid;place-items:center;min-height:100vh;padding:20px}"
        ".card{width:min(520px,100%%);background:#fffdf7;border:1px solid #d9d2c5;border-radius:24px;padding:28px;box-shadow:0 16px 34px rgba(24,56,50,.08)}"
        "h1{margin:0 0 12px;font-size:28px}p{margin:0 0 20px;line-height:1.7;color:#6a6f68}"
        "a{display:inline-flex;padding:12px 16px;border-radius:14px;background:#1f5c53;color:#fff;text-decoration:none;font-weight:800}"
        "</style></head><body><div class='card'><h1>%s</h1><p>%s</p><a href='/'>返回首页</a></div></body></html>",
        title_html,
        title_html,
        message_html);
    if (err != ESP_OK) {
        return err;
    }

    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t redirect_home(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    oclock_network_status_t status = {0};
    oclock_todo_item_t items[OCLOCK_TODO_MAX_ITEMS];
    oclock_weather_location_t weather_location = {0};
    size_t count = 0;
    size_t open_count = 0;
    size_t completed_count = 0;
    const char *mode_text = "NONE";
    const char *wifi_text = "";
    const char *ntp_text = "";
    esp_err_t err = ESP_OK;

    err = oclock_network_get_status(&status);
    if (err != ESP_OK) {
        return err;
    }

    count = oclock_todo_list_all(items, OCLOCK_TODO_MAX_ITEMS);
    for (size_t i = 0; i < count; ++i) {
        if (items[i].completed) {
            completed_count++;
        } else {
            open_count++;
        }
    }

    mode_text = status.mode == OCLOCK_NETWORK_MODE_STA ? "STA" :
                status.mode == OCLOCK_NETWORK_MODE_AP ? "AP" : "NONE";
    wifi_text = status.wifi_connected ? "&#24050;&#36830;&#25509;" : "&#31163;&#32447;";
    ntp_text = status.ntp_synced ? "&#24050;&#26657;&#26102;" : "&#31561;&#24453;&#21516;&#27493;";
    (void)oclock_weather_get_location(&weather_location);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    err = send_chunkf(
        req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>O'Clock Control Console</title>"
        "<script src='https://unpkg.com/omggif@1.0.10/omggif.js'></script>"
        "<style>"
        ":root{--bg:#f3efe4;--panel:#fffdf7;--line:#d9d2c5;--line-soft:#ebe4d6;--ink:#183832;--muted:#6a6f68;"
        "--accent:#1f5c53;--accent-soft:#e8f0ed;--danger:#b85b49;--danger-soft:#f8e5df;--shadow:0 16px 34px rgba(24,56,50,.08)}"
        "*{box-sizing:border-box}html,body{margin:0}body{font-family:'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;"
        "color:var(--ink);background:radial-gradient(circle at top right,#fbfaf5 0,#f3efe4 44%%,#ebe4d6 100%%)}"
        ".shell{max-width:1220px;margin:0 auto;padding:24px 16px 40px}"
        ".hero{display:grid;grid-template-columns:minmax(0,1fr) 280px;gap:20px;align-items:start;background:var(--panel);"
        "border:1px solid var(--line);border-radius:28px;padding:24px 26px;box-shadow:var(--shadow)}"
        ".brand{display:flex;gap:16px;align-items:flex-start}.brand-mark{width:56px;height:56px;border-radius:18px;"
        "background:linear-gradient(145deg,#244f46,#17312c);position:relative;box-shadow:inset 0 1px 0 rgba(255,255,255,.12)}"
        ".brand-mark:before,.brand-mark:after{content:'';position:absolute;background:#f8f5eb;border-radius:999px}"
        ".brand-mark:before{width:24px;height:14px;left:16px;top:18px;transform:rotate(-18deg)}"
        ".brand-mark:after{width:10px;height:10px;left:29px;top:22px}"
        ".eyebrow{margin:0 0 6px;font-size:12px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}"
        "h1{margin:0;font-size:36px;line-height:1.04}.hero-copy{margin:10px 0 0;max-width:720px;color:var(--muted);line-height:1.7}"
        ".hero-tags{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}.tag{padding:8px 12px;border-radius:999px;background:#f7f2e8;border:1px solid var(--line);font-size:13px;font-weight:600;color:var(--muted)}"
        ".hero-side{display:grid;gap:12px}.mode-pill{display:inline-flex;align-self:start;justify-self:end;padding:9px 14px;border-radius:999px;background:var(--accent);color:#fff;font-weight:800;letter-spacing:.06em}"
        ".hero-metric{padding:14px 16px;border-radius:18px;border:1px solid var(--line);background:#fbf8f0}.hero-metric span{display:block;font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin-bottom:8px}"
        ".hero-metric strong{display:block;font-size:21px;line-height:1.2;word-break:break-word}"
        ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:14px;margin:18px 0 22px}"
        ".status-card{background:rgba(255,253,247,.94);border:1px solid var(--line);border-radius:18px;padding:16px 18px;box-shadow:var(--shadow)}"
        ".status-label{display:block;font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:10px}"
        ".status-value{display:block;font-size:24px;font-weight:800;line-height:1.1}.status-note{display:block;margin-top:6px;font-size:13px;color:var(--muted)}"
        ".layout{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:20px}.stack{display:grid;gap:20px}"
        ".panel{background:var(--panel);border:1px solid var(--line);border-radius:24px;padding:22px;box-shadow:var(--shadow)}"
        ".panel-head{display:flex;justify-content:space-between;align-items:flex-start;gap:14px;margin-bottom:18px}"
        ".panel-head h2{margin:0;font-size:24px;line-height:1.08}.subcopy{margin:6px 0 0;font-size:14px;color:var(--muted);line-height:1.65;max-width:640px}"
        ".pill{display:inline-flex;align-items:center;padding:8px 12px;border-radius:999px;background:var(--accent-soft);color:var(--accent);font-weight:700;font-size:13px;white-space:nowrap}"
        ".form-stack{display:grid;gap:12px}.split-2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}"
        ".field{display:grid;gap:6px;min-width:0}.field label{font-size:13px;font-weight:700;color:var(--muted)}"
        "input,select{width:100%%;padding:12px 14px;border-radius:14px;border:1px solid var(--line);background:#fff;color:var(--ink);font-size:15px}"
        "input:focus,select:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px rgba(31,92,83,.12)}"
        ".actions{display:flex;gap:10px;flex-wrap:wrap}.btn{appearance:none;border:0;border-radius:14px;padding:12px 16px;font-weight:800;cursor:pointer;font-size:14px;transition:transform .12s ease,box-shadow .12s ease}"
        ".btn:hover{transform:translateY(-1px)}.btn-primary{background:var(--accent);color:#fff;box-shadow:0 12px 24px rgba(31,92,83,.18)}"
        ".btn-secondary{background:#eef2ee;color:var(--ink)}.btn-danger{background:var(--danger-soft);color:var(--danger)}"
        ".hint{margin:0;font-size:12px;color:var(--muted);line-height:1.65}.divider{height:1px;background:var(--line-soft);margin:4px 0}"
        ".table-wrap{overflow:auto;border:1px solid var(--line);border-radius:20px;background:#fff}"
        "table{width:100%%;border-collapse:collapse}th,td{padding:14px;border-bottom:1px solid var(--line-soft);vertical-align:top}"
        "th{text-align:left;font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);background:#faf7f0}"
        "tr:last-child td{border-bottom:0}.todo-id{font-weight:800;color:var(--muted)}"
        ".editor-main{display:grid;gap:14px;max-width:430px}"
        ".editor-main input,.editor-main select{width:100%%}"
        ".action-cell{display:grid;gap:14px;min-width:0}"
        ".action-top{display:grid;gap:12px;justify-items:start}"
        ".action-buttons{display:flex;gap:10px;flex-wrap:wrap}"
        ".action-bottom{display:grid;gap:10px;justify-items:start;padding-top:8px}"
        ".action-bottom .btn{min-width:120px}"
        ".priority-note{font-size:12px;color:var(--muted);line-height:1.6}"
        ".compact label{font-size:12px}.compact input,.compact select{padding:10px 12px;border-radius:12px}"
        ".inline-actions{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.inline-actions form{margin:0}"
        ".state-badge{display:inline-flex;align-items:center;padding:7px 10px;border-radius:999px;font-size:12px;font-weight:800;letter-spacing:.04em}"
        ".state-open{background:#eef3ed;color:#36554f}.state-done{background:#edeaf8;color:#5f5090}"
        ".empty{padding:28px 18px;text-align:center;color:var(--muted)}"
        "@media (max-width:980px){.hero{grid-template-columns:1fr}.mode-pill{justify-self:start}.layout{grid-template-columns:1fr}}"
        "@media (max-width:1100px){.editor-main{max-width:none}}"
        "@media (max-width:760px){.shell{padding-inline:12px}.split-2{grid-template-columns:1fr}}"
        "</style></head><body><div class='shell'>"
        "<section class='hero'><div class='brand'><div class='brand-mark'></div><div>"
        "<p class='eyebrow'>ESP32-S3 RLCD Desk Terminal</p>"
        "<h1>O&#39;Clock &#32456;&#31471;&#25511;&#21046;&#21488;</h1>"
        "<p class='hero-copy'>&#22312;&#19968;&#20010;&#39029;&#38754;&#37324;&#23436;&#25104; Wi-Fi &#37197;&#32593;&#12289;&#22825;&#27668;&#22478;&#24066;&#35774;&#32622;&#12289;&#24453;&#21150;&#21516;&#27493;&#19982;&#30456;&#20876;&#19978;&#20256;&#65292;&#35753; ESP &#31471;&#26356;&#25509;&#36817;&#25104;&#21697;&#25511;&#21046;&#21488;&#30340;&#20307;&#39564;&#12290;</p>"
        "<div class='hero-tags'><span class='tag'>RLCD 4.2</span><span class='tag'>ESP-IDF</span><span class='tag'>&#24453;&#21150;&#21516;&#27493;</span><span class='tag'>&#22825;&#27668;&#25289;&#21462;</span><span class='tag'>&#30456;&#20876;&#19978;&#20256;</span></div>"
        "</div></div><div class='hero-side'>"
        "<div class='mode-pill' id='modePill'>%s</div>"
        "<div class='hero-metric'><span>IP Address</span><strong id='statusIpHero'>%s</strong></div>"
        "<div class='hero-metric'><span>&#24050;&#36830;&#25509; SSID</span><strong id='statusSsidHero'>",
        mode_text,
        status.ip_address[0] ? status.ip_address : "-");
    if (err != ESP_OK) {
        return err;
    }

    err = send_html_escaped_chunk(req, status.sta_ssid[0] ? status.sta_ssid : "-");
    if (err != ESP_OK) {
        return err;
    }

    err = send_chunkf(
        req,
        "</strong></div></div></section>"
        "<section class='status-grid'>"
        "<article class='status-card'><span class='status-label'>&#27169;&#24335;</span><strong class='status-value' id='statusMode'>%s</strong><span class='status-note'>&#24403;&#21069;&#32593;&#32476;&#35282;&#33394;</span></article>"
        "<article class='status-card'><span class='status-label'>Wi-Fi</span><strong class='status-value' id='statusWifi'>%s</strong><span class='status-note'>&#31449;&#28857;&#36830;&#25509;&#29366;&#24577;</span></article>"
        "<article class='status-card'><span class='status-label'>NTP</span><strong class='status-value' id='statusNtp'>%s</strong><span class='status-note'>&#26102;&#38047;&#21516;&#27493;&#29366;&#24577;</span></article>"
        "<article class='status-card'><span class='status-label'>&#24453;&#21150;</span><strong class='status-value' id='statusOpen'>%lu</strong><span class='status-note'>&#20173;&#38656;&#22788;&#29702;</span></article>"
        "<article class='status-card'><span class='status-label'>&#24050;&#23436;&#25104;</span><strong class='status-value' id='statusCompleted'>%lu</strong><span class='status-note'>&#24050;&#32463;&#23436;&#25104;</span></article>"
        "</section><div class='layout'><div class='stack'>",
        mode_text,
        wifi_text,
        ntp_text,
        (unsigned long)open_count,
        (unsigned long)completed_count);
    if (err != ESP_OK) {
        return err;
    }

    err = send_chunkf(
        req,
        "<section class='panel'><div class='panel-head'><div><p class='eyebrow'>Connectivity</p><h2>&#32593;&#32476;&#19982;&#22825;&#27668;</h2>"
        "<p class='subcopy'>&#20462;&#25913;&#28909;&#28857;&#20973;&#35777;&#12289;&#20999;&#22238; AP &#37197;&#32593;&#27169;&#24335;&#65292;&#24182;&#20026;&#35774;&#22791;&#31471;&#33258;&#21160;&#21047;&#26032;&#20934;&#22791;&#22825;&#27668;&#22478;&#24066;&#12290;</p>"
        "</div><span class='pill' id='apBadge'>&#37197;&#32593; AP&#65306;");
    if (err != ESP_OK) {
        return err;
    }

    err = send_html_escaped_chunk(req, status.ap_ssid[0] ? status.ap_ssid : "-");
    if (err != ESP_OK) {
        return err;
    }

    err = send_chunkf(
        req,
        "</span></div>"
        "<form method='post' action='/wifi' class='form-stack'>"
        "<div class='split-2'>"
        "<div class='field'><label>Wi-Fi SSID</label><input name='ssid' maxlength='32' value='");
    if (err != ESP_OK) {
        return err;
    }

    err = send_html_escaped_chunk(req, status.sta_ssid);
    if (err != ESP_OK) {
        return err;
    }

    err = send_chunkf(
        req,
        "'></div>"
        "<div class='field'><label>&#23494;&#30721;</label><input name='password' type='password' maxlength='64' value='' placeholder='&#36755;&#20837;&#23494;&#30721;&#21518;&#20445;&#23384;'></div>"
        "</div><div class='actions'><button class='btn btn-primary' type='submit'>&#20445;&#23384; Wi-Fi</button></div></form>"
        "<div class='divider'></div>"
        "<div class='actions' style='margin-bottom:14px'><form method='post' action='/ap'><button class='btn btn-secondary' type='submit'>&#37325;&#21551; AP &#37197;&#32593;</button></form></div>"
        "<form method='post' action='/weather' class='form-stack'><div class='field'><label>&#22825;&#27668;&#22478;&#24066;</label><input name='city' maxlength='47' value='");
    if (err != ESP_OK) {
        return err;
    }

    err = send_html_escaped_chunk(req, weather_location.city_name);
    if (err != ESP_OK) {
        return err;
    }

    err = send_chunkf(
        req,
        "'></div><div class='actions'><button class='btn btn-primary' type='submit'>&#20445;&#23384;&#22825;&#27668;&#22478;&#24066;</button></div></form></section>");
    if (err != ESP_OK) {
        return err;
    }

    err = send_chunkf(
        req,
        "<section class='panel'><div class='panel-head'><div><p class='eyebrow'>Album Studio</p><h2>&#30456;&#20876;&#19978;&#20256;</h2>"
        "<p class='subcopy'>&#19978;&#20256;&#21333;&#24352;&#22270;&#12289;PNG &#24207;&#21015;&#25110; GIF&#65292;&#27983;&#35272;&#22120;&#20250;&#20808;&#36716;&#25104; 1-bit &#24087;&#25968;&#25454;&#65292;&#20877;&#21457;&#32473;&#35774;&#22791;&#31471;&#30340;&#30456;&#20876;&#39029;&#12290;</p>"
        "</div><span class='pill'>400 x 300 &#30011;&#24067;</span></div>"
        "<div class='form-stack'>"
        "<div class='field'><label>&#22270;&#29255; / GIF &#25991;&#20214;</label><input id='albumFiles' type='file' accept='image/*,.gif' multiple></div>"
        "<div class='field'><label>&#24087;&#38388;&#38548;&#65288;ms&#65289;</label><input id='albumInterval' type='number' value='1200' min='200' max='5000'></div>"
        "<div class='actions'><button class='btn btn-primary' type='button' onclick='uploadAlbum()'>&#19978;&#20256;&#30456;&#20876;</button>"
        "<form method='post' action='/album/clear'><button class='btn btn-danger' type='submit'>&#28165;&#31354;&#30456;&#20876;</button></form></div>"
        "<p class='hint'>&#25552;&#31034;&#65306;&#22914;&#26524; GIF &#22312;&#24403;&#21069;&#27983;&#35272;&#22120;&#37324;&#20173;&#28982;&#21482;&#33021;&#35299;&#20986;&#39318;&#24087;&#65292;&#21487;&#20197;&#25913;&#20026;&#19978;&#20256; PNG &#24207;&#21015;&#24103;&#26469;&#33719;&#24471;&#26356;&#31283;&#23450;&#30340;&#25773;&#25918;&#25928;&#26524;&#12290;</p>"
        "</div></section>"
        "</div><div class='stack'>"
        "<section class='panel'><div class='panel-head'><div><p class='eyebrow'>Task Creator</p><h2>&#26032;&#24314;&#24453;&#21150;</h2>"
        "<p class='subcopy'>&#22312;&#36825;&#37324;&#26032;&#22686;&#24453;&#21150;&#39033;&#65292;&#25552;&#20132;&#21518;&#20250;&#21516;&#27493;&#21040;&#35774;&#22791;&#31471;&#30340;&#24453;&#21150;&#21015;&#34920;&#21644;&#39318;&#39029;&#27010;&#35272;&#12290;</p>"
        "</div></div>"
        "<form method='post' action='/todos/create' class='form-stack'>"
        "<div class='field'><label>&#26631;&#39064;</label><input name='title' maxlength='63' required></div>"
        "<div class='split-2'>"
        "<div class='field'><label>&#25130;&#27490;&#26085;&#26399;</label><input name='due_date' type='date' required></div>"
        "<div class='field'><label>&#20248;&#20808;&#32423;</label><select name='priority'>"
        "<option value='high'>&#39640;</option><option value='medium' selected>&#20013;</option><option value='low'>&#20302;</option>"
        "</select></div></div>"
        "<div class='actions'><button class='btn btn-primary' type='submit'>&#28155;&#21152;&#24453;&#21150;</button></div></form></section>");
    if (err != ESP_OK) {
        return err;
    }

    err = send_chunkf(
        req,
        "<section class='panel'><div class='panel-head'><div><p class='eyebrow'>Task Manager</p><h2>&#24453;&#21150;&#31649;&#29702;</h2>"
        "<p class='subcopy'>&#22312;&#19968;&#20010;&#21306;&#22495;&#37324;&#32534;&#36753;&#26631;&#39064;&#12289;&#25130;&#27490;&#26085;&#26399;&#12289;&#20248;&#20808;&#32423;&#21644;&#29366;&#24577;&#65292;&#20445;&#23384;&#21518;&#23631;&#24149;&#20250;&#33258;&#21160;&#21516;&#27493;&#26368;&#26032;&#20869;&#23481;&#12290;</p>"
        "</div><span class='pill'>&#24453;&#21150; %lu / &#24050;&#23436;&#25104; %lu</span></div>"
        "<div class='table-wrap'><table><tr><th style='width:80px'>ID</th><th>&#20869;&#23481;&#32534;&#36753;</th><th style='width:220px'>&#29366;&#24577;&#19982;&#25805;&#20316;</th></tr>",
        (unsigned long)open_count,
        (unsigned long)completed_count);
    if (err != ESP_OK) {
        return err;
    }

    if (count == 0U) {
        err = send_chunkf(
            req,
            "<tr><td colspan='3'><div class='empty'>&#26242;&#26080;&#24453;&#21150;&#39033;&#65292;&#21487;&#20197;&#20808;&#22312;&#19978;&#38754;&#26032;&#24314;&#19968;&#26465;&#12290;</div></td></tr>");
        if (err != ESP_OK) {
            return err;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        char due_text[20] = {0};
        char form_id[24];

        if (items[i].due_year > 0U) {
            snprintf(due_text,
                     sizeof(due_text),
                     "%04u-%02u-%02u",
                     items[i].due_year,
                     items[i].due_month,
                     items[i].due_day);
        }
        snprintf(form_id, sizeof(form_id), "todo-update-%lu", (unsigned long)items[i].id);

        err = send_chunkf(
            req,
            "<tr><td class='todo-id'>#%lu</td><td><form id='%s' class='editor-main' method='post' action='/todos/update'>"
            "<input type='hidden' name='id' value='%lu'>"
            "<div class='field compact'><label>&#26631;&#39064;</label><input name='title' maxlength='63' required value='",
            (unsigned long)items[i].id,
            form_id,
            (unsigned long)items[i].id);
        if (err != ESP_OK) {
            return err;
        }

        err = send_html_escaped_chunk(req, items[i].title);
        if (err != ESP_OK) {
            return err;
        }

        err = send_chunkf(
            req,
            "'></div>"
            "<div class='field compact'><label>&#25130;&#27490;&#26085;&#26399;</label><input name='due_date' type='date' required value='%s'></div>"
            "<div class='field compact'><label>&#20248;&#20808;&#32423;</label><select name='priority'>"
            "<option value='high'%s>&#39640;</option>"
            "<option value='medium'%s>&#20013;</option>"
            "<option value='low'%s>&#20302;</option>"
            "</select></div></form></td>"
            "<td><div class='action-cell'><div class='action-top'><span class='state-badge %s'>%s</span>"
            "<div class='action-buttons'>"
            "<form method='post' action='/todos/complete'><input type='hidden' name='id' value='%lu'>"
            "<input type='hidden' name='completed' value='%d'>"
            "<button class='btn btn-primary' type='submit'>%s</button></form>"
            "<form method='post' action='/todos/delete'><input type='hidden' name='id' value='%lu'>"
            "<button class='btn btn-danger' type='submit'>&#21024;&#38500;</button></form></div></div>"
            "<div class='action-bottom'><button class='btn btn-secondary' type='submit' form='%s'>&#20445;&#23384;</button>"
            "<div class='priority-note'>&#24403;&#21069;&#20248;&#20808;&#32423;&#65306;%s</div></div></div></td></tr>",
            due_text,
            items[i].priority == OCLOCK_TODO_PRIORITY_HIGH ? " selected" : "",
            items[i].priority == OCLOCK_TODO_PRIORITY_MEDIUM ? " selected" : "",
            items[i].priority == OCLOCK_TODO_PRIORITY_LOW ? " selected" : "",
            items[i].completed ? "state-done" : "state-open",
            items[i].completed ? "&#24050;&#23436;&#25104;" : "&#24453;&#21150;&#20013;",
            (unsigned long)items[i].id,
            items[i].completed ? 0 : 1,
            items[i].completed ? "&#37325;&#26032;&#25171;&#24320;" : "&#26631;&#35760;&#23436;&#25104;",
            (unsigned long)items[i].id,
            form_id,
            todo_priority_web_label(items[i].priority));
        if (err != ESP_OK) {
            return err;
        }
    }

    err = send_chunkf(
        req,
        "</table></div></section></div></div>"
        "<script>"
        "window.refreshStatus=async function(){"
        "try{const res=await fetch('/status',{cache:'no-store'});if(!res.ok)return;const s=await res.json();"
        "const mode=(s.mode||'none').toUpperCase();"
        "document.getElementById('statusMode').textContent=mode;"
        "document.getElementById('modePill').textContent=mode;"
        "document.getElementById('statusIpHero').textContent=s.ip||'-';"
        "document.getElementById('statusSsidHero').textContent=s.sta_ssid||'-';"
        "document.getElementById('statusWifi').textContent=s.wifi_connected?'已连接':'离线';"
        "document.getElementById('statusNtp').textContent=s.ntp_synced?'已校时':'等待同步';"
        "document.getElementById('statusOpen').textContent=String(s.open_todos||0);"
        "document.getElementById('statusCompleted').textContent=String(s.completed_todos||0);"
        "document.getElementById('apBadge').textContent='配网 AP：'+(s.ap_ssid||'-');"
        "}catch(_e){}};"
        "window.setInterval(window.refreshStatus,4000);"
        "window.addEventListener('load',window.refreshStatus);"
        "window.bitmapToFrameHex=async function(bitmap,w,h){"
        "const c=document.createElement('canvas');c.width=w;c.height=h;"
        "const ctx=c.getContext('2d',{willReadFrequently:true});"
        "ctx.fillStyle='white';ctx.fillRect(0,0,w,h);ctx.drawImage(bitmap,0,0,w,h);"
        "const d=ctx.getImageData(0,0,w,h).data;let bits='';"
        "for(let i=0;i<d.length;i+=4){const gray=(d[i]+d[i+1]+d[i+2])/3;bits+=gray<150?'1':'0';}"
        "let hex='';for(let i=0;i<bits.length;i+=8){const byte=bits.slice(i,i+8).padEnd(8,'0');hex+=parseInt(byte,2).toString(16).padStart(2,'0');}"
        "if(bitmap.close)bitmap.close();return hex;};"
        "window.fileToFrame=async function(file,w,h){const bmp=await createImageBitmap(file);return window.bitmapToFrameHex(bmp,w,h);};"
        "window.gifToFrames=async function(file,w,h,maxFrames){"
        "if(typeof ImageDecoder==='undefined'){return window.gifToFramesFallback(file,w,h,maxFrames);}"
        "const data=await file.arrayBuffer();const decoder=new ImageDecoder({data,type:file.type||'image/gif'});"
        "await decoder.tracks.ready;const track=decoder.tracks.selectedTrack;const count=Math.min(track.frameCount||1,maxFrames);const frames=[];"
        "for(let i=0;i<count;i++){const result=await decoder.decode({frameIndex:i});frames.push(await window.bitmapToFrameHex(result.image,w,h));}"
        "return frames;};"
        "window.gifToFramesFallback=async function(file,w,h,maxFrames){"
        "const GifReader=window.GifReader||(window.omggif&&window.omggif.GifReader);if(!GifReader){throw new Error('GIF decoder library not loaded');}"
        "const data=new Uint8Array(await file.arrayBuffer());const reader=new GifReader(data);const srcW=reader.width,srcH=reader.height;"
        "const frameCount=Math.min(reader.numFrames(),maxFrames);const rgba=new Uint8ClampedArray(srcW*srcH*4);"
        "const srcCanvas=document.createElement('canvas');srcCanvas.width=srcW;srcCanvas.height=srcH;"
        "const srcCtx=srcCanvas.getContext('2d',{willReadFrequently:true});"
        "const dstCanvas=document.createElement('canvas');dstCanvas.width=w;dstCanvas.height=h;"
        "const dstCtx=dstCanvas.getContext('2d',{willReadFrequently:true});const frames=[];"
        "for(let i=0;i<frameCount;i++){reader.decodeAndBlitFrameRGBA(i,rgba);const imgData=new ImageData(rgba,srcW,srcH);"
        "srcCtx.putImageData(imgData,0,0);dstCtx.fillStyle='white';dstCtx.fillRect(0,0,w,h);dstCtx.drawImage(srcCanvas,0,0,w,h);"
        "const d=dstCtx.getImageData(0,0,w,h).data;let bits='';for(let p=0;p<d.length;p+=4){const gray=(d[p]+d[p+1]+d[p+2])/3;bits+=gray<150?'1':'0';}"
        "let hex='';for(let p=0;p<bits.length;p+=8){const byte=bits.slice(p,p+8).padEnd(8,'0');hex+=parseInt(byte,2).toString(16).padStart(2,'0');}frames.push(hex);}"
        "return frames;};"
        "window.filesToFrames=async function(files,w,h,maxFrames){"
        "if(files.length===1&&(((files[0].type||'').includes('gif'))||files[0].name.toLowerCase().endsWith('.gif'))){return window.gifToFrames(files[0],w,h,maxFrames);}"
        "const frames=[];for(const f of files){if(frames.length>=maxFrames)break;frames.push(await window.fileToFrame(f,w,h));}return frames;};"
        "window.uploadAlbum=async function(){"
        "const files=[...document.getElementById('albumFiles').files];"
        "if(!files.length){alert('请选择至少一张图片');return;}"
        "const interval=parseInt(document.getElementById('albumInterval').value||'1200',10);let frames=[];"
        "try{frames=await window.filesToFrames(files,400,300,8);}catch(err){alert(err&&err.message?err.message:'相册预处理失败');return;}"
        "const payload={width:400,height:300,interval_ms:interval,frames};"
        "const res=await fetch('/album/upload',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});"
        "if(res.ok){location.reload();}else{alert('相册上传失败');}};"
        "</script></body></html>");
    if (err != ESP_OK) {
        return err;
    }

    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    oclock_network_status_t status = {0};
    oclock_todo_item_t items[OCLOCK_TODO_MAX_ITEMS];
    char body[512];
    char ip_json[48];
    char ap_json[96];
    char sta_json[96];
    size_t count = 0;
    size_t open_count = 0;
    size_t completed_count = 0;

    ESP_RETURN_ON_ERROR(oclock_network_get_status(&status), TAG, "status fetch failed");

    count = oclock_todo_list_all(items, OCLOCK_TODO_MAX_ITEMS);
    for (size_t i = 0; i < count; ++i) {
        if (items[i].completed) {
            completed_count++;
        } else {
            open_count++;
        }
    }

    json_escape_copy(status.ip_address, ip_json, sizeof(ip_json));
    json_escape_copy(status.ap_ssid, ap_json, sizeof(ap_json));
    json_escape_copy(status.sta_ssid, sta_json, sizeof(sta_json));

    snprintf(body,
             sizeof(body),
             "{\"mode\":\"%s\",\"wifi_connected\":%s,\"ap_active\":%s,\"ntp_synced\":%s,"
             "\"ip\":\"%s\",\"ap_ssid\":\"%s\",\"sta_ssid\":\"%s\","
             "\"open_todos\":%lu,\"completed_todos\":%lu}",
             status.mode == OCLOCK_NETWORK_MODE_STA ? "sta" :
             status.mode == OCLOCK_NETWORK_MODE_AP ? "ap" : "none",
             status.wifi_connected ? "true" : "false",
             status.ap_active ? "true" : "false",
             status.ntp_synced ? "true" : "false",
             ip_json,
             ap_json,
             sta_json,
             (unsigned long)open_count,
             (unsigned long)completed_count);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static void url_decode_inplace(char *text)
{
    char *src = text;
    char *dst = text;

    while (*src != '\0') {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] != '\0' && src[2] != '\0') {
            int hi = hex_value(src[1]);
            int lo = hex_value(src[2]);
            if (hi >= 0 && lo >= 0) {
                *dst++ = (char)((hi << 4) | lo);
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

static void form_get_value(const char *body,
                           const char *key,
                           char *out,
                           size_t out_len)
{
    const char *start = strstr(body, key);
    if (start == NULL) {
        out[0] = '\0';
        return;
    }

    start += strlen(key);
    const char *end = strchr(start, '&');
    if (end == NULL) {
        end = body + strlen(body);
    }

    size_t len = (size_t)(end - start);
    if (len >= out_len) {
        len = out_len - 1U;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    url_decode_inplace(out);
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    char body[192] = {0};
    char ssid[33] = {0};
    char password[65] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);

    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    }
    body[received] = '\0';

    form_get_value(body, "ssid=", ssid, sizeof(ssid));
    form_get_value(body, "password=", password, sizeof(password));

    if (ssid[0] == '\0') {
        return send_message_page(req,
                                 "Wi-Fi 保存失败",
                                 "SSID 不能为空，请返回首页后重新填写。");
    }

    esp_err_t err = oclock_network_set_wifi_credentials(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save wifi failed: %s", esp_err_to_name(err));
        return send_message_page(req,
                                 "Wi-Fi 保存失败",
                                 "设备未能保存 Wi-Fi 凭据，请检查输入内容后重试。");
    }
    xTaskCreate(wifi_apply_task, "wifi_apply_task", 4096, NULL, 4, NULL);
    return redirect_home(req);
}

static esp_err_t ap_post_handler(httpd_req_t *req)
{
    esp_err_t err = oclock_network_force_ap_mode();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "force AP failed: %s", esp_err_to_name(err));
        return send_message_page(req,
                                 "AP 模式切换失败",
                                 "设备未能切换回配网 AP 模式，请稍后重试。");
    }
    return redirect_home(req);
}

static esp_err_t weather_post_handler(httpd_req_t *req)
{
    char body[160] = {0};
    char city[48] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);

    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    }
    body[received] = '\0';

    form_get_value(body, "city=", city, sizeof(city));
    if (city[0] == '\0') {
        return send_message_page(req,
                                 "天气城市保存失败",
                                 "城市名称不能为空，请返回首页后重新填写。");
    }

    esp_err_t err = oclock_weather_set_location(city);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save weather failed: %s", esp_err_to_name(err));
        return send_message_page(req,
                                 "天气城市保存失败",
                                 "设备未能保存天气城市，请检查输入内容后重试。");
    }
    xTaskCreate(weather_apply_task, "weather_apply_task", 4096, NULL, 4, NULL);
    return redirect_home(req);
}

static esp_err_t album_upload_post_handler(httpd_req_t *req)
{
    char *body = (char *)calloc(1, (size_t)req->content_len + 1U);
    if (body == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    }

    int total_received = 0;
    while (total_received < req->content_len) {
        int received = httpd_req_recv(req,
                                      body + total_received,
                                      req->content_len - total_received);
        if (received <= 0) {
            free(body);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        }
        total_received += received;
    }
    body[total_received] = '\0';

    esp_err_t err = oclock_album_store_from_json(body);
    free(body);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "album store failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Album store failed");
    }

    xTaskCreate(album_apply_task, "album_apply_task", 2048, NULL, 4, NULL);
    return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t album_clear_post_handler(httpd_req_t *req)
{
    esp_err_t err = oclock_album_clear();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear album failed: %s", esp_err_to_name(err));
        return send_message_page(req,
                                 "清空相册失败",
                                 "设备未能清空相册内容，请稍后重试。");
    }
    return redirect_home(req);
}

static esp_err_t todo_create_post_handler(httpd_req_t *req)
{
    char body[256] = {0};
    char title[OCLOCK_TODO_TITLE_MAX_LEN] = {0};
    char due_date[16] = {0};
    char priority[16] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);

    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    }
    body[received] = '\0';

    form_get_value(body, "title=", title, sizeof(title));
    form_get_value(body, "due_date=", due_date, sizeof(due_date));
    form_get_value(body, "priority=", priority, sizeof(priority));

    esp_err_t err = oclock_todo_create(title, due_date, priority);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "create todo failed: %s", esp_err_to_name(err));
        return send_message_page(req,
                                 "添加待办失败",
                                 "请确认标题和截止日期都已经填写，且待办数量未超过上限。");
    }
    return redirect_home(req);
}

static esp_err_t todo_update_post_handler(httpd_req_t *req)
{
    char body[256] = {0};
    char title[OCLOCK_TODO_TITLE_MAX_LEN] = {0};
    char due_date[16] = {0};
    char priority[16] = {0};
    char id_text[16] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);

    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    }
    body[received] = '\0';

    form_get_value(body, "id=", id_text, sizeof(id_text));
    form_get_value(body, "title=", title, sizeof(title));
    form_get_value(body, "due_date=", due_date, sizeof(due_date));
    form_get_value(body, "priority=", priority, sizeof(priority));

    esp_err_t err = oclock_todo_update((uint32_t)strtoul(id_text, NULL, 10),
                                       title,
                                       due_date,
                                       priority);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update todo failed: %s", esp_err_to_name(err));
        return send_message_page(req,
                                 "更新待办失败",
                                 "请确认标题和截止日期格式正确后再重试。");
    }
    return redirect_home(req);
}

static esp_err_t todo_complete_post_handler(httpd_req_t *req)
{
    char body[128] = {0};
    char id_text[16] = {0};
    char completed_text[8] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);

    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    }
    body[received] = '\0';

    form_get_value(body, "id=", id_text, sizeof(id_text));
    form_get_value(body, "completed=", completed_text, sizeof(completed_text));

    esp_err_t err = oclock_todo_set_completed((uint32_t)strtoul(id_text, NULL, 10),
                                              atoi(completed_text) != 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "complete todo failed: %s", esp_err_to_name(err));
        return send_message_page(req,
                                 "切换待办状态失败",
                                 "设备未能更新该待办项的完成状态，请刷新页面后重试。");
    }
    return redirect_home(req);
}

static esp_err_t todo_delete_post_handler(httpd_req_t *req)
{
    char body[64] = {0};
    char id_text[16] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);

    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    }
    body[received] = '\0';

    form_get_value(body, "id=", id_text, sizeof(id_text));

    esp_err_t err = oclock_todo_delete((uint32_t)strtoul(id_text, NULL, 10));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "delete todo failed: %s", esp_err_to_name(err));
        return send_message_page(req,
                                 "删除待办失败",
                                 "设备未能删除该待办项，请刷新页面后重试。");
    }
    return redirect_home(req);
}

esp_err_t oclock_web_start(void)
{
    if (g_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 14;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    ESP_RETURN_ON_ERROR(httpd_start(&g_server, &config), TAG, "httpd_start failed");

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
    };
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
    };
    httpd_uri_t wifi_uri = {
        .uri = "/wifi",
        .method = HTTP_POST,
        .handler = wifi_post_handler,
    };
    httpd_uri_t ap_uri = {
        .uri = "/ap",
        .method = HTTP_POST,
        .handler = ap_post_handler,
    };
    httpd_uri_t weather_uri = {
        .uri = "/weather",
        .method = HTTP_POST,
        .handler = weather_post_handler,
    };
    httpd_uri_t album_upload_uri = {
        .uri = "/album/upload",
        .method = HTTP_POST,
        .handler = album_upload_post_handler,
    };
    httpd_uri_t album_clear_uri = {
        .uri = "/album/clear",
        .method = HTTP_POST,
        .handler = album_clear_post_handler,
    };
    httpd_uri_t todo_create_uri = {
        .uri = "/todos/create",
        .method = HTTP_POST,
        .handler = todo_create_post_handler,
    };
    httpd_uri_t todo_update_uri = {
        .uri = "/todos/update",
        .method = HTTP_POST,
        .handler = todo_update_post_handler,
    };
    httpd_uri_t todo_complete_uri = {
        .uri = "/todos/complete",
        .method = HTTP_POST,
        .handler = todo_complete_post_handler,
    };
    httpd_uri_t todo_delete_uri = {
        .uri = "/todos/delete",
        .method = HTTP_POST,
        .handler = todo_delete_post_handler,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &index_uri),
                        TAG,
                        "register index failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &status_uri),
                        TAG,
                        "register status failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_uri),
                        TAG,
                        "register wifi failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &ap_uri),
                        TAG,
                        "register ap failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &weather_uri),
                        TAG,
                        "register weather failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &album_upload_uri),
                        TAG,
                        "register album upload failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &album_clear_uri),
                        TAG,
                        "register album clear failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &todo_create_uri),
                        TAG,
                        "register todo create failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &todo_update_uri),
                        TAG,
                        "register todo update failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &todo_complete_uri),
                        TAG,
                        "register todo complete failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &todo_delete_uri),
                        TAG,
                        "register todo delete failed");

    return ESP_OK;
}
