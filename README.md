# Terminal BMT

Terminal serial para Windows feito em C, pensado para uso no terminal integrado do VS Code com `gcc`/`gdb`.

## Recursos

- Configuracao completa de porta serial
- Monitor serial em modo `raw` ou `esp32`
- Colorizacao de logs do ESP32
- Filtro por nivel de log
- Timestamp opcional
- Envio manual de texto e hex
- Log em arquivo
- Reconexao automatica
- Salvamento de configuracao em `bmt_config.ini`
- Historico de comandos da sessao
- Atalho `Ctrl+Shift+T` para sair do monitor serial

## Estrutura

- `main/main.c`: implementacao principal
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
4. Use `Ctrl+Shift+T` para sair do modo de monitoramento
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
- `bmt -monitor raw|esp32`
- `bmt -timestamp on|off`
- `bmt -filter info|warning|error|debug|verbose|all`
- `bmt -logs esp32 on|off`
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

Mostra os dados recebidos sem interpretar o nivel de log.

### `esp32`

Interpreta linhas no formato tipico do ESP-IDF, como:

```text
I (1234) wifi: started
W (5678) adc: overload
E (9999) uart: framing error
```

Nesse modo, o terminal pode:

- aplicar cores por nivel
- filtrar por nivel
- mostrar timestamp local

## Persistencia

As configuracoes podem ser salvas em `bmt_config.ini` com `bmt -save`.

Se `bmt -autosave on` estiver ativo, cada alteracao de configuracao ja e persistida automaticamente.

## Observacoes

- O projeto e voltado para Windows
- O monitor serial usa `CreateFile`, `ReadFile`, `WriteFile` e configuracao via `DCB`
- O atalho `Ctrl+Shift+T` so vale durante a comunicacao serial ativa
