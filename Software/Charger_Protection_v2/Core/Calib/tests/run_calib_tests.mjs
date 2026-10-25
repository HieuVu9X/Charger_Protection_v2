import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const testDir = path.dirname(fileURLToPath(import.meta.url));
const coreDir = path.resolve(testDir, '../..');
const tempDir = await fs.mkdtemp(path.join(os.tmpdir(), 'calib-tests-'));
for (const name of ['flash_fake.h', 'flash_fake.c', 'calib_host_test.c'])
    await fs.copyFile(path.join(testDir, name + '.in'), path.join(tempDir, name));
function run(command, args) {
    const result = spawnSync(command, args, {stdio: 'inherit', windowsHide: true});
    if (result.error) throw result.error;
    if (result.status !== 0) process.exit(result.status || 1);
}
const exe = path.join(tempDir, process.platform === 'win32' ? 'calib.exe' : 'calib');
run(process.argv[2] || 'gcc', ['-std=c11', '-Os', '-flto', '-Wall', '-Wextra', '-Werror',
    '-I' + tempDir, '-I' + path.join(coreDir, 'Calib'),
    path.join(tempDir, 'calib_host_test.c'), path.join(tempDir, 'flash_fake.c'), '-o', exe]);
run(exe, []);
console.log('Host test artifacts: ' + tempDir);
