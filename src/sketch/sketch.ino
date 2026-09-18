#include "classifier_head.hpp"
#include "features.hpp"
#include "global.hpp"
#include "led.hpp"
#include "microfone.hpp"

namespace {
constexpr uint32_t TAXA_AMOSTRAGEM = 16000;
constexpr size_t TAMANHO_BUFFER_CIRCULAR = 4096;
constexpr size_t TAMANHO_BLOCO_I2S = 256;
constexpr size_t PASSO_JANELA = 160;

Microfone microfone;
FeatureExtractor extractor(TAXA_AMOSTRAGEM);
Led ledVermelho(PIN_LED_VERMELHO);
Led ledVerde(PIN_LED_VERDE);
int16_t bufferCircular[TAMANHO_BUFFER_CIRCULAR];
volatile size_t indiceEscrita = 0;
volatile size_t indiceLeitura = 0;
TaskHandle_t tarefaCaptura = nullptr;
TaskHandle_t tarefaFeatures = nullptr;
TaskHandle_t tarefaDeteccao = nullptr;
SemaphoreHandle_t mutexFeatures = nullptr;
QueueHandle_t filaFeatures = nullptr;
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

void atualizarIndicadorDeteccao(float probabilidadeLatido) {
	if (probabilidadeLatido >= CLASSIFIER_THRESHOLD) {
		ledVermelho.ligar();
		ledVerde.desligar();
	} else {
		ledVermelho.desligar();
		ledVerde.ligar();
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
			if (filaFeatures != nullptr) {
				xQueueSend(filaFeatures, &novasFeatures, portMAX_DELAY);
			}
		}
	}
}

void tarefaDeteccaoAnomalia(void *) {
	AudioFeatures features;
	float probabilidades[CLASSIFIER_OUTPUT_DIM];
	for (;;) {
		if (xQueueReceive(filaFeatures, &features, portMAX_DELAY) == pdTRUE) {
			ClassifierHead::inferir(features, probabilidades);

			const float probabilidadeNaoLatido = probabilidades[0];
			const float probabilidadeLatido = probabilidades[1];
			const bool eLatido = probabilidadeLatido >= CLASSIFIER_THRESHOLD;

			atualizarIndicadorDeteccao(probabilidadeLatido);

			Serial.printf("sem_latido=%.4f | latido=%.4f -> %s\n",
				probabilidadeNaoLatido, probabilidadeLatido, eLatido ? "LATIDO" : "NORMAL");
		}
	}
}
}

void setup() {
	Serial.begin(115200);
	ledVermelho.iniciar();
	ledVerde.iniciar();
	ledVerde.ligar();
	ledVermelho.desligar();
	if (!microfone.iniciar(TAXA_AMOSTRAGEM)) {
		Serial.println("Falha ao iniciar o microfone I2S");
		return;
	}

	mutexFeatures = xSemaphoreCreateMutex();
	filaFeatures = xQueueCreate(4, sizeof(AudioFeatures));
	xTaskCreatePinnedToCore(
		tarefaExtracaoFeatures, "features", 8192, nullptr, 2, &tarefaFeatures, 1);
	xTaskCreatePinnedToCore(
		tarefaDeteccaoAnomalia, "detector", 8192, nullptr, 1, &tarefaDeteccao, 1);
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
