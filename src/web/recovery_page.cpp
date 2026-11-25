// Lumen — built-in recovery page (compiled into the firmware, ~4 KB).
//
// Always reachable at /recovery, and served for "/" when no web UI pack is
// installed (first flash, or an interrupted UI update). Lets a signed-in
// user upload a web.pack or a firmware image without the React UI. Uses the
// same authenticated API as the UI; the script is a separate file because the
// CSP forbids inline scripts.
#include "lumen/web/recovery_page.hpp"

namespace lumen
{

const char kRecoveryHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lumen recovery</title>
<style>
:root{color-scheme:light dark;--bg:#f6f6f7;--fg:#18181b;--card:#fff;--mut:#5b5b66;--bd:#d4d4d8;--ac:#4338ca;--err:#b91c1c;--ok:#15803d}
@media (prefers-color-scheme:dark){:root{--bg:#111114;--fg:#f4f4f5;--card:#1c1c21;--mut:#a1a1aa;--bd:#3f3f46;--ac:#a5b4fc;--err:#f87171;--ok:#4ade80}}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,sans-serif}
main{max-width:34rem;margin:0 auto;padding:1.5rem 1rem}
section{background:var(--card);border:1px solid var(--bd);border-radius:.75rem;padding:1rem;margin:1rem 0}
h1{font-size:1.4rem;margin:.2rem 0}h2{font-size:1.05rem;margin:0 0 .5rem}
p{color:var(--mut);margin:.3rem 0}label{display:block;margin:.6rem 0 .2rem;font-weight:600;font-size:.9rem}
input{box-sizing:border-box;width:100%;min-height:2.75rem;padding:.4rem .6rem;border:1px solid var(--bd);border-radius:.5rem;background:transparent;color:inherit;font:inherit}
button{min-height:2.75rem;margin-top:.8rem;padding:0 1rem;border:0;border-radius:.5rem;background:var(--ac);color:var(--bg);font:inherit;font-weight:600;cursor:pointer}
progress{width:100%;margin-top:.6rem}.msg{min-height:1.5rem;margin-top:.5rem}.err{color:var(--err)}.ok{color:var(--ok)}[hidden]{display:none}
</style></head><body><main>
<h1>Lumen recovery</h1><p id="why">Use this page to reinstall the web interface or the firmware.</p>
<section id="login"><h2>Sign in</h2>
<label for="u">Username</label><input id="u" autocomplete="username" value="admin">
<label for="p">Password</label><input id="p" type="password" autocomplete="current-password">
<div id="chg" hidden><label for="np">New password (10+ characters)</label><input id="np" type="password" autocomplete="new-password"></div>
<button id="go">Sign in</button><div class="msg" id="lm" role="status"></div></section>
<section id="up" hidden><h2>Web interface</h2><p>Upload <code>web.pack</code> (from <code>npm run build:fs</code>, in <code>data/web/</code>).</p>
<input id="uf" type="file" accept=".pack" aria-label="Web UI pack file"><button id="ub">Install web interface</button>
<progress id="upr" max="1" value="0" hidden></progress><div class="msg" id="um" role="status"></div></section>
<section id="fw" hidden><h2>Firmware</h2><p>Upload <code>firmware.bin</code>. The controller verifies it, restarts, and returns to the previous firmware if the new one does not start correctly.</p>
<input id="ff" type="file" accept=".bin" aria-label="Firmware file"><button id="fb">Install firmware</button>
<progress id="fpr" max="1" value="0" hidden></progress><div class="msg" id="fm" role="status"></div></section>
<p><a href="/" style="color:var(--ac)">Open the controller</a></p>
<script src="/recovery.js"></script></main></body></html>)HTML";

const char kRecoveryJs[] = R"JS((function(){
var csrf=null,$=function(i){return document.getElementById(i)};
function msg(el,t,ok){el.textContent=t;el.className='msg '+(ok?'ok':'err')}
function api(m,p,b){return fetch('/api/v1'+p,{method:m,credentials:'same-origin',headers:Object.assign({'Content-Type':'application/json'},csrf&&m!=='GET'?{'X-CSRF-Token':csrf}:{}),body:b?JSON.stringify(b):undefined}).then(function(r){return r.json()})}
function signedIn(){$('login').hidden=true;$('up').hidden=false;$('fw').hidden=false}
api('GET','/auth/session').then(function(r){if(r.success&&!r.data.forcePasswordChange){csrf=r.data.csrfToken;signedIn()}}).catch(function(){});
$('go').onclick=function(){
 var lm=$('lm');msg(lm,'Signing in…',true);
 api('POST','/auth/login',{username:$('u').value,password:$('p').value}).then(function(r){
  if(!r.success){msg(lm,r.error.message);return}
  csrf=r.data.csrfToken;
  if(r.data.forcePasswordChange){
   if($('chg').hidden){$('chg').hidden=false;msg(lm,'Choose a new password first.');return}
   return api('PUT','/auth/password',{currentPassword:$('p').value,newPassword:$('np').value}).then(function(c){
    if(!c.success){msg(lm,c.error.message);return}
    return api('POST','/auth/login',{username:$('u').value,password:$('np').value}).then(function(l){if(l.success){csrf=l.data.csrfToken;signedIn()}else msg(lm,l.error.message)})})}
  signedIn()}).catch(function(){msg(lm,'The controller did not answer.')})};
function upload(file,path,pr,out,done){
 if(!file){msg(out,'Choose a file first.');return}
 var x=new XMLHttpRequest();x.open('POST','/api/v1'+path);x.setRequestHeader('Content-Type','application/octet-stream');x.setRequestHeader('X-CSRF-Token',csrf);
 pr.hidden=false;pr.value=0;x.upload.onprogress=function(e){if(e.lengthComputable)pr.value=e.loaded/e.total};
 x.onload=function(){var r={};try{r=JSON.parse(x.responseText)}catch(e){}
  if(r.success){pr.value=1;done(r.data)}else msg(out,(r.error&&r.error.message)||('Failed ('+x.status+')'))};
 x.onerror=function(){msg(out,'Connection lost during the upload.')};x.send(file)}
$('ub').onclick=function(){upload($('uf').files[0],'/system/ui/upload',$('upr'),$('um'),function(d){msg($('um'),'Installed ('+d.assets+' files). Opening the controller…',true);setTimeout(function(){location.href='/'},1500)})};
$('fb').onclick=function(){upload($('ff').files[0],'/ota/upload',$('fpr'),$('fm'),function(d){msg($('fm'),'Firmware '+d.newVersion+' verified. The controller is restarting — this page reloads in 30 s.',true);setTimeout(function(){location.reload()},30000)})};
})();)JS";

} // namespace lumen
