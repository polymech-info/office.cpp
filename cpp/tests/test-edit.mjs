/**
 * pm-office create/edit smoke. JSON commands exit 0 and print JSON.
 * view text/outline print plain text. Failures exit 1.
 */
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { crc32, inflateRawSync } from 'node:zlib';
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

function zipEntries(file) {
  const data = readFileSync(file);
  let eocd = -1;
  for (let i = data.length - 22; i >= 0; i -= 1) {
    if (data.readUInt32LE(i) === 0x06054b50) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) fail(`not a zip: ${file}`);
  const entries = new Map();
  let cursor = data.readUInt32LE(eocd + 16);
  const count = data.readUInt16LE(eocd + 10);
  for (let n = 0; n < count; n += 1) {
    if (data.readUInt32LE(cursor) !== 0x02014b50) break;
    const method = data.readUInt16LE(cursor + 10);
    const compressed = data.readUInt32LE(cursor + 20);
    const nameLength = data.readUInt16LE(cursor + 28);
    const extraLength = data.readUInt16LE(cursor + 30);
    const commentLength = data.readUInt16LE(cursor + 32);
    const local = data.readUInt32LE(cursor + 42);
    const name = data.subarray(cursor + 46, cursor + 46 + nameLength).toString('utf8');
    const localName = data.readUInt16LE(local + 26);
    const localExtra = data.readUInt16LE(local + 28);
    const start = local + 30 + localName + localExtra;
    const bytes = data.subarray(start, start + compressed);
    entries.set(name, method === 0 ? Buffer.from(bytes) : inflateRawSync(bytes));
    cursor += 46 + nameLength + extraLength + commentLength;
  }
  return entries;
}

