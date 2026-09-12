# 코드 아키텍처 스터디 가이드

이 프로젝트(STM32F411 CLI 모듈)가 **어떻게 설계되어 있는지**, 그리고
**핵심 코드를 어떤 순서로 읽으면 되는지** 정리한 문서입니다.

README.md 가 "쓰는 법"이라면, 이 문서는 "만든 법"입니다.

전체 2,421줄 / 18개 파일이지만, 진짜 핵심은 `cli.c` 1,009줄 안의 **5개 함수**입니다.

---

## 0. 30초 요약

| 항목 | 내용 |
|---|---|
| 무엇 | 시리얼 터미널에서 명령어를 쳐서 펌웨어를 조작하는 CLI 모듈 |
| 구조 | **4계층** (main → bsp → hw → ap) + 인터페이스로 분리된 드라이버 |
| 핵심 기법 | ① 상태머신 ② 링버퍼 ③ 원형버퍼 ④ 함수 포인터 테이블 ⑤ 논블로킹 폴링 |
| 이식성 | `uart.h` 의 **함수 6개**만 구현하면 어떤 플랫폼에서도 동작 (PC 시뮬레이터가 증거) |
| 검증 | PC 시뮬레이터에서 33개 항목 자동 테스트 통과 (ASan/UBSan 클린) |

---

## 1. 전체 아키텍처 — 왜 폴더를 이렇게 나눴나

### 1-1. 4계층 구조

```
   ┌─────────────────────────────────────────────┐
   │  ap/        응용 계층                        │  "무엇을 할 것인가"
   │             apMain() 무한루프                │
   └──────────────────┬──────────────────────────┘
                      │ 호출
   ┌──────────────────▼──────────────────────────┐
   │  common/hw/driver/   공통 드라이버           │  "어떻게 할 것인가"
   │             cli.c, led.c                    │  (MCU 종류를 모름!)
   └──────────────────┬──────────────────────────┘
                      │ uart.h / bsp.h 의 함수만 호출
   ┌──────────────────▼──────────────────────────┐
   │  bsp/       보드 지원 계층                   │  "이 칩에서는 이렇게"
   │             uart_stm32f411.c, bsp_stm32f411.c│
   └──────────────────┬──────────────────────────┘
                      │
   ┌──────────────────▼──────────────────────────┐
   │  STM32 HAL / 레지스터                        │
   └─────────────────────────────────────────────┘

   hw/    : 위 계층들을 조립하고(hw.c) 설정값을 모아두는(hw_def.h) 곳
```

### 1-2. 이 구조의 핵심 규칙 — **의존성은 아래로만**

> `cli.c` 안에는 `stm32f4xx_hal.h` 가 **단 한 줄도 없습니다.**

직접 확인해 보세요.

```bash
grep -rn "stm32f4xx_hal" src/
# -> src/bsp/ 안의 2개 파일에서만 나옵니다.
```

`cli.c` 는 `uart.h` 에 적힌 함수 6개만 씁니다.

```c
uartInit()      uartOpen()      uartAvailable()
uartRead()      uartWrite()     uartPrintf()
```

그래서 이 6개를 PC 의 키보드/화면으로 바꿔 끼운 게 `simulator/sim_port.c` 입니다.
**`cli.c` 를 한 줄도 안 고치고** 보드 없이 PC 에서 그대로 돌아갑니다.
USB VCP(가상 COM 포트)로 바꾸고 싶을 때도 이 6개만 다시 구현하면 끝입니다.

이걸 **HAL(Hardware Abstraction Layer)** 또는 **포트 계층 분리**라고 부릅니다.
임베디드에서 가장 많이 쓰는 설계 패턴이니 이 프로젝트에서 확실히 익혀두세요.

### 1-3. 파일별 역할 한 줄 요약

