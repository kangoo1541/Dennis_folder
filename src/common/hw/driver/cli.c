/*
 * cli.c
 *
 *  CLI (Command Line Interface) 구현
 *
 *  [전체 동작 흐름]
 *    터미널에서 키를 누른다
 *        -> UART 수신 버퍼에 1바이트 들어옴
 *        -> cliMain() 이 1바이트를 꺼내서 cliUpdate() 에 넘김
 *        -> cliUpdate() 가 "일반 문자 / 특수키" 를 구분해 라인 버퍼를 편집
 *        -> Enter 를 누르면 cliRunCmd() 가 문자열을 잘라서 등록된 함수를 호출
 *
 *  [왜 상태머신이 필요한가?]
 *    방향키는 1바이트가 아니라 3~4바이트로 들어옵니다.
 *      왼쪽 방향키 = 0x1B('ESC') + 0x5B('[') + 0x44('D')
 *    즉 ESC 를 받은 순간에는 아직 무슨 키인지 모릅니다.
 *    그래서 "ESC 를 받은 상태", "ESC [ 까지 받은 상태" 처럼
 *    지금까지 받은 단계를 기억해 두는 변수(state)가 필요합니다.
 *    이것을 상태머신(state machine) 이라고 부릅니다.
 */

#include "cli.h"

#ifdef _USE_HW_CLI

#include <stdarg.h>
#include <ctype.h>
#include "uart.h"


/* ---------------------------------------------------------------- */
/*  키 코드 정의 (ASCII / VT100 이스케이프 시퀀스)                    */
/* ---------------------------------------------------------------- */
#define CLI_KEY_BACKSPACE     0x08   /* Backspace 키          */
#define CLI_KEY_ENTER         0x0D   /* Enter 키 (CR)         */
#define CLI_KEY_LF            0x0A   /* 줄바꿈 (LF)           */
#define CLI_KEY_ESC           0x1B   /* ESC (특수키의 시작)   */
#define CLI_KEY_DEL           0x7F   /* Del (터미널에 따라 Backspace 로 들어옴) */
#define CLI_KEY_SPACE         0x20   /* 출력 가능한 문자의 시작 */
#define CLI_KEY_TILDE         0x7E   /* 출력 가능한 문자의 끝   */

/* MCU 가 터미널로 보내는 VT100 제어 문자열 (화면 커서를 움직이는 명령) */
#define CLI_VT100_ERASE_LINE  "\x1B[2K"   /* 현재 줄 전체 지우기 */
#define CLI_VT100_CURSOR_LEFT "\x1B[D"    /* 커서 한 칸 왼쪽     */
#define CLI_VT100_CURSOR_RIGHT "\x1B[C"   /* 커서 한 칸 오른쪽   */

#define CLI_PROMPT_STR        "cli# "


/* 수신 상태머신의 상태값 */
typedef enum
{
  CLI_RX_IDLE = 0,   /* 평소 상태 : 일반 문자를 받는 중        */
  CLI_RX_SP1,        /* 0x1B(ESC) 를 받음                      */
  CLI_RX_SP2,        /* 0x1B 0x5B('[') 까지 받음               */
  CLI_RX_SP3,        /* 0x1B 0x5B 숫자 까지 받음 ('~' 대기)    */
} cli_state_t;


/* 한 줄의 입력 상태를 담는 구조체 */
typedef struct
{
  uint8_t  buf[CLI_LINE_BUF_MAX];  /* 입력된 문자열 (맨 뒤는 항상 NULL) */
  uint16_t count;                  /* 현재 글자 수                      */
  uint16_t cursor;                 /* 커서 위치 (0 ~ count)             */
} cli_line_t;


/* 등록된 명령어 1개의 정보 */
typedef struct
{
  char      cmd_str[CLI_CMD_NAME_MAX];  /* 명령어 이름 (대문자로 저장) */
  cliFunc_t cmd_func;                   /* 실행할 함수                 */
} cli_cmd_t;


