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
SemaphoreHandle_t mutexBuffer = nullptr;
QueueHandle_t filaFeatures = nullptr;
AudioFeatures featuresAtuais = {};
volatile uint32_t contadorJanelasProcessadas = 0;
volatile uint32_t tempoInferenciaAcumuladoUs = 0;
volatile uint32_t ultimoTempoCapturaUs = 0;
volatile uint32_t ultimoTempoBufferUs = 0;
volatile uint32_t ultimoTempoExtracaoUs = 0;
volatile uint32_t ultimoTempoFilaUs = 0;
volatile uint32_t ultimoTempoInferenciaUs = 0;
volatile uint32_t ultimoTempoLedUs = 0;
volatile uint32_t ultimasDeteccoes = 0;

size_t amostrasDisponiveis() {
	if (mutexBuffer == nullptr) {
		return 0;
	}
	if (xSemaphoreTake(mutexBuffer, pdMS_TO_TICKS(5)) != pdTRUE) {
		return 0;
	}
	const size_t escrita = indiceEscrita;
	const size_t leitura = indiceLeitura;
	const size_t disponiveis = escrita >= leitura ? escrita - leitura : TAMANHO_BUFFER_CIRCULAR - leitura + escrita;
	xSemaphoreGive(mutexBuffer);
	return disponiveis;
}

