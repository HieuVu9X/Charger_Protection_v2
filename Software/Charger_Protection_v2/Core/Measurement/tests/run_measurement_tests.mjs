import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const testDir = path.dirname(fileURLToPath(import.meta.url));
const moduleDir = path.dirname(testDir);
const rteDir = path.join(path.dirname(moduleDir), 'Rte');
const protectionDir = path.join(path.dirname(moduleDir), 'Protection');
const comDir = path.join(path.dirname(moduleDir), 'Com');
const coreDir = path.dirname(moduleDir);
const tempDir = await fs.mkdtemp(path.join(os.tmpdir(), 'measurement-tests-'));
await fs.copyFile(path.join(testDir, 'main.h.in'), path.join(tempDir, 'main.h'));
await fs.copyFile(path.join(testDir, 'measurement_host_test.c.in'), path.join(tempDir, 'measurement_host_test.c'));
await fs.copyFile(path.join(testDir, 'rte_measurement_consumer.c.in'), path.join(tempDir, 'rte_measurement_consumer.c'));
const exe = path.join(tempDir, process.platform === 'win32' ? 'measurement_tests.exe' : 'measurement_tests');
const compiler = process.argv[2] || 'gcc';
for (const name of ['flash_fake.h', 'flash_fake.c'])
  await fs.copyFile(path.join(coreDir, 'Calib/tests', name + '.in'), path.join(tempDir, name));
function run(command, args) {
  const result = spawnSync(command, args, { stdio: 'inherit', windowsHide: true });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status || 1);
}
run(compiler, ['-std=c11', '-Os', '-flto', '-Wall', '-Wextra', '-Werror', '-Wno-pointer-to-int-cast', '-I'+tempDir, '-I'+moduleDir, '-I'+rteDir, '-I'+protectionDir, '-I'+comDir, '-I'+path.join(coreDir, 'Calib'), path.join(tempDir, 'measurement_host_test.c'), path.join(tempDir, 'rte_measurement_consumer.c'), path.join(rteDir, 'Rte.c'), path.join(coreDir, 'Calib/Calib.c'), path.join(tempDir, 'flash_fake.c'), '-lm', '-o', exe]);
run(exe, []);
console.log('Host test artifacts: ' + tempDir);
