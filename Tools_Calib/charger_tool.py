"""Windows desktop tool for Charger com v1. Tk UI, one serial worker."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, fields, astuple
import json
import math
from pathlib import Path
import queue
import struct
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from protocol import Client, Params, ProtectionParams, corrected_value

PROTECTION_FIELDS = (
    ('ov_v', 'Quá áp Vin / Vout', 'V', 1),
    ('uv_in_v', 'Thấp áp Vin', 'V', 1),
    ('oc_a', 'Quá dòng tải', 'A', 1),
    ('leak_a', 'Dòng rò RMS', 'mA', 1000),
    ('ot_c', 'Quá nhiệt', '°C', 1),
    ('ov_recover_v', 'Phục hồi quá áp', 'V', 1),
    ('uv_in_recover_v', 'Phục hồi thấp áp', 'V', 1),
    ('ot_recover_c', 'Phục hồi nhiệt độ', '°C', 1),
    ('leak_fast_peak_a', 'Dòng rò nhanh (đỉnh)', 'mA', 1000),
    ('recover_hold_ms', 'Chờ phục hồi', 'ms', 1),
)

CHANNELS = (
    ('vin_gain', 'Vin', 'vin', 'V', 1),
    ('vout_gain', 'Vout', 'vout', 'V', 2),
    ('current_gain', 'Iin', 'iin', 'A', 4),
    ('leakage_gain', 'Ileak', 'ileak', 'mA', 8),
    ('temperature_offset_c', 'Nhiệt độ', 'temperature', '°C', 16),
)
DISPLAY = (
    ('vin', 'Vin', 'V'), ('vout', 'Vout', 'V'), ('iin', 'Iin', 'A'),
    ('ileak', 'Dòng rò', 'mA'), ('temperature', 'Nhiệt độ', '°C'),
    ('power', 'Công suất P', 'W'), ('apparent_power', 'Công suất S', 'VA'),
    ('pf', 'PF', ''), ('frequency', 'Tần số', 'Hz'),
)
DEBUG_DISPLAY = (
    ('leakage_over_count_max', 'leakage_over_count_max', 'mẫu'),
    ('ileak_peak_max', 'Ileak đỉnh MAX', 'mA'),
    ('ileak_rms_max', 'Ileak RMS MAX', 'mA'),
    ('vin_adc_offset', 'ADC offset Vin', 'V'),
    ('vout_adc_offset', 'ADC offset Vout', 'V'),
    ('iin_adc_offset', 'ADC offset Iin', 'V'),
    ('ileak_adc_offset', 'ADC offset Ileak', 'V'),
    ('leak_adc_rms', 'ADC RMS dòng rò', 'count'), ('temp_adc', 'ADC nhiệt độ', 'count'),
)

# Protection.h fault bits are latched; they do not mean the latest reading
# still exceeds its threshold. Keep the raw mask available for diagnostics.
FAULT_NAMES = (
    (1 << 0, 'Điện áp vào cao'),
    (1 << 1, 'Điện áp ra cao'),
    (1 << 2, 'Dòng tải vượt ngưỡng'),
    (1 << 3, 'Dòng rò vượt ngưỡng'),
    (1 << 4, 'Nhiệt độ vượt ngưỡng'),
    (1 << 5, 'Dữ liệu đo không hợp lệ'),
    (1 << 6, 'Điện áp vào thấp'),
)
ADC_CHANNEL_NAMES = ((1, 'Vin'), (2, 'Vout'), (4, 'dòng tải'),
                     (8, 'dòng rò'), (16, 'nhiệt độ'))


def fault_description(mask):
    if not mask:
        return 'Chưa ghi nhận lỗi bảo vệ', ''
    names = [name for bit, name in FAULT_NAMES if mask & bit]
    unknown = mask & ~0x7f
    if unknown:
        names.append(f'Lỗi chưa nhận diện (0x{unknown:08X})')
    title = 'Lỗi đã ghi nhận:\n' + '\n'.join(f'• {name}' for name in names)
    if mask & 0x2c:  # OC / leakage / invalid measurement remain latched until reset.
        hint = 'Có lỗi giữ chốt: xử lý nguyên nhân rồi reset MCU để xóa lỗi.'
    elif unknown:
        hint = 'Có mã lỗi chưa nhận diện; cần kiểm tra phiên bản firmware.'
    else:
        hint = 'Có thể tự phục hồi khi số đo ổn định trong vùng cho phép đủ thời gian đã cấu hình.'
    return title, hint


def adc_description(mask):
    names = [name for bit, name in ADC_CHANNEL_NAMES if mask & bit]
    if mask & ~0x1f:
        names.append(f'kênh chưa nhận diện (0x{mask & ~0x1f:X})')
    return ', '.join(names) if names else 'Không'


def protection_text(value, scale):
    if isinstance(value, int):
        return str(value)
    short = f'{value * scale:.7g}'
    if struct.pack('<f', float(short) / scale) == struct.pack('<f', value):
        return short
    return f'{value * scale:.9g}'


def configure_flat_style(root):
    """Use a consistent white, flat theme across Windows Tk installations."""
    white, ink, muted = '#ffffff', '#172b4d', '#627086'
    line, soft, blue = '#dce3ec', '#f3f6fa', '#2563eb'
    root.configure(background=white)
    root.option_add('*TCombobox*Listbox.font', ('Segoe UI', 10))
    root.option_add('*Listbox.background', white)
    root.option_add('*Listbox.foreground', ink)
    root.option_add('*Listbox.selectBackground', '#e7efff')
    root.option_add('*Listbox.selectForeground', ink)
    style = ttk.Style(root)
    style.theme_use('clam')
    style.configure('.', background=white, foreground=ink, font=('Segoe UI', 10),
                    bordercolor=line, lightcolor=line, darkcolor=line,
                    focuscolor=blue)
    style.configure('TFrame', background=white)
    style.configure('TLabel', background=white, padding=0)
    style.configure('Title.TLabel', font=('Segoe UI', 23, 'bold'))
    style.configure('Muted.TLabel', foreground=muted)
    style.configure('Status.TLabel', foreground=blue)
    style.configure('Fault.TLabel', foreground='#b42318', font=('Segoe UI', 10, 'bold'))
    style.configure('TLabelframe', background=white, borderwidth=0, relief='flat')
    style.configure('TLabelframe.Label', background=white, foreground=ink,
                    font=('Segoe UI', 11, 'bold'))
    # Remove the raised border while retaining keyboard focus and padding.
    style.layout('TButton', [('Button.focus', {'sticky': 'nswe', 'children': [
        ('Button.padding', {'sticky': 'nswe', 'children': [
            ('Button.label', {'sticky': 'nswe'})]})]})])
    style.configure('TButton', background=soft, foreground=ink,
                    padding=(13, 6), borderwidth=0, relief='flat', focusthickness=1)
    style.map('TButton', background=[('disabled', '#f4f6f9'), ('pressed', '#dce3ec'),
                                   ('active', '#e8edf5')],
              foreground=[('disabled', '#8b96a7')])
    style.configure('Primary.TButton', background=blue, foreground=white)
    style.map('Primary.TButton',
              background=[('disabled', '#e7efff'), ('pressed', '#1e40af'),
                          ('active', '#1d4ed8')],
              foreground=[('disabled', '#8299c3'), ('!disabled', white)])
    style.configure('Compact.TButton', padding=(11, 4))
    style.configure('TEntry', fieldbackground=white, padding=(9, 4),
                    borderwidth=1, relief='flat', insertcolor=ink)
    style.map('TEntry', bordercolor=[('focus', blue)],
              lightcolor=[('focus', blue)], darkcolor=[('focus', blue)])
    style.configure('TCombobox', fieldbackground=white, background=soft,
                    arrowcolor=muted, padding=(9, 4), borderwidth=1, relief='flat')
    style.map('TCombobox', fieldbackground=[('disabled', soft), ('readonly', white)],
              foreground=[('disabled', '#8b96a7'), ('readonly', ink)],
              background=[('active', '#e8edf5')], bordercolor=[('focus', blue)])
    style.configure('TCheckbutton', background=white, padding=(0, 6),
                    indicatorbackground=white, indicatorforeground=blue)
    style.map('TCheckbutton', background=[('active', white)],
              indicatorbackground=[('selected', blue)],
              indicatorforeground=[('selected', white)])
    style.configure('Treeview', background=white, fieldbackground=white,
                    foreground=ink, rowheight=26, borderwidth=0, relief='flat')
    style.layout('Treeview', [('Treeview.treearea', {'sticky': 'nswe'})])
    style.configure('Treeview.Heading', background=soft, foreground=muted,
                    font=('Segoe UI', 10, 'bold'), padding=(10, 9),
                    borderwidth=0, relief='flat')
    style.map('Treeview.Heading', background=[('active', '#e8edf5')])
    style.map('Treeview', background=[('selected', '#e7efff')],
              foreground=[('selected', ink)])
    style.configure('TSeparator', background=line)
    style.configure('TPanedwindow', background=white)
    style.configure('Sash', sashthickness=16, background=white)


class App:
    def __init__(self, root):
        self.root = root
        self.root.title('Charger • Calib & Protection Tool')
        self.root.geometry('1180x860')
        self.root.minsize(1100, 850)
        self.client = None
        self.executor = ThreadPoolExecutor(max_workers=1)
        self.results = queue.Queue()
        self.busy = False
        self.foreground_busy = False
        self.closing = False
        self.active = Params()
        self.active_protection = None
        self.protection_pending = {key: tk.StringVar(value=f'{getattr(ProtectionParams(), key) * scale:.9g}')
                                   for key, _, _, scale in PROTECTION_FIELDS}
        self.protection_active_labels = {key: tk.StringVar(value='—') for key in self.protection_pending}
        self.protection_stored_labels = {key: tk.StringVar(value='—') for key in self.protection_pending}
        self.protection_note = tk.StringVar(value='Kết nối MCU để đọc ngưỡng.')
        self.info = None
        self.meas = None
        self.meas_time = 0
        self.failures = 0
        self.status = tk.StringVar(value='Chưa kết nối')
        self.board = tk.StringVar(value='Kết nối MCU để đọc trạng thái.')
        self.health = tk.StringVar(value='Chưa có số đo')
        self.fault_text = tk.StringVar(value='Chưa có trạng thái bảo vệ')
        self.fault_hint = tk.StringVar()
        self.fault_code = tk.StringVar()
        self.auto = tk.BooleanVar(value=True)
        self.port = tk.StringVar()
        self.pending = {f.name: tk.StringVar(value=str(v))
                        for f, v in zip(fields(Params), astuple(Params()))}
        self.references = {c[0]: tk.StringVar() for c in CHANNELS}
        self.active_labels = {c[0]: tk.StringVar(value='—') for c in CHANNELS}
        self.stored_labels = {c[0]: tk.StringVar(value='—') for c in CHANNELS}
        configure_flat_style(root)
        shell = ttk.Frame(root, padding=20)
        shell.pack(fill='both', expand=True)
        ttk.Label(shell, text='Charger Config', style='Title.TLabel').pack(anchor='w')
        ttk.Label(shell, text='Hệ số bù sai số linh kiện • UART 115200, 8N1 • Protocol v1',
                  style='Muted.TLabel').pack(anchor='w', pady=(4, 0))
        bar = ttk.Frame(shell)
        bar.pack(fill='x', pady=(16, 10))
        ttk.Label(bar, text='Cổng COM').pack(side='left')
        self.ports = ttk.Combobox(bar, textvariable=self.port, width=29, state='readonly')
        self.ports.pack(side='left', padx=5)
        self.refresh_button = ttk.Button(bar, text='Tìm cổng', command=self.refresh_ports)
        self.refresh_button.pack(side='left')
        self.connect_button = ttk.Button(bar, text='Kết nối', style='Primary.TButton', command=self.toggle_connection)
        self.connect_button.pack(side='left', padx=5)
        self.read_button = ttk.Button(bar, text='Đọc lại', command=self.read_all)
        self.read_button.pack(side='left')
        ttk.Checkbutton(bar, text='Tự đọc số đo (0.5 s)', variable=self.auto).pack(side='right')
        ttk.Label(shell, textvariable=self.board, wraplength=1060, style='Muted.TLabel').pack(anchor='w')
        ttk.Separator(shell).pack(fill='x', pady=(10, 6))
        panels = ttk.Panedwindow(shell, orient='horizontal')
        panels.pack(fill='both', expand=True, pady=(8, 14))
        left = ttk.LabelFrame(panels, text='Số đo MCU', padding=(0, 12, 12, 0))
        config_tabs = ttk.Notebook(panels)
        right = ttk.Frame(config_tabs, padding=(12, 12, 0, 0))
        protection_panel = ttk.Frame(config_tabs, padding=12)
        debug_panel = ttk.Frame(config_tabs, padding=12)
        config_tabs.add(right, text='Calib hệ số đo')
        config_tabs.add(protection_panel, text='Ngưỡng Protection')
        config_tabs.add(debug_panel, text='Debug')
        panels.add(left, weight=2)
        panels.add(config_tabs, weight=3)
        for col, title in enumerate(('Ngưỡng', 'Đang chạy', 'Trong Flash', 'Giá trị mới', 'Đơn vị')):
            ttk.Label(protection_panel, text=title, style='Muted.TLabel').grid(row=0, column=col, padx=4, pady=(0, 10))
        self.protection_entries = []
        for row, (key, label, unit, _scale) in enumerate(PROTECTION_FIELDS, 1):
            ttk.Label(protection_panel, text=label).grid(row=row, column=0, sticky='w', pady=3)
            ttk.Label(protection_panel, textvariable=self.protection_active_labels[key], width=10).grid(row=row, column=1)
            ttk.Label(protection_panel, textvariable=self.protection_stored_labels[key], width=10).grid(row=row, column=2)
            entry = ttk.Entry(protection_panel, textvariable=self.protection_pending[key], width=11)
            entry.grid(row=row, column=3, padx=4, pady=3)
            self.protection_entries.append(entry)
            ttk.Label(protection_panel, text=unit).grid(row=row, column=4, sticky='w')
        ttk.Label(protection_panel, textvariable=self.protection_note, wraplength=555,
                  style='Muted.TLabel').grid(row=11, column=0, columnspan=5, sticky='w', pady=12)
        ttk.Label(protection_panel, text='UV < UV phục hồi < OV phục hồi < OV.\nNhiệt phục hồi < nhiệt cắt. Rò RMS và rò đỉnh là hai ngưỡng riêng.',
                  wraplength=555, style='Muted.TLabel').grid(row=12, column=0, columnspan=5, sticky='w', pady=6)
        protection_actions = ttk.Frame(protection_panel)
        protection_actions.grid(row=13, column=0, columnspan=5, sticky='w', pady=10)
        self.protection_save_button = ttk.Button(protection_actions, text='Lưu ngưỡng vào Flash', style='Primary.TButton', command=self.save_protection)
        self.protection_save_button.pack(side='left')
        self.protection_defaults_button = ttk.Button(protection_actions, text='Khôi phục ngưỡng mặc định', command=self.restore_protection)
        self.protection_defaults_button.pack(side='left', padx=6)
        def readings_table(parent, fields, name_width):
            frame = ttk.Frame(parent)
            frame.pack(fill='both', expand=True)
            table = ttk.Treeview(frame, columns=('name', 'value', 'unit'),
                                 show='headings', height=len(fields))
            for col, title, width in (('name', 'Đại lượng', name_width), ('value', 'Giá trị', 90), ('unit', 'Đơn vị', 58)):
                table.heading(col, text=title)
                table.column(col, width=width, anchor='e' if col == 'value' else 'w')
            scroll = ttk.Scrollbar(frame, orient='vertical', command=table.yview)
            table.configure(yscrollcommand=scroll.set)
            scroll.pack(side='right', fill='y')
            table.pack(side='left', fill='both', expand=True)
            table.tag_configure('alternate', background='#f8fafc')
            for index, (key, label, unit) in enumerate(fields):
                table.insert('', 'end', iid=key, values=(label, '—', unit),
                             tags=('alternate',) if index % 2 else ())
            return frame, table

        table_frame, self.table = readings_table(left, DISPLAY, 166)
        _, self.debug_table = readings_table(debug_panel, DEBUG_DISPLAY, 260)
        ttk.Label(debug_panel, text='MAX giữ đến Reset MCU. Đỉnh sau lọc, từ khi offset fast sẵn sàng.',
                  wraplength=555, style='Muted.TLabel').pack(anchor='w', pady=(10, 0))
        diagnostics = ttk.Frame(left)
        diagnostics.pack(side='bottom', fill='x', pady=(12, 0), before=table_frame)
        self.fault_label = ttk.Label(diagnostics, textvariable=self.fault_text,
                                     wraplength=360, style='Muted.TLabel')
        self.fault_label.pack(anchor='w')
        ttk.Label(diagnostics, textvariable=self.fault_hint, wraplength=360,
                  style='Muted.TLabel').pack(anchor='w', pady=(4, 0))
        ttk.Label(diagnostics, textvariable=self.fault_code,
                  style='Muted.TLabel').pack(anchor='w', pady=(4, 6))
        ttk.Label(diagnostics, textvariable=self.health, wraplength=360,
                  style='Muted.TLabel').pack(anchor='w')
        for col, title in enumerate(('Kênh', 'Đang chạy', 'Trong Flash', 'Hệ số mới')):
            ttk.Label(right, text=title, style='Muted.TLabel').grid(row=0, column=col, padx=4, pady=(0, 10))
        for row, (key, label, *_rest) in enumerate(CHANNELS, 1):
            ttk.Label(right, text=label).grid(row=row, column=0, sticky='w')
            ttk.Label(right, textvariable=self.active_labels[key], width=11).grid(row=row, column=1)
            ttk.Label(right, textvariable=self.stored_labels[key], width=11).grid(row=row, column=2)
            ttk.Entry(right, textvariable=self.pending[key], width=13).grid(row=row, column=3, padx=4, pady=2)
        ttk.Label(right, text='Gain: 0.25…4  |  Offset nhiệt độ: −20…20 °C',
                  style='Muted.TLabel').grid(row=6, column=0, columnspan=4, sticky='w', pady=6)
        ttk.Separator(right).grid(row=7, column=0, columnspan=4, sticky='ew', pady=8)
        ttk.Label(right, text='Tính hệ số mới từ số đo chuẩn', font=('Segoe UI', 10, 'bold')).grid(row=8, column=0, columnspan=4, sticky='w')
        self.compute_buttons = []
        for row, channel in enumerate(CHANNELS, 9):
            key, label, _measurement, unit, _mask = channel
            ttk.Label(right, text=f'{label} chuẩn').grid(row=row, column=0, sticky='w')
            ttk.Entry(right, textvariable=self.references[key], width=13).grid(row=row, column=1, pady=2)
            ttk.Label(right, text=unit).grid(row=row, column=2, sticky='w')
            button = ttk.Button(right, text='Tính hệ số', style='Compact.TButton', command=lambda c=channel: self.compute(c))
            button.grid(row=row, column=3, pady=2)
            self.compute_buttons.append(button)
        ttk.Label(right, text='Gain mới = gain đang chạy × chuẩn / số đo.\nOffset mới = offset đang chạy + nhiệt độ chuẩn − số đo.',
                  wraplength=550, style='Muted.TLabel').grid(row=14, column=0, columnspan=4, sticky='w', pady=8)
        calib_actions = ttk.Frame(right)
        calib_actions.grid(row=15, column=0, columnspan=4, sticky='w', pady=(8, 0))
        self.save_button = ttk.Button(calib_actions, text='Lưu hệ số mới vào Flash', style='Primary.TButton', command=self.save)
        self.save_button.pack(side='left')
        self.defaults_button = ttk.Button(calib_actions, text='Lưu mặc định', command=self.restore)
        self.defaults_button.pack(side='left', padx=6)
        self.reset_button = ttk.Button(calib_actions, text='Reset MCU / áp dụng', command=self.reset)
        self.reset_button.pack(side='left')
        ttk.Separator(shell).pack(fill='x', pady=(0, 14))
        actions = ttk.Frame(shell)
        actions.pack(fill='x')
        ttk.Button(actions, text='Nhập JSON', command=self.load_json).pack(side='right', padx=4)
        ttk.Button(actions, text='Xuất JSON', command=self.export_json).pack(side='right')
        ttk.Label(shell, text='Lưu thay đổi sẽ mở relay và dừng đo đến khi reset. Calib chỉ bù sai số; mặc định gain = 1, offset = 0.',
                  wraplength=1060, style='Muted.TLabel').pack(anchor='w', pady=(14, 6))
        ttk.Label(shell, textvariable=self.status, wraplength=1060, style='Status.TLabel').pack(anchor='w')
        self.refresh_ports()
        self.update_buttons()
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.drain_id = root.after(100, self.drain)
        self.poll_id = root.after(500, self.poll)

    def refresh_ports(self):
        from serial.tools import list_ports
        ports = [p.device for p in list_ports.comports()]
        self.ports['values'] = ports
        if self.port.get() not in ports:
            self.port.set(ports[0] if ports else '')

    def update_buttons(self):
        def configure(widget, **options):
            changed = {key: value for key, value in options.items()
                       if str(widget.cget(key)) != str(value)}
            if changed:
                widget.configure(**changed)

        connected = self.client is not None
        configure(self.connect_button, text='Ngắt kết nối' if connected else 'Kết nối', state='disabled' if self.foreground_busy else 'normal')
        configure(self.ports, state='disabled' if connected or self.foreground_busy else 'readonly')
        configure(self.refresh_button, state='disabled' if connected or self.foreground_busy else 'normal')
        for b in (self.read_button, self.save_button, self.defaults_button, self.reset_button, *self.compute_buttons):
            configure(b, state='normal' if connected and not self.foreground_busy else 'disabled')
        protection_enabled = connected and not self.foreground_busy and self.info and self.info.supports_protection
        for widget in (self.protection_save_button, self.protection_defaults_button, *self.protection_entries):
            configure(widget, state='normal' if protection_enabled else 'disabled')

    def run(self, function, success, quiet=False, background=False):
        # One worker serializes UART. A user command can queue behind a poll;
        # only user commands lock the controls, including while queued.
        if self.closing or (self.busy and (background or self.foreground_busy)):
            return
        self.busy = True
        if not background:
            self.foreground_busy = True
        self.update_buttons()
        future = self.executor.submit(function)
        future.add_done_callback(lambda f: self.results.put((f, success, quiet, background)))

    def drain(self):
        if self.closing:
            return
        try:
            while True:
                future, success, quiet, background = self.results.get_nowait()
                if not background:
                    self.foreground_busy = False
                self.busy = self.foreground_busy
                try:
                    result = future.result()
                    self.failures = 0
                    success(result)
                except Exception as exc:
                    self.failures += 1
                    self.meas_time = 0
                    self.status.set(str(exc))
                    if self.failures >= 3:
                        self.auto.set(False)
                    if not quiet:
                        messagebox.showerror('Không hoàn thành', str(exc), parent=self.root)
                self.update_buttons()
        except queue.Empty:
            pass
        self.drain_id = self.root.after(100, self.drain)

    @staticmethod
    def read_measurement(client, info):
        meas = client.measurement()
        if info.supports_leak_max:
            meas.update(client.leakage_maxima())
        if info.supports_leak_count_max:
            meas['leakage_over_count_max'] = client.leakage_over_count_max()
        return meas

    @staticmethod
    def snapshot(client):
        info = client.info()
        protection = client.protection() if info.supports_protection else None
        stored_protection = client.stored_protection() if info.supports_protection else None
        return info, client.active(), client.stored(), App.read_measurement(client, info), protection, stored_protection

    def toggle_connection(self):
        if self.client:
            client = self.client
            def disconnected(_):
                self.client = None
                self.meas = self.info = None
                self.meas_time = 0
                self.board.set('Đã ngắt kết nối. Số đo trên bảng là dữ liệu cũ.')
                self.status.set('Chưa kết nối')
            self.run(client.close, disconnected)
            return
        if not self.port.get():
            messagebox.showinfo('Cổng COM', 'Chưa tìm thấy cổng. Cắm USB–UART rồi bấm Tìm cổng.', parent=self.root)
            return
        port = self.port.get()
        def connect():
            client = Client(port)
            try:
                return client, self.snapshot(client)
            except Exception:
                client.close()
                raise
        def connected(result):
            self.client, values = result
            self.display_snapshot(values, fill_pending=True)
            self.status.set(f'Đã kết nối {port} — 115200 8N1')
        self.run(connect, connected)

    def display_snapshot(self, values, fill_pending=False):
        self.info, self.active, stored, meas, self.active_protection, stored_protection = values
        source = 'Flash' if self.info.flags & 1 else 'mặc định (chưa bù sai số)'
        service = ' | ĐÃ DỪNG ĐO — reset để áp dụng và chạy lại' if self.info.reset_required else ''
        self.board.set(f'FW {self.info.major}.{self.info.minor} | Đang dùng: {source} | Bản lưu #{self.info.sequence} | RX lỗi: {self.info.rx_errors}{service}')
        for key in self.pending:
            self.active_labels[key].set(f'{getattr(self.active, key):.7g}')
            self.stored_labels[key].set(f'{getattr(stored, key):.7g}' if stored else 'Chưa có')
            if fill_pending:
                self.pending[key].set(repr(getattr(stored or self.active, key)))
        self.display_measurement(meas)
        for key, _label, _unit, scale in PROTECTION_FIELDS:
            active_value = getattr(self.active_protection, key) * scale if self.active_protection else None
            stored_value = getattr(stored_protection, key) * scale if stored_protection else None
            self.protection_active_labels[key].set(f'{active_value:.7g}' if active_value is not None else '—')
            self.protection_stored_labels[key].set(f'{stored_value:.7g}' if stored_value is not None else 'Chưa có')
            if fill_pending and self.active_protection:
                self.protection_pending[key].set(protection_text(getattr(stored_protection or self.active_protection, key), scale))
        self.protection_note.set('Firmware này chưa hỗ trợ cấu hình Protection.' if not self.info.supports_protection else
                                ('Flash chưa có ngưỡng hợp lệ; sau reset dùng mặc định. Bộ hiện tại ở cột Đang chạy.' if stored_protection is None else
                                 'Ngưỡng trong Flash được áp dụng sau reset. Lưu ngưỡng giữ nguyên Calib.'))
        self.update_buttons()

    def display_measurement(self, measurement):
        self.meas = measurement
        self.meas_time = time.monotonic()
        for table, fields in ((self.table, DISPLAY), (self.debug_table, DEBUG_DISPLAY)):
            for key, _label, _unit in fields:
                v = measurement.get(key)
                if v is None:
                    text = 'Chưa hỗ trợ'
                elif key in ('ileak_peak_max', 'leakage_over_count_max') and not measurement.get('fast_ready'):
                    text = 'Đang học offset'
                else:
                    if key in ('ileak', 'ileak_peak_max', 'ileak_rms_max'):
                        v *= 1000.0  # MCU wire unit is A; UI uses mA.
                    text = f'{v:.6g}' if math.isfinite(v) else 'INVALID'
                if table.set(key, 'value') != text:
                    table.set(key, 'value', text)
        prefix = 'SỐ ĐO ĐÃ DỪNG' if self.info and self.info.reset_required else ('Đã có cửa sổ đo' if measurement['ready'] else 'Đang khởi động')
        fault_text, hint = fault_description(measurement['faults'])
        code = f"Mã lỗi bảo vệ: 0x{measurement['faults']:08X}"
        for variable, text in ((self.fault_text, fault_text),
                               (self.fault_hint, hint), (self.fault_code, code)):
            if variable.get() != text:
                variable.set(text)
        fault_style = 'Fault.TLabel' if measurement['faults'] else 'Muted.TLabel'
        if self.fault_label.cget('style') != fault_style:
            self.fault_label.configure(style=fault_style)
        fast_leak = 'Đã kích hoạt' if measurement['leak_trip'] else 'Chưa kích hoạt'
        temperature = 'Có lỗi' if measurement['temp_fault'] else 'Không báo lỗi'
        health = (f"{prefix}\nADC chạm biên đo: {adc_description(measurement['clipped'])}"
                  f"\nCờ dòng rò nhanh: {fast_leak}\nCảm biến nhiệt: {temperature}")
        if self.health.get() != health:
            self.health.set(health)

    def read_all(self):
        if self.client:
            self.run(lambda: self.snapshot(self.client), self.display_snapshot)

    def poll(self):
        if self.closing:
            return
        if self.client and self.auto.get() and not self.busy:
            def read():
                info = self.client.info()
                return info, self.read_measurement(self.client, info)
            def show(values):
                previous = self.info
                self.info = values[0]
                self.display_measurement(values[1])
                if previous and (previous.flags, previous.sequence) != (self.info.flags, self.info.sequence):
                    self.run(lambda: self.snapshot(self.client), self.display_snapshot,
                             quiet=True, background=True)
            self.run(read, show, quiet=True, background=True)
        self.poll_id = self.root.after(500, self.poll)

    def compute(self, channel):
        try:
            key, _label, measurement, _unit, mask = channel
            if (self.meas is None or self.info is None or self.info.reset_required
                    or not self.meas['ready'] or time.monotonic() - self.meas_time > 2):
                raise ValueError('Cần số đo mới, MCU đang đo và đã khởi động xong.')
            if self.meas['clipped'] & mask or (key == 'temperature_offset_c' and self.meas['temp_fault']):
                raise ValueError('Kênh đang clipping hoặc lỗi cảm biến; không thể tính Calib.')
            reference = float(self.references[key].get())
            if key == 'leakage_gain':
                reference /= 1000.0  # reference entry is mA, wire measurement is A.
            value = corrected_value(getattr(self.active, key), self.meas[measurement],
                                    reference, key == 'temperature_offset_c')
            self.pending[key].set(f'{value:.9g}')
            self.status.set('Đã tính hệ số vào ô Hệ số mới. Bấm Lưu để ghi Flash.')
        except (ValueError, KeyError) as exc:
            messagebox.showerror('Không tính được', str(exc), parent=self.root)

    def pending_params(self):
        return Params(**{k: float(v.get()) for k, v in self.pending.items()}).validate()

    def pending_protection(self):
        values = {key: float(self.protection_pending[key].get()) / scale
                  for key, _, _, scale in PROTECTION_FIELDS}
        hold = values['recover_hold_ms']
        if not math.isfinite(hold) or not hold.is_integer():
            raise ValueError('Thời gian phục hồi phải là số nguyên ms.')
        values['recover_hold_ms'] = int(hold)
        params = ProtectionParams(**values).validate()
        params.pack()
        return params

    def save_protection(self):
        try:
            params = self.pending_protection()
        except ValueError as exc:
            messagebox.showerror('Ngưỡng không hợp lệ', str(exc), parent=self.root)
            return
        details = '\n'.join(f'{label}: {getattr(params, key) * scale:.9g} {unit}'
                            for key, label, unit, scale in PROTECTION_FIELDS)
        if not messagebox.askyesno('Lưu ngưỡng Protection?', details +
                                  '\n\nMCU mở relay và dừng đo khi có thay đổi. Reset để áp dụng.\nGiữ nguyên hệ số Calib đã lưu.', parent=self.root):
            return
        def write():
            status, _flags = self.client.save_protection(params)
            return status, self.snapshot(self.client)
        def done(result):
            self.display_snapshot(result[1])
            self.status.set(f'{result[0].name}: đã đọc lại và xác nhận ngưỡng. ' +
                            ('Bấm Reset MCU để áp dụng.' if self.info.reset_required else 'Không có thay đổi.'))
        self.run(write, done)

    def restore_protection(self):
        if not messagebox.askyesno('Khôi phục ngưỡng mặc định?',
                                  'Lưu ngưỡng mặc định ban đầu; giữ nguyên Calib.\nMCU mở relay và dừng đo khi có thay đổi. Reset để áp dụng.', parent=self.root):
            return
        def write():
            self.client.protection_defaults()
            return self.snapshot(self.client)
        def done(values):
            self.display_snapshot(values, fill_pending=True)
            self.status.set('Đã xác nhận ngưỡng mặc định trong Flash. ' +
                            ('Reset MCU để áp dụng.' if self.info.reset_required else 'Không có thay đổi.'))
        self.run(write, done)

    def save(self):
        try:
            params = self.pending_params()
        except ValueError as exc:
            messagebox.showerror('Hệ số không hợp lệ', str(exc), parent=self.root)
            return
        details = '\n'.join(f'{k}: {v:.9g}' for k, v in asdict(params).items())
        if not messagebox.askyesno('Lưu Calib vào Flash?', details + '\n\nNếu hệ số thay đổi: MCU mở relay và dừng đo đến khi reset.\nTiếp tục ghi page Flash Calib?', parent=self.root):
            return
        def write():
            status, _flags = self.client.save(params)
            return status, self.snapshot(self.client)
        def done(result):
            self.display_snapshot(result[1])
            self.status.set(f'{result[0].name}: đã đọc lại Flash và xác nhận. ' + ('Bấm Reset MCU để áp dụng.' if self.info.reset_required else 'Hệ số không thay đổi.'))
        self.run(write, done)

    def restore(self):
        if not messagebox.askyesno('Lưu mặc định?', 'Lưu gain = 1, offset nhiệt độ = 0.\nNếu thay đổi, MCU mở relay và dừng đo đến khi reset.', parent=self.root):
            return
        def write():
            self.client.defaults()
            return self.snapshot(self.client)
        def done(values):
            self.display_snapshot(values, fill_pending=True)
            self.status.set('Đã xác nhận mặc định trong Flash. ' + ('Reset MCU để áp dụng.' if self.info.reset_required else 'Không có thay đổi.'))
        self.run(write, done)

    def reset(self):
        if not messagebox.askyesno('Reset MCU?', 'MCU khởi động lại và áp dụng hệ số trong Flash.\nChức năng bảo vệ/relay sẽ chạy theo trình tự khởi động.', parent=self.root):
            return
        def reboot():
            self.client.reset()
            return self.snapshot(self.client)
        def done(values):
            self.display_snapshot(values, fill_pending=True)
            self.status.set('MCU đã reset; đã đọc lại hệ số đang áp dụng.')
        self.run(reboot, done)

    def load_json(self):
        path = filedialog.askopenfilename(filetypes=[('Calib JSON', '*.json')], parent=self.root)
        if path:
            try:
                params = Params(**json.loads(Path(path).read_text(encoding='utf-8'))).validate()
                for key, value in asdict(params).items():
                    self.pending[key].set(repr(value))
                self.status.set('Đã nhập hệ số mới từ JSON; chưa ghi MCU.')
            except Exception as exc:
                messagebox.showerror('JSON không hợp lệ', str(exc), parent=self.root)

    def export_json(self):
        try:
            params = self.pending_params()
            path = filedialog.asksaveasfilename(defaultextension='.json', filetypes=[('Calib JSON', '*.json')], parent=self.root)
            if path:
                Path(path).write_text(json.dumps(asdict(params), indent=2) + '\n', encoding='utf-8')
        except Exception as exc:
            messagebox.showerror('Không xuất được', str(exc), parent=self.root)

    def close(self):
        if self.busy:
            messagebox.showinfo('Đang giao tiếp', 'Chờ giao dịch hiện tại hoàn thành rồi đóng tool.', parent=self.root)
            return
        self.closing = True
        self.root.after_cancel(self.drain_id)
        self.root.after_cancel(self.poll_id)
        if self.client:
            self.executor.submit(self.client.close)
        self.executor.shutdown(wait=False)
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--smoke-test', action='store_true', help='Initialize all Tk widgets then exit')
    args = parser.parse_args()
    root = tk.Tk()
    if args.smoke_test:
        root.withdraw()
    app = App(root)
    if args.smoke_test:
        root.update_idletasks()
        assert len(app.table.get_children()) == 9
        assert len(app.debug_table.get_children()) == 9
        assert len(app.pending) == 5
        assert len(app.protection_pending) == 10
        app.close()
    else:
        root.mainloop()


if __name__ == '__main__':
    main()
