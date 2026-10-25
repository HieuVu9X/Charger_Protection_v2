"""Explicit GUI checks: python Tools/tests/gui_smoke.py (needs a desktop).
Exercises actual GUI actions against the compiled MCU simulator, no board.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import time
import gc
from threading import Event
import tkinter as tk
import unittest
from unittest.mock import patch
from charger_tool import App, CHANNELS
from protocol import Client, Params
import test_protocol as protocol_tests


class GuiTests(unittest.TestCase):
    def test_leakage_maxima_from_mcu_and_legacy_fallback(self):
        self.assertEqual(self.app.debug_table.set('leakage_over_count_max','value'),'7')
        self.assertEqual(self.app.debug_table.item('leakage_over_count_max','values')[2],'mẫu')
        self.assertEqual(self.app.debug_table.set('ileak_peak_max','value'),'50')
        self.assertEqual(self.app.debug_table.set('ileak_rms_max','value'),'34')
        self.assertEqual(self.app.table.set('ileak','value'),'12')
        self.app.read_all(); self.wait_worker()
        self.assertEqual(self.app.debug_table.set('ileak_peak_max','value'),'50')
        values=dict(self.app.meas,fast_ready=0)
        self.app.display_measurement(values)
        self.assertEqual(self.app.debug_table.set('ileak_peak_max','value'),'Đang học offset')
        self.assertEqual(self.app.debug_table.set('leakage_over_count_max','value'),'Đang học offset')
        from dataclasses import replace
        info=replace(self.app.info,minor=6,capabilities=0xfff)
        with patch.object(self.app.client,'info',return_value=info), \
                patch.object(self.app.client,'leakage_over_count_max') as counter:
            self.app.display_snapshot(self.app.snapshot(self.app.client))
            counter.assert_not_called()
        self.assertEqual(self.app.debug_table.set('ileak_peak_max','value'),'50')
        self.assertEqual(self.app.debug_table.set('leakage_over_count_max','value'),'Chưa hỗ trợ')
        info=replace(self.app.info,capabilities=0x7ff)
        with patch.object(self.app.client,'info',return_value=info), \
                patch.object(self.app.client,'leakage_maxima') as maxima, \
                patch.object(self.app.client,'leakage_over_count_max') as counter:
            self.app.display_snapshot(self.app.snapshot(self.app.client))
            maxima.assert_not_called()
            counter.assert_not_called()
        self.assertEqual(self.app.debug_table.set('ileak_peak_max','value'),'Chưa hỗ trợ')
        self.assertEqual(self.app.debug_table.set('ileak_rms_max','value'),'Chưa hỗ trợ')

    @classmethod
    def setUpClass(cls):
        protocol_tests.McuTests.setUpClass()

    @classmethod
    def tearDownClass(cls):
        protocol_tests.McuTests.tearDownClass()

    def setUp(self):
        self.root = tk.Tk()
        self.root.withdraw()
        self.app = App(self.root)
        self.app.auto.set(False)
        self.sim = protocol_tests.Simulator(protocol_tests.McuTests.exe)
        self.app.client = Client(transport=self.sim)
        self.app.display_snapshot(self.app.snapshot(self.app.client), fill_pending=True)
        self.root.update_idletasks()

    def tearDown(self):
        self.app.client = None
        self.app.close()
        self.app.executor.shutdown(wait=True)
        self.sim.close()
        gc.collect()  # Collect retired Tk widget cycles on the Tk/main thread.

    def wait_worker(self):
        deadline = time.monotonic() + 6
        while self.app.busy and time.monotonic() < deadline:
            self.root.update()
            time.sleep(.01)
        self.assertFalse(self.app.busy, 'GUI worker did not complete')

    def test_units_reference_and_validation(self):
        self.assertAlmostEqual(float(self.app.table.item('ileak', 'values')[1]), 12)
        self.assertEqual(self.app.table.item('ileak', 'values')[2], 'mA')
        self.assertEqual(self.app.debug_table.item('vin_adc_offset', 'values')[2], 'V')
        self.app.references['leakage_gain'].set('13.2')
        self.app.compute(CHANNELS[3])
        self.assertAlmostEqual(float(self.app.pending['leakage_gain'].get()), 1.1, places=6)
        self.app.references['temperature_offset_c'].set('27')
        self.app.compute(CHANNELS[4])
        self.assertEqual(float(self.app.pending['temperature_offset_c'].get()), 2)
        with patch('charger_tool.messagebox.showerror') as error:
            self.app.references['vout_gain'].set('220')
            self.app.compute(CHANNELS[1])  # simulator clips this channel
            error.assert_called_once()
        self.app.meas_time = 0
        with patch('charger_tool.messagebox.showerror') as error:
            self.app.compute(CHANNELS[0])
            error.assert_called_once()

    def test_gui_save_readback_and_reset(self):
        self.app.pending['vin_gain'].set('1.125')
        with patch('charger_tool.messagebox.askyesno', return_value=True), patch('charger_tool.messagebox.showerror') as error:
            self.app.save()
            self.wait_worker()
            error.assert_not_called()
        self.assertTrue(self.app.info.reset_required)
        self.assertIn('DỪNG ĐO', self.app.board.get())
        self.assertEqual(self.app.active.vin_gain, 1)
        self.assertEqual(self.app.stored_labels['vin_gain'].get(), '1.125')
        with patch('charger_tool.messagebox.askyesno', return_value=True), patch('charger_tool.messagebox.showerror') as error:
            self.app.reset()
            self.wait_worker()
            error.assert_not_called()
        self.assertFalse(self.app.info.reset_required)
        self.assertEqual(self.app.active.vin_gain, 1.125)
        self.assertEqual(self.sim.stats()[2], 1)

    def test_protection_units_save_reset_and_defaults(self):
        self.assertEqual(float(self.app.protection_pending['leak_a'].get()), 30)
        self.app.protection_pending['leak_a'].set('20')
        self.app.protection_pending['oc_a'].set('2')
        self.app.protection_pending['recover_hold_ms'].set('123')
        with patch('charger_tool.messagebox.askyesno', return_value=True), patch('charger_tool.messagebox.showerror') as error:
            self.app.save_protection(); self.wait_worker(); error.assert_not_called()
        self.assertTrue(self.app.info.reset_required)
        self.assertAlmostEqual(self.app.active_protection.oc_a, 2.8, places=6)
        self.assertEqual(float(self.app.protection_stored_labels['leak_a'].get()), 20)
        with patch('charger_tool.messagebox.askyesno', return_value=True):
            self.app.reset(); self.wait_worker()
        self.assertEqual(self.app.active_protection.oc_a, 2)
        self.assertEqual(self.app.active_protection.recover_hold_ms, 123)
        self.assertAlmostEqual(self.app.active_protection.leak_a, .02)
        with patch('charger_tool.messagebox.askyesno', return_value=True):
            self.app.restore_protection(); self.wait_worker()
        self.assertEqual(float(self.app.protection_pending['leak_a'].get()), 30)
        self.assertEqual(self.app.active_protection.oc_a, 2)  # still pending reset
        erases = self.sim.stats()[0]
        with patch('charger_tool.messagebox.askyesno', return_value=True):
            self.app.save_protection(); self.wait_worker()
        self.assertEqual(self.sim.stats()[0], erases)  # display rounding preserves float32
        self.app.protection_pending['recover_hold_ms'].set('1.5')
        with patch('charger_tool.messagebox.showerror') as error:
            self.app.save_protection(); error.assert_called_once()

    def test_old_firmware_disables_protection_controls(self):
        from dataclasses import replace
        info = replace(self.app.client.info(), capabilities=0x7f)
        with patch.object(self.app.client, 'info', return_value=info), patch.object(self.app.client, 'protection') as read:
            self.app.display_snapshot(self.app.snapshot(self.app.client), fill_pending=True)
            read.assert_not_called()
        self.assertIn('disabled', self.app.protection_save_button.state())
        self.assertIn('chưa hỗ trợ', self.app.protection_note.get())

    def test_background_poll_keeps_controls_enabled_and_does_not_redraw_unchanged_values(self):
        started, release = Event(), Event()
        original_info = self.app.client.info
        def blocked_info():
            started.set()
            if not release.wait(3):
                raise TimeoutError('Test did not release background read')
            return original_info()
        controls = (self.app.connect_button, self.app.read_button, self.app.save_button,
                    self.app.protection_save_button, *self.app.protection_entries,
                    *self.app.compute_buttons)
        self.app.protection_pending['oc_a'].set('2.25')
        with patch.object(self.app.client, 'info', side_effect=blocked_info), \
                patch.object(self.app.table, 'set', wraps=self.app.table.set) as cells, \
                patch.object(self.app.debug_table, 'set', wraps=self.app.debug_table.set) as debug_cells:
            self.root.after_cancel(self.app.poll_id)
            self.app.auto.set(True)
            self.app.poll()
            self.app.auto.set(False)
            try:
                self.assertTrue(started.wait(1))
                self.assertTrue(self.app.busy)
                for widget in controls:
                    self.assertNotIn('disabled', widget.state(), str(widget))
                self.assertEqual(self.app.protection_pending['oc_a'].get(), '2.25')
            finally:
                release.set()
                self.wait_worker()
            self.assertFalse(any(len(call.args) == 3 for call in cells.call_args_list))
            self.assertFalse(any(len(call.args) == 3 for call in debug_cells.call_args_list))
        for widget in controls:
            self.assertNotIn('disabled', widget.state())

    def test_save_clicked_during_poll_is_serialized_and_not_lost(self):
        started, release = Event(), Event()
        original_measurement = self.app.client.measurement
        calls = []
        def blocked_measurement():
            calls.append('read')
            started.set()
            if not release.wait(3):
                raise TimeoutError('Test did not release background read')
            return original_measurement()
        original_save = self.app.client.save
        def save(params):
            calls.append('save')
            return original_save(params)
        with patch.object(self.app.client, 'measurement', side_effect=blocked_measurement), \
                patch.object(self.app.client, 'save', side_effect=save), \
                patch('charger_tool.messagebox.askyesno', return_value=True), \
                patch('charger_tool.messagebox.showerror') as errors:
            self.root.after_cancel(self.app.poll_id)
            self.app.auto.set(True)
            self.app.poll()
            self.app.auto.set(False)
            try:
                self.assertTrue(started.wait(1))
                self.app.pending['vin_gain'].set('1.125')
                self.app.save()
                self.assertIn('disabled', self.app.save_button.state())
                self.assertEqual(self.sim.stats()[0], 0)
            finally:
                release.set()
                self.wait_worker()
            errors.assert_not_called()
        self.assertEqual(calls, ['read', 'save', 'read'])
        self.assertEqual(self.sim.stats()[0], 1)
        self.assertEqual(self.app.stored_labels['vin_gain'].get(), '1.125')
        self.assertNotIn('disabled', self.app.save_button.state())



if __name__ == '__main__':
    unittest.main()
