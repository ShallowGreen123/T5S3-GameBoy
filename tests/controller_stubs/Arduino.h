#pragma once
#include <stdint.h>
#include <stddef.h>
extern uint32_t test_now;
inline uint32_t millis() { return test_now; }
inline void delay(unsigned) {}
