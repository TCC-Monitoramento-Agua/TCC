import sys
import unittest
from datetime import datetime
from pathlib import Path
from unittest.mock import MagicMock, patch
import mysql.connector

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import main


class IdempotencyTests(unittest.TestCase):
    def setUp(self):
        self.conn = MagicMock()
        self.cursor = self.conn.cursor.return_value
        self.cursor.lastrowid = 42
        for patcher in (patch.object(main, 'get_connection', return_value=self.conn),
                        patch.object(main, '_db_schema_initialized', True)):
            patcher.start()
            self.addCleanup(patcher.stop)
        self.client = main.app.test_client()
        self.data = dict(equipamento_id='esp32-001', leitura_id='1b0c1010-a116-46b4-b3e9-447e5329af79',
                         ph=7.5, turbidez=2.3, temperatura=25, orp=None, data_hora='2026-10-07T12:00:00-03:00')

    def duplicate(self, ph=7.5):
        self.cursor.execute.side_effect = [mysql.connector.IntegrityError(errno=1062), None]
        self.cursor.fetchone.return_value = (42, ph, 2.3, 25, None, datetime(2026, 10, 7, 12))

    def test_new_reading_returns_receipt_with_original_uuid(self):
        r = self.client.post('/leituras', json=self.data)
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json['id'], 42)
        self.assertEqual(r.json['leitura_id'], self.data['leitura_id'])
        self.assertFalse(r.json['duplicada'])
        self.conn.commit.assert_called_once()

    def test_retry_returns_same_record_without_second_insert(self):
        self.duplicate()
        r = self.client.post('/leituras', json=self.data)
        self.assertEqual(r.status_code, 200)
        self.assertEqual(r.json['id'], 42)
        self.assertTrue(r.json['duplicada'])
        self.conn.commit.assert_not_called()
        self.assertEqual(self.cursor.execute.call_count, 2)

    def test_reused_uuid_with_changed_payload_is_conflict(self):
        self.duplicate(ph=8.5)
        r = self.client.post('/leituras', json=self.data)
        self.assertEqual(r.status_code, 409)
        self.conn.commit.assert_not_called()

    def test_invalid_uuid_or_missing_equipment_or_time_is_rejected(self):
        for field, value in [('leitura_id', 'invalid'), ('leitura_id', None),
                             ('equipamento_id', None), ('data_hora', None)]:
            with self.subTest(field=field):
                r = self.client.post('/leituras', json={**self.data, field: value})
                self.assertEqual(r.status_code, 400)
        self.cursor.execute.assert_not_called()

    def test_reused_uuid_with_changed_time_is_conflict(self):
        self.duplicate()
        r = self.client.post('/leituras', json={**self.data, 'data_hora': '2026-10-07 12:00:01'})
        self.assertEqual(r.status_code, 409)

    def test_uuid_is_canonicalized_before_insert(self):
        r = self.client.post('/leituras', json={**self.data, 'leitura_id': self.data['leitura_id'].upper()})
        self.assertEqual(r.status_code, 201)
        self.assertEqual(r.json['leitura_id'], self.data['leitura_id'])

    def test_old_client_remains_compatible_without_deduplication(self):
        data = {k: v for k, v in self.data.items() if k != 'leitura_id'}
        r = self.client.post('/leituras', json=data)
        self.assertEqual(r.status_code, 201)
        self.assertIsNone(r.json['leitura_id'])

    def test_failed_commit_is_not_acknowledged(self):
        self.conn.commit.side_effect = mysql.connector.Error('database unavailable')
        r = self.client.post('/leituras', json=self.data)
        self.assertEqual(r.status_code, 500)
        self.assertEqual(r.json['status'], 'erro')


if __name__ == '__main__':
    unittest.main()
