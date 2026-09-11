/*
 * sim_port.c
 *
 *  PC(리눅스/맥) 에서 CLI 를 그대로 돌려보기 위한 가짜 하드웨어 구현.
 *
 *  cli.c 는 uart.h 에 적힌 함수만 사용하기 때문에,
 *  그 함수들을 PC 의 키보드/화면으로 바꿔 끼우면
 *  STM32 보드 없이도 CLI 동작을 그대로 확인할 수 있습니다.
 *
 *    UART1 (ch 0) : 키보드 입력  <-> 화면 출력 (stdout)
 *    UART2 (ch 1) : 키 코드 로그 -> stderr
 *
 *  ※ 이 파일은 PC 전용입니다. STM32 프로젝트에는 넣지 마세요.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <time.h>

#include "bsp.h"
#include "uart.h"
#include "led.h"


#define SIM_RX_BUF_MAX   256


static uint8_t  rx_buf[SIM_RX_BUF_MAX];
static uint16_t rx_head = 0;
static uint16_t rx_tail = 0;
static bool     is_input_closed = false;

static struct termios  term_backup;
static bool            is_term_changed = false;
static uint32_t        start_time_ms = 0;


static uint32_t simGetTimeMs(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* 프로그램이 끝날 때 터미널 설정을 원래대로 되돌린다 */
static void simRestoreTerm(void)
{
  if (is_term_changed == true)
  {
    tcsetattr(STDIN_FILENO, TCSANOW, &term_backup);
    is_term_changed = false;
  }
}

/*
 *  키보드 입력을 "한 글자씩 즉시" 받도록 터미널을 바꾼다.
 *   ICANON 끄기 : Enter 를 누를 때까지 기다리지 않고 한 글자씩 전달
 *   ECHO   끄기 : 터미널이 글자를 자동으로 찍지 않게 함 (CLI 가 직접 찍으므로)
 *   ISIG 는 켠 채로 두어 Ctrl+C 로 빠져나올 수 있게 한다.
 */
static void simSetTermRaw(void)
{
  struct termios term;

  if (isatty(STDIN_FILENO) == 0)
  {
    return;   /* 파이프로 입력을 받는 경우(자동 테스트)는 설정할 것이 없다 */
  }

  tcgetattr(STDIN_FILENO, &term_backup);
  term = term_backup;

  term.c_lflag &= ~(ICANON | ECHO);
  term.c_cc[VMIN]  = 0;
  term.c_cc[VTIME] = 0;

  tcsetattr(STDIN_FILENO, TCSANOW, &term);

  is_term_changed = true;
  atexit(simRestoreTerm);
}

/* 키보드에 눌린 것이 있으면 링버퍼에 담아둔다 */
static void simPollInput(void)
{
  struct pollfd fds;
  uint8_t  buf[64];
  ssize_t  len;
  ssize_t  i;
  uint16_t next_head;

  if (is_input_closed == true)
  {
    return;
  }

  fds.fd     = STDIN_FILENO;
  fds.events = POLLIN;

  if (poll(&fds, 1, 0) <= 0)
  {
    return;   /* 아직 눌린 키가 없다 */
  }

  len = read(STDIN_FILENO, buf, sizeof(buf));

  if (len <= 0)
  {
    is_input_closed = true;   /* 입력이 끝났다 (Ctrl+D 또는 파이프 종료) */
    return;
  }

  for (i = 0; i < len; i++)
  {
    next_head = (uint16_t)((rx_head + 1) % SIM_RX_BUF_MAX);
    if (next_head != rx_tail)
    {
      rx_buf[rx_head] = buf[i];
      rx_head = next_head;
    }
  }
}


/* ================================================================ */
/*  BSP 구현                                                        */
/* ================================================================ */

bool bspInit(void)
{
  start_time_ms = simGetTimeMs();
  simSetTermRaw();

  return true;
}

uint32_t millis(void)
{
  return simGetTimeMs() - start_time_ms;
}

void delay(uint32_t time_ms)
{
  struct timespec ts;

  ts.tv_sec  = time_ms / 1000;
  ts.tv_nsec = (long)(time_ms % 1000) * 1000000L;

  nanosleep(&ts, NULL);
}


/* ================================================================ */
/*  LED 구현 (화면에 글자로 표시)                                    */
/* ================================================================ */

bool ledHwInit(void)
{
  return true;
}

void ledHwWrite(uint8_t ch, bool on_state)
{
  /* LED 상태는 stderr 로 뽑아서 CLI 화면(stdout)을 방해하지 않게 한다 */
  fprintf(stderr, "[LED%d] %s\n", ch, on_state ? "ON " : "OFF");
  fflush(stderr);
}


/* ================================================================ */
/*  UART 구현                                                       */
/* ================================================================ */

bool uartInit(void)
{
  rx_head = 0;
  rx_tail = 0;

  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  (void)baud;

  if (ch >= HW_UART_MAX_CH)
  {
    return false;
  }

  return true;
}

uint32_t uartAvailable(uint8_t ch)
{
  if (ch != _DEF_UART1)
  {
    return 0;   /* 로그 채널은 송신 전용 */
  }

  simPollInput();

  /* 입력이 끝났고 남은 데이터도 없으면 시뮬레이터를 종료한다 */
  if (is_input_closed == true && rx_head == rx_tail)
  {
    fprintf(stdout, "\r\n[simulator] input closed. bye.\r\n");
    fflush(stdout);
    exit(0);
  }

  return (uint32_t)((rx_head - rx_tail + SIM_RX_BUF_MAX) % SIM_RX_BUF_MAX);
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret;

  if (ch != _DEF_UART1 || rx_head == rx_tail)
  {
    return 0;
  }

  ret = rx_buf[rx_tail];
  rx_tail = (uint16_t)((rx_tail + 1) % SIM_RX_BUF_MAX);

  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  FILE *fp;

  fp = (ch == _DEF_UART1) ? stdout : stderr;

  fwrite(p_data, 1, length, fp);
  fflush(fp);

  return length;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  va_list arg;
  int len;
  char buf[256];

  va_start(arg, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, arg);
  va_end(arg);

  if (len <= 0)
  {
    return 0;
  }
  if (len > (int)sizeof(buf) - 1)
  {
    len = (int)sizeof(buf) - 1;
  }

  return uartWrite(ch, (uint8_t *)buf, (uint32_t)len);
}
