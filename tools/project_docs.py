"""Offline Public/All document validation and deterministic Wiki generation."""
from __future__ import annotations
import argparse
import html
from html.parser import HTMLParser
import json
import posixpath
from pathlib import Path
import re
import sys
import time
import unicodedata
from urllib.parse import unquote, urlsplit, urljoin
from repository_sync import Policy

SKILLS = ('simpilot-development','simpilot-ui-validation','simpilot-ci-release','simpilot-docs-sync')


def prose(text):
    lines=[]; fence=None
    for line in text.splitlines():
        mark=re.match(r'^\s{0,3}(`{3,}|~{3,})',line)
        if mark:
            if fence is None: fence=mark[1]
            elif mark[1][0]==fence[0] and len(mark[1])>=len(fence): fence=None
            lines.append(''); continue
        lines.append('' if fence else line)
    return re.sub(r'(`+)(.+?)\1','', '\n'.join(lines))


class HtmlRefs(HTMLParser):
    def __init__(self):
        super().__init__(); self.links=[]; self.ids=set(); self.skip=0; self.base=None
    def handle_starttag(self,tag,attrs):
        if tag in ('code','pre'): self.skip+=1
        if self.skip: return
        for key,value in attrs:
            if not value: continue
            if tag=='base' and key=='href': self.base=value
            elif key in ('href','src','poster'): self.links.append(value)
            elif key=='srcset' and not value.startswith('data:'):
                self.links.extend(item.strip().split()[0] for item in value.split(',') if item.strip())
            if key=='id' or tag=='a' and key=='name': self.ids.add(value)
    def handle_endtag(self,tag):
        if tag in ('code','pre') and self.skip: self.skip-=1


def link_targets(text):
    text=prose(text); targets=[]
    for match in re.finditer(r'!?\[[^\]\n]*\]\(',text):
        i=match.end(); start=i; depth=1
        if i<len(text) and text[i]=='<':
            end=text.find('>',i+1)
            if end>=0: targets.append(text[i+1:end])
            continue
        while i<len(text) and depth:
            if text[i]=='\\': i+=2; continue
            if text[i]=='(': depth+=1
            if text[i]==')': depth-=1
            if depth: i+=1
        if depth==0:
            targets.append(re.split(r'\s+["\']',text[start:i].strip(),maxsplit=1)[0])
    for match in re.finditer(r'^\s{0,3}\[[^\]]+\]:\s*(?:<([^>]+)>|(\S+))',text,re.M):
        targets.append(match[1] or match[2])
    definitions={m[1].strip().casefold() for m in re.finditer(r'^\s{0,3}\[([^\]]+)\]:',text,re.M)}
    for m in re.finditer(r'\[([^\]\n]+)\]\[([^\]\n]*)\]',text):
        if (m[2] or m[1]).strip().casefold() not in definitions:
            targets.append('missing-reference-definition:'+(m[2] or m[1]))
    parser=HtmlRefs(); parser.feed(text); targets+=parser.links
    if parser.base is not None:
        base=parser.base if parser.base.endswith('/') else posixpath.dirname(parser.base)
        targets=[target if urlsplit(target).scheme or target.startswith('/') else
                 urljoin(parser.base,target) if urlsplit(parser.base).scheme else
                 posixpath.normpath(posixpath.join(base,target)) for target in targets]+[parser.base]
    return targets


def anchors(text):
    result=set(); parser=HtmlRefs(); parser.feed(prose(text)); result.update(parser.ids)
    fenced=False
    for line in text.splitlines():
        if re.match(r'^\s{0,3}(`{3,}|~{3,})',line): fenced=not fenced; continue
        if fenced: continue
        match=re.match(r'^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$',line)
        if not match: continue
        heading=re.sub(r'!?\[([^]]+)\]\([^)]+\)',r'\1',match[1])
        heading=html.unescape(re.sub(r'<[^>]+>','',heading)).lower()
        slug=''.join(c for c in heading if c in ('-','_',' ') or unicodedata.category(c)[0] in ('L','N','M'))
        slug=slug.replace(' ','-'); final=slug; n=0
        while final in result: n+=1; final=f'{slug}-{n}'
        result.add(final)
    return result


def wiki_pages(root):
    source=Path(root)/'docs/user/wiki'; pages={}; names={}
    for language in ('zh-CN','en-US'):
        directory=source/language
        if not directory.is_dir(): raise ValueError(f'Missing Wiki language: {language}')
        for file in sorted(directory.glob('*.md')):
            key=file.name.casefold()
            if key in names: raise ValueError(f'Wiki page name collision: {file.name}')
            if key=='_sidebar.md': raise ValueError('Wiki language page conflicts with sidebar')
            names[key]=file
            pages[file.name]=re.sub(r'\.\./(?:zh-CN|en-US)/','',file.read_text(encoding='utf-8-sig')).rstrip()+'\n'
    if not (source/'_Sidebar.md').is_file(): raise ValueError('Missing Wiki sidebar')
    pages['_Sidebar.md']=(source/'_Sidebar.md').read_text(encoding='utf-8-sig').rstrip()+'\n'
    for name in ('Home.md','Home.en-US.md'):
        if name not in pages: raise ValueError(f'Missing Wiki home: {name}')
    return pages