void inserirAmostras(const int32_t *amostras, size_t quantidade) {
	if (mutexBuffer == nullptr || amostras == nullptr || quantidade == 0) {
		return;
	}
	if (xSemaphoreTake(mutexBuffer, pdMS_TO_TICKS(5)) != pdTRUE) {
		return;
	}
	for (size_t i = 0; i < quantidade; ++i) {
		const int32_t valor = amostras[i] >> 8;
		const size_t proximo = (indiceEscrita + 1) % TAMANHO_BUFFER_CIRCULAR;
		if (proximo == indiceLeitura) {
			indiceLeitura = (indiceLeitura + 1) % TAMANHO_BUFFER_CIRCULAR;
		}
		bufferCircular[indiceEscrita] = static_cast<int16_t>(constrain(valor, -32768L, 32767L));
		indiceEscrita = proximo;
	}
	xSemaphoreGive(mutexBuffer);
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

void registrarLatenciaPipeline(uint32_t capturaUs, uint32_t bufferUs, uint32_t extracaoUs, uint32_t filaUs,
	uint32_t inferenciaUs, uint32_t ledUs, float probabilidadeLatido, bool eLatido) {
	const uint32_t totalUs = capturaUs + bufferUs + extracaoUs + filaUs + inferenciaUs + ledUs;
	Serial.printf("latencia|captura_us=%lu | buffer_us=%lu | extracao_us=%lu | fila_us=%lu | inferencia_us=%lu | led_us=%lu | total_us=%lu | prob_latido=%.4f | status=%s\n",
		static_cast<unsigned long>(capturaUs),
		static_cast<unsigned long>(bufferUs),
		static_cast<unsigned long>(extracaoUs),
		static_cast<unsigned long>(filaUs),
		static_cast<unsigned long>(inferenciaUs),
		static_cast<unsigned long>(ledUs),
		static_cast<unsigned long>(totalUs),
		probabilidadeLatido,
		eLatido ? "LATIDO" : "NORMAL");
}

bool extrairJanela(int16_t *janela) {
	if (janela == nullptr || mutexBuffer == nullptr) {
		return false;
	}
	const uint32_t inicioBufferUs = micros();
	if (xSemaphoreTake(mutexBuffer, pdMS_TO_TICKS(5)) != pdTRUE) {
		return false;
	}
	const size_t escrita = indiceEscrita;
	const size_t leitura = indiceLeitura;
	const size_t disponiveis = escrita >= leitura ? escrita - leitura : TAMANHO_BUFFER_CIRCULAR - leitura + escrita;
	if (disponiveis < TAMANHO_JANELA) {
		xSemaphoreGive(mutexBuffer);
		ultimoTempoBufferUs = micros() - inicioBufferUs;
		return false;
	}
	for (size_t i = 0; i < TAMANHO_JANELA; ++i) {
		janela[i] = bufferCircular[(leitura + i) % TAMANHO_BUFFER_CIRCULAR];
	}
	indiceLeitura = (leitura + PASSO_JANELA) % TAMANHO_BUFFER_CIRCULAR;
	xSemaphoreGive(mutexBuffer);
	ultimoTempoBufferUs = micros() - inicioBufferUs;
	return true;
}

void tarefaCapturaAudio(void *) {
	int32_t bloco[TAMANHO_BLOCO_I2S];
	for (;;) {
		const uint32_t inicioCapturaUs = micros();
		const size_t lidas = microfone.ler(bloco, TAMANHO_BLOCO_I2S, portMAX_DELAY);
		if (lidas > 0) {
			inserirAmostras(bloco, lidas);
			ultimoTempoCapturaUs = micros() - inicioCapturaUs;
			xTaskNotifyGive(tarefaFeatures);
		}
	}
}

void tarefaExtracaoFeatures(void *) {
	int16_t janela[TAMANHO_JANELA];
	for (;;) {
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
		while (extrairJanela(janela)) {
			const uint32_t inicioExtracaoUs = micros();
			const AudioFeatures novasFeatures = extractor.calcular(janela, TAMANHO_JANELA);
			ultimoTempoExtracaoUs = micros() - inicioExtracaoUs;
			if (xSemaphoreTake(mutexFeatures, pdMS_TO_TICKS(5)) == pdTRUE) {
				featuresAtuais = novasFeatures;
				xSemaphoreGive(mutexFeatures);
			}
			if (filaFeatures != nullptr) {
				const uint32_t inicioFilaUs = micros();
				xQueueSend(filaFeatures, &novasFeatures, portMAX_DELAY);
				ultimoTempoFilaUs = micros() - inicioFilaUs;
			}
		}
	}
}

void tarefaDeteccaoAnomalia(void *) {
	AudioFeatures features;
	float probabilidades[CLASSIFIER_OUTPUT_DIM];
	for (;;) {
		if (xQueueReceive(filaFeatures, &features, portMAX_DELAY) == pdTRUE) {
			const uint32_t inicioInferenciaUs = micros();
			ClassifierHead::inferir(features, probabilidades);
			const uint32_t fimInferenciaUs = micros();
			const uint32_t tempoInferenciaUs = fimInferenciaUs - inicioInferenciaUs;
			ultimoTempoInferenciaUs = tempoInferenciaUs;
			tempoInferenciaAcumuladoUs += tempoInferenciaUs;
			contadorJanelasProcessadas++;
			ultimasDeteccoes++;

			const float probabilidadeNaoLatido = probabilidades[0];
			const float probabilidadeLatido = probabilidades[1];
			const bool eLatido = probabilidadeLatido >= CLASSIFIER_THRESHOLD;

			const uint32_t inicioLedUs = micros();
			atualizarIndicadorDeteccao(probabilidadeLatido);
			ultimoTempoLedUs = micros() - inicioLedUs;
			registrarLatenciaPipeline(ultimoTempoCapturaUs, ultimoTempoBufferUs, ultimoTempoExtracaoUs,
				ultimoTempoFilaUs, ultimoTempoInferenciaUs, ultimoTempoLedUs, probabilidadeLatido, eLatido);

			if (ultimasDeteccoes >= 10) {
				const float tempoMedioMs = (tempoInferenciaAcumuladoUs / static_cast<float>(contadorJanelasProcessadas)) / 1000.0f;
				Serial.printf("metrics|ja_processadas=%lu | media_inferencia_ms=%.3f | ultima_inferencia_us=%lu | prob_latido=%.4f | status=%s\n",
					static_cast<unsigned long>(contadorJanelasProcessadas),
					tempoMedioMs,
					static_cast<unsigned long>(ultimoTempoInferenciaUs),
					probabilidadeLatido,
					eLatido ? "LATIDO" : "NORMAL");
				ultimasDeteccoes = 0;
			}
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
	mutexBuffer = xSemaphoreCreateMutex();
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
		Serial.printf("features|RMS=%.4f | dB=%.2f | centroid=%.2fHz | MFCC0=%.2f | inferencia_ultima_us=%lu | janelas_total=%lu\n",
			features.rms, features.rmsDb, features.centroideEspectral, features.mfcc[0],
			static_cast<unsigned long>(ultimoTempoInferenciaUs),
			static_cast<unsigned long>(contadorJanelasProcessadas));
	}
	delay(500);
}
