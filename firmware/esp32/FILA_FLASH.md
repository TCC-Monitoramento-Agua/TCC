# Fila persistente de medições

Os firmwares do Wokwi (`src/main.cpp`) e do protótipo físico (`prototipo/src/main.cpp`) usam a mesma implementação em `include/`. A coleta não depende do Wi-Fi; os envios HTTP são feitos por uma tarefa FreeRTOS separada.

## Intervalo de coleta

Edite, em cada firmware, a constante perto do início:

```cpp
const uint32_t INTERVALO_MEDICAO_MS = 30000; // 30 segundos
```

Valores em milissegundos: 10000 = 10 s, 30000 = 30 s, 60000 = 1 min. O intervalo é entre os inícios das coletas, não um atraso depois do POST. A leitura média dos sensores demora vários segundos; se o intervalo for menor que esse tempo, aparece um alerta e não é possível manter a frequência desejada. Reduzir o intervalo também reduz o tempo de autonomia da fila.

## Capacidade e armazenamento

`CAPACIDADE_FILA = 2880` em `include/QueueRuntime.h` limita a fila a 2880 registros. Cada slot ocupa 512 bytes, inclusive tamanho, sequência e CRC: até 1.474.560 bytes para o arquivo de dados. O arquivo começa vazio e cresce um slot por medição até esse limite, evitando gravar 1,5 MB de zeros na inicialização. Arquivos antigos pré-alocados continuam aceitos; tamanho parcial de registro ou ACK incompatível bloqueiam a fila. A partição de flash é de 0x1F0000 bytes em uma placa ESP32 de **4 MB**, com espaço adicional para LittleFS, confirmações e referências de relógio. O tamanho de flash físico precisa ser confirmado antes de gravar em uma placa diferente.

Com intervalo de 30 segundos, a capacidade corresponde a 24 horas de medições sem envio; com 10 segundos, corresponde a 8 horas; com 60 segundos, 48 horas. A capacidade de registros foi testada preenchendo a fila, mas não constitui um teste físico contínuo de 24 horas, nem certificação de resistência a falhas elétricas/desgaste. Não altere a capacidade ou o mapa de partições com pendências: esvazie/exporte a fila primeiro. Metadados de muitos reinícios também consomem espaço adicional.

O sistema salva na flash antes de tentar enviar. Preserva equipamento, UUID, horário e valores originais. HTTP 200/201 só libera um slot se a resposta tiver `status=ok`, equipamento e UUID correspondentes e ID do banco positivo. Resposta perdida permite reenvio; a API já reconhece a mesma medição sem duplicá-la. Publique essa API antes de usar o firmware da fila.

Falhas de envio usam espera progressiva de 5 até 120 segundos. Há reconexão Wi-Fi. A confirmação local é persistida por arquivo temporário e rename do LittleFS. CRC e verificação de escrita detectam registros inválidos; falhas de integridade suspendem os envios e geram alerta, sem formatar ou descartar pendências. Só uma partição completamente apagada é formatada automaticamente na primeira instalação.

Com 80% de ocupação há alerta. Quando cheia, **novas medições não podem ser armazenadas** e isso é informado no log; as antigas não são sobrescritas. Uma fila finita não garante ausência de perdas em interrupções ilimitadas. Para coletar durante falta de energia é necessária alimentação de reserva; flash preserva apenas o que já foi gravado.

## Relógio e reinícios

Após NTP sincronizar, os registros usam o horário original de coleta. Sem horário válido, valores são preservados com o ID do boot e uptime. Se esse mesmo boot conseguir sincronizar depois, uma referência persistente permite reconstruir o horário estimado, mantendo-o estável nos reenvios. Se o ESP32 reiniciar antes de obter qualquer referência para aquele boot, não há como determinar a hora real daquelas medições sem um RTC: elas permanecem guardadas, com alerta, e o envio FIFO não avança sobre esse registro. Não substituímos a hora da coleta pela hora de reconexão. A resolução desse caso depende de RTC ou tratamento explícito posterior.

## Wokwi

Atualize as instâncias antigas explicitamente (o firmware anterior fica em backup):

```powershell
python firmware/esp32/preparar_wokwi.py --quantidade 2 --atualizar-firmware --compilar --abrir --logs
```

`--atualizar-firmware` copia a base sobre os `main.cpp` gerados: ajustes manuais, inclusive intervalos, ficam no backup e precisam ser reaplicados. Sem essa opção, firmwares existentes são preservados. As configurações, headers e mapa de partições necessários são preparados automaticamente.

A compilação gera `firmware-merged.bin` com bootloader, partições e aplicação, sem embutir uma imagem vazia de LittleFS. O atributo `firmwareOffset=0` indica a imagem completa. Cada simulador tem sua própria flash. A persistência ao parar/reabrir uma sessão depende do Wokwi: não assuma que reset e nova sessão preservam a flash da mesma forma que o dispositivo físico. A persistência real após desligamento precisa ser validada no ESP32 físico.

## Protótipo físico

Na raiz do repositório:

```powershell
pio run -d firmware/esp32/prototipo
pio run -d firmware/esp32/prototipo --target upload
```

O projeto usa os headers e mapa de partições do diretório pai. Confira Wi-Fi, modelo e tamanho da flash. Não execute `erase`, `uploadfs` nem altere partições com pendências, pois essas operações podem apagar o armazenamento.

## Validação

1. Com API funcionando, confira `Medição salva na flash` seguida de `Confirmada pela API`.
2. Interrompa a API/Wi-Fi por 5–10 minutos: as pendências devem aumentar, enquanto novas coletas continuam.
3. Reconecte: o número de pendências deve diminuir, mantendo UUIDs e horários.
4. No hardware, reinicie com pendências **que já tenham horário válido** e confira recuperação/reenvio.
5. Simule perda de resposta após gravação: a API deve confirmar sem duplicar.
6. Confira alerta de fila cheia e falha de armazenamento; nenhum desses casos pode ser tratado como sucesso.

O teste C++ nativo exercita o próprio núcleo da fila com armazenamento controlado: 2880 registros, FIFO, reinício, reutilização de slots, ACK perdido e corrupção. Os testes Python também verificam atualização segura das instâncias. As verificações de quedas de energia, funcionamento contínuo e sessões simultâneas exigem hardware/Wokwi.
