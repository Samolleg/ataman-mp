#!/usr/bin/env python3
"""Build the bilingual offline manual with only the Python standard library.

The renderer intentionally supports the small, documented Markdown subset used
by en/manual.md and ru/manual.md. Keeping it local makes the committed HTML
reproducible without a network or project-wide documentation toolchain.
"""

from __future__ import annotations

import html
import re
from pathlib import Path


MANUAL_ROOT = Path(__file__).resolve().parents[1]
SPECS = {
    "en": {
        "source": MANUAL_ROOT / "en" / "manual.md",
        "target": MANUAL_ROOT / "html" / "en" / "index.html",
        "title": "EI Mapper Endurance User Manual",
        "index": "Manual index",
        "other": "Русский",
    },
    "ru": {
        "source": MANUAL_ROOT / "ru" / "manual.md",
        "target": MANUAL_ROOT / "html" / "ru" / "index.html",
        "title": "Руководство пользователя EI Mapper Endurance",
        "index": "Содержание руководства",
        "other": "English",
    },
}


def slugify(text: str) -> str:
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"[`*_]", "", text).strip().lower()
    text = re.sub(r"[^\w\-\u0400-\u04ff]+", "-", text, flags=re.UNICODE)
    return text.strip("-") or "section"


def _tokenize_code(text: str) -> tuple[str, list[str]]:
    tokens: list[str] = []

    def replace(match: re.Match[str]) -> str:
        tokens.append(f"<code>{html.escape(match.group(1))}</code>")
        return f"\x00CODE{len(tokens) - 1}\x00"

    return re.sub(r"`([^`]+)`", replace, text), tokens


def inline(text: str, *, html_depth: int) -> str:
    text, code_tokens = _tokenize_code(text)
    text = html.escape(text, quote=False)

    def image(match: re.Match[str]) -> str:
        alt = html.escape(html.unescape(match.group(1)), quote=True)
        src = html.unescape(match.group(2))
        if src.startswith("../assets/") and html_depth == 2:
            src = "../../assets/" + src[len("../assets/"):]
        return f'<img src="{html.escape(src, quote=True)}" alt="{alt}" loading="lazy">'

    def link(match: re.Match[str]) -> str:
        label = match.group(1)
        href = html.unescape(match.group(2))
        if href.startswith("../") and not href.startswith("../assets/") and html_depth == 2:
            href = "../../" + href[3:]
        return f'<a href="{html.escape(href, quote=True)}">{label}</a>'

    text = re.sub(r"!\[([^]]*)\]\(([^)]+)\)", image, text)
    text = re.sub(r"(?<!!)\[([^]]+)\]\(([^)]+)\)", link, text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)
    for index, token in enumerate(code_tokens):
        text = text.replace(f"\x00CODE{index}\x00", token)
    return text


