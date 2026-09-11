/*
 * hw.c
 *
 *  드라이버 초기화 순서를 한곳에서 관리합니다.
 *  순서가 중요합니다.
 *    1) uartInit()  : CLI 가 UART 위에서 동작하므로 UART 가 먼저
 *    2) cliInit()   : 명령어를 등록할 그릇을 먼저 만들고
 *    3) ledInit()   : 각 드라이버가 cliAdd() 로 자기 명령어를 등록
 *    4) cliOpen()   : 마지막에 채널을 열어 프롬프트 출력
 */

#include "hw.h"

bool hwInit(void)
{
#ifdef _USE_HW_UART
  uartInit();
#endif

#ifdef _USE_HW_CLI
  cliInit();
#endif

#ifdef _USE_HW_LED
  ledInit();
#endif

#ifdef _USE_HW_CLI
  /* UART1(USB VCP) : 사용자와 대화하는 CLI 채널 */
  cliOpen(_DEF_UART1, 115200);

  /* UART2(물리 UART) : 눌린 키의 코드값을 확인하는 디버그 채널.
   * 필요 없으면 아래 한 줄을 주석 처리하면 됩니다. */
  cliOpenLog(_DEF_UART2, 115200);
#endif

  return true;
}
