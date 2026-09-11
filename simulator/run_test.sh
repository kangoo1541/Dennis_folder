#!/usr/bin/env bash
#
# CLI 자동 테스트
#
#  키보드를 직접 누르는 대신 키 코드를 파이프로 밀어 넣어서
#  CLI 가 제대로 동작하는지 확인합니다.
#
#   \r       : Enter
#   \b       : Backspace (0x08)
#   \033[A   : 위쪽 방향키      \033[B : 아래쪽 방향키
#   \033[C   : 오른쪽 방향키    \033[D : 왼쪽 방향키
#   \033[1~  : Home             \033[4~ : End
#   \033[3~  : Delete
#
set -u

BIN=build/cli_sim
OUT=$(mktemp)
PASS=0
FAIL=0

run_cli() {
  printf "$1" | ./$BIN 2>/dev/null > "$OUT"
}

check() {
  local desc="$1"
  local pattern="$2"
  if grep -q -- "$pattern" "$OUT"; then
    printf '  [ OK ] %s\n' "$desc"
    PASS=$((PASS + 1))
  else
    printf '  [FAIL] %s   (기대한 문자열: %s)\n' "$desc" "$pattern"
    FAIL=$((FAIL + 1))
  fi
}

check_count() {
  local desc="$1"
  local pattern="$2"
  local want="$3"
  local got
  got=$(grep -c -- "$pattern" "$OUT")
  if [ "$got" = "$want" ]; then
    printf '  [ OK ] %s\n' "$desc"
    PASS=$((PASS + 1))
  else
    printf '  [FAIL] %s   (기대 %s회, 실제 %s회)\n' "$desc" "$want" "$got"
    FAIL=$((FAIL + 1))
  fi
}

if [ ! -x "$BIN" ]; then
  echo "먼저 make 로 빌드하세요."
  exit 1
fi

echo "=============================================="
echo " 1. 기본 명령어"
echo "=============================================="
run_cli 'help\rinfo\rled list\r'
check "help 이 명령어 목록을 출력한다"        "Command List"
check "help 목록에 HELP 가 있다"              "^HELP"
check "help 목록에 MD 가 있다"                "^MD"
check "help 목록에 INFO 가 있다"              "^INFO"
check "led.c 가 등록한 LED 명령어가 보인다"   "^LED"
check "info 가 보드 이름을 출력한다"          "STM32F411-CLI"
check "led list 가 채널 상태를 출력한다"      "ch 0 : OFF"

echo
echo "=============================================="
echo " 2. 인자 파싱 / 사용법 안내"
echo "=============================================="
run_cli 'led on 0\rled off 0\rled\rmd\rnocmd\rLeD On 0\r'
check "led on 0  실행"                        "led 0 on"
check "led off 0 실행"                        "led 0 off"
check "인자가 틀리면 사용법 출력"             "led toggle \[ch\] \[time_ms\]"
check "md 인자 없으면 사용법 출력"            "Usage: md \[addr\] \[length\]"
check "없는 명령어는 Unknown 출력"            "NOCMD \] : Unknown command"
check "대소문자를 섞어 써도 인식한다"         "led 0 on"

echo
echo "=============================================="
echo " 3. 라인 편집 (커서 이동 / 삽입 / 삭제)"
echo "=============================================="
run_cli 'helpX\b\r'
check "Backspace 로 오타 지우기 -> help 실행"  "Command List"

run_cli 'elp\033[1~h\r'
check "Home 으로 맨 앞에 글자 삽입 -> help"    "Command List"

run_cli 'hep\033[Dl\r'
check "왼쪽 방향키로 중간 삽입 -> help"        "Command List"

run_cli 'xhelp\033[1~\033[3~\r'
check "Home + Delete 로 앞 글자 삭제 -> help"  "Command List"

run_cli 'help\033[1~\033[3~\033[3~\033[3~\033[3~info\r'
check "Delete 4번으로 비우고 새로 입력 -> info" "STM32F411-CLI"

run_cli 'hlp\033[1~\033[Ce\033[4~\r'
check "Home/오른쪽/End 조합 편집 -> help"      "Command List"

echo
echo "=============================================="
echo " 4. 명령어 히스토리 (방향키 위/아래)"
echo "=============================================="
run_cli 'info\rled list\r\033[A\033[A\r'
check_count "위 방향키 2번 -> info 재실행 (info 출력 2회)" "^Board" 2

run_cli 'info\rled list\r\033[A\033[A\033[B\r'
check_count "위2번+아래1번 -> led list 재실행 (총 2회)"  "ch 0 : OFF" 2

run_cli 'led list\rled on 0\r\033[A\033[A\r'
check "히스토리에서 꺼낸 명령 그대로 실행"     "led 0 on"

# 원형 버퍼 확인 : 기록은 4개까지만 남는다 (HW_CLI_LINE_HIS_MAX = 4)
run_cli 'info\rled list\rled on 0\rled off 0\rled list\r\033[A\033[A\033[A\033[A\033[A\r'
check_count "기록은 4개까지만 -> 가장 오래된 info 는 밀려나 재실행되지 않는다" "^Board" 1

run_cli 'led on 0\r\033[A\b1\r'
check "히스토리를 불러와 인자만 고쳐 재실행"   "ch must be 0 ~ 0"

run_cli 'help\rabc\033[A\033[B\r'
check "↑로 히스토리를 봤다가 ↓로 돌아오면 치던 줄이 복원된다" "ABC \] : Unknown command"

run_cli 'help\r\033[A\033[A\033[A\r'
check_count "가장 오래된 기록보다 더 올라가도 문제없다" "Command List" 2

echo
echo "=============================================="
echo " 5. md (메모리 덤프)"
echo "=============================================="
# -no-pie 로 빌드했으므로 전역변수 cli_node 의 주소를 미리 알 수 있다.
ADDR=$(nm build/cli_sim | awk '$3 == "cli_node" { print "0x"$1 }')
if [ -n "$ADDR" ]; then
  run_cli "md $ADDR 32\r"
  check "md 가 16진수 덤프를 출력한다"          "0x0*${ADDR#0x}"
  check_count "32바이트 요청 -> 16바이트씩 2줄"  " : " 2
else
  echo "  [SKIP] cli_node 심볼을 찾지 못함"
fi

run_cli 'md 0x1000 0\r'
check "길이 0 은 거부한다"                     "length must be 1 ~ 1024"

echo
echo "=============================================="
echo " 6. 버퍼 오버플로우 방어"
echo "=============================================="
# CLI_LINE_BUF_MAX = 32 이므로 31글자까지만 들어가야 한다.
LONG=$(printf 'A%.0s' $(seq 1 60))
run_cli "${LONG}\r"
check "32자를 넘겨도 죽지 않고 Unknown 처리"    "Unknown command"
run_cli "${LONG}\rhelp\r"
check "오버플로우 후에도 정상 동작한다"         "Command List"

echo
echo "=============================================="
echo " 7. cliKeepLoop (아무 키나 누르면 중단)"
echo "=============================================="
# led toggle 실행 중 'q' 를 누르면 루프를 빠져나와야 한다.
run_cli 'led toggle 0 10\rqhelp\r'
check "toggle 시작 메시지 출력"                "press any key to stop"
check "키 입력으로 루프 탈출 후 다음 명령 실행" "Command List"

echo
echo "=============================================="
printf ' 결과 : 성공 %d / 실패 %d\n' "$PASS" "$FAIL"
echo "=============================================="

rm -f "$OUT"

[ "$FAIL" -eq 0 ]
