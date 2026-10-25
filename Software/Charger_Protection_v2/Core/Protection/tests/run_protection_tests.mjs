import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const testDir = path.dirname(fileURLToPath(import.meta.url));
const moduleDir = path.dirname(testDir);
const rteDir = path.join(path.dirname(moduleDir), 'Rte');
const measurementDir = path.join(path.dirname(moduleDir), 'Measurement');
const coreDir = path.dirname(moduleDir);
const tempDir = await fs.mkdtemp(path.join(os.tmpdir(), 'protection-tests-'));
await fs.copyFile(path.join(testDir, 'main.h.in'), path.join(tempDir, 'main.h'));
await fs.copyFile(path.join(testDir, 'protection_host_test.c.in'), path.join(tempDir, 'test.c'));
const compiler = process.argv[2] || 'gcc';
const modes = [
    { name: 'RELEASE', flags: [] }, // No override: verifies the shipping default.
    { name: 'DEBUG', flags: ['-DPROTECTION_MODE=PROTECTION_MODE_DEBUG'] },
];
const executable = (name) => path.join(tempDir, name + (process.platform === 'win32' ? '.exe' : ''));
function run(command, args) {
    const result = spawnSync(command, args, { stdio: 'inherit', windowsHide: true });
    if (result.error) throw result.error;
    if (result.status !== 0) process.exit(result.status || 1);
}
const commonFlags = ['-std=c11', '-Os', '-flto', '-Wall', '-Wextra', '-Werror',
    '-I' + tempDir, '-I' + moduleDir, '-I' + rteDir, '-I' + measurementDir,
    '-I' + path.join(coreDir, 'Calib'), '-I' + path.join(coreDir, 'Com')];
for (const name of ['flash_fake.h', 'flash_fake.c'])
    await fs.copyFile(path.join(coreDir, 'Calib/tests', name + '.in'), path.join(tempDir, name));
for (const mode of modes) {
    console.log('Protection input mode: ' + mode.name);
    const exe = executable('protection_' + mode.name);
    run(compiler, [...commonFlags, ...mode.flags,
        path.join(tempDir, 'test.c'), path.join(moduleDir, 'Protection.c'),
        path.join(coreDir, 'Calib/Calib.c'), path.join(tempDir, 'flash_fake.c'),
        path.join(rteDir, 'Rte.c'), '-lm', '-o', exe]);
    run(exe, []);
}
// Compile the actual ADC/DMA measurement and relay modules into one host test.
const measurementTestDir = path.join(measurementDir, 'tests');
const combinedHeader = await fs.readFile(path.join(measurementTestDir, 'main.h.in'), 'utf8')
    + '\n' + await fs.readFile(path.join(testDir, 'main.h.in'), 'utf8');
await fs.writeFile(path.join(tempDir, 'main.h'), combinedHeader);
await fs.copyFile(path.join(measurementTestDir, 'measurement_host_test.c.in'),
    path.join(tempDir, 'measurement_host_test.c'));
await fs.copyFile(path.join(testDir, 'protection_integration_test.c.in'),
    path.join(tempDir, 'integration.c'));
for (const mode of modes) {
    console.log('Integration input mode: ' + mode.name);
    const integrationExe = executable('integration_' + mode.name);
    run(compiler, [...commonFlags, ...mode.flags, '-Wno-pointer-to-int-cast',
        '-x', 'c', path.join(tempDir, 'integration.c'),
        path.join(measurementTestDir, 'rte_measurement_consumer.c.in'),
        path.join(coreDir, 'Calib/Calib.c'), path.join(tempDir, 'flash_fake.c'),
        path.join(rteDir, 'Rte.c'), '-lm', '-o', integrationExe]);
    run(integrationExe, []);
}
const invalidMode = spawnSync(compiler, [...commonFlags, '-DPROTECTION_MODE=99',
    '-fsyntax-only', path.join(moduleDir, 'Protection.c')],
    { encoding: 'utf8', windowsHide: true });
if (invalidMode.error) throw invalidMode.error;
if (invalidMode.status === 0 || !invalidMode.stderr.includes('PROTECTION_MODE must be')) {
    throw new Error('Invalid protection mode was not rejected by the configuration guard');
}
console.log('Invalid input mode rejected as expected');
console.log('Host test artifacts: ' + tempDir);
