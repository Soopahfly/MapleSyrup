/*
 * webconfig.c — WiFi AP HTTP config server for MapleSyrup
 *
 * When PIN_MODE_BTN is held at boot:
 *   1. CYW43 starts as a WiFi access point "MapleSyrup" (open, no password)
 *   2. A DHCP server assigns the connecting host an IP (192.168.7.16+)
 *   3. An HTTP server on port 80 serves the config UI at http://192.168.7.1
 *
 * API endpoints (JSON):
 *   GET  /            — serve embedded HTML UI
 *   GET  /api/config  — return current global config as JSON
 *   POST /api/config  — apply global config from JSON body
 *   GET  /api/games   — return all game configs as JSON array
 *   POST /api/games   — upsert or delete a game config
 *   POST /api/save    — write config to flash
 *   POST /api/reboot  — save + watchdog reboot
 *
 * Bluetooth is never initialised in config mode — no CYW43 arch conflict.
 */

#include "webconfig.h"
#include "config_store.h"
#include "controller.h"
#include "config.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/timeouts.h"

#include "dhcpserver.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ── Network config ─────────────────────────────────────────────────────────────
#define WIFI_SSID       "MapleSyrup"
#define WIFI_CHANNEL    6
#define HTTP_PORT       80
#define AP_IP_STR       "192.168.7.1"
#define AP_NM_STR       "255.255.255.0"

