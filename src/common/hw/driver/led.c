/*
 * led.c
 *
 *  LED 드라이버 본체와, CLI 에 명령어를 등록하는 방법을 보여주는 예제.
 *
 *  [모듈별 명령어 등록 패턴]
 *   CLI 에 관련된 코드를 cli.c 한 곳에 몰아넣으면 파일이 점점 커지고
 *   LED 내부 변수에도 접근할 수 없습니다.
 *   그래서 각 드라이버 파일이 자기 명령어를 스스로 등록합니다.
 *
 *      ledInit()  ->  cliAdd("led", cliLed);
 *
 *   이렇게 하면 led.c 파일 하나만 프로젝트에 넣고 빼면
 *   명령어도 같이 생기고 같이 사라집니다.
 */

#include "led.h"

#ifdef _USE_HW_LED

#include "cli.h"
#include "bsp.h"


typedef struct
{
  bool on_state;    /* 현재 켜져 있는지 */
} led_t;


static led_t led_tbl[HW_LED_MAX_CH];

#ifdef _USE_HW_CLI
static void cliLed(cli_args_t *args);
#endif


bool ledInit(void)
{
  uint8_t i;

  for (i = 0; i < HW_LED_MAX_CH; i++)
  {
    led_tbl[i].on_state = false;
  }

  ledHwInit();

  for (i = 0; i < HW_LED_MAX_CH; i++)
  {
    ledOff(i);
  }

#ifdef _USE_HW_CLI
  /* CLI 에 "led" 명령어 등록 */
  cliAdd("led", cliLed);
#endif

  return true;
}

void ledOn(uint8_t ch)
{
  if (ch >= HW_LED_MAX_CH)
  {
    return;
  }
  led_tbl[ch].on_state = true;
  ledHwWrite(ch, true);
}

void ledOff(uint8_t ch)
{
  if (ch >= HW_LED_MAX_CH)
  {
    return;
  }
  led_tbl[ch].on_state = false;
  ledHwWrite(ch, false);
}

void ledToggle(uint8_t ch)
{
  if (ch >= HW_LED_MAX_CH)
  {
    return;
  }
  if (led_tbl[ch].on_state == true)
  {
    ledOff(ch);
  }
  else
  {
    ledOn(ch);
  }
}

bool ledGetState(uint8_t ch)
{
  if (ch >= HW_LED_MAX_CH)
  {
    return false;
  }
  return led_tbl[ch].on_state;
}



#ifdef _USE_HW_CLI
/*
 *  led 명령어 콜백
 *
 *   led list                    : LED 개수와 현재 상태 보기
 *   led on     [ch]             : 켜기
 *   led off    [ch]             : 끄기
 *   led toggle [ch] [time_ms]   : time_ms 주기로 계속 깜빡이기
 *                                 (아무 키나 누르면 멈춤)
 *
 *  args->argc 는 "led" 를 뺀 개수입니다.
 *  "led toggle 0 100" 을 입력하면 argc = 3, argv[0]="toggle" 이 됩니다.
 */
static void cliLed(cli_args_t *args)
{
  bool ret = false;
  uint8_t  ch;
  uint32_t period_ms;
  uint32_t pre_time;

  /* ---- led list ---- */
  if (args->argc == 1 && args->isStr(0, "list") == true)
  {
    cliPrintf("led count : %d\r\n", HW_LED_MAX_CH);
    for (ch = 0; ch < HW_LED_MAX_CH; ch++)
    {
      cliPrintf("  ch %d : %s\r\n", ch, ledGetState(ch) ? "ON" : "OFF");
    }
    ret = true;
  }

  /* ---- led on [ch] ---- */
  if (args->argc == 2 && args->isStr(0, "on") == true)
  {
    ch = (uint8_t)args->getData(1);

    if (ch < HW_LED_MAX_CH)
    {
      ledOn(ch);
      cliPrintf("led %d on\r\n", ch);
    }
    else
    {
      cliPrintf("ch must be 0 ~ %d\r\n", HW_LED_MAX_CH - 1);
    }
    ret = true;
  }

  /* ---- led off [ch] ---- */
  if (args->argc == 2 && args->isStr(0, "off") == true)
  {
    ch = (uint8_t)args->getData(1);

    if (ch < HW_LED_MAX_CH)
    {
      ledOff(ch);
      cliPrintf("led %d off\r\n", ch);
    }
    else
    {
      cliPrintf("ch must be 0 ~ %d\r\n", HW_LED_MAX_CH - 1);
    }
    ret = true;
  }

  /* ---- led toggle [ch] [time_ms] ---- */
  if (args->argc == 3 && args->isStr(0, "toggle") == true)
  {
    ch        = (uint8_t)args->getData(1);
    period_ms = (uint32_t)args->getData(2);

    if (ch >= HW_LED_MAX_CH)
    {
      cliPrintf("ch must be 0 ~ %d\r\n", HW_LED_MAX_CH - 1);
      return;
    }
    if (period_ms == 0)
    {
      cliPrintf("time_ms must be over 0\r\n");
      return;
    }

    cliPrintf("led %d toggle %d ms (press any key to stop)\r\n", ch, period_ms);

    pre_time = millis();

    /*
     *  [논블로킹 루프]
     *   delay(period_ms) 로 기다리면 그 시간 동안 아무것도 못 합니다.
     *   대신 millis() 로 "지난 시간"만 계속 확인하면서 루프를 돌면
     *   그 사이에 다른 일도 할 수 있고, 키 입력도 즉시 감지할 수 있습니다.
     *
     *   cliKeepLoop() 은 터미널에서 키가 눌리는 순간 false 가 되어
     *   루프를 빠져나가게 해 줍니다.
     */
    while (cliKeepLoop() == true)
    {
      if (millis() - pre_time >= period_ms)
      {
        pre_time = millis();
        ledToggle(ch);
      }
    }

    ledOff(ch);
    ret = true;
  }

  /* 어떤 형식에도 맞지 않으면 사용법을 알려준다 */
  if (ret != true)
  {
    cliPrintf("Usage:\r\n");
    cliPrintf("  led list\r\n");
    cliPrintf("  led on     [ch]\r\n");
    cliPrintf("  led off    [ch]\r\n");
    cliPrintf("  led toggle [ch] [time_ms]\r\n");
  }
}
#endif /* _USE_HW_CLI */

#endif /* _USE_HW_LED */
