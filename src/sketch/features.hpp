#pragma once

#include <Arduino.h>

constexpr size_t TAMANHO_JANELA = 512;
constexpr size_t QUANTIDADE_MFCC = 13;

struct AudioFeatures {
	float rms;
	float rmsDb;
	float centroideEspectral;
	float mfcc[QUANTIDADE_MFCC];
};

class FeatureExtractor {
public:
	explicit FeatureExtractor(uint32_t taxaAmostragem);
	AudioFeatures calcular(const int16_t *amostras, size_t quantidade) const;

private:
	uint32_t taxaAmostragem_;
};
