/*
 * hw_def.h
 *
 *  하드웨어(HW) 관련 "설정 스위치"와 "크기 상수"를 한 곳에 모아 둔 파일
 *
 *  [초보자 설명]
 *   _USE_HW_XXX  : 그 기능을 컴파일에 포함할지 말지 결정하는 스위치입니다.
 *                  #define 을 지우거나 주석 처리하면 해당 코드가 통째로 빠지므로
 *                  플래시(코드) 용량을 아낄 수 있습니다.
 *   XXX_MAX      : 배열 크기를 정하는 상수입니다. 값을 키우면 기능은 좋아지지만
 *                  RAM 을 더 많이 씁니다. STM32F411 은 RAM 이 128KB 라 넉넉한
 *                  편이지만, 습관적으로 필요한 만큼만 잡는 게 좋습니다.
 */

#ifndef HW_DEF_H_
#define HW_DEF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"


#define _DEF_FIRMWATRE_VERSION    "V240101R1"
#define _DEF_BOARD_NAME           "STM32F411-CLI"


/* -------------------------------------------------------------------------- */
/*  UART                                                                      */
/* -------------------------------------------------------------------------- */
#define _USE_HW_UART
#define      HW_UART_MAX_CH          2   /* 사용할 UART 채널 개수 */

/* 채널 번호에 이름을 붙여두면 코드를 읽기 쉬워집니다. */
#define      _DEF_UART1              0   /* USB CDC(VCP) : CLI 명령 입출력 채널   */
#define      _DEF_UART2              1   /* 물리 UART    : 키코드 확인용 로그 채널 */


/* -------------------------------------------------------------------------- */
/*  LED                                                                       */
/* -------------------------------------------------------------------------- */
#define _USE_HW_LED
#define      HW_LED_MAX_CH           1   /* 보드에 달린 LED 개수 */

#define      _DEF_LED_HEARTBEAT      0   /* 살아있음을 알리는 LED 채널 */


/* -------------------------------------------------------------------------- */
/*  CLI (Command Line Interface)                                              */
/* -------------------------------------------------------------------------- */
#define _USE_HW_CLI
/* 한 줄에 입력할 수 있는 최대 글자 수 (문자열 끝의 NULL 1바이트 포함해서 32) */
#define      HW_CLI_LINE_BUF_MAX     32
/* 방향키 위/아래로 되돌려 볼 수 있는 과거 명령어 개수                        */
/* RAM 사용량 = LINE_BUF_MAX * LINE_HIS_MAX 만큼 늘어나므로 4개로 제한         */
#define      HW_CLI_LINE_HIS_MAX     4
/* cliAdd() 로 등록할 수 있는 명령어 슬롯 개수                                */
#define      HW_CLI_CMD_LIST_MAX     16
/* 명령어 이름 하나의 최대 길이 (NULL 포함)                                   */
#define      HW_CLI_CMD_NAME_MAX     16
/* 한 줄에서 분리할 수 있는 최대 인자 개수 (명령어 이름 포함)                 */
#define      HW_CLI_CMD_ARGS_MAX     8


#ifdef __cplusplus
}
#endif

#endif /* HW_DEF_H_ */
