/**
 * orchestrator/test-commons.js — shared helpers for IPC orchestrator tests.
 */

import { randomUUID } from 'node:crypto';
import { existsSync } from 'node:fs';
import net from 'node:net';
import { resolve } from 'node:path';

/** kbot-ai live call runs unless KBOT_IPC_LLM is explicitly disabled. */
export function ipcLlmEnabled() {
  const v = process.env.KBOT_IPC_LLM;
  if (v === undefined || v === '') return true;
  const s = String(v).trim().toLowerCase();
  if (s === '0' || s === 'false' || s === 'no' || s === 'off') return false;
  return true;
}

/** Llama local (:8888) IPC block — on by default; set KBOT_IPC_LLAMA=0 to skip (CI / no server). */
export function ipcLlamaEnabled() {
  const v = process.env.KBOT_IPC_LLAMA;
  if (v === undefined || v === '') return true;
  const s = String(v).trim().toLowerCase();
  if (s === '0' || s === 'false' || s === 'no' || s === 'off') return false;
  return true;
}

/**
 * Classifier batch test (`test-ipc-classifier.mjs`): local llama :8888 by default.
 * Set KBOT_IPC_CLASSIFIER_LLAMA=0 to use KBOT_ROUTER / KBOT_IPC_MODEL (e.g. OpenRouter) instead.
 */
export function ipcClassifierLlamaEnabled() {
  const v = process.env.KBOT_IPC_CLASSIFIER_LLAMA;
  if (v === undefined || v === '') return true;
  const s = String(v).trim().toLowerCase();
  if (s === '0' || s === 'false' || s === 'no' || s === 'off') return false;
  return true;
}

/** Return the first existing path, falling back to the first candidate for useful diagnostics. */
export function firstExisting(...paths) {
  for (const p of paths) {
    if (p && existsSync(p)) return p;
  }
  return paths[0];
}

/** Repository root for tests under tests/orchestrator. */
export function repoRootFromOrchestrator(orchestratorDir) {
  return resolve(orchestratorDir, '..', '..');
}

/** Candidate locations for the pm-image executable used by orchestrator tests. */
export function mediaExeCandidates(orchestratorDir) {
  const repoRoot = repoRootFromOrchestrator(orchestratorDir);
  const exeName = process.platform === 'win32' ? 'pm-image.exe' : 'pm-image';
  const cliName = process.platform === 'win32' ? 'pm-image-cli.exe' : 'pm-image-cli';
  const candidates = [];

  if (process.platform === 'darwin') {
    candidates.push(resolve(repoRoot, 'dist-osx', exeName));
  }
  candidates.push(resolve(repoRoot, 'dist', exeName));
  if (process.platform === 'win32') {
    candidates.push(
      resolve(repoRoot, 'dist', 'win-x64', cliName),
      resolve(repoRoot, 'dist', 'win-x64', exeName),
    );
  }

  return candidates;
}

/** Candidate locations for the mono-cpp executable used by office parser smoke tests. */
export function officeExeCandidates(orchestratorDir) {
  const repoRoot = repoRootFromOrchestrator(orchestratorDir);
  const exeName = process.platform === 'win32' ? 'mono-cpp.exe' : 'mono-cpp';
  const candidates = [];

  candidates.push(resolve(repoRoot, 'dist', exeName));
  if (process.platform === 'win32') {
    candidates.push(
      resolve(repoRoot, 'dist', 'win-x64', exeName),
    );
  }

  return candidates;
}

/** Auto-start scripts/run-7b.sh when :8888 is closed (default on). */
export function llamaAutostartEnabled() {
  const v = process.env.KBOT_IPC_LLAMA_AUTOSTART;
  if (v === undefined || v === '') return true;
  const s = String(v).trim().toLowerCase();
  if (s === '0' || s === 'false' || s === 'no' || s === 'off') return false;
  return true;
}

/** TCP connect probe — true if something accepts connections. */
export function probeTcpPort(host, port, timeoutMs = 2000) {
  return new Promise((resolve) => {
    const socket = net.connect({ port, host });
    const done = (ok) => {
      socket.removeAllListeners();
      try {
        socket.destroy();
      } catch {
        /* ignore */
      }
      resolve(ok);
    };
    const timer = setTimeout(() => done(false), timeoutMs);
    socket.once('connect', () => {
      clearTimeout(timer);
      done(true);
    });
    socket.once('error', () => {
      clearTimeout(timer);
      done(false);
    });
  });
}

/** Counters for a test run (create one per process / suite). */
export function createAssert() {
  let passed = 0;
  let failed = 0;

  function assert(condition, label) {
    if (condition) {
      console.log(`  ✅ ${label}`);
      passed++;
    } else {
      console.error(`  ❌ ${label}`);
      failed++;
    }
  }

  return {
    assert,
    get passed() {
      return passed;
    },
    get failed() {
      return failed;
    },
  };
}