| 파일 | 줄 수 | 역할 |
|---|---:|---|
| `main.c` | 20 | 시작점. bsp → hw → ap 순서로 초기화 |
| `ap/ap.c` | 39 | 무한루프. `cliMain()` 을 계속 호출 |
| `hw/hw.c` | 38 | **드라이버 초기화 순서**를 결정하는 곳 |
| `hw/hw_def.h` | 70 | 모든 설정 스위치/크기 상수 (★ 여기만 고치면 됨) |
| `common/core/def.h` | 52 | 공통 타입, 에러코드 |
| `common/hw/include/*.h` | - | **인터페이스(약속)** 정의 — 구현은 없음 |
| `common/hw/driver/cli.c` | **1009** | ★★ CLI 본체 |
| `common/hw/driver/led.c` | 229 | 명령어 등록 방법을 보여주는 예제 |
| `bsp/uart_stm32f411.c` | 277 | STM32 UART (인터럽트 + 링버퍼) |
| `bsp/bsp_stm32f411.c` | 132 | 클럭, LED 핀, millis() |
| `simulator/sim_port.c` | 268 | 위 bsp 2개를 PC 용으로 대체한 것 |

---

## 2. 부팅부터 명령 실행까지 — 흐름 따라가기

### 2-1. 부팅 순서 (`main.c` → `hw.c`)

```c
int main(void)
{
  bspInit();   /* 1. 클럭 96MHz, SysTick 1ms 시작 */
  hwInit();    /* 2. 드라이버 초기화 (아래 참고)  */
  apInit();    /* 3. 응용 초기화                  */
  apMain();    /* 4. 무한루프 (return 없음)       */
}
```

`hwInit()` 의 **순서가 왜 중요한지**가 이 프로젝트의 첫 번째 포인트입니다.

```c
uartInit();              /* ① CLI 가 UART 위에서 도니까 UART 가 먼저 */
cliInit();               /* ② 명령어를 담을 "그릇"을 먼저 만든다     */
ledInit();               /* ③ 각 드라이버가 cliAdd() 로 자기 명령 등록 */
cliOpen(_DEF_UART1, ...) /* ④ 마지막에 채널을 열고 프롬프트 출력     */
```

순서를 바꾸면?
- `cliInit()` 보다 `ledInit()` 이 먼저면 → `cliAdd("led", ...)` 가 `memset` 으로 지워집니다.
- `cliOpen()` 을 먼저 하면 → 환영 메시지가 나온 뒤에 등록이 되어 `help` 결과가 달라집니다.

### 2-2. 키 하나가 화면에 찍히기까지

```
 [사용자가 'a' 키를 누름]
        │
        ▼
 USART1 하드웨어가 1바이트 수신 → 인터럽트 발생
        │
        ▼  (uart_stm32f411.c)
 HAL_UART_RxCpltCallback()  ── rx_buf[head++] = 'a'   ← 인터럽트가 "생산"
        │                      HAL_UART_Receive_IT() 다시 걸기 (★ 필수)
        │
        ▼  (메인 루프, ap.c)
 apMain() → cliMain()
        │
        ├─ uartAvailable() > 0 ?  ── 없으면 즉시 return (논블로킹)
        ├─ uartRead()  ── rx_buf[tail++] 에서 'a' 꺼냄  ← 메인이 "소비"
        │
        ▼  (cli.c)
 cliUpdate('a')  ── 상태머신: 일반 문자다 → cliLineInsert()
        │
        ├─ line.buf[cursor] = 'a'   ← RAM 버퍼 갱신
        └─ cliPrintf("%c", 'a')     ← 화면에 에코 출력
```

**여기서 꼭 기억할 것 2가지**

1. **인터럽트는 넣기만, 메인은 꺼내기만** 합니다 (링버퍼 = 생산자/소비자 분리).
   이렇게 하면 메인 루프가 다른 일을 하는 동안 키를 눌러도 글자가 안 사라집니다.
2. **RAM 버퍼(`line.buf`)와 화면은 별개**입니다.
   화면에 예쁘게 보인다고 배열이 맞는 게 아니고, 둘 다 따로 맞춰줘야 합니다.

### 2-3. Enter 를 누르면 (`cliMain()` 내부)

```c
if (cliUpdate(rx_data) == true)   /* Enter 를 눌러 한 줄 완성 */
{
  if (cli_node.line.count > 0)
  {
    cliHistoryAdd();   /* ★ 먼저 히스토리 저장 */
    cliRunCmd();       /* ★ 그다음 실행       */
  }
  cliLineClear();
  cliShowPrompt();
}
```

> **왜 히스토리 저장이 먼저인가?**
> `cliRunCmd()` 안의 `strtok()` 이 `line.buf` **원본을 직접 잘라서** 공백 자리에
> `'\0'` 을 박아 넣습니다. 실행 후에 저장하면 히스토리에 `"led"` 만 남고
> `" toggle 0 100"` 은 사라집니다. **순서가 곧 버그**인 대표적인 예입니다.

