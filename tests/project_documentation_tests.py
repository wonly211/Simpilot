"""Offline regressions. Disposable repositories never contact a remote service."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from repository_sync import Policy, check_staged, check_commit, check_history, export_tree, git, install_hooks
from project_docs import Checker, anchors, link_targets, wiki_pages, publish_wiki, SKILLS


def write(root,path,text):
    file=root/path; file.parent.mkdir(parents=True,exist_ok=True); file.write_text(text,encoding='utf-8'); return file


class Fixture(unittest.TestCase):
    def setUp(self):
        temporary_root=ROOT/'build/documentation-tests'
        temporary_root.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(prefix='case-',dir=temporary_root)
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'repo'; self.root.mkdir()
        shutil.copytree(ROOT/'tools',self.root/'tools',ignore=shutil.ignore_patterns('__pycache__'))
        self.policy=Policy(self.root)
        write(self.root,'.gitignore',self.policy.ignore_text())
        write(self.root,'CMakeLists.txt','project(Simpilot VERSION 1.2.3)\n')
        write(self.root,'README.md','# 中文入口\n')
        write(self.root,'docs/user/zh-CN/用户手册.md','# 用户手册\n适用版本：1.2.3\n')
        write(self.root,'docs/user/en-US/User-Manual.md','# Manual\nApplies to version: 1.2.3\n')
        write(self.root,'docs/user/wiki/zh-CN/Home.md','# 中文\n[English](../en-US/Home.en-US)\n')
        write(self.root,'docs/user/wiki/en-US/Home.en-US.md','# English\n[中文](../zh-CN/Home)\n')
        write(self.root,'docs/user/wiki/_Sidebar.md','[中文](Home)\n[English](Home.en-US)\n')
        git(self.root,'init','-b','main')
        git(self.root,'config','user.name','Offline Test')
        git(self.root,'config','user.email','offline@example.invalid')
        git(self.root,'config','commit.gpgsign','false')
        git(self.root,'config','core.autocrlf','false')

    def commit(self):
        git(self.root,'add','.')
        git(self.root,'commit','-m','fixture')
        return git(self.root,'rev-parse','HEAD').decode().strip()

    def rejects(self,text):
        with self.assertRaisesRegex(ValueError,text): Checker(self.root).run()

    def test_clean_public_without_local_material(self):
        self.assertGreater(Checker(self.root).run(),0)
        self.assertFalse((self.root/'.agents').exists())

    def test_public_private_link(self):
        write(self.root,'docs/development/private.md','# local\n')
        write(self.root,'README.md','[private](docs/development/private.md)')
        self.rejects('local-only')

    def test_html_private_reference(self):
        write(self.root,'README.md','<img src=".agents/private.png">')
        self.rejects('local-only')

    def test_remote_self_link_private(self):
        write(self.root,'README.md','[private](https://github.com/wonly211/Simpilot/blob/main/AGENTS.md)')
        self.rejects('local-only')

    def test_parent_escape(self):
        write(self.root,'README.md','[escape](../elsewhere.md)')
        self.rejects('escapes')

    def test_manual_wrong_version(self):
        write(self.root,'docs/user/zh-CN/用户手册.md','适用版本：1.2.30')
        self.rejects('version differs')

    def test_manual_missing(self):
        (self.root/'docs/user/en-US/User-Manual.md').unlink()
        self.rejects('version differs')

    def test_release_note_version(self):
        write(self.root,'docs/release-notes/v1.2.3.md','# Simpilot v1.2.4\n')
        self.rejects('version/title')

    def test_missing_public_link(self):
        write(self.root,'README.md','[missing](docs/user/en-US/Missing.md)')
        self.rejects('Broken link')

    def test_missing_anchor(self):
        write(self.root,'README.md','[bad](docs/user/zh-CN/用户手册.md#missing)')
        self.rejects('Missing anchor')

    def test_chinese_and_duplicate_anchors(self):
        write(self.root,'docs/user/zh-CN/用户手册.md','# 用户手册\n适用版本：1.2.3\n## 备份、迁入\n## 备份、迁入\n')
        write(self.root,'README.md','[第二个](docs/user/zh-CN/用户手册.md#备份迁入-1)')
        Checker(self.root).run()

    def test_reference_style_links(self):
        write(self.root,'README.md','[bad][ref]\n\n[ref]: docs/user/missing.md')
        self.rejects('Broken link')

    def test_undefined_reference(self):
        write(self.root,'README.md','[bad][undefined]')
        self.rejects('missing-reference')

    def test_html_base(self):
        targets=link_targets('<base href="../../"><img src="evidence/native/example.png">')
        self.assertIn('../../evidence/native/example.png',targets)

    def test_code_examples_not_links(self):
        write(self.root,'README.md','```md\n[example](missing)\n```\n`[example](missing)`\n')
        Checker(self.root).run()

    def test_wiki_name_collision(self):
        write(self.root,'docs/user/wiki/en-US/Home.md','# collision')
        self.rejects('collision')

    def test_wiki_case_collision(self):
        write(self.root,'docs/user/wiki/en-US/home.md','# collision')
        self.rejects('collision')

    def test_wiki_broken_flat_link(self):
        write(self.root,'docs/user/wiki/zh-CN/Home.md','[bad](Does-Not-Exist)')
        self.rejects('Wiki link')

    def test_wiki_publication(self):
        target=Path(self.temp.name)/'wiki'
        self.assertEqual(publish_wiki(self.root,target),3)
        self.assertIn('(Home.en-US)',(target/'Home.md').read_text(encoding='utf-8'))
        self.assertEqual(set(p.name for p in target.iterdir()),set(wiki_pages(self.root)))

    def test_wiki_source_not_deleted(self):
        before=(self.root/'docs/user/wiki/zh-CN/Home.md').read_bytes()
        with self.assertRaisesRegex(ValueError,'separate'): publish_wiki(self.root,self.root/'docs/user/wiki/zh-CN')
        self.assertEqual(before,(self.root/'docs/user/wiki/zh-CN/Home.md').read_bytes())

    def test_generated_ignore_drift(self):
        write(self.root,'.gitignore','*\n')
        self.rejects('Generated policy')

    def test_whitelist_exact_vendor_and_icons(self):
        for name in ('third_party/Everything/Everything.exe','assets/simpilot.ico','src/modules/foo/Tests/test.cpp'):
            self.assertTrue(self.policy.accepts(name),name)
        for name in ('third_party/Everything/Other.exe','assets/new-logo.png','docs/release-notes/v1.0.11-validation.md',
                     'src/AGENTS.md','src/x/AGENTS.override.md','src/x/.agents/test.cpp','docs/user/tmp.log','src/build/temp.cpp',
                     '../src/a.cpp','src//a.cpp','src/a.cpp.','/src/a.cpp'):
            self.assertFalse(self.policy.accepts(name),name)

    def test_staged_forbidden_materials(self):
        self.commit()
        for path in ('AGENTS.md','.codex/config.toml','.agents/skills/example/SKILL.md','docs/archive/design/a.html','tmp.txt','src/nested/AGENTS.md'):
            with self.subTest(path=path):
                write(self.root,path,'local')
                git(self.root,'add','-f',path)
                with self.assertRaisesRegex(ValueError,'Forbidden path'): check_staged(self.policy)
                git(self.root,'reset','--',path)

    def test_staged_allowed(self):
        git(self.root,'add','.')
        check_staged(self.policy)

    def test_staged_policy_and_ignore_match(self):
        self.commit()
        write(self.root,'.gitignore','*\n')
        git(self.root,'add','-f','.gitignore')
        with self.assertRaisesRegex(ValueError,'not generated'): check_staged(self.policy)

    def test_outgoing_history_added_then_removed(self):
        base=self.commit()
        write(self.root,'AGENTS.md','local')
        git(self.root,'add','-f','AGENTS.md'); git(self.root,'commit','-m','bad')
        git(self.root,'rm','AGENTS.md'); git(self.root,'commit','-m','removed')
        check_commit(self.policy,'HEAD')
        with self.assertRaisesRegex(ValueError,'Forbidden path'): check_history(self.policy,'HEAD',base)
        with self.assertRaisesRegex(ValueError,'Forbidden path'): check_history(self.policy,'HEAD')

    def test_missing_baseline_fails_closed(self):
        self.commit()
        with self.assertRaises(ValueError): check_history(self.policy,'HEAD','f'*40)

    def test_git_symlink_mode_rejected(self):
        self.commit()
        blob=git(self.root,'hash-object','-w','--stdin',input=b'../../private').decode().strip()
        git(self.root,'update-index','--add','--cacheinfo',f'120000,{blob},src/escape.cpp')
        with self.assertRaisesRegex(ValueError,'Unsupported Git mode'): check_staged(self.policy)

    def test_case_collision_rejected(self):
        with self.assertRaisesRegex(ValueError,'collision'):
            self.policy.check_entries([('100644','src/A.cpp'),('100644','src/a.cpp')],'fixture')

    def test_export_omits_local_and_history(self):
        self.commit(); write(self.root,'AGENTS.md','private'); write(self.root,'docs/archive/a.md','private')
        target=Path(self.temp.name)/'export'
        export_tree(self.policy,target)
        self.assertFalse((target/'AGENTS.md').exists()); self.assertFalse((target/'.git').exists())
        Checker(target).run()
        self.assertEqual((target/'README.md').read_bytes(),(self.root/'README.md').read_bytes())

    def test_export_refuses_nonempty_target(self):
        target=Path(self.temp.name)/'export'; write(target,'keep.txt','preserve')
        with self.assertRaisesRegex(ValueError,'empty'): export_tree(self.policy,target)
        self.assertEqual((target/'keep.txt').read_text(),'preserve')

    def test_actual_git_ignore_matches_policy(self):
        for name in ('src/nested/main.cpp','AGENTS.md','src/.agents/private.cpp','docs/user/zh-CN/extra.md','assets/new.png'):
            write(self.root,name,'fixture')
            p=subprocess.run(['git','-c',f'safe.directory={self.root.as_posix()}','-C',str(self.root),'check-ignore',name],capture_output=True)
            self.assertIn(p.returncode,(0,1)); self.assertEqual(p.returncode==1,self.policy.accepts(name),name)

    def test_hook_protects_existing_custom_hook(self):
        path=write(self.root,'.git/hooks/pre-commit','#!/bin/sh\nexit 0\n')
        with self.assertRaisesRegex(ValueError,'custom hook'): install_hooks(self.policy)
        self.assertEqual(path.read_text(),'#!/bin/sh\nexit 0\n')

    def test_real_precommit_hook(self):
        self.commit(); install_hooks(self.policy)
        write(self.root,'AGENTS.md','local'); git(self.root,'add','-f','AGENTS.md')
        with self.assertRaises(ValueError): git(self.root,'commit','-m','must fail')

    def test_real_prepush_hook_history(self):
        base=self.commit(); install_hooks(self.policy)
        write(self.root,'AGENTS.md','local'); git(self.root,'add','-f','AGENTS.md')
        git(self.root,'-c','core.hooksPath=/dev/null','commit','-m','fixture bypass')
        git(self.root,'rm','AGENTS.md'); git(self.root,'commit','-m','remove')
        head=git(self.root,'rev-parse','HEAD').decode().strip()
        p=subprocess.run(['pwsh','-NoProfile','-File',str(self.root/'tools/repository-sync.ps1'),'-Mode','Hook','-Root',str(self.root)],
                         input=f'refs/heads/main {head} refs/heads/main {base}\n'.encode(),capture_output=True)
        self.assertNotEqual(p.returncode,0)
        self.assertIn(b'Forbidden path',p.stderr)

    def test_local_metadata_does_not_leak_into_public(self):
        write(self.root,'.agents/skills/broken/SKILL.md','broken')
        Checker(self.root).run()
        checker=Checker(self.root,'All'); checker.local_metadata()
        self.assertTrue(any('Required skill missing' in e for e in checker.errors))

    def test_invalid_skill_frontmatter_and_config(self):
        for name in SKILLS: write(self.root,f'.agents/skills/{name}/SKILL.md','no frontmatter')
        write(self.root,'.codex/config.toml','model = "not-project-owned"')
        checker=Checker(self.root,'All'); checker.local_metadata()
        self.assertTrue(any('frontmatter' in e for e in checker.errors))
        self.assertTrue(any('parallelism' in e for e in checker.errors))


if __name__=='__main__': unittest.main(verbosity=2)