// ── Embedded HTML/JS UI ────────────────────────────────────────────────────────
// Served from flash — avoids any external file dependency.
// Uses fetch() to talk to /api/* — works in any browser on any OS.
static const char k_html[] =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>MapleSyrup Config</title>"
"<style>"
"body{font-family:monospace;max-width:900px;margin:40px auto;padding:0 20px;background:#1a1a1a;color:#e0e0e0}"
"h1{color:#f0a000}h2{color:#80c0ff;border-bottom:1px solid #444;padding-bottom:4px}"
"button{background:#333;color:#e0e0e0;border:1px solid #666;padding:6px 14px;cursor:pointer;margin:2px}"
"button:hover{background:#555}button.primary{background:#004080;border-color:#0080ff}"
"button.danger{background:#600;border-color:#f00}"
"input,select{background:#222;color:#e0e0e0;border:1px solid #555;padding:4px}"
"table{border-collapse:collapse;width:100%}td,th{border:1px solid #444;padding:6px 10px;text-align:left}"
"th{background:#2a2a2a}.status{padding:8px;background:#222;margin:8px 0;border-left:4px solid #f0a000}"
".ok{border-color:#0f0}.err{border-color:#f00}.section{background:#222;padding:16px;margin:12px 0;border-radius:4px}"
"label{display:inline-block;width:180px}"
"</style>"
"</head>"
"<body>"
"<h1>\xF0\x9F\x8D\x81 MapleSyrup Config</h1>"
"<div class='status' id='sts'>Loading config\xe2\x80\xa6</div>"
"<button class='primary' id='btn-save'>Save to Flash</button>"
"<button class='primary' id='btn-reboot'>Save &amp; Reboot</button>"
"<button id='btn-refresh'>Refresh</button>"
"<div class='section'><h2>Global Settings</h2>"
"<table>"
"<tr><th>Setting</th><th>Value</th><th>Notes</th></tr>"
"<tr><td><label>Deadzone inner (%)</label></td>"
"<td><input type='number' id='g-dz-inner' min='0' max='50' style='width:70px'></td>"
"<td>Stick &lt; this % = centre</td></tr>"
"<tr><td><label>Deadzone outer (%)</label></td>"
"<td><input type='number' id='g-dz-outer' min='51' max='100' style='width:70px'></td>"
"<td>Stick &gt; this % = full throw</td></tr>"
"<tr><td><label>Trigger threshold</label></td>"
"<td><input type='number' id='g-trig' min='0' max='255' style='width:70px'></td>"
"<td>Analogue trigger fires digital at this value (0\xe2\x80\x93255)</td></tr>"
"<tr><td><label>Rapid fire (Hz)</label></td>"
"<td><input type='number' id='g-rf-hz' min='1' max='30' style='width:70px'></td>"
"<td></td></tr>"
"<tr><td><label>Rapid fire mask</label></td>"
"<td><input type='text' id='g-rf-mask' style='width:70px'></td>"
"<td>DC button bitmask (C=1 B=2 A=4 Start=8 \xe2\x80\xa6)</td></tr>"
"<tr><td><label>Default mode</label></td>"
"<td><select id='g-mode'>"
"<option value='0'>0 \xe2\x80\x93 Standard</option>"
"<option value='1'>1 \xe2\x80\x93 Dual Analog</option>"
"<option value='2'>2 \xe2\x80\x93 Twin Stick</option>"
"<option value='3'>3 \xe2\x80\x93 Fight Stick</option>"
"<option value='4'>4 \xe2\x80\x93 Racing</option>"
"</select></td><td></td></tr>"
"<tr><td><label>Invert axes</label></td>"
"<td>LX<input type='checkbox' id='g-inv-lx'> "
"LY<input type='checkbox' id='g-inv-ly'> "
"RX<input type='checkbox' id='g-inv-rx'> "
"RY<input type='checkbox' id='g-inv-ry'></td><td></td></tr>"
"</table>"
"<button id='btn-apply-global'>Apply Global Settings</button>"
"</div>"
"<div class='section'><h2>Per-Game Button Remapping</h2>"
"<p style='color:#888;font-size:12px'>Each game is keyed by its GDEMU Game ID hash. "
"MapleSyrup auto-switches config when a game sends its ID over Maple.</p>"
"<div><label>Game hash (hex):</label>"
"<input type='text' id='game-hash-input' placeholder='e.g. A1B2C3D4' style='width:120px'>"
"<button id='btn-load-game'>Load / Create</button></div>"
"<div id='game-editor' style='display:none;margin-top:16px'>"
"<table>"
"<tr><th>Field</th><th>Value</th></tr>"
"<tr><td>Name</td><td><input type='text' id='ge-name' style='width:200px'></td></tr>"
"<tr><td>VMU Bank</td><td><select id='ge-bank'>"
"<option value='255'>Auto (hash-based)</option>"
"<option value='0'>0</option><option value='1'>1</option><option value='2'>2</option>"
"<option value='3'>3</option><option value='4'>4</option><option value='5'>5</option>"
"<option value='6'>6</option><option value='7'>7</option><option value='8'>8</option>"
"<option value='9'>9</option></select></td></tr>"
"<tr><td>Controller Mode</td><td><select id='ge-mode'>"
"<option value='255'>Default (global)</option>"
"<option value='0'>0 \xe2\x80\x93 Standard</option>"
"<option value='1'>1 \xe2\x80\x93 Dual Analog</option>"
"<option value='2'>2 \xe2\x80\x93 Twin Stick</option>"
"<option value='3'>3 \xe2\x80\x93 Fight Stick</option>"
"<option value='4'>4 \xe2\x80\x93 Racing</option>"
"</select></td></tr>"
"</table>"
"<h3 style='color:#80c0ff;margin-top:16px'>Button Remap</h3>"
"<p style='color:#888;font-size:12px'>Map each Dreamcast button to a source input. "
"255 = default mapping.</p>"
"<table id='remap-table'><tr><th>DC Button</th><th>Source</th></tr></table>"
"<div style='margin-top:10px'>"
"<button id='btn-apply-game'>Apply Game Config</button>"
"<button id='btn-del-game' class='danger'>Delete Game Config</button>"
"</div></div>"
"<h3 style='color:#80c0ff;margin-top:20px'>Stored Games</h3>"
"<div id='games-list'>\xe2\x80\x94 loading \xe2\x80\xa6</div>"
"</div>"
"<script>"
"const SRC=['A','B','X','Y','L_SHOULDER','R_SHOULDER','L_TRIGGER','R_TRIGGER',"
"'SELECT','START','DPAD_UP','DPAD_DOWN','DPAD_LEFT','DPAD_RIGHT'];"
"const DC=['C','B','A','START','UP','DOWN','LEFT','RIGHT','Z','Y','X','D'];"
"function sts(msg,cls=''){const e=document.getElementById('sts');e.textContent=msg;e.className='status '+cls;}"
"async function api(path,method='GET',body=null){"
"const opts={method,headers:{'Content-Type':'application/json'}};"
"if(body)opts.body=JSON.stringify(body);"
"const r=await fetch(path,opts);"
"if(!r.ok)throw new Error(await r.text());"
"return r.json();}"
"async function loadGlobal(){"
"const g=await api('/api/config');"
"document.getElementById('g-dz-inner').value=g.deadzone_inner;"
"document.getElementById('g-dz-outer').value=g.deadzone_outer;"
"document.getElementById('g-trig').value=g.trigger_threshold;"
"document.getElementById('g-rf-hz').value=g.rapid_fire_hz;"
"document.getElementById('g-rf-mask').value=g.rapid_fire_mask;"
"document.getElementById('g-mode').value=g.default_ctrl_mode;"
"document.getElementById('g-inv-lx').checked=!!g.invert_lx;"
"document.getElementById('g-inv-ly').checked=!!g.invert_ly;"
"document.getElementById('g-inv-rx').checked=!!g.invert_rx;"
"document.getElementById('g-inv-ry').checked=!!g.invert_ry;}"
"async function loadGames(){"
"const games=await api('/api/games');"
"const div=document.getElementById('games-list');"
"if(!games.length){div.textContent='(no game configs stored)';return;}"
"let h='<table><tr><th>Hash</th><th>Name</th><th>VMU</th><th>Mode</th><th></th></tr>';"
"for(const g of games){"
"h+=`<tr><td>${g.hash}</td><td>${g.name}</td><td>${g.vmu_bank===255?'auto':g.vmu_bank}</td>`;"
"h+=`<td>${g.ctrl_mode===255?'default':g.ctrl_mode}</td>`;"
"h+=`<td><button onclick='loadGame(\"${g.hash}\")'>Edit</button></td></tr>`;}"
"h+='</table>';div.innerHTML=h;}"
"async function refresh(){try{await loadGlobal();await loadGames();sts('Ready','ok');}catch(e){sts('Error: '+e,'err');}}"
"function buildRemapTable(remap){"
"const t=document.getElementById('remap-table');"
"while(t.rows.length>1)t.deleteRow(1);"
"for(const slot of DC){"
"const row=t.insertRow();row.insertCell().textContent=slot;"
"const sel=document.createElement('select');sel.id='remap-'+slot;"
"const dopt=document.createElement('option');dopt.value=255;dopt.textContent='default';sel.appendChild(dopt);"
"for(let i=0;i<SRC.length;i++){"
"const o=document.createElement('option');o.value=i;o.textContent=SRC[i];sel.appendChild(o);}"
"if(remap&&remap[slot]!==undefined)sel.value=remap[slot];"
"row.insertCell().appendChild(sel);}}"
"let curHash=null;"
"async function loadGame(hash){"
"document.getElementById('game-hash-input').value=hash;"
"try{"
"const g=await api('/api/games/'+hash);"
"document.getElementById('ge-name').value=g.name||'';"
"document.getElementById('ge-bank').value=g.vmu_bank;"
"document.getElementById('ge-mode').value=g.ctrl_mode;"
"buildRemapTable(g.remap);"
"}catch(e){document.getElementById('ge-name').value='';"
"document.getElementById('ge-bank').value=255;"
"document.getElementById('ge-mode').value=255;"
"buildRemapTable(null);}"
"document.getElementById('game-editor').style.display='block';"
"curHash=hash;}"
"document.getElementById('btn-refresh').addEventListener('click',refresh);"
"document.getElementById('btn-save').addEventListener('click',async()=>{"
"try{await api('/api/save','POST');sts('Saved to flash','ok');}catch(e){sts('Save failed: '+e,'err');}});"
"document.getElementById('btn-reboot').addEventListener('click',async()=>{"
"try{await api('/api/save','POST');sts('Rebooting\xe2\x80\xa6');fetch('/api/reboot',{method:'POST'}).catch(()=>{});}catch(e){sts('Error: '+e,'err');}});"
"document.getElementById('btn-apply-global').addEventListener('click',async()=>{"
"const body={"
"deadzone_inner:+document.getElementById('g-dz-inner').value,"
"deadzone_outer:+document.getElementById('g-dz-outer').value,"
"trigger_threshold:+document.getElementById('g-trig').value,"
"rapid_fire_hz:+document.getElementById('g-rf-hz').value,"
"rapid_fire_mask:+document.getElementById('g-rf-mask').value,"
"default_ctrl_mode:+document.getElementById('g-mode').value,"
"invert_lx:document.getElementById('g-inv-lx').checked?1:0,"
"invert_ly:document.getElementById('g-inv-ly').checked?1:0,"
"invert_rx:document.getElementById('g-inv-rx').checked?1:0,"
"invert_ry:document.getElementById('g-inv-ry').checked?1:0};"
"try{await api('/api/config','POST',body);sts('Global settings applied','ok');}catch(e){sts('Error: '+e,'err');}});"
"document.getElementById('btn-load-game').addEventListener('click',()=>{"
"const h=document.getElementById('game-hash-input').value.trim().toUpperCase();"
"if(h)loadGame(h);});"
"document.getElementById('btn-apply-game').addEventListener('click',async()=>{"
"if(!curHash)return;"
"const remap={};"
"for(const slot of DC){const sel=document.getElementById('remap-'+slot);if(sel)remap[slot]=+sel.value;}"
"const body={hash:curHash,name:document.getElementById('ge-name').value||'Game_'+curHash,"
"vmu_bank:+document.getElementById('ge-bank').value,"
"ctrl_mode:+document.getElementById('ge-mode').value,remap};"
"try{await api('/api/games','POST',body);sts('Game config applied','ok');await loadGames();}catch(e){sts('Error: '+e,'err');}});"
"document.getElementById('btn-del-game').addEventListener('click',async()=>{"
"if(!curHash)return;"
"try{await api('/api/games','POST',{hash:curHash,delete:true});document.getElementById('game-editor').style.display='none';"
"curHash=null;sts('Game deleted','ok');await loadGames();}catch(e){sts('Error: '+e,'err');}});"
"buildRemapTable(null);refresh();"
"</script>"
"</body></html>";