---

## 3. 핵심 코드 스터디 — 이 순서로 읽으세요

### 📖 STEP 1. `cliMain()` — 논블로킹 폴링 (cli.c 213줄)

```c
bool cliMain(void)
{
  if (cli_node.is_open != true) return false;

  if (uartAvailable(cli_node.ch) > 0)   /* ★ 데이터 있을 때만 처리 */
  {
    rx_data = uartRead(cli_node.ch);
    ...
  }
  return ret;
}
```

**배울 점 : 임베디드 메인 루프의 기본 형태**

```c
while (1)
{
  if (millis() - pre_time >= 1000) { ledToggle(0); }  /* 일 A */
  cliMain();                                          /* 일 B */
}
```

`cliMain()` 이 "데이터가 없으면 즉시 반환"하기 때문에 LED 깜빡임이 밀리지 않습니다.
만약 여기서 `scanf()` 처럼 키를 기다렸다면(**블로킹**) LED 는 영영 안 깜빡입니다.

> 💡 **연습** : `ap.c` 의 LED 주기를 100ms 로 바꾸고, `cliMain()` 앞에
> `delay(500);` 을 넣어보세요. 왜 CLI 가 버벅이는지 몸으로 알게 됩니다.

---

### 📖 STEP 2. `cliUpdate()` — 상태머신 (cli.c 252줄) ★ 가장 중요

**문제 상황부터 이해하기**

| 키 | 들어오는 바이트 |
|---|---|
| `a` | `0x61` (1바이트) |
| ← | `0x1B` `0x5B` `0x44` (3바이트) |
| Delete | `0x1B` `0x5B` `0x33` `0x7E` (4바이트) |

`0x1B`(ESC)를 받은 **그 순간에는 무슨 키인지 알 수 없습니다.** 다음 바이트를 봐야 합니다.
그렇다고 "다음 바이트가 올 때까지 기다리기"는 블로킹이라 안 됩니다.

**해법 : "지금 몇 번째 바이트를 받는 중인지"를 변수에 기억한다**

```
        ┌──────────────────────────────────────────────────┐
        │                                                  │
        ▼                                                  │
  ┌───────────┐  0x1B   ┌──────────┐  '['   ┌──────────┐   │
  │ RX_IDLE   │────────►│  RX_SP1  │───────►│  RX_SP2  │   │
  │ 일반문자   │         │ ESC 받음  │        │ ESC[ 받음│   │
  └───────────┘         └──────────┘        └────┬─────┘   │
        ▲                                        │         │
        │                        'A' ← ↑키       │         │
        │                        'B' ← ↓키       │         │
        │                        'C' ← →키  ─────┼─────────┘
        │                        'D' ← ←키       │
        │                                        │ '1'~'8'
        │                                        ▼
        │                                   ┌──────────┐
        └───────────────────────────────────│  RX_SP3  │
                        '~' → Delete/Home/End└──────────┘
```

코드로는 `switch(state)` 안에 `switch(rx_data)` 중첩 하나입니다.

```c
switch (cli_node.state)
{
  case CLI_RX_IDLE:
    if (rx_data == CLI_KEY_ESC) { cli_node.state = CLI_RX_SP1; break; }  /* 특수키 시작 */
    if (rx_data == CLI_KEY_ENTER) { ... ret = true; break; }             /* 줄 완성 */
    if (rx_data >= 0x20 && rx_data <= 0x7E) cliLineInsert(rx_data);      /* 보이는 문자만 */
    break;

  case CLI_RX_SP1:
    cli_node.state = (rx_data == '[') ? CLI_RX_SP2 : CLI_RX_IDLE;  /* 모르면 버리고 복귀 */
    break;

  case CLI_RX_SP2:
    switch (rx_data) {
      case 'A': cliLineChange(-1); cli_node.state = CLI_RX_IDLE; break;  /* ↑ */
      case 'D': cliCursorMove(-1); cli_node.state = CLI_RX_IDLE; break;  /* ← */
      default:
        if (rx_data >= '1' && rx_data <= '8') {   /* 4바이트 키는 한 단계 더 */
          cli_node.sp_data = rx_data;
          cli_node.state   = CLI_RX_SP3;
        } else cli_node.state = CLI_RX_IDLE;
    }
    break;
}
```

