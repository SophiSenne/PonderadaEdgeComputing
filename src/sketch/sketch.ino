#include "features.hpp"
#include "global.hpp"
#include "microfone.hpp"

namespace {
constexpr uint32_t TAXA_AMOSTRAGEM = 16000;
constexpr size_t TAMANHO_BUFFER_CIRCULAR = 4096;
constexpr size_t TAMANHO_BLOCO_I2S = 256;
constexpr size_t PASSO_JANELA = 160;

Microfone microfone;
FeatureExtractor extractor(TAXA_AMOSTRAGEM);
int16_t bufferCircular[TAMANHO_BUFFER_CIRCULAR];
volatile size_t indiceEscrita = 0;
volatile size_t indiceLeitura = 0;
TaskHandle_t tarefaCaptura = nullptr;
TaskHandle_t tarefaFeatures = nullptr;
SemaphoreHandle_t mutexFeatures = nullptr;
AudioFeatures featuresAtuais = {};

size_t amostrasDisponiveis() {
	const size_t escrita = indiceEscrita;
	const size_t leitura = indiceLeitura;
	return escrita >= leitura ? escrita - leitura : TAMANHO_BUFFER_CIRCULAR - leitura + escrita;
}

void inserirAmostras(const int32_t *amostras, size_t quantidade) {
	for (size_t i = 0; i < quantidade; ++i) {
		const int32_t valor = amostras[i] >> 8;
		const size_t proximo = (indiceEscrita + 1) % TAMANHO_BUFFER_CIRCULAR;
		if (proximo == indiceLeitura) {
			indiceLeitura = (indiceLeitura + 1) % TAMANHO_BUFFER_CIRCULAR;
		}
		bufferCircular[indiceEscrita] = static_cast<int16_t>(constrain(valor, -32768L, 32767L));
		indiceEscrita = proximo;
	}
}

bool extrairJanela(int16_t *janela) {
	if (amostrasDisponiveis() < TAMANHO_JANELA) {
		return false;
	}
	for (size_t i = 0; i < TAMANHO_JANELA; ++i) {
		janela[i] = bufferCircular[(indiceLeitura + i) % TAMANHO_BUFFER_CIRCULAR];
	}
	indiceLeitura = (indiceLeitura + PASSO_JANELA) % TAMANHO_BUFFER_CIRCULAR;
	return true;
}

void tarefaCapturaAudio(void *) {
	int32_t bloco[TAMANHO_BLOCO_I2S];
	for (;;) {
		const size_t lidas = microfone.ler(bloco, TAMANHO_BLOCO_I2S, portMAX_DELAY);
		if (lidas > 0) {
			inserirAmostras(bloco, lidas);
			xTaskNotifyGive(tarefaFeatures);
		}
	}
}

void tarefaExtracaoFeatures(void *) {
	int16_t janela[TAMANHO_JANELA];
	for (;;) {
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
		while (extrairJanela(janela)) {
			const AudioFeatures novasFeatures = extractor.calcular(janela, TAMANHO_JANELA);
			if (xSemaphoreTake(mutexFeatures, pdMS_TO_TICKS(5)) == pdTRUE) {
				featuresAtuais = novasFeatures;
				xSemaphoreGive(mutexFeatures);
			}
		}
	}
}
}

void setup() {
	Serial.begin(115200);
	if (!microfone.iniciar(TAXA_AMOSTRAGEM)) {
		Serial.println("Falha ao iniciar o microfone I2S");
		return;
	}

	mutexFeatures = xSemaphoreCreateMutex();
	xTaskCreatePinnedToCore(
		tarefaExtracaoFeatures, "features", 8192, nullptr, 2, &tarefaFeatures, 1);
	xTaskCreatePinnedToCore(
		tarefaCapturaAudio, "captura", 4096, nullptr, 3, &tarefaCaptura, 1);
}

void loop() {
	AudioFeatures features;
	if (mutexFeatures != nullptr && xSemaphoreTake(mutexFeatures, pdMS_TO_TICKS(10)) == pdTRUE) {
		features = featuresAtuais;
		xSemaphoreGive(mutexFeatures);
		Serial.printf("RMS: %.4f | dB: %.2f | Centroid: %.2f Hz | MFCC0: %.2f\n",
			features.rms, features.rmsDb, features.centroideEspectral, features.mfcc[0]);
	}
	delay(500);
}
