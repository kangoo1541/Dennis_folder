/*
 * hw.h
 *
 *  모든 하드웨어 드라이버를 한 번에 초기화해 주는 상위 모듈
 */

#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#include "uart.h"
#include "led.h"
#include "cli.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif /* HW_H_ */