**배울 점**
- 상태머신 = "시간에 걸쳐 나눠 들어오는 데이터"를 논블로킹으로 처리하는 표준 기법입니다.
- 모르는 조합은 **무조건 IDLE 로 복귀**시킵니다. 안 그러면 한 번 꼬이면 영원히 먹통.
- 이 패턴은 CLI 말고도 **모드버스, 패킷 프로토콜, GPS NMEA 파싱** 등에 그대로 씁니다.

> 💡 **연습** : `cd simulator && make run` 후 방향키를 누르면서
> stderr 로그(`rx:0x1B` `rx:0x5B` `rx:0x41`)를 직접 확인해 보세요.
> 로그를 찍는 건 `cliShowLog()` (cli.c 836줄) 입니다.

---

### 📖 STEP 3. 라인 편집 — `memmove` + VT100 (cli.c 412~581줄)

**핵심 딜레마 : 버퍼와 화면을 둘 다 맞춰야 한다**

`hep` 에서 커서를 `e` 와 `p` 사이에 두고 `l` 을 입력하면:

```
 [RAM 버퍼]                          [화면]
  h  e  p                            "cli# hep"  (커서는 p 앞)
       ↑ cursor=2
                                     ① \x1B[2K  : 줄 전체 지우기
  memmove 로 'p' 를 한 칸 밀고        ② \r       : 커서 맨 앞으로
  h  e  _  p                         ③ "cli# help" 다시 출력
  빈자리에 'l'                        ④ \x1B[1D  : 커서 1칸 왼쪽
  h  e  l  p                            (buf 의 cursor 위치와 일치시킴)
          ↑ cursor=3
```

```c
static void cliLineInsert(uint8_t rx_data)
{
  if (p_line->count >= (CLI_LINE_BUF_MAX - 1)) return;   /* ★ 오버플로우 방어 */

  if (p_line->cursor == p_line->count)       /* 맨 뒤에 덧붙이는 경우 */
  {
    p_line->buf[p_line->cursor] = rx_data;
    p_line->count++; p_line->cursor++;
    p_line->buf[p_line->count] = 0;
    cliPrintf("%c", rx_data);                /* 한 글자만 출력하면 끝 (빠름) */
  }
  else                                       /* 중간에 끼워 넣는 경우 */
  {
    memmove(&p_line->buf[p_line->cursor + 1],
            &p_line->buf[p_line->cursor],
            p_line->count - p_line->cursor);
    p_line->buf[p_line->cursor] = rx_data;
    p_line->count++; p_line->cursor++;
    p_line->buf[p_line->count] = 0;
    cliLineRedraw();                         /* 줄 전체를 다시 그려야 함 */
  }
}
```

**배울 점 3가지**

1. **`memcpy` 가 아니라 `memmove`** — 원본과 대상이 겹치기 때문입니다.
   `memcpy` 는 겹칠 때 결과를 보장하지 않습니다(컴파일러/최적화에 따라 깨짐).
2. **빠른 길 / 느린 길 분리** — 맨 뒤 추가는 1바이트 출력, 중간 삽입만 전체 재그리기.
   대부분의 입력은 맨 뒤 추가이므로 이게 훨씬 빠릅니다.
3. **항상 `count` 자리에 `0`(NULL)을 넣습니다** — C 문자열의 끝 표시.
   `CLI_LINE_BUF_MAX - 1` 까지만 채우는 이유도 이 NULL 자리를 남기기 위함입니다.

**VT100 제어 문자열 정리** (MCU → 터미널)

| 문자열 | 뜻 | 쓰이는 곳 |
|---|---|---|
| `\x1B[2K` | 줄 전체 지우기 | `cliLineRedraw()` |
| `\r` | 커서를 줄 맨 앞으로 | `cliLineRedraw()` |
| `\x1B[nD` | 커서 n칸 왼쪽 | 재그리기 후 위치 복원, Home |
| `\x1B[nC` | 커서 n칸 오른쪽 | End |
| `\b \b` | 뒤로→공백→뒤로 | 맨 뒤 글자 삭제 (Backspace) |

---

### 📖 STEP 4. 히스토리 — 원형 버퍼 (cli.c 595~684줄)

