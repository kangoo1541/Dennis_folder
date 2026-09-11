/*
 * uart.h
 *
 *  CLI 가 사용하는 UART 드라이버의 "인터페이스(약속)" 정의
 *
 *  [초보자 설명]
 *   CLI 모듈은 UART 가 STM32 의 USART1 인지, USB CDC 인지, PC 시뮬레이터인지
 *   전혀 신경 쓰지 않습니다. 아래 6개의 함수만 있으면 동작합니다.
 *   이렇게 "필요한 함수 목록"만 헤더에 정해두고 실제 구현은 갈아끼우는 방식을
 *   HAL(Hardware Abstraction Layer) 이라고 부릅니다.
 */

#ifndef UART_H_
#define UART_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_UART


/* UART 전체 초기화 (부팅 시 1회) */
bool     uartInit(void);

/* 지정한 채널을 baud 속도로 사용 시작 */
bool     uartOpen(uint8_t ch, uint32_t baud);

/* 수신 버퍼에 읽지 않은 바이트가 몇 개 있는지 반환 (0이면 없음) */
uint32_t uartAvailable(uint8_t ch);

/* 수신 버퍼에서 1바이트 꺼내기 (uartAvailable() > 0 일 때만 호출) */
uint8_t  uartRead(uint8_t ch);

/* length 바이트 송신, 실제로 보낸 바이트 수 반환 */
uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length);

/* printf 처럼 서식 문자열 송신 */
uint32_t uartPrintf(uint8_t ch, const char *fmt, ...);


#endif /* _USE_HW_UART */

#ifdef __cplusplus
}
#endif

#endif /* UART_H_ */
