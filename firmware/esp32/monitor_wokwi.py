"""Reúne as saídas seriais reais das instâncias Wokwi via RFC2217."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import threading
import uuid

import serial


def acompanhar(nome, porta, parar, emitir):
    emitir(nome, 'monitor', f'Aguardando simulação na porta {porta}')
    while not parar.is_set():
        try:
            with serial.serial_for_url(f'rfc2217://127.0.0.1:{porta}', baudrate=115200, timeout=.5) as stream:
                emitir(nome, 'monitor', 'Serial conectada')
                buffer = b''
                while not parar.is_set():
                    chunk = stream.read(4096)
                    if not chunk:
                        continue
                    buffer += chunk
                    while b'\n' in buffer:
                        line, buffer = buffer.split(b'\n', 1)
                        emitir(nome, 'serial', line.decode('utf-8', errors='replace').rstrip('\r'))
                    if len(buffer) > 16384:
                        emitir(nome, 'serial', buffer.decode('utf-8', errors='replace'))
                        buffer = b''
                if buffer:
                    emitir(nome, 'serial', buffer.decode('utf-8', errors='replace'))
        except (serial.SerialException, OSError) as error:
            if not parar.is_set():
                # Não repete erros de conexão enquanto a simulação ainda não foi iniciada.
                if 'buffer' in locals():
                    if buffer:
                        emitir(nome, 'serial', buffer.decode('utf-8', errors='replace'))
                        buffer = b''
                    emitir(nome, 'monitor', f'Serial desconectada; aguardando reconexão ({type(error).__name__})')
                    del buffer
        parar.wait(2)


def monitorar(projetos, pasta_logs):
    import re
    targets = []
    for projeto in projetos:
        config = (projeto / 'wokwi.toml').read_text(encoding='utf-8')
        match = re.search(r'^rfc2217ServerPort\s*=\s*(\d+)\s*$', config, re.M)
        if not match:
            raise ValueError(f'{projeto.name}: porta serial não configurada. Execute o gerador atualizado.')
        targets.append((projeto.name, int(match.group(1))))
    if len({p for _, p in targets}) != len(targets):
        raise ValueError('Portas seriais repetidas no grupo')
    pasta_logs.mkdir(parents=True, exist_ok=True)
    arquivo = pasta_logs / f'{datetime.now():%Y%m%d-%H%M%S}-{uuid.uuid4().hex[:8]}.jsonl'
    parar = threading.Event()
    lock = threading.Lock()
    with arquivo.open('x', encoding='utf-8') as log:
        def emitir(nome, origem, mensagem):
            agora = datetime.now().astimezone().isoformat(timespec='seconds')
            with lock:
                print(f'[{agora}] [{nome}] {mensagem}', flush=True)
                log.write(json.dumps(dict(horario=agora, equipamento=nome, origem=origem, mensagem=mensagem), ensure_ascii=False) + '\n')
                log.flush()
        threads = [threading.Thread(target=acompanhar, args=(nome, porta, parar, emitir), daemon=True)
                   for nome, porta in targets]
        print(f'Logs: {arquivo}\nInicie o Wokwi em cada janela. Ctrl+C encerra somente este monitor.', flush=True)
        for thread in threads:
            thread.start()
        try:
            while not parar.wait(.5):
                pass
        except KeyboardInterrupt:
            parar.set()
        for thread in threads:
            thread.join(timeout=8)
    return arquivo


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--diretorio', type=Path, required=True)
    parser.add_argument('--quantidade', type=int, required=True)
    args = parser.parse_args()
    if not 1 <= args.quantidade <= 32:
        parser.error('--quantidade deve estar entre 1 e 32')
    projetos = [args.diretorio / f'esp32-{i:03d}' for i in range(1, args.quantidade + 1)]
    try:
        monitorar(projetos, args.diretorio / 'logs')
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