**저장 칸은 4개인데 5번째 명령어가 들어오면?**

```
  hist[0]  hist[1]  hist[2]  hist[3]
  "help"   "info"   "led"    "md"       hist_head = 0 (여기에 덮어쓸 차례)
    ▲
    └── 가장 오래된 것 위에 새 명령어를 덮어쓴다

  hist_head = (hist_head + 1) % CLI_LINE_HIS_MAX;   ← 이 한 줄이 전부
```

```c
static void cliHistoryAdd(void)
{
  cli_node.line.buf[cli_node.line.count] = 0;
  cli_node.hist[cli_node.hist_head] = cli_node.line;   /* 구조체 통째로 복사 */
  cli_node.hist_head = (cli_node.hist_head + 1) % CLI_LINE_HIS_MAX;

  if (cli_node.hist_count < CLI_LINE_HIS_MAX) cli_node.hist_count++;
  cli_node.hist_pos = 0;
}
```

**↑ 키를 눌렀을 때 몇 번 칸을 꺼내야 하나?** (이 계산이 핵심)

```c
index = (hist_head + CLI_LINE_HIS_MAX - new_pos) % CLI_LINE_HIS_MAX;
        └───┬────┘  └────────┬───────┘
        다음 저장 위치      음수 방지용으로 미리 더해둔다
```

- `hist_pos = 1` → 가장 최근 명령어 = `hist_head` **바로 앞칸**
- `hist_pos = 2` → 그 이전 ...

`hist_head` 가 0 일 때 `0 - 1 = -1` 이 되면 배열 범위를 벗어납니다.
그래서 **MAX 를 미리 더한 뒤 나머지 연산**을 합니다. 원형 버퍼의 단골 관용구입니다.

**놓치기 쉬운 배려 : `line_backup`**

```c
if (cli_node.hist_pos == 0)            /* 히스토리 탐색을 "시작"하는 순간 */
{
  cli_node.line_backup = cli_node.line;   /* 치던 내용을 따로 보관 */
}
```

`led tog` 까지 치다가 ↑ 를 눌러 옛날 명령을 구경한 뒤 ↓ 로 돌아오면
치던 `led tog` 가 그대로 복원됩니다. bash 도 똑같이 동작합니다.

**배울 점** : 원형 버퍼는 **링버퍼(UART 수신)와 완전히 같은 원리**입니다.
`%` 연산 하나로 고정 크기 배열을 무한히 재사용하는 것 — 임베디드 필수 기법입니다.

---

### 📖 STEP 5. 명령어 실행 엔진 — 함수 포인터 테이블 (cli.c 687~795줄) ★★

이 프로젝트에서 **가장 아름다운 부분**입니다.

**① 등록 (`cliAdd`)**

```c
typedef struct {
  char      cmd_str[CLI_CMD_NAME_MAX];  /* 이름 (대문자로 저장) */
  cliFunc_t cmd_func;                   /* 실행할 함수 주소     */
} cli_cmd_t;

cli_cmd_t cmd_list[CLI_CMD_LIST_MAX];   /* 이 배열이 "명령어 사전" */
```

`cliAdd("led", cliLed)` 를 호출하면 이름을 `"LED"` 로 대문자 변환해 저장합니다.
→ 사용자가 `led`, `LED`, `LeD` 어느 쪽으로 쳐도 찾힙니다.

**② 파싱 + 디스패치 (`cliRunCmd`)**

```
 "led toggle 0 100"
        │
        │ strtok(buf, " \t")  ← 공백/탭에 '\0' 을 박아 넣어 자른다
        ▼
 argv[0]="led"  argv[1]="toggle"  argv[2]="0"  argv[3]="100"   argc=4
        │
        │ argv[0] 을 대문자로 → "LED"
        ▼
 for (i = 0; i < cmd_count; i++)                    ← 선형 탐색
   if (strcmp(cmd_list[i].cmd_str, argv[0]) == 0)
        │
        ▼
 cmd_args.argc = argc - 1;      ← ★ 명령어 이름을 뺀다
 cmd_args.argv = &argv[1];      ← ★ 포인터를 한 칸 밀어서 넘긴다
 cmd_list[i].cmd_func(&cmd_args);
        │
        ▼
 콜백 안에서는 args->argv[0] 이 "toggle"
```

