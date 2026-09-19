# Ponderada Edge Computing

Este projeto implementa um sistema embarcado para detecção de anomalias acústicas usando um ESP32, microfone INMP441 e arquitetura multitarefa com FreeRTOS. O objetivo é capturar áudio, extrair características do sinal, classificar o evento com um modelo pré-treinado e sinalizar o resultado por meio de LEDs.

## Objetivo do sistema

Considerando o objetivo da atividade acima descrito, esse projeto implementa a detecção de latidos de cachorro: caso haja latidos o led vermelho é aceso, caso não haja, o led verde. Desse modo, é possível monitorar o comportamento dos cachorros mesmo sem um ser humano presente e saber se tem algo de errado com os pets.

## Estrutura do projeto

A estrutura do repositório está organizada da seguinte forma:

- `src/sketch/` : firmware para ESP32 em Arduino/FreeRTOS
- `src/modelo/` : notebook e modelo ONNX treinado
- `docs` : documentação do desenvolvimento

### Fluxo geral

1. O microfone lê amostras contínuas via I2S.
2. O buffer circular guarda os dados para processamento.
3. A tarefa de features extrai janelas de áudio e calcula métricas.
4. A tarefa de detecção recebe as features por fila.
5. O modelo classifica o evento.
6. Os LEDs indicam o resultado do sistema.

## Processo de implementação

Para o desenvolvimento dessa atividade segui os seguintes passos:
1. Montagem Física com a conexão dos sensores ao ESP32 conforme documentado em [MontagemFisica.md](./docs/MontagemFisica.md);
2. Criação das classes para utilização dos sensores conforme documentado em [Firmware.md](./docs/Firmware.md);
3. Leitura de áudio e extração de features, também documentado em [Firmware.md](./docs/Firmware.md);
4. Seleção do modelo e análise do ONNX conforme documentado em [TreinamentoModelo.md](./docs/TreinamentoModelo.md);
5. Implementação do modelo no firmware de acordo com [Firmware.md](./docs/Firmware.md);
6. Integração RTOS assim como descrito em [RTOS.md](./docs/RTOS.md);
7. Testes detalhados em [Testes.md](./docs/Testes.md).

## Modelos

Os arquivos .onnx podem ser acessados [aqui](https://drive.google.com/drive/folders/1EmpfFopQ1ySJdd6ZSA6xMGXmVehAg92K?usp=sharing).
