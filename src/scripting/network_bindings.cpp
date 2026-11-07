#include "scripting/network_bindings.h"
#include "network/net_events.h"
#include "network/http_client.h"
#include "app/app_state.h"
namespace linweb {
namespace {
std::string str(JSContext* c, JSValueConst v) { const char* p = JS_ToCString(c,v); if (!p) return {}; std::string s(p); JS_FreeCString(c,p); return s; }
std::map<std::string,std::string> get_headers(JSContext* c, JSValueConst a) {
    std::map<std::string,std::string> h; JSValue n = JS_GetPropertyStr(c,a,"length"); uint32_t size=0; JS_ToUint32(c,&size,n); JS_FreeValue(c,n);
    for (uint32_t i=0;i+1<size;i+=2) { JSValue k=JS_GetPropertyUint32(c,a,i), v=JS_GetPropertyUint32(c,a,i+1); h[str(c,k)]=str(c,v); JS_FreeValue(c,k); JS_FreeValue(c,v); } return h;
}
JSValue start(JSContext* c, JSValueConst, int n, JSValueConst* a) { if(n<4) return JS_ThrowTypeError(c,"Missing request arguments"); return JS_NewInt32(c,net::start_request(str(c,a[0]),resolve_url(g_page_url,str(c,a[1])),get_headers(c,a[2]),str(c,a[3]))); }
JSValue stream(JSContext* c, JSValueConst, int n, JSValueConst* a) { if(n<2) return JS_ThrowTypeError(c,"Missing stream arguments"); return JS_NewInt32(c,net::open_stream(resolve_url(g_page_url,str(c,a[0])),get_headers(c,a[1]))); }
JSValue cancel(JSContext* c, JSValueConst, int n, JSValueConst* a) { int32_t id=0; if(n) JS_ToInt32(c,&id,a[0]); net::cancel(id); return JS_UNDEFINED; }
JSValue take(JSContext* c, JSValueConst, int, JSValueConst*) { auto s=net::take_events(); return JS_NewStringLen(c,s.data(),s.size()); }
JSValue page_url(JSContext* c, JSValueConst, int, JSValueConst*) { return JS_NewString(c,g_page_url.c_str()); }
}
void install_network_bindings(JSEngine& js) {
    js.add_function("__netStart",start); js.add_function("__netOpenStream",stream); js.add_function("__netCancel",cancel); js.add_function("__netTake",take); js.add_function("__pageUrl",page_url);
    js.execute(R"JS(
(() => {
  const pending = new Map();
  const flat = h => { const a=[]; if (h) { if (Array.isArray(h)) h.forEach(x => a.push(String(x[0]),String(x[1]))); else Object.keys(h).forEach(k => a.push(k,String(h[k]))); } return a; };
  const header = (h,n) => { n=String(n).toLowerCase(); for (const k of Object.keys(h || {})) if(k.toLowerCase()===n) return h[k]; return null; };
  window._pump_network = () => { for(const e of JSON.parse(__netTake())) { const f=pending.get(e.id); if(f) { try { f(e); } catch(err) { console.error(err); } } } };
  window.fetch = (url, init) => new Promise((resolve,reject) => { init=init || {}; let id;
    try { id=__netStart(String(init.method || 'GET').toUpperCase(),String(url),flat(init.headers),init.body == null ? '' : String(init.body)); } catch(e) { reject(new TypeError(String(e))); return; }
    pending.set(id,e => { pending.delete(id); if(e.type==='error') { reject(new TypeError(e.message)); return; }
      resolve({ok:e.status>=200 && e.status<300,status:e.status,statusText:'',url:e.url,headers:{get:n=>header(e.headers,n)},text:()=>Promise.resolve(e.body),json:()=>Promise.resolve().then(()=>JSON.parse(e.body))}); });
  });
  class XHR {
    constructor() { this.readyState=0; this.status=0; this.statusText=''; this.responseText=''; this.response=''; this._headers={}; this._listeners={}; }
    _emit(t) { const ev={type:t,target:this}; if(typeof this['on'+t]==='function') this['on'+t](ev); for(const f of (this._listeners[t]||[])) f.call(this,ev); }
    addEventListener(t,f) { (this._listeners[t] || (this._listeners[t]=[])).push(f); }
    open(m,u) { this._method=String(m).toUpperCase(); this._url=String(u); this.readyState=1; this._emit('readystatechange'); }
    setRequestHeader(k,v) { this._headers[k]=String(v); }
    send(body) { const id=__netStart(this._method,this._url,flat(this._headers),body == null ? '' : String(body)); this._id=id; pending.set(id,e=>{ pending.delete(id); this._id=0; if(e.type==='error') { this.readyState=4; this._emit('readystatechange'); this._emit('error'); return; } this.status=e.status; this._responseHeaders=e.headers; this.readyState=2; this._emit('readystatechange'); this.responseText=e.body; this.response=e.body; this.readyState=3; this._emit('readystatechange'); this.readyState=4; this._emit('readystatechange'); this._emit('load'); }); }
    abort() { if(this._id) { __netCancel(this._id); pending.delete(this._id); this._id=0; } this.readyState=0; }
    getResponseHeader(n) { return header(this._responseHeaders,n); }
    getAllResponseHeaders() { return Object.keys(this._responseHeaders||{}).map(k=>k+': '+this._responseHeaders[k]+'\r\n').join(''); }
  }
  window.XMLHttpRequest=XHR;
  class SSE {
    constructor(url) { this.url=String(url); this.readyState=0; this._listeners={}; this._retry=3000; this._last=''; this._buffer=''; this._data=[]; this._event=''; this._connect(); }
    addEventListener(t,f) { (this._listeners[t] || (this._listeners[t]=[])).push(f); }
    _emit(t,e) { e=e||{}; e.type=t; e.target=this; if(typeof this['on'+t]==='function') this['on'+t](e); for(const f of (this._listeners[t]||[])) f.call(this,e); }
    _connect() { if(this.readyState===2) return; this.readyState=0; this._buffer=''; this._data=[]; const h=['Accept','text/event-stream']; if(this._last) h.push('Last-Event-ID',this._last); const id=__netOpenStream(this.url,h); this._id=id; pending.set(id,e=>{ if(e.type==='open') { this.readyState=1; this._emit('open'); } else if(e.type==='chunk') this._feed(e.data); else { pending.delete(id); this._id=0; if(this.readyState!==2) { this.readyState=0; this._emit('error'); setTimeout(()=>this._connect(),this._retry); } } }); }
    _feed(s) { this._buffer+=s; let i; while((i=this._buffer.search(/[\r\n]/))>=0) { if(this._buffer[i]==='\r' && i===this._buffer.length-1) break; let line=this._buffer.slice(0,i); let skip=this._buffer[i]==='\r' && this._buffer[i+1]==='\n'?2:1; this._buffer=this._buffer.slice(i+skip); if(!line) { if(this._data.length) this._emit(this._event||'message',{data:this._data.join('\n'),lastEventId:this._last}); this._data=[]; this._event=''; continue; } if(line[0]===':') continue; let j=line.indexOf(':'), key=j<0?line:line.slice(0,j), val=j<0?'':line.slice(j+1); if(val[0]===' ') val=val.slice(1); if(key==='data') this._data.push(val); else if(key==='event') this._event=val; else if(key==='id' && val.indexOf('\0')<0) this._last=val; else if(key==='retry' && /^\d+$/.test(val)) this._retry=Number(val); } }
    close() { this.readyState=2; if(this._id) { __netCancel(this._id); pending.delete(this._id); this._id=0; } }
  }
  SSE.CONNECTING=0; SSE.OPEN=1; SSE.CLOSED=2; window.EventSource=SSE;
  Object.defineProperty(document,'title',{get(){const t=document.querySelector('title'); return t ? t.textContent.trim() : '';},set(v){let t=document.querySelector('title'); if(!t) { t=document.createElement('title'); (document.querySelector('head')||document.body).appendChild(t); } t.textContent=String(v);}, configurable:true});
  const loc={}; function setUrl(url) { const m=/^(\w+:)\/\/([^/?#]*)([^?#]*)(\?[^#]*)?(#.*)?$/.exec(url); if(!m) return; loc.href=url; loc.protocol=m[1]; loc.host=m[2]; loc.hostname=m[2].split(':')[0]; loc.port=m[2].split(':')[1]||''; loc.origin=m[1]+'//'+m[2]; loc.pathname=m[3]||'/'; loc.search=m[4]||''; loc.hash=m[5]||''; } setUrl(__pageUrl()); loc.reload=()=>{}; loc.assign=()=>{throw new Error('Navigation is not implemented');}; window.location=loc;
  window.navigator={userAgent:'Linweb/0.1',language:'en-US',onLine:true,platform:'Linweb'};
  window.history={length:1,state:null,pushState(s,t,u){this.state=s; this.length++; if(u) setUrl(u[0]==='/'?loc.origin+u:u);},replaceState(s,t,u){this.state=s; if(u) setUrl(u[0]==='/'?loc.origin+u:u);},back(){},forward(){}};
})();
)JS");
}
}
