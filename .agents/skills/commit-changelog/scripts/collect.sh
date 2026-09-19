#!/usr/bin/env bash
# 收集某个提交生成 changelog 所需的全部原始信息。
# 用法: bash collect.sh [commit-ish]   （缺省 HEAD）
# 输出: FILE/META/DOCS/STAT 段 + 完整 diff（可能截断）。
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
ref="${1:-HEAD}"

if ! git rev-parse --verify --quiet "$ref^{commit}" >/dev/null; then
  echo "ERROR: '$ref' 不是有效提交" >&2
  exit 1
fi

# --- 序号：目录内最大 3 位序号 + 1 ---
dir="docs/changelogs"
last=""
if [ -d "$dir" ]; then
  last=$(ls "$dir" | grep -E '^[0-9]{3}-' | sort -r | head -1 | cut -c1-3 || true)
fi
next=$((10#${last:-0} + 1))
seq=$(printf '%03d' "$next")

# --- hash 与 slug ---
short=$(git rev-parse --short=7 "$ref")
full=$(git rev-parse "$ref")
subject=$(git log -1 --format=%s "$ref")
# slug: 去掉 conventional commit 前缀，转小写，非字母数字折叠为 -，截断 60 字符
slug=$(printf '%s' "$subject" \
  | sed -E 's/^[a-z]+(\([^)]*\))?:[[:space:]]*//' \
  | tr '[:upper:]' '[:lower:]' \
  | sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//' \
  | cut -c1-60 \
  | sed -E 's/-+$//')

# --- 重复检查 ---
if [ -d "$dir" ] && ls "$dir" | grep -q -- "-${short}-"; then
  echo "WARNING: 目录中已存在包含短 hash ${short} 的文件：" >&2
  ls "$dir" | grep -- "-${short}-" >&2
fi

echo "FILE: ${dir}/${seq}-${short}-${slug}.md"
echo
echo "META:"
git show -s --format='  full: %H%n  date: %ad%n  author: %an%n  parents: %P%n  subject: %s' --date=format:'%Y-%m-%d %H:%M:%S %z' "$ref"
echo
echo "DOCS:"
git show --name-only --format= "$ref" | grep -E '^docs/.*\.md$' | sed 's/^/  /' || echo "  (none)"
echo
echo "STAT:"
git show --stat --format= "$ref" | sed 's/^/  /'
echo
echo "DIFF:"
git show --format= -U3 "$ref"
