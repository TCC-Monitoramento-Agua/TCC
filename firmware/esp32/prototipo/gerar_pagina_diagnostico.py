"""Comprime a página local no build, sem ocupar a flash com HTML descomprimido."""
import gzip
from pathlib import Path


def gerar_pagina(origem: Path, destino: Path) -> bytes:
    dados = gzip.compress(origem.read_bytes(), compresslevel=9, mtime=0)
    linhas = [", ".join(f"0x{x:02x}" for x in dados[i:i + 16]) for i in range(0, len(dados), 16)]
    texto = "#pragma once\nstatic const uint8_t PAGINA_DIAGNOSTICO_GZIP[] PROGMEM = {\n"
    texto += ",\n".join(linhas) + "\n};\n"
    destino.parent.mkdir(parents=True, exist_ok=True)
    if not destino.exists() or destino.read_text(encoding="utf-8") != texto:
        destino.write_text(texto, encoding="utf-8")
    return dados


if "Import" in globals():
    Import("env")
    projeto = Path(env.subst("$PROJECT_DIR"))
    destino = Path(env.subst("$BUILD_DIR")) / "generated" / "PaginaDiagnosticoGzip.h"
    gerar_pagina(projeto.parent / "include" / "PaginaDiagnostico.html", destino)
    env.Append(CPPPATH=[str(destino.parent)])
