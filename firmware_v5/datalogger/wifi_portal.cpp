/******************************************************************************
 * WiFi setup portal (SoftAP browser UI). Used with ENABLE_HTTPD + AP mode.
 * Connect phone/PC to device AP, open http://192.168.4.1/ or http://192.168.4.1/portal
 * USB Serial Monitor on the PC can stay open for live logs while configuring Wi‑Fi.
 ******************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <nvs.h>
#include <string.h>
#include <stdio.h>
#include <httpd.h>
#include "config.h"
#include "netcfg.h"
#include "obd_user_pids.h"
#include "api_push.h"

#if ENABLE_HTTPD && ENABLE_WIFI && ENABLE_WIFI_PORTAL

#ifndef WIFI_SCAN_RUNNING
#define WIFI_SCAN_RUNNING (-1)
#endif
#ifndef WIFI_SCAN_FAILED
#define WIFI_SCAN_FAILED (-2)
#endif

extern nvs_handle_t nvs;
extern char wifiSSID[32];
extern char wifiPassword[32];
extern char vin[18];
extern char devid[12];

/** Same string WiFi.setHostname uses (router “client name” / DHCP hostname). */
static void formatDhcpName(char* out, size_t outlen)
{
  out[0] = 0;
  if (!devid[0] || outlen < 8) {
    return;
  }
  snprintf(out, outlen, "fm-%s", devid);
  for (char* p = out; *p; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') {
      *p = (char)(c - 'A' + 'a');
    } else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      *p = '-';
    }
  }
}

#ifndef ISFLAGSET
#define ISFLAGSET(hs, bit) ((hs)->flags & (bit))
#endif

static uint32_t portalAuthUntil = 0;
#if ENABLE_HTTPD && ENABLE_WIFI && ENABLE_WIFI_PORTAL
/* Credentials saved in HTTP handler; telemetry task applies STA (safe context + avoid WiFi.disconnect(true) killing AP). */
static volatile bool s_staReconnectPending = false;
#endif

static bool portalAuthed()
{
  return portalAuthUntil != 0 && millis() < portalAuthUntil;
}

static void jsonGetStr(const char* json, const char* key, char* out, size_t outlen)
{
  out[0] = 0;
  if (!json || !key || !out || outlen < 2) return;
  char pat[48];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* p = strstr(json, pat);
  if (!p) return;
  p += strlen(pat);
  size_t i = 0;
  while (*p && *p != '"' && i < outlen - 1) {
    if (*p == '\\' && p[1]) {
      p++;
    }
    out[i++] = *p++;
  }
  out[i] = 0;
}

static int jsonErr(UrlHandlerParam* param, int code, const char* msg)
{
  char* buf = param->pucBuffer;
  snprintf(buf, param->bufSize, "{\"error\":\"%s\"}", msg);
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  param->hs->response.statusCode = (uint16_t)code;
  return FLAG_DATA_RAW;
}

