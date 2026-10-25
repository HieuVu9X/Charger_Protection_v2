"""Charger com wire v1. No automatic retries of Flash writes or reset."""
from dataclasses import dataclass, astuple
from enum import IntEnum
import math
import struct
import time

SOF = b'\xa5\x5a'
VERSION = 1


class Command(IntEnum):
    INFO = 1
    MEAS = 2
    ACTIVE = 3
    STORED = 4
    SAVE = 5
    DEFAULTS = 6
    RESET = 7
    PROTECTION = 8
    STORED_PROTECTION = 9
    SAVE_PROTECTION = 10
    PROTECTION_DEFAULTS = 11
    LEAK_MAX = 12
    LEAK_COUNT_MAX = 13


class Status(IntEnum):
    OK = 0
    UNCHANGED = 1
    INVALID = 2
    BUSY = 3
    FLASH_ERROR = 4
    BAD_COMMAND = 0x80
    BAD_LENGTH = 0x81
    BAD_CRC = 0x82  # reserved; MCU silently drops bad CRC frames
    BAD_VERSION = 0x83
    NO_STORED = 0x84


class ProtocolError(Exception):
    pass


class DeviceError(ProtocolError):
    def __init__(self, status):
        self.status = status
        super().__init__(f'MCU: {status.name}')


def crc16(data):
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def frame(command, sequence, payload=b'', version=VERSION):
    if len(payload) > 72:
        raise ValueError('Payload too long')
    body = bytes((version, command, sequence, len(payload))) + payload
    return SOF + body + struct.pack('<H', crc16(body))


class Decoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 2:
            index = self.buffer.find(SOF)
            if index < 0:
                self.buffer[:] = self.buffer[-1:] if self.buffer[-1] == 0xA5 else b''
                break
            del self.buffer[:index]
            if len(self.buffer) < 6:
                break
            length = self.buffer[5]
            if length > 72:
                del self.buffer[0]
                continue
            total = length + 8
            if len(self.buffer) < total:
                break
            raw = bytes(self.buffer[:total])
            if crc16(raw[2:-2]) != struct.unpack('<H', raw[-2:])[0]:
                del self.buffer[0]
                continue
            del self.buffer[:total]
            frames.append((raw[2], raw[3], raw[4], raw[6:-2]))
        return frames


@dataclass(frozen=True)
class Params:
    vin_gain: float = 1.0
    vout_gain: float = 1.0
    current_gain: float = 1.0
    leakage_gain: float = 1.0
    temperature_offset_c: float = 0.0

    def validate(self):
        values = astuple(self)
        if (not all(math.isfinite(v) for v in values)
                or not all(0.25 <= v <= 4.0 for v in values[:4])
                or not -20.0 <= values[4] <= 20.0):
            raise ValueError('Gain phải trong 0.25..4; offset nhiệt độ trong -20..20 °C.')
        return self

    def pack(self):
        return struct.pack('<5f', *astuple(self.validate()))

    @classmethod
    def unpack(cls, data):
        if len(data) != 20:
            raise ProtocolError('Sai độ dài hệ số Calib')
        return cls(*struct.unpack('<5f', data)).validate()


