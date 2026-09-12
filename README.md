# STM32F411 CLI (Command Line Interface) 모듈

시리얼 터미널에서 명령어를 쳐서 펌웨어를 조작하는 CLI 모듈입니다.
(STM32 펌웨어 기초 16편 · 17편 내용을 실제로 동작하는 코드로 구현했습니다)

```
cli# help
---------- Command List (4/16) ----------
HELP
MD
INFO
LED
cli# led toggle 0 100
led 0 toggle 100 ms (press any key to stop)
```

---

> 📘 **코드 내부 구조가 궁금하다면** → [docs/STUDY.md](docs/STUDY.md)
> (계층 구조, 핵심 함수 7개 스터디 순서, 설계 이유, 고칠 때 조심할 점)

---

## 1. 이 모듈이 왜 필요한가요?

LED 점멸 주기를 100ms 에서 200ms 로 바꿔 보고 싶다고 해 봅시다.

**CLI 가 없으면**
> 코드 수정 → 컴파일 → ST-Link 로 다운로드 → 확인 → 다시 코드 수정 → ...
> (한 번에 30초 ~ 1분씩 걸립니다)

**CLI 가 있으면**
> 터미널에 `led toggle 0 200` 입력 → 끝 (1초)

ST-Link 를 연결할 필요도 없고, 컴파일도 필요 없습니다.
하드웨어를 검증할 때 시간을 정말 많이 아껴 줍니다.

---

## 2. 폴더 구조

```
src/
├── main.c                         프로그램 시작점
├── ap/                            응용 계층 (메인 루프)
│   ├── ap.c
│   └── ap.h
├── hw/                            드라이버 초기화 + 설정값
│   ├── hw.c
│   ├── hw.h
│   └── hw_def.h                   ★ 버퍼 크기 등 설정을 모아둔 파일
├── bsp/                           보드에 직접 닿는 코드
│   ├── bsp.h
│   ├── bsp_stm32f411.c            STM32F411 : 클럭, LED 핀
│   └── uart_stm32f411.c           STM32F411 : UART (인터럽트 + 링버퍼)
└── common/
    ├── core/def.h                 공통 타입
    └── hw/
        ├── include/
        │   ├── cli.h              ★ CLI 헤더
        │   ├── uart.h             CLI 가 요구하는 UART 인터페이스
        │   └── led.h
        └── driver/
            ├── cli.c              ★★ CLI 본체 (이 프로젝트의 핵심)
            └── led.c              CLI 명령어를 등록하는 예제

simulator/                         보드 없이 PC 에서 돌려보는 환경
├── sim_port.c                     UART/LED 를 PC 의 키보드·화면으로 대체
├── Makefile
└── run_test.sh                    자동 테스트 (33개 항목)
```

---

## 3. 보드 없이 PC 에서 먼저 해보기

STM32 보드가 없어도, 리눅스/맥에서 CLI 를 그대로 체험할 수 있습니다.

```bash
cd simulator
make          # 빌드
make run      # 실행 (Ctrl+C 로 종료)
```

실행 후 이런 것들을 눌러 보세요.

| 해볼 것 | 입력 |
|---|---|
| 명령어 목록 보기 | `help` + Enter |
| 대소문자 섞어 쓰기 | `HeLp` + Enter (그래도 실행됩니다) |
| 오타 수정 | `hellp` → Backspace 로 지우기 |
| 중간 글자 수정 | `hep` → ← 키 → `l` 입력 |
| 이전 명령어 불러오기 | ↑ 키 (최대 4개) |
| LED 깜빡이기 | `led toggle 0 100` → 아무 키나 눌러 중지 |
| 메모리 덤프 | `md` (사용법이 나옵니다) |

자동 테스트도 있습니다.

```bash
make test     # 33개 테스트 항목 검증
```

---

## 4. STM32CubeIDE 프로젝트에 넣는 방법

### 4-1. 파일 복사

`src/` 폴더를 프로젝트에 통째로 복사합니다.
(`simulator/` 폴더는 PC 전용이므로 **넣지 마세요**)

### 4-2. 인클루드 경로 등록

`Project Properties → C/C++ General → Paths and Symbols → Includes` 에서 추가:

