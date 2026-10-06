# Vários equipamentos no Wokwi do VS Code

A instância atual continua intacta. Para gerar duas cópias independentes do circuito e do firmware:

```bash
python firmware/esp32/preparar_wokwi.py --quantidade 2
```

Abra `firmware/esp32/.wokwi-instances/esp32-001` em uma janela do VS Code e `esp32-002` em outra (Arquivo → Nova Janela → Abrir Pasta). Abra cada pasta individualmente, sem reunir as duas em um workspace. Na máquina que já executa Wokwi e PlatformIO, em cada janela:

1. Abra o terminal e execute `pio run`.
2. Abra a paleta de comandos e execute `Wokwi: Start Simulator`.
3. Mantenha ambas as simulações rodando. Os controles dos sensores são independentes; altere um potenciômetro em cada janela para verificar.

Cada projeto compila só `main.cpp`, usa seu próprio `.pio`, `diagram.json` e `wokwi.toml`, e envia um ID fixo diferente (`esp32-001`, `esp32-002`). Todos usam a URL de API configurada no firmware original. Para salvar os IDs, publique primeiro a API que aceita `equipamento_id` e cria a coluna correspondente; ela precisa de permissão ALTER no MySQL. Sem esse deploy, a API antiga poderá aceitar as leituras mas ignorar os IDs.

Os projetos gerados são cópias: alterações posteriores no firmware original não se propagam automaticamente. Para alterar delays em uma instância, edite seu `src/main.cpp`, compile novamente e reinicie aquela simulação. Para aplicar alterações novas do original a todas, gere em outra pasta com `--saida`. O gerador recusa sobrescrever projetos existentes.

Depois de validar dois, gere quatro em outro destino:

```bash
python firmware/esp32/preparar_wokwi.py --quantidade 4 --saida firmware/esp32/.wokwi-instances/quatro
```

Pare o grupo anterior se quiser exatamente quatro equipamentos ativos, abra cada pasta gerada em uma janela e repita a compilação/inicialização. O mesmo comando aceita 8, 16 ou 32. Não misture grupos com IDs iguais no mesmo teste: a numeração recomeça em esp32-001 a cada geração.

Isso executa o firmware e sensores reais do simulador Wokwi, sem substituir por envios Python. As instâncias começam manualmente; ficam ativas simultaneamente, mas não começam no mesmo milissegundo. A conexão de cada uma precisa funcionar como na instância que você já usa. A disponibilidade de sessões simultâneas depende da extensão e de sua conta Wokwi; este ambiente cloud não possui a extensão gráfica e não validou essa capacidade.

O SCADA atual identifica o equipamento da última leitura global e do histórico; não captura todas as leituras entre consultas nem mede latências de todos os dispositivos. Verifique a persistência por equipamento com `GET /leituras?equipamento_id=esp32-001&limite=10`. Para descobrir capacidade da API com esses testes, registre falhas/tempos no monitor serial de cada instância e monitore o Render e o banco. O desempenho do computador com muitas instâncias Wokwi também pode limitar o teste.
