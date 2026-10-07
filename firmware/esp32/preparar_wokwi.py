"""Prepara ou amplia um grupo de instâncias Wokwi sem substituir seus firmwares."""
import argparse
import copy
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

BASE = Path(__file__).resolve().parent


def placa_esp32(circuito):
    placas = [part for part in circuito.get('parts', [])
              if part.get('type') == 'board-esp32-devkit-c-v4']
    if len(placas) != 1:
        raise ValueError('O circuito deve conter exatamente uma board-esp32-devkit-c-v4.')
    return placas[0]


def salvar_mac(projeto, mac):
    arquivo = projeto / 'diagram.json'
    circuito = json.loads(arquivo.read_text(encoding='utf-8'))
    attrs = placa_esp32(circuito).setdefault('attrs', {})
    if attrs.get('macAddress') == mac:
        return
    backup = arquivo.with_name('diagram.json.bak')
    indice = 1
    while backup.exists():
        backup = arquivo.with_name(f'diagram.json.bak.{indice}')
        indice += 1
    shutil.copy2(arquivo, backup)
    attrs['macAddress'] = mac
    arquivo.write_text(json.dumps(circuito, indent=2) + '\n', encoding='utf-8')
    print(f'MAC atualizado: {projeto.name} → {mac} (backup: {backup.name})')


def configurar_serial(projeto, porta):
    arquivo = projeto / 'wokwi.toml'
    original = arquivo.read_text(encoding='utf-8')
    linhas = original.splitlines(keepends=True)
    inicio = next((i for i, line in enumerate(linhas) if line.strip() == '[wokwi]'), None)
    if inicio is None:
        raise ValueError(f'{projeto.name}: seção [wokwi] não encontrada')
    fim = next((i for i in range(inicio + 1, len(linhas)) if linhas[i].lstrip().startswith('[')), len(linhas))
    for i in range(inicio + 1, fim):
        if re.match(r'\s*rfc2217ServerPort\s*=', linhas[i]):
            linhas[i] = f'rfc2217ServerPort = {porta}\n'
            break
    else:
        if fim > 0 and not linhas[fim-1].endswith('\n'):
            linhas[fim-1] += '\n'
        linhas.insert(fim, f'rfc2217ServerPort = {porta}\n')
    novo = ''.join(linhas)
    if novo != original:
        backup = arquivo.with_name('wokwi.toml.bak')
        indice = 1
        while backup.exists():
            backup = arquivo.with_name(f'wokwi.toml.bak.{indice}')
            indice += 1
        shutil.copy2(arquivo, backup)
        arquivo.write_text(novo, encoding='utf-8')


