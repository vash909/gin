"""Real-FS checks. DRIVER selects strict compiled driver, no production hooks."""
import os
import signal
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
SCRATCH = Path(os.environ.get('TMPDIR', str(BASE / '.build')))
SCRATCH.mkdir(parents=True, exist_ok=True)
DRIVER = os.environ['DRIVER']

class AccountFS(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='account-fs-', dir=SCRATCH)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'root'

    def run_ops(self, commands):
        p = subprocess.Popen([DRIVER, str(self.root)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, start_new_session=True)
        try:
            out, err = p.communicate(commands, timeout=12)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.communicate()
            self.fail('driver deadline exceeded; process group killed/reaped')
        self.assertEqual(p.returncode, 0, (out, err))
        return [tuple(map(int, row.split())) for row in out.splitlines()]

    def test_coexistence_flags_and_preservation(self):
        commands = 'ni Net x\n' + ''.join('wi Net ' + t + '\n' for t in ['#IN', '#OUT', 'in', 'out', '#A%B', '&Room'])
        self.assertTrue(all(r[0] == 0 for r in self.run_ops(commands)))
        net = self.root / '%4Eet'
        self.assertEqual(set(p.name for p in net.iterdir()), {'in', 'out', '#in', '#out', '@in', '@out', '#a%25b', '+26room'})
        paths = [net / 'in', net / 'out', net / '#in' / 'in', net / '#in' / 'out']
        before = [p.stat().st_ino for p in paths]
        rows = self.run_ops('no Net x\nwo Net #in\nni Net x\nwi Net #in\nnr Net x\nwr Net #IN\nwd Net #in\nno Net x\nwo Net #in\n')
        self.assertTrue(all(r[0] == 0 for r in rows))
        self.assertEqual(before, [p.stat().st_ino for p in paths])
        for p in [net / 'out', net / '#in' / 'out']:
            self.assertEqual(p.read_bytes(), b'prefix\nprefix\n')
        for i in [0, 1, 7, 8]:
            self.assertTrue(rows[i][2] & os.O_APPEND)
            self.assertEqual(rows[i][2] & os.O_ACCMODE, os.O_WRONLY)
        for i in [4, 5]:
            self.assertTrue(rows[i][2] & os.O_NONBLOCK)
            self.assertEqual(rows[i][2] & os.O_ACCMODE, os.O_RDONLY)
        for p in net.rglob('*'):
            self.assertEqual(stat.S_IMODE(p.stat().st_mode), 0o700 if p.is_dir() else 0o600)

    def test_invalid_names_do_not_mutate_children(self):
        self.assertEqual(self.run_ops('ni net x\n')[0][0], 0)
        before = sorted(str(p.relative_to(self.root)) for p in self.root.rglob('*'))
        rows = self.run_ops('ni ../escape x\nwi net ../escape\nwi net #..\nwi net #\nwi ../bad #ok\n')
        self.assertTrue(all(r[0] == -1 for r in rows))
        self.assertEqual(before, sorted(str(p.relative_to(self.root)) for p in self.root.rglob('*')))

    def test_unsafe_endpoints_fail_without_repair(self):
        for window in [False, True]:
            for endpoint in ['in', 'out']:
                for attack in ['symlink', 'hardlink', 'mode', 'type']:
                    with self.subTest(window=window, endpoint=endpoint, attack=attack):
                        self.root = Path(self.temp.name) / ('r-' + str(window) + endpoint + attack)
                        self.run_ops('ni net x\nwi net #in\n')
                        parent = self.root / 'net'
                        if window:
                            parent /= '#in'
                        p = parent / endpoint
                        if attack == 'mode':
                            p.chmod(0o644)
                        elif attack == 'hardlink':
                            os.link(p, parent / 'alias')
                        else:
                            p.unlink()
                            if attack == 'symlink':
                                p.symlink_to('missing')
                            elif endpoint == 'out':
                                os.mkfifo(p, 0o600)
                            else:
                                p.write_bytes(b'not fifo')
                                p.chmod(0o600)
                        before = p.lstat()
                        op = ('w' if window else 'n') + ('r' if endpoint == 'in' else 'o')
                        init = 'wi net #in\n' if window else 'ni net x\n'
                        rows = self.run_ops(op + ' net #in\n' + init)
                        self.assertTrue(all(r[0] == -1 for r in rows))
                        after = p.lstat()
                        self.assertEqual((before.st_ino, before.st_mode, before.st_nlink), (after.st_ino, after.st_mode, after.st_nlink))

    def test_unsafe_directories_fail_without_repair(self):
        for window in [False, True]:
            for attack in ['symlink', 'mode', 'type']:
                with self.subTest(window=window, attack=attack):
                    self.root = Path(self.temp.name) / ('dir-' + str(window) + attack)
                    self.run_ops('ni net x\nwi net #in\n')
                    p = self.root / 'net'
                    if window:
                        p /= '#in'
                    if attack == 'mode':
                        p.chmod(0o755)
                    else:
                        p.rename(p.with_name(p.name + '-safe'))
                        if attack == 'symlink':
                            p.symlink_to(p.name + '-safe', target_is_directory=True)
                        else:
                            p.write_bytes(b'not directory')
                            p.chmod(0o700)
                    before = p.lstat()
                    commands = 'wi net #in\nwr net #in\nwo net #in\nwd net #in\n'
                    if not window:
                        commands += 'ni net x\nnr net x\nno net x\n'
                    self.assertTrue(all(r[0] == -1 for r in self.run_ops(commands)))
                    after = p.lstat()
                    self.assertEqual((before.st_ino, before.st_mode), (after.st_ino, after.st_mode))

    def test_held_root_permission_changes_reject_window_init(self):
        def snapshot():
            result = {}
            for p in self.root.rglob('*'):
                st = p.lstat()
                result[str(p.relative_to(self.root))] = (
                    st.st_ino, st.st_mode, st.st_nlink,
                    p.read_bytes() if stat.S_ISREG(st.st_mode) else None)
            return result

        for api in ['u', 'l']:
            for mode in ['755', '770']:
                with self.subTest(api=api, mode=mode):
                    self.root = Path(self.temp.name) / (api + mode)
                    self.assertTrue(all(r[0] == 0 for r in self.run_ops(
                        'ni net x\nwi net #kept\nno net x\nwo net #kept\n')))
                    before = snapshot()
                    op = api + mode
                    rows = self.run_ops(
                        op + ' net #kept\n' + op + ' net #new\n' +
                        op + ' missing #new\n')
                    with self.subTest(invariant='return codes'):
                        self.assertEqual([r[0] for r in rows], [-1, -1, -1],
                                         'unsafe held Root must reject window init')
                    with self.subTest(invariant='no mutation'):
                        self.assertEqual(snapshot(), before,
                                         'unsafe held Root created children or changed logs')
                    self.assertEqual(stat.S_IMODE(self.root.stat().st_mode), 0o700)

    def test_network_direct_endpoints(self):
        self.assertEqual(self.run_ops('ni Net ignored\n')[0][0], 0)
        net = self.root / '%4Eet'
        self.assertTrue((net / 'in').is_fifo(), 'direct network/in FIFO absent (legacy #status bridge is not network IO)')
        self.assertTrue((net / 'out').is_file())
        self.assertFalse((net / 'status').exists())

if __name__ == '__main__':
    unittest.main()
