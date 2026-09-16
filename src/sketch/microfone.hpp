#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

class Microfone {
public:
	explicit Microfone(i2s_port_t porta = I2S_NUM_0);
	~Microfone();

	bool iniciar(uint32_t taxaAmostragem = 16000);
	bool disponivel() const;
	bool ler(int32_t &amostra, uint32_t tempoLimiteMs = 0);
	size_t ler(int32_t *amostras, size_t quantidade, uint32_t tempoLimiteMs = 0);
	void parar();

private:
	i2s_port_t porta_;
	bool iniciado_;
};
