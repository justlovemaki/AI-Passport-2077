const $=id=>document.getElementById(id),ids=['alias','name','department','title','employeeId','signature','brandName','badgeCaption'];
const avatar=$('cropPreview'),av=avatar.getContext('2d'),preview=$('badgePreview'),pc=preview.getContext('2d'),logo=$('logoPreview'),lc=logo.getContext('2d'),qr=$('qrPreview'),qc=qr.getContext('2d');
function canvas(w,h){const c=document.createElement('canvas');c.width=w;c.height=h;return c}
const card=canvas(84,148),cc=card.getContext('2d'),brand=canvas(148,32),bc=brand.getContext('2d'),footer=canvas(208,40),fc=footer.getContext('2d'),headerId=canvas(168,12),hc=headerId.getContext('2d');
const fontsReady=Promise.all([document.fonts.load('700 20px BadgeDisplay'),document.fonts.load('600 18px BadgeSignature'),document.fonts.load('700 18px BadgeBrand')]);
let maxProfileVersion=0,maxBadgeLayout=0;
let source=null,logoSource=null,qrReady=false,device=null,busy=false,dirty=false,previewQr=false,wifi={},wifiDirty=false,xiaozhiBackend={},xiaozhiBackendDirty=false,muse={},museDirty=false;
let loadedAvatarBytes=null,loadedLogoBytes=null,loadedQrBytes=null,avatarDirty=false,logoDirty=false,qrDirty=false;
let scanBusy=false,networks=[];
let editingBadge=0,activeBadge=0,badgeCount=5,badges=[],loadedBadgeRevision=null,staleBadge=false;
const viaHotspot=location.protocol==='http:'&&location.hostname==='192.168.4.1';
function fields(){return {...Object.fromEntries(ids.map(k=>[k,$(k).value.trim()])),theme:Object.fromEntries(THEME_KEYS.map(k=>[k,$(k).value])),logoCustom:!!logoSource,qrPresent:qrReady}}
function message(text,error=false){$('status').textContent=text;$('status').className='status '+(error?'error':'success')}
function controls(){const connected=device&&!device.closed;$('connect').disabled=busy;$('connect').textContent=connected?'断开连接':'连接工牌';for(const k of ['save','reload','saveWifi','saveXiaozhiBackend','saveMuseConfig'])$(k).disabled=busy||!connected;for(const k of ['resetXiaozhiBackend','clearMuseProxy'])$(k).disabled=busy||!connected;$('scanWifi').disabled=busy||scanBusy||!connected;$('scanWifi').textContent=scanBusy?'扫描中…':'刷新列表';for(const el of $('networkList').querySelectorAll('button'))el.disabled=busy||!connected||el.dataset.supported!=='true';$('hotspot').disabled=busy||!connected||viaHotspot;$('hotspot').textContent=wifi.apActive?'关闭工牌热点':'开启工牌热点';$('led').className='led'+(connected?' on':'');$('connectionText').textContent=connected?(viaHotspot?'热点已连接':'USB 已连接'):'未连接';for(const el of $('form').elements)el.disabled=busy;for(const k of ['ssid','wifiPassword','openWifi','xiaozhiBackend','museToken','museStyle','museProxyHost','museProxyPort'])$(k).disabled=busy;$('badgeSelect').disabled=busy||!connected||badgeCount<2;$('displayBadge').disabled=busy||!connected||dirty||!badges[editingBadge]?.configured||editingBadge===activeBadge;$('clearBadge').disabled=busy||!connected||!badges[editingBadge]?.configured;$('save').disabled=busy||!connected||staleBadge;$('stateText').textContent=connected?(dirty?'有未保存的资料修改。':'已读取设备配置，可编辑并保存。'):'连接工牌以读取已保存资料。'}
function drawLogo(ctx,size,t){ctx.fillStyle=t.background;ctx.fillRect(0,0,size,size);if(logoSource){const scale=Math.min(size/logoSource.width,size/logoSource.height),w=logoSource.width*scale,h=logoSource.height*scale;ctx.drawImage(logoSource,(size-w)/2,(size-h)/2,w,h);return}const s=size/64;ctx.save();ctx.scale(s,s);ctx.strokeStyle=t.accent;ctx.fillStyle=t.accent;ctx.lineWidth=3;ctx.beginPath();ctx.arc(32,32,28,0,Math.PI*2);ctx.stroke();ctx.beginPath();ctx.moveTo(32,59);ctx.lineTo(32,17);ctx.moveTo(32,39);ctx.lineTo(14,28);ctx.moveTo(32,39);ctx.lineTo(50,28);ctx.stroke();for(const [x,y]of[[32,17],[14,28],[50,28]]){ctx.beginPath();ctx.arc(x,y,6,0,Math.PI*2);ctx.fill()}ctx.restore()}
// Legacy crop locations stay documented for compatibility tests.
const badgeRegions=[[8,8,72,88,66,74,384],[88,4,112,32,14,211,256],[88,35,112,20,14,246,230],[88,60,112,20,122,246,230],[8,105,192,18,14,263,230],[0,126,208,20,14,279,128]];
function badgeFrame(ctx,t){
 ctx.save();ctx.fillStyle=t.accent;
 ctx.beginPath();ctx.moveTo(0,30);ctx.lineTo(240,30);ctx.lineTo(240,79);ctx.lineTo(137,79);ctx.lineTo(125,89);ctx.lineTo(0,89);ctx.closePath();ctx.fill();
 const ink=luminance(t.accent)>.179?'#08090b':'#ffffff';
 ctx.fillStyle=ink;for(let i=0;i<3;i++){ctx.globalAlpha=i===1?.4:1;ctx.fillRect(213+i*6,38,3,3)}ctx.globalAlpha=1;
 ctx.strokeStyle=t.accent;ctx.lineWidth=1;
 ctx.beginPath();ctx.moveTo(0,293);ctx.lineTo(25,293);ctx.lineTo(34,288);ctx.lineTo(129,288);ctx.lineTo(139,293);ctx.lineTo(240,293);ctx.stroke();
 ctx.beginPath();ctx.moveTo(222,293);ctx.lineTo(231,285);ctx.lineTo(239,285);ctx.stroke();
 ctx.fillRect(33,287,2,2);ctx.fillRect(138,292,2,2);
 ctx.restore();
}
function wrapRows(ctx,text,width,limit){
 const rows=[];let row='';
 // Keep Latin words together; split oversized tokens only at grapheme boundaries.
 const tokens=text.match(/[A-Za-z0-9\u00c0-\u024f]+|\s+|[^A-Za-z0-9\u00c0-\u024f\s]+/gu)||[];
 const glyphs=s=>typeof Intl.Segmenter==='function'?[...new Intl.Segmenter(undefined,{granularity:'grapheme'}).segment(s)].map(x=>x.segment):Array.from(s);
 for(const token of tokens){
  if(!token.trim()){if(row)row+=' ';continue}
  if(ctx.measureText(token).width<=width){if(row&&ctx.measureText(row+token).width>width){rows.push(row.trim());row=''}row+=token;continue}
  for(const ch of glyphs(token)){if(row&&ctx.measureText(row+ch).width>width){rows.push(row.trim());row=''}row+=ch}
 }
 if(row.trim())rows.push(row.trim());
 if(rows.length>limit){const tail=rows.slice(limit-1).join('');rows.splice(limit-1);rows.push(fit(ctx,tail,width))}
 return rows;
}
function drawRows(ctx,text,x,y,width,limit,lineHeight){const rows=wrapRows(ctx,text,width,limit);rows.forEach((line,i)=>ctx.fillText(line,x,y+i*lineHeight));return rows;}
const badgeBodyFont='"Microsoft YaHei","PingFang SC",sans-serif';
function textBlock(ctx,key,text,size,lineHeight,font,color,limit=2,width=84){
 ctx.font=font(size);return {key,size,lineHeight,font:font(size),color,rows:wrapRows(ctx,text,width,limit)};
}
function identityTextLayout(ctx,p){
 const display=n=>`700 ${n}px BadgeDisplay,${badgeBodyFont}`,body=n=>`500 ${n}px ${badgeBodyFont}`,department=n=>`600 ${n}px ${badgeBodyFont}`;
 let nameSize=16;ctx.font=display(nameSize);
 while(nameSize>14&&ctx.measureText(p.name).width>84)ctx.font=display(--nameSize);
 const name=textBlock(ctx,'name',p.name,nameSize,nameSize===16?21:16,display,p.theme.text);
 if(name.rows.length===1)name.lineHeight=32;
 const blocks=[name,textBlock(ctx,'department',p.department,12,17,department,p.theme.text),textBlock(ctx,'title',p.title,11,16,body,p.theme.muted)];
 const tops=[0,44,90],heights=[32,34,32];
 blocks.forEach((b,i)=>{b.top=tops[i];b.height=heights[i]});
 return blocks;
}
function paintTextBlock(ctx,b,x=0,width=84,align='left'){
 if(!b.rows.length)return;
 ctx.save();ctx.font=b.font;ctx.fillStyle=b.color;ctx.textAlign=align;
 // Center actual ink in each line box, including Chinese and Latin descenders.
 const samples=b.rows.map(s=>ctx.measureText(s)),ascent=Math.max(...samples.map(m=>m.actualBoundingBoxAscent??b.size*.85)),descent=Math.max(...samples.map(m=>m.actualBoundingBoxDescent??b.size*.2));
 const baseline=b.top+(b.height-b.rows.length*b.lineHeight)/2+Math.max(ascent,(b.lineHeight-ascent-descent)/2+ascent);
 ctx.beginPath();ctx.rect(x,b.top,width,b.height);ctx.clip();
 b.rows.forEach((line,i)=>ctx.fillText(line,align==='right'?x+width:x,Math.round(baseline+i*b.lineHeight)));ctx.restore();
}
function signatureTextLayout(ctx,p){
 const cjk=/[\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}\p{Script=Hangul}]/u.test(p.signature),font=n=>cjk?`${n}px "KaiTi","STKaiti",${badgeBodyFont}`:`600 ${n}px BadgeSignature,cursive`;
 let b;
 for(let size=cjk?13:18;size>=10;size--){
  b=textBlock(ctx,'signature',p.signature,size,size+3,font,p.theme.accent,99,88);
  if(b.rows.length*b.lineHeight<=38)break;
 }
 // Long signatures use three readable 13px line boxes rather than overlapping cursive.
 b.rows=wrapRows(ctx,p.signature,88,Math.floor(40/b.lineHeight));b.height=b.rows.length*b.lineHeight;b.top=Math.floor((40-b.height)/2);return b;
}
function paintBrandName(ctx,text,color){
 ctx.save();ctx.fillStyle=color;ctx.textAlign='left';ctx.textBaseline='alphabetic';
 ctx.fontKerning='normal';ctx.letterSpacing='0px';ctx.wordSpacing='0px';
 const font=size=>`700 ${size}px BadgeBrand,${badgeBodyFont}`;
 let size=18;ctx.font=font(size);
 while(size>11&&ctx.measureText(text).width>144)ctx.font=font(--size);
 // Reduce font size uniformly; never squeeze a wide name horizontally.
 const fitted=fit(ctx,text,144),metrics=ctx.measureText(fitted);
 const left=Math.max(0,metrics.actualBoundingBoxLeft||0);
 ctx.fillText(fitted,2+left,16);ctx.restore();
}
function renderPortraitAssets(p){
 const t=p.theme,aw=112,ah=144,ink=luminance(t.accent)>.179?'#08090b':'#ffffff';
 av.fillStyle=t.panel;av.fillRect(0,0,aw,ah);
 if(source){const scale=Math.max(aw/source.width,ah/source.height)*Number($('zoom').value),w=source.width*scale,h=source.height*scale;av.drawImage(source,(aw-w)/2+Number($('cropX').value)/100*(w-aw)/2,(ah-h)/2+Number($('cropY').value)/100*(h-ah)/2,w,h)}
 else{av.fillStyle=t.muted;av.beginPath();av.arc(56,47,24,0,Math.PI*2);av.fill();av.beginPath();av.ellipse(56,137,45,51,0,0,Math.PI*2);av.fill()}
 cc.fillStyle=t.background;cc.fillRect(0,0,84,148);
 const blocks=identityTextLayout(cc,p);
 for(let i=0;i<blocks.length;i++){
  const b=blocks[i];
  if(i&&b.key!=='alias'){const prev=blocks[i-1],y=Math.floor((prev.top+prev.height+b.top)/2);cc.fillStyle=t.muted;cc.globalAlpha=.18;cc.fillRect(0,y,84,1);cc.globalAlpha=1;}
  paintTextBlock(cc,b);
 }
 fc.fillStyle=t.background;fc.fillRect(0,0,208,40);
 let title=(p.alias||p.name).toUpperCase(),size=40;
 const font=n=>`700 ${n}px BadgeDisplay,"Microsoft YaHei",sans-serif`;
 fc.font=font(size);while(size>24&&fc.measureText(title).width>108/.65){fc.font=font(--size)}title=fit(fc,title,108/.65);
 const widthScale=Math.min(1,108/Math.max(1,fc.measureText(title).width));
 const letters=Array.from(title),split=Math.max(1,Math.ceil(letters.length*.6)),solid=letters.slice(0,split).join(''),outline=letters.slice(split).join('');
 fc.save();fc.beginPath();fc.rect(0,0,115,40);fc.clip();fc.transform(widthScale,0,-.13,1,5,0);fc.fillStyle=t.accent;fc.strokeStyle=t.accent;fc.lineWidth=1;fc.fillText(solid,0,36);fc.strokeText(outline,fc.measureText(solid).width,36);fc.restore();
 const signature=signatureTextLayout(fc,p);if(signature.rows.length)paintTextBlock(fc,signature,120,88,'right');
 hc.fillStyle=t.accent;hc.fillRect(0,0,168,12);hc.fillStyle=ink;hc.font=`10px Consolas,${badgeBodyFont}`;hc.textAlign='right';hc.fillText(fit(hc,p.employeeId,164),168,10);
 drawLogo(lc,32,{...t,background:t.accent,accent:ink});
 bc.fillStyle=t.accent;bc.fillRect(0,0,148,32);paintBrandName(bc,p.brandName,ink);
 bc.fillStyle=ink;bc.font=`600 9px ${badgeBodyFont}`;bc.globalAlpha=.8;bc.fillText(fit(bc,p.badgeCaption,148),0,30);bc.globalAlpha=1;
}
function assetOrRender(existing,dirty,expected,render){return !dirty&&existing&&existing.length===expected?existing:render()}
function portraitPayload(){
 const atlas=new Uint8Array(43264);atlas.set(to565(cc.getImageData(0,0,84,124)));atlas.set(to565(hc.getImageData(0,0,168,12)),20832);atlas.set(to565(fc.getImageData(0,0,208,40)),24864);atlas.set([73,68,72,49],41504);
 const avatarBytes=assetOrRender(loadedAvatarBytes,avatarDirty,112*144*2,()=>to565(av.getImageData(0,0,112,144)));
 const logoBytes=logoSource?assetOrRender(loadedLogoBytes,logoDirty,32*32*2,()=>to565(lc.getImageData(0,0,32,32))):to565(lc.getImageData(0,0,32,32));
 const qrBytes=qrReady?assetOrRender(loadedQrBytes,qrDirty,4608,()=>packQr(qc.getImageData(0,0,192,192))):new Uint8Array(4608);
 const parts=[atlas,avatarBytes,to565(bc.getImageData(0,0,148,32)),logoBytes,qrBytes];
 const bytes=new Uint8Array(91648);let cursor=0;for(const part of parts){bytes.set(part,cursor);cursor+=part.length}if(cursor!==bytes.length)throw Error('资料格式错误');return bytes;
}
function drawTacticalPhoto(ctx,t){
 ctx.drawImage(avatar,0,90,140,180);
 const rgba=(hex,a)=>`rgba(${parseInt(hex.slice(1,3),16)},${parseInt(hex.slice(3,5),16)},${parseInt(hex.slice(5,7),16)},${a})`;
 let g=ctx.createLinearGradient(87,0,140,0);g.addColorStop(0,rgba(t.background,0));g.addColorStop(1,t.background);ctx.fillStyle=g;ctx.fillRect(87,90,53,180);
 g=ctx.createLinearGradient(0,226,0,270);g.addColorStop(0,rgba(t.background,0));g.addColorStop(1,t.background);ctx.fillStyle=g;ctx.fillRect(0,226,140,44);
}
function render(){const p=fields(),t=p.theme;document.documentElement.style.setProperty('--bg',t.background);document.documentElement.style.setProperty('--panel',t.panel);document.documentElement.style.setProperty('--red',t.accent);document.documentElement.style.setProperty('--ink',t.text);document.documentElement.style.setProperty('--muted',t.muted);document.documentElement.style.setProperty('--button-ink',luminance(t.accent)>.179?'#08090b':'#ffffff');$('brandHeading').textContent=p.brandName||'BADGE';
 const ratio=Math.min(contrast(t.text,t.panel),contrast(t.muted,t.panel),contrast(t.text,t.background),contrast(t.muted,t.background));$('contrast').textContent=ratio<4.5?'文字对比度偏低，建议调亮文字或调暗背景。':'文字与背景对比度良好。';
 renderPortraitAssets(p);
 pc.fillStyle=t.background;pc.fillRect(0,0,240,320);pc.fillStyle=t.muted;pc.font='10px monospace';pc.fillText('AI Passport',14,20);pc.textAlign='right';pc.fillText((editingBadge+1)+'/'+badgeCount,172,20);pc.textAlign='left';pc.fillStyle=t.text;pc.textAlign='right';pc.fillText('86%',226,20);pc.textAlign='left';pc.fillStyle=t.accent;pc.globalAlpha=.4;pc.fillRect(14,28,212,1);pc.fillRect(14,291,212,1);pc.globalAlpha=1;
 badgeFrame(pc,t);pc.drawImage(brand,46,43);pc.drawImage(logo,12,43,26,26);pc.drawImage(headerId,60,31);
 if(previewQr){
  if(qrReady){const view=canvas(176,176),vc=view.getContext('2d'),pixels=qrDisplayPixels(qc.getImageData(0,0,192,192).data);vc.putImageData(new ImageData(pixels,176,176),0,0);pc.drawImage(view,32,100);}
  else{pc.textAlign='center';pc.fillStyle=t.text;pc.font='14px "Microsoft YaHei",sans-serif';pc.fillText('还没有上传二维码',120,170);pc.fillStyle=t.muted;pc.fillText('请在配置页上传微信二维码',120,197);pc.textAlign='left';}
  pc.fillStyle=t.muted;pc.font='10px "Microsoft YaHei",sans-serif';pc.textAlign='center';pc.fillText('下/OK返回工牌 长OK返回',120,310);pc.textAlign='left';
 }else{drawTacticalPhoto(pc,t);pc.drawImage(card,0,0,84,124,145,98,84,124);pc.drawImage(footer,0,0,120,40,8,250,120,40);pc.drawImage(footer,120,0,88,40,128,244,88,40);pc.fillStyle=t.text;pc.font='10px "Microsoft YaHei",sans-serif';pc.textAlign='center';pc.fillStyle=t.muted;pc.fillText('上小程序 下亮码 OK菜单 长OK切换 长上小智',120,310);pc.textAlign='left'}
 $('showBadge').className=previewQr?'':'selected';$('showQr').className=previewQr?'selected':'';
}
function changed(){dirty=true;render();controls()}
for(const id of [...ids,...THEME_KEYS])$(id).addEventListener('input',()=>{if(THEME_KEYS.includes(id))$('preset').value='custom';changed()});
for(const id of ['zoom','cropX','cropY'])$(id).addEventListener('input',()=>{avatarDirty=true;loadedAvatarBytes=null;changed()});
$('showBadge').onclick=()=>{previewQr=false;render()};$('showQr').onclick=()=>{previewQr=true;render()};
const presets={arasaka:DEFAULT_THEME,cyan:{background:'#091115',panel:'#102028',accent:'#55d9f3',text:'#edf5f5',muted:'#a0b7bf'},amber:{background:'#120e09',panel:'#241b11',accent:'#f5b84b',text:'#f0e4cf',muted:'#bcb09a'},green:{background:'#090f0c',panel:'#132019',accent:'#9ad875',text:'#e6efe2',muted:'#a5b7a5'}};
$('preset').onchange=()=>{const t=presets[$('preset').value];if(t){for(const k of THEME_KEYS)$(k).value=t[k];changed()}};
async function readImage(file){if(!file||!['image/jpeg','image/png','image/webp'].includes(file.type)||file.size>10*1024*1024)throw Error('请选择 10 MB 以内的 JPG、PNG 或 WebP 图片');const url=URL.createObjectURL(file);try{const img=new Image();img.src=url;await img.decode();return img}finally{URL.revokeObjectURL(url)}}
$('photo').onchange=async()=>{if(!$('photo').files[0])return;try{source=await readImage($('photo').files[0]);loadedAvatarBytes=null;avatarDirty=true;$('zoom').value=1;$('cropX').value=$('cropY').value=0;changed();message('头像已载入，请保存到工牌。')}catch(e){message(e.message,true)}};
$('logo').onchange=async()=>{if(!$('logo').files[0])return;try{logoSource=await readImage($('logo').files[0]);loadedLogoBytes=null;logoDirty=true;changed();message('品牌图标已载入。')}catch(e){message(e.message,true)}};
$('resetLogo').onclick=()=>{logoSource=null;loadedLogoBytes=null;logoDirty=true;$('logo').value='';changed()};
$('removePhoto').onclick=()=>{source=null;loadedAvatarBytes=null;avatarDirty=true;$('photo').value='';$('zoom').value=1;$('cropX').value=$('cropY').value=0;changed()};
function drawQrCandidate(img,rect,maxSide,enhance=false,smooth=true){
 const scale=Math.min(1,maxSide/Math.max(rect.w,rect.h)),w=Math.max(1,Math.round(rect.w*scale)),h=Math.max(1,Math.round(rect.h*scale)),c=canvas(w,h),ctx=c.getContext('2d',{willReadFrequently:true});
 ctx.fillStyle='#fff';ctx.fillRect(0,0,w,h);ctx.imageSmoothingEnabled=smooth;ctx.drawImage(img,rect.x,rect.y,rect.w,rect.h,0,0,w,h);
 const raw=ctx.getImageData(0,0,w,h);
 if(enhance){
  const p=raw.data;let min=255,max=0;
  for(let i=0;i<p.length;i+=4){const y=.2126*p[i]+.7152*p[i+1]+.0722*p[i+2];if(y<min)min=y;if(y>max)max=y}
  const stretch=max-min>12?255/(max-min):1;
  for(let i=0;i<p.length;i+=4){let y=(.2126*p[i]+.7152*p[i+1]+.0722*p[i+2]-min)*stretch;y=y<128?0:255;p[i]=p[i+1]=p[i+2]=y;p[i+3]=255}
 }
 return {raw,w,h};
}
function qrCandidateRects(img){
 const scale=Math.min(1,1400/Math.max(img.width,img.height)),w=Math.max(1,Math.round(img.width*scale)),h=Math.max(1,Math.round(img.height*scale)),c=canvas(w,h),ctx=c.getContext('2d',{willReadFrequently:true});
 ctx.fillStyle='#fff';ctx.fillRect(0,0,w,h);ctx.drawImage(img,0,0,w,h);
 const p=ctx.getImageData(0,0,w,h).data,mask=new Uint8Array(w*h),ii=new Uint32Array((w+1)*(h+1));
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){
  const i=(y*w+x)*4,r=p[i],g=p[i+1],b=p[i+2],max=Math.max(r,g,b),min=Math.min(r,g,b),lum=.2126*r+.7152*g+.0722*b,sat=max-min;
  // WeChat codes can be black or themed. Treat dark modules and saturated
  // colored modules as foreground, then search for the largest dense square.
  mask[y*w+x]=y>h*.2&&(lum<205||(sat>32&&lum<246))?1:0;
 }
 for(let y=1;y<=h;y++){
  let row=0;
  for(let x=1;x<=w;x++){row+=mask[(y-1)*w+x-1];ii[y*(w+1)+x]=ii[(y-1)*(w+1)+x]+row}
 }
 const sum=(x,y,s)=>ii[(y+s)*(w+1)+x+s]-ii[y*(w+1)+x+s]-ii[(y+s)*(w+1)+x]+ii[y*(w+1)+x];
 const minSide=Math.min(w,h),hits=[];
 for(const ratio of [.8,.72,.64,.56,.5,.44,.38,.32]){
  const s=Math.max(96,Math.round(minSide*ratio)),step=Math.max(14,Math.round(s/7));
  if(s>w||s>h)continue;
  for(let y=Math.floor(h*.2);y<=h-s;y+=step)for(let x=0;x<=w-s;x+=step){
   const count=sum(x,y,s),density=count/(s*s);
   if(density>.035)hits.push({x,y,s,count,density,score:count*Math.min(1,density/.22)});
  }
 }
 hits.sort((a,b)=>b.score-a.score);
 const rects=[];
 for(const hit of hits.slice(0,8)){
  const cx=hit.x+hit.s/2,cy=hit.y+hit.s/2;
  for(const mul of [1.14,1,.88]){
   const side=hit.s*mul,x=Math.max(0,(cx-side/2)/scale),y=Math.max(0,(cy-side/2)/scale),s=Math.min(side/scale,img.width-x,img.height-y);
   if(s>120)rects.push({x,y,w:s,h:s});
  }
 }
 return rects;
}
function qrDecodeAttempts(img){
 const rects=[...qrCandidateRects(img),{x:0,y:0,w:img.width,h:img.height}];
 const minSide=Math.min(img.width,img.height),cx=img.width/2,cy=img.height/2;
 for(const ratio of [.96,.86,.76,.66,.56,.46,.36]){
  const side=Math.round(minSide*ratio),x=Math.max(0,Math.round(cx-side/2)),y=Math.max(0,Math.round(cy-side/2));
  rects.push({x,y,w:Math.min(side,img.width-x),h:Math.min(side,img.height-y)});
 }
 const seen=new Set(),attempts=[];
 for(const rect of rects)for(const maxSide of [512,384,640,900,1200,1600]){
  const key=[rect.x,rect.y,rect.w,rect.h,maxSide].join(',');
  if(seen.has(key))continue;seen.add(key);attempts.push({rect,maxSide,enhance:false,smooth:true});attempts.push({rect,maxSide,enhance:false,smooth:false});attempts.push({rect,maxSide,enhance:true,smooth:false});
 }
 return attempts;
}
function sameBytes(a,b){if(!a||!b||a.length!==b.length)return false;for(let i=0;i<a.length;i++)if(a[i]!==b[i])return false;return true}
async function decodeUploadedQr(img){
 await new Promise(r=>setTimeout(r,30));
 for(const attempt of qrDecodeAttempts(img)){
  const {raw,w,h}=drawQrCandidate(img,attempt.rect,attempt.maxSide,attempt.enhance,attempt.smooth),result=jsQR(raw.data,w,h,{inversionAttempts:'attemptBoth'});
  if(result)return result;
 }
 return null;
}
function qrPlaceholder(){qc.fillStyle='#181216';qc.fillRect(0,0,192,192);qc.fillStyle='#a69c9f';qc.font='13px sans-serif';qc.fillText('等待上传二维码',49,101)}
$('qrFile').onchange=async()=>{if(!$('qrFile').files[0])return;busy=true;controls();try{message('正在本机识别二维码…');const img=await readImage($('qrFile').files[0]),result=await decodeUploadedQr(img);if(!result)throw Error('没有识别到二维码，请上传微信保存的原图或更清晰的截图');const pixels=qrPixels(result.binaryData),check=jsQR(pixels,192,192);if(!check||!sameBytes(check.binaryData,result.binaryData))throw Error('二维码过于复杂，缩放后无法可靠识别');const displayCheck=jsQR(qrDisplayPixels(pixels),176,176);if(!displayCheck||!sameBytes(displayCheck.binaryData,result.binaryData))throw Error('二维码过于复杂，无法在工牌亮码区域清晰识别');qc.putImageData(new ImageData(pixels,192,192),0,0);loadedQrBytes=null;qrDirty=true;qrReady=true;$('qrStatus').textContent='二维码已识别并校验，保存后可在工牌菜单中展示。';previewQr=true;changed();message('二维码已准备好，点击「保存到工牌」。')}catch(e){message(e.message,true)}finally{busy=false;controls()}};
$('removeQr').onclick=()=>{qrReady=false;loadedQrBytes=null;qrDirty=true;$('qrFile').value='';qrPlaceholder();$('qrStatus').textContent='尚未上传二维码。';changed()};
async function readAsset(asset,size,badge=editingBadge){const bytes=new Uint8Array(size);for(let offset=0;offset<size;offset+=FORMAT.chunk){const r=await device.rpc('read',{badge,asset,offset,count:Math.min(FORMAT.chunk,size-offset)}),raw=atob(r.data);if(raw.length!==Math.min(FORMAT.chunk,size-offset))throw Error('设备返回的图像数据不完整');bytes.set(Uint8Array.from(raw,c=>c.charCodeAt(0)),offset)}return bytes}
function from565(bytes,w,h){const c=canvas(w,h);c.getContext('2d').putImageData(new ImageData(rgba565(bytes,w,h),w,h),0,0);return c}
function renderKnownWifis(){
 const list=$('knownWifiList');if(!list)return;list.replaceChildren();
 const known=wifi.knownWifis||[];
 if(!known.length){$('knownWifiSection').hidden=true;return}
 $('knownWifiSection').hidden=false;
 $('knownCount').textContent=`(${known.length}/8)`;
 for(const item of known){
  const row=document.createElement('div');row.className='network-row';
  const name=document.createElement('span');name.className='network-name';
  name.textContent=item.ssid+(item.ssid===wifi.ssid?' · (当前)':item.hasPassword?' · (已存密)':'');
  const btnGroup=document.createElement('div');btnGroup.style.display='flex';btnGroup.style.gap='8px';
  const useBtn=document.createElement('button');useBtn.type='button';useBtn.textContent='选用';useBtn.style.padding='4px 8px';useBtn.style.fontSize='12px';
  useBtn.onclick=()=>{
   $('ssid').value=item.ssid;$('wifiPassword').value='';$('openWifi').checked=!item.hasPassword;
   wifiDirty=true;
   $('wifiPassword').placeholder=item.hasPassword?'已保存密码，留空可直接使用原密码连接':'此网络无需密码';
   controls();message(`已选用网络「${item.ssid}」，已保存密码，点击「保存并连接」即可！`);
  };
  const delBtn=document.createElement('button');delBtn.type='button';delBtn.textContent='删除';delBtn.style.padding='4px 8px';delBtn.style.fontSize='12px';delBtn.style.color='var(--red)';
  delBtn.onclick=async()=>{
   if(!confirm(`确认从工牌中删除已知网络「${item.ssid}」吗？`))return;
   busy=true;controls();
   try{
    await device.rpc('wifi_delete',{ssid:item.ssid});
    message(`已删除网络：${item.ssid}`);
    await new Promise(r=>setTimeout(r,500));
    showWifi((await device.rpc('wifi_info')).wifi,false);
   }catch(e){message('删除失败：'+e.message,true)}finally{busy=false;controls()}
  };
  btnGroup.append(useBtn,delBtn);row.append(name,btnGroup);list.append(row);
 }
}

