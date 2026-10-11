
const FORMAT={w:208,h:148,aw:72,ah:88,chunk:768};
function to565(image){const bytes=new Uint8Array(image.width*image.height*2),p=image.data;for(let i=0,j=0;i<p.length;i+=4,j+=2){const v=((p[i]>>3)<<11)|((p[i+1]>>2)<<5)|(p[i+2]>>3);bytes[j]=v&255;bytes[j+1]=v>>8}return bytes}
function rgba565(bytes,w,h){if(bytes.length!==w*h*2)throw Error('图像数据长度不正确');const rgba=new Uint8ClampedArray(w*h*4);for(let i=0,j=0;i<bytes.length;i+=2,j+=4){const v=bytes[i]|bytes[i+1]<<8;rgba[j]=((v>>11)&31)*255/31;rgba[j+1]=((v>>5)&63)*255/63;rgba[j+2]=(v&31)*255/31;rgba[j+3]=255}return rgba}
function validateProfile(p){for(const k of ['name','department','title','employeeId','signature']){if(typeof p[k]!=='string'||new TextEncoder().encode(p[k]).length>128||/[\x00-\x1f\x7f]/.test(p[k]))throw Error('资料内容过长或含有不可显示字符')}if(!p.name.trim())throw Error('请填写姓名或昵称');return p}
function fit(ctx,text,width){if(ctx.measureText(text).width<=width)return text;const chars=Array.from(text);while(chars.length&&ctx.measureText(chars.join('')+'…').width>width)chars.pop();return chars.join('')+'…'}
// Name region is 112x32 in the persisted card; never overwrite adjacent fields.
function nameLayout(ctx,text,width=112,family='"Microsoft YaHei","PingFang SC",sans-serif'){
 const value=String(text).trim().replace(/\s+/gu,' ');
 const graphemes=s=>typeof Intl.Segmenter==='function'?[...new Intl.Segmenter(undefined,{granularity:'grapheme'}).segment(s)].map(x=>x.segment):Array.from(s);
 const font=size=>{ctx.font=`700 ${size}px ${family}`};
 function wrap(){
  const lines=[];let line='';
  for(const word of value.split(' ')){
   const next=line?line+' '+word:word;
   if(ctx.measureText(next).width<=width){line=next;continue}
   if(line){lines.push(line);line=''}
   for(const ch of graphemes(word)){
    if(line&&ctx.measureText(line+ch).width>width){lines.push(line);line=''}
    line+=ch;
   }
  }
  if(line)lines.push(line);return lines;
 }
 font(20);if(ctx.measureText(value).width<=width)return {size:20,lines:[value],truncated:false};
 let lines=wrap();
 // Preserve useful word breaks from the heading size; two compact rows fit the slot.
 if(lines.length<=2){font(14);return {size:14,lines,truncated:false}}
 for(const size of [14,12,10]){font(size);lines=wrap();if(lines.length<=2)return {size,lines,truncated:false}}
 let tail=graphemes(lines.slice(1).join(' '));while(tail.length&&ctx.measureText(tail.join('')+'…').width>width)tail.pop();
 return {size:10,lines:[lines[0],tail.join('')+'…'],truncated:true};
}
function drawName(ctx,text,x=88,y=4,width=112,height=32,family){
 ctx.save();ctx.beginPath();ctx.rect(x,y,width,height);ctx.clip();
 const layout=nameLayout(ctx,text,width,family);ctx.textBaseline='alphabetic';ctx.textAlign='left';
 if(layout.lines.length===1)ctx.fillText(layout.lines[0],x,y+24);
 else layout.lines.forEach((line,i)=>ctx.fillText(line,x,y+13+i*16));
 ctx.restore();return layout;
}
function deviceError(reply){
 const messages={display_pending:'工牌正在刷新资料，请稍候重试。',profile_changed:'这张工牌已被其他页面或小智修改。当前编辑已保留，请核对并重新读取设备资料后保存。',upload_busy:'另一个配置页面正在上传，请等待其完成或断开。',backup_busy:'另一个配置页面正在导入或导出，请等待其完成。'};
 const error=Error(messages[reply.reason]||reply.error||'设备拒绝请求');error.code=reply.error;error.reason=reply.reason;return error;
}
async function beginProfileUpload(device,args){
 // Retry only an explicitly transient display barrier. Never replace a stale
 // baseRevision or automatically overwrite another client's changes.
 for(let attempt=0;;attempt++){
  try{return await device.rpc('begin',args)}catch(e){
   if(e.reason!=='display_pending'||attempt>=15)throw e;
   await new Promise(resolve=>setTimeout(resolve,250));
  }
 }
}
class BadgeSerial{
 constructor(){this.id=0;this.session=Math.random().toString(36).slice(2)+Date.now().toString(36);this.pending=new Map();this.buffer='';this.closed=true}
 async connect(){if(!navigator.serial)throw Error('请在电脑 Chrome 或 Edge 中打开此配置页');this.port=await navigator.serial.requestPort({filters:[{usbVendorId:0x303a,usbProductId:0x1001}]});await this.port.open({baudRate:115200,bufferSize:4096});await this.port.setSignals({dataTerminalReady:false,requestToSend:false});this.writer=this.port.writable.getWriter();this.reader=this.port.readable.getReader();this.closed=false;this.readTask=this.readLoop()}
 async readLoop(){const decoder=new TextDecoder();try{while(true){const {value,done}=await this.reader.read();if(done)break;this.buffer+=decoder.decode(value,{stream:true});let n;while((n=this.buffer.indexOf('\n'))>=0){const line=this.buffer.slice(0,n);this.buffer=this.buffer.slice(n+1);const start=line.indexOf('@BADGE ');if(start<0)continue;try{const r=JSON.parse(line.slice(start+7)),p=this.pending.get(r.id);if(p){clearTimeout(p.timer);this.pending.delete(r.id);r.ok?p.resolve(r):p.reject(deviceError(r))}}catch(e){}}if(this.buffer.length>8192)this.buffer=''}}catch(e){}finally{this.closed=true;this.reader.releaseLock();for(const p of this.pending.values()){clearTimeout(p.timer);p.reject(Error('USB 连接已断开'))}this.pending.clear();this.onClose?.()}}
 async rpc(op,extra={}){if(this.closed)throw Error('请先连接工牌');const id=++this.id;const response=new Promise((resolve,reject)=>{const timer=setTimeout(()=>{this.pending.delete(id);reject(Error('设备响应超时，请检查连接和固件版本'))},12000);this.pending.set(id,{resolve,reject,timer})});try{await this.writer.write(new TextEncoder().encode(JSON.stringify({id,op,session:this.session,...extra})+'\n'))}catch(e){const p=this.pending.get(id);if(p){clearTimeout(p.timer);this.pending.delete(id);p.reject(e)}}return response}
 async disconnect(){if(this.reader&&!this.closed){await this.reader.cancel();await this.readTask}this.writer?.releaseLock();if(this.port)await this.port.close();this.port=null;this.writer=null}
}

