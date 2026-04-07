# Terminal BMT

Aplicacao Windows em C para operacao de serial, CAN/SLCAN, dashboards locais em HTML e monitoramento de sistemas embarcados.

O projeto foi pensado para uso tecnico real em bancada e em desenvolvimento automotivo/embarcado, mantendo dois modos de operacao ao mesmo tempo:
- `CLI` para controle rapido, scripts, diagnostico e fallback tecnico
- `UI Web` local para operacao visual, dashboards, monitores e configuracao assistida

## Visao Geral

O Terminal BMT concentra, em um unico executavel, estas capacidades:

- terminal serial completo para Windows
- monitor serial bruto e monitor serial com interpretacao de logs ESP32
- suporte a CAN via adaptadores compativeis com `SLCAN`, como CANable
- parser DBC para decodificacao de sinais CAN
- dashboard web local da `VCU`
- dashboard web local dos `Inverters`
- monitor CAN bruto em interface web
- monitor serial bruto em interface web
- pagina web de configuracao para serial, CAN e DBC
- persistencia de configuracao em arquivo local

## Principais Recursos

### CLI

- prompt interativo `bmt>`
- ajuda detalhada por comando
- historico de comandos da sessao
- status completo do sistema
- configuracao persistente

### Serial

- selecao de `COM`
- `baud rate`
- `data bits`
- `parity`
- `stop bits`
- `flow control`
- timestamp local opcional
- log em arquivo
- reconexao automatica
- envio manual de texto
- envio manual de bytes em hexadecimal
- monitor `raw`
- monitor `esp32`
- pause e resume de exibicao com buffer em memoria

### ESP32 Logs

- reconhecimento do padrao de logs do `ESP-IDF`
- colorizacao por nivel
- filtro por severidade
- temas visuais para contraste

### CAN / SLCAN

- suporte a adaptadores CAN via `SLCAN`
- configuracao de `COM` e bitrate
- modos de exibicao `raw` e `table`
- envio de frames CAN padrao de 11 bits
- filtros por ID
- estatisticas de sessao
- logs em arquivo

### DBC

- carregamento separado de DBC da `VCU`
- carregamento separado de DBC dos `Inverters`
- persistencia dos caminhos dos DBCs
- informacoes resumidas de mensagens e sinais carregados

### UI Web Local

- servidor HTTP local embutido
- splash screen de abertura
- dashboard `VCU`
- dashboard `Inverters`
- `Serial Monitor`
- `CAN Monitor`
- `Settings`
- pagina `About`

### Dashboards

- `VCU Dashboard` com estados, entradas, health, faults e comunicacao
- `Inverters Dashboard` com dois lados espelhados, para dois inversores com mesma estrutura e bases de ID diferentes
- deteccao de `stale/timeout` para dados que pararam de atualizar

### Monitores Web

- `Serial Monitor` com:
  - stream bruto
  - pause de visualizacao
  - resume
  - clear
  - autoscroll
  - wrap
  - busca textual
  - export de log

- `CAN Monitor` com:
  - stream bruto de frames
  - destaque de `RX` e `TX`
  - tipo de frame
  - filtro por direcao
  - filtro por ID
  - busca textual
  - export de log

### Settings Web

- aplicacao de configuracoes seriais
- aplicacao de configuracoes CAN
- carregamento de DBC
- autosave
- controle de `open`, `start`, `stop` e `close`
- deteccao automatica de portas COM

## Arquitetura do Projeto

### Nucleo

- [`main/app/main.c`](main/app/main.c)
  ponto de entrada

- [`main/app/terminal.c`](main/app/terminal.c)
  loop principal, parser de comandos, integracao de serial, CAN, DBC e UI web

- [`main/app/terminal.h`](main/app/terminal.h)
  interface publica do terminal

### CAN

- [`main/can/can_types.h`](main/can/can_types.h)
  tipos, estruturas e configuracoes do subsistema CAN

- [`main/can/can_slcan.h`](main/can/can_slcan.h)
  interface do backend SLCAN

- [`main/can/can_slcan.c`](main/can/can_slcan.c)
  implementacao do backend CAN/SLCAN

### DBC

- [`main/dbc/dbc_types.h`](main/dbc/dbc_types.h)
  tipos do parser DBC

- [`main/dbc/dbc_parser.h`](main/dbc/dbc_parser.h)
  interface do parser DBC