class Checker:
    def __init__(self,root,scope='Public'):
        self.root=Path(root).resolve(); self.scope=scope; self.policy=Policy(self.root)
        self.errors=[]; self.cache={}; self.checked=0

    def error(self,path,message): self.errors.append(f'{path}: {message}')

    def resolve_link(self,source,target,public,virtual=None):
        if target.startswith('missing-reference-definition:'): return self.error(source,target)
        target=html.unescape(target); url=urlsplit(target)
        if url.scheme in ('http','https') or target.startswith('//'):
            m=re.match(r'^/(?:wonly211/Simpilot)/(?:blob|raw)/[^/]+/(.+)',url.path,re.I)
            if url.netloc.lower()=='github.com' and m:
                return self.resolve_link(source,'/'+unquote(m[1])+('#'+url.fragment if url.fragment else ''),public)
            return
        if url.scheme in ('mailto','tel','data'): return
        if url.scheme: return self.error(source,f'Unsupported or local URI: {target}')
        path=unquote(url.path); fragment=unquote(url.fragment)
        if '\\' in path: return self.error(source,f'Use repository-relative / paths: {target}')
        if virtual is not None:
            name=Path(path).name if path else source
            if '/' in path: return self.error(source,f'Unflattened Wiki link: {target}')
            if name not in virtual and name+'.md' in virtual: name+='.md'
            if name not in virtual: return self.error(source,f'Broken published Wiki link: {target}')
            if fragment and fragment not in anchors(virtual[name]): self.error(source,f'Missing Wiki anchor: {target}')
            return
        resolved=(self.root/path.lstrip('/') if path.startswith('/') else (self.root/source).parent/path).resolve() if path else self.root/source
        if not resolved.exists() and source.startswith('docs/user/wiki/') and path:
            candidates=[resolved.with_name(resolved.name+'.md')]
            if source=='docs/user/wiki/_Sidebar.md':
                candidates += [self.root/'docs/user/wiki'/lang/(path+'.md') for lang in ('zh-CN','en-US')]
            found=[p for p in candidates if p.is_file()]
            if len(found)==1: resolved=found[0]
        if not resolved.is_relative_to(self.root): return self.error(source,f'Link escapes repository: {target}')
        relative=resolved.relative_to(self.root).as_posix()
        if public:
            permitted=self.policy.accepts(relative)
            if resolved.is_dir(): permitted=any(p.relative_to(self.root).as_posix().startswith(relative+'/') for p in self.public_files)
            if not permitted: return self.error(source,f'Public link points to local-only or excluded path: {target}')
        if not resolved.exists(): return self.error(source,f'Broken link: {target}')
        if fragment and resolved.is_file() and resolved.suffix.lower() in ('.md','.html'):
            if resolved not in self.cache: self.cache[resolved]=anchors(resolved.read_text(encoding='utf-8-sig'))
            if fragment not in self.cache[resolved]: self.error(source,f'Missing anchor: {target}')

    def local_metadata(self):
        for name,limit in {'AGENTS.md':8192,'src/AGENTS.md':4096,'tests/AGENTS.md':4096,'docs/AGENTS.md':4096}.items():
            path=self.root/name
            if not path.is_file(): self.error(name,'Required local instruction missing')
            elif path.stat().st_size>limit: self.error(name,f'Instruction exceeds {limit} bytes')
        config=self.root/'.codex/config.toml'
        if not config.is_file(): self.error(str(config),'Missing local project configuration')
        else:
            lines=[s.strip() for s in config.read_text(encoding='utf-8').splitlines() if s.strip() and not s.lstrip().startswith('#')]
            if lines != ['[shell_environment_policy.set]','CMAKE_BUILD_PARALLEL_LEVEL = "4"','CTEST_PARALLEL_LEVEL = "1"']:
                self.error('.codex/config.toml','Only the two approved parallelism defaults are allowed')
        for name in SKILLS:
            skill=self.root/'.agents/skills'/name/'SKILL.md'
            if not skill.is_file(): self.error(name,'Required skill missing'); continue
            text=skill.read_text(encoding='utf-8')
            match=re.match(r'\A---\r?\n(.+?)\r?\n---(?:\r?\n|$)',text,re.S)
            if not match: self.error(name,'Missing YAML frontmatter'); continue
            fields={}
            for line in match[1].splitlines():
                if ':' not in line: self.error(name,'Invalid YAML field'); continue
                key,value=line.split(':',1); fields[key.strip()]=value.strip().strip('"\'')
            if fields.get('name')!=name or not fields.get('description'): self.error(name,'Invalid skill name/description')
            if '[TODO' in text: self.error(name,'Unfinished skill scaffold')
            ui=skill.parent/'agents/openai.yaml'
            if not ui.is_file(): self.error(name,'Missing UI metadata'); continue
            data=ui.read_text(encoding='utf-8')
            for field in ('display_name','short_description','default_prompt'):
                value=re.search(r'^  '+field+r': (".*")$',data,re.M)
                if not value: self.error(name,f'Missing quoted UI field: {field}'); continue
                try: decoded=json.loads(value[1])
                except ValueError: self.error(name,f'Invalid UI string: {field}'); continue
                if field=='default_prompt' and '$'+name not in decoded: self.error(name,'Prompt must name the skill')
                if field=='short_description' and not 25<=len(decoded)<=64: self.error(name,'UI description must be 25–64 characters')
            if re.search(r'allow_implicit_invocation:\s*false',data): self.error(name,'Automatic skill selection must remain enabled')
        inventory=self.root/'docs/documentation-map.json'
        if not inventory.is_file(): self.error(str(inventory),'Missing migration inventory'); return
        data=json.loads(inventory.read_text(encoding='utf-8')); rows=data.get('documents',[])
        if data.get('baselineCount')!=75 or len(rows)!=75 or len({r['source'] for r in rows})!=75:
            self.error(str(inventory),'Migration inventory must cover all 75 unique original documents')
        for row in rows:
            if not (self.root/row['destination']).is_file(): self.error(row['source'],'Migration destination missing')
            if row['action'] not in ('keep','move','merge') or not row.get('purpose'): self.error(row['source'],'Missing migration decision/purpose')
            if not re.fullmatch(r'[0-9a-f]{64}',row.get('originalSha256','')): self.error(row['source'],'Missing original digest')
            if self.policy.accepts(row['destination']) != (row['scope']=='Public'): self.error(row['source'],'Migration scope conflicts with policy')

    def run(self):
        self.public_files=self.policy.files()
        if (self.root/'.gitignore').read_text(encoding='utf-8')!=self.policy.ignore_text(): self.error('.gitignore','Generated policy differs')
        files={p for p in self.public_files if p.suffix.lower() in ('.md','.html')}
        cmake=(self.root/'CMakeLists.txt').read_text(encoding='utf-8')
        match=re.search(r'project\s*\(\s*Simpilot\s+VERSION\s+(\d+\.\d+\.\d+)',cmake)
        if not match: self.error('CMakeLists.txt','Cannot read source version')
        else:
            for name,prefix in [('docs/user/zh-CN/用户手册.md','适用版本：'),('docs/user/en-US/User-Manual.md','Applies to version: ')]:
                path=self.root/name
                declaration=re.findall(re.escape(prefix)+r'(\d+\.\d+\.\d+)',path.read_text(encoding='utf-8')) if path.exists() else []
                if declaration != [match[1]]: self.error(name,'Manual version differs from source')
        for file in self.public_files:
            if file.parent==self.root/'docs/release-notes':
                version=re.fullmatch(r'v(\d+\.\d+\.\d+)\.md',file.name)
                title=file.read_text(encoding='utf-8-sig').splitlines()[0]
                if not version or not re.match(r'^# .*\bv'+re.escape(version[1])+r'(?:\s|$)',title): self.error(file.name,'Release note version/title mismatch')
        if self.scope=='All':
            for directory in ('docs','.agents','src','tests'):
                files.update(p for p in (self.root/directory).rglob('*') if p.is_file() and p.suffix.lower() in ('.md','.html'))
            files.update(self.root.glob('AGENTS*.md')); self.local_metadata()
        for file in sorted(files):
            source=file.relative_to(self.root).as_posix(); text=file.read_text(encoding='utf-8-sig')
            for target in link_targets(text): self.resolve_link(source,target,self.policy.accepts(source))
            self.checked+=1
        try:
            pages=wiki_pages(self.root)
            for name,content in pages.items():
                for target in link_targets(content): self.resolve_link(name,target,True,pages)
        except ValueError as e: self.errors.append(str(e))
        if self.errors: raise ValueError('\n'.join(self.errors))
        return self.checked