```
src
src/ap
src/bsp
src/hw
src/common/core
src/common/hw/include
```

### 4-3. 배선

| 채널 | 용도 | 핀 |
|---|---|---|
| UART1 (ch 0) | CLI 명령 입출력 | PA9(TX), PA10(RX) |
| UART2 (ch 1) | 키 코드 확인용 로그 | PA2(TX), PA3(RX) |

USB-Serial 변환기를 PA9/PA10 에 연결하고 Tera Term 에서 **115200-8-N-1** 로 접속하면 됩니다.
로그 채널이 필요 없으면 `src/hw/hw.c` 의 `cliOpenLog()` 한 줄만 주석 처리하세요.

> **USB VCP(가상 COM 포트)를 쓰고 싶다면?**
> `uart.h` 에 있는 6개 함수를 USB CDC 로 구현해서 `uart_stm32f411.c` 의 ch0 자리에 끼워 넣으면 됩니다.
> `cli.c` 는 한 줄도 고칠 필요가 없습니다.

### 4-4. 주의사항

`uart_stm32f411.c` 안에 `USART1_IRQHandler` / `USART2_IRQHandler` 가 있습니다.
CubeMX 가 만든 `stm32f4xx_it.c` 에도 같은 이름이 있으면 링크 에러(`multiple definition`)가 납니다.
→ **둘 중 하나를 지우세요.**

---

## 5. 내 명령어 추가하기 (3단계)

예를 들어 `motor` 라는 명령어를 만든다면, `motor.c` 안에서:

```c
#include "cli.h"

/* 1단계 : 콜백 함수 만들기 */
static void cliMotor(cli_args_t *args)
{
  bool ret = false;

  /* motor speed 1500  ->  argc=2, argv[0]="speed", argv[1]="1500" */
  if (args->argc == 2 && args->isStr(0, "speed") == true)
  {
    int32_t speed = args->getData(1);   /* 문자열 "1500" -> 숫자 1500 */

    motorSetSpeed(speed);
    cliPrintf("speed : %d\r\n", speed);
    ret = true;
  }

  /* 2단계 : 형식이 안 맞으면 사용법 알려주기 */
  if (ret != true)
  {
    cliPrintf("Usage: motor speed [rpm]\r\n");
  }
}

bool motorInit(void)
{
  /* ... 모터 초기화 ... */

  /* 3단계 : 등록 */
#ifdef _USE_HW_CLI
  cliAdd("motor", cliMotor);
#endif
  return true;
}
```

이게 전부입니다. `cli.c` 는 건드리지 않습니다.

### `args` 로 할 수 있는 것

| 쓰는 법 | 설명 | 예 (`motor speed 1500`) |
|---|---|---|
| `args->argc` | 인자 개수 (명령어 이름 제외) | `2` |
| `args->getStr(0)` | 문자열 그대로 | `"speed"` |
| `args->getData(1)` | 정수로 변환 (`0x` 접두사도 인식) | `1500` |
| `args->getFloat(1)` | 실수로 변환 | `1500.0f` |
| `args->isStr(0, "speed")` | 문자열이 같은지 비교 | `true` |

### 오래 도는 루프를 만들 때는 `cliKeepLoop()`

```c
while (cliKeepLoop() == true)      /* 아무 키나 누르면 false 가 되어 탈출 */
{
  if (millis() - pre_time >= 100)  /* delay() 대신 millis() 로 시간 확인 */
  {
    pre_time = millis();
    ledToggle(0);
  }
}
```

`delay()` 로 기다리면 그 시간 동안 아무것도 못 하고, 무한루프에 빠지면 보드를 리셋해야 합니다.
`cliKeepLoop()` 을 쓰면 키 한 번으로 빠져나올 수 있습니다.

---

## 6. 기본으로 들어있는 명령어

| 명령어 | 설명 |
|---|---|
| `help` | 등록된 명령어 목록 |
| `info` | 펌웨어 버전, 버퍼 설정, 명령어 개수 |
| `md [주소] [길이]` | 메모리 덤프 (Flash/SRAM/레지스터 확인) |
| `led list` / `led on 0` / `led off 0` / `led toggle 0 100` | LED 제어 예제 |

