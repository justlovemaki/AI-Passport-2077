const assert=require('assert'),fs=require('fs'),vm=require('vm');
const elements=new Map();
class Element{constructor(){this._value='';this.placeholder='';this.textContent='';this.disabled=false}set value(v){this._value=String(v)}get value(){return this._value}}
function get(id){if(!elements.has(id))elements.set(id,new Element());return elements.get(id)}
const source=fs.readFileSync('configure/app.js','utf8');
const begin=source.indexOf('function showMuseConfig');
const end=source.indexOf('function xiaozhiBackendPayload');
assert(begin>=0&&end>begin);
const scope={$:get,muse:{},museDirty:false,controls:()=>{},console};
vm.createContext(scope);vm.runInContext(source.slice(begin,end),scope);
scope.showMuseConfig({tokenSet:true,paired:true,proxyHost:'192.168.1.8',proxyPort:7890},true);
assert.equal(get('museToken').value,'');
assert.match(get('museToken').placeholder,/已保存/);
assert.match(get('museStatus').textContent,/已配对/);
assert.equal(get('museStyle').value,'0');
assert.deepEqual({...scope.museConfigPayload()},{token:'',style:0,proxyHost:'192.168.1.8',proxyPort:7890});
get('museToken').value='mgst_123456789';get('museProxyHost').value='';get('museProxyPort').value='';
assert.deepEqual({...scope.museConfigPayload()},{token:'mgst_123456789',style:0,proxyHost:'',proxyPort:0});
get('museProxyHost').value='proxy.local';get('museProxyPort').value='7890';
assert.throws(()=>scope.museConfigPayload(),/IPv4/);
get('museProxyHost').value='192.168.50.10';get('museProxyPort').value='65536';
assert.throws(()=>scope.museConfigPayload(),/1–65535/);
console.log('Muse configuration UI: PASS (write-only token, proxy validation, pairing status)');
