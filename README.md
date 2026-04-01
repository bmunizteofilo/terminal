# Terminal BMT

Terminal serial para Windows feito em C, pensado para uso no terminal integrado do VS Code com `gcc` e `gdb`.

## Recursos

- Configuracao completa de porta serial
- Monitor serial em modo `raw` ou `esp32`
- Colorizacao de logs no padrao do ESP-IDF
- Filtro por nivel de log do ESP32
- Timestamp opcional nas linhas recebidas
- Envio manual de texto e bytes hexadecimais
- Gravacao da saida em arquivo
- Reconexao automatica da porta serial
- Salvamento de configuracao em `bmt_config.ini`
- Autosave opcional das alteracoes
- Historico de comandos da sessao
- Pause e resume do monitor com fila em memoria
- Atalho `Ctrl+Shift+T` para sair do monitor serial

## Estrutura

- `main/main.c`: implementacao principal
- `README.md`: documentacao do projeto
- `bmt_config.ini`: configuracao persistida
- `bmt_serial.log`: arquivo de log padrao
- `.vscode/tasks.json`: build com `gcc`
- `.vscode/launch.json`: debug com `gdb`

## Build

No terminal do VS Code:

```powershell
C:/mingw64/bin/gcc.exe -g .\main\main.c -o .\terminal_BMT.exe
```

Ou use `Ctrl+Shift+B` no VS Code.

## Execucao

```powershell
.\terminal_BMT.exe
```

## Fluxo basico

1. Escolha a porta com `bmt -com COM12`
2. Ajuste baud e demais parametros se necessario
3. Use `bmt -start` para iniciar a escuta
4. Durante o monitor serial:
   - pressione `p` para pausar a exibicao
   - pressione `r` para retomar e despejar a fila acumulada
   - pressione `Ctrl+Shift+T` para sair do monitor e voltar ao prompt
5. Use `bmt -save` para persistir a configuracao

## Comandos

### Ajuda e informacao

- `bmt -list`
- `bmt -help <comando>`
- `bmt -status`
- `bmt -history`
- `bmt -about`
- `bmt -exit`

### Porta e configuracao serial

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

### Monitor serial

- `bmt -start`
- `bmt -stop`
- `Ctrl+Shift+T`
- `p`
- `r`
- `bmt -monitor raw|esp32`
- `bmt -timestamp on|off`
- `bmt -filter info|warning|error|debug|verbose|all`
- `bmt -logs esp32 on|off`
- `bmt -logs dma on|off`
- `bmt -reconnect on|off`
- `bmt -theme`
- `bmt -theme default|light|highcontrast`

### Envio de dados

- `bmt -send <texto>`
- `bmt -sendhex <hex>`

Exemplos:

```text
bmt -send reboot
bmt -sendhex AA 55 01 FF
```

### Arquivos e persistencia

- `bmt -save`
- `bmt -reload`
- `bmt -reset`
- `bmt -autosave on|off`
- `bmt -logfile`
- `bmt -logfile on|off`
- `bmt -logfile path <arquivo>`

## Modos de monitor

### `raw`

Mostra os dados recebidos como chegaram na serial. Nesse modo, o terminal nao interpreta nivel de log do ESP32 para filtro por severidade.

### `esp32`

Interpreta linhas no formato tipico do ESP-IDF, por exemplo:

```text
I (1234) wifi: started
W (5678) adc: overload
E (9999) uart: framing error
```

Nesse modo, o terminal pode:

- aplicar cores por nivel
- filtrar por nivel
- usar os temas visuais definidos para os logs
- mostrar timestamp local

## Pause e Resume

Durante a comunicacao serial ativa:

- pressione `p` para pausar a exibicao
- enquanto estiver pausado, os eventos continuam sendo guardados em uma fila em memoria
- pressione `r` para retomar
- ao retomar, a fila acumulada e despejada na tela antes de voltar ao fluxo em tempo real

Observacao:

- a fila de pausa usa um buffer grande em memoria para reduzir perdas durante a navegacao
- se esse buffer atingir o limite, o terminal informa quantos eventos nao puderam ser armazenados

## Persistencia

As configuracoes podem ser salvas em `bmt_config.ini` com `bmt -save`.

Se `bmt -autosave on` estiver ativo, cada alteracao de configuracao ja e persistida automaticamente.

Itens persistidos:

- porta COM
- baud rate
- paridade
- data bits
- stop bits
- flow control
- modo de monitor
- filtro ESP32
- logs coloridos
- timestamp
- logfile e caminho do logfile
- autosave
- reconnect
- tema

## Log em arquivo

Quando `bmt -logfile on` estiver ativo, cada linha exibida pelo monitor tambem e gravada no arquivo configurado.

Comandos principais:

```text
bmt -logfile
bmt -logfile on
bmt -logfile off
bmt -logfile path logs\serial.txt
```

## Temas

Os temas afetam a colorizacao dos logs no modo `esp32` quando `bmt -logs esp32 on` estiver habilitado.

Temas disponiveis:

- `default`
- `light`
- `highcontrast`

## Atalho DMA

Para agilizar a visualizacao de logs de DMA no padrao ESP32:

```text
bmt -logs dma on
```

Esse comando ativa ao mesmo tempo:

- `bmt -monitor esp32`
- `bmt -logs esp32 on`

Para desfazer o preset:

```text
bmt -logs dma off
```

Esse comando retorna o monitor para `raw` e desliga a colorizacao de logs ESP32.

## Observacoes

- O projeto e voltado para Windows
- O monitor serial usa `CreateFile`, `ReadFile`, `WriteFile` e configuracao via `DCB`
- O atalho `Ctrl+Shift+T` so vale durante a comunicacao serial ativa
- `p` e `r` tambem so valem durante o monitor serial ativo
- para ver colorizacao por nivel do ESP32, use `bmt -monitor esp32`