- [`main/dbc/dbc_parser.c`](main/dbc/dbc_parser.c)
  parser DBC e decodificacao de sinais

### UI e Dashboards

- [`main/ui/vcu_dashboard.h`](main/ui/vcu_dashboard.h)
- [`main/ui/vcu_dashboard.c`](main/ui/vcu_dashboard.c)
  runtime do dashboard VCU

- [`main/ui/inverter_dashboard.h`](main/ui/inverter_dashboard.h)
- [`main/ui/inverter_dashboard.c`](main/ui/inverter_dashboard.c)
  runtime do dashboard de inversores

- [`main/ui/serial_monitor.h`](main/ui/serial_monitor.h)
- [`main/ui/serial_monitor.c`](main/ui/serial_monitor.c)
  buffer circular e suporte aos monitores web

- [`main/ui/web_ui.h`](main/ui/web_ui.h)
- [`main/ui/web_ui.c`](main/ui/web_ui.c)
  servidor HTTP local, API e HTML/CSS/JS embutidos

## Requisitos

- Windows
- `gcc` MinGW
- opcionalmente VS Code para build e debug
- para CAN: adaptador compativel com `SLCAN`, como CANable

## Build

### Compilacao manual

```powershell
C:/mingw64/bin/gcc.exe -I./main/app -I./main/can -I./main/dbc -I./main/ui -g ./main/app/main.c ./main/app/terminal.c ./main/can/can_slcan.c ./main/dbc/dbc_parser.c ./main/ui/inverter_dashboard.c ./main/ui/serial_monitor.c ./main/ui/vcu_dashboard.c ./main/ui/web_ui.c -lws2_32 -lshell32 -o ./terminal_BMT.exe
```

### Compilacao pelo VS Code

Use:

```text
Ctrl+Shift+B
```

## Execucao

### Pelo terminal

```powershell
.\terminal_BMT.exe
```

### Por duplo clique

O executavel foi preparado para abrir em console Windows e permanecer ativo aguardando comandos.

## Primeira Utilizacao

Ao abrir, o terminal apresenta banner e prompt `bmt>`.

O fluxo mais comum e:

1. configurar serial ou CAN
2. salvar configuracao se desejar
3. ativar a UI web, se quiser dashboards e monitores visuais
4. iniciar a interface escolhida

## Comandos CLI

## Comandos Gerais

- `bmt -list`
- `bmt -help <comando>`
- `bmt -status`
- `bmt -history`
- `bmt -about`
- `bmt -clear`
- `bmt -exit`

## Configuracao Serial

- `bmt -com`
- `bmt -com refresh`
- `bmt -com COMx`
- `bmt -baud`
- `bmt -baud <valor>`
- `bmt -parity`
- `bmt -parity none|odd|even|mark|space`
- `bmt -databits`
- `bmt -databits 5|6|7|8`
- `bmt -stopbits`
- `bmt -stopbits 1|1.5|2`
- `bmt -flow`
- `bmt -flow none|xonxoff|rtscts|dsrdtr`
- `bmt -timestamp on|off`
- `bmt -monitor raw|esp32`
- `bmt -filter info|warning|error|debug|verbose|all`
- `bmt -logs esp32 on|off`
- `bmt -theme`
- `bmt -theme default|light|highcontrast`
- `bmt -reconnect on|off`

## Operacao Serial

- `bmt -start`
- `bmt -stop`
- `bmt -send <texto>`
- `bmt -sendhex <hex>`

Durante o monitor serial:

- `p` pausa a exibicao
- `r` retoma a exibicao
- `Ctrl+Shift+T` encerra o monitor e volta ao prompt

## Configuracao CAN

- `bmt -can`
- `bmt -can help`
- `bmt -can list`
- `bmt -can com`
- `bmt -can com COMx`
- `bmt -can bitrate`
- `bmt -can bitrate <valor>`
- `bmt -can view raw|table`
- `bmt -can timestamp on|off`
- `bmt -can color on|off`
- `bmt -can filter id <hex>`
- `bmt -can filter clear`
- `bmt -can serialmonitor on|off`
- `bmt -can stats`
- `bmt -can save`
- `bmt -can reload`

## Operacao CAN

- `bmt -can open`
- `bmt -can start`
- `bmt -can stop`
- `bmt -can close`
- `bmt -can send <id> <bytes...>`

Durante o monitor CAN:

- `p` pausa a exibicao
- `r` retoma a exibicao
- `Ctrl+Shift+T` encerra o monitor e volta ao prompt

## DBC

- `bmt -can dbc info`
- `bmt -can dbc load vcu <arquivo>`
- `bmt -can dbc load inverter <arquivo>`
- `bmt -can dbc unload vcu`
- `bmt -can dbc unload inverter`

## Inversores

- `bmt -can inverter status`
- `bmt -can inverter base left <id>`
- `bmt -can inverter base right <id>`

## UI Web

- `bmt -ui status`
- `bmt -ui on`
- `bmt -ui off`
- `bmt -ui open`
- `bmt -ui port <valor>`

## Persistencia

- `bmt -save`
- `bmt -reload`
- `bmt -reset`
- `bmt -autosave on|off`
- `bmt -logfile`
- `bmt -logfile on|off`
- `bmt -logfile path <arquivo>`

## Exemplos de Uso

### Monitor Serial Basico

```text
bmt -com COM12
bmt -baud 115200
bmt -start
```

### Monitor Serial com Logs ESP32

```text
bmt -monitor esp32
bmt -logs esp32 on
bmt -filter all
bmt -start
```

### Envio Serial Manual

```text
bmt -send reboot
bmt -sendhex AA 55 01 FF
```

### CAN com SLCAN

```text
bmt -can com COM9
bmt -can bitrate 500000
bmt -can view table
bmt -can open
bmt -can start
```

### Carregar DBC da VCU

```text
bmt -can dbc load vcu dbc\vcu_status.dbc
```

### Carregar DBC dos Inversores

```text
bmt -can dbc load inverter DBC_CAN_Inverter.dbc
```

### Configurar Bases dos Inversores

```text
bmt -can inverter base left 0x500
bmt -can inverter base right 0x600
```

### Subir a UI Web

```text
bmt -ui on
bmt -ui open
```

## UI Web

Quando a UI esta ativa, o Terminal BMT sobe um servidor HTTP local em:

```text
http://127.0.0.1:<porta>
```

A pagina e aberta no navegador padrao com:

```text
bmt -ui open
```

## Estrutura da UI

### Dashboard

Subareas:

- `VCU`
- `Inverters`

### Serial Monitor

Monitor bruto da serial em interface web.

### CAN Monitor

Monitor bruto do barramento CAN em interface web.

### Settings

Tela de configuracao assistida para serial, CAN, DBC e persistencia.

### About

Janela de apoio com explicacoes do painel.

## Dashboard VCU

O dashboard VCU usa o DBC carregado para VCU e exibe:

- estado da VCU
- origem de comando
- torque liberado
- torque limitado
- health de throttle e brake
- long request
- status de pedais
- flags de funcionalidade
- faults
- heartbeat e comunicacao

O topo do dashboard inclui:

- estado do CAN
- estado do DBC da VCU
- freshness dos dados
- ultimo update
- acoes rapidas para abrir, iniciar e parar o CAN

## Dashboard Inverters

O dashboard Inverters foi projetado para dois inversores com mesma estrutura de mensagens e bases de ID diferentes.

Ele mostra dois paineis espelhados:

- `Inverter A`
- `Inverter B`

Cada lado exibe:

- state
- event
- control mode
- direction
- speed
- speed ref
- torque ref
- DC voltage
- AC current
- AC voltage
- temp motor
- temp aux
- temp IGBT
- faults principais
- estado dos gate drivers

O topo do dashboard inclui:

- estado do CAN
- estado do DBC dos inversores
- base esquerda
- base direita
- ultimo update

## Serial Monitor Web

A aba `Serial Monitor` espelha os dados recebidos na serial, sem depender de protocolo estruturado.

Recursos:

- stream de linhas
- pause de visualizacao
- resume
- clear
- autoscroll
- wrap
- busca textual
- export de log

Observacao:

- o pause da UI pausa apenas a visualizacao web
- a recepcao serial do backend continua ativa

## CAN Monitor Web

A aba `CAN Monitor` mostra frames CAN brutos que nao estao sendo tratados como dashboard VCU/Inverters ou que voce deseja acompanhar diretamente.

Recursos:

- destaque por `RX` e `TX`
- tipo de frame
- filtro por direcao
- filtro por ID
- busca textual
- clear
- export de log

### Regra de prioridade da UI

Quando:

```text
bmt -can serialmonitor on
```

acontece o seguinte:

- `CAN Monitor` fica ativo
- `VCU` fica com overlay `off`
- `Inverters` fica com overlay `off`

Quando:

```text
bmt -can serialmonitor off
```

os dashboards `VCU` e `Inverters` voltam ao funcionamento normal.

## Settings Web

### Serial

Permite configurar:

- COM
- baud rate
- data bits
- parity
- stop bits
- flow control
- timestamp
- log em arquivo
- caminho do log
- reconnect

Tambem oferece:

- `Apply Serial`
- `Start`
- `Stop`

### CAN Interface

Permite configurar:

- COM
- bitrate

Tambem oferece:

- `Apply CAN`
- `Open`
- `Start`
- `Stop`
- `Close`

### DBC

Permite:

- carregar DBC
- descarregar DBC
- visualizar status, versao, quantidade de mensagens e sinais

### Persistence

Permite:

- habilitar ou desabilitar autosave
- salvar configuracao atual

### Deteccao automatica de COM

A UI realiza varredura automatica de portas COM e tambem possui botao manual `Refresh COMs`.

O estado visual mostra:

- portas detectadas
- horario da ultima varredura
- feedback de sucesso ou falha

## Timeouts e Freshness

Os dashboards `VCU` e `Inverters` identificam quando os dados pararam de atualizar.

Quando isso ocorre:

- o estado visual pode mudar para `STALE`
- a interface destaca que o dado nao esta mais fresco

Isso ajuda a separar claramente:

- problema de comunicacao
- CAN parado
- dado antigo
- valor realmente valido

## Persistencia de Configuracao

As configuracoes sao gravadas em:

- [`bmt_config.ini`](bmt_config.ini)

Podem ser salvas manualmente com:

```text
bmt -save
```

Ou automaticamente com:

```text
bmt -autosave on
```

Itens persistidos incluem:

- configuracao serial
- preferencias de monitor serial
- caminhos de logfile
- configuracao CAN
- filtros CAN
- caminhos dos DBCs
- porta da UI
- estado do CAN Monitor na UI
- bases configuradas dos inversores

## Arquivos de Log

Arquivos padrao:

- `bmt_serial.log`
- `bmt_can.log`

Na UI web tambem e possivel exportar:

- log atual do `Serial Monitor`
- log atual do `CAN Monitor`

## DBCs do Projeto

Exemplos presentes no workspace:

- [`dbc/vcu_status.dbc`](dbc/vcu_status.dbc)
- [`DBC_CAN_Inverter.dbc`](DBC_CAN_Inverter.dbc)

## Observacoes Operacionais

- o projeto e focado em Windows
- a UI web e local, servida pelo proprio executavel
- a CLI nunca deixa de ser a interface principal
- a UI foi desenhada para facilitar operacao, mas nao substitui o terminal tecnico
- o monitor CAN atual foi projetado para adaptadores compativeis com `SLCAN`
- o envio CAN atual cobre frames padrao de 11 bits

## Solucao de Problemas

### O executavel abre e fecha

Use o binario principal:

- [`terminal_BMT.exe`](terminal_BMT.exe)

Ele deve abrir console e permanecer aguardando comandos.

### A UI nao abre

Verifique:

- `bmt -ui on`
- `bmt -ui open`
- se a porta HTTP configurada nao esta ocupada

### O dashboard nao atualiza

Verifique:

- se o CAN foi aberto
- se o barramento foi iniciado
- se o DBC correto foi carregado
- se os IDs esperados realmente estao presentes no barramento
- se o estado nao esta `STALE`

### VCU ou Inverters estao escurecidos

Verifique se:

```text
bmt -can serialmonitor on
```

esta habilitado.

Se estiver, os dashboards `VCU` e `Inverters` ficam bloqueados na UI ate o `CAN Monitor` ser desativado.

## Roadmap Natural

O projeto ja esta pronto para uso tecnico real, e os proximos ganhos mais naturais sao:

- presets de projeto
- configuracao das bases dos inversores diretamente na UI
- layouts visuais adicionais
- suporte a mais DBCs por dominio
- refinamentos de UX e exportacao

## Licenciamento e Uso

Este repositório foi estruturado para desenvolvimento interno, validacao em bancada e operacao assistida de sistemas embarcados e automotivos.

Se o projeto for evoluir para distribuicao externa, a recomendacao e complementar este `README` com:

- licenca formal
- changelog
- guia de release
- politica de versao
