/*
 * ap.c
 *
 *  메인 루프. 여기서 cliMain() 을 계속 호출해 주어야 CLI 가 동작합니다.
 */

#include "ap.h"


void apInit(void)
{
  /* 응용 초기화가 필요하면 여기에 */
}

void apMain(void)
{
  uint32_t pre_time = millis();

  while (1)
  {
#ifdef _USE_HW_LED
    /* 1초마다 LED 를 깜빡여 "펌웨어가 살아있다"는 것을 표시 (heartbeat) */
    if (millis() - pre_time >= 1000)
    {
      pre_time = millis();
      ledToggle(_DEF_LED_HEARTBEAT);
    }
#endif

    /*
     * CLI 처리.
     * 수신된 문자가 없으면 아무것도 하지 않고 바로 반환되므로
     * 위의 LED 처리가 밀리지 않습니다.
     */
#ifdef _USE_HW_CLI
    cliMain();
#endif
  }
}
