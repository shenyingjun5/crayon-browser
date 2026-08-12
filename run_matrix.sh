#!/bin/bash
# 站点覆盖测试脚本：extract -> (空则) sniff，日志存 logs/
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$SCRIPT_DIR"
mkdir -p logs

timeout() {
  local secs="$1"; shift
  "$@" &
  local pid=$!
  ( sleep "$secs"; kill -9 "$pid" 2>/dev/null ) &
  local wd=$!
  wait "$pid" 2>/dev/null
  kill "$wd" 2>/dev/null; wait "$wd" 2>/dev/null
}

run_one() {
  local id="$1" url="$2"
  pkill -f crayon-legacy-app 2>/dev/null; sleep 1
  echo "=== [$id] extract: $url"
  timeout 120 ./target/debug/crayon-legacy-app --extract-cli "$url" > "logs/${id}_extract.log" 2>&1
  local json
  json=$(grep -o 'EXTRACT_RESULT_JSON: .*' "logs/${id}_extract.log" | head -1 | sed 's/EXTRACT_RESULT_JSON: //')
  local need_sniff=1
  if [ -n "$json" ]; then
    # 用 python 解析：有 formats 且非空则不需要 sniff
    need_sniff=$(python3 -c "
import json,sys
try:
    d=json.loads(sys.argv[1])
    if 'error' in d: print(1)
    elif len(d.get('formats',[]))>0: print(0)
    else: print(1)
except Exception:
    print(1)
" "$json")
  else
    echo "!!! [$id] 无 EXTRACT_RESULT_JSON 输出"
  fi
  if [ "$need_sniff" = "1" ]; then
    pkill -f crayon-legacy-app 2>/dev/null; sleep 1
    echo "=== [$id] sniff: $url"
    timeout 120 ./target/debug/crayon-legacy-app --sniff-cli "$url" > "logs/${id}_sniff.log" 2>&1
  fi
}

run_one "$1" "$2"
echo "=== [$1] done"
