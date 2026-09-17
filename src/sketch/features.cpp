#include "features.hpp"

#include <math.h>

namespace {
	constexpr float PI_LOCAL = 3.14159265358979323846f;
	constexpr size_t QUANTIDADE_FILTROS_MEL = 26;
	constexpr float EPSILON = 1.0e-12f;

	float hzParaMel(float frequencia) {
		return 1127.0f * logf(1.0f + frequencia / 700.0f);
	}

	float melParaHz(float mel) {
		return 700.0f * (expf(mel / 1127.0f) - 1.0f);
	}

	void fft(float *real, float *imaginario, size_t tamanho) {
		for (size_t i = 1, j = 0; i < tamanho; ++i) {
			size_t bit = tamanho >> 1;
			for (; (j & bit) != 0; bit >>= 1) {
				j ^= bit;
			}
			j ^= bit;
			if (i < j) {
				const float realTemp = real[i];
				real[i] = real[j];
				real[j] = realTemp;
				const float imaginarioTemp = imaginario[i];
				imaginario[i] = imaginario[j];
				imaginario[j] = imaginarioTemp;
			}
		}

		for (size_t tamanhoBloco = 2; tamanhoBloco <= tamanho; tamanhoBloco <<= 1) {
			const float angulo = -2.0f * PI_LOCAL / static_cast<float>(tamanhoBloco);
			const float passoReal = cosf(angulo);
			const float passoImaginario = sinf(angulo);
			for (size_t inicio = 0; inicio < tamanho; inicio += tamanhoBloco) {
				float fatorReal = 1.0f;
				float fatorImaginario = 0.0f;
				const size_t metade = tamanhoBloco / 2;
				for (size_t i = 0; i < metade; ++i) {
					const size_t par = inicio + i;
					const size_t impar = par + metade;
					const float produtoReal = fatorReal * real[impar] - fatorImaginario * imaginario[impar];
					const float produtoImaginario = fatorReal * imaginario[impar] + fatorImaginario * real[impar];
					real[impar] = real[par] - produtoReal;
					imaginario[impar] = imaginario[par] - produtoImaginario;
					real[par] += produtoReal;
					imaginario[par] += produtoImaginario;
					const float novoFatorReal = fatorReal * passoReal - fatorImaginario * passoImaginario;
					fatorImaginario = fatorReal * passoImaginario + fatorImaginario * passoReal;
					fatorReal = novoFatorReal;
				}
			}
		}
	}
}

FeatureExtractor::FeatureExtractor(uint32_t taxaAmostragem)
	: taxaAmostragem_(taxaAmostragem) {}

AudioFeatures FeatureExtractor::calcular(const int16_t *amostras, size_t quantidade) const {
	AudioFeatures resultado = {};
	if (amostras == nullptr || quantidade < TAMANHO_JANELA) {
		return resultado;
	}

	float media = 0.0f;
	for (size_t i = 0; i < TAMANHO_JANELA; ++i) {
		media += static_cast<float>(amostras[i]);
	}
	media /= static_cast<float>(TAMANHO_JANELA);

	float real[TAMANHO_JANELA] = {};
	float imaginario[TAMANHO_JANELA] = {};
	float energia = 0.0f;
	float amostraAnterior = 0.0f;
	for (size_t i = 0; i < TAMANHO_JANELA; ++i) {
		const float atual = static_cast<float>(amostras[i]) - media;
		energia += atual * atual;
		const float preEnfase = atual - 0.97f * amostraAnterior;
		amostraAnterior = atual;
		const float janela = 0.5f - 0.5f * cosf(2.0f * PI_LOCAL * i / (TAMANHO_JANELA - 1));
		real[i] = preEnfase * janela;
	}

	resultado.rms = sqrtf(energia / TAMANHO_JANELA) / 32768.0f;
	resultado.rmsDb = 20.0f * log10f(resultado.rms + EPSILON);

	fft(real, imaginario, TAMANHO_JANELA);
	float potencia[TAMANHO_JANELA / 2] = {};
	float somaMagnitude = 0.0f;
	float somaFrequencia = 0.0f;
	for (size_t bin = 1; bin < TAMANHO_JANELA / 2; ++bin) {
		const float magnitude = sqrtf(real[bin] * real[bin] + imaginario[bin] * imaginario[bin]);
		potencia[bin] = magnitude * magnitude;
		const float frequencia = static_cast<float>(bin) * taxaAmostragem_ / TAMANHO_JANELA;
		somaMagnitude += magnitude;
		somaFrequencia += frequencia * magnitude;
	}
	resultado.centroideEspectral = somaMagnitude > EPSILON ? somaFrequencia / somaMagnitude : 0.0f;

	float energiaMel[QUANTIDADE_FILTROS_MEL] = {};
	const float melMinimo = hzParaMel(300.0f);
	const float melMaximo = hzParaMel(static_cast<float>(taxaAmostragem_) / 2.0f);
	float pontos[QUANTIDADE_FILTROS_MEL + 2] = {};
	for (size_t i = 0; i < QUANTIDADE_FILTROS_MEL + 2; ++i) {
		const float mel = melMinimo + (melMaximo - melMinimo) * i / (QUANTIDADE_FILTROS_MEL + 1);
		const float frequencia = melParaHz(mel);
		pontos[i] = floorf((TAMANHO_JANELA + 1) * frequencia / taxaAmostragem_);
	}

	for (size_t filtro = 0; filtro < QUANTIDADE_FILTROS_MEL; ++filtro) {
		const size_t inicio = min(static_cast<size_t>(pontos[filtro]), TAMANHO_JANELA / 2 - 1);
		const size_t meio = min(static_cast<size_t>(pontos[filtro + 1]), TAMANHO_JANELA / 2 - 1);
		const size_t fim = min(static_cast<size_t>(pontos[filtro + 2]), TAMANHO_JANELA / 2 - 1);
		for (size_t bin = inicio; bin < meio; ++bin) {
			if (meio > inicio) {
				energiaMel[filtro] += potencia[bin] * (bin - inicio) / (meio - inicio);
			}
		}
		for (size_t bin = meio; bin <= fim && bin < TAMANHO_JANELA / 2; ++bin) {
			if (fim > meio) {
				energiaMel[filtro] += potencia[bin] * (fim - bin) / (fim - meio);
			}
		}
	}

	for (size_t coeficiente = 0; coeficiente < QUANTIDADE_MFCC; ++coeficiente) {
		for (size_t filtro = 0; filtro < QUANTIDADE_FILTROS_MEL; ++filtro) {
			const float logEnergia = logf(energiaMel[filtro] + EPSILON);
			resultado.mfcc[coeficiente] += logEnergia * cosf(
				PI_LOCAL * coeficiente * (filtro + 0.5f) / QUANTIDADE_FILTROS_MEL);
		}
	}

	return resultado;
}