```
cli# md 0x08000000 32
0x08000000 : 00 00 02 20 C1 01 00 08 BD 01 00 08 BF 01 00 08  ... ............
0x08000010 : C1 01 00 08 C3 01 00 08 C5 01 00 08 00 00 00 00  ................
```

> `md` 는 주소를 그대로 읽습니다. **STM32F411 에 실제로 존재하는 주소만** 쓰세요.
> Flash `0x0800_0000` / SRAM `0x2000_0000` / 주변장치 `0x4000_0000`
> 없는 주소를 읽으면 HardFault 가 나서 보드가 멈춥니다.

---

## 7. 동작 원리

### 7-1. 방향키는 1바이트가 아닙니다

`a` 를 누르면 `0x61` 한 바이트가 오지만, **방향키는 3바이트**가 옵니다.

| 키 | 들어오는 코드 |
|---|---|
| Enter | `0x0D` |
| Backspace | `0x08` (터미널에 따라 `0x7F`) |
| ← | `0x1B` `0x5B` `0x44` (`ESC` `[` `D`) |
| → | `0x1B` `0x5B` `0x43` (`ESC` `[` `C`) |
| ↑ | `0x1B` `0x5B` `0x41` (`ESC` `[` `A`) |
| ↓ | `0x1B` `0x5B` `0x42` (`ESC` `[` `B`) |
| Delete | `0x1B` `0x5B` `0x33` `0x7E` (`ESC` `[` `3` `~`) — 4바이트 |
| Home / End | 4바이트 |

`0x1B`(ESC) 를 받은 순간에는 **아직 무슨 키인지 알 수 없습니다.**
그래서 "지금까지 몇 번째 바이트를 받았는지"를 기억하는 변수가 필요합니다. 이게 **상태머신**입니다.

```
CLI_RX_IDLE ──0x1B──> CLI_RX_SP1 ──'['──> CLI_RX_SP2 ──'A'──> 위쪽 방향키!
     ↑                                          │
     └──────────────────────────────────────────┘
                                          └─'3'─> CLI_RX_SP3 ──'~'──> Delete!
```

이렇게 해두면 일반 문자 처리와 특수키 처리가 섞이지 않습니다.

### 7-2. 화면 제어도 같은 방식 (MCU → 터미널)

반대로 MCU 가 터미널에 제어 문자를 보내면 화면을 조작할 수 있습니다. (VT100 규약)

| 보내는 문자열 | 터미널이 하는 일 |
|---|---|
| `\x1B[2K` | 커서가 있는 줄 전체 지우기 |
| `\r` | 커서를 줄 맨 앞으로 |
| `\x1B[3D` | 커서를 왼쪽으로 3칸 |
| `\x1B[3C` | 커서를 오른쪽으로 3칸 |

글자를 중간에 끼워 넣을 때는 "줄 지우기 → 다시 그리기 → 커서를 제자리로" 순서로 처리합니다.

### 7-3. RAM 버퍼와 화면은 따로 관리됩니다

화면에 글자가 예쁘게 보이는 것과, MCU 안의 배열이 올바른 것은 **별개**입니다.
커서가 중간에 있을 때 글자를 넣거나 지우면 배열도 같이 밀어줘야 합니다.

```
버퍼:  h  e  p           커서 위치 2 에 'l' 삽입
                ↓        memmove 로 'p' 를 한 칸 오른쪽으로 밀고
버퍼:  h  e  l  p        빈자리에 'l' 을 넣는다
```

`memmove()` 는 메모리 영역이 겹쳐도 안전하게 옮겨주는 표준 함수입니다.
(`memcpy()` 는 겹치면 결과를 보장하지 않으므로 여기서는 쓰면 안 됩니다)

### 7-4. 히스토리는 원형 버퍼

저장 칸은 4개인데 5번째 명령어가 들어오면?
→ 가장 오래된 칸에 덮어쓰고 `0 → 1 → 2 → 3 → 0 → 1 ...` 로 돌립니다.
나머지 연산(`%`) 하나면 구현됩니다.

```c
hist_head = (hist_head + 1) % CLI_LINE_HIS_MAX;
```

### 7-5. 명령어 실행 과정

