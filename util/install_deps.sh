#!/usr/bin/env bash
# install_deps.sh — установка зависимостей для clean_corpus.sh (Ubuntu/Debian)
set -euo pipefail

sudo apt-get update
sudo apt-get install -y parallel python3 python3-pip \
    hunspell hunspell-ru hunspell-en-us libhunspell-dev

pip3 install --user hunspell

echo "Зависимости установлены."
