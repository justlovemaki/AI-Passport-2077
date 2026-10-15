#!/usr/bin/env python3
"""Host tests for deterministic, synchronized release version updates."""
from __future__ import annotations
import contextlib
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location("pipeline",ROOT/"tools"/"pipeline.py")
assert SPEC and SPEC.loader
PIPE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(PIPE)
class PipelineVersionTest(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix="pipeline-version-");self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name);(self.root/"main").mkdir()
        self.header=self.root/"main"/"badge_profile.h";self.english=self.root/"README.md";self.chinese=self.root/"README.zh_CN.md"
        patterns=tuple(pattern for _,pattern in PIPE.VERSION_TARGETS)
        self.targets=((self.header,patterns[0]),(self.english,patterns[1]),(self.chinese,patterns[2]))
        patches=(patch.object(PIPE,"REPO_ROOT",self.root),patch.object(PIPE,"HEADER_FILE",self.header),patch.object(PIPE,"README_FILE",self.english),patch.object(PIPE,"README_ZH_FILE",self.chinese),patch.object(PIPE,"VERSION_TARGETS",self.targets))
        for item in patches:item.start();self.addCleanup(item.stop)
        self.write("2.7.3")
    def write(self,version,*,english=None,chinese=None):
        self.header.write_bytes(f'#pragma once\r\n#define BADGE_VERSION "{version}"\r\n'.encode())
        self.english.write_bytes(f'English | [简体中文](README.zh_CN.md)\r\n> Firmware Version: **v{english or version}**\r\n'.encode())
        self.chinese.write_bytes(f'[English](README.md)\r\n> 固件版本：**v{chinese or version}**\r\n'.encode())
    def test_bump_boundaries_and_rejects_non_semver(self):
        self.assertEqual(PIPE.bump_version_string("2.7.3","patch"),"2.7.4")
        self.assertEqual(PIPE.bump_version_string("2.7.3","minor"),"2.8.0")
        self.assertEqual(PIPE.bump_version_string("2.7.3","major"),"3.0.0")
        for version,part in (("2.7","patch"),("v2.7.3","patch"),("2.x.3","minor"),("2.7.3","build")):
            with self.subTest(version=version,part=part),self.assertRaises(ValueError):PIPE.bump_version_string(version,part)
    def test_update_synchronizes_all_targets_and_preserves_crlf(self):
        self.assertEqual(PIPE.get_current_version(),"2.7.3")
        with contextlib.redirect_stdout(io.StringIO()):PIPE.update_version("2.7.4")
        self.assertEqual(PIPE.get_current_version(),"2.7.4")
        for path,_ in self.targets:
            data=path.read_bytes();self.assertIn(b"2.7.4",data);self.assertIn(b"\r\n",data);self.assertNotIn(b"2.7.3",data)
            self.assertFalse(path.with_name(path.name+".version-tmp").exists())
    def test_release_metadata_runs_repository_check_after_alignment(self):
        with patch.object(PIPE.subprocess,"run",return_value=Mock(returncode=0)) as run:
            with contextlib.redirect_stdout(io.StringIO()):PIPE.verify_release_metadata("2.7.3")
        run.assert_called_once_with([PIPE.sys.executable,str(self.root/"tools"/"check_repo.py")])
        with patch.object(PIPE.subprocess,"run") as run, self.assertRaisesRegex(ValueError,"expected 2.7.4"):
            PIPE.verify_release_metadata("2.7.4")
        run.assert_not_called()

    def test_mismatch_fails_before_writing_any_target(self):
        self.write("2.7.3",english="2.7.2",chinese="2.7.3")
        before={path:path.read_bytes() for path,_ in self.targets}
        with self.assertRaisesRegex(ValueError,"Version targets disagree"):PIPE.update_version("2.7.4")
        self.assertEqual(before,{path:path.read_bytes() for path,_ in self.targets})
    def test_invalid_or_duplicate_markers_fail_closed(self):
        before={path:path.read_bytes() for path,_ in self.targets}
        for value in ("2.7","v2.7.4","2.7.x",""):
            with self.subTest(value=value),self.assertRaises(ValueError):PIPE.update_version(value)
            self.assertEqual(before,{path:path.read_bytes() for path,_ in self.targets})
        self.header.write_text('#define BADGE_VERSION "2.7.3"\n#define BADGE_VERSION "2.7.3"\n')
        duplicate=self.header.read_bytes()
        with self.assertRaisesRegex(ValueError,"exactly one"):PIPE.get_current_version()
        self.assertEqual(self.header.read_bytes(),duplicate)
if __name__=="__main__":unittest.main()
