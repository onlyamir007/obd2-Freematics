/* Served as GET /portal.js (not inline) so the login page HTML fits the HTTP buffer. Edit, then: python embed_js.py && python patch_portal_cpp.py */
var liveIv=0;
var STALE_MS=180000,MAXH=200,pidHistory={},sessOn=0,sessT0=0,sessLast={},sessSeries={};
var CHART_MAX_CARDS=24;
function chartStrokeForIndex(i){
 var h=(i*41+17)%360,s=58,l=44;
 return "hsl("+h+","+s+"%,"+l+"%)";
}
function pidHexByte(b){b=b&255;var h=b.toString(16);return"0x"+(h.length<2?"0":"")+h.toUpperCase();}
var O={0:"Supported PIDs [01-20]",1:"Monitor status",2:"Freeze DTC",3:"Fuel system",4:"Calculated load %",5:"Coolant temp",6:"STFT bank1 %",7:"LTFT bank1 %",8:"STFT bank2 %",9:"LTFT bank2 %",10:"Fuel pressure",11:"Intake MAP",12:"Engine RPM",13:"Vehicle speed",14:"Timing advance",15:"Intake air temp",16:"MAF flow",17:"Throttle %",18:"Cmd secondary air",19:"O2 sensors present",20:"O2 B1S1",21:"O2 B1S2",22:"O2 B1S3",23:"O2 B1S4",24:"O2 B2S1",25:"O2 B2S2",26:"O2 B2S3",27:"O2 B2S4",28:"OBD standard",29:"O2 sensors [1D]",30:"Aux input",31:"Run time since start",32:"Supported PIDs [21-40]",33:"Distance w/ MIL",34:"Fuel rail pressure",35:"O2 B1S1 AFR",36:"O2 B2S1 AFR",49:"Evap vapor pressure",50:"Barometric",51:"Catalyst B1S1",52:"Catalyst B2S1",53:"Catalyst B1S2",54:"Catalyst B2S2",56:"Ctrl module V",57:"Abs load %",66:"Cmd throttle actuator",67:"Time w/ MIL",68:"Time since codes cleared",69:"Ethanol fuel %",70:"Fuel rail abs pressure",71:"Hybrid battery %",72:"Engine oil temp",73:"Fuel injection timing",74:"Engine fuel rate",81:"Fuel rail pressure (diesel)",94:"Engine oil life %",95:"Engine oil life % GM",96:"Supported PIDs [61-80]",97:"Demand torque",98:"Actual torque",99:"Ref torque",100:"Engine pct torque",128:"Supported PIDs [81-A0]",160:"Supported PIDs [A1-C0]",166:"Odometer"};
function pidName(b){b=b&255;return O[b]||("Mode 01 PID "+pidHexByte(b));}
function rowStale(p){return p.age>=999000||p.age>STALE_MS;}
var _lastDtcSig="";
function pidValueFromOb(ob,pidByte){
 var a=ob.pid||[],u=ob.user||[],i,p;
 for(i=0;i<a.length;i++){p=a[i];if((p.pid&255)===pidByte&&!rowStale(p))return p.value;}
 for(i=0;i<u.length;i++){p=u[i];if((p.pid&255)===pidByte&&!rowStale(p))return p.value;}
 return null;
}
function updateLiveAlerts(j){
 var ob=j.obd||{},el=document.getElementById("alertHost");
 if(!el)return;
 var crit=[],warn=[];
 if(ob.dtc_count>0&&ob.dtc_hex&&ob.dtc_hex.length){
  crit.push("ENGINE / DTC: "+ob.dtc_hex.join(", ")+" (hex from ECU — look up code or use a full OBD scanner).");
  var sig=ob.dtc_hex.join("|");
  if(sig!==_lastDtcSig){
   _lastDtcSig=sig;
   try{L("Check engine: fault code(s) "+ob.dtc_hex.join(", "));}catch(e){}
  }
 }else{_lastDtcSig="";}
 var cool=pidValueFromOb(ob,0x05),rpm=pidValueFromOb(ob,0x0C),loadv=pidValueFromOb(ob,0x04),iat=pidValueFromOb(ob,0x0F),oil=pidValueFromOb(ob,0x5C);
 if(cool!==null&&cool>=115)crit.push("Coolant very high: "+cool+" C — stop if safe.");
 else if(cool!==null&&cool>=105)warn.push("Coolant elevated: "+cool+" C");
 if(rpm!==null&&rpm>=6200)warn.push("Engine speed very high: "+rpm+" RPM");
 if(loadv!==null&&loadv>=98)warn.push("Engine load very high: "+loadv+"%");
 if(ob.battery!==undefined&&ob.battery<11.5)warn.push("Vehicle voltage low: "+ob.battery+" V");
 if(iat!==null&&iat>=65)warn.push("Intake air very hot: "+iat+" C");
 if(oil!==null&&oil>=130)crit.push("Engine oil temp very high: "+oil+" C");
 if(!crit.length&&!warn.length){el.style.display="none";el.innerHTML="";return;}
 el.style.display="block";
 var html="<small style=opacity:.85>Alerts use simple thresholds — not a substitute for professional diagnosis.</small><br>";
 if(crit.length){el.style.borderColor="#c00";el.style.background="#fff0f0";el.style.color="#600";html+="<div style=margin-top:6px><b>Attention:</b> "+crit.join(" ")+"</div>";}
 if(warn.length){
  if(!crit.length){el.style.borderColor="#a60";el.style.background="#fff8f0";el.style.color="#630";}
  html+="<div style=margin-top:4px><b>Notice:</b> "+warn.join(" | ")+"</div>";
 }
 el.innerHTML=html;
}
function rowHtml(p){
 if(rowStale(p))return"";
 var b=p.pid&255,name=pidName(b),hx=pidHexByte(b);
 return"<tr><td><b>"+name+"</b><br><span class=pv>"+hx+"</span></td><td>"+p.value+"</td><td>"+p.age+"</td></tr>";
}
function pushHistory(j){
 var ob=j.obd||{},add=function(arr){
  for(var i=0;i<arr.length;i++){
   var p=arr[i]; if(rowStale(p))continue;
   var b=p.pid&255,key=pidHexByte(b);
   if(!pidHistory[key])pidHistory[key]=[];
   var a=pidHistory[key]; a.push({t:Date.now(),v:p.value});
   while(a.length>MAXH)a.shift();
  }
 };
 add(ob.pid||[]); add(ob.user||[]);
}
function drawChart(cv,pts,lab,stroke){
 if(!cv||!pts||pts.length<1)return;
 stroke=stroke||"#0a6ebd";
 var ctx=cv.getContext("2d"),w=cv.width,h=cv.height,pad=Math.max(20,Math.min(28,Math.floor(h*0.2)));
 ctx.fillStyle="#fff";ctx.fillRect(0,0,w,h);
 ctx.strokeStyle="#e8e8e8";ctx.strokeRect(pad,pad,w-2*pad,h-2*pad);
 if(pts.length<2){ctx.fillStyle="#666";ctx.font="12px sans-serif";ctx.fillText("Need 2+ points — keep Live auto-refresh on",pad+4,pad+22);return;}
 var t0=pts[0].t,t1=pts[pts.length-1].t,dt=t1-t0||1,vs=pts.map(function(p){return p.v}),mn=Math.min.apply(null,vs),mx=Math.max.apply(null,vs);
 if(mn===mx){mn--;mx++;}
 var i,x0,y0,x1,y1,bh=h-2*pad;
 ctx.lineWidth=2;ctx.strokeStyle=stroke;ctx.lineJoin="round";ctx.lineCap="round";
 ctx.beginPath();
 for(i=0;i<pts.length;i++){
  x0=pad+(pts[i].t-t0)/dt*(w-2*pad);
  y0=h-pad-((pts[i].v-mn)/(mx-mn))*bh;
  if(i===0)ctx.moveTo(x0,y0);else ctx.lineTo(x0,y0);
 }
 ctx.stroke();
 ctx.fillStyle=stroke;ctx.globalAlpha=0.12;
 ctx.beginPath();ctx.moveTo(pad,h-pad);
 for(i=0;i<pts.length;i++){
  x0=pad+(pts[i].t-t0)/dt*(w-2*pad);y0=h-pad-((pts[i].v-mn)/(mx-mn))*bh;
  ctx.lineTo(x0,y0);
 }
 ctx.lineTo(pad+(pts[pts.length-1].t-t0)/dt*(w-2*pad),h-pad);ctx.closePath();ctx.fill();ctx.globalAlpha=1;
 ctx.fillStyle="#333";ctx.font="11px sans-serif";
 ctx.fillText(lab+" · n="+pts.length+" · min "+mn.toFixed(1)+" max "+mx.toFixed(1),pad,h-6);
}
function renderChartList(){
 var grid=document.getElementById("cGrid"); if(!grid)return;
 var ks=Object.keys(pidHistory),total=ks.length;
 if(!total){grid.innerHTML="<p style=color:#888>No samples yet — open <b>Live data</b> with auto-refresh.</p>";return;}
 ks.sort(function(a,b){return parseInt(a.substr(2),16)-parseInt(b.substr(2),16);});
 var truncated=total>CHART_MAX_CARDS,show=truncated?ks.slice(0,CHART_MAX_CARDS):ks;
 var html=truncated?"<p style=font-size:12px;color:#a60;margin:0 0 10px>Showing first "+CHART_MAX_CARDS+" of "+total+" PIDs (browser performance).</p>":"";
 html+="<div class=cgrid>";
 var j,k,b,pts,col,cv;
 for(j=0;j<show.length;j++){
  k=show[j];b=parseInt(k.substr(2),16);col=chartStrokeForIndex(j);
  html+="<div class=cCard><div class=cCardHead><span class=cDot style=background:"+col+"></span>"+pidName(b)+" <small class=cPid>"+k+"</small></div>";
  html+="<canvas class=cMini width=520 height=140 data-pid=\""+k+"\"></canvas></div>";
 }
 html+="</div>";
 grid.innerHTML=html;
 for(j=0;j<show.length;j++){
  k=show[j];b=parseInt(k.substr(2),16);
  cv=grid.querySelector("canvas[data-pid=\""+k+"\"]");
  if(cv){drawChart(cv,pidHistory[k]||[],pidName(b),chartStrokeForIndex(j));}
 }
}
function sessResetBuffer(){
 sessT0=Date.now();sessLast={};sessSeries={};
}
function sessStart(){sessOn=1;sessResetBuffer();var e=document.getElementById("sessStat");if(e)e.textContent="Recording… (change-only)";}
function sessClear(){sessResetBuffer();var e=document.getElementById("sessStat");if(e)e.textContent=sessOn?"Recording… (buffer cleared)":"Buffer cleared";}
function sessRecordChanges(j){
 var ob=j.obd||{},t=Date.now()-sessT0;
 function bump(k,v){
  var prev=sessLast[k];
  if(prev!==undefined&&prev===v)return;
  sessLast[k]=v;
  if(!sessSeries[k])sessSeries[k]=[];
  sessSeries[k].push([t,v]);
 }
 function eachPid(arr){
  var i,p,b,k;
  for(i=0;i<arr.length;i++){
   p=arr[i];if(rowStale(p))continue;
   b=p.pid&255;k=pidHexByte(b);
   bump(k,p.value);
  }
 }
 eachPid(ob.pid||[]);eachPid(ob.user||[]);
 if(ob.battery!==undefined)bump("_battery_V",ob.battery);
 if(ob.dtc_hex)bump("_dtc_hex",JSON.stringify(ob.dtc_hex));
 if(ob.vin&&ob.vin.length&&!sessLast._vin_once){
  sessLast._vin_once=true;
  if(!sessSeries._vin)sessSeries._vin=[];
  sessSeries._vin.push([t,ob.vin]);
 }
}
function sessStopDl(){
 sessOn=0;
 var ks=Object.keys(sessSeries),i,pts=0;
 for(i=0;i<ks.length;i++)pts+=sessSeries[ks[i]].length;
 var o={
  format:"obd_session_change_v1",
  exported_utc:new Date().toISOString(),
  time_unit:"ms_since_session_start",
  note:"Start/stop is manual (not ECU engine-detect). Each key is a PID (0x..) or _battery_V / _dtc_hex / _vin. Values are [t_ms,value] rows appended only when that value changed.",
  channels:ks.length,
  data_points:pts,
  series:sessSeries
 };
 var blob=new Blob([JSON.stringify(o)],{type:"application/json"});
 var a=document.createElement("a"); a.href=URL.createObjectURL(blob); a.download="obd-session.json"; a.click();
 var e=document.getElementById("sessStat");if(e)e.textContent="Downloaded "+ks.length+" channel(s), "+pts+" point(s) (change-only)";
}
function L(m){document.getElementById("log").textContent+=m+"\n";}
function showTab(i){
 clearInterval(liveIv);liveIv=0;
 var panes=document.querySelectorAll(".tabPane");
 for(var j=0;j<panes.length;j++)panes[j].style.display=j===i?"block":"none";
 var tabs=document.querySelectorAll(".tabnav .tab");
 for(var k=0;k<tabs.length;k++)tabs[k].className="tab"+(k===i?" on":"");
 if(i===1){refreshLive();if(document.getElementById("liveAuto").checked)liveIv=setInterval(refreshLive,2000);}
 if(i===2)renderChartList();
 if(i===3)loadObdPids();
 if(i===4)refreshDev();
 if(i===5)loadRemoteCfg();
 if(i===6)loadSysCfg();
 if(i===7)loadBleCfg();
}
function onLiveAuto(){clearInterval(liveIv);liveIv=0;if(!document.getElementById("liveAuto").checked)return;
 var Lp=document.getElementById("tabLive");if(Lp&&Lp.style.display==="block")liveIv=setInterval(refreshLive,2000);}