/* CLI 모듈 전체가 사용하는 변수 묶음 */
typedef struct
{
  bool     is_open;       /* CLI 채널이 열렸는가        */
  bool     is_log_open;   /* 디버그 로그 채널이 열렸는가 */
  uint8_t  ch;            /* CLI 채널 번호              */
  uint8_t  log_ch;        /* 로그 채널 번호             */

  uint8_t  state;         /* 수신 상태머신 상태         */
  uint8_t  sp_data;       /* ESC [ 다음에 온 숫자 보관  */
  bool     last_is_cr;    /* 직전 문자가 CR 이었는가    */

  cli_line_t line;        /* 지금 편집 중인 줄          */
  cli_line_t line_backup; /* 히스토리 탐색 전 원래 줄   */

  /* 명령어 히스토리 : 원형(circular) 버퍼 */
  cli_line_t hist[CLI_LINE_HIS_MAX];
  uint16_t   hist_head;   /* 다음에 저장할 위치                     */
  uint16_t   hist_count;  /* 저장된 개수 (최대 CLI_LINE_HIS_MAX)    */
  uint16_t   hist_pos;    /* 0=편집중, 1=가장 최근, 2=그 이전 ...   */

  /* 파싱 결과 */
  uint8_t    argc;
  char      *argv[CLI_CMD_ARGS_MAX];

  cli_args_t cmd_args;
  cli_cmd_t  cmd_list[CLI_CMD_LIST_MAX];
  uint16_t   cmd_count;
} cli_t;


static cli_t cli_node;


/* 내부에서만 쓰는 함수들 (static = 이 파일 밖에서는 안 보임) */
static bool    cliUpdate(uint8_t rx_data);
static void    cliLineClear(void);
static void    cliLineRedraw(void);
static void    cliLineInsert(uint8_t rx_data);
static void    cliLineDelBack(void);
static void    cliLineDelFront(void);
static void    cliCursorMove(int16_t step);
static void    cliCursorHome(void);
static void    cliCursorEnd(void);
static void    cliHistoryAdd(void);
static void    cliLineChange(int8_t dir);
static void    cliShowPrompt(void);
static bool    cliRunCmd(void);
static void    cliShowLog(uint8_t rx_data);

/* cli_args_t 안에 들어가는 도우미 함수들 */
static int32_t cliArgsGetData(uint8_t index);
static float   cliArgsGetFloat(uint8_t index);
static char   *cliArgsGetStr(uint8_t index);
static bool    cliArgsIsStr(uint8_t index, const char *p_str);

/* 기본 내장 명령어 */
static void    cliCmdHelp(cli_args_t *args);
static void    cliCmdMemoryDump(cli_args_t *args);
static void    cliCmdInfo(cli_args_t *args);



/* ================================================================ */
/*  초기화 / 채널 열기                                               */
/* ================================================================ */

bool cliInit(void)
{
  memset(&cli_node, 0, sizeof(cli_t));

  cli_node.is_open     = false;
  cli_node.is_log_open = false;
  cli_node.state       = CLI_RX_IDLE;
  cli_node.cmd_count   = 0;

  cliLineClear();

  /* 콜백 함수에서 쓸 도우미 함수들을 미리 연결해 둔다 */
  cli_node.cmd_args.getData  = cliArgsGetData;
  cli_node.cmd_args.getFloat = cliArgsGetFloat;
  cli_node.cmd_args.getStr   = cliArgsGetStr;
  cli_node.cmd_args.isStr    = cliArgsIsStr;

  /* 어디서나 쓸 수 있는 기본 명령어 등록 */
  cliAdd("help", cliCmdHelp);
  cliAdd("md",   cliCmdMemoryDump);
  cliAdd("info", cliCmdInfo);

  return true;
}

bool cliOpen(uint8_t ch, uint32_t baud)
{
  cli_node.ch      = ch;
  cli_node.is_open = uartOpen(ch, baud);

  if (cli_node.is_open == true)
  {
    cliPrintf("\r\n[ %s CLI ]\r\n", _DEF_BOARD_NAME);
    cliPrintf("Type 'help' to see the command list.\r\n");
    cliShowPrompt();
  }

  return cli_node.is_open;
}

/*
 * 키를 누를 때 실제로 어떤 코드가 들어오는지 확인하기 위한 별도 채널.
 * CLI 채널(USB VCP)에 로그를 같이 찍으면 화면이 엉망이 되므로
 * 물리 UART 같은 다른 채널로 따로 뽑아서 봅니다.
 */
bool cliOpenLog(uint8_t ch, uint32_t baud)
{
  cli_node.log_ch      = ch;
  cli_node.is_log_open = uartOpen(ch, baud);

  if (cli_node.is_log_open == true)
  {
    uartPrintf(cli_node.log_ch, "\r\n[ CLI Key Log ]\r\n");
  }

  return cli_node.is_log_open;
}

