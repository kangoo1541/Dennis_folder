/*
 * led.h
 *
 *  LED 드라이버 + CLI 명령어 예제
 */

#ifndef LED_H_
#define LED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_LED


/* LED 초기화. 이 안에서 cliAdd("led", ...) 로 명령어도 함께 등록한다. */
bool ledInit(void);

void ledOn(uint8_t ch);
void ledOff(uint8_t ch);
void ledToggle(uint8_t ch);
bool ledGetState(uint8_t ch);


/* ---- 보드마다 다르게 구현해야 하는 부분 ----------------------------
 * STM32 보드용 구현 : src/bsp/bsp_stm32f411.c
 * PC 시뮬레이터용    : simulator/sim_port.c
 * 이렇게 "하드웨어에 직접 닿는 부분"만 분리해 두면,
 * led.c 본체는 어떤 보드에서도 그대로 재사용할 수 있습니다.
 */
bool ledHwInit(void);
void ledHwWrite(uint8_t ch, bool on_state);


#endif /* _USE_HW_LED */

#ifdef __cplusplus
}
#endif

#endif /* LED_H_ */
