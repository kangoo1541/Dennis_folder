/*
 * uart_stm32f411.c
 *
 *  STM32F411 용 UART 드라이버 (HAL 기반, 수신은 인터럽트 + 링버퍼)
 *
 *  [왜 링버퍼(Ring Buffer)를 쓰나요?]
 *   CLI 는 메인 루프에서 가끔씩 문자를 꺼내 갑니다.
 *   그 사이에 사용자가 키를 여러 개 누르면 문자가 사라져 버립니다.
 *   그래서 수신 인터럽트가 발생할 때마다 문자를 배열에 차곡차곡 쌓아두고,
 *   메인 루프는 거기서 하나씩 꺼내 갑니다. 이 배열을 링버퍼라고 부릅니다.
 *
 *     head : 인터럽트가 데이터를 넣는 위치
 *     tail : 메인 루프가 데이터를 꺼내는 위치
 *     둘이 같으면 -> 읽을 데이터 없음
 *
 *  [핀 배치]
 *   UART1 (ch 0) : TX = PA9,  RX = PA10
 *   UART2 (ch 1) : TX = PA2,  RX = PA3
 *
 *  ※ 주의 : 아래의 USART1_IRQHandler / USART2_IRQHandler 는
 *     CubeMX 가 만든 stm32f4xx_it.c 에도 같은 이름으로 들어있을 수 있습니다.
 *     그럴 때는 둘 중 하나를 지워야 "multiple definition" 링크 에러가 안 납니다.
 */

#if defined(USE_HAL_DRIVER)

#include "uart.h"

#ifdef _USE_HW_UART

#include <stdarg.h>
#include "stm32f4xx_hal.h"


#define UART_RX_BUF_MAX     128   /* 링버퍼 크기 (2의 제곱수로 두면 좋음) */


typedef struct
{
  bool               is_open;
  UART_HandleTypeDef handle;

  uint8_t  rx_buf[UART_RX_BUF_MAX];
  uint16_t rx_head;    /* 인터럽트가 쓰는 위치 */
  uint16_t rx_tail;    /* 메인이 읽는 위치     */
  uint8_t  rx_temp;    /* 인터럽트 수신용 1바이트 임시 저장소 */
} uart_tbl_t;


static uart_tbl_t uart_tbl[HW_UART_MAX_CH];


bool uartInit(void)
{
  uint8_t i;

  for (i = 0; i < HW_UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open = false;
    uart_tbl[i].rx_head = 0;
    uart_tbl[i].rx_tail = 0;
  }

  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  GPIO_InitTypeDef  gpio_init = {0};
  uart_tbl_t       *p_uart;

  if (ch >= HW_UART_MAX_CH)
  {
    return false;
  }

  p_uart = &uart_tbl[ch];

  /* UART 공통 설정 : 8비트 데이터, 패리티 없음, 스톱비트 1 (8N1) */
  p_uart->handle.Init.BaudRate     = baud;
  p_uart->handle.Init.WordLength   = UART_WORDLENGTH_8B;
  p_uart->handle.Init.StopBits     = UART_STOPBITS_1;
  p_uart->handle.Init.Parity       = UART_PARITY_NONE;
  p_uart->handle.Init.Mode         = UART_MODE_TX_RX;
  p_uart->handle.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  p_uart->handle.Init.OverSampling = UART_OVERSAMPLING_16;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio_init.Mode      = GPIO_MODE_AF_PP;       /* 대체기능(Alternate Function) 출력 */
  gpio_init.Pull      = GPIO_PULLUP;
  gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init.Alternate = GPIO_AF7_USART1;       /* USART1/2 모두 AF7 */

  switch (ch)
  {
    case _DEF_UART1:
      __HAL_RCC_USART1_CLK_ENABLE();
      p_uart->handle.Instance = USART1;

      gpio_init.Pin = GPIO_PIN_9 | GPIO_PIN_10;   /* PA9(TX), PA10(RX) */
      HAL_GPIO_Init(GPIOA, &gpio_init);

      HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
      HAL_NVIC_EnableIRQ(USART1_IRQn);
      break;

    case _DEF_UART2:
      __HAL_RCC_USART2_CLK_ENABLE();
      p_uart->handle.Instance = USART2;

      gpio_init.Alternate = GPIO_AF7_USART2;
      gpio_init.Pin = GPIO_PIN_2 | GPIO_PIN_3;    /* PA2(TX), PA3(RX) */
      HAL_GPIO_Init(GPIOA, &gpio_init);

      HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
      HAL_NVIC_EnableIRQ(USART2_IRQn);
      break;

    default:
      return false;
  }

  if (HAL_UART_Init(&p_uart->handle) != HAL_OK)
  {
    return false;
  }

  p_uart->rx_head = 0;
  p_uart->rx_tail = 0;
  p_uart->is_open = true;

  /* 1바이트 수신 인터럽트 시작. 받을 때마다 다시 걸어줘야 한다. */
  HAL_UART_Receive_IT(&p_uart->handle, &p_uart->rx_temp, 1);

  return true;
}