bool cliIsOpen(void)
{
  return cli_node.is_open;
}



/* ================================================================ */
/*  메인 루프에서 호출되는 함수                                      */
/* ================================================================ */

bool cliMain(void)
{
  bool ret = false;
  uint8_t rx_data;

  if (cli_node.is_open != true)
  {
    return false;
  }

  /* 수신 버퍼에 데이터가 있을 때만 처리한다.
   * 없으면 바로 빠져나오므로 다른 일을 하는 데 방해가 되지 않는다. (논블로킹) */
  if (uartAvailable(cli_node.ch) > 0)
  {
    rx_data = uartRead(cli_node.ch);

    cliShowLog(rx_data);            /* 디버그 채널로 키코드 출력 */

    if (cliUpdate(rx_data) == true) /* Enter 를 눌러 한 줄이 완성됨 */
    {
      if (cli_node.line.count > 0)
      {
        cliHistoryAdd();            /* 실행 전에 히스토리에 먼저 저장 */
        cliRunCmd();                /* strtok 이 버퍼를 잘라 쓰므로 순서 중요 */
        ret = true;
      }
      cliLineClear();
      cliShowPrompt();
    }
  }

  return ret;
}


/*
 * 수신된 1바이트를 처리하는 상태머신
 *  반환값 : Enter 를 눌러 한 줄 입력이 끝났으면 true
 */
static bool cliUpdate(uint8_t rx_data)
{
  bool ret = false;

  switch (cli_node.state)
  {
    /* ---------------- 평소 상태 : 일반 문자 처리 ---------------- */
    case CLI_RX_IDLE:
      if (rx_data == CLI_KEY_ESC)
      {
        /* 특수키가 시작되었다. 다음 바이트를 봐야 무슨 키인지 알 수 있다. */
        cli_node.state = CLI_RX_SP1;
        break;
      }

      if (rx_data == CLI_KEY_ENTER)
      {
        cli_node.last_is_cr = true;
        cli_node.line.buf[cli_node.line.count] = 0;  /* 문자열 끝 표시 */
        cliPrintf("\r\n");
        ret = true;
        break;
      }

      if (rx_data == CLI_KEY_LF)
      {
        /* CR + LF 을 함께 보내는 터미널 대응 :
         * 방금 CR 을 처리했다면 뒤따라온 LF 는 무시한다. */
        if (cli_node.last_is_cr == true)
        {
          cli_node.last_is_cr = false;
          break;
        }
        cli_node.line.buf[cli_node.line.count] = 0;
        cliPrintf("\r\n");
        ret = true;
        break;
      }

      cli_node.last_is_cr = false;

      if (rx_data == CLI_KEY_BACKSPACE || rx_data == CLI_KEY_DEL)
      {
        /* 터미널마다 Backspace 를 0x08 로 보내기도 하고 0x7F 로 보내기도 한다.
         * 둘 다 "커서 왼쪽 글자 지우기" 로 처리한다.
         * (커서 오른쪽 글자를 지우는 Delete 키는 ESC [ 3 ~ 로 따로 들어온다) */
        cliLineDelBack();
        break;
      }

      /* 눈에 보이는 문자(공백 ~ '~')만 버퍼에 넣는다 */
      if (rx_data >= CLI_KEY_SPACE && rx_data <= CLI_KEY_TILDE)
      {
        cliLineInsert(rx_data);
      }
      break;

    /* ---------------- ESC 를 받은 상태 ---------------- */
    case CLI_RX_SP1:
      if (rx_data == '[')
      {
        cli_node.state = CLI_RX_SP2;
      }
      else
      {
        cli_node.state = CLI_RX_IDLE;   /* 모르는 조합이면 무시 */
      }
      break;

    /* ---------------- ESC [ 까지 받은 상태 ---------------- */
    case CLI_RX_SP2:
      switch (rx_data)
      {
        case 'A':                      /* 위쪽 방향키  : 이전 명령어 */
          cliLineChange(-1);
          cli_node.state = CLI_RX_IDLE;
          break;

        case 'B':                      /* 아래쪽 방향키: 다음 명령어 */
          cliLineChange(+1);
          cli_node.state = CLI_RX_IDLE;
          break;

        case 'C':                      /* 오른쪽 방향키 */
          cliCursorMove(+1);
          cli_node.state = CLI_RX_IDLE;
          break;

        case 'D':                      /* 왼쪽 방향키   */
          cliCursorMove(-1);
          cli_node.state = CLI_RX_IDLE;
          break;

        case 'H':                      /* Home (ESC [ H 형식) */
          cliCursorHome();
          cli_node.state = CLI_RX_IDLE;
          break;

        case 'F':                      /* End  (ESC [ F 형식) */
          cliCursorEnd();
          cli_node.state = CLI_RX_IDLE;
          break;

        default:
          if (rx_data >= '1' && rx_data <= '8')
          {
            /* Home/End/Delete 는 ESC [ 숫자 ~ 형태의 4바이트로 들어온다.
             * 숫자를 기억해 두고 '~' 가 오기를 기다린다. */
            cli_node.sp_data = rx_data;
            cli_node.state   = CLI_RX_SP3;
          }
          else
          {
            cli_node.state = CLI_RX_IDLE;
          }
          break;
      }
      break;

    /* ---------------- ESC [ 숫자 까지 받은 상태 ---------------- */
    case CLI_RX_SP3:
      if (rx_data == '~')
      {
        switch (cli_node.sp_data)
        {
          case '1':                    /* Home */
          case '7':
            cliCursorHome();
            break;

          case '3':                    /* Delete : 커서 오른쪽 글자 삭제 */
            cliLineDelFront();
            break;

          case '4':                    /* End  */
          case '8':
            cliCursorEnd();
            break;

          default:
            break;
        }
      }
      cli_node.state = CLI_RX_IDLE;
      break;

    default:
      cli_node.state = CLI_RX_IDLE;
      break;
  }

  return ret;
}