async function refreshLive(){
 try{
 const r=await fetch("/api/live");const t=await r.text();let j=JSON.parse(t);
 var ob=j.obd||{};
 var h="<p><b>VIN</b> "+((ob.vin&&ob.vin.length)?ob.vin:"—")+" &nbsp; <b>Batt</b> "+(ob.battery!==undefined?ob.battery:"—")+" V</p>";
 h+="<table class=grid><thead><tr><th>Name / PID</th><th>Value</th><th>Age ms</th></tr></thead><tbody>";
 var arr=ob.pid||[];
 for(var x=0;x<arr.length;x++){h+=rowHtml(arr[x]);}
 h+="</tbody></table>";
 var uarr=ob.user||[];
 if(uarr.length){
  h+="<h3>Extra PIDs (portal)</h3><table class=grid><thead><tr><th>Name / PID</th><th>Value</th><th>Age ms</th></tr></thead><tbody>";
  for(var ux=0;ux<uarr.length;ux++){h+=rowHtml(uarr[ux]);}
  h+="</tbody></table>";
 }
 if(j.gps)h+="<h3>GPS</h3><pre class=json>"+JSON.stringify(j.gps,null,2)+"</pre>";
 if(j.mems)h+="<h3>MEMS</h3><pre class=json>"+JSON.stringify(j.mems,null,2)+"</pre>";
 document.getElementById("liveBox").innerHTML=h;
 pushHistory(j);
 if(sessOn)sessRecordChanges(j);
 renderChartList();
 updateLiveAlerts(j);
 }catch(e){document.getElementById("liveBox").textContent="Error: "+e;}
}
async function loadObdPids(){try{const r=await fetch("/api/portal/obd-pids");const o=await r.json();if(o.error){L(o.error);return;}
document.getElementById("obdHex").value=o.hex||"";if(o.max!==undefined){var em=document.getElementById("obdMax");if(em)em.textContent=o.max}
}catch(e){L("Load PIDs: "+e)}}
async function saveObdPids(){try{const hex=document.getElementById("obdHex").value.trim();
const r=await fetch("/api/portal/obd-pids",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({hex})});const o=await r.json();
if(o.ok)L("Extra PIDs saved");else L("Error: "+(o.error||JSON.stringify(o)))}catch(e){L("Save failed: "+e)}}
async function refreshDev(){
try{const r=await fetch("/api/info");document.getElementById("devBox").textContent=await r.text();}
catch(e){document.getElementById("devBox").textContent="Error: "+e}}
async function doLogin(){
const j=JSON.stringify({user:document.getElementById("u").value,pass:document.getElementById("p").value});
const r=await fetch("/api/portal/login",{method:"POST",headers:{"Content-Type":"application/json"},body:j});
const o=await r.json();if(o.ok){document.getElementById("loginS").style.display="none";document.getElementById("mainS").style.display="block";L("OK logged in");showTab(0);doStatus()}
else L("Login failed")}
async function doScan(){L("Scanning (10-25s, stay on this page)...");try{
const r=await fetch("/api/portal/scan");if(!r.ok){L("HTTP "+r.status);return}
const t=await r.text();let o;try{o=JSON.parse(t)}catch(e){L("Bad JSON: "+t.substring(0,280));return}
if(o.error){L("Error: "+o.error);return}
const s=document.getElementById("ssid");s.innerHTML="<option value=\"\">- pick -</option>";
(o.networks||[]).forEach(n=>{const e=document.createElement("option");e.value=n.ssid;e.textContent=(n.ssid||"(hidden)")+" ("+n.rssi+" dBm)";s.appendChild(e)});
L("Done. Found "+(o.networks?o.networks.length:0)+" APs")}catch(e){L("Scan failed: "+e)}}
async function doConnect(){let ssid=document.getElementById("ssidText").value.trim();
if(!ssid)ssid=document.getElementById("ssid").value;
const password=document.getElementById("wp").value;
if(!ssid){L("Enter SSID (manual) or pick from list");return}
const r=await fetch("/api/portal/connect",{method:"POST",headers:{"Content-Type":"application/json"},
body:JSON.stringify({ssid:ssid,password:password})});const o=await r.json();L(o.ok?"Saved & connecting":"Error: "+(o.error||"?"))}
async function doStatus(){const r=await fetch("/api/portal/status");const o=await r.json();
const v=document.getElementById("vinVal");if(v)v.textContent=(o.vin&&o.vin.length)?o.vin:"(not read - need ECU/OBD)";
const hc=document.getElementById("hnC"),mc=document.getElementById("macC"),ic=document.getElementById("ipC"),dc=document.getElementById("idC");
if(hc)hc.textContent=o.dhcp_name||"-";if(mc)mc.textContent=o.sta_mac||"-";if(ic)ic.textContent=o.sta_ip||"0.0.0.0";if(dc)dc.textContent=o.device_id||"-";
document.getElementById("st").textContent=JSON.stringify(o)}
function collectRemoteJson(){var b=document.getElementById("remBase"),k=document.getElementById("remKey"),a=document.getElementById("remAuthP"),i=document.getElementById("remIngP"),n=document.getElementById("remIvl"),e=document.getElementById("remEn");
return{base:(b&&b.value)?b.value.trim():"",pathAuth:(a&&a.value)?a.value.trim():"",pathIngest:(i&&i.value)?i.value.trim():"",ivl:parseInt(n&&n.value,10)||0,en:(e&&e.checked)?1:0,key:(k&&k.value)?k.value:""};}
async function loadRemoteCfg(){var st=document.getElementById("remSt");if(st)st.textContent="…";try{const r=await fetch("/api/portal/remote");const t=await r.text();const o=JSON.parse(t);
if(o.error&&st){st.textContent=o.error;return}var b=document.getElementById("remBase"),a=document.getElementById("remAuthP"),i=document.getElementById("remIngP"),n=document.getElementById("remIvl"),e=document.getElementById("remEn"),k=document.getElementById("remKey");
if(b)b.value=o.base||"";if(a)a.value=o.pathAuth||"";if(i)i.value=o.pathIngest||"";if(n)n.value=String(o.ivl!=null?o.ivl:0);if(e)e.checked=!!o.en;if(k)k.value="";
if(st)st.textContent="Saved auth: "+(o.authOk?"yes":"no")+(o.keySet?" · key is set":"");}catch(err){if(st)st.textContent=String(err);}}
async function saveRemoteCfg(){var st=document.getElementById("remSt");try{const b=Object.assign({},collectRemoteJson());if(!b.key)delete b.key;const r=await fetch("/api/portal/remote",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(b)});const o=await r.json();if(st)st.textContent=o.ok?"Saved.":"Error: "+(o.error||"?");
if(o.ok)loadRemoteCfg();}catch(e){if(st)st.textContent="Save: "+e}}
async function testRemoteAuth(){var st=document.getElementById("remSt");try{const b=Object.assign({},collectRemoteJson());if(!b.key)delete b.key;const r=await fetch("/api/portal/remote-test",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(b)});const o=await r.json();
if(st)st.textContent="HTTP "+(o.code!=null?o.code:"?")+": "+(o.ok?"ok":"fail")+(o.err?(" — "+o.err):"");
if(o.ok)loadRemoteCfg();}catch(e){if(st)st.textContent="Test: "+e}}
async function loadSysCfg(){
 var st=document.getElementById("sysSt"),note=document.getElementById("sysNote");
 var ap=document.getElementById("sysAp"),sta=document.getElementById("sysSta"),ble=document.getElementById("sysBle"),cell=document.getElementById("sysCell");
 var nu=document.getElementById("sysNuser"),np=document.getElementById("sysNpass"),br=document.getElementById("sysBleRow"),cr=document.getElementById("sysCellRow");
 if(st)st.textContent="…";
 if(np)np.value="";
 try{
  const r=await fetch("/api/portal/cfg");
  const o=await r.json();
  if(o.error){if(st)st.textContent=o.error;return;}
  if(ap)ap.checked=!!o.ap;if(sta)sta.checked=!!o.sta;if(ble)ble.checked=!!o.ble;
  if(cell){cell.checked=!!o.cell;cell.disabled=!o.cellAvail;cell.parentElement.style.opacity=o.cellAvail?"1":"0.55";}
  if(cr)cr.style.display="block";
  if(ble&&o.bleBuild===0){ble.disabled=true;if(br)br.title="BLE not compiled in this firmware";}
  if(nu)nu.value=o.user||"";
  if(note)note.textContent=o.rebootNote?"After saving, reboot to apply radio settings.":"";
  if(st)st.textContent="";
 }catch(e){if(st)st.textContent=String(e);}}
