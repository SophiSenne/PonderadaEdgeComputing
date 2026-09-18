



O modelo AST (`modelo.ipynb`) tem ~86M de parâmetros e não cabe em um ESP32 (flash de poucos MB, RAM de algumas centenas de KB, sem runtime de transformer). O firmware já extrai features leves de áudio (RMS, RMS em dB, centroide espectral e 13 MFCCs — 16 valores no total) em `features.cpp`, e esperava um classificador compatível, mas o `classifier_head.hpp` anterior expandia essas 16 features para 1024 dimensões de forma artificial (senoide, sem relação com o treino), gerando um classificador que não funciona.