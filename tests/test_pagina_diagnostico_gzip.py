import gzip
import importlib.util
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "pagina_gzip", ROOT / "firmware/esp32/prototipo/gerar_pagina_diagnostico.py")
gerador = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gerador)


class PaginaGzipTests(unittest.TestCase):
    def test_header_reproduz_html_e_geracao_deterministica(self):
        origem = ROOT / "firmware/esp32/include/PaginaDiagnostico.html"
        with tempfile.TemporaryDirectory() as pasta:
            destino = Path(pasta) / "generated/pagina.h"
            dados = gerador.gerar_pagina(origem, destino)
            header = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", destino.read_text()))
            self.assertEqual(header, dados)
            self.assertEqual(gzip.decompress(header), origem.read_bytes())
            self.assertLess(len(header), len(origem.read_bytes()))
            anterior = destino.stat().st_mtime_ns
            self.assertEqual(gerador.gerar_pagina(origem, destino), dados)
            self.assertEqual(destino.stat().st_mtime_ns, anterior)