// ── HTTP connection state ──────────────────────────────────────────────────────
#define REQ_BUF_SIZE    1024
#define BODY_BUF_SIZE   1024

typedef struct {
    char    req[REQ_BUF_SIZE];
    int     req_len;
    char    body[BODY_BUF_SIZE];
    int     body_len;
    int     content_length;
    bool    headers_done;
    bool    close;
} http_conn_t;

// ── Minimal JSON helpers ───────────────────────────────────────────────────────
// Return integer value of key in flat JSON object (returns def if not found)
static int json_int(const char *json, const char *key, int def) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return def;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') { p++; } // skip string quotes for bool-as-string
    return (int)strtol(p, NULL, 10);
}

// Return 1/0 for a JSON boolean key
static int json_bool(const char *json, const char *key, int def) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return def;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p == 't') return 1;
    if (*p == 'f') return 0;
    return (int)strtol(p, NULL, 10);
}

static bool json_key_exists(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    return strstr(json, search) != NULL;
}

// Copy string value of key into out (max out_len-1 chars, NUL-terminated)
static void json_str(const char *json, const char *key, char *out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') { out[0] = '\0'; return; }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) out[i++] = *p++;
    out[i] = '\0';
}

// ── Response builders ─────────────────────────────────────────────────────────
static void send_str(struct tcp_pcb *pcb, const char *s) {
    tcp_write(pcb, s, (u16_t)strlen(s), TCP_WRITE_FLAG_COPY);
}

