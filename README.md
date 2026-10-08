# Sistema de Monitoramento Inteligente da Qualidade da Água de Reservatórios

Projeto de TCC voltado ao monitoramento de parâmetros da água utilizando ESP32, API em Python, banco MySQL em nuvem (Aiven) e dashboard em Power BI.

## Tecnologias
- ESP32
- C/C++
- Python
- Flask 3.x
- MySQL (hospedado em Aiven ou similar)
- SCADA

## Estrutura do projeto
- `firmware/esp32`: código do microcontrolador
- `backend/api_python`: API Flask para recebimento e armazenamento dos dados
- `database`: scripts SQL

## Fluxo do sistema
Sensores -> ESP32 -> API Python -> MySQL (Aiven) -> Dashboard

## Parâmetros monitorados
- pH
- Turbidez
- Temperatura
- ORP ou oxigênio dissolvido

## Arquitetura de persistência MySQL

O projeto usa MySQL hospedado em nuvem (Aiven) como banco de dados principal. A API Flask cria automaticamente a tabela `leituras` quando é iniciada.

### Inicialização automática do schema
- Funciona tanto **localmente** (`python main.py`) quanto **em produção** (`gunicorn main:app` no Render)
- Usa `@app.before_request` (compatível com Flask 3.x)
- Também tenta inicializar no import do módulo para funcionar com gunicorn

### Estrutura da tabela
Arquivo `database/schema.sql` (criado automaticamente):
```sql
CREATE TABLE leituras (
  id INT AUTO_INCREMENT PRIMARY KEY,
  ph FLOAT NOT NULL,
  turbidez FLOAT NOT NULL,
  temperatura FLOAT NOT NULL,
  orp FLOAT NULL,
  data_hora DATETIME NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
```

## Variáveis de ambiente necessárias

### Obrigatórias
```env
DB_HOST=seu-banco.aivencloud.com
DB_PORT=13306
DB_USER=avnadmin
DB_PASSWORD=sua-senha
DB_NAME=monitoramento_agua
```

### Opcionais (SSL/Aiven)
```env
DB_SSL_REQUIRED=true
DB_SSL_CA=/caminho/para/ca.pem
DB_SSL_CERT=/caminho/para/cert.pem
DB_SSL_KEY=/caminho/para/key.pem
DB_SSL_VERIFY=true
DB_CONNECT_TIMEOUT=10
PORT=5000
```

## Executando localmente

### Pré-requisitos
- Python 3.10+
- Virtual environment

### Passos

1. Vá para o diretório da API:
```bash
cd backend/api_python
```

2. Configure as variáveis de ambiente:
```bash
cp .env.example .env
# Edite .env com suas credenciais do Aiven
```

3. Instale dependências:
```bash
pip install -r requirements.txt
```

4. Execute:
```bash
python main.py
```

5. Teste a API:
```bash
curl http://localhost:5000/status
```

Resposta esperada:
```json
{"status":"ok","api":"online","database":"conectado"}
```

## Executando em produção (Render + Aiven)

### Configuração no Render

1. **Conecte seu repositório GitHub ao Render**

2. **Configure variáveis de ambiente no painel Render:**
   - `DB_HOST` (seu servidor Aiven)
   - `DB_PORT` (padrão Aiven: 13306)
   - `DB_USER` (usuário Aiven)
   - `DB_PASSWORD` (senha Aiven)
   - `DB_NAME` (nome do banco)
   - `DB_SSL_REQUIRED=true` (recomendado)
   - `PYTHON_VERSION=3.10`

3. **Configure o comando de inicialização:**
```bash
pip install -r backend/api_python/requirements.txt && gunicorn -w 4 -b 0.0.0.0:$PORT backend.api_python.main:app
```

4. **Deploy!**

### Banco MySQL no Aiven
- Crie uma instância MySQL no console Aiven
- Copie as credenciais
- Insira nas variáveis de ambiente do Render
- SSL é habilitado automaticamente

## Endpoints da API

### GET `/`
Status básico
```json
{"status":"ok","message":"API de monitoramento da água em execução"}
```

### GET `/status`
Status da API e banco
```json
{"status":"ok","api":"online","database":"conectado"}
```

### POST `/leituras`
Enviar leitura de sensores

Request:
```json
{
  "ph": 7.5,
  "turbidez": 2.3,
  "temperatura": 22.5,
  "orp": 450.0,
  "data_hora": "2026-06-03T10:30:00"
}
```

Response:
```json
{"status":"ok","mensagem":"Leitura salva com sucesso","id":1}
```

### GET `/leituras`
Listar leituras

Query: `?limite=10`

Response:
```json
{
  "status":"ok",
  "leituras":[
    {"id":1,"ph":7.5,"turbidez":2.3,"temperatura":22.5,"orp":450.0,"data_hora":"2026-06-03 10:30:00"}
  ]
}
```

## Troubleshooting

| Erro | Solução |
|------|---------|
| Variáveis de ambiente faltando | Copie `.env.example` para `.env` e configure |
| Can't connect to MySQL | Verifique DB_HOST, DB_PORT e credenciais |
| SSL certificate verify failed | Configure `DB_SSL_REQUIRED=true` |
| Schema não criado | Verifique logs da primeira requisição |

## Autores
- João Henrique Tomaz Dutra
- Lucas Balint Vilar

## Professor Orientador
- Marcelo do Carmo Camargo Gaiotto


### Otimização de múltiplas simulações Wokwi

Depois de atualizar a branch, pare as simulações e execute na raiz:

```powershell
python firmware/esp32/preparar_wokwi.py --quantidade 4 --atualizar-firmware --compilar --abrir-todas
```

Inicie manualmente cada Wokwi. `--atualizar-firmware` substitui os fontes das instâncias existentes pela base e salva cada original em `src/main.cpp.bak` (ou sufixo numerado). Ajustes locais de URL e intervalo precisam ser reaplicados; sem a opção, os fontes antigos são preservados. Circuito e configurações dos sensores são preservados.

A espera do DS18B20 usa `delay` pelo tempo de conversão correspondente à resolução, evitando consultas OneWire contínuas. As três amostras e a média continuam iguais. `LOG_AMOSTRAS` no início dos dois firmwares vale 0; use 1 para reativar detalhes por amostra. Médias, JSON e respostas HTTP continuam visíveis.

O gerador define `ATRASO_INICIAL_MS` como 0, 3000, 6000 e 9000 nas primeiras quatro instâncias. Isso distribui a inicialização quando elas são iniciadas juntas; não sincroniza janelas nem garante separação permanente dos POSTs. As esperas entre tentativas também variam de 2 a 5 segundos por equipamento. No protótipo físico o atraso padrão é zero e pode ser definido por build flag. `SEND_INTERVAL = 10000` continua sendo a espera **após** terminar o ciclo, nos dois firmwares.

Os logs `[TEMPO]` mostram a duração da coleta/preparo e do envio (incluindo tentativas), em milissegundos do ESP32. Para comparar desempenho, rode primeiro uma, depois duas e então quatro instâncias por cinco minutos de relógio real, com todas as abas visíveis. Compare a porcentagem de velocidade do Wokwi, os HTTP 200/201 e as falhas. HTTPS continua exigindo processamento; a otimização não garante quatro simuladores a 100% nem elimina falhas TLS. Essa validação precisa ser feita no computador que executa o Wokwi.
