# RTOS

Um RTOS, ou Real-Time Operating System, é um sistema operacional projetado para executar tarefas com requisitos de tempo real, ou seja, com previsibilidade e controle sobre prazos de execução. Em aplicações embarcadas, isso é especialmente importante quando há sensores, processamento de sinais, comunicação e controle em paralelo.

Nesse sentido, um RTOS organiza a execução em tarefas concorrentes e fornece mecanismos de agendamento, sincronização e comunicação entre elas. Isso permite que diferentes blocos do sistema funcionem ao mesmo tempo sem interferirem uns nos outros.

Como o firmware do projeto precisa realizar várias operações ao mesmo tempo:

- capturar áudio continuamente do microfone;
- armazenar esse sinal em buffer circular;
- extrair features em janelas de tempo;
- executar inferência do modelo de classificação;
- acender LEDs conforme a decisão;
- manter a comunicação com o monitor serial.

Se tudo fosse executado em um único fluxo sequencial, a leitura do microfone poderia atrapalhar o processamento da classificação, e a resposta do sistema ficaria mais lenta e menos estável. O uso de RTOS permite separar essas atividades em tarefas distintas, cada uma com prioridade e sincronização apropriadas.

Esse tipo de arquitetura é ideal para projetos embarcados com processamento contínuo de sinais, pois torna o sistema mais organizado, previsível e escalável.

## FreeRTOS

O FreeRTOS é um RTOS de código aberto, leve e muito utilizado em sistemas embarcados. Ele oferece recursos como:

- criação de tarefas;
- agendamento por prioridade;
- filas para troca de mensagens;
- semáforos para sincronização;
- temporizadores e notificações;
- baixo consumo de memória e boa compatibilidade com microcontroladores.

No contexto do ESP32, o FreeRTOS é a base do ambiente de execução do Arduino e de muitas aplicações de IoT e processamento embarcado. Ele permite que o firmware execute múltiplas tarefas de forma eficiente, sem perder determinismo e sem depender de um sistema operacional completo como Linux.

## Arquitetura RTOS aplicada ao firmware

O firmware usa FreeRTOS com sincronização por semáforos e filas.

### Tarefa de captura de áudio

#### Nome
- `tarefaCapturaAudio`

#### Função
- lê blocos continuamente do microfone;
- escreve os dados em um buffer circular;
- notifica a tarefa seguinte quando há dados suficientes para processamento.

#### Características
- prioridade alta;
- operação contínua;
- foco em aquisição do sinal sem atrasos excessivos.

### Tarefa de extração de features

#### Nome
- `tarefaExtracaoFeatures`

#### Função
- processa as janelas do buffer circular;
- calcula as features do sinal;
- atualiza o estado compartilhado;
- envia as features para a fila de inferência.

Essas features servem como descritores do conteúdo acústico da janela de análise e são utilizadas como entrada para o classificador.

### Tarefa de detecção

#### Nome
- `tarefaDeteccaoAnomalia`

#### Função
- recebe as features da fila;
- executa a inferência do modelo pré-treinado;
- compara a probabilidade da classe detectada com o threshold;
- aciona LEDs conforme o resultado;
- publica o resultado no monitor serial.

## Sincronização e comunicação entre tarefas

A coordenação entre as tarefas foi implementada com os mecanismos do FreeRTOS:

- `mutexFeatures`: protege o acesso a `featuresAtuais`;
- `xQueueSend` e `xQueueReceive`: trocam dados entre extração e detecção;
- `xTaskNotifyGive`: sinaliza a tarefa de features quando há dados novos;
- `TaskHandle_t`: identifica e controla cada tarefa.

Essa organização evita conflitos de concorrência e separa as responsabilidades de cada parte do pipeline.

## Modelagem do sinal e extração de features

A classificação é baseada em features acústicas calculadas pela classe `FeatureExtractor`, presente em `features.cpp`.

## Fluxo geral do sistema

O firmware realiza o seguinte fluxo:

1. captação do áudio pelo microfone;
2. armazenamento em buffer circular;
3. segmentação em janelas;
4. extração de features;
5. envio das features para fila de processamento;
6. inferência do modelo;
7. comparação com o limiar;
8. atualização do estado dos LEDs;
9. impressão das métricas no monitor serial.

Esse fluxo é o que torna o sistema um detector embarcado de latido em tempo real, funcionando de forma contínua e em paralelo.

## Conclusão

O uso do FreeRTOS foi fundamental para viabilizar a execução concorrente das funções críticas do projeto no ESP32. Em vez de uma solução monolítica e sequencial, a arquitetura em tarefas com prioridade e sincronização permitiu separar claramente três operações essenciais: captura, processamento e decisão.

Essa organização melhora a responsividade do sistema, reduz o risco de conflitos de memória e facilita a manutenção do firmware. Em projetos embarcados com processamento de sinais e classificação em tempo real, o RTOS é uma escolha natural e eficiente.
