"""Contrato HTTP/unidades com conexão simulada; sem acesso a banco externo."""
from datetime import datetime
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import MagicMock, patch
from uuid import uuid4

import mysql.connector

ROOT = Path(__file__).resolve().parents[1]


class TurbidezDemonstrativaApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("api_demo_tests", ROOT / "backend/api_python/main.py")
        cls.api = importlib.util.module_from_spec(spec)
        with patch("mysql.connector.connect", side_effect=mysql.connector.Error("conexão bloqueada no teste")), \
                patch("logging.Logger.warning"):
            spec.loader.exec_module(cls.api)
        cls.api._db_schema_initialized = True
        cls.client = cls.api.app.test_client()

    def setUp(self):
        self.conn = MagicMock()
        self.cursor = self.conn.cursor.return_value
        self.cursor.lastrowid = 7
        self.binding = patch.object(self.api, "get_connection", return_value=self.conn)
        self.binding.start()
        self.addCleanup(self.binding.stop)
        self.payload = dict(equipamento_id="teste-demo", leitura_id=str(uuid4()), ph=7.2,
                            turbidez_demonstrativa=2.5, temperatura=22.5, tds=130.0,
                            data_hora="2026-11-07 12:00:00")

    def post(self, payload):
        return self.client.post("/leituras", json=payload)

    def test_nova_medicao_preserva_origem_e_unidade(self):
        result = self.post(self.payload)
        self.assertEqual(result.status_code, 201)
        self.assertEqual(result.json["turbidez_unidade"], "ntu_demonstrativo")
        self.assertFalse(result.json["duplicada"])
        params = self.cursor.execute.call_args.args[1]
        self.assertEqual(params[3:5], (2.5, "ntu_demonstrativo"))
        self.conn.commit.assert_called_once()

    def test_validacao_nao_insere_dados_ambiguos_ou_invalidos(self):
        for invalid in (-1, 1001, "NaN", "Infinity", None):
            self.assertEqual(self.post(dict(self.payload, turbidez_demonstrativa=invalid)).status_code, 400)
        for outro in ("turbidez", "turbidez_relativa"):
            self.assertEqual(self.post(dict(self.payload, **{outro: 2.5})).status_code, 400)
        self.cursor.execute.assert_not_called()

    def preparar_duplicidade(self, unidade):
        self.cursor.execute.side_effect = [mysql.connector.IntegrityError("duplicate", errno=1062), None]
        self.cursor.fetchone.return_value = (7, 7.2, 2.5, 22.5, 130.0,
                                            datetime(2026, 11, 7, 12), unidade, "medido", None)

    def test_reenvio_identico_confirma_sem_inserir_novo_registro(self):
        self.preparar_duplicidade("ntu_demonstrativo")
        result = self.post(self.payload)
        self.assertEqual(result.status_code, 200)
        self.assertTrue(result.json["duplicada"])
        self.conn.rollback.assert_called_once()
        self.conn.commit.assert_not_called()

    def test_nao_confunde_registro_ntu_anterior_com_demonstracao(self):
        self.preparar_duplicidade("ntu")
        self.assertEqual(self.post(self.payload).status_code, 409)
        self.conn.commit.assert_not_called()
