static const char OTA_UPDATE_HTML[] PROGMEM = R"HTML(
<hr><h3>Firmware update</h3>
<div id="drop" style="border:2px dashed #888;padding:28px;text-align:center;cursor:pointer">Drop <b>firmware.bin</b> here or click<input id="fw" type="file" accept=".bin" hidden></div>
<progress id="bar" max="100" value="0" style="width:100%;height:22px;margin-top:10px"></progress>
<div id="status">Waiting for firmware.</div><div id="detail" style="font-family:monospace"></div>
<ol id="events" style="font-family:monospace;font-size:.9em"></ol>
<script>
const d=document.getElementById('drop'),f=document.getElementById('fw'),b=document.getElementById('bar'),s=document.getElementById('status'),v=document.getElementById('detail'),ev=document.getElementById('events');
let active=false,lastProgress=0,stallTimer;
const bytes=n=>n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KiB':(n/1048576).toFixed(2)+' MiB';
const duration=n=>!isFinite(n)?'--':n<60?Math.ceil(n)+'s':Math.floor(n/60)+'m '+Math.ceil(n%60)+'s';
function event(text){let x=document.createElement('li');x.textContent=new Date().toLocaleTimeString()+' - '+text;ev.appendChild(x)}
function stage(text){s.textContent=text;event(text)}
d.onclick=()=>!active&&f.click();d.ondragover=e=>{e.preventDefault();d.style.borderColor='#06c'};d.ondragleave=()=>d.style.borderColor='#888';
d.ondrop=e=>{e.preventDefault();d.style.borderColor='#888';if(!active)upload(e.dataTransfer.files[0])};f.onchange=()=>upload(f.files[0]);
function upload(file){
 if(!file)return;if(!file.name.toLowerCase().endsWith('.bin')){stage('Rejected: select a .bin firmware file.');return}
 active=true;b.value=0;v.textContent='';ev.innerHTML='';d.style.opacity='.55';
 let form=new FormData(),xhr=new XMLHttpRequest(),start=performance.now(),lastTime=start,lastBytes=0,speed=0,stalled=false;
 form.append('firmware',file,file.name);stage('Preparing '+file.name+' ('+bytes(file.size)+')');
 xhr.open('POST','/update');xhr.timeout=600000;lastProgress=Date.now();
 stallTimer=setInterval(()=>{if(active&&Date.now()-lastProgress>5000&&!stalled){stalled=true;event('Warning: no upload progress for 5 seconds. Still waiting...')}},1000);
 xhr.upload.onloadstart=()=>stage('Uploading firmware to device...');
 xhr.upload.onprogress=e=>{if(!e.lengthComputable)return;let now=performance.now(),dt=(now-lastTime)/1000,instant=dt?(e.loaded-lastBytes)/dt:0;speed=speed?speed*.7+instant*.3:instant;lastTime=now;lastBytes=e.loaded;lastProgress=Date.now();stalled=false;let pct=e.loaded/e.total*100;b.value=pct;v.textContent=pct.toFixed(1)+'% | '+bytes(e.loaded)+' / '+bytes(e.total)+' | '+bytes(Math.round(speed))+'/s | ETA '+duration((e.total-e.loaded)/speed)+' | elapsed '+duration((now-start)/1000)};
 xhr.upload.onload=()=>{b.value=100;stage('Upload received. Device is validating and finalizing...')};
 xhr.onload=()=>{finish();if(xhr.status>=200&&xhr.status<300){stage(xhr.responseText||'Update complete. Rebooting...');waitForDevice(20)}else stage('Update failed: '+(xhr.responseText||('HTTP '+xhr.status)))};
 xhr.onerror=()=>{finish();stage('Connection lost during update. Check whether the device rebooted.')};xhr.ontimeout=()=>{finish();stage('Update timed out after 10 minutes.')};xhr.onabort=()=>{finish();stage('Update cancelled.')};xhr.send(form);
 function finish(){active=false;clearInterval(stallTimer);d.style.opacity='1'}
}
function waitForDevice(left){if(!left){event('Device did not return yet. Reload manually.');return}stage('Rebooting; checking device ('+left+' attempts left)...');setTimeout(()=>fetch('/',{cache:'no-store'}).then(r=>{if(r.ok){event('Device is back online. Reloading...');setTimeout(()=>location.reload(),800)}else throw 0}).catch(()=>waitForDevice(left-1)),1500)}
</script>
)HTML";

void handleUpdateUpload()
{
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START)
  {
    WiFiUDP::stopAll();
    if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) Log("OTA: not enough space");
  }
  else if(upload.status == UPLOAD_FILE_WRITE)
  {
    if(Update.write(upload.buf, upload.currentSize) != upload.currentSize) Log("OTA: write failed");
  }
  else if(upload.status == UPLOAD_FILE_END)
  {
    if(!Update.end(true)) Log("OTA: finalize failed");
  }
  yield();
}

void handleUpdateFinished()
{
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "Update complete. Rebooting..." : "Update failed.");
  if(ok)
  {
    delay(250);
    ESP.restart();
  }
}

