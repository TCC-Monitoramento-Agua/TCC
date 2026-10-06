"""Exercita a ordenação e o LIMIT da consulta usando um banco em memória."""
import sqlite3
import sys
import unittest
from datetime import datetime
from pathlib import Path
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import main


class ReadingOrderTests(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(':memory:')
        self.addCleanup(self.db.close)
        self.db.row_factory = sqlite3.Row
        self.db.execute('CREATE TABLE leituras (id INTEGER PRIMARY KEY, equipamento_id TEXT, ph REAL, turbidez REAL, temperatura REAL, orp REAL, data_hora TEXT)')
        self.cursor = self.db.cursor()
        connection = MagicMock()
        cursor = connection.cursor.return_value
        cursor.execute.side_effect = lambda query, params: self.cursor.execute(query.replace('%s', '?'), params)
        def fetch():
            rows = [dict(row) for row in self.cursor.fetchall()]
            for row in rows:
                row['data_hora'] = datetime.fromisoformat(row['data_hora'])
            return rows
        cursor.fetchall.side_effect = fetch
        self.connection_patch = patch.object(main, 'get_connection', return_value=connection)
        self.schema_patch = patch.object(main, '_db_schema_initialized', True)
        self.connection_patch.start()
        self.schema_patch.start()
        self.addCleanup(self.connection_patch.stop)
        self.addCleanup(self.schema_patch.stop)
        self.client = main.app.test_client()

    def insert(self, id, equipment, timestamp):
        self.db.execute('INSERT INTO leituras VALUES (?, ?, 7, 2, 25, 300, ?)', (id, equipment, timestamp))

    def test_newest_measurement_is_selected_before_limit_even_with_lower_id(self):
        self.insert(1, 'esp32-001', '2026-10-06 19:00:00')
        for id in range(2, 14):
            self.insert(id, 'esp32-002', '2026-10-06 18:00:00')
        response = self.client.get('/leituras?limite=10')
        self.assertEqual(response.status_code, 200)
        rows = response.get_json()['leituras']
        self.assertEqual(len(rows), 10)
        self.assertEqual(rows[0]['id'], 1)
        self.assertEqual(rows[0]['data_hora'], '2026-10-06T19:00:00-03:00')
        self.assertEqual(rows[1]['id'], 13)

    def test_equal_timestamps_use_highest_id_as_tiebreaker(self):
        for id in (1, 2, 3):
            self.insert(id, 'esp32-001', '2026-10-06 19:00:00')
        rows = self.client.get('/leituras').get_json()['leituras']
        self.assertEqual([row['id'] for row in rows], [3, 2, 1])

    def test_equipment_filter_keeps_chronological_order(self):
        self.insert(1, 'esp32-001', '2026-10-06 19:00:00')
        self.insert(2, 'esp32-001', '2026-10-06 18:00:00')
        self.insert(3, 'esp32-002', '2026-10-06 20:00:00')
        rows = self.client.get('/leituras?equipamento_id=esp32-001').get_json()['leituras']
        self.assertEqual([row['id'] for row in rows], [1, 2])


if __name__ == '__main__':
    unittest.main()