function showWifi(w,loadFields=false){
 wifi=w||{};
 if(loadFields&&!wifiDirty){
  $('ssid').value=wifi.ssid||'';$('wifiPassword').value='';
  $('openWifi').checked=!!wifi.ssid&&!wifi.passwordSet;
  $('wifiPassword').placeholder=wifi.passwordSet?'已保存密码，留空保留':'输入 Wi-Fi 密码'
 }
 let info=wifi.connected?'已连接 '+wifi.ssid+' · '+wifi.ip:(wifi.ssid?'已保存 '+wifi.ssid+' · '+(wifi.message||'未连接'):'尚未设置 Wi-Fi');
 if(wifi.apActive)info+='\n热点：'+wifi.apSsid+'\n热点密码：'+wifi.apPassword+'\n手机打开：http://192.168.4.1';
 $('wifiStatus').textContent=info;
 renderKnownWifis();
 controls()
}
function showXiaozhiBackend(xz,loadFields=false){xiaozhiBackend=xz||{};const url=xiaozhiBackend.backendUrl||'https://api.tenclass.net/xiaozhi/ota/';if(loadFields&&!xiaozhiBackendDirty)$('xiaozhiBackend').value=xiaozhiBackend.backendCustom?url:'';$('xiaozhiBackendStatus').textContent=(xiaozhiBackend.backendCustom?'当前使用自定义后端：':'当前使用官方默认后端：')+url;controls()}
function showMuseConfig(value,loadFields=false){muse=value||{};if(loadFields&&!museDirty){$('museToken').value='';$('museStyle').value=String(muse.style??0);$('museProxyHost').value=muse.proxyHost||'';$('museProxyPort').value=muse.proxyPort||''}$('museToken').placeholder=muse.tokenSet?'已保存 Token，留空保留':'mgst_…';$('museStatus').textContent=(muse.tokenSet?'SDK Token：已保存':'SDK Token：未设置')+'\n账号配对：'+(muse.paired?'已配对':'未配对')+'\n网络代理：'+(muse.proxyHost?muse.proxyHost+':'+muse.proxyPort:'直连（未设置）');controls()}
function museConfigPayload(){const token=$('museToken').value.trim(),style=Number($('museStyle').value),proxyHost=$('museProxyHost').value.trim(),rawPort=$('museProxyPort').value.trim();if(!Number.isInteger(style)||style<0||style>3)throw Error('Muse 形象风格无效。');if(token&&(!token.startsWith('mgst_')||token.length<12||token.length>=64||/[\s\x00-\x20\x7f]/.test(token)))throw Error('SDK Token 格式无效，应以 mgst_ 开头。');if(!proxyHost){if(rawPort)throw Error('清空代理地址时端口也必须清空。');return {token,style,proxyHost:'',proxyPort:0}}const parts=proxyHost.split('.');if(parts.length!==4||parts.some(x=>!/^\d{1,3}$/.test(x)||Number(x)>255))throw Error('代理地址必须是局域网 IPv4，不能填写网址或域名。');const octets=parts.map(Number);if(octets.every(x=>x===0)||octets[0]===127||octets[0]>=224||octets[3]===255)throw Error('代理地址不能使用回环、组播或广播地址。');const proxyPort=Number(rawPort);if(!Number.isInteger(proxyPort)||proxyPort<1||proxyPort>65535)throw Error('代理端口必须是 1–65535。');return {token,style,proxyHost,proxyPort}}
function xiaozhiBackendPayload(){const url=$('xiaozhiBackend').value.trim();if(!url)return {url:''};if(url.length>255||(!url.startsWith('https://')&&!url.startsWith('http://'))||/[\s\x00-\x20\x7f]/.test(url))throw Error('小智后端地址必须以 http:// 或 https:// 开头，且不能包含空格。');return {url}}
function showCatalog(info){
 activeBadge=info.activeBadge??0;badgeCount=info.badgeCount??1;
 badges=info.badges??[{id:0,configured:!!info.custom,name:info.profile?.name||'',revision:info.badgeRevision??info.revision}];
 const select=$('badgeSelect');select.replaceChildren();
 for(const b of badges){const o=document.createElement('option');o.value=String(b.id);o.textContent=`${b.id+1} · ${b.configured?(b.name||'已设置'):'未设置'}${b.id===activeBadge?' · 当前显示':''}`;select.append(o)}
 select.value=String(editingBadge);
 staleBadge=loadedBadgeRevision!==null&&badges[editingBadge]?.revision!==loadedBadgeRevision;
 $('badgeStatus').textContent=`正在编辑：工牌 ${editingBadge+1}　当前显示：工牌 ${activeBadge+1}`+(staleBadge?'。设备上的这张资料已变更，请重新读取。':'');
 $('displayBadge').textContent=editingBadge===activeBadge?'正在显示这张工牌':'显示这张工牌';controls();
}
async function load(target=null){
 const info=await device.rpc('info',target===null?{}:{badge:target});
 if(info.protocol!==2||info.cardWidth!==208||![76,104,148].includes(info.cardHeight))throw Error('请先安装 1.3.0 或更新版本固件');
 maxProfileVersion=info.maxProfileVersion??2;maxBadgeLayout=info.maxBadgeLayout??1;
 target=info.badge??0;
 const p={...DEFAULT_PROFILE,...info.profile,theme:{...DEFAULT_THEME,...info.profile?.theme}};
 let nextAvatar=null,nextLogo=null,nextQr=null,nextAvatarBytes=null,nextLogoBytes=null;
 if(info.custom){nextAvatarBytes=await readAsset('avatar',info.avatarWidth*info.avatarHeight*2,target);nextAvatar=from565(nextAvatarBytes,info.avatarWidth,info.avatarHeight);if(info.profileVersion>=2&&p.logoCustom){nextLogoBytes=await readAsset('logo',2048,target);nextLogo=from565(nextLogoBytes,32,32)}if(info.qrPresent)nextQr=await readAsset('qr',4608,target)}
 const check=await device.rpc('info',{badge:target});
 if((check.badgeRevision??check.revision)!==(info.badgeRevision??info.revision))throw Error('读取期间这张工牌资料发生变化，请重新读取');
 editingBadge=target;loadedBadgeRevision=info.badgeRevision??info.revision;
 for(const k of ids)$(k).value=p[k];for(const k of THEME_KEYS)$(k).value=p.theme[k];
 $('preset').value='custom';source=nextAvatar;logoSource=nextLogo;qrReady=!!nextQr;loadedAvatarBytes=nextAvatarBytes;loadedLogoBytes=nextLogoBytes;loadedQrBytes=nextQr;avatarDirty=false;logoDirty=false;qrDirty=false;
 if(nextQr)qc.putImageData(new ImageData(unpackQr(nextQr),192,192),0,0);else qrPlaceholder();
 $('qrStatus').textContent=qrReady?'已读取这张工牌的二维码。':'尚未上传二维码。';
 $('zoom').value=1;$('cropX').value=$('cropY').value=0;dirty=false;wifiDirty=false;xiaozhiBackendDirty=false;museDirty=false;
 showYaoTime(info.yaoTime,info.yaoLocationStatus);showWifi(info.wifi,true);showXiaozhiBackend(info.xiaozhi,true);showMuseConfig(info.muse,true);showCatalog(check);render();message(`已读取工牌 ${editingBadge+1} 的品牌、头像、资料和二维码。`);
}
$('badgeSelect').onchange=async()=>{
 const next=Number($('badgeSelect').value);$('badgeSelect').value=String(editingBadge);
 if((dirty||wifiDirty||xiaozhiBackendDirty||museDirty)&&!confirm('切换编辑会放弃尚未保存的修改，继续吗？'))return;
 busy=true;controls();try{await load(next)}catch(e){message(e.message,true)}finally{busy=false;controls()}
};
$('displayBadge').onclick=async()=>{
 if(dirty){message('请先保存这张工牌，再切换显示。',true);return}
 busy=true;controls();try{const result=await device.rpc('badge_select',{badge:editingBadge});showCatalog(result);message(`设备已切换显示工牌 ${editingBadge+1}。`)}catch(e){message('切换失败，请稍后重试。'+e.message,true)}finally{busy=false;controls()}
};
$('clearBadge').onclick=async()=>{
 if(!confirm(`清空工牌 ${editingBadge+1} 的资料、头像、品牌和二维码？其他工牌不受影响。`))return;
 busy=true;controls();try{await device.rpc('clear',{badge:editingBadge,baseRevision:loadedBadgeRevision});await new Promise(r=>setTimeout(r,350));await load(editingBadge)}catch(e){message('清空结果待确认，请重新读取。'+e.message,true)}finally{busy=false;controls()}
};
$('connect').onclick=async()=>{if(device&&!device.closed){await device.disconnect();device=null;networks=[];renderNetworks();$('scanStatus').textContent='连接工牌后可扫描附近网络。';controls();message('已断开连接。');return}if((dirty||xiaozhiBackendDirty||museDirty)&&!confirm('连接后将读取设备资料，并替换当前未保存的编辑。继续吗？'))return;busy=true;controls();try{device=viaHotspot?new BadgeHttp():new BadgeSerial();device.onClose=()=>{if(!busy){controls();message('USB 连接已断开。',true)}};await device.connect();await load()}catch(e){try{await device?.disconnect()}catch(_){}device=null;message(e.name==='NotFoundError'?'已取消连接。':e.message,true)}finally{busy=false;controls()}if(device&&!device.closed)scanWifi()};
$('reload').onclick=async()=>{if((dirty||wifiDirty||xiaozhiBackendDirty||museDirty)&&!confirm('重新读取会放弃尚未保存的修改，继续吗？'))return;busy=true;controls();try{await load(editingBadge)}catch(e){message(e.message,true)}finally{busy=false;controls()}};
$('save').onclick=async()=>{if(busy)return;let committed=false,begun=false;try{if(maxProfileVersion<4||maxBadgeLayout<2)throw Error('请先安装支持品牌栏编号的 2.4.4 固件');const p=validateExtended(fields());busy=true;controls();await fontsReady;render();const bytes=portraitPayload();$('progress').hidden=false;$('progress').value=0;message('正在写入工牌，请保持连接…');await beginProfileUpload(device,{badge:editingBadge,baseRevision:loadedBadgeRevision,profile:p,format:4});begun=true;for(let offset=0;offset<bytes.length;offset+=FORMAT.chunk){const chunk=bytes.subarray(offset,offset+FORMAT.chunk);await device.rpc('chunk',{offset,data:btoa(String.fromCharCode(...chunk))});$('progress').value=Math.round((offset+chunk.length)/bytes.length*100)}committed=true;await device.rpc('commit');await new Promise(r=>setTimeout(r,400));const info=await device.rpc('info',{badge:editingBadge});if(!info.custom||JSON.stringify(info.profile)!==JSON.stringify(p))throw Error('资料回读不一致');dirty=false;loadedBadgeRevision=info.badgeRevision??info.revision;showCatalog(info);message(`工牌 ${editingBadge+1} 已保存。${editingBadge===activeBadge?'屏幕已更新。':'点击「显示这张工牌」可切换屏幕。'}`)}catch(e){if(!committed){if(begun){try{await device?.rpc('abort')}catch(_){}}message('保存未完成，原资料仍保留。'+e.message,true)}else message('保存结果待确认，请重新读取。'+e.message,true)}finally{busy=false;$('progress').hidden=true;controls()}};
for(const id of ['ssid','wifiPassword','openWifi'])$(id).oninput=()=>{wifiDirty=true};
function wifiFormPayload(allowKeep=true){
 const ssid=$('ssid').value.trim(),password=$('wifiPassword').value,open=$('openWifi').checked;
 const isKnown=wifi.knownWifis?.some(k=>k.ssid===ssid);
 const keepPassword=allowKeep&&!open&&!password&&(isKnown||(wifi.passwordSet&&ssid===wifi.ssid));
 const enc=new TextEncoder();
 if(!ssid||enc.encode(ssid).length>32||enc.encode(password).length>63||(!open&&!keepPassword&&enc.encode(password).length<8))throw Error('网络名称最多 32 字节；Wi-Fi 密码为 8–63 字节。');
 return {ssid,password,open,keepPassword};
}
$('saveWifi').onclick=async()=>{let payload;try{payload=wifiFormPayload(true)}catch(e){message(e.message,true);return}busy=true;controls();try{await device.rpc('wifi_save',payload);wifiDirty=false;$('wifiPassword').value='';message('网络设置已保存，正在连接。可在下方查看连接结果。');await new Promise(r=>setTimeout(r,1000));showWifi((await device.rpc('wifi_info')).wifi,true)}catch(e){message('网络设置结果待确认，请重新读取。'+e.message,true)}finally{busy=false;controls()}};
$('hotspot').onclick=async()=>{busy=true;controls();try{await device.rpc('hotspot');message('已请求切换热点，请查看工牌网络设置页。');await new Promise(r=>setTimeout(r,1200));showWifi((await device.rpc('wifi_info')).wifi)}catch(e){message(e.message,true)}finally{busy=false;controls()}};
$('xiaozhiBackend').oninput=()=>{xiaozhiBackendDirty=true;controls()};
$('resetXiaozhiBackend').onclick=()=>{$('xiaozhiBackend').value='';xiaozhiBackendDirty=true;controls();message('已切换为恢复官方默认，点击「保存小智后端」后生效。')};
$('saveXiaozhiBackend').onclick=async()=>{let payload;try{payload=xiaozhiBackendPayload()}catch(e){message(e.message,true);return}busy=true;controls();try{await device.rpc('xiaozhi_backend_save',payload);xiaozhiBackendDirty=false;const result=await device.rpc('xiaozhi_backend');showXiaozhiBackend(result,true);message(payload.url?'小智后端已保存，下次连接小智时生效。':'已恢复小智官方默认后端。')}catch(e){message(e.message==='ESP_ERR_INVALID_STATE'?'小智正在运行，请退出小智后再保存后端。':'小智后端保存失败。'+e.message,true)}finally{busy=false;controls()}};
for(const id of ['museToken','museStyle','museProxyHost','museProxyPort'])$(id).oninput=()=>{museDirty=true;controls()};
$('clearMuseProxy').onclick=()=>{$('museProxyHost').value='';$('museProxyPort').value='';museDirty=true;controls();message('已清空代理，点击「保存 Muse 配置」后改为直连。')};
$('saveMuseConfig').onclick=async()=>{let payload;try{payload=museConfigPayload()}catch(e){message(e.message,true);return}busy=true;controls();try{const result=await device.rpc('muse_config_save',payload);museDirty=false;showMuseConfig(result.muse,true);message('Muse 配置已保存。请打开 Muse 小程序完成手机配对或重试连接。')}catch(e){message(e.message==='ESP_ERR_INVALID_STATE'?'Muse 正在运行，请先退出小程序再保存配置。':'Muse 配置保存失败。'+e.message,true)}finally{busy=false;controls()}};

