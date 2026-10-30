"""Integração com MySQL descartável; nunca aponta para o banco da aplicação.

Execute com TCC_TEST_MYSQL_PORT e TCC_TEST_MYSQL_PASSWORD configurados:
    python -m unittest discover -s tests -v
O servidor local deve conter o banco tcc_tests e o usuário tcc_test.
"""
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch
from uuid import uuid4

import mysql.connector

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.getenv("TCC_TEST_MYSQL_PORT"), "MySQL descartável não configurado")
class TdsIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binding = patch.dict(os.environ, {
            "DB_HOST": "127.0.0.1",
            "DB_PORT": os.environ["TCC_TEST_MYSQL_PORT"],
            "DB_USER": "tcc_test",
            "DB_PASSWORD": os.environ["TCC_TEST_MYSQL_PASSWORD"],
            "DB_NAME": "tcc_tests",
        })
        cls.binding.start()
        cls.addClassCleanup(cls.binding.stop)
        spec = importlib.util.spec_from_file_location("tds_api", ROOT / "backend/api_python/main.py")
        cls.api = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.api)
        cls.client = cls.api.app.test_client()

    def sql(self, query, params=()):
        with self.api.get_connection() as conn:
            with conn.cursor() as cursor:
                cursor.execute(query, params)
                rows = cursor.fetchall() if cursor.with_rows else []
                conn.commit()
                return rows

    def setUp(self):
        self.sql("DROP TABLE IF EXISTS leituras")
        self.api.init_db()
        self.api._db_schema_initialized = True
        self.payload = dict(equipamento_id="teste-tds", leitura_id=str(uuid4()),
                            ph=7.2, turbidez=2.3, temperatura=22.5, tds=450.0,
                            data_hora="2026-10-21T10:00:00-03:00")

    def post(self, payload):
        return self.client.post("/leituras", json=payload)

    def readings(self):
        response = self.client.get("/leituras?equipamento_id=teste-tds")
        self.assertEqual(response.status_code, 200)
        return response.get_json()["leituras"]

    def test_new_schema_and_repeated_initialization(self):
        self.api.init_db()
        columns = {row[0] for row in self.sql("SHOW COLUMNS FROM leituras")}
        self.assertIn("tds", columns)

    def test_tds_round_trip_and_conflict(self):
        self.assertEqual(self.post(self.payload).status_code, 201)
        self.assertEqual(self.readings()[0]["tds"], 450.0)
        duplicate = self.post(self.payload)
        self.assertEqual(duplicate.status_code, 200)
        self.assertTrue(duplicate.get_json()["duplicada"])
        self.assertEqual(len(self.readings()), 1)
        self.assertEqual(self.post(dict(self.payload, tds=451)).status_code, 409)

    def test_invalid_tds_rejected(self):
        for value in ("inválido", "NaN", "Infinity", [], {}):
            with self.subTest(value=value):
                self.assertEqual(self.post(dict(self.payload, tds=value)).status_code, 400)
        self.assertEqual(self.readings(), [])

    def test_tds_remains_optional(self):
        for value in (None, "missing"):
            payload = dict(self.payload, leitura_id=str(uuid4()), tds=value)
            if value == "missing":
                payload.pop("tds")
            self.assertEqual(self.post(payload).status_code, 201)
        self.assertTrue(all(row["tds"] is None for row in self.readings()))

    def test_relative_turbidity_and_disabled_tds_round_trip(self):
        payload = dict(self.payload, turbidez_relativa=12.5, tds=None)
        payload.pop("turbidez")
        result = self.post(payload)
        self.assertEqual(result.status_code, 201)
        self.assertEqual(result.get_json()["turbidez_unidade"], "percentual_relativo")
        self.assertEqual(self.post(payload).status_code, 200)
        leitura = self.readings()[0]
        self.assertEqual(leitura["turbidez"], 12.5)
        self.assertEqual(leitura["turbidez_unidade"], "percentual_relativo")
        self.assertIsNone(leitura["tds"])
        # Mesma medida numérica em outra unidade é outra informação: conflito.
        self.assertEqual(self.post(dict(self.payload, turbidez=12.5, tds=None)).status_code, 409)
        self.assertEqual(len(self.readings()), 1)

    def test_relative_turbidity_validation(self):
        self.assertEqual(self.post(dict(self.payload, turbidez_relativa=10)).status_code, 400)
        for value in (-1, 101, "NaN", "Infinity", None):
            payload = dict(self.payload, turbidez_relativa=value)
            payload.pop("turbidez")
            self.assertEqual(self.post(payload).status_code, 400)
        self.assertEqual(self.readings(), [])

    def test_unit_migration_preserves_existing_measurements(self):
        self.assertEqual(self.post(self.payload).status_code, 201)
        self.sql("ALTER TABLE leituras DROP COLUMN turbidez_unidade")
        self.api.init_db()
        self.api.init_db()
        leitura = self.readings()[0]
        self.assertEqual(leitura["tds"], 450.0)
        self.assertEqual(leitura["turbidez"], 2.3)
        self.assertEqual(leitura["turbidez_unidade"], "ntu")
        self.assertEqual(self.post(self.payload).status_code, 200)

    def test_accented_equipment_id_round_trip_and_deduplication(self):
        payload = dict(self.payload, equipamento_id="físico_1")
        self.assertEqual(self.post(payload).status_code, 201)
        self.assertEqual(self.post(payload).status_code, 200)
        response = self.client.get("/leituras", query_string={"equipamento_id": "físico_1"})
        self.assertEqual(response.status_code, 200)
        rows = response.get_json()["leituras"]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["equipamento_id"], "físico_1")
        self.assertEqual(self.post(dict(payload, equipamento_id="físico/1")).status_code, 400)
        self.assertEqual(self.post(dict(payload, equipamento_id="x" * 65)).status_code, 400)


if __name__ == "__main__":
    unittest.main()
