import { execFileSync } from 'node:child_process';
import { readFile, readdir, mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
const cli = process.env.npm_execpath;
if (!cli) throw new Error('请通过 npm run notices 运行');
let listing;
try { listing = execFileSync(process.execPath, [cli, 'ls', '--omit=dev', '--all', '--parseable'], { encoding: 'utf8', maxBuffer: 10 * 1024 * 1024 }); }
catch (error) { if (!error.stdout) throw error; listing = error.stdout; }
const sections = ['DR Music — bundled production dependency licenses\nGenerated from the locked dependency tree. Original copyright notices follow.\n'];
for (const directory of [...new Set(listing.trim().split(/\r?\n/))].sort()) {
  if (!directory.includes('node_modules')) continue;
  const pkg = JSON.parse(await readFile(path.join(directory, 'package.json'), 'utf8'));
  const files = (await readdir(directory)).filter(name => /^(licen[sc]e|copying|notice|copyright)(\.|$|-)/i.test(name));
  sections.push(`\n${'='.repeat(78)}\n${pkg.name}@${pkg.version}\nLicense: ${JSON.stringify(pkg.license || 'See upstream license')}\nRepository: ${typeof pkg.repository === 'string' ? pkg.repository : pkg.repository?.url || pkg.homepage || ''}\n`);
  for (const file of files) {
    try { sections.push(`--- ${file} ---\n${await readFile(path.join(directory, file), 'utf8')}\n`); } catch { /* directory rather than license file */ }
  }
  if (!files.length) sections.push('No standalone license file in the installed package; see the package repository and SPDX license identifier above.\n');
}
await mkdir('licenses', { recursive: true });
await writeFile('licenses/DEPENDENCY_LICENSES.txt', sections.join('\n'));
console.log('Updated licenses/DEPENDENCY_LICENSES.txt');