static void send_headers(struct tcp_pcb *pcb, int status, const char *ct, int body_len) {
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status == 200 ? "OK" : "Not Found", ct, body_len);
    send_str(pcb, hdr);
}

// ── Route handlers ─────────────────────────────────────────────────────────────
static void route_get_root(struct tcp_pcb *pcb) {
    int len = (int)strlen(k_html);
    send_headers(pcb, 200, "text/html; charset=utf-8", len);
    // Send in chunks to avoid exceeding tcp_write buffer
    const char *p = k_html;
    int rem = len;
    while (rem > 0) {
        int chunk = rem < 1024 ? rem : 1024;
        tcp_write(pcb, p, (u16_t)chunk, TCP_WRITE_FLAG_COPY);
        p   += chunk;
        rem -= chunk;
    }
}

static void route_get_config(struct tcp_pcb *pcb) {
    global_cfg_t *g = config_global();
    char body[256];
    snprintf(body, sizeof(body),
        "{\"deadzone_inner\":%u,\"deadzone_outer\":%u,"
        "\"trigger_threshold\":%u,\"rapid_fire_hz\":%u,"
        "\"rapid_fire_mask\":%u,\"default_ctrl_mode\":%u,"
        "\"invert_lx\":%u,\"invert_ly\":%u,"
        "\"invert_rx\":%u,\"invert_ry\":%u}",
        g->deadzone_inner, g->deadzone_outer,
        g->trigger_threshold, g->rapid_fire_hz,
        g->rapid_fire_mask, g->default_ctrl_mode,
        g->invert_lx, g->invert_ly,
        g->invert_rx, g->invert_ry);
    send_headers(pcb, 200, "application/json", (int)strlen(body));
    send_str(pcb, body);
}

