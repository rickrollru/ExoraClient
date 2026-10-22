import sys
exec(open(sys.argv[1]).read())
def q(s): return '"'+s.replace('\\','\\\\').replace('"','\\"').replace('\n','\\n')+'"'
out=['// Generated from the wiki texts: the pages of the editor wiki (xeditor_wiki.cpp). English, translated with XcUi::Tr.','#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_WIKI_PAGES_H','#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_WIKI_PAGES_H','','namespace XcWiki','{','']
types={'head':'BLOCK_HEAD','text':'BLOCK_TEXT','tip':'BLOCK_TIP','warn':'BLOCK_WARN','steps':'BLOCK_STEPS','role':'BLOCK_ROLE','grid':'BLOCK_GRID','code':'BLOCK_CODE'}
pairs=[]
for pid,en,ru,blocks in P:
    out.append('static const SBlock s_aBlocks_%s[] = {'%pid)
    pairs.append((en,ru))
    for t,ben,bru,extra in blocks:
        if t=='grid': assert len(extra)==25,(pid,extra,len(extra))
        out.append('\t{%s, %s, %s},'%(types[t],q(ben),q(extra)))
        if t!='code': pairs.append((ben,bru))
    out.append('};')
out.append('')
out.append('static const SPage s_aPages[] = {')
for pid,en,ru,blocks in P:
    out.append('\t{%s, %s, s_aBlocks_%s, (int)std::size(s_aBlocks_%s)},'%(q(pid),q(en),pid,pid))
out.append('};')
out+=['','} // namespace XcWiki','','#endif','']
open(sys.argv[2],'w').write('\n'.join(out))
with open(sys.argv[3],'w') as f:
    for en,ru in pairs:
        assert '|' not in en and '|' not in ru
        f.write(en.replace('\\','\\\\').replace('\n','\\n')+'|'+ru.replace('\\','\\\\').replace('\n','\\n')+'\n')
print(len(P),'pages',len(pairs),'strings')