def publish_wiki(root,target):
    root=Path(root).resolve(); target=Path(target).resolve(); source=root/'docs/user/wiki'
    if target==root or target in root.parents or target==source or source in target.parents or target in source.parents:
        raise ValueError('Wiki output must be separate from source')
    Checker(root).run()
    pages=wiki_pages(root); target.mkdir(parents=True,exist_ok=True)
    for existing in target.glob('*.md'): existing.unlink()
    for name,text in pages.items(): (target/name).write_text(text,encoding='utf-8',newline='\n')
    return len(pages)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',default=str(Path(__file__).resolve().parents[1])); p.add_argument('--scope',choices=['Public','All'],default='Public')
    p.add_argument('--publish-wiki'); p.add_argument('--report')
    args=p.parse_args(); started=time.perf_counter()
    count=publish_wiki(args.root,args.publish_wiki) if args.publish_wiki else Checker(args.root,args.scope).run()
    result={'scope':args.scope,'documents':count,'seconds':round(time.perf_counter()-started,3),'status':'success','offline':True}
    if args.report:
        path=Path(args.report); path.parent.mkdir(parents=True,exist_ok=True); path.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':
    try: main()
    except (ValueError,OSError,KeyError) as e: print(str(e),file=sys.stderr); sys.exit(1)