function writeStoredZip(file, entries) {
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const [name, text] of entries) {
    const nameBytes = Buffer.from(name);
    const data = Buffer.from(text);
    const crc = crc32(data) >>> 0;
    const local = Buffer.alloc(30 + nameBytes.length);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(data.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBytes.length, 26);
    nameBytes.copy(local, 30);
    locals.push(local, data);
    const central = Buffer.alloc(46 + nameBytes.length);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(20, 4);
    central.writeUInt16LE(20, 6);
    central.writeUInt32LE(crc, 16);
    central.writeUInt32LE(data.length, 20);
    central.writeUInt32LE(data.length, 24);
    central.writeUInt16LE(nameBytes.length, 28);
    central.writeUInt32LE(offset, 42);
    nameBytes.copy(central, 46);
    centrals.push(central);
    offset += local.length + data.length;
  }
  const centralBytes = Buffer.concat(centrals);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(entries.length, 8);
  eocd.writeUInt16LE(entries.length, 10);
  eocd.writeUInt32LE(centralBytes.length, 12);
  eocd.writeUInt32LE(offset, 16);
  writeFileSync(file, Buffer.concat([...locals, centralBytes, eocd]));
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
  const docxMarkdown = expectJson(['office', 'view', docx, 'markdown']);
  if (!docxMarkdown.markdown.includes('# Hello') || !docxMarkdown.markdown.includes('Second')) {
    fail('docx markdown', { status: 0, stdout: JSON.stringify(docxMarkdown), stderr: '' });
  }
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
  const xlsxMarkdown = expectJson(['office', 'view', xlsx, 'markdown']);
  const alice = (xlsxMarkdown.cells || []).find((cell) => cell.ref === 'A1' && cell.value === 'Alice');
  if (!xlsxMarkdown.markdown.includes('Alice') || !alice) {
    fail('xlsx markdown cells', { status: 0, stdout: JSON.stringify(xlsxMarkdown), stderr: '' });
  }

  const pptx = resolve(dir, 'deck.pptx');
  expectJson(['office', 'create', pptx]);
  expectJson(['office', 'add', pptx, '/', '--type', 'slide', '--prop', 'title=Deck']);
  expectJson(['office', 'add', pptx, '/slide[1]', '--type', 'shape', '--prop', 'text=Body']);
  const slides = expectOk(['office', 'view', pptx, 'text']);
  if (!slides.stdout.includes('Deck') || !slides.stdout.includes('Body')) fail('pptx text', slides);
  expectJson(['office', 'validate', pptx]);
  expectJson(['office', 'set', pptx, '/slide[1]', '--prop', 'background=1B2838', '--prop', 'transition=fade']);
  expectJson(['office', 'set', pptx, '/slide[1]/shape[2]', '--prop', 'fill=2E86C1', '--prop', 'animation=fade-entrance-500']);
  const slideXml = zipEntries(pptx).get('ppt/slides/slide1.xml').toString('utf8');
  if (!slideXml.includes('1B2838') || !slideXml.includes('<p:fade') || !slideXml.includes('filter="fade"')) {
    fail('pptx background, transition, and animation were not written', { status: 0, stdout: slideXml, stderr: '' });
  }
  const dropped = run(['office', 'set', pptx, '/slide[1]/shape[2]', '--prop', 'glow=1']);
  if (dropped.status === 0) fail('unknown pptx property should fail', dropped);

  const png = resolve(dir, 'pixel.png');
  writeFileSync(png, Buffer.from(
    'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==',
    'base64'));
  expectJson(['office', 'add', pptx, '/slide[1]', '--type', 'picture', '--prop', `src=${png}`, '--prop', 'name=Shot']);
  const packed = zipEntries(pptx);
  const withPicture = packed.get('ppt/slides/slide1.xml').toString('utf8');
  if (!withPicture.includes('r:embed') || !withPicture.includes('Shot')) {
    fail('pptx picture was not placed', { status: 0, stdout: withPicture, stderr: '' });
  }
  if (!packed.has('ppt/media/image1.png')) fail('pptx media part missing');
  expectJson(['office', 'validate', pptx]);

  const odfNs = 'xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"';
  const odt = resolve(dir, 'note.odt');
  writeStoredZip(odt, [
    ['mimetype', 'application/vnd.oasis.opendocument.text'],
    ['content.xml', `<office:document-content ${odfNs}><office:body><office:text><text:h text:outline-level="1">Title</text:h><text:p>Hello ODT</text:p><text:list><text:list-item><text:p>Item</text:p></text:list-item></text:list></office:text></office:body></office:document-content>`],
  ]);
  const odtView = expectJson(['office', 'view', odt, 'markdown']);
  if (!odtView.markdown.includes('# Title') || !odtView.markdown.includes('Hello ODT') || !odtView.markdown.includes('- Item')) {
    fail('odt markdown', { status: 0, stdout: JSON.stringify(odtView), stderr: '' });
  }
  const odtGet = expectJson(['office', 'get', odt, '/']);
  if (!odtGet.markdown.includes('Hello ODT')) fail('odt get', { status: 0, stdout: JSON.stringify(odtGet), stderr: '' });
  const odtCreate = run(['office', 'create', resolve(dir, 'new.odt')]);
  if (odtCreate.status === 0) fail('odt create should be unimplemented', odtCreate);

  const ods = resolve(dir, 'book.ods');
  writeStoredZip(ods, [
    ['mimetype', 'application/vnd.oasis.opendocument.spreadsheet'],
    ['content.xml', `<office:document-content ${odfNs}><office:body><office:spreadsheet><table:table table:name="Data"><table:table-row><table:table-cell office:value-type="string"><text:p>Alice</text:p></table:table-cell><table:table-cell office:value-type="float" office:value="3"><text:p>3</text:p></table:table-cell></table:table-row></table:table></office:spreadsheet></office:body></office:document-content>`],
  ]);
  const odsView = expectJson(['office', 'view', ods, 'markdown']);
  const odsAlice = (odsView.cells || []).find((cell) => cell.sheet === 'Data' && cell.ref === 'A1' && cell.value === 'Alice');
  const odsNumber = (odsView.cells || []).find((cell) => cell.ref === 'B1' && cell.value === '3');
  if (!odsAlice || !odsNumber || !odsView.markdown.includes('Alice')) {
    fail('ods cells', { status: 0, stdout: JSON.stringify(odsView), stderr: '' });
  }

  const odp = resolve(dir, 'deck.odp');
  writeStoredZip(odp, [
    ['mimetype', 'application/vnd.oasis.opendocument.presentation'],
    ['content.xml', `<office:document-content ${odfNs}><office:body><office:presentation><draw:page draw:name="Intro"><draw:frame><draw:text-box><text:p>Slide hello</text:p></draw:text-box></draw:frame></draw:page></office:presentation></office:body></office:document-content>`],
  ]);
  const odpView = expectJson(['office', 'view', odp, 'markdown']);
  if (!odpView.markdown.includes('Intro') || !odpView.markdown.includes('Slide hello')) {
    fail('odp markdown', { status: 0, stdout: JSON.stringify(odpView), stderr: '' });
  }

  const exists = run(['office', 'create', docx]);
  if (exists.status === 0) fail('create should fail when the file exists', exists);
  console.log('pm-office edit tests passed');
} finally {
  rmSync(dir, { recursive: true, force: true });
}