async function saveSysCfg(){
 var st=document.getElementById("sysSt");
 var ap=document.getElementById("sysAp"),sta=document.getElementById("sysSta"),ble=document.getElementById("sysBle"),cell=document.getElementById("sysCell");
 var nu=document.getElementById("sysNuser"),np=document.getElementById("sysNpass");
 var body={ap:ap&&ap.checked?1:0,sta:sta&&sta.checked?1:0,ble:ble&&ble.checked&&!ble.disabled?1:0,cell:cell&&!cell.disabled&&cell.checked?1:0};
 if(nu&&nu.value.trim())body.nuser=nu.value.trim();
 if(np&&np.value)body.npass=np.value;
 try{
  const r=await fetch("/api/portal/cfg",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)});
  const o=await r.json();
  if(st)st.textContent=o.ok?("Saved. Reboot if you changed WiFi/BLE/cell or login."):("Error: "+(o.error||JSON.stringify(o)));
  if(o.ok)loadSysCfg();
 }catch(e){if(st)st.textContent=String(e);}}
async function loadBleCfg(){var st=document.getElementById("bleSt"),onp=document.getElementById("bleOnP"),nm=document.getElementById("bleNm");
if(st)st.textContent="…";try{const r=await fetch("/api/portal/bt");const o=await r.json();
if(o.error&&st){st.textContent=o.error;return}
if(nm)nm.value=o.name||"";
if(onp)onp.textContent="SPP: "+(o.on?"on (firmware)":"off")+" — saved name is used at next power cycle.";
if(st)st.textContent="";}catch(e){if(st)st.textContent=String(e);}}
async function saveBleCfg(){var st=document.getElementById("bleSt"),nm=document.getElementById("bleNm");
var n=nm?nm.value.trim():"";
if(!n){if(st)st.textContent="Enter a name";return}
try{const r=await fetch("/api/portal/bt",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({name:n})});
const o=await r.json();if(st)st.textContent=o.ok?"Saved. Reboot the device to apply.":"Error: "+(o.error||"?");}catch(e){if(st)st.textContent=String(e);}}