/* ================================================================ */
/*  라인 버퍼 편집                                                   */
/* ================================================================ */

/* 입력 줄을 비운다 */
static void cliLineClear(void)
{
  memset(cli_node.line.buf, 0, CLI_LINE_BUF_MAX);
  cli_node.line.count  = 0;
  cli_node.line.cursor = 0;
  cli_node.hist_pos    = 0;   /* 히스토리 탐색 위치도 처음으로 */
}

/*
 * 화면의 현재 줄을 지우고 버퍼 내용을 다시 그린다.
 *  - "\x1B[2K" : 커서가 있는 줄 전체를 지워라
 *  - "\r"      : 커서를 줄 맨 앞으로 보내라
 *  - "\x1B[nD" : 커서를 왼쪽으로 n칸 옮겨라
 * 화면에 다시 그린 뒤, 커서를 buf 안의 cursor 위치와 똑같이 맞춰준다.
 */
static void cliLineRedraw(void)
{
  cliPrintf(CLI_VT100_ERASE_LINE "\r");
  cliPrintf("%s%s", CLI_PROMPT_STR, cli_node.line.buf);

  if (cli_node.line.count > cli_node.line.cursor)
  {
    cliPrintf("\x1B[%dD", cli_node.line.count - cli_node.line.cursor);
  }
}

/*
 * 커서 위치에 문자 1개 끼워 넣기
 *
 *  [끝에 추가하는 경우]  abc| + 'd'  -> abcd|   : 그냥 한 글자 출력하면 끝
 *  [중간에 넣는 경우]    a|bc + 'x'  -> ax|bc   : 뒤의 글자들을 한 칸씩 밀어야 함
 *
 *  memmove() 는 메모리 블록을 통째로 옮겨주는 표준 함수입니다.
 *  (memcpy 와 달리 영역이 겹쳐도 안전합니다. 여기서는 겹치므로 memmove 를 써야 합니다)
 */
static void cliLineInsert(uint8_t rx_data)
{
  cli_line_t *p_line = &cli_node.line;

  /* 버퍼가 가득 찼는가? (맨 뒤 NULL 자리 1칸은 항상 비워둔다) */
  if (p_line->count >= (CLI_LINE_BUF_MAX - 1))
  {
    return;
  }

  if (p_line->cursor == p_line->count)
  {
    /* 맨 뒤에 덧붙이기 */
    p_line->buf[p_line->cursor] = rx_data;
    p_line->count++;
    p_line->cursor++;
    p_line->buf[p_line->count] = 0;

    /* 화면에도 방금 누른 글자를 그대로 보여준다 (에코) */
    cliPrintf("%c", rx_data);
  }
  else
  {
    /* 커서 뒤쪽 글자들을 오른쪽으로 한 칸씩 밀고 그 자리에 끼워 넣기 */
    memmove(&p_line->buf[p_line->cursor + 1],
            &p_line->buf[p_line->cursor],
            p_line->count - p_line->cursor);

    p_line->buf[p_line->cursor] = rx_data;
    p_line->count++;
    p_line->cursor++;
    p_line->buf[p_line->count] = 0;

    cliLineRedraw();
  }
}