static void portal_effective_user(char* o, size_t olen)
{
  o[0] = 0;
  if (!nvs || olen < 2) {
    strncpy(o, PORTAL_HTTP_USER, olen - 1);
    o[olen - 1] = 0;
    return;
  }
  size_t l = olen;
  if (nvs_get_str(nvs, "C_USR", o, &l) != ESP_OK || !o[0]) {
    strncpy(o, PORTAL_HTTP_USER, olen - 1);
    o[olen - 1] = 0;
  }
}
static void portal_effective_pass(char* o, size_t olen)
{
  o[0] = 0;
  if (!nvs || olen < 2) {
    strncpy(o, PORTAL_HTTP_PASS, olen - 1);
    o[olen - 1] = 0;
    return;
  }
  size_t l = olen;
  if (nvs_get_str(nvs, "C_PSW", o, &l) != ESP_OK || !o[0]) {
    strncpy(o, PORTAL_HTTP_PASS, olen - 1);
    o[olen - 1] = 0;
  }
}
static int jsonKeyBool(const char* j, const char* key, int* out)
{
  if (!j || !key || !out) {
    return 0;
  }
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* s = strstr(j, pat);
  if (!s) {
    return 0;
  }
  s += strlen(pat);
  while (*s == ' ' || *s == '\t') s++;
  if (!strncmp(s, "false", 5) && (s[5] == ',' || s[5] == '}' || s[5] == '\0')) {
    *out = 0;
  } else if (!strncmp(s, "true", 4) && (s[4] == ',' || s[4] == '}' || s[4] == '\0')) {
    *out = 1;
  } else if (*s == '0' && (s[1] == ',' || s[1] == '}' || s[1] == '\n' || !s[1])) {
    *out = 0;
  } else if (*s == '1' && (s[1] == ',' || s[1] == '}' || s[1] == '\n' || !s[1])) {
    *out = 1;
  } else {
    *out = (s[0] == 't' || s[0] == 'T' || s[0] == '1') ? 1 : 0;
  }
  return 1;
}
int handlerPortalCfg(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  if (!portalAuthed()) {
    return jsonErr(param, 403, "login required");
  }
  if (!nvs) {
    return jsonErr(param, 500, "nvs");
  }
  if (ISFLAGSET(param->hs, FLAG_REQUEST_POST)) {
    if (!param->pucPayload || param->payloadSize == 0) {
      return jsonErr(param, 400, "body");
    }
    char body[800];
    size_t pl = param->payloadSize;
    if (pl >= sizeof(body)) {
      pl = sizeof(body) - 1;
    }
    memcpy(body, param->pucPayload, pl);
    body[pl] = 0;
    int a = (int)g_rt_wifi_ap, st = (int)g_rt_wifi_sta, b = (int)g_rt_ble, ce = (int)g_rt_cell;
    (void)jsonKeyBool(body, "ap", &a);
    (void)jsonKeyBool(body, "sta", &st);
    (void)jsonKeyBool(body, "ble", &b);
    (void)jsonKeyBool(body, "cell", &ce);
    if (a == 0 && st == 0) {
      return jsonErr(param, 400, "need_ap_or_sta");
    }
    nvs_set_u8(nvs, "C_AP", a ? 1u : 0u);
    nvs_set_u8(nvs, "C_STA", st ? 1u : 0u);
    nvs_set_u8(nvs, "C_BLE", b ? 1u : 0u);
    nvs_set_u8(nvs, "C_CELL", ce ? 1u : 0u);
    char u[48] = {0}, p2[64] = {0};
    jsonGetStr(body, "nuser", u, sizeof(u));
    char tmpPass[64] = {0};
    jsonGetStr(body, "npass", tmpPass, sizeof(tmpPass));
    if (u[0]) {
      nvs_set_str(nvs, "C_USR", u);
    }
    if (tmpPass[0]) {
      nvs_set_str(nvs, "C_PSW", tmpPass);
    }
    nvs_commit(nvs);
    netcfg_reload_from_nvs();
    snprintf(buf, param->bufSize, "{\"ok\":true,\"reboot\":1}");
    param->contentLength = strlen(buf);
    param->contentType = HTTPFILETYPE_JSON;
    return FLAG_DATA_RAW;
  }
  char ueff[48] = {0}, peff[64] = {0};
  size_t plen = sizeof(peff);
  int pset = 0;
  if (nvs_get_str(nvs, "C_PSW", peff, &plen) == ESP_OK && peff[0]) {
    pset = 1;
  }
  portal_effective_user(ueff, sizeof(ueff));
  char eusr[120] = {0};
  size_t ej = 0;
  for (size_t i = 0; ueff[i] && ej + 2 < sizeof(eusr); i++) {
    if (ueff[i] == '"' || ueff[i] == '\\') eusr[ej++] = '\\';
    eusr[ej++] = ueff[i];
  }
  eusr[ej] = 0;
  snprintf(
      buf,
      param->bufSize,
      "{\"ap\":%u,\"sta\":%u,\"ble\":%u,\"cell\":%u,\"cellAvail\":%d,\"bleBuild\":%d,\"user\":\"%s\","
      "\"hasPass\":%d,\"rebootNote\":1}",
      (unsigned)g_rt_wifi_ap, (unsigned)g_rt_wifi_sta, (unsigned)g_rt_ble, (unsigned)g_rt_cell,
#if HAVE_RUNTIME_CELL
      1,
#else
      0,
#endif
#if ENABLE_BLE
      1,
#else
      0,
#endif
      eusr, pset);
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  return FLAG_DATA_RAW;
}
// PORTAL_JS_BEGIN
static const char PORTAL_JS[] =
    "/* Served as GET /portal.js (not inline) so the login page HTML fits the HTTP buffer. Edit, then: python embed_js.py && python patch_portal_cpp.py */\nvar liveIv=0;\nvar STALE_MS=180000,MAXH=200,pidHistory={},sessOn=0,sessT0=0,sessLast={},sessSeries={};\nvar CHART_MAX_CARDS=24;\nfunction chartStrokeForIndex(i){\n var h=(i*41+17)%360,s=58,l=44;\n return \"hsl(\"+h+\",\"+s+\"%,\"+l+\"%)\";\n}\nfunction pidHexByte(b){b=b&255;var h=b.toString(16);return\"0x\"+(h.length<2?\"0\":\"\")+h.toUpperCase();}\nvar O={0:\"Supported PIDs [01-20]\",1:\"Monitor status\",2:\"Freeze DTC\",3:\"Fuel system\",4:\"Calculated load %\",5:\"Coolant temp\",6:\"STFT bank1 %\",7:\"LTFT bank1 %\",8:\"STFT bank2 %\",9:\"LTFT bank2 %\",10:\"Fuel pressure\",11:\"Intake MAP\",12:\"Engine RPM\",13:\"Vehicle speed\",14:\"Timing advance\",15:\"Intake air temp\",16:\"MAF flow\",17:\"Throttle %\",18:\"Cmd secondary air\",19:\"O2 sensors present\",20:\"O2 B1S1\",21:\"O2 B1S2\",22:\"O2 B1S3\",23:\"O2 B1S4\",24:\"O2 B2S1\",25:\"O2 B2S2\",26:\"O2 B2S3\",27:\"O2 B2S4\",28:\"OBD standard\",29:\"O2 sensors [1D]\",30:\"Aux input\",31:\"Run time since start\",32:\"Supported PIDs [21-40]\",33:\"Distance w/ MIL\",34:\"Fuel rail pressure\",35:\"O2 B1S1 AFR\",36:\"O2 B2S1 AFR\",49:\"Evap vapor pressure\",50:\"Barometric\",51:\"Catalyst B1S1\",52:\"Catalyst B2S1\",53:\"Catalyst B1S2\",54:\"Catalyst B2S2\",56:\"Ctrl module V\",57:\"Abs load %\",66:\"Cmd throttle actuator\",67:\"Time w/ MIL\",68:\"Time since codes cleared\",69:\"Ethanol fuel %\",70:\"Fuel rail abs pressure\",71:\"Hybrid battery %\",72:\"Engine oil temp\",73:\"Fuel injection timing\",74:\"Engine fuel rate\",81:\"Fuel rail pressure (diesel)\",94:\"Engine oil life %\",95:\"Engine oil life % GM\",96:\"Supported PIDs [61-80]\",97:\"Demand torque\",98:\"Actual torque\",99:\"Ref torque\",100:\"Engine pct torque\",128:\"Supported PIDs [81-A0]\",160:\"Supported PIDs [A1-C0]\",166:\"Odometer\"};\nfunction pidName(b){b=b&255;return O[b]||(\"Mode 01 PID \"+pidHexByte(b));}\nfunction rowStale(p){return p.age>=999000||p.age>STALE_MS;}\nvar _lastDtcSig=\"\";\nfunction pidValueFromOb(ob,pidByte){\n var a=ob.pid||[],u=ob.user||[],i,p;\n for(i=0;i<a.length;i++){p=a[i];if((p.pid&255)===pidByte&&!rowStale(p))return p.value;}\n for(i=0;i<u.length;i++){p=u[i];if((p.pid&255)===pidByte&&!rowStale(p))return p.value;}\n return null;\n}\nfunction updateLiveAlerts(j){\n var ob=j.obd||{},el=document.getElementById(\"alertHost\");\n if(!el)return;\n var crit=[],warn=[];\n if(ob.dtc_count>0&&ob.dtc_hex&&ob.dtc_hex.length){\n  crit.push(\"ENGINE / DTC: \"+ob.dtc_hex.join(\", \")+\" (hex from ECU — look up code or use a full OBD scanner).\");\n  var sig=ob.dtc_hex.join(\"|\");\n  if(sig!==_lastDtcSig){\n   _lastDtcSig=sig;\n   try{L(\"Check engine: fault code(s) \"+ob.dtc_hex.join(\", \"));}catch(e){}\n  }\n }else{_lastDtcSig=\"\";}\n var cool=pidValueFromOb(ob,0x05),rpm=pidValueFromOb(ob,0x0C),loadv=pidValueFromOb(ob,0x04),iat=pidValueFromOb(ob,0x0F),oil=pidValueFromOb(ob,0x5C);\n if(cool!==null&&cool>=115)crit.push(\"Coolant very high: \"+cool+\" C — stop if safe.\");\n else if(cool!==null&&cool>=105)warn.push(\"Coolant elevated: \"+cool+\" C\");\n if(rpm!==null&&rpm>=6200)warn.push(\"Engine speed very high: \"+rpm+\" RPM\");\n if(loadv!==null&&loadv>=98)warn.push(\"Engine load very high: \"+loadv+\"%\");\n if(ob.battery!==undefined&&ob.battery<11.5)warn.push(\"Vehicle voltage low: \"+ob.battery+\" V\");\n if(iat!==null&&iat>=65)warn.push(\"Intake air very hot: \"+iat+\" C\");\n if(oil!==null&&oil>=130)crit.push(\"Engine oil temp very high: \"+oil+\" C\");\n if(!crit.length&&!warn.length){el.style.display=\"none\";el.innerHTML=\"\";return;}\n el.style.display=\"block\";\n var html=\"<small style=opacity:.85>Alerts use simple thresholds — not a substitute for professional diagnosis.</small><br>\";\n if(crit.length){el.style.borderColor=\"#c00\";el.style.background=\"#fff0f0\";el.style.color=\"#600\";html+=\"<div style=margin-top:6px><b>Attention:</b> \"+crit.join(\" \")+\"</div>\";}\n if(warn.length){\n  if(!crit.length){el.style.borderColor=\"#a60\";el.style.background=\"#fff8f0\";el.style.color=\"#630\";}\n  html+=\"<div style=margin-top:4px><b>Notice:</b> \"+warn.join(\" | \")+\"</div>\";\n }\n el.innerHTML=html;\n}\nfunction rowHtml(p){\n if(rowStale(p))return\"\";\n var b=p.pid&255,name=pidName(b),hx=pidHexByte(b);\n return\"<tr><td><b>\"+name+\"</b><br><span class=pv>\"+hx+\"</span></td><td>\"+p.value+\"</td><td>\"+p.age+\"</td></tr>\";\n}\nfunction pushHistory(j){\n var ob=j.obd||{},add=function(arr){\n  for(var i=0;i<arr.length;i++){\n   var p=arr[i]; if(rowStale(p))continue;\n   var b=p.pid&255,key=pidHexByte(b);\n   if(!pidHistory[key])pidHistory[key]=[];\n   var a=pidHistory[key]; a.push({t:Date.now(),v:p.value});\n   while(a.length>MAXH)a.shift();\n  }\n };\n add(ob.pid||[]); add(ob.user||[]);\n}\nfunction drawChart(cv,pts,lab,stroke){\n if(!cv||!pts||pts.length<1)return;\n stroke=stroke||\"#0a6ebd\";\n var ctx=cv.getContext(\"2d\"),w=cv.width,h=cv.height,pad=Math.max(20,Math.min(28,Math.floor(h*0.2)));\n ctx.fillStyle=\"#fff\";ctx.fillRect(0,0,w,h);\n ctx.strokeStyle=\"#e8e8e8\";ctx.strokeRect(pad,pad,w-2*pad,h-2*pad);\n if(pts.length<2){ctx.fillStyle=\"#666\";ctx.font=\"12px sans-serif\";ctx.fillText(\"Need 2+ points — keep Live auto-refresh on\",pad+4,pad+22);return;}\n var t0=pts[0].t,t1=pts[pts.length-1].t,dt=t1-t0||1,vs=pts.map(function(p){return p.v}),mn=Math.min.apply(null,vs),mx=Math.max.apply(null,vs);\n if(mn===mx){mn--;mx++;}\n var i,x0,y0,x1,y1,bh=h-2*pad;\n ctx.lineWidth=2;ctx.strokeStyle=stroke;ctx.lineJoin=\"round\";ctx.lineCap=\"round\";\n ctx.beginPath();\n for(i=0;i<pts.length;i++){\n  x0=pad+(pts[i].t-t0)/dt*(w-2*pad);\n  y0=h-pad-((pts[i].v-mn)/(mx-mn))*bh;\n  if(i===0)ctx.moveTo(x0,y0);else ctx.lineTo(x0,y0);\n }\n ctx.stroke();\n ctx.fillStyle=stroke;ctx.globalAlpha=0.12;\n ctx.beginPath();ctx.moveTo(pad,h-pad);\n for(i=0;i<pts.length;i++){\n  x0=pad+(pts[i].t-t0)/dt*(w-2*pad);y0=h-pad-((pts[i].v-mn)/(mx-mn))*bh;\n  ctx.lineTo(x0,y0);\n }\n ctx.lineTo(pad+(pts[pts.length-1].t-t0)/dt*(w-2*pad),h-pad);ctx.closePath();ctx.fill();ctx.globalAlpha=1;\n ctx.fillStyle=\"#333\";ctx.font=\"11px sans-serif\";\n ctx.fillText(lab+\" · n=\"+pts.length+\" · min \"+mn.toFixed(1)+\" max \"+mx.toFixed(1),pad,h-6);\n}\nfunction renderChartList(){\n var grid=document.getElementById(\"cGrid\"); if(!grid)return;\n var ks=Object.keys(pidHistory),total=ks.length;\n if(!total){grid.innerHTML=\"<p style=color:#888>No samples yet — open <b>Live data</b> with auto-refresh.</p>\";return;}\n ks.sort(function(a,b){return parseInt(a.substr(2),16)-parseInt(b.substr(2),16);});\n var truncated=total>CHART_MAX_CARDS,show=truncated?ks.slice(0,CHART_MAX_CARDS):ks;\n var html=truncated?\"<p style=font-size:12px;color:#a60;margin:0 0 10px>Showing first \"+CHART_MAX_CARDS+\" of \"+total+\" PIDs (browser performance).</p>\":\"\";\n html+=\"<div class=cgrid>\";\n var j,k,b,pts,col,cv;\n for(j=0;j<show.length;j++){\n  k=show[j];b=parseInt(k.substr(2),16);col=chartStrokeForIndex(j);\n  html+=\"<div class=cCard><div class=cCardHead><span class=cDot style=background:\"+col+\"></span>\"+pidName(b)+\" <small class=cPid>\"+k+\"</small></div>\";\n  html+=\"<canvas class=cMini width=520 height=140 data-pid=\\\"\"+k+\"\\\"></canvas></div>\";\n }\n html+=\"</div>\";\n grid.innerHTML=html;\n for(j=0;j<show.length;j++){\n  k=show[j];b=parseInt(k.substr(2),16);\n  cv=grid.querySelector(\"canvas[data-pid=\\\"\"+k+\"\\\"]\");\n  if(cv){drawChart(cv,pidHistory[k]||[],pidName(b),chartStrokeForIndex(j));}\n }\n}\nfunction sessResetBuffer(){\n sessT0=Date.now();sessLast={};sessSeries={};\n}\nfunction sessStart(){sessOn=1;sessResetBuffer();var e=document.getElementById(\"sessStat\");if(e)e.textContent=\"Recording… (change-only)\";}\nfunction sessClear(){sessResetBuffer();var e=document.getElementById(\"sessStat\");if(e)e.textContent=sessOn?\"Recording… (buffer cleared)\":\"Buffer cleared\";}\nfunction sessRecordChanges(j){\n var ob=j.obd||{},t=Date.now()-sessT0;\n function bump(k,v){\n  var prev=sessLast[k];\n  if(prev!==undefined&&prev===v)return;\n  sessLast[k]=v;\n  if(!sessSeries[k])sessSeries[k]=[];\n  sessSeries[k].push([t,v]);\n }\n function eachPid(arr){\n  var i,p,b,k;\n  for(i=0;i<arr.length;i++){\n   p=arr[i];if(rowStale(p))continue;\n   b=p.pid&255;k=pidHexByte(b);\n   bump(k,p.value);\n  }\n }\n eachPid(ob.pid||[]);eachPid(ob.user||[]);\n if(ob.battery!==undefined)bump(\"_battery_V\",ob.battery);\n if(ob.dtc_hex)bump(\"_dtc_hex\",JSON.stringify(ob.dtc_hex));\n if(ob.vin&&ob.vin.length&&!sessLast._vin_once){\n  sessLast._vin_once=true;\n  if(!sessSeries._vin)sessSeries._vin=[];\n  sessSeries._vin.push([t,ob.vin]);\n }\n}\nfunction sessStopDl(){\n sessOn=0;\n var ks=Object.keys(sessSeries),i,pts=0;\n for(i=0;i<ks.length;i++)pts+=sessSeries[ks[i]].length;\n var o={\n  format:\"obd_session_change_v1\",\n  exported_utc:new Date().toISOString(),\n  time_unit:\"ms_since_session_start\",\n  note:\"Start/stop is manual (not ECU engine-detect). Each key is a PID (0x..) or _battery_V / _dtc_hex / _vin. Values are [t_ms,value] rows appended only when that value changed.\",\n  channels:ks.length,\n  data_points:pts,\n  series:sessSeries\n };\n var blob=new Blob([JSON.stringify(o)],{type:\"application/json\"});\n var a=document.createElement(\"a\"); a.href=URL.createObjectURL(blob); a.download=\"obd-session.json\"; a.click();\n var e=document.getElementById(\"sessStat\");if(e)e.textContent=\"Downloaded \"+ks.length+\" channel(s), \"+pts+\" point(s) (change-only)\";\n}\nfunction L(m){document.getElementById(\"log\").textContent+=m+\"\\n\";}\nfunction showTab(i){\n clearInterval(liveIv);liveIv=0;\n var panes=document.querySelectorAll(\".tabPane\");\n for(var j=0;j<panes.length;j++)panes[j].style.display=j===i?\"block\":\"none\";\n var tabs=document.querySelectorAll(\".tabnav .tab\");\n for(var k=0;k<tabs.length;k++)tabs[k].className=\"tab\"+(k===i?\" on\":\"\");\n if(i===1){refreshLive();if(document.getElementById(\"liveAuto\").checked)liveIv=setInterval(refreshLive,2000);}\n if(i===2)renderChartList();\n if(i===3)loadObdPids();\n if(i===4)refreshDev();\n if(i===5)loadRemoteCfg();\n if(i===6)loadSysCfg();\n if(i===7)loadBleCfg();\n}\nfunction onLiveAuto(){clearInterval(liveIv);liveIv=0;if(!document.getElementById(\"liveAuto\").checked)return;\n var Lp=document.getElementById(\"tabLive\");if(Lp&&Lp.style.display===\"block\")liveIv=setInterval(refreshLive,2000);}\nasync function refreshLive(){\n try{\n const r=await fetch(\"/api/live\");const t=await r.text();let j=JSON.parse(t);\n var ob=j.obd||{};\n var h=\"<p><b>VIN</b> \"+((ob.vin&&ob.vin.length)?ob.vin:\"—\")+\" &nbsp; <b>Batt</b> \"+(ob.battery!==undefined?ob.battery:\"—\")+\" V</p>\";\n h+=\"<table class=grid><thead><tr><th>Name / PID</th><th>Value</th><th>Age ms</th></tr></thead><tbody>\";\n var arr=ob.pid||[];\n for(var x=0;x<arr.length;x++){h+=rowHtml(arr[x]);}\n h+=\"</tbody></table>\";\n var uarr=ob.user||[];\n if(uarr.length){\n  h+=\"<h3>Extra PIDs (portal)</h3><table class=grid><thead><tr><th>Name / PID</th><th>Value</th><th>Age ms</th></tr></thead><tbody>\";\n  for(var ux=0;ux<uarr.length;ux++){h+=rowHtml(uarr[ux]);}\n  h+=\"</tbody></table>\";\n }\n if(j.gps)h+=\"<h3>GPS</h3><pre class=json>\"+JSON.stringify(j.gps,null,2)+\"</pre>\";\n if(j.mems)h+=\"<h3>MEMS</h3><pre class=json>\"+JSON.stringify(j.mems,null,2)+\"</pre>\";\n document.getElementById(\"liveBox\").innerHTML=h;\n pushHistory(j);\n if(sessOn)sessRecordChanges(j);\n renderChartList();\n updateLiveAlerts(j);\n }catch(e){document.getElementById(\"liveBox\").textContent=\"Error: \"+e;}\n}\nasync function loadObdPids(){try{const r=await fetch(\"/api/portal/obd-pids\");const o=await r.json();if(o.error){L(o.error);return;}\ndocument.getElementById(\"obdHex\").value=o.hex||\"\";if(o.max!==undefined){var em=document.getElementById(\"obdMax\");if(em)em.textContent=o.max}\n}catch(e){L(\"Load PIDs: \"+e)}}\nasync function saveObdPids(){try{const hex=document.getElementById(\"obdHex\").value.trim();\nconst r=await fetch(\"/api/portal/obd-pids\",{method:\"POST\",headers:{\"Content-Type\":\"application/json\"},body:JSON.stringify({hex})});const o=await r.json();\nif(o.ok)L(\"Extra PIDs saved\");else L(\"Error: \"+(o.error||JSON.stringify(o)))}catch(e){L(\"Save failed: \"+e)}}\nasync function refreshDev(){\ntry{const r=await fetch(\"/api/info\");document.getElementById(\"devBox\").textContent=await r.text();}\ncatch(e){document.getElementById(\"devBox\").textContent=\"Error: \"+e}}\nasync function doLogin(){\nconst j=JSON.stringify({user:document.getElementById(\"u\").value,pass:document.getElementById(\"p\").value});\nconst r=await fetch(\"/api/portal/login\",{method:\"POST\",headers:{\"Content-Type\":\"application/json\"},body:j});\nconst o=await r.json();if(o.ok){document.getElementById(\"loginS\").style.display=\"none\";document.getElementById(\"mainS\").style.display=\"block\";L(\"OK logged in\");showTab(0);doStatus()}\nelse L(\"Login failed\")}\nasync function doScan(){L(\"Scanning (10-25s, stay on this page)...\");try{\nconst r=await fetch(\"/api/portal/scan\");if(!r.ok){L(\"HTTP \"+r.status);return}\nconst t=await r.text();let o;try{o=JSON.parse(t)}catch(e){L(\"Bad JSON: \"+t.substring(0,280));return}\nif(o.error){L(\"Error: \"+o.error);return}\nconst s=document.getElementById(\"ssid\");s.innerHTML=\"<option value=\\\"\\\">- pick -</option>\";\n(o.networks||[]).forEach(n=>{const e=document.createElement(\"option\");e.value=n.ssid;e.textContent=(n.ssid||\"(hidden)\")+\" (\"+n.rssi+\" dBm)\";s.appendChild(e)});\nL(\"Done. Found \"+(o.networks?o.networks.length:0)+\" APs\")}catch(e){L(\"Scan failed: \"+e)}}\nasync function doConnect(){let ssid=document.getElementById(\"ssidText\").value.trim();\nif(!ssid)ssid=document.getElementById(\"ssid\").value;\nconst password=document.getElementById(\"wp\").value;\nif(!ssid){L(\"Enter SSID (manual) or pick from list\");return}\nconst r=await fetch(\"/api/portal/connect\",{method:\"POST\",headers:{\"Content-Type\":\"application/json\"},\nbody:JSON.stringify({ssid:ssid,password:password})});const o=await r.json();L(o.ok?\"Saved & connecting\":\"Error: \"+(o.error||\"?\"))}\nasync function doStatus(){const r=await fetch(\"/api/portal/status\");const o=await r.json();\nconst v=document.getElementById(\"vinVal\");if(v)v.textContent=(o.vin&&o.vin.length)?o.vin:\"(not read - need ECU/OBD)\";\nconst hc=document.getElementById(\"hnC\"),mc=document.getElementById(\"macC\"),ic=document.getElementById(\"ipC\"),dc=document.getElementById(\"idC\");\nif(hc)hc.textContent=o.dhcp_name||\"-\";if(mc)mc.textContent=o.sta_mac||\"-\";if(ic)ic.textContent=o.sta_ip||\"0.0.0.0\";if(dc)dc.textContent=o.device_id||\"-\";\ndocument.getElementById(\"st\").textContent=JSON.stringify(o)}\nfunction collectRemoteJson(){var b=document.getElementById(\"remBase\"),k=document.getElementById(\"remKey\"),a=document.getElementById(\"remAuthP\"),i=document.getElementById(\"remIngP\"),n=document.getElementById(\"remIvl\"),e=document.getElementById(\"remEn\");\nreturn{base:(b&&b.value)?b.value.trim():\"\",pathAuth:(a&&a.value)?a.value.trim():\"\",pathIngest:(i&&i.value)?i.value.trim():\"\",ivl:parseInt(n&&n.value,10)||0,en:(e&&e.checked)?1:0,key:(k&&k.value)?k.value:\"\"};}\nasync function loadRemoteCfg(){var st=document.getElementById(\"remSt\");if(st)st.textContent=\"…\";try{const r=await fetch(\"/api/portal/remote\");const t=await r.text();const o=JSON.parse(t);\nif(o.error&&st){st.textContent=o.error;return}var b=document.getElementById(\"remBase\"),a=document.getElementById(\"remAuthP\"),i=document.getElementById(\"remIngP\"),n=document.getElementById(\"remIvl\"),e=document.getElementById(\"remEn\"),k=document.getElementById(\"remKey\");\nif(b)b.value=o.base||\"\";if(a)a.value=o.pathAuth||\"\";if(i)i.value=o.pathIngest||\"\";if(n)n.value=String(o.ivl!=null?o.ivl:0);if(e)e.checked=!!o.en;if(k)k.value=\"\";\nif(st)st.textContent=\"Saved auth: \"+(o.authOk?\"yes\":\"no\")+(o.keySet?\" · key is set\":\"\");}catch(err){if(st)st.textContent=String(err);}}\nasync function saveRemoteCfg(){var st=document.getElementById(\"remSt\");try{const b=Object.assign({},collectRemoteJson());if(!b.key)delete b.key;const r=await fetch(\"/api/portal/remote\",{method:\"POST\",headers:{\"Content-Type\":\"application/json\"},body:JSON.stringify(b)});const o=await r.json();if(st)st.textContent=o.ok?\"Saved.\":\"Error: \"+(o.error||\"?\");\nif(o.ok)loadRemoteCfg();}catch(e){if(st)st.textContent=\"Save: \"+e}}\nasync function testRemoteAuth(){var st=document.getElementById(\"remSt\");try{const b=Object.assign({},collectRemoteJson());if(!b.key)delete b.key;const r=await fetch(\"/api/portal/remote-test\",{method:\"POST\",headers:{\"Content-Type\":\"application/json\"},body:JSON.stringify(b)});const o=await r.json();\nif(st)st.textContent=\"HTTP \"+(o.code!=null?o.code:\"?\")+\": \"+(o.ok?\"ok\":\"fail\")+(o.err?(\" — \"+o.err):\"\");\nif(o.ok)loadRemoteCfg();}catch(e){if(st)st.textContent=\"Test: \"+e}}\nasync function loadSysCfg(){\n var st=document.getElementById(\"sysSt\"),note=document.getElementById(\"sysNote\");\n var ap=document.getElementById(\"sysAp\"),sta=document.getElementById(\"sysSta\"),ble=document.getElementById(\"sysBle\"),cell=document.getElementById(\"sysCell\");\n var nu=document.getElementById(\"sysNuser\"),np=document.getElementById(\"sysNpass\"),br=document.getElementById(\"sysBleRow\"),cr=document.getElementById(\"sysCellRow\");\n if(st)st.textContent=\"…\";\n if(np)np.value=\"\";\n try{\n  const r=await fetch(\"/api/portal/cfg\");\n  const o=await r.json();\n  if(o.error){if(st)st.textContent=o.error;return;}\n  if(ap)ap.checked=!!o.ap;if(sta)sta.checked=!!o.sta;if(ble)ble.checked=!!o.ble;\n  if(cell){cell.checked=!!o.cell;cell.disabled=!o.cellAvail;cell.parentElement.style.opacity=o.cellAvail?\"1\":\"0.55\";}\n  if(cr)cr.style.display=\"block\";\n  if(ble&&o.bleBuild===0){ble.disabled=true;if(br)br.title=\"BLE not compiled in this firmware\";}\n  if(nu)nu.value=o.user||\"\";\n  if(note)note.textContent=o.rebootNote?\"After saving, reboot to apply radio settings.\":\"\";\n  if(st)st.textContent=\"\";\n }catch(e){if(st)st.textContent=String(e);}}\nasync function saveSysCfg(){\n var st=document.getElementById(\"sysSt\");\n var ap=document.getElementById(\"sysAp\"),sta=document.getElementById(\"sysSta\"),ble=document.getElementById(\"sysBle\"),cell=document.getElementById(\"sysCell\");\n var nu=document.getElementById(\"sysNuser\"),np=document.getElementById(\"sysNpass\");\n var body={ap:ap&&ap.checked?1:0,sta:sta&&sta.checked?1:0,ble:ble&&ble.checked&&!ble.disabled?1:0,cell:cell&&!cell.disabled&&cell.checked?1:0};\n if(nu&&nu.value.trim())body.nuser=nu.value.trim();\n if(np&&np.value)body.npass=np.value;\n try{\n  const r=await fetch(\"/api/portal/cfg\",{method:\"POST\",headers:{\"Content-Type\":\"application/json\"},body:JSON.stringify(body)});\n  const o=await r.json();\n  if(st)st.textContent=o.ok?(\"Saved. Reboot if you changed WiFi/BLE/cell or login.\"):(\"Error: \"+(o.error||JSON.stringify(o)));\n  if(o.ok)loadSysCfg();\n }catch(e){if(st)st.textContent=String(e);}}\nasync function loadBleCfg(){var st=document.getElementById(\"bleSt\"),onp=document.getElementById(\"bleOnP\"),nm=document.getElementById(\"bleNm\");\nif(st)st.textContent=\"…\";try{const r=await fetch(\"/api/portal/bt\");const o=await r.json();\nif(o.error&&st){st.textContent=o.error;return}\nif(nm)nm.value=o.name||\"\";\nif(onp)onp.textContent=\"SPP: \"+(o.on?\"on (firmware)\":\"off\")+\" — saved name is used at next power cycle.\";\nif(st)st.textContent=\"\";}catch(e){if(st)st.textContent=String(e);}}\nasync function saveBleCfg(){var st=document.getElementById(\"bleSt\"),nm=document.getElementById(\"bleNm\");\nvar n=nm?nm.value.trim():\"\";\nif(!n){if(st)st.textContent=\"Enter a name\";return}\ntry{const r=await fetch(\"/api/portal/bt\",{method:\"POST\",headers:{\"Content-Type\":\"application/json\"},body:JSON.stringify({name:n})});\nconst o=await r.json();if(st)st.textContent=o.ok?\"Saved. Reboot the device to apply.\":\"Error: \"+(o.error||\"?\");}catch(e){if(st)st.textContent=String(e);}}\n";
