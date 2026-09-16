#pragma once

#include <Arduino.h>

class Led {
public:
	explicit Led(uint8_t pino);

	void iniciar();
	void ligar();
	void desligar();
	bool estaLigado() const;

private:
	uint8_t pino_;
	bool ligado_;
};
