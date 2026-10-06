# Simulações Wokwi simultâneas no VS Code

## Começar com duas e aumentar o grupo

Na raiz do repositório, no terminal onde `pio` e `code` estão disponíveis:

```bash
python firmware/esp32/preparar_wokwi.py --quantidade 2 --compilar --abrir
```

O script cria ou reaproveita `esp32-001` e `esp32-002`, atribui IDs e MACs diferentes automaticamente, compila cada firmware e abre cada projeto em uma janela própria do VS Code. Em cada janela, use `Wokwi: Start Simulator` e mantenha a aba do simulador visível. A extensão continua responsável por iniciar a simulação; o script não inicia nem sincroniza o Wokwi automaticamente.

Quando duas funcionarem, aumente o total:

```bash
python firmware/esp32/preparar_wokwi.py --quantidade 4 --compilar --abrir
python firmware/esp32/preparar_wokwi.py --quantidade 8 --compilar --abrir
python firmware/esp32/preparar_wokwi.py --quantidade 16 --compilar --abrir
```

Execute um comando por etapa. `--quantidade 4` significa quatro no total: preserva 001/002 e cria 003/004. `--abrir` abre somente as novas janelas quando há novas instâncias; se nenhuma for nova, abre o grupo solicitado. Para abrir todas explicitamente, use `--abrir-todas`. Não execute os comandos de etapas seguintes até avaliar a atual. A compilação usa o cache incremental de cada projeto; se alguma falhar, o script não abre janelas.

Também pode adicionar duas às existentes:

```bash
python firmware/esp32/preparar_wokwi.py --adicionar 2 --compilar --abrir
```

O limite do gerador é 32. `--adicionar` conta a partir do maior número de instância existente; lacunas anteriores são preenchidas. Solicitar um total menor não apaga projetos, não fecha janelas e não para simulações: pare as extras manualmente para testar exatamente aquela quantidade. Execute uma geração de cada vez.

## Instâncias antigas e alterações locais

As instâncias antigas são reaproveitadas. Seu `src/main.cpp`, configuração de build e ajustes dos sensores são preservados. O script corrige automaticamente apenas o MAC da placa no `diagram.json`, salvando o original como `diagram.json.bak` (ou um backup numerado se necessário). Pare as simulações antes da primeira execução com esta versão e reinicie as antigas após a correção do MAC; a mudança do circuito não exige recompilar, mas o comando com `--compilar` verifica o firmware.

Novas instâncias copiam o firmware e circuito base atuais. Alterações posteriores em `src/main.cpp` da pasta base não substituem automaticamente firmware de instâncias já existentes. Edite as cópias desejadas ou use `--saida` para preparar um grupo novo. IDs e MACs recomeçam por grupo: não rode dois grupos com os mesmos identificadores no mesmo teste.

## Ferramentas e API

Sem `pio` no PATH, use o terminal do PlatformIO. Sem `code` no PATH, configure o comando do VS Code ou omita `--abrir` e abra as pastas manualmente. É possível executar só `--quantidade 2` para preparar os arquivos e depois compilar com `pio run` em cada janela.

Cada instância utiliza sensores do Wokwi, seu próprio firmware, circuito e diretório `.pio`. O ID (`esp32-001`) identifica a leitura na API. O MAC (`02:00:00:00:00:01`) identifica a interface Wi-Fi, usando o atributo `macAddress` documentado em https://docs.wokwi.com/guides/esp32#changing-the-mac-address. MACs distintos eliminam identidades duplicadas, mas não garantem a resolução de falhas TLS ou a disponibilidade de várias sessões na sua conta Wokwi.

Todos usam a URL configurada no firmware. Publique a API com suporte a `equipamento_id` para salvar a identificação; o usuário MySQL precisa de permissão ALTER para a criação da coluna no banco existente. Leituras antigas continuam preservadas. Para consultar um equipamento: `GET /leituras?equipamento_id=esp32-001&limite=10`.

A execução simultânea precisa ser validada no VS Code: este ambiente cloud não possui a extensão gráfica. Monitore HTTP 201 nos terminais de todas as instâncias, erros e tempos de resposta, além do Render/banco. O SCADA mostra a leitura global mais recente de cada consulta e não captura todas as leituras entre consultas. Muitas instâncias também podem limitar o desempenho do computador, sem representar o limite da API.

## Organização

- `src/main.cpp`: firmware base do Wokwi.
- `diagram.json`, `platformio.ini`, `wokwi.toml`: circuito e configuração base.
- `preparar_wokwi.py`: prepara e amplia os grupos.
- `.wokwi-instances/`: projetos gerados, ignorados pelo Git.
- `prototipo/`: projeto separado do equipamento físico (`pio run -d firmware/esp32/prototipo`).
- `tests/`: testes do gerador (`python -m unittest discover -s firmware/esp32/tests`).
