import importlib.util
from pathlib import Path
import tempfile
import threading
import tomllib
import unittest
from unittest.mock import patch

BASE = Path(__file__).resolve().parents[1]
def load(name):
    spec = importlib.util.spec_from_file_location(name, BASE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
monitor = load('monitor_wokwi')
generator = load('preparar_wokwi')


class MonitorTests(unittest.TestCase):
    def test_serial_chunks_keep_multibyte_characters_and_equipment_label(self):
        stop = threading.Event()
        chunks = iter([b'[HTTP] C\xc3', b'\xb3digo: 201\r\n[JSON] {"ph":7}\n', b''])
        class Stream:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, count):
                chunk = next(chunks)
                if not chunk: stop.set()
                return chunk
        events = []
        with patch.object(monitor.serial, 'serial_for_url', return_value=Stream()) as connect:
            monitor.acompanhar('esp32-002', 4002, stop, lambda *args: events.append(args))
        connect.assert_called_once_with('rfc2217://127.0.0.1:4002', baudrate=115200, timeout=.5)
        lines = [event for event in events if event[1] == 'serial']
        self.assertEqual(lines, [('esp32-002', 'serial', '[HTTP] Código: 201'),
                                 ('esp32-002', 'serial', '[JSON] {"ph":7}')])

    def test_serial_configuration_is_unique_preserves_other_settings_and_repeatable(self):
        with tempfile.TemporaryDirectory() as directory:
            projects = generator.preparar(4, Path(directory))
            ports = [tomllib.loads((p / 'wokwi.toml').read_text())['wokwi']['rfc2217ServerPort'] for p in projects]
            self.assertEqual(ports, [4001, 4002, 4003, 4004])
            path = projects[0] / 'wokwi.toml'
            path.write_text('[wokwi]\nversion = 1\nfirmware = "custom.bin"\nrfc2217ServerPort = 9999\n[[net.forward]]\nfrom = "localhost:8180"\nto = "target:80"\n')
            generator.configurar_serial(projects[0], 4001)
            parsed = tomllib.loads(path.read_text())
            self.assertEqual(parsed['wokwi']['firmware'], 'custom.bin')
            self.assertEqual(parsed['net']['forward'][0]['from'], 'localhost:8180')
            expected = path.read_bytes()
            generator.configurar_serial(projects[0], 4001)
            self.assertEqual(path.read_bytes(), expected)


if __name__ == '__main__': unittest.main()
