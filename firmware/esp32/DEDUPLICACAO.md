# Proteção contra duplicação

Cada coleta cria um `leitura_id` UUID v4. O `id` auto incrementado continua identificando a linha no MySQL. A chave única `(equipamento_id, leitura_id)` impede que tentativas simultâneas ou reenvios criem duas linhas para a mesma medição. UUID, valores e horário são definidos antes do POST e permanecem iguais nas tentativas.

A API retorna 201 e `duplicada: false` na primeira gravação; em um reenvio idêntico retorna 200, `duplicada: true` e o mesmo `id`. Mesmo identificador com valores ou horário diferentes retorna 409. Números são comparados com tolerância de 1e-6 para acomodar o armazenamento FLOAT no MySQL. Cada nova coleta cria outro UUID, mesmo se os valores dos sensores forem iguais.

A API adiciona a coluna `leitura_id` e o índice automaticamente ao banco existente, sem apagar leituras antigas. Linhas antigas permanecem com UUID NULL. Clientes antigos sem UUID continuam aceitos, mas seus envios não têm deduplicação. O usuário do banco precisa de permissão para ALTER TABLE; confira os logs do deploy se a migração falhar. O `schema.sql` serve para bancos novos e não altera sozinho uma tabela já existente.

Os dois firmwares confirmam sucesso somente quando a resposta 200/201 contém `status: ok`, UUID e equipamento correspondentes e um ID positivo. A versão atual também guarda leituras em uma fila flash de 20 registros, descrita em FILA_LOCAL.md. A deduplicação permite reenvio seguro dessas pendências. Não resolve lentidão do Wokwi ou erro TLS.

## Publicar a API antes de testar

O Render precisa executar a API deste commit. Confirme a branch configurada no serviço e o commit do deploy; fazer push em `b.lucasdxl` não atualiza um serviço ligado à `main`. Se necessário, faça deploy manual da branch configurada com esta alteração. Não inicie o firmware novo contra a API anterior: ela não retorna a confirmação com UUID.

## Preparar uma instância Wokwi

Pare as simulações e, na raiz do repositório, execute:

```powershell
git pull origin b.lucasdxl
python firmware/esp32/preparar_wokwi.py --quantidade 1 --atualizar-firmware --compilar --abrir
```

O gerador preserva outras instâncias, mas não as inicie durante esse teste. `--atualizar-firmware` é necessário para atualizar o código de uma instância já criada: o original fica em `src/main.cpp.bak` ou sufixo numerado. Ajustes manuais de URL ou intervalos devem ser reaplicados. Diagrama e valores dos sensores são preservados.

## Forçar um reenvio pelo próprio Wokwi

Antes de iniciar, acrescente esta linha no topo de `firmware/esp32/.wokwi-instances/esp32-001/src/main.cpp`, antes dos includes:

```cpp
#define TESTAR_DUPLICACAO 1
```

Recompile a instância:

```powershell
pio run -d firmware/esp32/.wokwi-instances/esp32-001
```

Abra essa pasta no VS Code e use `Wokwi: Start Simulator`. Após uma coleta confirmada, o modo de teste faz um segundo POST do **mesmo JSON**, sem coletar novamente e sem alterar UUID/horário. Cada ciclo em modo de teste gera duas requisições, o que aumenta o trabalho HTTPS; use uma instância e desligue depois.

Confira na serial:

1. `[JSON]` com `leitura_id`.
2. Primeiro envio: HTTP 201, `duplicada: false` e um `id`.
3. `[TESTE] Reenviando o mesmo JSON`.
4. Segundo envio: HTTP 200, `duplicada: true` e **o mesmo id e UUID**.

Se o primeiro POST tiver sido salvo, mas sua resposta se perder, uma tentativa seguinte já pode retornar 200. Isso também é esperado. Timeout ou erro TLS não aprovam o teste: aguarde dois envios confirmados e confira o banco.

No MySQL Workbench, substitua o UUID abaixo pelo mostrado na serial:

```sql
SELECT id, equipamento_id, leitura_id, ph, turbidez, temperatura, orp, data_hora
FROM leituras
WHERE equipamento_id = 'esp32-001'
  AND leitura_id = 'UUID_COPIADO_DA_SERIAL';
```

Deve existir **uma linha**. A próxima coleta normal tem outro UUID e deve criar outra linha, mesmo com valores iguais. O SCADA mostra a leitura, mas a consulta acima é a comprovação da deduplicação.

Depois, remova `#define TESTAR_DUPLICACAO 1` ou altere para 0, recompile e reinicie o simulador. O padrão é 0 nos dois firmwares. Não altere delays, sensores nem o UUID para fazer esse teste.
