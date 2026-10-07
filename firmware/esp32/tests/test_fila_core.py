from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class QueueCoreTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'Compilador C++ nativo não instalado')
    def test_persistence_capacity_ack_failures_and_corruption(self):
        base = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'queue-test'
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(base/'include'), str(base/'tests/fila_core.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
