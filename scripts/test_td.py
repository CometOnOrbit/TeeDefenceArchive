#!/usr/bin/env python3
"""TeeDefenceArchive 集成测试工具

无需 MySQL、无需客户端，自动化验证服务器基本功能。
依赖: Python 3, 编译好的 ArchiveServer 二进制, 地图文件。

用法:
  scripts/test_td.py               # 运行全部测试
  scripts/test_td.py --list        # 列出测试
  scripts/test_td.py TestWave      # 只运行指定测试
"""

import subprocess
import sys
import os
import time
import re
import signal
import shutil
import json

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(TEST_DIR)
BUILD_DIR = os.path.join(PROJECT_ROOT, "build")
SERVER_BIN = os.path.join(BUILD_DIR, "ArchiveServer")
MAPS_SRC = os.path.join(PROJECT_ROOT, "build-mysql", "maps")
MAPS_DST = os.path.join(PROJECT_ROOT, "maps")
WORLDS_JSON = os.path.join(MAPS_DST, "worlds.json")
AUTOEXEC = os.path.join(PROJECT_ROOT, "autoexec.cfg")

# ─── Test Infrastructure ───────────────────────────────────────

class TestCase:
    def __init__(self, name, fn, desc=""):
        self.name = name
        self.fn = fn
        self.desc = desc

    def run(self):
        print(f"  [{self.name}] {self.desc}" if self.desc else f"  [{self.name}]")
        try:
            self.fn()
            print(f"    → PASS")
            return True
        except AssertionError as e:
            print(f"    → FAIL: {e}")
            return False
        except Exception as e:
            print(f"    → FAIL: {type(e).__name__}: {e}")
            return False


class ServerHarness:
    """管理服务器进程的生命周期。"""
    
    def __init__(self, maps_cfg=None):
        self.process = None
        self.log = []
        self.maps_cfg = maps_cfg or self._default_maps()
        self._saved_worlds_json = None
    
    @staticmethod
    def _default_maps():
        return {
            "worlds": [
                {"title": "Test Hub", "map": "TDef1", "mode": "hub", "travel_locked": False},
                {"title": "Test Defence", "map": "TDef-Shwar", "mode": "defence", "travel_locked": False},
            ]
        }
    
    def setup(self):
        """准备测试环境：maps/ + worlds.json + autoexec.cfg。"""
        os.makedirs(MAPS_DST, exist_ok=True)
        
        # 保存原 worlds.json
        if os.path.exists(WORLDS_JSON) and not os.path.islink(WORLDS_JSON):
            with open(WORLDS_JSON) as f:
                self._saved_worlds_json = f.read()
        
        # 写临时的 worlds.json
        with open(WORLDS_JSON, "w") as f:
            json.dump(self.maps_cfg, f, indent=2)
        
        # 写 autoexec.cfg（禁用 MySQL 以便在无 DB 的环境下运行）
        with open(AUTOEXEC, "w") as f:
            f.write("sv_mysql_enable 0\n")
        
        # 软链地图文件
        if os.path.isdir(MAPS_SRC):
            for entry in self.maps_cfg.get("worlds", []):
                map_name = entry.get("map", "")
                if not map_name:
                    continue
                src = os.path.join(MAPS_SRC, f"{map_name}.map")
                dst = os.path.join(MAPS_DST, f"{map_name}.map")
                if os.path.exists(src) and not os.path.exists(dst):
                    os.symlink(src, dst)
                elif not os.path.exists(src) and not os.path.exists(dst):
                    print(f"  [setup] WARNING: map not found: {src}")
    
    def cleanup(self):
        """清理测试环境。"""
        self.stop()
        # 恢复原 worlds.json
        if self._saved_worlds_json is not None:
            with open(WORLDS_JSON, "w") as f:
                f.write(self._saved_worlds_json)
        # 清理 symlinks
        if os.path.isdir(MAPS_DST):
            for f in os.listdir(MAPS_DST):
                fp = os.path.join(MAPS_DST, f)
                if os.path.islink(fp):
                    os.unlink(fp)
        # 清理 autoexec
        if os.path.exists(AUTOEXEC):
            os.unlink(AUTOEXEC)
    
    def start(self, timeout=5):
        """启动服务器，等待初始化完成。"""
        if not os.path.exists(SERVER_BIN):
            raise RuntimeError(f"Server binary not found: {SERVER_BIN}")
        
        self.process = subprocess.Popen(
            [SERVER_BIN],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            cwd=TEST_DIR,
            text=True,
            bufsize=1,
        )
        
        # 等待服务器初始化
        start_time = time.time()
        ready = False
        while time.time() - start_time < timeout:
            line = self.process.stdout.readline()
            if line:
                self.log.append(line.rstrip())
                print(f"  [server] {line.rstrip()}")
                if "starting..." in line:
                    ready = True
                    # 再多读一会确保不遗漏后续输出
                    break
            else:
                time.sleep(0.05)
        
        if not ready:
            self.stop()
            raise RuntimeError("Server failed to start within timeout")
        
        return True
    
    def stop(self):
        """停止服务器。"""
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
            self.process = None


# ─── Test Cases ─────────────────────────────────────────────────

