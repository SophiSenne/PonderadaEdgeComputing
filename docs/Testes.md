# Documentação dos testes

## 1. Objetivo

Este documento consolida os testes realizados para validar a detecção de latido usando o modelo ONNX e o firmware embarcado no ESP32. Os testes foram conduzidos em duas frentes:

1. validação offline do modelo com áudio real em Python;
2. validação em tempo real no monitor serial do firmware, com coleta das métricas de inferência e do comportamento do sistema.

## 2. Teste 1: inferência com áudio real em Python

### Modelo utilizado
- Modelo ONNX: `bark_classifier.onnx`
- Limiar de decisão: `0.5`
- Ambiente de execução: Python + ONNX Runtime

### Arquivo de entrada usado no teste offline
- https://pixabay.com/pt/sound-effects/natureza-dog-barking-406629/

### Pipeline do teste
- carregamento do áudio
- segmentação em janelas
- extração de features
- normalização
- inferência no modelo ONNX
- agregação da probabilidade média por janela
- classificação final

### Comando executado

```bash
python src/testes/teste_onnx.py '/home/inteli/Downloads/dragon-studio-dog-barking-406629.mp3' --model bark_classifier.onnx --threshold 0.5
```

### Resultado obtido

```text
=== Detecção com ONNX ===
Arquivo: /home/inteli/Downloads/dragon-studio-dog-barking-406629.mp3
Modelo: bark_classifier.onnx
Janelas processadas: 1260
Probabilidade média de latido: 0.3235
Classificação: SEM_LATIDO

=== Métricas de performance ===
tempo_total_ms: 1316.176
tempo_extracao_features_ms: 1305.76
tempo_normalizacao_ms: 0.09
tempo_inferencia_onnx_ms: 0.642
latencia_media_ms_por_janela: 1.045
throughput_janelas_por_segundo: 957.319
```

### Interpretação

- A classificação final foi `SEM_LATIDO`, com probabilidade média de latido de `0.3235`.
- O limiar de decisão foi `0.5`, portanto o modelo não considerou o sinal como latido para esse arquivo específico.
- A etapa mais onerosa foi a extração de features, enquanto a inferência ONNX foi muito rápida.
- O throughput observado foi de aproximadamente `957 janelas/s`, o que evidencia boa capacidade de processamento em fluxo contínuo.

### Conclusão do teste offline

O modelo foi capaz de processar um sinal real de áudio e produzir uma saída consistente em termos de estrutura e tempo de execução. No entanto, o arquivo testado não foi classificado como latido, indicando que a detecção depende fortemente do tipo de sinal, do ruído presente e da adequação do limiar.

## 4. Teste 2: monitor serial do firmware em execução

### Objetivo

Validar o comportamento do sistema embarcado em tempo real, incluindo:

- número total de janelas processadas;
- tempo médio de inferência;
- probabilidade de latido por janela;
- mudança de status entre `NORMAL` e `LATIDO`;
- observação do impacto do processamento em execução contínua.

### Trechos observados no monitor serial

```text
metrics|ja_processadas=5450 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.0840 | status=NORMAL
metrics|ja_processadas=5460 | media_inferencia_ms=0.056 | ultima_inferencia_us=39 | prob_latido=0.0026 | status=NORMAL
metrics|ja_processadas=5470 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.6146 | status=NORMAL
metrics|ja_processadas=5480 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.6146 | status=NORMAL
metrics|ja_processadas=5490 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.0548 | status=NORMAL
features|RMS=0.2113 | dB=-13.50 | centroid=4349.03Hz | MFCC0=558.02 | inferencia_ultima_us=38 | janelas_total=5497
metrics|ja_processadas=5500 | media_inferencia_ms=0.056 | ultima_inferencia_us=38 | prob_latido=0.6695 | status=NORMAL
metrics|ja_processadas=5510 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.6146 | status=NORMAL
metrics|ja_processadas=5520 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.6146 | status=NORMAL
metrics|ja_processadas=5530 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.0061 | status=NORMAL
metrics|ja_processadas=5540 | media_inferencia_ms=0.056 | ultima_inferencia_us=39 | prob_latido=0.7715 | status=LATIDO
features|RMS=0.5358 | dB=-5.42 | centroid=2935.89Hz | MFCC0=611.93 | inferencia_ultima_us=52 | janelas_total=5546
metrics|ja_processadas=5550 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.1421 | status=NORMAL
metrics|ja_processadas=5560 | media_inferencia_ms=0.056 | ultima_inferencia_us=53 | prob_latido=0.6146 | status=NORMAL
```

### Análise dos resultados

- A latência média de inferência ficou constante em torno de `0.056 ms` por janela.
- Em alguns momentos, a probabilidade da classe latido ultrapassou o limiar, mas o status final permaneceu `NORMAL` em vários trechos, indicando que a decisão é instável e dependente do valor da janela atual.
- O exemplo com `prob_latido=0.7715` e `status=LATIDO` confirma que o sistema consegue alternar corretamente entre estado normal e detecção positiva quando o sinal entra em uma faixa de energia e características acústicas mais compatíveis com latido.
- A presença de picos na probabilidade indica que a sensibilidade do modelo pode ser influenciada pela dinâmica do ambiente e pelo tipo de ruído presente.

### Conclusão do teste serial

A execução em tempo real mostrou que a inferência é extremamente leve do ponto de vista computacional no ESP32. A principal observação é a variabilidade da probabilidade por janela, o que sugere que a decisão final pode ser melhorada com técnicas adicionais, como média móvel, limiar adaptativo ou acúmulo de eventos de detecção.

## 5. Evidências em vídeo

Foram registrados vídeos dos testes para complementar a validação visual do comportamento do sistema:

- Teste com miado: https://drive.google.com/file/d/1Bg6Q_7ANWBtRa8-uyrN5vtNorK0my4VS/view?usp=sharing
- Teste com latido: https://drive.google.com/file/d/1Twr65I6Mfl-C8AlyrAb-sXZoEuyKPxch/view?usp=sharing

## 6. Considerações finais

Os testes realizados indicam que:

- a inferência ONNX é rápida o suficiente para operação em tempo real;
- a detecção depende diretamente do tipo de áudio e da robustez dos dados no ambiente;
- a característica mais crítica não é a latência do modelo, mas a estabilidade da decisão final frente a ruído e variações acústicas.
