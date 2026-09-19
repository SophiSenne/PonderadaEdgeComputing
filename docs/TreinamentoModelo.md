# Processo de desenvolvimento dos modelos

Este projeto passou por duas abordagens de detecção de latido:

1. uma etapa exploratória com um modelo AST (Audio Spectrogram Transformer) pré-treinado para classificar sons do AudioSet;
2. uma solução final, leve e compatível com o ESP32, baseada em um classificador MLP treinado com as mesmas features extraídas no firmware.

A segunda abordagem foi a adotada no produto final porque o modelo AST era muito pesado para um microcontrolador, enquanto a solução leve era compatível.

## Modelo AST: prova de conceito inicial

O notebook `modelo.ipynb` foi usado para verificar se era viável detectar latidos com um modelo já treinado no AudioSet, aproveitando a classe `Bark` presente no conjunto de rótulos do modelo.

### Dependências e carregamento

O notebook instala e carrega as bibliotecas necessárias para áudio, transformadores e exportação ONNX, como:

- `transformers`
- `torch`
- `torchaudio`
- `librosa`
- `soundfile`
- `onnx`
- `onnxruntime`
- `onnxscript`
- `numpy`

O modelo é carregado com `AutoFeatureExtractor` e `AutoModelForAudioClassification`, usando o checkpoint:

- `MIT/ast-finetuned-audioset-10-10-0.4593`

### 2.3 Pipeline de inferência

A lógica do notebook faz o seguinte:

- carrega o áudio em 16 kHz;
- prepara o tensor com o `feature_extractor` do Hugging Face;
- executa a rede;
- obtém o logit da classe `Bark`;
- converte para probabilidade com `sigmoid`;
- compara com um threshold de `0.5` para decidir se o áudio é `BARK` ou `NON-BARK`.

Esse conjunto de células demonstrou que o conceito era válido: um modelo treinado em AudioSet consegue detectar um latido com probabilidade razoável em amostras de exemplo.

### Limitação para o ESP32

O modelo AST tem cerca de 86 milhões de parâmetros. Isso o torna impraticável para execução no ESP32 por causa de:

- custo de memória elevado;
- uso de bibliotecas pesadas e dependentes de runtime de transformadores;
- processamento incompatível com os recursos de microcontrolador.

Em outras palavras, o modelo era ótimo como referencial de pesquisa ou prova de conceito, mas não servia para o firmware embarcado do projeto.

Foi feita a conversão para onnx e a quantização mas mesmo assim o modelo continuava com mais de 80KB, inviável de ser executado no ESP32 que possui x de memória.

## Modelo final: classificador leve para o ESP32

O notebook `train_classifier_head.ipynb` foi criado para treinar um modelo pequeno, com arquitetura leve e compatível com a execução direta no microcontrolador.

### Arquitetura escolhida

A arquitetura final foi montada como:

- entrada: 16 features;
- camada oculta 1: 32 neurônios;
- camada oculta 2: 16 neurônios;
- saída: 2 classes.

Em termos formais, a rede segue a estrutura:

- `16 -> 32 -> 16 -> 2`

Essa rede é pequena o suficiente para ser executada no ESP32 e preserva a lógica de features já usada no firmware.

## Extração de features: espelhando o firmware

### Objetivo da paridade

A etapa mais importante do treinamento foi replicar em Python a mesma lógica de extração de features do firmware. Isso garante que o classificador treinado em `train_classifier_head.ipynb` e o classificador executado em `classifier_head.hpp` sejam equivalentes.

### Pipeline implementado

A função de extração de janela do notebook reproduz o algoritmo de `features.cpp`:

- remoção de DC;
- cálculo de RMS e RMS em dB;
- pré-ênfase;
- janela de Hamming;
- FFT;
- centroide espectral;
- filtros Mel;
- cálculo dos MFCCs.

A saída final é um vetor de 16 valores na ordem:

```text
[rms, rms_db, centroide_espectral, mfcc[0], mfcc[1], ..., mfcc[12]]
```

Essa ordem é crítica, pois qualquer alteração na sequência causaria inconsistência entre treino e execução embarcada.

### Janela e passo do processamento

A pipeline usa:

- `TAMANHO_JANELA = 512`
- `PASSO_JANELA = 160`
- `taxa_amostragem = 16000 Hz`

Isso é consistente com o processamento em tempo real do firmware, e faz cada janela virar uma amostra independente de classificação.

## Construção do dataset

O dataset usado foi o ESC-50, um conjunto público de sons ambientais. A classe `dog` foi usada como exemplo positivo, enquanto as outras categorias foram tratadas como negativas.

### 6.2 Rotulagem binária

A lógica foi a seguinte:

- se o arquivo pertence à categoria `dog`, o rótulo é `1` (latido);
- caso contrário, o rótulo é `0` (sem latido).

### Treinamento por janela

O modelo não foi treinado sobre o áudio completo; ao contrário, cada janela extraída do áudio virou uma amostra de treino. Isso está alinhado ao comportamento da detecção em tempo real do firmware, que toma decisões por janela e não por áudio inteiro.

