/*
 * bsp_stm32f411.c
 *
 *  STM32F411 실제 보드용 구현 (STM32CubeIDE 프로젝트에서만 컴파일됨)
 *
 *  ※ 이 파일은 HAL 라이브러리가 있어야 빌드됩니다.
 *     그래서 USE_HAL_DRIVER 매크로가 정의된 경우에만 내용이 살아납니다.
 *     PC 시뮬레이터로 빌드할 때는 통째로 비어 있는 파일이 됩니다.
 */

#if defined(USE_HAL_DRIVER)

#include "bsp.h"
#include "led.h"
#include "stm32f4xx_hal.h"


/* ---------------------------------------------------------------- */
/*  LED 핀 설정 — 본인 보드에 맞게 수정하세요                         */
/* ---------------------------------------------------------------- */
/*  WeAct Black Pill (STM32F411CEU6) : PC13, LOW 일 때 켜짐(Active Low) */
#define LED_GPIO_PORT     GPIOC
#define LED_GPIO_PIN      GPIO_PIN_13
#define LED_GPIO_CLK_EN() __HAL_RCC_GPIOC_CLK_ENABLE()
#define LED_ON_STATE      GPIO_PIN_RESET   /* Active Low 보드 기준 */
#define LED_OFF_STATE     GPIO_PIN_SET


static void SystemClock_Config(void);


bool bspInit(void)
{
  HAL_Init();              /* HAL 초기화 + SysTick 1ms 설정 */
  SystemClock_Config();    /* 클럭 설정 (96MHz / USB 48MHz) */

  return true;
}

uint32_t millis(void)
{
  return HAL_GetTick();
}

void delay(uint32_t time_ms)
{
  HAL_Delay(time_ms);
}


/* ---------------------------------------------------------------- */
/*  LED 하드웨어 제어                                                */
/* ---------------------------------------------------------------- */
bool ledHwInit(void)
{
  GPIO_InitTypeDef gpio_init = {0};

  LED_GPIO_CLK_EN();

  gpio_init.Pin   = LED_GPIO_PIN;
  gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;   /* 푸시풀 출력 */
  gpio_init.Pull  = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_PORT, &gpio_init);

  HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, LED_OFF_STATE);

  return true;
}

void ledHwWrite(uint8_t ch, bool on_state)
{
  if (ch != 0)
  {
    return;
  }

  HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN,
                    on_state ? LED_ON_STATE : LED_OFF_STATE);
}


/* ---------------------------------------------------------------- */
/*  클럭 설정                                                        */
/* ---------------------------------------------------------------- */
/*
 *  내부 발진기(HSI 16MHz)만으로 96MHz 를 만드는 설정입니다.
 *  외부 크리스탈이 없는 보드에서도 동작하도록 이렇게 잡았습니다.
 *
 *    VCO 입력  = 16MHz / PLLM(16) = 1MHz
 *    VCO 출력  = 1MHz  * PLLN(384) = 384MHz
 *    SYSCLK    = 384MHz / PLLP(4)  = 96MHz
 *    USB 클럭  = 384MHz / PLLQ(8)  = 48MHz   <- USB CDC 를 쓰려면 꼭 48MHz 여야 함
 *
 *  ※ CubeMX 로 만든 SystemClock_Config() 가 이미 있다면 그것을 쓰고
 *     이 함수는 지우세요.
 */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc_init = {0};
  RCC_ClkInitTypeDef clk_init = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  osc_init.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  osc_init.HSIState            = RCC_HSI_ON;
  osc_init.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc_init.PLL.PLLState        = RCC_PLL_ON;
  osc_init.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
  osc_init.PLL.PLLM            = 16;
  osc_init.PLL.PLLN            = 384;
  osc_init.PLL.PLLP            = RCC_PLLP_DIV4;
  osc_init.PLL.PLLQ            = 8;
  if (HAL_RCC_OscConfig(&osc_init) != HAL_OK)
  {
    while (1);   /* 클럭 설정 실패 : 여기서 멈춤 */
  }

  clk_init.ClockType      = RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK
                          | RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
  clk_init.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk_init.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* HCLK  = 96MHz */
  clk_init.APB1CLKDivider = RCC_HCLK_DIV2;     /* PCLK1 = 48MHz (최대 50MHz) */
  clk_init.APB2CLKDivider = RCC_HCLK_DIV1;     /* PCLK2 = 96MHz */
  if (HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_3) != HAL_OK)
  {
    while (1);
  }
}

#endif /* USE_HAL_DRIVER */