// PORTAL_JS_END
int handlerPortalJs(UrlHandlerParam* param)
{
  static const char oom[] =
    "/* portal.js: increase HTTP_BUFFER_SIZE in Arduino libraries/httpd/httpd.h (firmware portal is ~18k+ escaped) and rebuild. */";
  param->contentType = HTTPFILETYPE_JS;
  size_t full = strlen(PORTAL_JS);
  if (full >= param->bufSize) {
    size_t m = strlen(oom);
    if (m >= param->bufSize) {
      m = param->bufSize - 1U;
    }
    memcpy(param->pucBuffer, oom, m);
    param->pucBuffer[m] = 0;
    param->contentLength = (unsigned int)m;
    return FLAG_DATA_RAW;
  }
  memcpy(param->pucBuffer, PORTAL_JS, full);
  param->pucBuffer[full] = 0;
  param->contentLength = (unsigned int)full;
  return FLAG_DATA_RAW;
}

int handlerPortalHtml(UrlHandlerParam* param)
{
  static const char HTML[] =
    "<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<link rel=icon href=/favicon.ico>"
    "<title>Amir Datalogger (based on Freematics) — portal</title><style>"
    "body{font-family:system-ui,sans-serif;max-width:880px;margin:16px auto;padding:0 12px}"
    "h1{font-size:1.2rem;margin:0 0 4px}label{display:block;margin:8px 0 4px}input,select{width:100%;padding:8px;box-sizing:border-box}"
    ".don8{margin:0 0 12px;font-size:12px;color:#5a5a5a}.don8 a{color:#0a6ebd;text-decoration:none}.don8 a:hover{text-decoration:underline}"
    "button{margin:8px 8px 8px 0;padding:10px 16px;cursor:pointer}"
    "#log{background:#111;color:#0f0;padding:8px;font-size:12px;min-height:48px;white-space:pre-wrap}"
    ".row{display:flex;gap:8px;align-items:center}#st{margin-top:8px;padding:8px;background:#eee;border-radius:6px;font-size:12px;word-break:break-all}"
    ".tabnav{display:flex;gap:4px;border-bottom:2px solid #ccc;margin:12px 0 16px;flex-wrap:wrap}"
    ".tab{padding:10px 16px;cursor:pointer;border:1px solid transparent;border-radius:8px 8px 0 0;background:#f0f0f0;font-size:14px}"
    ".tab.on{background:#fff;border-color:#ccc;border-bottom-color:#fff;font-weight:600;margin-bottom:-2px}"
    "table.grid{width:100%;font-size:13px;border-collapse:collapse;margin:8px 0}"
    "table.grid td,table.grid th{border:1px solid #ddd;padding:6px 8px;text-align:left}"
    "table.grid th{background:#f5f5f5}pre.json{font-size:11px;overflow:auto;background:#f7f7f7;padding:10px;border-radius:8px;max-height:70vh}"
    "#pv{font-size:11px;color:#666}.cgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:12px;align-items:start}"
    ".cCard{border:1px solid #ddd;border-radius:10px;padding:10px;background:#fafafa;box-shadow:0 1px 3px rgba(0,0,0,.06)}"
    ".cCardHead{font-size:13px;font-weight:600;margin:0 0 8px;line-height:1.3}.cDot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px;vertical-align:middle}"
    ".cPid{font-size:11px;color:#666;font-weight:400}.cMini{width:100%;height:auto;display:block;border-radius:6px;background:#fff;border:1px solid #eee}"
    "</style></head><body>"
    "<h1>Amir Datalogger (based on Freematics)</h1>"
    "<p class=don8>☕ <a href=\"https://buymeacoffee.com/amirtechhub\" target=_blank rel=noopener>amirtechhub</a> — OBD2 / Freematics hub build</p>"
    "<p>Log in, then use the tabs below.</p>"
    "<div id=loginS><label>User</label><input id=u value=admin autocomplete=username>"
    "<label>Portal password</label><input id=p type=password value=amir autocomplete=current-password>"
    "<button type=button onclick=doLogin()>Login</button></div>"
    "<div id=mainS style=display:none>"
    "<div class=tabnav>"
    "<button type=button class=\"tab on\" onclick=\"showTab(0)\">WiFi</button>"
    "<button type=button class=tab onclick=\"showTab(1)\">Live data</button>"
    "<button type=button class=tab onclick=\"showTab(2)\">Charts</button>"
    "<button type=button class=tab onclick=\"showTab(3)\">Extra PIDs</button>"
    "<button type=button class=tab onclick=\"showTab(4)\">Device</button>"
    "<button type=button class=tab onclick=\"showTab(5)\">API</button>"
    "<button type=button class=tab onclick=\"showTab(6)\">System</button>"
    "<button type=button class=tab onclick=\"showTab(7)\">Bluetooth</button>"
    "</div>"
    "<div id=tabWifi class=tabPane style=display:block>"
    "<p id=vinP style=margin:8px 0;font-size:15px><b>VIN</b>: <span id=vinVal>—</span></p>"
    "<p id=netP style=margin:8px 0;font-size:14px;line-height:1.45><b>On your router</b>, look for "
    "<b>hostname</b> <code id=hnC>—</code> or <b>MAC</b> <code id=macC>—</code> (Device ID <code id=idC>—</code>). "
    "<b>STA IP</b> (LAN): <code id=ipC>—</code></p>"
    "<button type=button onclick=doScan()>Scan WiFi</button>"
    "<label>Or type SSID (hidden / manual)</label><input id=ssidText placeholder=\"e.g. MyNetwork\" autocapitalize=none>"
    "<label>From scan</label><select id=ssid><option value=\"\">— pick after scan —</option></select>"
    "<label>WiFi password</label><input id=wp type=password placeholder=\"WiFi PSK\">"
    "<button type=button onclick=doConnect()>Save & connect</button>"
    "<button type=button onclick=doStatus()>Refresh status</button>"
    "<div id=st class=row></div></div>"
    "<div id=tabLive class=tabPane style=display:none>"
    "<p><button type=button onclick=refreshLive()>Refresh now</button>"
    "<label style=display:inline><input type=checkbox id=liveAuto onchange=onLiveAuto> Auto every 2s</label></p>"
    "<div id=alertHost style=display:none;margin:0 0 12px;padding:12px 14px;border-radius:8px;border:2px solid #c00;background:#fff5f5;color:#800;font-size:14px;font-weight:600></div>"
    "<div id=liveBox>— open Live data tab —</div></div>"
    "<div id=tabCharts class=tabPane style=display:none>"
    "<p style=font-size:13px;color:#444>Each PID has its own chart (colors differ). Data comes from <b>Live</b> — turn on auto-refresh. Rendering runs on your phone/PC, not the ESP32.</p>"
    "<div id=cGrid><p style=color:#888>— Open Live data first —</p></div>"
    "<hr style=border:none;border-top:1px solid #ddd;margin:16px 0>"
    "<p><b>Session JSON</b> <span style=font-size:12px;color:#666>(one file, all PIDs — only stores new values when a reading changes; browser clock — Start at engine on, Stop &amp; download; not auto engine-detect)</span></p>"
    "<p><button type=button onclick=sessStart()>Start recording</button> "
    "<button type=button onclick=sessStopDl()>Stop &amp; download JSON</button> "
    "<button type=button onclick=sessClear()>Clear</button></p>"
    "<p id=sessStat style=font-size:12px;color:#666></p></div>"
    "<div id=tabExtra class=tabPane style=display:none>"
    "<p>Comma-separated hex Mode $01 bytes (e.g. <code>14,15,1C</code>). Stored in flash; polled with telemetry.</p>"
    "<label>Extra PIDs</label><textarea id=obdHex rows=5 style=width:100%;box-sizing:border-box placeholder=\"14,42,5C\"></textarea>"
    "<p><button type=button onclick=saveObdPids()>Save</button> <small>Max <span id=obdMax>24</span> entries.</small></p></div>"
    "<div id=tabDev class=tabPane style=display:none>"
    "<p><button type=button onclick=refreshDev()>Refresh</button> <small>/api/info</small></p>"
    "<pre id=devBox class=json>—</pre></div>"
    "<div id=tabApi class=tabPane style=display:none>"
    "<label>Base URL (https or http)</label><input id=remBase placeholder=\"https://example.com\" autocapitalize=none spellcheck=false>"
    "<label>Key</label><input id=remKey type=password placeholder=\"(unchanged if empty)\" autocomplete=off>"
    "<label>Auth path</label><input id=remAuthP placeholder=\"/v1/auth\" autocapitalize=none>"
    "<label>Ingest path</label><input id=remIngP placeholder=\"/v1/ingest\" autocapitalize=none>"
    "<label>Upload period (minutes)</label><input id=remIvl type=number min=0 max=10080 value=0>"
    "<p><label><input type=checkbox id=remEn>Enable timed uploads (averages per period)</label></p>"
    "<p id=remSt style=font-size:14px;min-height:1.2em>—</p>"
    "<p><button type=button onclick=saveRemoteCfg()>Save</button> "
    "<button type=button onclick=testRemoteAuth()>Test</button></p>"
    "</div>"
    "<div id=tabSys class=tabPane style=display:none>"
    "<p style=font-size:13px;color:#444>Runtime toggles and portal login are stored in flash. <b>Reboot</b> the device to apply WiFi/BLE/cell mode changes.</p>"
    "<p id=sysNote style=font-size:12px;color:#a60></p>"
    "<p><label><input type=checkbox id=sysAp> WiFi access point (192.168.4.1)</label></p>"
    "<p><label><input type=checkbox id=sysSta> WiFi station (connect to your router)</label></p>"
    "<p id=sysBleRow><label><input type=checkbox id=sysBle> Bluetooth SPP (GATT)</label></p>"
    "<p id=sysCellRow><label><input type=checkbox id=sysCell disabled> Cellular (not available on this build)</label></p>"
    "<hr style=border:none;border-top:1px solid #ddd;margin:12px 0>"
    "<label>Portal username (leave empty to keep current)</label><input id=sysNuser autocomplete=username spellcheck=false>"
    "<label>New portal password (leave empty to keep current)</label><input id=sysNpass type=password autocomplete=new-password placeholder=\"(unchanged)\">"
    "<p><button type=button onclick=saveSysCfg()>Save to flash</button></p><p id=sysSt style=font-size:13px></p>"
    "</div>"
    "<div id=tabBt class=tabPane style=display:none>"
    "<p style=font-size:13px;color:#444>SPP: pair a phone/PC, then use serial-style commands (UPTIME, VIN, 01+hex PID, etc.). The name you set applies after a reboot.</p>"
    "<p id=bleOnP style=margin:8px 0></p>"
    "<label>Device name in Bluetooth settings (max 28 characters)</label>"
    "<input id=bleNm placeholder=\"FreematicsPlus\" maxlength=28 autocapitalize=none spellcheck=false>"
    "<p><button type=button onclick=saveBleCfg()>Save to flash</button></p><p id=bleSt style=font-size:13px></p>"
    "</div>"
    "</div>"
    "<div id=log></div>"
    "<script src=/portal.js defer></script></body></html>";






  param->contentType = HTTPFILETYPE_HTML;
  size_t n = strlen(HTML);
  if (n >= param->bufSize) n = param->bufSize - 1;
  memcpy(param->pucBuffer, HTML, n);
  param->pucBuffer[n] = 0;
  param->contentLength = (unsigned int)n;
  return FLAG_DATA_RAW;
}

