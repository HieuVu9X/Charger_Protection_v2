import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const testDir = path.dirname(fileURLToPath(import.meta.url));
const coreDir = path.resolve(testDir, '../..');
const tempDir = await fs.mkdtemp(path.join(os.tmpdir(), 'leakage-idle-audit-'));
const mainHeader = await fs.readFile(path.join(testDir, 'main.h.in'), 'utf8')
    + '\n' + await fs.readFile(path.join(coreDir, 'Protection/tests/main.h.in'), 'utf8');
await fs.writeFile(path.join(tempDir, 'main.h'), mainHeader);
for (const name of ['measurement_host_test.c', 'leakage_idle_audit.c'])
    await fs.copyFile(path.join(testDir, name + '.in'), path.join(tempDir, name));
for (const name of ['flash_fake.h', 'flash_fake.c'])
    await fs.copyFile(path.join(coreDir, 'Calib/tests', name + '.in'), path.join(tempDir, name));
const exe = path.join(tempDir, process.platform === 'win32' ? 'audit.exe' : 'audit');
function run(command, args) {
    const result = spawnSync(command, args, { stdio: 'inherit', windowsHide: true });
    if (result.error) throw result.error;
    if (result.status !== 0) process.exit(result.status || 1);
}
run(process.argv[2] || 'gcc', ['-std=c11', '-Os', '-flto', '-Wall', '-Wextra', '-Werror',
    '-Wno-pointer-to-int-cast', '-I' + tempDir,
    ...['Measurement', 'Protection', 'Calib', 'Rte', 'Com'].map(name => '-I' + path.join(coreDir, name)),
    path.join(tempDir, 'leakage_idle_audit.c'), path.join(tempDir, 'flash_fake.c'),
    path.join(coreDir, 'Calib/Calib.c'), path.join(coreDir, 'Rte/Rte.c'),
    '-x', 'c', path.join(testDir, 'rte_measurement_consumer.c.in'), '-lm', '-o', exe]);
run(exe, []);
console.log('Audit artifacts: ' + tempDir);