`argv = &argv[1]` 이 한 줄로 "명령어 이름 빼고 넘기기"가 끝납니다.
배열 이름이 곧 포인터라는 C 의 성질을 활용한 것입니다.

**③ 콜백 쪽 편의 함수 (`cli_args_t`)**

```c
typedef struct {
  uint8_t   argc;
  char    **argv;
  int32_t (*getData) (uint8_t index);                     /* 함수 포인터 */
  float   (*getFloat)(uint8_t index);
  char   *(*getStr)  (uint8_t index);
  bool    (*isStr)   (uint8_t index, const char *p_str);
} cli_args_t;
```

구조체 안에 **함수 포인터를 넣어두면** 콜백에서 이렇게 짧게 쓸 수 있습니다.

```c
if (args->argc == 3 && args->isStr(0, "toggle") == true)
{
  ch        = args->getData(1);   /* "0"   -> 0    */
  period_ms = args->getData(2);   /* "100" -> 100  */
}
```

`getData()` 내부는 `strtol(str, NULL, 0)` 입니다. 마지막 인자가 `0` 이라
`"0x20"` 을 주면 **16진수로 자동 인식**합니다 (`md 0x08000000` 이 그래서 됩니다).

**배울 점 — 이게 "확장 가능한 설계"입니다**

```
       ┌──────────┐                ┌──────────┐
       │  led.c   │ cliAdd("led")  │  motor.c │ cliAdd("motor")
       └────┬─────┘                └────┬─────┘
            └───────────┬───────────────┘
                        ▼
                 ┌─────────────┐
                 │   cli.c     │  ← 새 명령이 늘어도 여기는 안 고침
                 │  cmd_list[] │
                 └─────────────┘
```

명령어를 추가할 때 **`cli.c` 를 절대 건드리지 않습니다.**
`motor.c` 파일 하나를 넣으면 `motor` 명령이 생기고, 빼면 사라집니다.
이것이 **개방-폐쇄 원칙(확장에는 열려있고 수정에는 닫혀있다)** 의 C 언어 버전입니다.

---

### 📖 STEP 6. `cliKeepLoop()` — 탈출구 있는 무한루프 (cli.c 797줄)

```c
bool cliKeepLoop(void)
{
  if (uartAvailable(cli_node.ch) == 0) return true;   /* 키 안 눌렸으면 계속 */

  uartRead(cli_node.ch);   /* ★ 탈출시킨 키 1바이트는 버린다 */
  return false;
}
```

`led.c` 에서 이렇게 씁니다.

```c
pre_time = millis();
while (cliKeepLoop() == true)          /* 아무 키나 누르면 탈출 */
{
  if (millis() - pre_time >= period_ms)   /* delay() 대신 millis() 비교 */
  {
    pre_time = millis();
    ledToggle(ch);
  }
}
ledOff(ch);
```

**배울 점 2가지**

1. `delay()` 를 쓰면 그 시간 동안 아무것도 못 하고, **키 입력도 못 받습니다.**
   `millis()` 로 경과 시간만 비교하면 루프를 계속 돌면서 다른 일도 할 수 있습니다.
2. **읽은 키를 버리는 이유** : 안 버리면 그 키가 수신 버퍼에 남아 있다가
   다음 프롬프트에 튀어나옵니다.

> `millis() - pre_time >= period_ms` 는 **오버플로우에도 안전한 관용구**입니다.
> `millis()` 가 `uint32_t` 라 약 49.7일마다 0 으로 돌아가지만,
> 부호 없는 정수의 뺄셈은 돌아간 뒤에도 정확한 경과시간을 줍니다.
> (`pre_time + period_ms <= millis()` 로 쓰면 49.7일째에 버그가 납니다)

---

### 📖 STEP 7. UART 링버퍼 — 인터럽트와 메인의 만남 (uart_stm32f411.c)