static void route_post_config(struct tcp_pcb *pcb, const char *body) {
    global_cfg_t *g = config_global();
    int v;
    v = json_int(body, "deadzone_inner",    (int)g->deadzone_inner);
    if (v >= 0 && v <= 50)   g->deadzone_inner = (uint8_t)v;
    v = json_int(body, "deadzone_outer",    (int)g->deadzone_outer);
    if (v >= 51 && v <= 100) g->deadzone_outer = (uint8_t)v;
    v = json_int(body, "trigger_threshold", (int)g->trigger_threshold);
    if (v >= 0 && v <= 255)  g->trigger_threshold = (uint8_t)v;
    v = json_int(body, "rapid_fire_hz",     (int)g->rapid_fire_hz);
    if (v >= 1 && v <= 30)   g->rapid_fire_hz = (uint8_t)v;
    v = json_int(body, "rapid_fire_mask",   (int)g->rapid_fire_mask);
    g->rapid_fire_mask = (uint16_t)v;
    v = json_int(body, "default_ctrl_mode", (int)g->default_ctrl_mode);
    if (v >= 0 && v < (int)CTRL_MODE_COUNT) g->default_ctrl_mode = (uint8_t)v;
    g->invert_lx = (uint8_t)json_bool(body, "invert_lx", (int)g->invert_lx);
    g->invert_ly = (uint8_t)json_bool(body, "invert_ly", (int)g->invert_ly);
    g->invert_rx = (uint8_t)json_bool(body, "invert_rx", (int)g->invert_rx);
    g->invert_ry = (uint8_t)json_bool(body, "invert_ry", (int)g->invert_ry);
    const char *resp = "{\"ok\":true}";
    send_headers(pcb, 200, "application/json", (int)strlen(resp));
    send_str(pcb, resp);
}