```
"led toggle 0 100"  입력
        │
        ├─ strtok() 으로 공백/탭 단위로 자른다
        │     argv[0]="led"  argv[1]="toggle"  argv[2]="0"  argv[3]="100"
        │
        ├─ argv[0] 을 대문자로 → "LED"   (대소문자 구분 없이 쓰라고)
        │
        ├─ 등록된 목록에서 "LED" 를 찾는다
        │
        └─ 콜백 호출. 이때 명령어 이름은 빼고 넘긴다
              argc = 4-1 = 3,  argv = &argv[1]
              → 콜백 안에서는 args->argv[0] 이 "toggle"
```

---

## 8. 설정 바꾸기 (`src/hw/hw_def.h`)

| 매크로 | 기본값 | 설명 |
|---|---|---|
| `_USE_HW_CLI` | 정의됨 | 주석 처리하면 CLI 코드가 통째로 빠집니다 |
| `HW_CLI_LINE_BUF_MAX` | 32 | 한 줄 최대 글자 수 (NULL 포함) |
| `HW_CLI_LINE_HIS_MAX` | 4 | ↑키로 불러올 명령어 개수 |
| `HW_CLI_CMD_LIST_MAX` | 16 | `cliAdd()` 로 등록 가능한 명령어 수 |
| `HW_CLI_CMD_NAME_MAX` | 16 | 명령어 이름 최대 길이 |
| `HW_CLI_CMD_ARGS_MAX` | 8 | 한 줄에서 분리할 최대 인자 개수 |

**RAM 사용량 계산**
히스토리 버퍼가 가장 많이 차지합니다: `32 × 4 = 128 바이트`
명령어가 길어졌다면 `HW_CLI_LINE_BUF_MAX` 를 64로 늘리세요 (그러면 256 바이트).

---

## 9. 자주 겪는 문제

**터미널에 아무것도 안 나와요**
- 보레이트가 115200 인지 확인
- TX/RX 가 서로 교차(TX↔RX)되어 연결됐는지 확인
- `hwInit()` 안에서 `cliOpen()` 이 호출되는지 확인

**글자가 두 번씩 보여요 (`hheellpp`)**
- 터미널의 **로컬 에코(Local Echo)** 설정을 끄세요.
  CLI 가 직접 글자를 되돌려 보내기 때문에 터미널까지 찍으면 두 번 보입니다.
- Tera Term: `Setup → Terminal → Local echo` 체크 해제

**Enter 를 치면 줄이 두 번 넘어가요**
- Tera Term: `Setup → Terminal → New-line Transmit` 을 `CR` 로 설정

**방향키를 누르면 `^[[A` 같은 이상한 글자가 찍혀요**
- 터미널이 VT100 모드가 아닙니다. `Setup → Terminal → Terminal ID` 를 `VT100` 으로 설정

**한 글자만 받고 그다음부터 반응이 없어요**
- 수신 인터럽트를 다시 걸지 않은 경우입니다.
  `HAL_UART_Receive_IT()` 는 1바이트를 받으면 자동으로 해제되므로,
  `HAL_UART_RxCpltCallback()` 안에서 **매번 다시 호출**해 줘야 합니다.

**DFU 부트로더를 쓰는데 디버깅이 안 돼요**
- 부트로더가 플래시에 없는 상태에서 메인 펌웨어만 단독으로 디버깅하면,
  리셋 직후 부트로더 시작 주소로 분기해서 멈춰버립니다.
- `Debug Configuration → Debugger 탭 → Reset behavior` 를 **`None`** 으로 바꾸세요.

---

## 10. 검증 내역

`simulator/run_test.sh` 로 33개 항목을 자동 검증합니다. (`cd simulator && make test`)

- 기본 명령어 (help / info / led list) 동작
- 인자 파싱, 대소문자 무시, 사용법 안내, 없는 명령어 처리
- 라인 편집: Backspace / Delete / Home / End / ←→ 삽입
- 히스토리: ↑↓ 이동, 원형 버퍼 4개 제한, 불러와서 수정 후 재실행
- `md` 덤프 출력 형식, 잘못된 길이 거부
- 32자 초과 입력 시 버퍼 오버플로우 방어
- `cliKeepLoop()` 로 루프 탈출

AddressSanitizer / UndefinedBehaviorSanitizer 를 켜고 실행해도 경고가 없습니다.