function renderNetworks(){
 const list=$('networkList');list.replaceChildren();
 for(const network of networks){
  const button=document.createElement('button');button.type='button';button.className='network-row'+($('ssid').value===network.ssid?' selected':'');button.dataset.supported=String(network.supported);
  const name=document.createElement('span');name.className='network-name';name.textContent=network.ssid;
  const detail=document.createElement('span');detail.className='network-detail';const strength=network.rssi>=-55?'强':network.rssi>=-70?'良好':'较弱';
  detail.textContent=strength+' · '+network.rssi+' dBm · '+(network.open?'开放':network.security)+(network.supported?'':' · 暂不支持');
  button.append(name,detail);button.onclick=()=>{if($('ssid').value!==network.ssid)$('wifiPassword').value='';$('ssid').value=network.ssid;$('openWifi').checked=network.open;wifiDirty=true;$('wifiPassword').placeholder=network.ssid===wifi.ssid&&wifi.passwordSet?'已保存密码，留空保留':network.open?'此网络无需密码':'输入此网络的密码';renderNetworks();controls()};list.append(button);
 }
 controls();
}
async function scanWifi(){
 if(scanBusy||busy||!device||device.closed)return;
 const target=device;scanBusy=true;controls();$('scanStatus').textContent='正在扫描附近的 2.4 GHz 网络…';
 try{
  let result=await target.rpc('wifi_scan');
  const deadline=Date.now()+20000;
  while(result.scanning&&Date.now()<deadline){await new Promise(r=>setTimeout(r,500));if(device!==target||target.closed)return;result=await target.rpc('wifi_scan_results')}
  if(device!==target||target.closed)return;
  if(result.scanning)throw Error('扫描超时，请稍后刷新');
  if(result.scanError)throw Error(result.scanError==='ESP_ERR_WIFI_STATE'?'工牌正在连接网络，请稍后刷新':'扫描失败，请稍后刷新');
  networks=result.networks||[];renderNetworks();$('scanStatus').textContent=networks.length?'找到 '+networks.length+' 个网络，按信号强度排序。':'未发现可见网络。请靠近路由器并确认已开启 2.4 GHz，或手动输入网络名称。';
 }catch(e){if(device===target)$('scanStatus').textContent=e.message==='ESP_ERR_INVALID_ARG'?'请先安装支持 Wi-Fi 扫描的 1.3.2 固件。':e.message}
 finally{scanBusy=false;controls()}
}
$('scanWifi').onclick=scanWifi;

