import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const testDir = path.dirname(fileURLToPath(import.meta.url));
const rteDir = path.dirname(testDir);
const calibDir = path.join(path.dirname(rteDir), 'Calib');
const comDir = path.join(path.dirname(rteDir), 'Com');
const tempDir = await fs.mkdtemp(path.join(os.tmpdir(), 'scheduler-tests-'));
await fs.copyFile(path.join(testDir, 'main.h.in'), path.join(tempDir, 'main.h'));
await fs.copyFile(path.join(testDir, 'scheduler_host_test.c.in'), path.join(tempDir, 'test.c'));
await fs.writeFile(path.join(tempDir, 'Measurement.h'),
    'void MeasurementInit(void);\nvoid Measurement_MainFunction(void);\nvoid Measurement_DMA_IRQHandler(void);\n');
await fs.writeFile(path.join(tempDir, 'Protection.h'),
    'void ProtectionInit(void);\nvoid Protection_MainFunction(void);\nvoid Protection_Tick1msISR(void);\n'
    + 'void Protection_FastLeakageISR(void);\nvoid Protection_ZeroCrossISR(void);\nvoid Protection_EmergencyStop(void);\n');
const exe = path.join(tempDir, process.platform === 'win32' ? 'scheduler_tests.exe' : 'scheduler_tests');
function run(command, args) {
    const result = spawnSync(command, args, { stdio: 'inherit', windowsHide: true });
    if (result.error) throw result.error;
    if (result.status !== 0) process.exit(result.status || 1);
}
run(process.argv[2] || 'gcc', ['-std=c11', '-Os', '-flto', '-Wall', '-Wextra', '-Werror',
    '-I' + tempDir, '-I' + rteDir, '-I' + calibDir, '-I' + comDir, path.join(tempDir, 'test.c'),
    path.join(rteDir, 'scheduler.c'), '-o', exe]);
run(exe, []);
await fs.copyFile(path.join(testDir, 'rte_host_test.c.in'), path.join(tempDir, 'rte_test.c'));
const rteExe = path.join(tempDir, process.platform === 'win32' ? 'rte_tests.exe' : 'rte_tests');
run(process.argv[2] || 'gcc', ['-std=c11', '-Os', '-flto', '-Wall', '-Wextra', '-Werror',
    '-I' + tempDir, '-I' + rteDir, '-I' + calibDir, '-I' + comDir, path.join(tempDir, 'rte_test.c'),
    path.join(rteDir, 'Rte.c'), '-o', rteExe]);
run(rteExe, []);
console.log('Host test artifacts: ' + tempDir);