/* Backspace : 커서 "왼쪽" 글자 1개 삭제 */
static void cliLineDelBack(void)
{
  cli_line_t *p_line = &cli_node.line;

  if (p_line->cursor == 0)
  {
    return;   /* 맨 앞이면 지울 글자가 없다 */
  }

  if (p_line->cursor == p_line->count)
  {
    /* 맨 뒤 글자 삭제 : 커서를 한 칸 뒤로 -> 공백 출력 -> 다시 한 칸 뒤로 */
    p_line->count--;
    p_line->cursor--;
    p_line->buf[p_line->count] = 0;
    cliPrintf("\b \b");
  }
  else
  {
    /* 중간 글자 삭제 : 커서 뒤쪽 글자들을 왼쪽으로 한 칸씩 당긴다 */
    memmove(&p_line->buf[p_line->cursor - 1],
            &p_line->buf[p_line->cursor],
            p_line->count - p_line->cursor);

    p_line->count--;
    p_line->cursor--;
    p_line->buf[p_line->count] = 0;

    cliLineRedraw();
  }
}

/* Delete : 커서 "오른쪽" 글자 1개 삭제 (ESC [ 3 ~) */
static void cliLineDelFront(void)
{
  cli_line_t *p_line = &cli_node.line;

  if (p_line->cursor >= p_line->count)
  {
    return;   /* 커서가 맨 뒤면 지울 글자가 없다 */
  }

  memmove(&p_line->buf[p_line->cursor],
          &p_line->buf[p_line->cursor + 1],
          p_line->count - p_line->cursor - 1);

  p_line->count--;
  p_line->buf[p_line->count] = 0;

  cliLineRedraw();
}

/* 커서를 좌(-1) / 우(+1) 로 한 칸 이동 */
static void cliCursorMove(int16_t step)
{
  cli_line_t *p_line = &cli_node.line;

  if (step < 0)
  {
    if (p_line->cursor > 0)
    {
      p_line->cursor--;
      cliPrintf(CLI_VT100_CURSOR_LEFT);
    }
  }
  else
  {
    if (p_line->cursor < p_line->count)
    {
      p_line->cursor++;
      cliPrintf(CLI_VT100_CURSOR_RIGHT);
    }
  }
}

/* Home : 커서를 줄 맨 앞으로 */
static void cliCursorHome(void)
{
  if (cli_node.line.cursor > 0)
  {
    cliPrintf("\x1B[%dD", cli_node.line.cursor);
    cli_node.line.cursor = 0;
  }
}

/* End : 커서를 줄 맨 뒤로 */
static void cliCursorEnd(void)
{
  if (cli_node.line.cursor < cli_node.line.count)
  {
    cliPrintf("\x1B[%dC", cli_node.line.count - cli_node.line.cursor);
    cli_node.line.cursor = cli_node.line.count;
  }
}

/* 프롬프트 출력 */
static void cliShowPrompt(void)
{
  cliPrintf("%s", CLI_PROMPT_STR);
}



/* ================================================================ */
/*  명령어 히스토리 (원형 버퍼)                                      */
/* ================================================================ */
/*
 *  [원형 버퍼란?]
 *   칸이 4개인 배열에 5번째 명령어를 저장하려면 자리가 없습니다.
 *   이때 가장 오래된 0번 칸에 덮어쓰고, 다시 1, 2, 3, 0, 1 ... 순서로
 *   빙글빙글 돌면서 저장하는 방식을 원형 버퍼라고 합니다.
 *   나머지 연산(%) 하나면 구현됩니다.
 *
 *   hist_head  : 다음에 저장할 칸
 *   hist_count : 지금까지 저장된 개수 (최대 4)
 *   hist_pos   : 0   = 지금 타이핑 중인 줄
 *                1   = 가장 최근에 입력한 명령어
 *                2   = 그 이전 ...
 */

