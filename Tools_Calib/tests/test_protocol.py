"""Run with python -m unittest discover -s Tools/tests -v.
Cross-language tests build and execute actual MCU components using host gcc.
"""
from dataclasses import replace
import math
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent if (TOOLS.parent/'Core').is_dir() else TOOLS.parent/'software/Charger_Protection_v2'
sys.path.insert(0, str(TOOLS))
from protocol import (Client, Params, ProtectionParams, Command, Status, DeviceError, ProtocolError,
                      Decoder, frame, crc16, corrected_value)


class Simulator:
    def __init__(self, exe):
        self.process = subprocess.Popen([str(exe)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.rx = bytearray()

    def command(self, op, data=''):
        if isinstance(data, bytes):
            data = data.hex(' ')
        self.process.stdin.write(op + ' ' + str(data) + '\n')
        self.process.stdin.flush()
        response = self.process.stdout.readline().strip()
        if not response:
            raise AssertionError('Simulator failed: ' + self.process.stderr.read())
        return response

    def raw(self, data, op='x'):
        response = self.command(op, data)
        return b'' if response == '-' else bytes.fromhex(response)

    def stats(self):
        return tuple(map(int, self.command('s').split()))

    def reset_input_buffer(self):
        self.rx.clear()

    def write(self, data):
        self.rx.extend(self.raw(data))
        return len(data)

    def flush(self):
        pass

    def read(self, size):
        result = bytes(self.rx[:size])
        del self.rx[:size]
        return result

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=3)
        stderr = self.process.stderr.read()
        self.process.stderr.close()
        self.process.stdout.close()
        if self.process.returncode:
            raise AssertionError(stderr)


class CodecTests(unittest.TestCase):
    def test_leak_count_max_validation(self):
        client=Client(transport=object())
        for data in (b'',b'\x01\x02'):
            with patch.object(client,'request',return_value=(Status.OK,data)):
                with self.assertRaises(ProtocolError): client.leakage_over_count_max()
        for value in (0,20,255):
            with patch.object(client,'request',return_value=(Status.OK,bytes([value]))):
                self.assertEqual(client.leakage_over_count_max(),value)

    def test_leak_max_validation(self):
        client=Client(transport=object())
        for data in (b'',struct.pack('<ffB',math.nan,0,1),
                     struct.pack('<ffB',0,-1,1),struct.pack('<ffB',0,0,2)):
            with patch.object(client,'request',return_value=(Status.OK,data)):
                with self.assertRaises(ProtocolError): client.leakage_maxima()

    def test_protection_validation_and_layout(self):
        self.assertEqual(len(ProtectionParams().pack()), 40)
        self.assertEqual(ProtectionParams.unpack(ProtectionParams().pack()).pack(), ProtectionParams().pack())
        for key in ProtectionParams.__dataclass_fields__:
            for value in (math.nan, math.inf, -math.inf):
                with self.assertRaises(ValueError):
                    replace(ProtectionParams(), **{key: value}).pack()
        for changes in (dict(uv_in_v=200), dict(ov_v=230), dict(ot_c=65),
                        dict(leak_a=0), dict(leak_fast_peak_a=1.1),
                        dict(recover_hold_ms=0), dict(recover_hold_ms=65536),
                        dict(recover_hold_ms=1.5)):
            with self.assertRaises(ValueError):
                replace(ProtectionParams(), **changes).pack()
        replace(ProtectionParams(), oc_a=.01, leak_a=.001, recover_hold_ms=65535).pack()

    def test_crc_golden_and_chunked_noise(self):
        self.assertEqual(crc16(b'123456789'), 0x29B1)
        packet = frame(Command.ACTIVE | 0x80, 255, b'\0' + Params().pack())
        decoder = Decoder()
        result = []
        for value in b'noise\xa5' + packet:
            result.extend(decoder.feed(bytes([value])))
        self.assertEqual(result, [(1, 0x83, 255, b'\0' + Params().pack())])

    def test_bad_crc_oversize_and_recovery(self):
        good = frame(0x81, 2, b'\0')
        bad = bytearray(good)
        bad[-1] ^= 1
        result = Decoder().feed(b'\xa5\x5a\x01\x81\x00\xff' + bad + good)
        self.assertEqual(result, [(1, 0x81, 2, b'\0')])

    def test_validation_and_formula(self):
        for value in (float('nan'), float('inf'), -1, 0.249, 4.01):
            with self.assertRaises(ValueError):
                replace(Params(), vin_gain=value).pack()
        self.assertAlmostEqual(corrected_value(1.1, 220, 230), 1.15)
        self.assertEqual(corrected_value(2, 28, 25, True), -1)
        for args in ((1, 0, 220), (1, 220, float('nan')), (1, 1, 10)):
            with self.assertRaises(ValueError):
                corrected_value(*args)
        self.assertEqual(Params.unpack(Params().pack()), Params())

    def test_client_filters_stale_and_no_automatic_write_retry(self):
        class Port:
            writes = 0
            def reset_input_buffer(self): pass
            def flush(self): pass
            def write(self, data):
                self.writes += 1
                return len(data)
            def read(self, size): return b''
        port = Port()
        client = Client(transport=port, timeout=0.001)
        with self.assertRaises(TimeoutError):
            client.save(Params())
        self.assertEqual(port.writes, 1)
        data = struct.pack('<BBBBIHH', 1, 2, 0, 0, 0, 0x7F, 0)
        class ReplyPort(Port):
            def write(self, request):
                self.reply = (frame(0x81, (request[4] - 1) & 255, b'\0' + data)
                              + frame(0x82, request[4], b'\0')
                              + frame(0x81, request[4], b'\0' + data))
                return len(request)
            def read(self, size):
                result, self.reply = self.reply[:3], self.reply[3:]
                return result
        self.assertEqual(Client(transport=ReplyPort()).info().major, 2)


class McuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='charger-com-')
        folder = Path(cls.temp.name)
        for directory, names in ((ROOT/'Core/com/tests', ('main.h', 'com_sim.c')),
                                 (ROOT/'Core/Calib/tests', ('flash_fake.h', 'flash_fake.c'))):
            for name in names:
                shutil.copyfile(directory/(name + '.in'), folder/name)
        cls.exe = folder/'com_sim.exe'
        gcc = os.environ.get('HOST_GCC', 'C:/mingw64/bin/gcc.exe' if os.name == 'nt' else 'gcc')
        command = [gcc, '-std=c11', '-Os', '-flto', '-Wall', '-Wextra', '-Werror',
                   '-I'+str(folder)]
        command += ['-I'+str(ROOT/'Core'/directory) for directory in ('Rte', 'Calib', 'Protection', 'com')]
        command += [str(folder/'com_sim.c'), str(folder/'flash_fake.c')]
        command += [str(ROOT/'Core'/file) for file in ('com/com.c', 'Calib/Calib.c', 'Rte/Rte.c')]
        subprocess.run(command + ['-o', str(cls.exe)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.sim = Simulator(self.exe)
        self.client = Client(transport=self.sim, timeout=0.01)

    def tearDown(self):
        self.sim.close()

    def response(self, data):
        frames = Decoder().feed(data)
        self.assertEqual(len(frames), 1)
        return frames[0][3]

    def test_boot_info_and_measurement_layout(self):
        info = self.client.info()
        self.assertEqual((info.flags, info.capabilities, info.rx_errors), (0, 0x1FFF, 0))
        self.assertEqual(self.client.active(), Params())
        self.assertIsNone(self.client.stored())
        m = self.client.measurement()
        expected = (220, 1.5, 300, 330, .91, 50, 219, .012, 25,
                    1.65, 1.64, 1.63, 1.62, 22, 2048, 2, 0x41, 1, 1, 0)
        for actual, value in zip(m.values(), expected):
            self.assertAlmostEqual(actual, value, places=5)
        self.assertEqual(self.sim.stats(), (0, 0, 0, 0))

    def test_leakage_maxima_command(self):
        self.assertTrue(self.client.info().supports_leak_max)
        values=self.client.leakage_maxima()
        self.assertAlmostEqual(values['ileak_peak_max'],.050)
        self.assertAlmostEqual(values['ileak_rms_max'],.034)
        self.assertEqual(values['fast_ready'],1)
        status=self.response(self.sim.raw(frame(Command.LEAK_MAX,40,b'\0')))[0]
        self.assertEqual(status,Status.BAD_LENGTH)
        self.assertEqual(self.sim.stats(),(0,0,0,0))

    def test_leakage_count_max_command(self):
        info=self.client.info()
        self.assertEqual((info.major,info.minor),(2,7))
        self.assertTrue(info.supports_leak_count_max)
        self.assertEqual(self.client.leakage_over_count_max(),7)
        data=self.response(self.sim.raw(frame(Command.LEAK_COUNT_MAX,41)))
        self.assertEqual(data,bytes((Status.OK,7)))
        status=self.response(self.sim.raw(frame(Command.LEAK_COUNT_MAX,42,b'\0')))[0]
        self.assertEqual(status,Status.BAD_LENGTH)
        self.assertEqual(self.sim.stats(),(0,0,0,0))

    def test_protection_save_preserves_calib_reset_and_restore(self):
        defaults = ProtectionParams().pack()
        self.assertTrue(self.client.info().supports_protection)
        self.assertEqual(self.client.protection().pack(), defaults)
        self.assertIsNone(self.client.stored_protection())
        params = replace(ProtectionParams(), ov_v=250, oc_a=2, leak_a=.02,
                         leak_fast_peak_a=.05, recover_hold_ms=345)
        self.assertEqual(self.client.save_protection(params), (Status.OK, 6))
        self.assertEqual(self.client.protection().pack(), defaults)
        self.assertEqual(self.client.stored_protection().pack(), params.pack())
        calib = Params(1.125)
        self.client.save(calib)
        self.assertEqual(self.client.stored_protection().pack(), params.pack())
        self.client.reset()
        self.assertEqual(self.client.protection().pack(), params.pack())
        self.assertEqual(self.client.active(), calib)
        erases = self.sim.stats()[0]
        self.assertEqual(self.client.save_protection(params)[0], Status.UNCHANGED)
        self.assertEqual(self.sim.stats()[0], erases)
        self.client.defaults()
        self.assertEqual(self.client.stored_protection().pack(), params.pack())
        self.client.save(calib)
        self.client.protection_defaults()
        self.assertEqual(self.client.stored(), calib)
        self.client.reset()
        self.assertEqual(self.client.protection().pack(), defaults)
        self.assertEqual(self.client.active(), calib)

    def test_protection_invalid_length_busy_failure_and_fragmented_frame(self):
        params = replace(ProtectionParams(), oc_a=2)
        self.assertEqual(self.response(self.sim.raw(frame(10, 1, params.pack()[:-1])))[0], Status.BAD_LENGTH)
        bad = bytearray(params.pack()); bad[:4] = struct.pack('<f', math.nan)
        self.assertEqual(self.response(self.sim.raw(frame(10, 2, bad)))[0], Status.INVALID)
        self.assertEqual(self.sim.stats(), (0, 0, 0, 0))
        self.sim.command('b', 1)
        with self.assertRaises(DeviceError) as caught:
            self.client.save_protection(params)
        self.assertEqual(caught.exception.status, Status.BUSY)
        self.sim.command('b', 0)
        request = frame(10, 3, params.pack())
        self.assertEqual(self.sim.raw(request[:30]), b'')
        self.assertEqual(self.response(self.sim.raw(request[30:]))[0], Status.OK)
        self.sim.command('f', 2)
        with self.assertRaises(DeviceError) as caught:
            self.client.save_protection(replace(params, oc_a=1.5))
        self.assertEqual(caught.exception.status, Status.FLASH_ERROR)
        self.assertTrue(self.client.info().reset_required)
        self.assertIsNone(self.client.stored_protection())
        self.client.reset()
        self.assertEqual(self.client.protection().pack(), ProtectionParams().pack())

    def test_save_active_stored_reset_default_and_unchanged(self):
        params = Params(1.01, .99, 1.02, 1.1, -1.5)
        expected = Params.unpack(params.pack())
        self.assertEqual(self.client.save(params), (Status.OK, 6))
        self.assertEqual(self.client.active(), Params())
        self.assertEqual(self.client.stored(), expected)
        self.assertEqual(self.client.save(params), (Status.UNCHANGED, 6))
        self.assertEqual(self.sim.stats()[:2], (1, 39))
        self.assertTrue(self.client.info().reset_required)
        self.assertEqual(self.client.reset().flags, 3)
        self.assertEqual(self.client.active(), expected)
        self.assertEqual(self.client.save(params), (Status.UNCHANGED, 3))
        self.assertEqual(self.client.defaults(), (Status.OK, 7))
        self.assertEqual(self.client.active(), expected)
        self.assertEqual(self.client.reset().flags, 3)
        self.assertEqual(self.client.active(), Params())
        self.assertEqual(self.sim.stats(), (2, 78, 2, 1))

    def test_validation_busy_and_flash_failure(self):
        for values in ((math.nan, 1, 1, 1, 0), (1, 1, 1, 1, 20.1), (.1, 1, 1, 1, 0)):
            data = self.response(self.sim.raw(frame(5, 8, struct.pack('<5f', *values))))
            self.assertEqual(data, bytes((Status.INVALID, 0)))
        self.assertEqual(self.sim.stats(), (0, 0, 0, 0))
        self.sim.command('b', 1)
        with self.assertRaises(DeviceError) as caught:
            self.client.save(Params(1.1))
        self.assertEqual(caught.exception.status, Status.BUSY)
        self.assertFalse(self.client.info().reset_required)
        self.sim.command('b', 0)
        self.sim.command('f', 2)
        with self.assertRaises(DeviceError) as caught:
            self.client.save(Params(1.1))
        self.assertEqual(caught.exception.status, Status.FLASH_ERROR)
        self.assertEqual(self.client.info().flags, 4)
        self.assertIsNone(self.client.stored())
        self.assertEqual(self.client.reset().flags, 0)
        self.assertEqual(self.client.active(), Params())

    def test_invalid_frames_never_write(self):
        valid = frame(5, 99, Params(1.1).pack())
        bad = bytearray(valid); bad[-1] ^= 1
        self.assertEqual(self.sim.raw(bytes(bad)), b'')
        self.assertEqual(self.client.info().rx_errors, 1)
        self.assertEqual(self.response(self.sim.raw(frame(5, 1, b'\0')))[0], Status.BAD_LENGTH)
        self.assertEqual(self.response(self.sim.raw(frame(42, 1)))[0], Status.BAD_COMMAND)
        self.assertEqual(self.response(self.sim.raw(frame(1, 1, version=2)))[0], Status.BAD_VERSION)
        self.assertEqual(self.sim.raw(b'\xa5\x5a\x01\x05\x01\xff'), b'')
        self.assertEqual(self.sim.raw(frame(0x85, 1, Params(1.1).pack())), b'')
        self.assertEqual(self.sim.stats(), (0, 0, 0, 0))
        self.assertEqual(self.client.active(), Params())

    def test_partial_timeout_and_wrap_recovery(self):
        request = frame(5, 1, Params(1.1).pack())
        self.assertEqual(self.sim.raw(request[:9]), b'')
        self.sim.command('a', 200)
        self.assertEqual(self.sim.raw(request[9:]), b'')
        self.assertEqual(self.sim.stats()[0], 0)
        # Clock wrap with a valid partial frame: elapsed 199ms then final bytes.
        self.sim.command('a', 4294967000)
        self.assertEqual(self.sim.raw(request[:9]), b'')
        self.sim.command('a', 199)
        self.assertEqual(self.response(self.sim.raw(request[9:]))[0], Status.OK)

    def test_uart_error_and_overflow_flush_partial_frame(self):
        request = frame(5, 1, Params(1.1).pack())
        self.sim.raw(request[:9])
        self.sim.command('e')
        self.assertEqual(self.sim.raw(request[9:]), b'')
        self.sim.raw(b'\0' * 160, op='r')
        self.sim.command('n')
        self.assertEqual(self.client.active(), Params())
        self.assertGreaterEqual(self.client.info().rx_errors, 2)
        self.assertEqual(self.sim.stats()[0], 0)

    def test_flash_runs_only_in_task_and_reset_waits_for_tc(self):
        request = frame(5, 1, Params(1.1).pack())
        self.sim.raw(request, op='r')
        self.assertEqual(self.sim.stats()[0], 0)
        self.sim.command('n')
        self.assertEqual(self.sim.stats()[0], 1)
        self.assertEqual(self.response(self.sim.raw(b'', op='d'))[0], Status.OK)
        self.sim.command('t')
        self.sim.raw(frame(7, 2), op='q')
        self.assertEqual(self.sim.stats()[2], 0)
        self.assertEqual(self.response(self.sim.raw(b'', op='d')), b'\0')
        self.assertEqual(self.sim.stats()[2], 0)
        self.sim.command('t')
        self.assertEqual(self.sim.stats()[2], 1)

    def test_fuzz_then_valid_command(self):
        rng = random.Random(11)
        for _ in range(200):
            noise = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 170)))
            self.sim.raw(noise)
            self.sim.command('a', 200)
            self.sim.command('n')
            self.assertEqual(self.client.active(), Params())
        self.assertEqual(self.sim.stats()[0], 0)


if __name__ == '__main__':
    unittest.main()
