/*
 * main.c
 *
 *  프로그램의 시작점.
 *  계층 구조 : main -> bsp(보드) -> hw(드라이버) -> ap(응용)
 */

#include "ap.h"


int main(void)
{
  bspInit();   /* 클럭/인터럽트 등 MCU 기본 설정 */
  hwInit();    /* UART, LED, CLI 드라이버 초기화 */
  apInit();    /* 응용 초기화                    */

  apMain();    /* 무한 루프 (여기서 빠져나오지 않음) */

  return 0;
}