Essa decisão aumenta a quantidade de exemplos e torna o problema mais coerente com a aplicação embarcada.

## Normalização dos dados

Antes do treinamento, as features são normalizadas com z-score usando a média e o desvio padrão do conjunto de treino. Esse passo é fundamental para que os valores de entrada fiquem em escala comparável e a rede converja de maneira estável.

Os valores normalizados são armazenados e depois reutilizados no firmware para manter a mesma escala de entrada da rede.

## Treinamento do classificador leve

O notebook usa um conjunto de treinamento e teste montado a partir de janelas extraídas de diversos arquivos do dataset. A rede recebe um vetor de entrada de 16 dimensões e produz a classe de saída.

Durante o treino, o modelo usa `CrossEntropyLoss`, adequado para classificação binária.

O treinamento é conduzido com `Adam`, usando taxa de aprendizado da ordem de `1e-3`.

### Métricas importantes

As métricas principais observadas foram:

- perda de treino ao longo das épocas;
- acurácia no conjunto de teste;
- recall e precision da classe positiva;
- matriz de confusão;
- f1-score.

A acurácia final ficou na faixa de ~0,80 para o conjunto de teste, o que foi considerado aceitável para a aplicação de detecção de latidos em uma janela de áudio curta.

## Exportação para ONNX

O notebook exporta o modelo com `torch.onnx.export`, definindo:

- `input_names=["features"]`
- `output_names=["logits"]`
- `opset_version=17`
- saída de logits para classificação binária

Depois, o arquivo ONNX é validado com `onnx.checker.check_model`.

### Verificação prática

A verificação também pode ser feita comparando a saída do PyTorch com a saída do `onnxruntime`, garantindo que o modelo exportado executa os mesmos cálculos do modelo original.

Esse passo foi importante para confirmar que a decisão final de usar o classificador leve e exportá-lo para ONNX era técnica e numericamente correta.

## Geração do `classifier_head.hpp`

### Objetivo

Para que o firmware do ESP32 pudesse executar o modelo sem dependência de ONNX, o notebook gera um header C++ contendo todos os parâmetros treinados.

Esse arquivo inclui:

- média e desvio padrão das features;
- pesos e biases das camadas;
- funções de inferência em C++;
- lógica de ativação e classificação.

### Conteúdo gerado

O header final contém arrays de pesos e bias formatados no mesmo layout usado pela implementação em C++, além da função:

```cpp
ClassifierHead::inferir(const AudioFeatures &features, float *probs);
```

Essa função:

- normaliza as features;
- aplica as operações de cada camada;
- executa ReLU;
- produz a probabilidade de latido.

### Importância da solução

Essa escolha faz o firmware funcionar com baixo custo computacional e sem precisar carregar um runtime ONNX completo no microcontrolador. O resultado é um sistema leve, direto e adequado ao ESP32.

```text
Sketch uses 315456 bytes (24%) of program storage space. Maximum is 1310720 bytes.
Global variables use 30572 bytes (9%) of dynamic memory, leaving 297108 bytes for local variables. Maximum is 327680 bytes.
```

## Comparação entre os caminhos avaliados

### Modelo AST

Vantagens:

- modelo forte e já treinado em dados de áudio;
- adequado para classificação genérica de sons;
- útil como referência de conceito.

Desvantagens:

- muito pesado;
- incompatível com o ESP32;
- não adequado ao firmware embarcado em termos de memória e velocidade.

### Modelo leve do projeto

Vantagens:

- leve e executável no ESP32;
- compatível com as features do firmware;
- fácil de exportar para ONNX e C++;
- adequado para inferência em tempo real.

Desvantagens:

- menos generalista que um modelo grande;
- mais dependente da qualidade da feature extraction;
- sensível ao ambiente e ao ruído.

## Conclusão

A implementação final foi construída a partir de um erro de modelagem bem específico: a entrada do classificador foi alterada artificialmente e saiu da correspondência com a feature extraction do firmware. A correção exigiu reposicionar o trabalho em um modelo pequeno e coerente com a pipeline embarcada.

A sequência correta do projeto foi:

1. validar a ideia com o modelo AST;
2. constatar a incompatibilidade com o ESP32;
3. reescrever a pipeline para um classificador de 16 features;
4. treinar o modelo usando a mesma lógica do firmware;
5. exportar para ONNX para validação;
6. gerar o header `classifier_head.hpp` para execução no microcontrolador.

Esse processo foi o que tornou o projeto viável e funcional no contexto de computação de borda.

## Arquivos principais

- `src/modelo/modelo.ipynb` — exploração do modelo AST e exportação para ONNX;
- `src/modelo/train_classifier_head.ipynb` — treinamento do classificador leve e geração do header C++;
- `src/sketch/features.cpp` — extração real das features no firmware;
- `src/sketch/classifier_head.hpp` — implementação embarcada do classificador;
- `src/testes/teste_onnx.py` — benchmark de inferência em Python.