static void route_get_games(struct tcp_pcb *pcb) {
    // Build JSON array of all stored game configs
    char body[2048];
    int pos = 0;
    body[pos++] = '[';
    bool first = true;
    for (int i = 0; i < MAX_GAME_CFGS; i++) {
        game_cfg_t *gc = config_game_slot((uint8_t)i);
        if (!(gc->flags & 1)) continue;
        if (!first) body[pos++] = ',';
        first = false;
        // remap array
        char remap_json[256];
        int rp = 0;
        remap_json[rp++] = '{';
        bool rf = true;
        for (int s = 0; s < DC_NUM_BUTTONS && rp < (int)sizeof(remap_json) - 20; s++) {
            if (!rf) remap_json[rp++] = ',';
            rf = false;
            rp += snprintf(remap_json + rp, sizeof(remap_json) - (size_t)rp,
                           "\"%s\":%u", k_dc_slot_names[s], (unsigned)gc->btn_remap[s]);
        }
        remap_json[rp++] = '}';
        remap_json[rp] = '\0';
        pos += snprintf(body + pos, sizeof(body) - (size_t)pos,
            "{\"hash\":\"%08lX\",\"name\":\"%s\","
            "\"vmu_bank\":%u,\"ctrl_mode\":%u,"
            "\"remap\":%s}",
            (unsigned long)gc->hash, gc->name,
            (unsigned)gc->vmu_bank, (unsigned)gc->ctrl_mode,
            remap_json);
    }
    body[pos++] = ']';
    body[pos]   = '\0';
    send_headers(pcb, 200, "application/json", pos);
    send_str(pcb, body);
}

static void route_get_game(struct tcp_pcb *pcb, const char *hash_str) {
    uint32_t hash = (uint32_t)strtoul(hash_str, NULL, 16);
    game_cfg_t *gc = config_game_by_hash(hash);
    if (!gc) {
        const char *e = "{\"error\":\"not found\"}";
        send_headers(pcb, 404, "application/json", (int)strlen(e));
        send_str(pcb, e);
        return;
    }
    char remap_json[256];
    int rp = 0;
    remap_json[rp++] = '{';
    bool rf = true;
    for (int s = 0; s < DC_NUM_BUTTONS && rp < (int)sizeof(remap_json) - 20; s++) {
        if (!rf) remap_json[rp++] = ',';
        rf = false;
        rp += snprintf(remap_json + rp, sizeof(remap_json) - (size_t)rp,
                       "\"%s\":%u", k_dc_slot_names[s], (unsigned)gc->btn_remap[s]);
    }
    remap_json[rp++] = '}';
    remap_json[rp] = '\0';
    char body[512];
    snprintf(body, sizeof(body),
        "{\"hash\":\"%08lX\",\"name\":\"%s\","
        "\"vmu_bank\":%u,\"ctrl_mode\":%u,"
        "\"remap\":%s}",
        (unsigned long)gc->hash, gc->name,
        (unsigned)gc->vmu_bank, (unsigned)gc->ctrl_mode,
        remap_json);
    send_headers(pcb, 200, "application/json", (int)strlen(body));
    send_str(pcb, body);
}

static void route_post_games(struct tcp_pcb *pcb, const char *body) {
    char hash_str[16];
    json_str(body, "hash", hash_str, sizeof(hash_str));
    uint32_t hash = (uint32_t)strtoul(hash_str, NULL, 16);

    // Delete?
    if (json_key_exists(body, "delete") && json_bool(body, "delete", 0)) {
        config_game_delete(hash);
        const char *resp = "{\"ok\":true}";
        send_headers(pcb, 200, "application/json", (int)strlen(resp));
        send_str(pcb, resp);
        return;
    }

    game_cfg_t *gc = config_game_alloc(hash);
    if (!gc) {
        const char *e = "{\"error\":\"no free slots\"}";
        send_headers(pcb, 200, "application/json", (int)strlen(e));
        send_str(pcb, e);
        return;
    }

    char name[32];
    json_str(body, "name", name, sizeof(name));
    if (name[0]) strncpy(gc->name, name, sizeof(gc->name) - 1);

    int vb = json_int(body, "vmu_bank", (int)gc->vmu_bank);
    gc->vmu_bank = (uint8_t)vb;

    int cm = json_int(body, "ctrl_mode", (int)gc->ctrl_mode);
    gc->ctrl_mode = (uint8_t)cm;

    // Parse remap sub-object
    const char *remap_start = strstr(body, "\"remap\"");
    if (remap_start) {
        remap_start = strchr(remap_start, '{');
        for (int s = 0; s < DC_NUM_BUTTONS; s++) {
            int val = json_int(remap_start, k_dc_slot_names[s], -1);
            if (val >= 0)
                gc->btn_remap[s] = (uint8_t)val;
        }
    }

    const char *resp = "{\"ok\":true}";
    send_headers(pcb, 200, "application/json", (int)strlen(resp));
    send_str(pcb, resp);
}