static void cliHistoryAdd(void)
{
  cli_node.line.buf[cli_node.line.count] = 0;

  cli_node.hist[cli_node.hist_head] = cli_node.line;
  cli_node.hist_head = (cli_node.hist_head + 1) % CLI_LINE_HIS_MAX;

  if (cli_node.hist_count < CLI_LINE_HIS_MAX)
  {
    cli_node.hist_count++;
  }

  cli_node.hist_pos = 0;
}

/*
 * 방향키 위/아래로 히스토리 이동
 *   dir = -1 : 위쪽 방향키 (더 과거의 명령어)
 *   dir = +1 : 아래쪽 방향키 (더 최근의 명령어)
 */
static void cliLineChange(int8_t dir)
{
  uint16_t new_pos;
  uint16_t index;

  if (cli_node.hist_count == 0)
  {
    return;   /* 저장된 명령어가 아직 없다 */
  }

  new_pos = cli_node.hist_pos;

  if (dir < 0)
  {
    if (new_pos >= cli_node.hist_count)
    {
      return;   /* 가장 오래된 것까지 왔다 */
    }
    new_pos++;
  }
  else
  {
    if (new_pos == 0)
    {
      return;   /* 이미 편집 중인 줄이다 */
    }
    new_pos--;
  }

  /* 히스토리 탐색을 처음 시작하는 순간이라면,
   * 지금 타이핑하던 내용을 잃어버리지 않도록 따로 보관해 둔다. */
  if (cli_node.hist_pos == 0)
  {
    cli_node.line_backup = cli_node.line;
  }

  cli_node.hist_pos = new_pos;

  if (new_pos == 0)
  {
    /* 아래쪽 방향키로 끝까지 내려왔다 -> 원래 타이핑하던 줄로 복귀 */
    cli_node.line = cli_node.line_backup;
  }
  else
  {
    /* hist_head 바로 앞칸이 가장 최근 명령어.
     * 음수가 되지 않도록 CLI_LINE_HIS_MAX 를 더한 뒤 나머지 연산을 한다. */
    index = (cli_node.hist_head + CLI_LINE_HIS_MAX - new_pos) % CLI_LINE_HIS_MAX;

    cli_node.line        = cli_node.hist[index];
    cli_node.line.cursor = cli_node.line.count;   /* 커서는 맨 뒤로 */
  }

  cliLineRedraw();
}



/* ================================================================ */
/*  명령어 등록 / 파싱 / 실행                                        */
/* ================================================================ */

bool cliAdd(const char *cmd_str, cliFunc_t func)
{
  uint16_t index;
  uint16_t i;

  if (cli_node.cmd_count >= CLI_CMD_LIST_MAX)
  {
    return false;   /* 슬롯이 가득 찼다 (hw_def.h 에서 개수를 늘리면 된다) */
  }
  if (cmd_str == NULL || func == NULL)
  {
    return false;
  }
  if (strlen(cmd_str) >= CLI_CMD_NAME_MAX)
  {
    return false;   /* 이름이 너무 길다 */
  }

  index = cli_node.cmd_count;

  strncpy(cli_node.cmd_list[index].cmd_str, cmd_str, CLI_CMD_NAME_MAX - 1);
  cli_node.cmd_list[index].cmd_str[CLI_CMD_NAME_MAX - 1] = 0;

  /* 사용자가 led 를 치든 LED 를 치든 똑같이 찾히도록 대문자로 통일해 저장 */
  for (i = 0; i < strlen(cli_node.cmd_list[index].cmd_str); i++)
  {
    cli_node.cmd_list[index].cmd_str[i] =
        (char)toupper((int)cli_node.cmd_list[index].cmd_str[i]);
  }

  cli_node.cmd_list[index].cmd_func = func;
  cli_node.cmd_count++;

  return true;
}

/*
 * 입력된 한 줄을 잘라서(파싱) 해당하는 명령어 함수를 호출한다.
 *
 *  "led toggle 1 100"
 *      -> strtok 으로 공백/탭 단위로 자르기
 *      -> argv[0]="LED", argv[1]="toggle", argv[2]="1", argv[3]="100"
 *      -> 등록 목록에서 "LED" 찾기
 *      -> 콜백에는 명령어 이름을 뺀 나머지만 전달
 *         (argc = 4-1 = 3, argv = &argv[1])
 */