@dataclass(frozen=True)
class ProtectionParams:
    ov_v: float = 240.0
    uv_in_v: float = 190.0
    oc_a: float = 2.8
    leak_a: float = 0.030
    ot_c: float = 70.0
    ov_recover_v: float = 230.0
    uv_in_recover_v: float = 200.0
    ot_recover_c: float = 65.0
    leak_fast_peak_a: float = 0.030
    recover_hold_ms: int = 1000

    def validate(self):
        if (not all(math.isfinite(v) for v in astuple(self))
                or not 1 <= self.uv_in_v < self.uv_in_recover_v < self.ov_recover_v < self.ov_v <= 400
                or not struct.unpack('<f', struct.pack('<f', .01))[0] <= self.oc_a <= 30
                or not .001 <= self.leak_a <= 1
                or not .001 <= self.leak_fast_peak_a <= 1
                or not -40 <= self.ot_recover_c < self.ot_c <= 150
                or isinstance(self.recover_hold_ms, bool)
                or not isinstance(self.recover_hold_ms, int)
                or not 1 <= self.recover_hold_ms <= 65535):
            raise ValueError('Ngưỡng không hợp lệ. Cần UV < UV phục hồi < OV phục hồi < OV; '
                             'nhiệt phục hồi < nhiệt cắt; thời gian 1..65535 ms. '
                             'Điện áp 1..400 V, tải 0.01..30 A, rò 1..1000 mA, nhiệt -40..150 °C.')
        return self

    def pack(self):
        self.validate()
        data = struct.pack('<9fI', *astuple(self))
        # Validate again after float32 rounding, as the MCU sees these values.
        type(self).unpack(data)
        return data

    @classmethod
    def unpack(cls, data):
        if len(data) != 40:
            raise ProtocolError('Sai độ dài cấu hình Protection')
        return cls(*struct.unpack('<9fI', data)).validate()


@dataclass(frozen=True)
class Info:
    protocol: int
    major: int
    minor: int
    flags: int
    sequence: int
    capabilities: int
    rx_errors: int

    @property
    def reset_required(self):
        return bool(self.flags & 4)

    @property
    def supports_protection(self):
        return self.capabilities & 0x780 == 0x780

    @property
    def supports_leak_max(self):
        return bool(self.capabilities & 0x800)

    @property
    def supports_leak_count_max(self):
        return bool(self.capabilities & 0x1000)


MEAS_NAMES = ('vin', 'iin', 'power', 'apparent_power', 'pf', 'frequency',
              'vout', 'ileak', 'temperature', 'vin_adc_offset', 'vout_adc_offset',
              'iin_adc_offset', 'ileak_adc_offset', 'leak_adc_rms', 'temp_adc',
              'clipped', 'faults', 'ready', 'leak_trip', 'temp_fault')


def corrected_value(active, measured, reference, temperature=False):
    if not all(math.isfinite(v) for v in (active, measured, reference)):
        raise ValueError('Số đo và giá trị chuẩn phải hữu hạn.')
    if temperature:
        result = active + reference - measured
        limits = (-20, 20)
    else:
        if measured <= 0 or reference <= 0:
            raise ValueError('Số đo và giá trị chuẩn phải > 0.')
        result = active * reference / measured
        limits = (0.25, 4)
    if not limits[0] <= result <= limits[1]:
        raise ValueError('Hệ số tính được ngoài giới hạn cho phép.')
    return result