int handlerPortalLogin(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  if (!ISFLAGSET(param->hs, FLAG_REQUEST_POST) || !param->pucPayload || param->payloadSize == 0)
    return jsonErr(param, 400, "POST JSON {user,pass} required");

  char body[192];
  size_t pl = param->payloadSize;
  if (pl >= sizeof(body)) pl = sizeof(body) - 1;
  memcpy(body, param->pucPayload, pl);
  body[pl] = 0;

  char user[48] = {0}, pass[64] = {0};
  jsonGetStr(body, "user", user, sizeof(user));
  jsonGetStr(body, "pass", pass, sizeof(pass));

  char expU[48], expP[64];
  portal_effective_user(expU, sizeof(expU));
  portal_effective_pass(expP, sizeof(expP));
  if (strcmp(user, expU) == 0 && strcmp(pass, expP) == 0) {
    portalAuthUntil = millis() + (30UL * 60 * 1000);
    snprintf(buf, param->bufSize, "{\"ok\":true}");
    param->contentLength = strlen(buf);
    param->contentType = HTTPFILETYPE_JSON;
    Serial.println("[PORTAL] login ok");
    return FLAG_DATA_RAW;
  }
  snprintf(buf, param->bufSize, "{\"ok\":false}");
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  return FLAG_DATA_RAW;
}

