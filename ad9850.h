#ifndef AD9850_H
#define AD9850_H

#include "main.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

void AD9850_Init(void);
void AD9850_WriteCmd(uint8_t _ucPhase, double _dOutFreq);
#endif