/** Normalize IPC payload (object or JSON string). */
export function payloadObj(msg) {
  const p = msg?.payload;
  if (p == null) return null;
  if (typeof p === 'string') {
    try {
      return JSON.parse(p);
    } catch {
      return { raw: p };
    }
  }
  return p;
}

/** Print LLM job_result so it is easy to spot (stdout, not mixed with worker stderr). */
export function logKbotAiResponse(stepLabel, msg) {
  const p = payloadObj(msg);
  const text = p?.text != null ? String(p.text) : '';
  const err = p?.error != null ? String(p.error) : '';
  const maxRaw = process.env.KBOT_IPC_LLM_LOG_MAX;
  const max =
    maxRaw === undefined || maxRaw === ''
      ? Infinity
      : Number.parseInt(maxRaw, 10);

  console.log('');
  console.log(`  ┌── ${stepLabel} ──────────────────────────────────────────`);
  console.log(`  │ type: ${msg?.type ?? '?'}`);
  if (p && typeof p === 'object') {
    console.log(`  │ status: ${p.status ?? '?'}`);
    if (p.mode != null) console.log(`  │ mode: ${p.mode}`);
    if (p.router != null) console.log(`  │ router: ${p.router}`);
    if (p.model != null) console.log(`  │ model: ${p.model}`);
  }
  if (err) {
    const showErr =
      Number.isFinite(max) && err.length > max
        ? `${err.slice(0, max)}… [truncated, ${err.length} chars]`
        : err;
    console.log(`  │ error: ${showErr.replace(/\n/g, '\n  │        ')}`);
  }
  if (p?.llm != null && typeof p.llm === 'object') {
    const raw = JSON.stringify(p.llm);
    const cap = 4000;
    const shown = raw.length > cap ? `${raw.slice(0, cap)}… [+${raw.length - cap} chars]` : raw;
    console.log(`  │ llm (usage / provider JSON): ${shown}`);
  }
  if (text) {
    let body = text;
    let note = '';
    if (Number.isFinite(max) && text.length > max) {
      body = text.slice(0, max);
      note = `\n  │ … [truncated: ${text.length} chars total; set KBOT_IPC_LLM_LOG_MAX= to adjust]`;
    }
    console.log('  │ text:');
    for (const line of body.split('\n')) {
      console.log(`  │   ${line}`);
    }
    if (note) console.log(note);
  } else if (!err) {
    console.log('  │ (no text in payload)');
  }
  console.log('  └────────────────────────────────────────────────────────────');
  console.log('');
}

/**
 * Length-prefixed JSON framing used by the C++ UDS worker.
 * Call `attach()` once to wire `socket.on('data', ...)`.
 */
export function createIpcClient(socket) {
  const pending = new Map();
  let readyResolve;
  const readyPromise = new Promise((res) => {
    readyResolve = res;
  });

  let buffer = Buffer.alloc(0);

  function onData(chunk) {
    buffer = Buffer.concat([buffer, chunk]);
    while (buffer.length >= 4) {
      const len = buffer.readUInt32LE(0);
      if (buffer.length >= 4 + len) {
        const payload = buffer.toString('utf8', 4, 4 + len);
        buffer = buffer.subarray(4 + len);
        try {
          const msg = JSON.parse(payload);
          if (msg.type === 'ready') {
            readyResolve(msg);
          } else if (msg.id && pending.has(msg.id)) {
            const p = pending.get(msg.id);
            clearTimeout(p.timer);
            pending.delete(msg.id);
            p.resolve(msg);
          }
        } catch (e) {
          console.error('[orchestrator] frame parse error', e);
        }
      } else {
        break;
      }
    }
  }

  function request(msg, timeoutMs = 5000) {
    return new Promise((resolve, reject) => {
      const id = msg.id || randomUUID();
      msg.id = id;
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(new Error(`IPC request timed out`));
      }, timeoutMs);
      pending.set(id, { resolve, reject, timer });

      const str = JSON.stringify(msg);
      const lenBuf = Buffer.alloc(4);
      lenBuf.writeUInt32LE(Buffer.byteLength(str));
      socket.write(lenBuf);
      socket.write(str);
    });
  }

  return {
    pending,
    readyPromise,
    request,
    attach() {
      socket.on('data', onData);
    },
  };
}

/** Forward worker stderr lines to console (prefixed). */
export function pipeWorkerStderr(workerProc, label = '[worker:stderr]') {
  workerProc.stderr.on('data', (d) => {
    const txt = d.toString().trim();
    if (txt) console.error(`${label} ${txt}`);
  });
}