class Client:
    """Single-threaded stop-and-wait client; GUI serializes it on one worker."""
    def __init__(self, port=None, transport=None, timeout=3.0):
        if transport is None:
            import serial
            transport = serial.Serial(port, 115200, timeout=0.05,
                                      write_timeout=1.0, rtscts=False, dsrdtr=False)
        self.serial = transport
        self.timeout = timeout
        self.sequence = 0

    def close(self):
        self.serial.close()

    def request(self, command, payload=b''):
        if len(payload) > 40:
            raise ValueError('MCU request payload limit is 40 bytes')
        self.sequence = (self.sequence + 1) & 255
        self.serial.reset_input_buffer()
        wire = frame(command, self.sequence, payload)
        if self.serial.write(wire) != len(wire):
            raise ProtocolError('UART write incomplete; trạng thái lệnh chưa xác định.')
        self.serial.flush()
        decoder = Decoder()
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            for version, reply, sequence, data in decoder.feed(self.serial.read(80)):
                if reply != (int(command) | 0x80) or sequence != self.sequence:
                    continue
                if version != VERSION or not data:
                    raise ProtocolError('Sai phiên bản hoặc thiếu status trong phản hồi')
                try:
                    status = Status(data[0])
                except ValueError as exc:
                    raise ProtocolError(f'Status lạ: {data[0]}') from exc
                if status not in (Status.OK, Status.UNCHANGED):
                    raise DeviceError(status)
                return status, data[1:]
        raise TimeoutError('MCU không phản hồi. Không tự gửi lại lệnh; đọc Flash để kiểm tra nếu vừa lưu.')

    def info(self):
        _, data = self.request(Command.INFO)
        if len(data) != 12:
            raise ProtocolError('Sai độ dài INFO')
        info = Info(*struct.unpack('<BBBBIHH', data))
        if info.protocol != VERSION or info.capabilities & 0x7F != 0x7F:
            raise ProtocolError('Firmware chưa hỗ trợ đủ giao thức v1')
        return info

    def measurement(self):
        _, data = self.request(Command.MEAS)
        if len(data) != 71:
            raise ProtocolError('Sai độ dài MEAS')
        return dict(zip(MEAS_NAMES, struct.unpack('<15fIIBBB', data)))

    def leakage_maxima(self):
        _, data = self.request(Command.LEAK_MAX)
        if len(data) != 9:
            raise ProtocolError('Sai độ dài LEAK_MAX')
        peak, rms, ready = struct.unpack('<ffB', data)
        if not all(math.isfinite(v) and v >= 0 for v in (peak, rms)) or ready not in (0, 1):
            raise ProtocolError('LEAK_MAX không hợp lệ')
        return dict(ileak_peak_max=peak, ileak_rms_max=rms, fast_ready=ready)

    def leakage_over_count_max(self):
        _, data = self.request(Command.LEAK_COUNT_MAX)
        if len(data) != 1:
            raise ProtocolError('Sai độ dài LEAK_COUNT_MAX')
        return data[0]

    def active(self):
        return Params.unpack(self.request(Command.ACTIVE)[1])

    def stored(self):
        try:
            return Params.unpack(self.request(Command.STORED)[1])
        except DeviceError as exc:
            if exc.status == Status.NO_STORED:
                return None
            raise

    def save(self, params):
        expected = params.pack()
        status, flags = self.request(Command.SAVE, expected)
        if len(flags) != 1:
            raise ProtocolError('Sai độ dài SAVE')
        stored = self.stored()
        if stored is None or stored.pack() != expected:
            raise ProtocolError('Đọc lại Flash khác dữ liệu vừa lưu.')
        return status, flags[0]

    def defaults(self):
        status, flags = self.request(Command.DEFAULTS)
        if len(flags) != 1:
            raise ProtocolError('Sai độ dài DEFAULTS')
        stored = self.stored()
        if stored is None or stored.pack() != Params().pack():
            raise ProtocolError('Đọc lại Flash khác hệ số mặc định.')
        return status, flags[0]

    def reset(self):
        _, data = self.request(Command.RESET)
        if data:
            raise ProtocolError('Sai độ dài RESET')
        time.sleep(0.6)
        return self.info()

    def protection(self):
        return ProtectionParams.unpack(self.request(Command.PROTECTION)[1])

    def stored_protection(self):
        try:
            return ProtectionParams.unpack(self.request(Command.STORED_PROTECTION)[1])
        except DeviceError as exc:
            if exc.status == Status.NO_STORED:
                return None
            raise

    def save_protection(self, params):
        expected = params.pack()
        status, flags = self.request(Command.SAVE_PROTECTION, expected)
        if len(flags) != 1:
            raise ProtocolError('Sai độ dài SAVE_PROTECTION')
        stored = self.stored_protection()
        if stored is None or stored.pack() != expected:
            raise ProtocolError('Đọc lại Flash khác ngưỡng vừa lưu.')
        return status, flags[0]

    def protection_defaults(self):
        status, flags = self.request(Command.PROTECTION_DEFAULTS)
        if len(flags) != 1:
            raise ProtocolError('Sai độ dài PROTECTION_DEFAULTS')
        stored = self.stored_protection()
        if stored is None or stored.pack() != ProtectionParams().pack():
            raise ProtocolError('Đọc lại Flash khác ngưỡng mặc định.')
        return status, flags[0]
