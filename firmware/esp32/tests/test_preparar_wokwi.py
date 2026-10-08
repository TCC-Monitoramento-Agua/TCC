import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('preparar_wokwi', Path(__file__).resolve().parents[1] / 'preparar_wokwi.py')
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.destino = Path(self.temp.name) / 'instancias'

    def test_expansion_preserves_firmware_and_generates_unique_macs(self):
        first = generator.preparar(2, self.destino)
        source = first[0] / 'src/main.cpp'
        source.write_text(source.read_text() + '\n// delay personalizado\n')
        expected = source.read_bytes()
        for n in (4, 8, 16, 32):
            projects = generator.preparar(n, self.destino)
            self.assertEqual(len(projects), n)
            self.assertEqual(source.read_bytes(), expected)
            macs = set()
            for i, project in enumerate(projects, 1):
                diagram = json.loads((project / 'diagram.json').read_text())
                mac = generator.placa_esp32(diagram)['attrs']['macAddress']
                self.assertEqual(mac, f'02:00:00:00:00:{i:02x}')
                macs.add(mac)
            self.assertEqual(len(macs), n)

    def test_update_preserves_backup_and_assigns_four_start_offsets(self):
        projects = generator.preparar(4, self.destino)
        source = projects[0] / 'src/main.cpp'
        source.write_text(source.read_text() + '\n// ajuste local\n')
        original = source.read_bytes()
        generator.preparar(4, self.destino, atualizar_firmware=True)
        self.assertEqual(source.with_name('main.cpp.bak').read_bytes(), original)
        for i, project in enumerate(projects):
            self.assertIn(f'#define ATRASO_INICIAL_MS {i * 3000}\n', (project/'src/main.cpp').read_text())
        generator.preparar(4, self.destino, atualizar_firmware=True)
        self.assertFalse(source.with_name('main.cpp.bak.1').exists())

    def test_old_diagram_is_repaired_with_backup_and_sensor_settings_preserved(self):
        project = generator.preparar(2, self.destino)[0]
        file = project / 'diagram.json'
        circuit = json.loads(file.read_text())
        generator.placa_esp32(circuit)['attrs'].pop('macAddress')
        circuit['parts'][1]['attrs']['value'] = '5000'
        file.write_text(json.dumps(circuit))
        original = file.read_bytes()
        generator.preparar(2, self.destino)
        self.assertEqual((project / 'diagram.json.bak').read_bytes(), original)
        self.assertEqual(json.loads(file.read_text())['parts'][1]['attrs']['value'], '5000')
        generator.preparar(2, self.destino)
        self.assertFalse((project / 'diagram.json.bak.1').exists())

    def test_smaller_count_does_not_delete_existing_instances(self):
        generator.preparar(4, self.destino)
        generator.preparar(2, self.destino)
        self.assertTrue((self.destino / 'esp32-004/src/main.cpp').is_file())

    def test_unexpected_equipment_id_blocks_changes(self):
        project = generator.preparar(2, self.destino)[0]
        (project / 'src/main.cpp').write_text('// firmware de outro projeto')
        with self.assertRaises(ValueError):
            generator.preparar(4, self.destino)
        self.assertFalse((self.destino / 'esp32-003').exists())

    def test_add_and_open_only_new_instances(self):
        generator.preparar(2, self.destino)
        with patch('sys.argv', ['preparar_wokwi.py', '--saida', str(self.destino), '--adicionar', '2', '--abrir']), \
             patch.object(generator.shutil, 'which', return_value='code'), \
             patch.object(generator.subprocess, 'run') as run:
            self.assertEqual(generator.main(), 0)
        opened = [Path(call.args[0][-1]).name for call in run.call_args_list]
        self.assertEqual(opened, ['esp32-003', 'esp32-004'])

    def test_build_failure_prevents_opening_windows(self):
        with patch('sys.argv', ['preparar_wokwi.py', '--saida', str(self.destino), '--quantidade', '2', '--compilar', '--abrir']), \
             patch.object(generator.shutil, 'which', return_value='tool'), \
             patch.object(generator.subprocess, 'run', side_effect=generator.subprocess.CalledProcessError(1, 'pio')) as run:
            self.assertEqual(generator.main(), 1)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0], ['tool', 'run'])


if __name__ == '__main__':
    unittest.main()
