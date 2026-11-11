#!/usr/bin/env bash
#
# 진도 확인 스크립트
#
#   사용법:  bash study/progress.sh
#
#   study/PROGRESS.md 의 체크박스를 세어서
#   전체 진도율, Phase 별 진도율, 다음에 할 일을 보여 줍니다.
#
set -eu

FILE="$(dirname "$0")/PROGRESS.md"
[ -f "$FILE" ] || { echo "PROGRESS.md 를 찾을 수 없습니다: $FILE"; exit 1; }

awk '
function bar(done, total,   w, n, s, i) {
  w = 20
  n = (total > 0) ? int(done * w / total) : 0
  s = ""
  for (i = 0; i < w; i++) s = s ((i < n) ? "#" : ".")
  return s
}
/^## Phase/ {
  p++
  name[p] = substr($0, 4)
}
/^- \[[ xX]\]/ {
  total[p]++; all++
  if ($0 ~ /^- \[[xX]\]/) { done[p]++; alldone++ }
  else if (next_item == "") next_item = substr($0, 7)
}
END {
  printf "\n  SW 엔지니어 성장 과정 진도\n"
  printf "  ==========================================================\n"
  for (i = 1; i <= p; i++) {
    d = done[i] + 0; t = total[i] + 0
    printf "  [%s] %3d%%  %3d/%-3d  %s\n", bar(d, t), (t ? d * 100 / t : 0), d, t, name[i]
  }
  printf "  ----------------------------------------------------------\n"
  printf "  [%s] %3d%%  %3d/%-3d  전체\n", bar(alldone, all), (all ? alldone * 100 / all : 0), alldone + 0, all
  printf "\n  다음 할 일: %s\n\n", (next_item == "" ? "모든 과정 완료! 🎉" : next_item)
}
' "$FILE"
