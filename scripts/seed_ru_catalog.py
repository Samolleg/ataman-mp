"""Seed unfinished Qt TS entries through Google Translate, preserving UI tokens.

This is an explicit maintainer tool, not part of the build. Review the resulting
catalog and run tests/localization_contract_tests.py before committing it.
"""

import concurrent.futures
import argparse
import json
import re
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "translations/ei_mapper_ru.ts"
TOKEN_RE = re.compile(r"%(?:L?\d+|[a-zA-Z])|</?[^>]+>|&&|&(?=\w)|\n")


def protect(text):
    tokens = []

    def replace(match):
        marker = f"ZXQ{len(tokens)}QXZ"
        tokens.append((marker, match.group(0)))
        return marker

    return TOKEN_RE.sub(replace, text), tokens


def restore(text, tokens):
    for marker, token in tokens:
        text = re.sub(re.escape(marker), lambda _: token, text,
                      flags=re.IGNORECASE)
    return text


def normalize(source, translated):
    translated = translated.replace("МОБ", "MOB").replace("МПР", "MPR")
    translated = translated.replace("МНР", "MPR").replace("ДЖМВ", "JMV")
    translated = translated.replace("НИД", "NID").replace("*.mоб", "*.mob")
    translated = re.sub(r"\bмоб(?:а|у|ом|е|ы|ов|ами|ах)?\b", "MOB",
                        translated, flags=re.IGNORECASE)
    if "Logic" in source:
        translated = translated.replace("Logic", "Логика")
    if "graph" in source.lower():
        translated = re.sub(r"график(а|у|ом|е|и|ов)?", r"граф\1",
                            translated, flags=re.IGNORECASE)
    return translated


def translate(source):
    protected, tokens = protect(source)
    query = urllib.parse.urlencode({
        "client": "gtx", "sl": "en", "tl": "ru", "dt": "t", "q": protected
    })
    request = urllib.request.Request(
        "https://translate.googleapis.com/translate_a/single?" + query,
        headers={"User-Agent": "EI-Mapper-Endurance-localization/1.0"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                payload = json.loads(response.read().decode("utf-8"))
            translated = "".join(part[0] for part in payload[0] if part[0])
            return restore(translated, tokens)
        except Exception:
            if attempt == 4:
                raise
            time.sleep(1.5 * (attempt + 1))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--all", action="store_true",
                        help="replace reviewed entries as well as unfinished entries")
    arguments = parser.parse_args()
    tree = ET.parse(CATALOG)
    messages = []
    for message in tree.getroot().findall(".//message"):
        translation = message.find("translation")
        if translation is not None and (arguments.all or
                translation.attrib.get("type") == "unfinished"):
            messages.append((message, message.findtext("source", default="")))

    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
        futures = {executor.submit(translate, source): (message, source)
                   for message, source in messages}
        completed = 0
        for future in concurrent.futures.as_completed(futures):
            message, source = futures[future]
            translated = normalize(source, future.result())
            node = message.find("translation")
            node.attrib.pop("type", None)
            node.text = translated
            completed += 1
            if completed % 100 == 0:
                ET.indent(tree, space="    ")
                tree.write(CATALOG, encoding="utf-8", xml_declaration=True)
                print(f"translated {completed}/{len(messages)}")

    ET.indent(tree, space="    ")
    tree.write(CATALOG, encoding="utf-8", xml_declaration=True)
    print(f"translated {len(messages)} messages")


if __name__ == "__main__":
    main()