static bool cliRunCmd(void)
{
  bool ret = false;
  uint16_t i;
  char *p_tok;

  /* 1) 공백(' ') 과 탭('\t') 을 기준으로 토큰 분리 */
  cli_node.argc = 0;

  p_tok = strtok((char *)cli_node.line.buf, " \t");
  while (p_tok != NULL && cli_node.argc < CLI_CMD_ARGS_MAX)
  {
    cli_node.argv[cli_node.argc] = p_tok;
    cli_node.argc++;
    p_tok = strtok(NULL, " \t");
  }

  if (cli_node.argc == 0)
  {
    return false;   /* 공백만 입력했다 */
  }

  /* 2) 명령어 이름(첫 토큰)을 대문자로 변환 -> 대소문자 구분 없이 사용 가능 */
  for (i = 0; i < strlen(cli_node.argv[0]); i++)
  {
    cli_node.argv[0][i] = (char)toupper((int)cli_node.argv[0][i]);
  }

  /* 3) 등록된 명령어 목록을 처음부터 끝까지 훑어서 찾는다 (선형 탐색) */
  for (i = 0; i < cli_node.cmd_count; i++)
  {
    if (strcmp(cli_node.cmd_list[i].cmd_str, cli_node.argv[0]) == 0)
    {
      /* 4) 명령어 이름은 빼고 뒤의 인자들만 콜백으로 넘긴다 */
      cli_node.cmd_args.argc = cli_node.argc - 1;
      cli_node.cmd_args.argv = &cli_node.argv[1];

      cli_node.cmd_list[i].cmd_func(&cli_node.cmd_args);

      ret = true;
      break;
    }
  }

  if (ret != true)
  {
    cliPrintf("[ %s ] : Unknown command. Type 'help'\r\n", cli_node.argv[0]);
  }

  return ret;
}

/*
 * 명령어 안에서 오래 도는 while 루프의 탈출 조건으로 사용한다.
 *
 *   while (cliKeepLoop())
 *   {
 *     ... LED 깜빡이기 ...
 *   }
 *
 * 터미널에서 아무 키나 누르면 수신 버퍼에 데이터가 생기고,
 * 이 함수가 false 를 반환하므로 루프를 빠져나와 프롬프트로 돌아간다.
 * (무한루프에 빠져 보드를 리셋해야 하는 상황을 막아준다)
 */
bool cliKeepLoop(void)
{
  if (uartAvailable(cli_node.ch) == 0)
  {
    return true;
  }

  /* 탈출시킨 키 1바이트는 버려서 다음 프롬프트에 찍히지 않게 한다 */
  uartRead(cli_node.ch);

  return false;
}

void cliPrintf(const char *fmt, ...)
{
  va_list arg;
  int len;
  char print_buf[256];

  if (cli_node.is_open != true)
  {
    return;
  }

  va_start(arg, fmt);
  len = vsnprintf(print_buf, sizeof(print_buf), fmt, arg);
  va_end(arg);

  if (len > 0)
  {
    if (len > (int)sizeof(print_buf) - 1)
    {
      len = (int)sizeof(print_buf) - 1;   /* 버퍼보다 길면 잘라서 보낸다 */
    }
    uartWrite(cli_node.ch, (uint8_t *)print_buf, (uint32_t)len);
  }
}

/* 디버그 로그 채널로 "지금 어떤 코드가 들어왔는지" 를 찍어준다 */
static void cliShowLog(uint8_t rx_data)
{
  if (cli_node.is_log_open != true)
  {
    return;
  }

  uartPrintf(cli_node.log_ch, "rx:0x%02X(%c) cursor:%d count:%d free:%d\r\n",
             rx_data,
             (rx_data >= CLI_KEY_SPACE && rx_data <= CLI_KEY_TILDE) ? rx_data : ' ',
             cli_node.line.cursor,
             cli_node.line.count,
             CLI_LINE_BUF_MAX - 1 - cli_node.line.count);
}



/* ================================================================ */
/*  cli_args_t 도우미 함수                                           */
/* ================================================================ */

/* index 번째 인자를 정수로 변환.
 * strtol 의 마지막 인자를 0 으로 주면 "0x" 로 시작할 때 16진수로 알아서 해석한다. */
static int32_t cliArgsGetData(uint8_t index)
{
  if (index >= cli_node.cmd_args.argc)
  {
    return 0;
  }
  return (int32_t)strtol(cli_node.cmd_args.argv[index], NULL, 0);
}

/* index 번째 인자를 실수로 변환 */
static float cliArgsGetFloat(uint8_t index)
{
  if (index >= cli_node.cmd_args.argc)
  {
    return 0.0f;
  }
  return (float)strtof(cli_node.cmd_args.argv[index], NULL);
}

