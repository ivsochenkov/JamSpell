#!/usr/bin/env bash
#
# clean_corpus.sh — очистка большого текстового корпуса (RU/EN) от:
#   1) артефактов переноса слов через дефис без разрыва строки
#      ("соответствен-ный" -> "соответственный");
#   2) строк, где доля несловарных/ошибочных слов > заданного порога.
#      Тем же порогом отсеиваются и строки на других языках (укр., бел.
#      и т.п.) — их лексика почти не пересекается со словарями ru_RU/en_US,
#      поэтому доля "ошибок" в них обычно намного выше порога.
#
# Использование:
#   ./clean_corpus.sh -i input.txt -o output.txt [-t 0.3] [-j N] [-b 64M]
#
# Зависимости (см. также install_deps.sh):
#   - GNU parallel
#   - python3, пакет hunspell (python-биндинг, например пакет "hunspell" из PyPI)
#   - системные словари hunspell: ru_RU, en_US (пакеты hunspell-ru, hunspell-en-us)
#
set -euo pipefail

INPUT=""
OUTPUT=""
THRESHOLD=0.3
JOBS="$(nproc)"
BLOCK="64M"
MIN_WORDS=1
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKER="${SCRIPT_DIR}/process_line.py"

usage() {
  cat <<EOF
Использование: $0 -i input.txt -o output.txt [опции]
  -i FILE     входной файл корпуса (обязателен)
  -o FILE     выходной файл (обязателен)
  -t FLOAT    порог доли несловарных слов, 0..1 (по умолчанию 0.3)
  -w N        мин. число слов в строке, ниже которого фильтр по доле не применяется (по умолчанию 1)
  -j N        число параллельных процессов (по умолчанию: число ядер)
  -b SIZE     размер блока для parallel --pipepart (по умолчанию 64M)
  -h          показать эту справку

Пример:
  $0 -i corpus_100gb.txt -o corpus_clean.txt -t 0.3 -j 16 -b 128M
EOF
  exit 1
}

while getopts "i:o:t:w:j:b:h" opt; do
  case "$opt" in
    i) INPUT="$OPTARG" ;;
    o) OUTPUT="$OPTARG" ;;
    t) THRESHOLD="$OPTARG" ;;
    w) MIN_WORDS="$OPTARG" ;;
    j) JOBS="$OPTARG" ;;
    b) BLOCK="$OPTARG" ;;
    h) usage ;;
    *) usage ;;
  esac
done

[[ -z "$INPUT" || -z "$OUTPUT" ]] && usage
[[ -f "$INPUT" ]] || { echo "Входной файл не найден: $INPUT" >&2; exit 1; }
[[ -f "$WORKER" ]] || { echo "Не найден воркер: $WORKER (должен лежать рядом со скриптом)" >&2; exit 1; }

command -v parallel >/dev/null || { echo "Нужен GNU parallel (sudo apt install parallel)" >&2; exit 1; }
command -v python3   >/dev/null || { echo "Нужен python3" >&2; exit 1; }

echo "Вход:      $INPUT" >&2
echo "Выход:     $OUTPUT" >&2
echo "Процессы:  $JOBS, блок $BLOCK" >&2
echo "Порог мусорных слов: $THRESHOLD" >&2

# --pipepart разбивает файл на части по байтовым смещениям без физического
# копирования на диск — критично для файлов 100+ ГБ.
# -k сохраняет порядок строк, соответствующий исходному файлу.
parallel --pipepart -a "$INPUT" --block "$BLOCK" -j "$JOBS" --recend '\n' -k \
  python3 "$WORKER" --threshold "$THRESHOLD" --min-words "$MIN_WORDS" \
  > "$OUTPUT"

echo "Готово. Результат: $OUTPUT" >&2
