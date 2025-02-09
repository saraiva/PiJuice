/*
 * analog.h
 *
 *  Created on: 11.12.2016.
 *      Author: milan
 */

#ifndef ANALOG_H_
#define ANALOG_H_

void ANALOG_Init(const uint32_t sysTime);
void ANALOG_Service(const uint32_t sysTime);
void ANALOG_Shutdown(void);

uint16_t ANALOG_GetAVDDMv(void);
uint16_t ANALOG_GetMv(const uint8_t channelIdx);
uint16_t ANALOG_GetBatteryMv(void);
uint16_t ANALOG_Get5VRailMv(void);
int16_t ANALOG_Get5VRailMa(void);
int16_t ANALOG_GetMCUTemp(void);

#endif /* ANALOG_H_ */