/* index 번째 인자 문자열을 그대로 반환 */
static char *cliArgsGetStr(uint8_t index)
{
  if (index >= cli_node.cmd_args.argc)
  {
    return NULL;
  }
  return cli_node.cmd_args.argv[index];
}

/* index 번째 인자가 p_str 과 같은 문자열인지 확인 */
static bool cliArgsIsStr(uint8_t index, const char *p_str)
{
  if (index >= cli_node.cmd_args.argc)
  {
    return false;
  }
  if (strcmp(cli_node.cmd_args.argv[index], p_str) == 0)
  {
    return true;
  }
  return false;
}



/* ================================================================ */
/*  기본 내장 명령어                                                 */
/* ================================================================ */

/* help : 등록된 명령어 목록 출력 */
static void cliCmdHelp(cli_args_t *args)
{
  uint16_t i;

  (void)args;   /* 인자를 쓰지 않는다는 표시 (컴파일 경고 방지) */

  cliPrintf("---------- Command List (%d/%d) ----------\r\n",
            cli_node.cmd_count, CLI_CMD_LIST_MAX);

  for (i = 0; i < cli_node.cmd_count; i++)
  {
    cliPrintf("%s\r\n", cli_node.cmd_list[i].cmd_str);
  }
}

/*
 * md : Memory Dump
 *   사용법) md [주소] [길이]
 *   예)    md 0x20000000 32      <- SRAM 시작 주소에서 32바이트 덤프
 *          md 0x08000000 64      <- Flash 시작 주소에서 64바이트 덤프
 *
 *   ※ 존재하지 않는 주소를 읽으면 HardFault 가 발생할 수 있으니
 *      STM32F411 의 메모리 맵(Flash 0x0800_0000 / SRAM 0x2000_0000) 안에서 쓰세요.
 */
static void cliCmdMemoryDump(cli_args_t *args)
{
  uint32_t addr;
  uint32_t length;
  uint32_t i;
  uint32_t j;
  uint8_t  data;
  volatile uint8_t *p_data;

  if (args->argc != 2)
  {
    cliPrintf("Usage: md [addr] [length]\r\n");
    cliPrintf("  ex) md 0x20000000 32\r\n");
    return;
  }

  addr   = (uint32_t)args->getData(0);
  length = (uint32_t)args->getData(1);

  if (length == 0 || length > 1024)
  {
    cliPrintf("length must be 1 ~ 1024\r\n");
    return;
  }

  p_data = (volatile uint8_t *)(uintptr_t)addr;

  /* 한 줄에 16바이트씩 출력한다 */
  for (i = 0; i < length; i += 16)
  {
    cliPrintf("0x%08X : ", (unsigned int)(addr + i));

    /* 16진수 부분 */
    for (j = 0; j < 16; j++)
    {
      if ((i + j) < length)
      {
        cliPrintf("%02X ", p_data[i + j]);
      }
      else
      {
        cliPrintf("   ");
      }
    }

    /* 오른쪽에 사람이 읽을 수 있는 문자로도 보여준다 */
    cliPrintf(" ");
    for (j = 0; j < 16 && (i + j) < length; j++)
    {
      data = p_data[i + j];
      if (data >= CLI_KEY_SPACE && data <= CLI_KEY_TILDE)
      {
        cliPrintf("%c", data);
      }
      else
      {
        cliPrintf(".");   /* 글자로 표현 못 하는 값은 점으로 */
      }
    }
    cliPrintf("\r\n");
  }
}

/* info : 펌웨어와 CLI 설정 정보 출력 */
static void cliCmdInfo(cli_args_t *args)
{
  (void)args;

  cliPrintf("Board    : %s\r\n", _DEF_BOARD_NAME);
  cliPrintf("Version  : %s\r\n", _DEF_FIRMWATRE_VERSION);
  cliPrintf("Build    : %s %s\r\n", __DATE__, __TIME__);
  cliPrintf("Line Buf : %d bytes\r\n", CLI_LINE_BUF_MAX);
  cliPrintf("History  : %d ea (used %d)\r\n", CLI_LINE_HIS_MAX, cli_node.hist_count);
  cliPrintf("Cmd List : %d / %d\r\n", cli_node.cmd_count, CLI_CMD_LIST_MAX);
}

#endif /* _USE_HW_CLI */
