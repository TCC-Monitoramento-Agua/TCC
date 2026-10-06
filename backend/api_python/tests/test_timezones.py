import sys
import unittest
from datetime import datetime
from pathlib import Path
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import main


class TimezoneTests(unittest.TestCase):
    def test_naive_sensor_time_is_preserved(self):
        self.assertEqual(main.parse_data_hora('2026-10-06 16:19:29'),
                         '2026-10-06 16:19:29')

    def test_utc_time_is_converted_before_storage(self):
        self.assertEqual(main.parse_data_hora('2026-10-06T19:19:29+00:00'),
                         '2026-10-06 16:19:29')

    def test_missing_timestamp_uses_brasilia(self):
        with patch.object(main, 'datetime') as clock:
            clock.now.return_value = datetime(2026, 10, 6, 16, 19, 29)
            self.assertEqual(main.parse_data_hora(None), '2026-10-06 16:19:29')
            clock.now.assert_called_once_with(main.BRAZIL_TIMEZONE)

    def test_api_sends_local_datetime_with_explicit_offset(self):
        connection = MagicMock()
        connection.cursor.return_value.fetchall.return_value = [
            {'id': 1, 'ph': 7.5, 'turbidez': 2.3, 'temperatura': 22.5,
             'orp': 450.0, 'data_hora': datetime(2026, 10, 6, 16, 19, 29)}
        ]
        with patch.object(main, 'get_connection', return_value=connection), \
                patch.object(main, '_db_schema_initialized', True):
            response = main.app.test_client().get('/leituras')
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.get_json()['leituras'][0]['data_hora'],
                         '2026-10-06T16:19:29-03:00')


if __name__ == '__main__':
    unittest.main()
