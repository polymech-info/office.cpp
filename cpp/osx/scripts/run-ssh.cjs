/**
 * Run a shell command after cd to packages/media/cpp: either on this machine (no ssh) or
 * on a Mac over SSH (e.g. from Windows).
 *
 * Local mode (no ssh) when any of:
 *   PM_LOCAL=1 | PM_USE_SSH=0
 *   PM_OSX_SSH=local
 *   Node runs on process.platform "darwin" (Mac) and PM_OSX_FORCE_SSH is not set
 *
 * Environment (ssh mode):
 *   PM_OSX_SSH       (default: osx)  — destination for ssh(1)
 *   PM_OSX_CPP_ROOT  — path to packages/media/cpp on the *remote* Mac
 *
 * Environment (local mode on Mac):
 *   PM_OSX_CPP_ROOT  — optional; defaults to repo root (this script in osx/scripts -> ../..)
 *   On Darwin, prepends LIBRARY_PATH with Homebrew lib dirs so linking pm-image finds libidn2 (curl).
 *
 * Usage:
 *   node scripts/run-ssh.cjs -- cmake --version
 *   node scripts/run-ssh.cjs "cmake --preset release -G Ninja"
 */

const { spawnSync } = require('node:child_process');
const path = require('node:path');
const process = require('node:process');
const os = require('node:os');

function shSingleQuotePath(p) {
  // Safe for sh/bash: ' + path with every ' as '"'"' + '
  return `'` + p.replace(/'/g, `'"'"'`) + `'`;
}

const WIN_DEFAULT_ROOT =
  '/Volumes/VMware Shared Folders/zx/Desktop/polymech/polymech-mono/packages/media/cpp';

function resolveCppRoot() {
  if (process.env.PM_OSX_CPP_ROOT) {
    return String(process.env.PM_OSX_CPP_ROOT).trim();
  }
  if (os.platform() === 'darwin') {
    // osx/scripts -> ../.. = packages/media/cpp
    return path.resolve(__dirname, '..', '..');
  }
  return WIN_DEFAULT_ROOT;
}

function useSsh() {
  if (process.env.PM_LOCAL === '1' || (process.env.PM_LOCAL || '').toLowerCase() === 'true') {
    return false;
  }
  if (process.env.PM_USE_SSH === '0' || (process.env.PM_USE_SSH || '').toLowerCase() === 'false') {
    return false;
  }
  const dest = (process.env.PM_OSX_SSH || '').trim().toLowerCase();
  if (dest === 'local' || dest === '0' || dest === '-') {
    return false;
  }
  if (process.env.PM_OSX_FORCE_SSH === '1' || (process.env.PM_OSX_FORCE_SSH || '').toLowerCase() === 'true') {
    return true;
  }
  if (os.platform() === 'darwin') {
    return false;
  }
  return true;
}

function main() {
  const argv = process.argv.slice(2);
  if (argv.length === 0 || (argv[0] === '--help' || argv[0] === '-h')) {
    const mode = useSsh()
      ? 'ssh to PM_OSX_SSH (typical from Windows)'
      : 'local bash (Node on macOS, or PM_LOCAL=1 / PM_USE_SSH=0 / PM_OSX_SSH=local)';
    const msg = [
      'Run a command after cd to packages/media/cpp. Mode: ' + mode + '.',
      '',
      '  Local (no ssh): PM_LOCAL=1, or PM_USE_SSH=0, or PM_OSX_SSH=local, or run Node on macOS (darwin) without PM_OSX_FORCE_SSH=1.',
      '  Ssh:              PM_OSX_SSH=osx (default from Windows) and set PM_OSX_CPP_ROOT to the guest path if needed.',
      '  PM_OSX_CPP_ROOT   — C++ root; on Mac defaults to this packages/media/cpp in the clone.',
      '',
      'Usage:',
      '  node scripts/run-ssh.cjs -- <args passed to remote shell>…',
      '  node scripts/run-ssh.cjs "cmake --preset release && cmake --build --preset release"',
      '',
    ].join('\n');
    console.log(msg);
    process.exit(argv.length === 0 ? 1 : 0);
  }

  let userParts = argv;
  if (userParts[0] === '--') {
    userParts = userParts.slice(1);
  }
  if (userParts.length === 0) {
    console.error('run-ssh: missing command after optional --');
    process.exit(1);
  }

  const root = resolveCppRoot();
  const host = (process.env.PM_OSX_SSH || 'osx').trim() || 'osx';
  const remote = useSsh();

  const userCmd = userParts.map((a) => String(a)).join(' ');

  // Non-interactive ssh often has no Homebrew in PATH. Keep brew/cmake on PATH.
  // Feed a script to `ssh host /bin/bash -s` (stdin) so the remote is always bash, not the user's
  // default zsh (sshd is picky about `ssh host bash -c "…"` in some OpenSSH + macOS setups).
  const fullScript = [
    'export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/local/sbin:$PATH"',
    // AppleClang + static libcurl often passes `-lidn2` without `-L`; Homebrew libs live here (ARM + Intel).
    'if [ "$(uname -s)" = Darwin ]; then export LIBRARY_PATH="/opt/homebrew/lib:/usr/local/lib:${LIBRARY_PATH:-}"; fi',
    'set -e',
    'cd ' + shSingleQuotePath(root),
    userCmd,
    '',
  ].join('\n');

  if (remote) {
    const r = spawnSync('ssh', [host, '/bin/bash', '-s'], {
      input: fullScript,
      stdio: ['pipe', 'inherit', 'inherit'],
      env: process.env,
    });
    if (r.status === null) {
      if (r.error) {
        console.error('run-ssh: failed to start ssh — is OpenSSH in PATH?', r.error.message);
      } else {
        console.error('run-ssh: ssh exited with no status');
      }
      process.exit(1);
    }
    process.exit(r.status);
  }

  const bashShell = os.platform() === 'win32' ? 'bash' : '/bin/bash';
  const rLocal = spawnSync(bashShell, ['-s'], {
    input: fullScript,
    stdio: ['pipe', 'inherit', 'inherit'],
    env: process.env,
  });
  if (rLocal.status === null) {
    if (rLocal.error) {
      console.error('run-ssh: failed to start ' + bashShell + ':', rLocal.error.message);
    } else {
      console.error('run-ssh: bash exited with no status');
    }
    process.exit(1);
  }
  process.exit(rLocal.status);
}

main();
