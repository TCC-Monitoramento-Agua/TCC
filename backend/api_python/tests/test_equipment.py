import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import main


class EquipmentTests(unittest.TestCase):
    def setUp(self):
        self.connection = MagicMock()
        self.cursor = self.connection.cursor.return_value
        self.connection_patch = patch.object(main, 'get_connection', return_value=self.connection)
        self.schema_patch = patch.object(main, '_db_schema_initialized', True)
        self.connection_patch.start()
        self.schema_patch.start()
        self.addCleanup(self.connection_patch.stop)
        self.addCleanup(self.schema_patch.stop)
        self.client = main.app.test_client()
        self.payload = {'ph': 7.5, 'turbidez': 2.3, 'temperatura': 22.5}

    def test_equipment_is_saved_and_returned(self):
        self.cursor.lastrowid = 123
        response = self.client.post('/leituras', json={**self.payload, 'equipamento_id': 'sim-001'})
        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.get_json()['equipamento_id'], 'sim-001')
        self.assertEqual(self.cursor.execute.call_args.args[1][0], 'sim-001')
        self.connection.commit.assert_called_once()

    def test_old_clients_without_equipment_still_work(self):
        self.cursor.lastrowid = 124
        response = self.client.post('/leituras', json=self.payload)
        self.assertEqual(response.status_code, 201)
        self.assertIsNone(response.get_json()['equipamento_id'])
        self.assertIsNone(self.cursor.execute.call_args.args[1][0])

    def test_invalid_equipment_is_rejected_before_insert(self):
        for value in ['', 123, 'a' * 65, '<script>']:
            with self.subTest(value=value):
                response = self.client.post('/leituras', json={**self.payload, 'equipamento_id': value})
                self.assertEqual(response.status_code, 400)
        self.cursor.execute.assert_not_called()

    def test_non_object_json_is_rejected(self):
        response = self.client.post('/leituras', json=[1, 2])
        self.assertEqual(response.status_code, 400)

    def test_filter_is_parameterized(self):
        self.cursor.fetchall.return_value = []
        response = self.client.get('/leituras?equipamento_id=sim-001&limite=5')
        self.assertEqual(response.status_code, 200)
        query, params = self.cursor.execute.call_args.args
        self.assertIn('WHERE equipamento_id = %s', query)
        self.assertEqual(params, ('sim-001', 5))

    def test_migration_runs_only_when_column_missing(self):
        self.cursor.fetchone.return_value = None
        main.init_db()
        self.assertTrue(any('ALTER TABLE' in call.args[0] for call in self.cursor.execute.call_args_list))
        self.cursor.reset_mock()
        self.cursor.fetchone.return_value = ('equipamento_id',)
        main.init_db()
        self.assertFalse(any('ALTER TABLE' in call.args[0] for call in self.cursor.execute.call_args_list))


if __name__ == '__main__':
    unittest.main()
