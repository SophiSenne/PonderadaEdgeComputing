#include "led.hpp"

Led::Led(uint8_t pino) : pino_(pino), ligado_(false) {}

void Led::iniciar() {
	pinMode(pino_, OUTPUT);
	desligar();
}

void Led::ligar() {
	digitalWrite(pino_, HIGH);
	ligado_ = true;
}

void Led::desligar() {
	digitalWrite(pino_, LOW);
	ligado_ = false;
}

bool Led::estaLigado() const {
	return ligado_;
}
