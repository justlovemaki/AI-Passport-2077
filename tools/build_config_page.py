from pathlib import Path
import gzip,base64
root=Path(__file__).resolve().parent.parent
folder=root/'configure'
scripts=[]
for name in ['profile-core.js','vendor/jsQR.js','vendor/qrcodegen.js','vendor/config-crypto.js','config-backup.js','app.js']:
    source=(folder/name).read_text(encoding='utf-8').replace('</script','<\\/script')
    if name=='vendor/jsQR.js': source='/*\n'+(folder/'vendor/jsQR.LICENSE').read_text(encoding='utf-8').replace('*/','* /')+'\n*/\n'+source
    if name=='vendor/config-crypto.js':
        notices='\n\n'.join((folder/'vendor'/license).read_text(encoding='utf-8').replace('*/','* /') for license in ['noble-ciphers.LICENSE','noble-hashes.LICENSE'])
        source='/*\n'+notices+'\n*/\n'+source
    scripts.append('<script>\n'+source+'\n</script>')
fonts=[]
for name,family,weight in [('Orbitron','BadgeDisplay',700),('Caveat','BadgeSignature',600),('NotoSansSC-brand','BadgeBrand',700)]:
    data=base64.b64encode((root/'assets/fonts'/f'{name}-latin.woff2').read_bytes()).decode('ascii')
    fonts.append(f"@font-face{{font-family:{family};font-weight:{weight};src:url(data:font/woff2;base64,{data}) format('woff2');font-display:block;}}")
page=(folder/'page.html').read_text(encoding='utf-8').replace('<!-- SCRIPTS -->','\n'.join(scripts)).replace('/* BADGE_FONTS */','\n'.join(fonts))
(folder/'index.html').write_text(page,encoding='utf-8')
(root/'main/assets/configure.html.gz').write_bytes(gzip.compress(page.encode('utf-8'),mtime=0))
print('Offline configuration page:',len(page.encode('utf-8')),'bytes')
