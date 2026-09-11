/*
 * cli.h
 *
 *  CLI (Command Line Interface) 모듈
 *
 *  [이 모듈이 필요한 이유]
 *   펌웨어를 조금 바꿀 때마다 "코드 수정 -> 컴파일 -> 다운로드 -> 확인"을
 *   반복하면 시간이 너무 오래 걸립니다.
 *   CLI 를 넣어두면 ST-Link 를 연결하지 않아도 시리얼 터미널(Tera Term 등)에서
 *
 *      cli# led toggle 1 100
 *
 *   처럼 글자를 쳐서 곧바로 하드웨어를 테스트할 수 있습니다.
 *
 *  [제공 기능]
 *   1. 한 줄 입력 편집 : 커서 좌/우 이동, 중간 삽입, Backspace/Delete
 *   2. 명령어 히스토리 : 방향키 위/아래로 예전에 친 명령어 불러오기 (원형 버퍼)
 *   3. 명령어 실행 엔진 : 문자열을 잘라서(파싱) 등록된 함수를 호출
 */

#ifndef CLI_H_
#define CLI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_CLI

#define CLI_LINE_BUF_MAX      HW_CLI_LINE_BUF_MAX
#define CLI_LINE_HIS_MAX      HW_CLI_LINE_HIS_MAX
#define CLI_CMD_LIST_MAX      HW_CLI_CMD_LIST_MAX
#define CLI_CMD_NAME_MAX      HW_CLI_CMD_NAME_MAX
#define CLI_CMD_ARGS_MAX      HW_CLI_CMD_ARGS_MAX


/*
 * 명령어 콜백 함수로 전달되는 인자 묶음
 *
 *  예) 터미널에 "led toggle 1 100" 을 입력하면
 *      argc = 3
 *      argv[0] = "toggle", argv[1] = "1", argv[2] = "100"
 *      (명령어 이름 "led" 는 빠지고 뒤의 인자들만 전달됩니다)
 *
 *  구조체 안에 함수 포인터를 넣어두면 콜백 안에서
 *      args->getData(1)        -> 1   (문자열 "1" 을 숫자로 변환)
 *      args->isStr(0, "toggle")-> true
 *  처럼 짧게 쓸 수 있어 코드가 훨씬 읽기 좋아집니다.
 *  (함수 포인터 4개 = 32비트 MCU 기준 16바이트를 더 쓰는 대신 편의성을 얻는 것)
 */
typedef struct
{
  uint8_t   argc;      /* 인자 개수                     */
  char    **argv;      /* 인자 문자열들의 포인터 배열   */

  int32_t (*getData) (uint8_t index);                     /* 정수로 변환   */
  float   (*getFloat)(uint8_t index);                     /* 실수로 변환   */
  char   *(*getStr)  (uint8_t index);                     /* 문자열 그대로 */
  bool    (*isStr)   (uint8_t index, const char *p_str);  /* 문자열 비교   */
} cli_args_t;


/* 명령어가 실행될 때 호출되는 함수의 모양 */
typedef void (*cliFunc_t)(cli_args_t *args);


/* ---------------------------------------------------------------- */
/*  공개 함수                                                        */
/* ---------------------------------------------------------------- */

/* CLI 내부 변수 초기화 + 기본 명령어(help, md, info) 등록 */
bool cliInit(void);

/* CLI 를 사용할 UART 채널 열기 (사용자 입력/프롬프트 출력 채널) */
bool cliOpen(uint8_t ch, uint32_t baud);

/* 눌린 키의 코드값을 확인하기 위한 디버그 로그 채널 열기 (선택 사항) */
bool cliOpenLog(uint8_t ch, uint32_t baud);

/* 메인 루프에서 계속 호출해 주는 함수. 수신된 문자를 1개씩 처리한다.
 * 반환값 : 한 줄이 완성되어 명령어를 실행했으면 true */
bool cliMain(void);

/* 명령어 등록. cmd_str 이름으로 func 함수를 연결한다.
 * 예) cliAdd("led", cliLed); */
bool cliAdd(const char *cmd_str, cliFunc_t func);

/* 명령어 콜백 안에서 while 루프를 돌 때 탈출 조건으로 사용.
 * 터미널에서 아무 키나 누르면(수신 데이터가 생기면) false 를 반환한다. */
bool cliKeepLoop(void);

/* CLI 채널로 printf */
void cliPrintf(const char *fmt, ...);

/* CLI 가 열려 있는지 확인 */
bool cliIsOpen(void);


#endif /* _USE_HW_CLI */

#ifdef __cplusplus
}
#endif

#endif /* CLI_H_ */