int handlerPortalScan(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  int bufsize = (int)param->bufSize;
  if (!portalAuthed())
    return jsonErr(param, 403, "login required");

  /*
   * Async scan in WIFI_AP_STA often stays WIFI_SCAN_FAILED or never completes while STA is up.
   * Use one synchronous scan per request (blocks ~3–20s); browser waits once with a clear message.
   */
  WiFi.scanDelete();
  delay(80);

  int16_t n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true, /*passive=*/false,
                                /*max_ms_per_chan=*/300, /*channel=*/0);
  if (n == WIFI_SCAN_FAILED || n < 0) {
    Serial.printf("[PORTAL] scan failed (code %d)\n", (int)n);
    snprintf(buf, bufsize, "{\"scanning\":false,\"error\":\"scan_failed\",\"networks\":[]}");
    param->contentLength = strlen(buf);
    param->contentType = HTTPFILETYPE_JSON;
    return FLAG_DATA_RAW;
  }

  Serial.printf("[PORTAL] scan OK, %d APs\n", (int)n);

  int o = snprintf(buf, (size_t)bufsize, "{\"scanning\":false,\"networks\":[");
  if (o < 0 || o >= bufsize - 4) {
    snprintf(buf, (size_t)bufsize, "{\"scanning\":false,\"error\":\"buf\",\"networks\":[]}");
    param->contentLength = strlen(buf);
    param->contentType = HTTPFILETYPE_JSON;
    WiFi.scanDelete();
    return FLAG_DATA_RAW;
  }

  bool first = true;
  for (int i = 0; i < n; i++) {
    /* +3 for "]}"; build row in a side buffer so we never leave a truncated object in buf. */
    if (o >= bufsize - 8) {
      break;
    }
    String ss = WiFi.SSID(i);
    char esc[64];
    strncpy(esc, ss.c_str(), sizeof(esc) - 1);
    esc[sizeof(esc) - 1] = 0;
    if (!esc[0]) {
      strncpy(esc, "(hidden)", sizeof(esc) - 1);
    }
    for (char* p = esc; *p; p++) {
      if (*p == '"' || *p == '\\') {
        *p = '?';
      }
    }
    char row[128];
    int w = snprintf(row, sizeof(row), "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                     first ? "" : ",", esc, WiFi.RSSI(i));
    if (w < 0 || w >= (int)sizeof(row)) {
      continue;
    }
    if (o + w + 3 > bufsize) {
      break;
    }
    memcpy(buf + o, row, (size_t)w);
    o += w;
    first = false;
  }

  int wclose = snprintf(buf + o, (size_t)(bufsize - o), "]}");
  if (wclose < 0 || wclose >= bufsize - o) {
    if (o + 3 < bufsize) {
      buf[o++] = ']';
      buf[o++] = '}';
      buf[o] = 0;
    } else {
      buf[bufsize - 1] = 0;
      o = bufsize - 1;
    }
  } else {
    o += wclose;
  }
  param->contentLength = (unsigned int)o;
  param->contentType = HTTPFILETYPE_JSON;
  WiFi.scanDelete();
  return FLAG_DATA_RAW;
}