static void route_post_save(struct tcp_pcb *pcb) {
    config_store_save();
    const char *resp = "{\"ok\":true}";
    send_headers(pcb, 200, "application/json", (int)strlen(resp));
    send_str(pcb, resp);
}

static void route_post_reboot(struct tcp_pcb *pcb) {
    config_store_save();
    const char *resp = "{\"ok\":true}";
    send_headers(pcb, 200, "application/json", (int)strlen(resp));
    send_str(pcb, resp);
    tcp_output(pcb);
    sleep_ms(250);
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}

// ── Request dispatch ──────────────────────────────────────────────────────────
static void dispatch(struct tcp_pcb *pcb, const char *method, const char *path,
                     const char *body) {
    printf("[webconfig] %s %s\n", method, path);

    if (!strcmp(method, "GET") && !strcmp(path, "/")) {
        route_get_root(pcb);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/config")) {
        route_get_config(pcb);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/config")) {
        route_post_config(pcb, body);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/games")) {
        route_get_games(pcb);
    } else if (!strncmp(method, "GET", 3) && !strncmp(path, "/api/games/", 11)) {
        route_get_game(pcb, path + 11);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/games")) {
        route_post_games(pcb, body);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/save")) {
        route_post_save(pcb);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/reboot")) {
        route_post_reboot(pcb);  // does not return
    } else {
        const char *e = "Not Found";
        send_headers(pcb, 404, "text/plain", (int)strlen(e));
        send_str(pcb, e);
    }
}

// ── lwIP TCP callbacks ────────────────────────────────────────────────────────
static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg; (void)len;
    // Close after all data sent
    tcp_close(pcb);
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    http_conn_t *conn = (http_conn_t *)arg;
    if (!p || err != ERR_OK) {
        if (p) pbuf_free(p);
        tcp_close(pcb);
        return ERR_OK;
    }

    // Accumulate request bytes
    u16_t copy = p->tot_len;
    if (conn->req_len + copy >= (int)sizeof(conn->req) - 1)
        copy = (u16_t)(sizeof(conn->req) - 1 - conn->req_len);
    pbuf_copy_partial(p, conn->req + conn->req_len, copy, 0);
    conn->req_len += copy;
    conn->req[conn->req_len] = '\0';
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // Wait until we have the full headers
    if (!conn->headers_done) {
        const char *hdr_end = strstr(conn->req, "\r\n\r\n");
        if (!hdr_end) return ERR_OK;
        conn->headers_done = true;

        // Parse Content-Length
        const char *cl = strstr(conn->req, "Content-Length:");
        if (!cl) cl = strstr(conn->req, "content-length:");
        conn->content_length = cl ? atoi(cl + 15) : 0;

        // Copy body bytes already received after the header separator
        const char *body_start = hdr_end + 4;
        int already = (int)(conn->req + conn->req_len - body_start);
        if (already > 0 && already < (int)sizeof(conn->body)) {
            memcpy(conn->body, body_start, (size_t)already);
            conn->body_len = already;
            conn->body[already] = '\0';
        }
    } else {
        // Accumulate body
        int chunk = conn->req_len;
        const char *hdr_end = strstr(conn->req, "\r\n\r\n");
        const char *body_start = hdr_end ? hdr_end + 4 : conn->req;
        int already = (int)(conn->req + conn->req_len - body_start);
        if (already > conn->body_len) {
            int new_bytes = already - conn->body_len;
            if (conn->body_len + new_bytes < (int)sizeof(conn->body)) {
                memcpy(conn->body + conn->body_len,
                       body_start + conn->body_len, (size_t)new_bytes);
                conn->body_len += new_bytes;
                conn->body[conn->body_len] = '\0';
            }
        }
        (void)chunk;
    }

    // Have we received the full body?
    if (conn->body_len < conn->content_length) return ERR_OK;

    // Parse request line: METHOD PATH HTTP/1.x
    char method[8] = {0};
    char path[128] = {0};
    sscanf(conn->req, "%7s %127s", method, path);

    // Set up sent callback before dispatching (dispatch may close via reboot)
    tcp_sent(pcb, http_sent);

    dispatch(pcb, method, path, conn->body);
    tcp_output(pcb);

    return ERR_OK;
}

