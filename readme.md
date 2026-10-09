# Perícia Forense Digital — EstufaSmart

### SENAI Dev Experience | Módulo B — IoT e Cibersegurança

## Sobre o Projeto

Este projeto foi desenvolvido durante o **SENAI Dev Experience**, com o objetivo de investigar um incidente de superaquecimento em uma estufa inteligente (EstufaSmart), utilizando o **Wireshark** para analisar o tráfego de rede e identificar possíveis falhas de segurança.

A investigação permitiu identificar um ataque cibernético responsável pelo desligamento indevido do sistema de ventilação da estufa.

## Objetivos

- Analisar pacotes de rede utilizando o Wireshark.
- Identificar protocolos e dispositivos envolvidos na comunicação.
- Detectar vulnerabilidades em conexões HTTP e MQTT.
- Investigar comandos suspeitos e reconstruir o incidente.
- Propor medidas para melhorar a segurança do sistema.

## Tecnologias Utilizadas

| Tecnologia | Finalidade |
|---|---|
| Wireshark | Análise de tráfego de rede |
| MQTT | Comunicação entre dispositivos IoT |
| HTTP | Comunicação com o painel web |
| TCP/IP | Comunicação e endereçamento |
| JSON | Estruturação dos dados |
| Arduino UNO R4 WiFi | Microcontrolador da estufa |

## Investigação Realizada

Durante a análise do arquivo `captura_estufasmart_2026.pcap`, foram identificadas credenciais transmitidas sem criptografia e falhas no controle de acesso do broker MQTT.

Também foi localizado um comando malicioso enviado por um dispositivo não autorizado:

```json
{
  "cmd": "EXAUSTOR_OFF",
  "cmd_id": "k12"
}
```

O comando desligou o exaustor da estufa, contribuindo para a elevação da temperatura de **29,5 °C para 35,1 °C**, colocando o sistema em estado crítico.