/**
 * pm-office create/edit smoke. JSON commands exit 0 and print JSON.
 * view text/outline print plain text. Failures exit 1.
 */
import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');
const exe = process.env.PM_OFFICE_CLI
  || resolve(root, 'dist', process.platform === 'win32' ? 'pm-office.exe' : 'pm-office');

function run(args) {
  const result = spawnSync(exe, args, { encoding: 'utf8' });
  return {
    status: result.status,
    stdout: result.stdout || '',
    stderr: result.stderr || '',
    error: result.error,
  };
}

function fail(message, result) {
  const detail = result
    ? `\nstatus ${result.status}\nstdout: ${result.stdout}\nstderr: ${result.stderr}`
    : '';
  throw new Error(message + detail);
}

function expectOk(args) {
  const result = run(args);
  if (result.error) fail(result.error.message, result);
  if (result.status !== 0) fail(args.join(' '), result);
  return result;
}

function expectJson(args) {
  const result = expectOk(args);
  let json;
  try {
    json = JSON.parse(result.stdout);
  } catch (err) {
    fail(`not JSON: ${err.message}`, result);
  }
  if (json.ok === false) fail('ok:false', result);
  return json;
}

const helpOnly = process.argv.includes('--help-only');
const help = expectOk(['office', 'help']);
if (!/docx/.test(help.stdout) || !/xlsx/.test(help.stdout) || !/pptx/.test(help.stdout)) {
  fail('help is missing a format', help);
}
if (helpOnly) {
  process.stdout.write(help.stdout);
  process.exit(0);
}

expectOk(['office', 'parse-path', 'Sheet1!A1', '--format', 'xlsx']);

const dir = mkdtempSync(resolve(tmpdir(), 'pm-office-'));
try {
  const docx = resolve(dir, 'note.docx');
  expectJson(['office', 'create', docx]);
  const first = expectJson(['office', 'add', docx, '/', '--type', 'paragraph', '--prop', 'text=Hello', '--prop', 'style=Heading1']);
  const second = expectJson(['office', 'add', docx, '/', '--type', 'paragraph', '--prop', 'text=Second']);
  const viewed = expectOk(['office', 'view', docx, 'text']);
  if (!viewed.stdout.includes('Hello') || !viewed.stdout.includes('Second')) fail('view text', viewed);
  const outline = expectOk(['office', 'view', docx, 'outline']);
  if (!outline.stdout.includes('Heading1')) fail('outline style', outline);
  expectJson(['office', 'set', docx, first.path, '--prop', 'bold=true']);
  const got = expectOk(['office', 'get', docx, first.path]);
  if (!got.stdout.includes('Hello')) fail('get paragraph', got);
  expectJson(['office', 'set', docx, '/', '--find', 'Hello', '--replace', 'Hi']);
  const replaced = expectOk(['office', 'view', docx, 'text']);
  if (!replaced.stdout.includes('Hi') || replaced.stdout.includes('Hello')) fail('find/replace', replaced);
  expectJson(['office', 'swap', docx, first.path, second.path]);
  const swapped = expectOk(['office', 'view', docx, 'text']);
  if (swapped.stdout.indexOf('Second') > swapped.stdout.indexOf('Hi')) fail('swap order', swapped);
  expectJson(['office', 'remove', docx, second.path]);
  expectJson(['office', 'validate', docx]);
  const chart = run(['office', 'add', docx, '/', '--type', 'chart']);
  if (chart.status === 0) fail('chart should be unsupported', chart);

  const batchFile = resolve(dir, 'batch.docx');
  expectJson(['office', 'create', batchFile]);
  const batch = expectJson(['office', 'batch', batchFile, '--commands',
    JSON.stringify([{ command: 'add', parent: '/', type: 'paragraph', props: { text: 'Batch' } }])]);
  if (!Array.isArray(batch.results) || !batch.results[0]?.ok) fail('batch result', { status: 0, stdout: JSON.stringify(batch), stderr: '' });
  const batched = expectOk(['office', 'view', batchFile, 'text']);
  if (!batched.stdout.includes('Batch')) fail('batch text', batched);

  const xlsx = resolve(dir, 'book.xlsx');
  expectJson(['office', 'create', xlsx]);
  expectJson(['office', 'set', xlsx, 'Sheet1!A1', '--prop', 'value=Alice']);
  expectJson(['office', 'set', xlsx, 'Sheet1!A2', '--prop', 'value=10']);
  const cell = expectOk(['office', 'get', xlsx, 'Sheet1!A1']);
  if (!cell.stdout.includes('Alice')) fail('xlsx get', cell);
  const queried = expectOk(['office', 'query', xlsx, 'sheet[name=Sheet1] > row[1] > cell']);
  if (!queried.stdout.includes('Alice')) fail('xlsx query', queried);
  expectJson(['office', 'add', xlsx, '/', '--type', 'sheet', '--prop', 'name=Data']);
  const sheets = expectOk(['office', 'view', xlsx, 'outline']);
  if (!sheets.stdout.includes('Data')) fail('xlsx sheet', sheets);

  const pptx = resolve(dir, 'deck.pptx');
  expectJson(['office', 'create', pptx]);
  expectJson(['office', 'add', pptx, '/', '--type', 'slide', '--prop', 'title=Deck']);
  expectJson(['office', 'add', pptx, '/slide[1]', '--type', 'shape', '--prop', 'text=Body']);
  const slides = expectOk(['office', 'view', pptx, 'text']);
  if (!slides.stdout.includes('Deck') || !slides.stdout.includes('Body')) fail('pptx text', slides);
  expectJson(['office', 'validate', pptx]);

  const exists = run(['office', 'create', docx]);
  if (exists.status === 0) fail('create should fail when the file exists', exists);
  console.log('pm-office edit tests passed');
} finally {
  rmSync(dir, { recursive: true, force: true });
}
