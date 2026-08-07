#!/usr/bin/env python3
"""
process_line.py — построчная обработка корпуса (читает stdin, пишет stdout):
  1) склеивает слова, разорванные дефисом-артефактом переноса
     ("соответствен-ный" -> "соответственный", "шо-ковый" -> "шоковый");
  2) отфильтровывает строки, где доля несловарных/ошибочных слов > threshold
     (hunspell, словари ru_RU + en_US). Отдельного определения языка нет:
     строки на украинском/белорусском и т.п. отсеиваются тем же порогом —
     специфичные буквы (і, ї, є, ґ, ў) и большая часть лексики этих языков
     просто отсутствуют в ru_RU/en_US, поэтому доля "ошибок" в них обычно
     намного выше 30%.

Запускается как воркер `parallel --pipepart ... python3 process_line.py`,
поэтому словари загружаются один раз на процесс, а не на строку — иначе
на файле 100+ ГБ загрузка словарей "съест" всё время.
"""

import sys
import re
import argparse

import hunspell


WORD_RE = re.compile(r"[A-Za-zА-Яа-яЁё]+")
# Кандидат на "склейку через дефис": две буквенные части, дефис без пробелов.
HYPHEN_RE = re.compile(r"\b([A-Za-zА-Яа-яЁё]+)-([A-Za-zА-Яа-яЁё]+)\b")


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--threshold", type=float, default=0.3,
                    help="максимально допустимая доля несловарных слов в строке")
    p.add_argument("--min-words", type=int, default=1,
                    help="строки с меньшим числом слов пропускаются без фильтра по доле ошибок")
    p.add_argument("--ru-dic", default="/usr/share/hunspell/ru_RU.dic")
    p.add_argument("--ru-aff", default="/usr/share/hunspell/ru_RU.aff")
    p.add_argument("--en-dic", default="/usr/share/hunspell/en_US.dic")
    p.add_argument("--en-aff", default="/usr/share/hunspell/en_US.aff")
    return p.parse_args()


class SpellChecker:
    """Объединяет ru_RU и en_US словари hunspell в один чекер с кэшем."""

    def __init__(self, ru_dic, ru_aff, en_dic, en_aff):
        self.ru = hunspell.HunSpell(ru_dic, ru_aff)
        self.en = hunspell.HunSpell(en_dic, en_aff)
        self._cache = {}

    def is_known(self, word: str) -> bool:
        if len(word) <= 1:
            # однобуквенные предлоги/союзы (в, к, с, о, a, I ...) не считаем ошибкой
            return True
        cached = self._cache.get(word)
        if cached is not None:
            return cached
        ok = (
            self.ru.spell(word) or self.en.spell(word)
            or self.ru.spell(word.lower()) or self.en.spell(word.lower())
            or self.ru.spell(word.capitalize()) or self.en.spell(word.capitalize())
        )
        if len(self._cache) < 2_000_000:  # ограничиваем рост кэша на процесс
            self._cache[word] = ok
        return ok


def fix_hyphen_artifacts(line: str, spell: SpellChecker) -> str:
    """
    Если "часть1-часть2" при склейке даёт словарное слово, а хотя бы одна
    из частей по отдельности словом не является — считаем это артефактом
    переноса и склеиваем. Настоящие дефисные конструкции ("какой-то",
    "по-моему", "из-за", "премьер-министр") при склейке обычно словарного
    слова не дают, либо обе части сами по себе являются словами —
    такие строки не трогаются.
    """

    def repl(m: re.Match) -> str:
        part1, part2 = m.group(1), m.group(2)
        joined = part1 + part2
        if spell.is_known(joined) and not (spell.is_known(part1) and spell.is_known(part2)):
            return joined
        return m.group(0)

    return HYPHEN_RE.sub(repl, line)


def bad_word_ratio(words, spell: SpellChecker) -> float:
    if not words:
        return 0.0
    bad = sum(1 for w in words if not spell.is_known(w))
    return bad / len(words)


def main():
    args = parse_args()
    spell = SpellChecker(args.ru_dic, args.ru_aff, args.en_dic, args.en_aff)

    out = sys.stdout
    for raw_line in sys.stdin:
        line = raw_line.rstrip("\n")
        if not line.strip():
            continue

        # 1) склейка дефисных артефактов переноса
        line = fix_hyphen_artifacts(line, spell)

        # 2) доля несловарных/ошибочных слов (этот же порог отсеивает и
        #    строки на других языках — укр./бел./польск. и т.п., т.к. их
        #    лексика почти не пересекается с ru_RU/en_US)
        words = WORD_RE.findall(line)
        if len(words) < args.min_words:
            out.write(line + "\n")
            continue
        if bad_word_ratio(words, spell) > args.threshold:
            continue

        out.write(line + "\n")


if __name__ == "__main__":
    main()
