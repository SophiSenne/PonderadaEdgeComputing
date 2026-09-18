## Arquitetura RTOS

O firmware usa FreeRTOS com sincronização por semáforos e filas.

### Tarefas principais

#### 1. Captura de áudio
- Prioridade alta
- Lê blocos continuamente do microfone
- Escreve em buffer circular
- Notifica a tarefa de features quando há dados suficientes

#### 2. Extração de features
- Prioridade média
- Processa janelas do buffer circular
- Calcula:
  - RMS
  - nível em dB
  - spectral centroid
  - MFCCs
- Atualiza estado compartilhado
- Envia as features para fila de inferência

#### 3. Detecção de anomalia
- Prioridade baixa
- Recebe features da fila
- Aplica inferência do modelo pré-treinado
- Compara a probabilidade de classe com threshold
- Aciona LEDs conforme resultado

### Sincronização

- `mutexFeatures` : protege o acesso a `featuresAtuais`
- `xQueueSend/xQueueReceive` : troca de dados entre extração e detecção
- `xTaskNotifyGive` : sinalização entre captura e extração

Essa organização evita conflitos de concorrência e permite desacoplar leitura, processamento e decisão.

## Modelagem do sinal e extração de features

A classificação é baseada em features acústicas calculadas pela classe `FeatureExtractor`, presente em `features.cpp`.

### Features extraídas

- RMS do sinal
- nível em dB
- spectral centroid
- MFCCs (13 coeficientes)

Essas features servem como descritores do conteúdo acústico da janela de 512 amostras, capturadas a 16 kHz.

A arquitetura do processamento de áudio usa uma janela de análise e passo de deslocamento para garantir que o sistema opere com baixa latência, mantendo fluxo contínuo de dados.

## Modelo de detecção

O arquivo `src/modelo/classifier_head.onnx` representa a cabeça de classificação binária usada para detectar latido de cachorro. A lógica do projeto considera a classe de índice 1 como resposta positiva para presença de latido, enquanto a classe de índice 0 corresponde a ausência de latido ou outro ruído.

### Estrutura da rede

A rede exportada possui:

- entrada: vetor de 1024 posições (`yamnet_embedding`)
- camada densa 1: 128 neurônios
- ReLU
- camada densa 2: 64 neurônios
- ReLU
- camada de saída: 2 classes
- softmax final

A saída do modelo é um vetor de probabilidades por classe. No fluxo do firmware, a decisão de presença de latido usa `probabilidades[1] >= 0.55`.

### Mapeamento do classificador no firmware

A implementação C++ em `classifier_head.hpp` reproduz as operações do modelo exportado em ONNX, incluindo:

- multiplicação de matrizes
- adição de bias
- ReLU
- softmax

Os pesos foram extraídos do modelo ONNX e copiados para arrays estáticos para permitir inferência no ESP32 sem dependência de runtime de ONNX no dispositivo.

---

## Lógica dos LEDs

No firmware atual, o comportamento dos LEDs segue a probabilidade de latido:

- `probabilidades[1] >= 0.55` → LED vermelho acende, indicando latido de cachorro
- caso contrário → LED verde acende, indicando ausência de latido

### Interpretação

- LED verde: sem latido detectado
- LED vermelho: latido de cachorro detectado

A lógica concreta está no fluxo da tarefa de detecção, em `tarefaDeteccaoAnomalia()`.

---