int handlerFavicon(UrlHandlerParam* param)
{
  param->contentLength = 0;
  param->contentType = HTTPFILETYPE_UNKNOWN;
  param->hs->response.statusCode = 204;
  return FLAG_DATA_RAW;
}

int handlerPortalConnect(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  if (!ISFLAGSET(param->hs, FLAG_REQUEST_POST) || !param->pucPayload || param->payloadSize == 0)
    return jsonErr(param, 400, "POST JSON required");
  if (!portalAuthed())
    return jsonErr(param, 403, "login required");

  char body[256];
  size_t pl = param->payloadSize;
  if (pl >= sizeof(body)) pl = sizeof(body) - 1;
  memcpy(body, param->pucPayload, pl);
  body[pl] = 0;

  char ssid[64] = {0}, pwd[96] = {0};
  jsonGetStr(body, "ssid", ssid, sizeof(ssid));
  jsonGetStr(body, "password", pwd, sizeof(pwd));
  if (ssid[0] == 0)
    return jsonErr(param, 400, "ssid required");

  if (nvs && nvs_set_str(nvs, "WIFI_SSID", ssid) != ESP_OK) {
    return jsonErr(param, 500, "nvs ssid");
  }
  if (nvs && nvs_set_str(nvs, "WIFI_PWD", pwd) != ESP_OK) {
    return jsonErr(param, 500, "nvs pwd");
  }
  if (nvs)
    nvs_commit(nvs);

  strncpy(wifiSSID, ssid, sizeof(wifiSSID) - 1);
  wifiSSID[sizeof(wifiSSID) - 1] = 0;
  strncpy(wifiPassword, pwd, sizeof(wifiPassword) - 1);
  wifiPassword[sizeof(wifiPassword) - 1] = 0;

  /* Do not call WiFi.disconnect/begin here: lwIP/HTTP context + disconnect(true) powers radio off and kills SoftAP. */
  s_staReconnectPending = true;

  snprintf(buf, param->bufSize, "{\"ok\":true}");
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  Serial.print("[PORTAL] saved SSID=");
  Serial.println(wifiSSID);
  return FLAG_DATA_RAW;
}

