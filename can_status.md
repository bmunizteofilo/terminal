# CAN Status Dictionary

Este documento descreve o mini-dicionario CAN proposto para a VCU usando apenas dois IDs periodicos:

- `0x100`: estado consolidado da VCU
- `0x101`: comando longitudinal validado do motorista

A ideia central e manter a interface CAN orientada a produto:

- `ID 1` responde: em que condicao a VCU esta e se o torque esta autorizado
- `ID 2` responde: qual e o comando longitudinal pedido pelo motorista

## Objetivos

- reduzir a quantidade de frames para o conjunto essencial
- separar estado global da VCU do comando longitudinal do motorista
- publicar somente sinais validados pela aplicacao
- manter espaco para expansao futura sem quebrar compatibilidade

## Frame 1

`BO_ 256 VCU_Status: 8 VCU`

Periodicidade sugerida: `20 ms`

### Sinais

```text
SG_ VCU_State                   : 0|4@1+ (1,0) [0|15] "" Vector__XXX
SG_ VCU_CommandSource           : 4|2@1+ (1,0) [0|3]  "" Vector__XXX
SG_ VCU_TorqueAllowed           : 6|1@1+ (1,0) [0|1]  "" Vector__XXX
SG_ VCU_TorqueLimited           : 7|1@1+ (1,0) [0|1]  "" Vector__XXX

SG_ VCU_ThrottleHealth          : 8|2@1+ (1,0) [0|3]  "" Vector__XXX
SG_ VCU_BrakeHealth             : 10|2@1+ (1,0) [0|3] "" Vector__XXX
SG_ VCU_RegenEnabled            : 12|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_YawEnabled              : 13|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_TractionEnabled         : 14|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_BrakeDominanceHard      : 15|1@1+ (1,0) [0|1] "" Vector__XXX

SG_ VCU_HwFault                 : 16|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_CanFault                : 17|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_InverterHandshakeFault  : 18|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_RemoteAuthorized        : 19|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_TcuHeartbeatOk          : 20|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_HpcHeartbeatOk          : 21|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_StartupFailed           : 22|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_ControlledResetReq      : 23|1@1+ (1,0) [0|1] "" Vector__XXX

SG_ VCU_ThrottleState           : 24|2@1+ (1,0) [0|3] "" Vector__XXX
SG_ VCU_BrakeState              : 26|2@1+ (1,0) [0|3] "" Vector__XXX
SG_ VCU_DriverTakeover          : 28|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_InverterHandshakeOk     : 29|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_ReservedFlags           : 30|2@1+ (1,0) [0|3] "" Vector__XXX

SG_ VCU_AliveCounter            : 32|4@1+ (1,0) [0|15] "" Vector__XXX
SG_ VCU_StatusVersion           : 36|4@1+ (1,0) [0|15] "" Vector__XXX
SG_ VCU_Reserved                : 40|24@1+ (1,0) [0|0] "" Vector__XXX
```

### Enums

```text
VAL_ 256 VCU_State
 0 "INIT_STARTUP_CHECK"
 1 "NORMAL_DRIVER_ACTIVE"
 2 "DEGRADED_YAW_OFF"
 3 "DEGRADED_REGEN_OFF"
 4 "LIMP_TORQUE_LIMITED"
 5 "SAFE_STATE"
 6 "REMOTE_CONTROL_ACTIVE"
 7 "AUTONOMOUS_ACTIVE";

VAL_ 256 VCU_CommandSource
 0 "NONE"
 1 "DRIVER"
 2 "TCU"
 3 "HPC";

VAL_ 256 VCU_ThrottleHealth
 0 "OK"
 1 "DEGRADED_USE_A"
 2 "DEGRADED_USE_B"
 3 "CRITICAL";

VAL_ 256 VCU_BrakeHealth
 0 "OK"
 1 "DEGRADED_USE_A"
 2 "DEGRADED_USE_B"
 3 "CRITICAL";

VAL_ 256 VCU_ThrottleState
 0 "FAULT"
 1 "IDLE"
 2 "ACCEL"
 3 "FULL";

VAL_ 256 VCU_BrakeState
 0 "FAULT"
 1 "RELEASED"
 2 "BRAKING"
 3 "FULL";
```

### Comentario de engenharia

Este frame responde se a VCU esta pronta para entregar torque, em qual estado operacional ela se encontra e quais degradacoes ou falhas relevantes estao ativas.

## Frame 2

`BO_ 257 VCU_Pedals: 8 VCU`

Periodicidade sugerida: `10 ms`

### Sinais

```text
SG_ VCU_LongitudinalRequest     : 0|16@1- (1,0) [-100|100] "%" Vector__XXX
SG_ VCU_ThrottleRawHealth       : 32|2@1+ (1,0) [0|3] "" Vector__XXX
SG_ VCU_BrakeRawHealth          : 34|2@1+ (1,0) [0|3] "" Vector__XXX
SG_ VCU_PedalsValid             : 36|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_PedalsTxEnabled         : 37|1@1+ (1,0) [0|1] "" Vector__XXX
SG_ VCU_PedalsReservedFlags     : 38|2@1+ (1,0) [0|3] "" Vector__XXX
SG_ VCU_PedalsAliveCounter      : 40|4@1+ (1,0) [0|15] "" Vector__XXX
SG_ VCU_PedalsReserved          : 44|20@1+ (1,0) [0|0] "" Vector__XXX
```

### Enums

```text
VAL_ 257 VCU_ThrottleRawHealth
 0 "OK"
 1 "DEGRADED_USE_A"
 2 "DEGRADED_USE_B"
 3 "CRITICAL";

VAL_ 257 VCU_BrakeRawHealth
 0 "OK"
 1 "DEGRADED_USE_A"
 2 "DEGRADED_USE_B"
 3 "CRITICAL";
```

### Comentario de engenharia

Este frame responde qual e o comando longitudinal pedido pelo motorista, sempre com valor validado pela camada de aplicacao.

## Regras de interpretacao

- `VCU_TorqueAllowed` e o sinal principal para qualquer consumidor externo
- `VCU_State` explica o contexto operacional do sistema
- `VCU_LongitudinalRequest` usa faixa assinada de `-100` a `100`
- valores positivos representam acelerador
- valores negativos representam freio
- se acelerador e freio forem pressionados ao mesmo tempo, o freio tem prioridade
- o valor bruto transmitido no CAN e exatamente `-100 .. 100` em `16 bits`
- `VCU_PedalsValid=0` indica que os valores nao devem ser usados como referencia confiavel
- `VCU_PedalsTxEnabled=0` diferencia pedal zerado de publicacao inibida pela VCU

## Notas de implementacao

- a implementacao proposta foi movida para `src/App/Cmd_Can/can_status.c` e `src/App/Cmd_Can/can_status.h`
- o contador `alive` ocupa 4 bits em cada frame
- a versao do protocolo ocupa 4 bits no frame `VCU_Status`