def preparar(quantidade, destino, atualizar_firmware=False):
    if not 1 <= quantidade <= 32:
        raise ValueError('A quantidade total deve estar entre 1 e 32.')
    nomes = [f'esp32-{i:03d}' for i in range(1, quantidade + 1)]
    fonte = (BASE / 'src/main.cpp').read_text(encoding='utf-8')
    circuito = json.loads((BASE / 'diagram.json').read_text(encoding='utf-8'))
    placa_esp32(circuito)
    config = (BASE / 'platformio.ini').read_text(encoding='utf-8')
    # Valida tudo antes de alterar projetos existentes.
    for nome in nomes:
        projeto = destino / nome
        if projeto.exists():
            if not all((projeto / f).is_file() for f in
                       ('src/main.cpp', 'diagram.json', 'platformio.ini', 'wokwi.toml')):
                raise ValueError(f'{projeto}: projeto incompleto; use outro destino ou restaure os arquivos.')
            if not (projeto / 'src/main.cpp').read_text(encoding='utf-8').startswith(f'#define EQUIPAMENTO_ID "{nome}"'):
                raise ValueError(f'{projeto}: ID inesperado; nenhum firmware será sobrescrito.')
            placa_esp32(json.loads((projeto / 'diagram.json').read_text(encoding='utf-8')))
    destino.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destino) as temporario:
        novos = []
        for numero, nome in enumerate(nomes, 1):
            if (destino / nome).exists():
                continue
            circuito_instancia = copy.deepcopy(circuito)
            placa_esp32(circuito_instancia).setdefault('attrs', {})['macAddress'] = f'02:00:00:00:00:{numero:02x}'
            projeto = Path(temporario) / nome
            (projeto / 'src').mkdir(parents=True)
            (projeto / '.vscode').mkdir()
            (projeto / 'src/main.cpp').write_text(f'#define EQUIPAMENTO_ID "{nome}"\n' + fonte, encoding='utf-8')
            (projeto / 'diagram.json').write_text(json.dumps(circuito_instancia, indent=2) + '\n', encoding='utf-8')
            (projeto / 'platformio.ini').write_text(config, encoding='utf-8')
            (projeto / 'wokwi.toml').write_text('[wokwi]\nversion = 1\nfirmware = ".pio/build/esp32dev/firmware-merged.bin"\nelf = ".pio/build/esp32dev/firmware.elf"\n', encoding='utf-8')
            tarefas = {'version': '2.0.0', 'tasks': [{
                'label': 'Compilar ESP32', 'type': 'shell', 'command': 'pio', 'args': ['run'],
                'options': {'cwd': '${workspaceFolder}'}, 'problemMatcher': [],
                'group': {'kind': 'build', 'isDefault': True}}]}
            (projeto / '.vscode/tasks.json').write_text(json.dumps(tarefas, indent=2) + '\n', encoding='utf-8')
            novos.append(nome)
        for nome in novos:
            shutil.move(str(Path(temporario) / nome), str(destino / nome))
            print(f'Criada: {nome}')
    for nome in nomes:
        projeto = destino / nome
        shutil.copytree(BASE / 'include', projeto / 'include', dirs_exist_ok=True)
        for recurso in ('partitions.csv', 'gerar_imagem.py'):
            shutil.copy2(BASE / recurso, projeto / recurso)
        ini = projeto / 'platformio.ini'
        texto = ini.read_text(encoding='utf-8')
        if 'board_build.partitions' not in texto:
            texto = texto.replace('monitor_speed = 115200', 'monitor_speed = 115200\nboard_build.partitions = partitions.csv')
        if 'extra_scripts' not in texto:
            texto = texto.replace('monitor_speed = 115200', 'monitor_speed = 115200\nextra_scripts = post:gerar_imagem.py')
        ini.write_text(texto, encoding='utf-8')
        config_wokwi = projeto / 'wokwi.toml'
        texto = config_wokwi.read_text(encoding='utf-8').replace('/firmware.bin', '/firmware-merged.bin')
        config_wokwi.write_text(texto, encoding='utf-8')
        circuito_projeto = json.loads((projeto / 'diagram.json').read_text(encoding='utf-8'))
        attrs = placa_esp32(circuito_projeto).setdefault('attrs', {})
        if attrs.get('firmwareOffset') != '0':
            backup_flash = projeto / 'diagram-before-flash.json.bak'
            indice = 1
            while backup_flash.exists():
                backup_flash = projeto / f'diagram-before-flash.json.bak.{indice}'
                indice += 1
            shutil.copy2(projeto / 'diagram.json', backup_flash)
            attrs['firmwareOffset'] = '0'
            (projeto / 'diagram.json').write_text(json.dumps(circuito_projeto, indent=2) + '\n', encoding='utf-8')
        if atualizar_firmware and nome not in novos:
            arquivo = projeto / 'src/main.cpp'
            novo = f'#define EQUIPAMENTO_ID "{nome}"\n' + fonte
            if arquivo.read_text(encoding='utf-8') != novo:
                backup = arquivo.with_name('main.cpp.bak')
                indice = 1
                while backup.exists():
                    backup = arquivo.with_name(f'main.cpp.bak.{indice}')
                    indice += 1
                shutil.copy2(arquivo, backup)
                arquivo.write_text(novo, encoding='utf-8')
                print(f'Firmware atualizado: {nome}; backup em {backup.name}')
    for numero, nome in enumerate(nomes, 1):
        salvar_mac(destino / nome, f'02:00:00:00:00:{numero:02x}')
        configurar_serial(destino / nome, 4000 + numero)
    return [destino / nome for nome in nomes]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    grupo = parser.add_mutually_exclusive_group()
    grupo.add_argument('--quantidade', type=int, help='Total desejado: 2, 4, 8, 16 ou até 32')
    grupo.add_argument('--adicionar', type=int, help='Acrescenta N instâncias ao grupo existente')
    parser.add_argument('--saida', type=Path, default=BASE / '.wokwi-instances')
    parser.add_argument('--compilar', action='store_true', help='Compila o grupo com PlatformIO antes de abrir')
    parser.add_argument('--abrir', action='store_true', help='Abre novas instâncias no VS Code; se nenhuma for nova, abre o grupo')
    parser.add_argument('--abrir-todas', action='store_true', help='Abre todo o grupo no VS Code, mesmo ao adicionar instâncias')
    parser.add_argument("--logs", action="store_true", help="Reúne as seriais no terminal principal até Ctrl+C")
    parser.add_argument("--atualizar-firmware", action="store_true", help="Atualiza main.cpp das instâncias antigas a partir da base, criando backups")
    args = parser.parse_args()
    destino = args.saida.resolve()
    existentes = {p.name for p in destino.glob('esp32-*') if p.is_dir()}
    quantidade = args.quantidade if args.quantidade is not None else 2
    if args.adicionar is not None:
        if args.adicionar <= 0:
            parser.error('--adicionar deve ser positivo')
        numeros = [int(nome[6:]) for nome in existentes if nome[6:].isdigit()]
        quantidade = max(numeros, default=0) + args.adicionar
    pio = shutil.which('pio') or shutil.which('platformio')
    code = shutil.which('code')
    if args.compilar and not pio:
        parser.error('PlatformIO não encontrado. Execute no terminal do PlatformIO com pio disponível no PATH.')
    if (args.abrir or args.abrir_todas) and not code:
        parser.error('Comando code não encontrado no PATH. Abra as pastas manualmente ou configure o comando do VS Code.')
    try:
        projetos = preparar(quantidade, destino, args.atualizar_firmware)
        if args.compilar:
            for projeto in projetos:
                print(f'Compilando {projeto.name}...', flush=True)
                subprocess.run([pio, 'run'], cwd=projeto, check=True)
                if not (projeto / '.pio/build/esp32dev/firmware-merged.bin').is_file():
                    raise ValueError(f'{projeto.name}: compilação não gerou firmware.bin')
        if args.abrir or args.abrir_todas:
            novos = [p for p in projetos if p.name not in existentes]
            for projeto in (projetos if args.abrir_todas else novos or projetos):
                subprocess.run([code, '--new-window', str(projeto)], check=True)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'Erro: {error}', file=sys.stderr)
        return 1
    print(f'Grupo preparado: {len(projetos)} instâncias em {destino}')
    print('Use Wokwi: Start Simulator em cada janela. Sem --compilar, execute pio run primeiro.')
    print('Instâncias fora do total solicitado não são apagadas ou paradas. Pare-as manualmente se necessário.')
    if args.logs:
        try:
            return subprocess.run([sys.executable, str(BASE / 'monitor_wokwi.py'),
                                   '--diretorio', str(destino), '--quantidade', str(quantidade)]).returncode
        except KeyboardInterrupt:
            return 130
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