def _table_cells(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def _is_table_separator(line: str) -> bool:
    cells = _table_cells(line)
    return bool(cells) and all(re.fullmatch(r":?-{3,}:?", c) for c in cells)


def render_markdown(markdown: str, *, html_depth: int = 2) -> str:
    lines = markdown.replace("\r\n", "\n").split("\n")
    out: list[str] = []
    i = 0
    heading_ids: dict[str, int] = {}

    def heading_id(label: str) -> str:
        base = slugify(label)
        count = heading_ids.get(base, 0)
        heading_ids[base] = count + 1
        return base if count == 0 else f"{base}-{count + 1}"

    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue

        anchor = re.fullmatch(r'\s*<a id="([A-Za-z0-9_-]+)"></a>\s*', line)
        if anchor:
            out.append(f'<a id="{anchor.group(1)}"></a>')
            i += 1
            continue

        fence = re.match(r"^```(.*)$", line)
        if fence:
            language = fence.group(1).strip()
            code: list[str] = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            i += 1
            cls = f' class="language-{html.escape(language, quote=True)}"' if language else ""
            out.append(f"<pre><code{cls}>{html.escape(chr(10).join(code))}</code></pre>")
            continue

        heading = re.match(r"^(#{1,6})\s+(.+)$", line)
        if heading:
            level = len(heading.group(1))
            label = heading.group(2).strip()
            out.append(f'<h{level} id="{heading_id(label)}">{inline(label, html_depth=html_depth)}</h{level}>')
            i += 1
            continue

        if line.startswith(">"):
            quoted: list[str] = []
            while i < len(lines) and (lines[i].startswith(">") or not lines[i].strip()):
                if lines[i].startswith(">"):
                    quoted.append(re.sub(r"^> ?", "", lines[i]))
                else:
                    quoted.append("")
                i += 1
                if i < len(lines) and not lines[i].startswith(">") and lines[i].strip():
                    break
            out.append("<blockquote>" + render_markdown("\n".join(quoted), html_depth=html_depth) + "</blockquote>")
            continue

        if i + 1 < len(lines) and line.lstrip().startswith("|") and _is_table_separator(lines[i + 1]):
            headers = _table_cells(line)
            i += 2
            rows: list[list[str]] = []
            while i < len(lines) and lines[i].lstrip().startswith("|"):
                rows.append(_table_cells(lines[i]))
                i += 1
            out.append("<table><thead><tr>" + "".join(
                f"<th>{inline(c, html_depth=html_depth)}</th>" for c in headers
            ) + "</tr></thead><tbody>")
            for row in rows:
                padded = row + [""] * max(0, len(headers) - len(row))
                out.append("<tr>" + "".join(
                    f"<td>{inline(c, html_depth=html_depth)}</td>" for c in padded[:len(headers)]
                ) + "</tr>")
            out.append("</tbody></table>")
            continue

        list_match = re.match(r"^(\s*)([-+*]|\d+\.)\s+(.+)$", line)
        if list_match:
            ordered = list_match.group(2).endswith(".") and list_match.group(2)[0].isdigit()
            tag = "ol" if ordered else "ul"
            items: list[str] = []
            while i < len(lines):
                item = re.match(r"^(\s*)([-+*]|\d+\.)\s+(.+)$", lines[i])
                if not item:
                    break
                is_ordered = item.group(2).endswith(".") and item.group(2)[0].isdigit()
                if is_ordered != ordered or len(item.group(1)) != len(list_match.group(1)):
                    break
                text_parts = [item.group(3).strip()]
                i += 1
                while i < len(lines):
                    if not lines[i].strip():
                        break
                    if re.match(r"^\s*([-+*]|\d+\.)\s+", lines[i]):
                        break
                    if lines[i].startswith(" "):
                        text_parts.append(lines[i].strip())
                        i += 1
                    else:
                        break
                items.append(" ".join(text_parts))
                while i < len(lines) and not lines[i].strip():
                    # A blank ends the list unless another same-kind item follows.
                    look = i + 1
                    while look < len(lines) and not lines[look].strip():
                        look += 1
                    nxt = re.match(r"^(\s*)([-+*]|\d+\.)\s+", lines[look]) if look < len(lines) else None
                    if nxt and len(nxt.group(1)) == len(list_match.group(1)):
                        i = look
                    break
            out.append(f"<{tag}>" + "".join(
                f"<li>{inline(item, html_depth=html_depth)}</li>" for item in items
            ) + f"</{tag}>")
            continue

        paragraph = [line.strip()]
        i += 1
        while i < len(lines) and lines[i].strip():
            candidate = lines[i]
            if (re.match(r"^(#{1,6})\s+", candidate)
                    or re.fullmatch(r'\s*<a id="[A-Za-z0-9_-]+"></a>\s*', candidate)
                    or candidate.startswith(">")
                    or candidate.startswith("```")
                    or re.match(r"^\s*([-+*]|\d+\.)\s+", candidate)
                    or (i + 1 < len(lines) and candidate.lstrip().startswith("|")
                        and _is_table_separator(lines[i + 1]))):
                break
            paragraph.append(candidate.strip())
            i += 1
        out.append(f"<p>{inline(' '.join(paragraph), html_depth=html_depth)}</p>")

    return "\n".join(out)


def render_document(lang: str) -> str:
    spec = SPECS[lang]
    other = "ru" if lang == "en" else "en"
    body = render_markdown(spec["source"].read_text(encoding="utf-8"))
    return "\n".join([
        "<!doctype html>",
        f'<html lang="{lang}">',
        "<head>",
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        f"<title>{html.escape(spec['title'])}</title>",
        '<link rel="stylesheet" href="../../assets/manual.css">',
        "</head>",
        "<body><main>",
        '<nav class="manual-nav" aria-label="Manual navigation">',
        f'<a href="../../README.md">{html.escape(spec["index"])}</a>',
        f'<a href="../{other}/index.html">{html.escape(spec["other"])}</a>',
        "</nav>",
        body,
        "</main></body>",
        "</html>",
        "",
    ])


def build() -> None:
    for lang, spec in SPECS.items():
        target: Path = spec["target"]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(render_document(lang), encoding="utf-8", newline="\n")
        print(f"built {target.relative_to(MANUAL_ROOT)}")


if __name__ == "__main__":
    build()