```
   [인터럽트 문맥]                      [메인 루프 문맥]
   HAL_UART_RxCpltCallback()            uartRead()
        │ 쓰기만                             │ 읽기만
        ▼                                    ▼
   ┌────┬────┬────┬────┬────┬────┬────┬────┐
   │ h  │ e  │ l  │ p  │    │    │    │    │   rx_buf[128]
   └────┴────┴────┴────┴────┴────┴────┴────┘
     ▲                   ▲
    tail                head
    (메인이 읽을 위치)   (인터럽트가 쓸 위치)

   남은 개수 = (head - tail + MAX) % MAX     ← 한 바퀴 돌아도 정확
   head == tail  →  읽을 데이터 없음
```

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  next_head = (p_uart->rx_head + 1) % UART_RX_BUF_MAX;

  if (next_head != p_uart->rx_tail)          /* 가득 차지 않았으면 */
  {
    p_uart->rx_buf[p_uart->rx_head] = p_uart->rx_temp;
    p_uart->rx_head = next_head;
  }
  /* ★★ 다음 1바이트 수신을 다시 걸어준다 — 이걸 빼먹으면 한 글자만 받고 멈춤 */
  HAL_UART_Receive_IT(&p_uart->handle, &p_uart->rx_temp, 1);
}
```

**배울 점 3가지**

1. **`HAL_UART_Receive_IT()` 는 1바이트 받으면 자동으로 해제됩니다.**
   콜백 안에서 **매번 다시 호출**해야 계속 받습니다. 초보자가 가장 많이 겪는 함정입니다.
2. **한 칸을 항상 비워둡니다** (`next_head != rx_tail`).
   꽉 채우면 "가득 참"과 "텅 빔"이 둘 다 `head == tail` 이 되어 구분이 안 됩니다.
3. **한쪽은 head 만, 다른 쪽은 tail 만 건드립니다.**
   그래서 인터럽트를 막지 않고도(`__disable_irq()` 없이) 안전합니다.
   이걸 **SPSC(Single Producer Single Consumer) 링버퍼**라고 부릅니다.

`HAL_UART_ErrorCallback()` 에서도 수신을 다시 거는데,
오버런 에러가 나면 수신이 멈춰버리기 때문에 **살려내는 장치**입니다.

---

## 4. 설계 결정 정리 — "왜 이렇게 했나"

| 결정 | 이유 | 대안과 비교 |
|---|---|---|
| 수신은 인터럽트, 송신은 블로킹 | 수신을 놓치면 복구 불가, 송신은 짧고 급하지 않음 | 송신도 인터럽트로 하면 빠르지만 복잡도 상승 |
| 전역 구조체 `cli_node` 1개 | MCU 에 CLI 는 하나면 충분, 동적할당 회피 | 인스턴스 여러 개면 핸들 방식으로 바꿔야 함 |
| `malloc` 을 쓰지 않음 | 임베디드는 힙 단편화 = 장기 실행 시 죽음 | 모든 배열이 컴파일 타임 고정 크기 |
| 명령어 탐색이 선형(`for`) | 16개뿐이라 해시테이블은 과설계 | 명령어가 100개 넘으면 그때 고민 |
| `_USE_HW_XXX` 매크로 스위치 | 안 쓰는 기능은 **컴파일에서 통째로 제외** → Flash 절약 | `if` 로 런타임 분기하면 코드는 그대로 남음 |
| 설정을 `hw_def.h` 한 곳에 | 버퍼 크기를 찾아 헤맬 필요 없음 | 각 파일에 흩어두면 RAM 계산이 불가능 |

---

## 5. 직접 확인해 보는 실습

```bash
cd simulator
make test     # 33개 자동 테스트 (전부 통과해야 정상)
make run      # 직접 조작
```

| # | 해볼 것 | 확인 포인트 |
|---|---|---|
| 1 | `make run` 후 ← 키를 누르고 stderr 로그 보기 | 3바이트(`0x1B 0x5B 0x44`)가 들어오는 것 확인 |
| 2 | `hep` 치고 ← 로 커서 옮겨 `l` 삽입 | `cliLineInsert()` 의 `memmove` 경로 |
| 3 | 5개 명령을 친 뒤 ↑ 를 5번 | 4개까지만 나옴 → 원형 버퍼 한계 |
| 4 | `led tog` 까지 치고 ↑ ↓ | 치던 내용이 복원됨 → `line_backup` |
| 5 | 40글자짜리 문자열 입력 | 32자에서 안 늘어남 → 오버플로우 방어 |
| 6 | `hw_def.h` 의 `HW_CLI_LINE_HIS_MAX` 를 2로 바꾸고 재빌드 | 설정 한 곳만 고치면 되는 구조 체감 |
| 7 | `hw_def.h` 의 `#define _USE_HW_LED` 를 주석 처리 | `help` 에서 LED 가 사라짐 (컴파일 제외) |

