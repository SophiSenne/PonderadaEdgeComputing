#include "microfone.hpp"

#include "global.hpp"

Microfone::Microfone(i2s_port_t porta) : porta_(porta), iniciado_(false) {}

Microfone::~Microfone() {
	parar();
}

bool Microfone::iniciar(uint32_t taxaAmostragem) {
	if (iniciado_) {
		return true;
	}

	const i2s_config_t configuracao = {
		.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
		.sample_rate = static_cast<int>(taxaAmostragem),
		.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
		.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
		.communication_format = I2S_COMM_FORMAT_I2S,
		.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
		.dma_buf_count = 4,
		.dma_buf_len = 256,
		.use_apll = false,
		.tx_desc_auto_clear = false,
		.fixed_mclk = 0
	};

	const i2s_pin_config_t pinos = {
		.bck_io_num = PIN_MICROFONE_SCK,
		.ws_io_num = PIN_MICROFONE_WS,
		.data_out_num = I2S_PIN_NO_CHANGE,
		.data_in_num = PIN_MICROFONE_SD
	};

	if (i2s_driver_install(porta_, &configuracao, 0, nullptr) != ESP_OK) {
		return false;
	}

	if (i2s_set_pin(porta_, &pinos) != ESP_OK) {
		i2s_driver_uninstall(porta_);
		return false;
	}

	i2s_zero_dma_buffer(porta_);
	iniciado_ = true;
	return true;
}

bool Microfone::disponivel() const {
	return iniciado_;
}

bool Microfone::ler(int32_t &amostra, uint32_t tempoLimiteMs) {
	return ler(&amostra, 1, tempoLimiteMs) == 1;
}

size_t Microfone::ler(int32_t *amostras, size_t quantidade, uint32_t tempoLimiteMs) {
	if (!iniciado_ || amostras == nullptr || quantidade == 0) {
		return 0;
	}

	size_t bytesLidos = 0;
	const TickType_t tempoLimite = pdMS_TO_TICKS(tempoLimiteMs);
	const esp_err_t resultado = i2s_read(
		porta_,
		amostras,
		quantidade * sizeof(int32_t),
		&bytesLidos,
		tempoLimite);

	if (resultado != ESP_OK) {
		return 0;
	}

	return bytesLidos / sizeof(int32_t);
}

void Microfone::parar() {
	if (!iniciado_) {
		return;
	}

	i2s_driver_uninstall(porta_);
	iniciado_ = false;
}
