/*
 * bsp.h  (BSP : Board Support Package)
 *
 *  MCU/보드에 직접 닿는 가장 아래층 함수들.
 *  STM32 에서는 HAL 을 쓰고, PC 시뮬레이터에서는 OS 함수를 씁니다.
 */

#ifndef BSP_H_
#define BSP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"


/* 클럭, 인터럽트 등 MCU 기본 설정 */
bool bspInit(void);

/* 부팅 후 흘러간 시간(ms). STM32 에서는 HAL_GetTick() 과 같습니다. */
uint32_t millis(void);

/* time_ms 만큼 기다리기 (이 시간 동안 아무 일도 못 하므로 남용 금지) */
void delay(uint32_t time_ms);


#ifdef __cplusplus
}
#endif

#endif /* BSP_H_ */