const THEME_KEYS=['background','panel','accent','text','muted'];
const DEFAULT_THEME={background:'#08090b',panel:'#181216',accent:'#f03543',text:'#e8e6df',muted:'#a69c9f'};
const DEFAULT_PROFILE={alias:'',name:'FOLO MAKER',department:'网络安全部',title:'安全工程师',employeeId:'NC-2077',signature:'',brandName:'ARASAKA',badgeCaption:'员工身份档案',theme:DEFAULT_THEME,logoCustom:false,qrPresent:false};
function validateExtended(p){validateProfile(p);if(typeof p.alias!=='string'||new TextEncoder().encode(p.alias).length>128||/[\x00-\x1f\x7f]/.test(p.alias))throw Error('请输入有效的代号');for(const k of ['brandName','badgeCaption']){if(typeof p[k]!=='string'||!p[k].trim()||new TextEncoder().encode(p[k]).length>128||/[\x00-\x1f\x7f]/.test(p[k]))throw Error('请填写有效的品牌名称和工牌标题')}for(const k of THEME_KEYS)if(!/^#[0-9a-f]{6}$/i.test(p.theme[k]))throw Error('颜色格式不正确');return p}
function luminance(h){const c=[1,3,5].map(i=>parseInt(h.slice(i,i+2),16)/255).map(x=>x<=.04045?x/12.92:((x+.055)/1.055)**2.4);return c[0]*.2126+c[1]*.7152+c[2]*.0722}
function contrast(a,b){const x=luminance(a),y=luminance(b);return(Math.max(x,y)+.05)/(Math.min(x,y)+.05)}
function packQr(image){const result=new Uint8Array(4608);if(image.width!==192||image.height!==192)throw Error('二维码尺寸不正确');for(let i=0;i<192*192;i++)if(image.data[i*4]<128)result[i>>3]|=128>>(i&7);return result}
function unpackQr(bytes){if(bytes.length!==4608)throw Error('二维码数据不完整');const p=new Uint8ClampedArray(192*192*4);for(let i=0;i<192*192;i++){const c=bytes[i>>3]&(128>>(i&7))?0:255;p.set([c,c,c,255],i*4)}return p}
// Match the device's binary, nearest-center 176px display; keep storage at 192px.
function qrDisplayPixels(source){const data=new Uint8ClampedArray(176*176*4);for(let y=0;y<176;y++)for(let x=0;x<176;x++){const sx=Math.floor((x*192+88)/176),sy=Math.floor((y*192+88)/176),c=source[(sy*192+sx)*4]<128?0:255;data.set([c,c,c,255],(y*176+x)*4)}return data}
function qrPixels(payload){if(!payload.length||payload.length>1200)throw Error('二维码内容过长');const q=qrcodegen.QrCode.encodeSegments([qrcodegen.QrSegment.makeBytes(payload)],qrcodegen.QrCode.Ecc.MEDIUM,1,17,-1,true),scale=Math.floor(192/(q.size+8));if(scale<2)throw Error('二维码过于复杂，无法清晰显示');const data=new Uint8ClampedArray(192*192*4);data.fill(255);const offset=Math.floor((192-q.size*scale)/2);for(let y=0;y<q.size;y++)for(let x=0;x<q.size;x++)if(q.getModule(x,y))for(let dy=0;dy<scale;dy++)for(let dx=0;dx<scale;dx++){const i=((offset+y*scale+dy)*192+offset+x*scale+dx)*4;data[i]=data[i+1]=data[i+2]=0}return data}
class BadgeHttp{
 constructor(){this.id=0;this.session=Math.random().toString(36).slice(2)+Date.now().toString(36);this.closed=true}
 async connect(){const r=await fetch('/session',{cache:'no-store',signal:AbortSignal.timeout(12000)});if(!r.ok)throw Error('请连接工牌热点后打开配置页');this.token=(await r.json()).token;this.closed=false}
 async rpc(op,extra={}){if(this.closed)throw Error('请先连接工牌');const r=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/json','X-Badge-Token':this.token},body:JSON.stringify({id:++this.id,op,session:this.session,...extra}),signal:AbortSignal.timeout(15000)});if(!r.ok)throw Error('热点连接中断，请重新连接');const data=await r.json();if(!data.ok)throw deviceError(data);return data}
 async disconnect(){this.closed=true}
}
