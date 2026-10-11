const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path'),{webcrypto}=require('crypto');
const root=path.join(__dirname,'..');
const context={crypto:webcrypto,TextEncoder,TextDecoder,Uint8Array,Uint16Array,Uint32Array,DataView,ArrayBuffer,console,setTimeout,clearTimeout,btoa:s=>Buffer.from(s,'binary').toString('base64'),atob:s=>Buffer.from(s,'base64').toString('binary')};
context.globalThis=context;vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(root,'configure/vendor/config-crypto.js'),'utf8'),context);
vm.runInContext(fs.readFileSync(path.join(root,'configure/config-backup.js'),'utf8')+'\nthis.ConfigBackup=ConfigBackup;',context);
const C=context.ConfigBackup,bytes=(size,seed)=>Uint8Array.from({length:size},(_,i)=>(i*17+seed)&255);
const profile=(slot,format)=>{const assets={};for(const [name,size] of Object.entries(C.assetSizes(format)))assets[name]=C.record(bytes(size,slot+size));return{slot,configured:true,format,profile:{name:'Synthetic '+slot,department:'Test',title:'Fixture',employeeId:'ID-'+slot,signature:'',alias:'',brandName:'TEST',badgeCaption:'FIXTURE',theme:{background:'#08090b',panel:'#181216',accent:'#f03543',text:'#e8e6df',muted:'#a69c9f'},logoCustom:false,qrPresent:false},assets}};
const payload={kind:C.KIND,version:C.VERSION,device:'02:00:00:00:00:01',createdAt:'2026-01-01T00:00:00.000Z',activeBadge:0,nvs:C.record(bytes(24576,3)),profiles:[profile(0,4),{slot:1,configured:false,format:0,profile:null,assets:{}},{slot:2,configured:false,format:0,profile:null,assets:{}},{slot:3,configured:false,format:0,profile:null,assets:{}},{slot:4,configured:false,format:0,profile:null,assets:{}}]};
(async()=>{
 const password='synthetic-browser-password';
 const archive=await C.encrypt(payload,password);assert(archive.length<2000000);assert(!Buffer.from(archive).includes(Buffer.from(payload.device)));assert(!Buffer.from(archive).includes(Buffer.from(password)));
 const restored=await C.decrypt(archive,password),parsed=C.validatePayload(restored);assert.deepEqual(Array.from(parsed.nvs),Array.from(bytes(24576,3)));assert(parsed.profiles[0].assets.card.length===43264);
 const fallback={crypto:{getRandomValues:webcrypto.getRandomValues.bind(webcrypto)},TextEncoder,TextDecoder,Uint8Array,Uint16Array,Uint32Array,DataView,ArrayBuffer,console,setTimeout,clearTimeout,btoa:context.btoa,atob:context.atob};fallback.globalThis=fallback;vm.createContext(fallback);vm.runInContext(fs.readFileSync(path.join(root,'configure/vendor/config-crypto.js'),'utf8'),fallback);vm.runInContext(fs.readFileSync(path.join(root,'configure/config-backup.js'),'utf8')+'\nthis.ConfigBackup=ConfigBackup;',fallback);assert.equal((await fallback.ConfigBackup.decrypt(archive,password)).device,payload.device);
 await assert.rejects(()=>C.decrypt(archive,'wrong-synthetic-password'),/密码错误|损坏/);
 const damaged=archive.slice();damaged[damaged.length-2]^=1;await assert.rejects(()=>C.decrypt(damaged,password));
 for(const value of [0,true,1000000000]){const envelope=JSON.parse(new TextDecoder().decode(archive));envelope.iterations=value;await assert.rejects(()=>C.decrypt(new TextEncoder().encode(JSON.stringify(envelope)),password));}
 const malformed=structuredClone(payload);malformed.profiles[0].assets.card.size=1;assert.throws(()=>C.validatePayload(malformed));const metadata=structuredClone(payload);metadata.profiles[0].profile.unexpected='x';assert.throws(()=>C.validatePayload(metadata));const control=structuredClone(payload);control.profiles[0].profile.name='bad\u0000name';assert.throws(()=>C.validatePayload(control));
 const empty={...payload,activeBadge:0,profiles:Array.from({length:5},(_,slot)=>({slot,configured:false,format:0,profile:null,assets:{}}))};assert(C.validatePayload(empty));
 const generated=fs.readFileSync(path.join(root,'configure/index.html'),'utf8');assert(generated.includes('id="exportConfig"')&&generated.includes('id="importConfig"'));assert(generated.indexOf('globalThis.BadgeCrypto')<generated.indexOf('const ConfigBackup=')&&generated.indexOf('const ConfigBackup=')<generated.indexOf("$('exportConfig').onclick"));
 console.log('Web configuration backup: PASS (WebCrypto/hotspot fallback, full profile assets, strict schema, tamper/wrong-password rejection, embedded controls)');
})().catch(error=>{console.error(error);process.exitCode=1});
