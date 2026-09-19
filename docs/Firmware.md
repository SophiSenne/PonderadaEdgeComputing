# Firmware

O firmware foi desenvolvido utilizando o Arduino IDE como ambiente de desenvolvimento e compilação. A implementação foi organizada em arquivos específicos para separar responsabilidades e manter o código mais legível.

## 1. Estrutura do firmware

A organização do código foi pensada em camadas:

- gerenciamento do microfone
- controle dos LEDs
- extração de características do sinal
- integração do fluxo principal em `sketch.ino`
- uso de FreeRTOS para execução concorrente

Os arquivos principais ficam em `src/sketch` e incluem:

- `microfone.hpp` e `microfone.cpp`
- `led.hpp` e `led.cpp`
- `features.hpp` e `features.cpp`
- `classifier_head.hpp`
- `global.hpp`
- `sketch.ino`

## 2. Classe do LED

A classe `Led` foi criada para encapsular o controle do estado dos LEDs indicadores do sistema.

### Funcionalidades

- inicialização do pino GPIO
- ativação do LED
- desativação do LED
- verificação do estado atual do pino

### Objetivo no sistema

No projeto, os LEDs são utilizados como retorno visual da detecção:

- LED verde: sistema em estado normal
- LED vermelho: presença de latido detectada

Essa lógica é implementada no fluxo principal do firmware, onde a probabilidade de latido é comparada ao limiar definido no classificador.

## 3. Classe do microfone

A classe `Microfone` encapsula toda a comunicação com o sensor de áudio digital via interface I2S.

### Funcionalidades

- inicialização do módulo I2S
- configuração da taxa de amostragem
- leitura de blocos de amostras
- verificação de disponibilidade do sinal
- parada do driver quando necessário

### Configuração do hardware

O microfone é configurado para operar com:

- taxa de amostragem: 16 kHz
- formato de dados: 32 bits por amostra
- canal: mono, configurado no canal esquerdo

Essa configuração foi escolhida para permitir a extração de features acústicas em janelas temporais adequadas ao processamento em tempo real no ESP32.

## 4. Extração de features

A classe `FeatureExtractor` foi criada para transformar o sinal de áudio em um conjunto de características relevantes para a classificação.

### Features calculadas

- RMS (root mean square)
- nível em dB
- centroide espectral
- coeficientes MFCC

Essas features são extraídas a partir de janelas de áudio e servem como entrada para a inferência do modelo de classificação.

### Estrutura da classe

A classe recebe a taxa de amostragem e expõe o método `calcular()`, que recebe um vetor de amostras e retorna uma estrutura `AudioFeatures` contendo os valores processados.

Esse módulo é essencial porque a rede de classificação não opera diretamente sobre o sinal bruto, mas sim sobre um vetor de atributos extraídos do áudio.

## 5. Classificador embutido

Além das classes de hardware e processamento, o firmware contém a implementação do classificador em `classifier_head.hpp`.

Esse módulo:

- normaliza os valores de entrada
- aplica as camadas densas da rede
- usa ReLU como função de ativação
- calcula os logits de saída
- aplica softmax para obter as probabilidades
- compara a probabilidade da classe de latido com o limiar

A decisão final é feita com base no valor da probabilidade da classe de latido.

## 6. Integração no `sketch.ino`

O arquivo `sketch.ino` reúne todos os módulos e implementa a lógica principal do firmware em um fluxo multitarefa com FreeRTOS.

A arquitetura de concorrência e detalhes específicos do uso do FreeRTOS estão descritos em [RTOS.md](RTOS.md). Esse documento complementa a implementação do firmware e explica melhor a dinâmica das tarefas e da sincronização do sistema.