def test_server_startup():
    """验证服务器能正常启动并加载 Hub + Defence 世界。"""
    harness = ServerHarness()
    try:
        harness.setup()
        harness.start()
        worlds = [l for l in harness.log if "multiworld" in l and "world" in l]
        assert len(worlds) >= 2, f"至少应加载 2 个世界: {worlds}"
        assert any("mode=hub" in l for l in worlds), f"Hub 没加载: {worlds}"
        assert any("mode=defence" in l for l in worlds), f"Defence 没加载: {worlds}"
    finally:
        harness.cleanup()


def test_world_count():
    """验证 worlds.json 中的世界正确计数。"""
    cfg = {
        "worlds": [
            {"title": "W1", "map": "TDef1", "mode": "hub", "travel_locked": False},
            {"title": "W2", "map": "TDef-Shwar", "mode": "defence", "travel_locked": False},
        ]
    }
    harness = ServerHarness(cfg)
    try:
        harness.setup()
        harness.start()
        worlds = [l for l in harness.log if "multiworld" in l and "world" in l]
        assert len(worlds) == 2, f"应加载 2 个世界: {worlds}"
    finally:
        harness.cleanup()


def test_defence_mode():
    """验证 Defence 世界成功实例化。"""
    cfg = {
        "worlds": [
            {"title": "TD", "map": "TDef-Shwar", "mode": "defence", "travel_locked": False},
        ]
    }
    harness = ServerHarness(cfg)
    try:
        harness.setup()
        harness.start()
        worlds = [l for l in harness.log if "multiworld" in l and "world" in l]
        assert any("mode=defence" in l for l in worlds), f"Defence 没加载: {worlds}"
    finally:
        harness.cleanup()


def test_missing_map_skip():
    """验证缺失地图被优雅跳过，服务器不崩溃。"""
    cfg = {
        "worlds": [
            {"title": "Real", "map": "TDef1", "mode": "hub", "travel_locked": False},
            {"title": "Fake", "map": "nonexistent", "mode": "defence", "travel_locked": False},
        ]
    }
    harness = ServerHarness(cfg)
    try:
        harness.setup()
        # 确保 fake 没有地图文件
        fake_map = os.path.join(MAPS_DST, "nonexistent.map")
        if os.path.exists(fake_map):
            os.unlink(fake_map)
        
        harness.start()
        
        # 必须看到跳过消息
        assert any("skipping" in l for l in harness.log), f"没看到 skip: {harness.log}"
        
        # 检查服务器确实还在运行
        time.sleep(0.3)
        rc = harness.process.poll()
        assert rc is None, f"服务器在地图加载失败后退出了! exit code={rc}"
    finally:
        harness.cleanup()


def test_autoexec_config():
    """验证 autoexec.cfg 被正确执行（内容不回显到日志，所以检查 "executing" 消息）。"""
    cfg = {
        "worlds": [
            {"title": "Test", "map": "TDef1", "mode": "hub", "travel_locked": False},
        ]
    }
    harness = ServerHarness(cfg)
    try:
        harness.setup()
        harness.start()
        # autoexec.cfg 会在日志中打印 "executing 'autoexec.cfg'"
        assert any("executing" in l and "autoexec" in l for l in harness.log), \
            f"autoexec.cfg 没有被执行: {harness.log}"
        # sv_mysql_enable 0 使服务器跳过 MySQL — 如果没生效会出现 FATAL
        assert not any("FATAL" in l and "mysql" in l.lower() for l in harness.log), \
            f"MySQL 没有被跳过，autoexec 可能没生效: {[l for l in harness.log if 'FATAL' in l or 'mysql' in l.lower()]}"
    finally:
        harness.cleanup()

# ─── Test Runner ────────────────────────────────────────────────

ALL_TESTS = [
    TestCase("ServerStartup", test_server_startup, "基本启动 + Hub/Defence 加载"),
    TestCase("WorldCount", test_world_count, "多世界计数"),
    TestCase("DefenceMode", test_defence_mode, "Defence 控制器"),
    TestCase("MissingMapSkip", test_missing_map_skip, "地图缺失优雅跳过"),
    TestCase("AutoexecCfg", test_autoexec_config, "autoexec.cfg 配置"),
]

def run_tests(filter_name=None):
    results = {"pass": 0, "fail": 0}
    for test in ALL_TESTS:
        if filter_name and filter_name.lower() not in test.name.lower():
            print(f"  [{test.name}] SKIP")
            continue
        if test.run():
            results["pass"] += 1
        else:
            results["fail"] += 1
    return results


def main():
    print("=" * 60)
    print("  TeeDefenceArchive 集成测试")
    print("=" * 60)
    print()
    
    if not os.path.exists(SERVER_BIN):
        print(f"❌ {SERVER_BIN} not found — run 'cd build && cmake --build .' first")
        sys.exit(1)
    
    if not os.path.isdir(MAPS_SRC):
        print(f"⚠️  {MAPS_SRC} not found, maps may be missing")
    
    filter_name = None
    if len(sys.argv) > 1:
        if sys.argv[1] == "--list":
            print("Tests:")
            for t in ALL_TESTS:
                print(f"  {t.name}: {t.desc}")
            return
        filter_name = sys.argv[1]
    
    results = run_tests(filter_name)
    
    print()
    print("-" * 40)
    passed = results["pass"]
    failed = results["fail"]
    print(f"  {passed + failed} tests:  ✓ {passed}  ✗ {failed}")
    print("-" * 40)
    
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