let polling=false;setInterval(async()=>{if(!device||device.closed||busy||polling)return;polling=true;try{const net=await device.rpc('wifi_info');showWifi(net.wifi);showYaoTime(net.yaoTime,net.yaoLocationStatus);if(badgeCount>1)showCatalog(await device.rpc('badges'))}catch(e){$('wifiStatus').textContent='暂时无法读取网络状态，请检查连接。'}finally{polling=false}},5000);
$('form').onsubmit=e=>e.preventDefault();qrPlaceholder();render();controls();fontsReady.then(()=>render()).catch(()=>message('字体加载失败，请重新打开配置页。',true));if(viaHotspot)$('connect').click();

function showYaoTime(t,status){
 if(!t||!status){$('yaoTimeStatus').textContent='升级到 2.6.29 后，联网自动获取位置，无需填写。';return;}
 const states={offline:'尚未联网，未校准真太阳时',pending:'已排队自动定位和校时',fetching:'正在自动获取位置并校准真太阳时',retrying:'本次定位失败，设备将在 15 秒后自动重试',unavailable:'未获取到位置，直接使用系统时间，不校准真太阳时'};
 $('yaoTimeStatus').textContent=(t.location_set?`网络定位（近似）：${t.region} · 经度 ${t.longitude_east}°`:states[status]||'无可用位置，直接使用系统时间')+'\n'+(t.clock_valid?`读取时北京时间：${t.beijing_utc8}（UTC+8）`:'设备尚未校时，不推断起卦时间')+(t.true_solar_time?`\n当地真太阳时（近似）：${t.true_solar_time}`:'');
}