int handlerPortalStatus(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  if (!portalAuthed())
    return jsonErr(param, 403, "login required");

  wl_status_t st = WiFi.status();
  IPAddress ip = (st == WL_CONNECTED) ? WiFi.localIP() : IPAddress((uint32_t)0);
  IPAddress apip = WiFi.softAPIP();
  char ssidSafe[40];
  strncpy(ssidSafe, wifiSSID, sizeof(ssidSafe) - 1);
  ssidSafe[sizeof(ssidSafe) - 1] = 0;
  for (char* p = ssidSafe; *p; p++) {
    if (*p == '"' || *p == '\\') *p = '?';
  }
  char vinSafe[24] = {0};
  if (vin[0]) {
    strncpy(vinSafe, vin, sizeof(vinSafe) - 1);
    for (char* p = vinSafe; *p; p++) {
      if (*p == '"' || *p == '\\') *p = '?';
    }
  }
  char devSafe[16] = {0};
  if (devid[0]) {
    strncpy(devSafe, devid, sizeof(devSafe) - 1);
    for (char* p = devSafe; *p; p++) {
      if (*p == '"' || *p == '\\') *p = '?';
    }
  }
  char dhcpName[40] = {0};
  formatDhcpName(dhcpName, sizeof(dhcpName));
  for (char* p = dhcpName; *p; p++) {
    if (*p == '"' || *p == '\\') *p = '?';
  }

  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  char macStr[20];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  snprintf(buf, param->bufSize,
           "{\"vin\":\"%s\",\"device_id\":\"%s\",\"dhcp_name\":\"%s\",\"sta_mac\":\"%s\","
           "\"sta_status\":%d,\"sta_ssid\":\"%s\",\"sta_ip\":\"%d.%d.%d.%d\",\"rssi\":%d,"
           "\"ap_ip\":\"%d.%d.%d.%d\"}",
           vinSafe, devSafe, dhcpName, macStr, (int)st, ssidSafe, ip[0], ip[1], ip[2], ip[3],
           (st == WL_CONNECTED) ? WiFi.RSSI() : 0,
           apip[0], apip[1], apip[2], apip[3]);
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  return FLAG_DATA_RAW;
}

