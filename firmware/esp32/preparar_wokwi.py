"""Cria projetos Wokwi independentes a partir do firmware e circuito atuais."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile


def preparar(quantidade, destino):
    base = Path(__file__).resolve().parent
    nomes = [f'esp32-{i:03d}' for i in range(1, quantidade + 1)]
    if any((destino / nome).exists() for nome in nomes):
        raise ValueError('Já existem instâncias nesse destino. Use outro --saida para preservar suas alterações.')
    fonte = (base / 'src/main.cpp').read_text(encoding='utf-8')
    circuito = json.loads((base / 'diagram.json').read_text(encoding='utf-8'))
    config = (base / 'platformio.ini').read_text(encoding='utf-8')
    destino.mkdir(parents=True, exist_ok=True)
    # Prepara tudo antes de mover os projetos ao destino final.
    with tempfile.TemporaryDirectory(dir=destino) as temporario:
        for nome in nomes:
            projeto = Path(temporario) / nome
            (projeto / 'src').mkdir(parents=True)
            (projeto / '.vscode').mkdir()
            (projeto / 'src/main.cpp').write_text(
                f'#define EQUIPAMENTO_ID "{nome}"\n' + fonte, encoding='utf-8')
            (projeto / 'diagram.json').write_text(json.dumps(circuito, indent=2) + '\n', encoding='utf-8')
            (projeto / 'platformio.ini').write_text(config, encoding='utf-8')
            (projeto / 'wokwi.toml').write_text(
                '[wokwi]\nversion = 1\nfirmware = ".pio/build/esp32dev/firmware.bin"\n'
                'elf = ".pio/build/esp32dev/firmware.elf"\n', encoding='utf-8')
            tarefas = {'version': '2.0.0', 'tasks': [{
                'label': 'Compilar ESP32', 'type': 'shell', 'command': 'pio', 'args': ['run'],
                'options': {'cwd': '${workspaceFolder}'}, 'problemMatcher': [],
                'group': {'kind': 'build', 'isDefault': True}}]}
            (projeto / '.vscode/tasks.json').write_text(json.dumps(tarefas, indent=2) + '\n', encoding='utf-8')
        for nome in nomes:
            shutil.move(str(Path(temporario) / nome), str(destino / nome))
    return [destino / nome for nome in nomes]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--quantidade', type=int, default=2)
    parser.add_argument('--saida', type=Path, default=Path(__file__).resolve().parent / '.wokwi-instances')
    args = parser.parse_args()
    if not 1 <= args.quantidade <= 32:
        parser.error('--quantidade deve estar entre 1 e 32')
    try:
        projetos = preparar(args.quantidade, args.saida.resolve())
    except ValueError as error:
        parser.error(str(error))
    for projeto in projetos:
        print(projeto)
    print('Abra cada pasta em uma janela diferente do VS Code, execute pio run no terminal dela e use Wokwi: Start Simulator.')
    print('Mantenha todas as simulações rodando; a inicialização manual não é sincronizada.')


if __name__ == '__main__':
    main()