static void http_err(void *arg, err_t err) {
    (void)err;
    http_conn_t *conn = (http_conn_t *)arg;
    if (conn) {
        memset(conn, 0, sizeof(*conn));
        free(conn);
    }
}

// Small pool of connection state structs to avoid malloc
#define MAX_HTTP_CONNS 4
static http_conn_t s_conn_pool[MAX_HTTP_CONNS];
static bool        s_conn_used[MAX_HTTP_CONNS];

static http_conn_t *conn_alloc(void) {
    for (int i = 0; i < MAX_HTTP_CONNS; i++) {
        if (!s_conn_used[i]) {
            s_conn_used[i] = true;
            memset(&s_conn_pool[i], 0, sizeof(s_conn_pool[i]));
            return &s_conn_pool[i];
        }
    }
    return NULL;
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    http_conn_t *conn = conn_alloc();
    if (!conn) {
        tcp_close(newpcb);
        return ERR_MEM;
    }
    tcp_setprio(newpcb, TCP_PRIO_MIN);
    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, http_recv);
    tcp_err(newpcb, http_err);
    return ERR_OK;
}

// ── Entry point ───────────────────────────────────────────────────────────────
void __attribute__((noreturn)) webconfig_run(bool sd_available) {
    (void)sd_available;
    printf("[webconfig] entering config mode\n");

    // Init CYW43 with lwIP (poll model — manual cyw43_arch_poll() in loop)
    if (cyw43_arch_init()) {
        printf("[webconfig] CYW43 init failed — halting\n");
        for (;;) tight_loop_contents();
    }

    // Create open WiFi AP
    cyw43_arch_enable_ap_mode(WIFI_SSID, NULL, CYW43_AUTH_OPEN);
    printf("[webconfig] WiFi AP '%s' started\n", WIFI_SSID);

    // Configure static IP on the AP netif
    ip4_addr_t ip, nm, gw;
    ip4addr_aton(AP_IP_STR, &ip);
    ip4addr_aton(AP_NM_STR, &nm);
    ip4_addr_copy(gw, ip);

    netif_set_ipaddr(netif_default, &ip);
    netif_set_netmask(netif_default, &nm);
    netif_set_gw(netif_default, &gw);

    // Start DHCP server to hand out addresses to connecting clients
    ip_addr_t lwip_ip, lwip_nm;
    ip_addr_copy_from_ip4(lwip_ip, ip);
    ip_addr_copy_from_ip4(lwip_nm, nm);
    dhcp_server_t dhcp;
    dhcp_server_init(&dhcp, &lwip_ip, &lwip_nm);

    // Start TCP HTTP listener
    struct tcp_pcb *listen_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!listen_pcb) {
        printf("[webconfig] tcp_new failed\n");
        for (;;) tight_loop_contents();
    }
    tcp_bind(listen_pcb, IP_ANY_TYPE, HTTP_PORT);
    listen_pcb = tcp_listen_with_backlog(listen_pcb, 4);
    tcp_accept(listen_pcb, http_accept);

    printf("[webconfig] HTTP server running at http://%s\n", AP_IP_STR);
    printf("[webconfig] Connect to WiFi '%s' then open a browser\n", WIFI_SSID);

    // Blink LED slowly to indicate config mode
    bool led = false;
    uint32_t last_blink = to_ms_since_boot(get_absolute_time());

    while (true) {
        cyw43_arch_poll();
        sys_check_timeouts();

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_blink >= 1000) {
            led = !led;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
            last_blink = now;
        }

        sleep_ms(1);
    }
}