int handlerPortalObdPids(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  if (!portalAuthed())
    return jsonErr(param, 403, "login required");

  if (!ISFLAGSET(param->hs, FLAG_REQUEST_POST)) {
    char hex[OBD_USER_NVS_MAX];
    obdUserPidsGetSavedString(hex, sizeof(hex));
    char esc[OBD_USER_NVS_MAX * 2 + 4];
    size_t ei = 0;
    for (size_t i = 0; hex[i] && ei + 2 < sizeof(esc); i++) {
      if (hex[i] == '\\' || hex[i] == '"')
        esc[ei++] = '\\';
      esc[ei++] = hex[i];
    }
    esc[ei] = 0;
    snprintf(buf, param->bufSize, "{\"hex\":\"%s\",\"max\":%u}", esc, (unsigned)OBD_USER_PID_MAX);
    param->contentLength = strlen(buf);
    param->contentType = HTTPFILETYPE_JSON;
    return FLAG_DATA_RAW;
  }

  if (!param->pucPayload || param->payloadSize == 0)
    return jsonErr(param, 400, "POST JSON {hex} required");

  char body[OBD_USER_NVS_MAX + 64];
  size_t pl = param->payloadSize;
  if (pl >= sizeof(body)) pl = sizeof(body) - 1;
  memcpy(body, param->pucPayload, pl);
  body[pl] = 0;

  char hex[OBD_USER_NVS_MAX];
  jsonGetStr(body, "hex", hex, sizeof(hex));
  if (!nvs)
    return jsonErr(param, 500, "nvs not ready");
  if (!obdUserPidsSave(nvs, hex))
    return jsonErr(param, 500, "save failed");

  snprintf(buf, param->bufSize, "{\"ok\":true}");
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  return FLAG_DATA_RAW;
}

int handlerPortalRemote(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  if (!portalAuthed())
    return jsonErr(param, 403, "login required");
  if (ISFLAGSET(param->hs, FLAG_REQUEST_POST)) {
    if (!param->pucPayload || param->payloadSize == 0)
      return jsonErr(param, 400, "body");
    char body[800];
    size_t pl = param->payloadSize;
    if (pl >= sizeof(body)) pl = sizeof(body) - 1;
    memcpy(body, param->pucPayload, pl);
    body[pl] = 0;
    if (!api_push_from_json(body))
      return jsonErr(param, 400, "save");
    snprintf(buf, param->bufSize, "{\"ok\":true}");
    param->contentLength = strlen(buf);
    param->contentType = HTTPFILETYPE_JSON;
    return FLAG_DATA_RAW;
  }
  if (!api_push_to_json(buf, param->bufSize))
    return jsonErr(param, 500, "cfg");
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  return FLAG_DATA_RAW;
}

int handlerPortalRemoteTest(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  if (!portalAuthed())
    return jsonErr(param, 403, "login required");
  if (!ISFLAGSET(param->hs, FLAG_REQUEST_POST))
    return jsonErr(param, 400, "POST");
  if (param->pucPayload && param->payloadSize > 0) {
    char body[800];
    size_t pl = param->payloadSize;
    if (pl >= sizeof(body)) pl = sizeof(body) - 1;
    memcpy(body, param->pucPayload, pl);
    body[pl] = 0;
    api_push_from_json(body);
  }
  char err[160];
  int c = api_push_test_auth(err, sizeof(err));
  char es[220];
  size_t j = 0;
  const char* s = err[0] ? err : "";
  for (; *s && j + 2 < sizeof(es); s++) {
    if (*s == '"' || *s == '\\') {
      if (j + 2 < sizeof(es)) {
        es[j++] = '\\';
      }
    }
    es[j++] = *s;
  }
  es[j] = 0;
  snprintf(buf, param->bufSize, "{\"ok\":%s,\"code\":%d,\"err\":\"%s\"}",
    (c >= 200 && c < 300) ? "true" : "false", c, es);
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  return FLAG_DATA_RAW;
}

int handlerPortalBt(UrlHandlerParam* param)
{
  char* buf = param->pucBuffer;
  if (!portalAuthed())
    return jsonErr(param, 403, "login required");
  if (!nvs)
    return jsonErr(param, 500, "nvs");
  if (ISFLAGSET(param->hs, FLAG_REQUEST_POST)) {
    if (!param->pucPayload || param->payloadSize == 0)
      return jsonErr(param, 400, "body");
    char body[200];
    size_t pl = param->payloadSize;
    if (pl >= sizeof(body)) pl = sizeof(body) - 1;
    memcpy(body, param->pucPayload, pl);
    body[pl] = 0;
    char name[32];
    jsonGetStr(body, "name", name, sizeof(name));
    if (name[0] == 0) {
      return jsonErr(param, 400, "name");
    }
    if (nvs_set_str(nvs, "BLE_SPP_NM", name) != ESP_OK) {
      return jsonErr(param, 500, "nvs");
    }
    nvs_commit(nvs);
    snprintf(buf, param->bufSize, "{\"ok\":true}");
    param->contentLength = strlen(buf);
    param->contentType = HTTPFILETYPE_JSON;
    return FLAG_DATA_RAW;
  }
  char name[32];
  size_t nl = sizeof(name);
  name[0] = 0;
  if (nvs_get_str(nvs, "BLE_SPP_NM", name, &nl) != ESP_OK) {
    name[0] = 0;
  }
  if (!name[0]) {
    strncpy(name, "FreematicsPlus", sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
  }
  int bten = 0;
#if ENABLE_BLE
  bten = 1;
#endif
  char ebn[64];
  size_t ei = 0;
  for (size_t i = 0; name[i] && ei + 2 < sizeof(ebn); i++) {
    if (name[i] == '"' || name[i] == '\\') {
      ebn[ei++] = '\\';
    }
    ebn[ei++] = name[i];
  }
  ebn[ei] = 0;
  snprintf(buf, param->bufSize, "{\"on\":%d,\"name\":\"%s\",\"reload\":1}", bten, ebn);
  param->contentLength = strlen(buf);
  param->contentType = HTTPFILETYPE_JSON;
  return FLAG_DATA_RAW;
}

bool wifi_portal_consume_sta_reconnect_pending(void)
{
  if (!s_staReconnectPending) {
    return false;
  }
  s_staReconnectPending = false;
  return true;
}

#else /* portal disabled */

int handlerPortalHtml(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalJs(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalLogin(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalScan(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalConnect(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalStatus(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalObdPids(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalRemote(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalRemoteTest(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalBt(UrlHandlerParam* param) { (void)param; return 0; }
int handlerPortalCfg(UrlHandlerParam* param) { (void)param; return 0; }
int handlerFavicon(UrlHandlerParam* param) { (void)param; return 0; }

bool wifi_portal_consume_sta_reconnect_pending(void) { return false; }

#endif