uint32_t uartAvailable(uint8_t ch)
{
  uart_tbl_t *p_uart;

  if (ch >= HW_UART_MAX_CH || uart_tbl[ch].is_open != true)
  {
    return 0;
  }

  p_uart = &uart_tbl[ch];

  /* head 가 tail 보다 앞서 있는 만큼이 읽지 않은 데이터 수.
   * 링버퍼라 한 바퀴 돌 수 있으므로 MAX 를 더한 뒤 나머지 연산을 한다. */
  return (uint32_t)((p_uart->rx_head - p_uart->rx_tail + UART_RX_BUF_MAX) % UART_RX_BUF_MAX);
}

uint8_t uartRead(uint8_t ch)
{
  uart_tbl_t *p_uart;
  uint8_t ret;

  if (ch >= HW_UART_MAX_CH)
  {
    return 0;
  }

  p_uart = &uart_tbl[ch];

  if (p_uart->rx_head == p_uart->rx_tail)
  {
    return 0;   /* 읽을 데이터 없음 */
  }

  ret = p_uart->rx_buf[p_uart->rx_tail];
  p_uart->rx_tail = (p_uart->rx_tail + 1) % UART_RX_BUF_MAX;

  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  if (ch >= HW_UART_MAX_CH || uart_tbl[ch].is_open != true)
  {
    return 0;
  }

  /* 송신은 보낼 때까지 기다리는 방식(블로킹). 짧은 문자열이라 문제없다. */
  if (HAL_UART_Transmit(&uart_tbl[ch].handle, p_data, (uint16_t)length, 100) != HAL_OK)
  {
    return 0;
  }

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


/* ---------------------------------------------------------------- */
/*  인터럽트 처리                                                    */
/* ---------------------------------------------------------------- */

/* HAL 이 1바이트 수신을 끝내면 자동으로 불러주는 함수 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint8_t ch;
  uart_tbl_t *p_uart;
  uint16_t next_head;

  for (ch = 0; ch < HW_UART_MAX_CH; ch++)
  {
    p_uart = &uart_tbl[ch];

    if (p_uart->is_open == true && p_uart->handle.Instance == huart->Instance)
    {
      next_head = (p_uart->rx_head + 1) % UART_RX_BUF_MAX;

      /* 버퍼가 가득 차면 가장 오래된 데이터를 지키기 위해 새 데이터를 버린다 */
      if (next_head != p_uart->rx_tail)
      {
        p_uart->rx_buf[p_uart->rx_head] = p_uart->rx_temp;
        p_uart->rx_head = next_head;
      }

      /* 다음 1바이트 수신을 다시 걸어준다 (이걸 빼먹으면 한 글자만 받고 멈춤) */
      HAL_UART_Receive_IT(&p_uart->handle, &p_uart->rx_temp, 1);
      break;
    }
  }
}

/* 통신 에러(오버런 등)가 나도 수신을 다시 걸어 살려낸다 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  uint8_t ch;

  for (ch = 0; ch < HW_UART_MAX_CH; ch++)
  {
    if (uart_tbl[ch].is_open == true && uart_tbl[ch].handle.Instance == huart->Instance)
    {
      HAL_UART_Receive_IT(&uart_tbl[ch].handle, &uart_tbl[ch].rx_temp, 1);
      break;
    }
  }
}

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&uart_tbl[_DEF_UART1].handle);
}

void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&uart_tbl[_DEF_UART2].handle);
}

#endif /* _USE_HW_UART */

#endif /* USE_HAL_DRIVER */