---

## 6. 코드를 고칠 때 조심할 점 (실제 함정)

1. **`strtok()` 은 원본을 파괴합니다.**
   `cliRunCmd()` 실행 후 `line.buf` 는 `"led\0toggle\0..."` 상태입니다.
   그래서 `cliHistoryAdd()` 를 **반드시 먼저** 호출합니다.

2. **`argv[]` 는 `line.buf` 안을 가리키는 포인터입니다.**
   콜백이 끝나고 `cliLineClear()` 가 불리면 내용이 지워집니다.
   → 인자 값을 나중에 쓰려면 **콜백 안에서 복사**해 두세요.

3. **인자가 8개(`HW_CLI_CMD_ARGS_MAX`)를 넘으면 조용히 잘립니다.**
   에러 메시지가 없으니, 인자를 많이 받는 명령을 만들면 이 값을 늘리세요.

4. **`cliPrintf()` 는 스택에 256바이트 버퍼를 잡습니다.**
   콜백 안에서 재귀적으로 부르거나 스택이 작으면 위험합니다.
   256자를 넘는 출력은 잘립니다.

5. **`uartWrite()` 는 블로킹(최대 100ms)입니다.**
   `md 0x20000000 1024` 처럼 긴 덤프 중에는 메인 루프가 멈춥니다.
   실시간 제어와 함께 쓸 거라면 송신도 인터럽트/DMA 로 바꿔야 합니다.

6. **`md` 는 주소를 그대로 읽습니다.**
   STM32F411 에 없는 주소를 읽으면 **HardFault** 로 보드가 멈춥니다.
   Flash `0x0800_0000` / SRAM `0x2000_0000` / 주변장치 `0x4000_0000` 범위만 쓰세요.

7. **`USART1_IRQHandler` 중복 정의**
   CubeMX 가 만든 `stm32f4xx_it.c` 에도 같은 이름이 있으면 링크 에러가 납니다.
   둘 중 하나를 지우세요.

---

## 7. 다음 단계로 확장한다면

| 하고 싶은 것 | 손댈 곳 |
|---|---|
| 새 명령어 추가 | 새 `.c` 파일에서 `cliAdd()` 호출 — `cli.c` 는 안 건드림 |
| USB VCP 로 바꾸기 | `uart.h` 의 함수 6개를 USB CDC 로 구현 |
| Tab 자동완성 | `cliUpdate()` 의 IDLE 상태에 `0x09` 처리 추가 + `cmd_list` 접두사 검색 |
| 명령어 100개 이상 | `cliRunCmd()` 의 선형탐색을 이진탐색/해시로 교체 |
| 송신 지연 제거 | `uartWrite()` 를 송신 링버퍼 + 인터럽트 방식으로 변경 |
| 다른 MCU 로 이식 | `bsp/` 폴더의 2개 파일만 새로 작성 |

---

## 부록 : 함수 위치 빠른 찾기 (`src/common/hw/driver/cli.c`)

| 함수 | 줄 | 역할 |
|---|---:|---|
| `cliInit()` | 144 | 초기화 + 기본 명령어 3개 등록 |
| `cliOpen()` | 169 | CLI 채널 열기, 환영 메시지 |
| `cliMain()` | 213 | ★ 메인 루프에서 호출, 1바이트 처리 |
| `cliUpdate()` | 252 | ★★ 수신 상태머신 |
| `cliLineRedraw()` | 427 | 줄 지우고 다시 그리기 (VT100) |
| `cliLineInsert()` | 447 | ★ 커서 위치에 문자 삽입 (memmove) |
| `cliLineDelBack()` | 485 | Backspace |
| `cliLineDelFront()` | 518 | Delete |
| `cliHistoryAdd()` | 605 | ★ 원형 버퍼에 저장 |
| `cliLineChange()` | 625 | ★ ↑↓ 히스토리 탐색 |
| `cliAdd()` | 687 | ★ 명령어 등록 |
| `cliRunCmd()` | 733 | ★★ 파싱 + 콜백 호출 |
| `cliKeepLoop()` | 797 | ★ 루프 탈출 조건 |
| `cliPrintf()` | 810 | CLI 채널 printf |
| `cliCmdHelp/MemoryDump/Info()` | 909~ | 내장 명령어 3종 |
