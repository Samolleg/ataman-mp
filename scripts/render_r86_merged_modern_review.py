"""Merge the audited modern R64 queue with the focused Zone13 R85 review."""

from __future__ import annotations

import hashlib
import html
import json
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIT = Path(r"C:\Users\ilufu\Downloads\ПЗ Жамевю\Codex\work\semantic-atlas-r85-modern-r64-audit\bundle")
AUDIT_JSON = AUDIT / "audit-results.json"
AUDIT_HTML = AUDIT / "modern-r64-semantic-audit.html"
R85 = ROOT / "artifacts" / "semantic-atlas-r85-zone13-slabs-review"
OUTPUT = AUDIT / "semantic-atlas-r86-merged-questionnaire.html"
MANIFEST = AUDIT / "merged-questionnaire-manifest.json"

R85_TARGETS = (
    (113, "NW"), (114, "NW"), (115, "NW"), (116, "NW"),
    (117, "NW"), (118, "NW"), (376, "SW"),
)
R85_REFERENCES = (
    ("zone10", 212, "Slabs / Square", "Reviewed cross-atlas uniform"),
    ("zone13", 61, "Paving / Square", "Same-atlas uniform"),
    ("zone13", 7, "Ground / Light", "Same-atlas uniform"),
    ("zone13", 0, "Snow / Common", "Same-atlas uniform"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def select_options(result: dict) -> list[str]:
    values = [result["authoritativeDefault"]]
    if result.get("auditSuggestion"):
        values.append(result["auditSuggestion"])
    for evidence in result.get("variantEvidence", {}).values():
        for candidate in evidence.get("globalFamilyTopThree", []):
            values.append(candidate["family"])
    values.extend((
        "Not uniform — review all four corners",
        "Different family (note)",
        "Still uncertain",
    ))
    return list(dict.fromkeys(values))


def controls(key: str, default: str, options: list[str], audit: str,
             suggestion: str | None = None) -> str:
    rendered = []
    for value in options:
        label = value
        if value == default:
            label = f"Keep current: {value}"
        elif suggestion and value == suggestion:
            label = f"Audit suggestion: {value}"
        rendered.append(
            f'<option value="{html.escape(value, quote=True)}"'
            f'{" selected" if value == default else ""}>{html.escape(label)}</option>'
        )
    badge = html.escape(audit)
    return (
        f'<div class="review-controls" data-key="{html.escape(key, quote=True)}" '
        f'data-default="{html.escape(default, quote=True)}">'
        f'<span class="audit-badge {html.escape(audit, quote=True)}">Audit: {badge}</span>'
        f'<label>Decision<select class="answer">{"".join(rendered)}</select></label>'
        '<label>Comment<input class="note" placeholder="Optional comparison note or exact family name"></label>'
        '</div>'
    )


def inject_audit_controls(source: str, results: list[dict]) -> str:
    for result in results:
        key = result["key"]
        pattern = re.compile(
            rf'(<article class="group [^"]+" id="{re.escape(key)}"><h2>.*?</h2>)'
        )
        block = controls(
            f"r86-{key.replace('/', '-family-')}",
            result["authoritativeDefault"],
            select_options(result),
            result["classification"],
            result.get("auditSuggestion"),
        )
        source, count = pattern.subn(rf"\1{block}", source, count=1)
        if count != 1:
            raise ValueError(f"could not inject review controls for {key}")
    return source


def r85_card(namespace: str, slot: int, family: str, role: str,
             corner: str | None = None) -> str:
    name = f"hd-{namespace}-{slot}.jpg"
    marker = f'<span class="corner-marker {corner.lower()}">{corner}</span>' if corner else ""
    return (
        '<div class="r85-card"><div class="r85-tile">'
        f'<img loading="lazy" src="images/r85/{name}" alt="{namespace} slot {slot}">{marker}</div>'
        f'<strong>{html.escape(family)}</strong><span>{namespace} slot {slot}</span>'
        f'<small>{html.escape(role)}</small></div>'
    )


def r85_section() -> str:
    questions = []
    options = [
        "Slabs / Square", "Paving / Square", "Ground / Light",
        "Snow / Common", "Not uniform — review all four corners",
        "Different family (note)", "Still uncertain",
    ]
    for number, (slot, corner) in enumerate(R85_TARGETS, 1):
        key = f"zone13-slot{slot}-{corner.lower()}"
        cards = r85_card("zone13", slot, "Question target",
                         "Current corner: Slabs / Square", corner)
        cards += "".join(r85_card(*reference) for reference in R85_REFERENCES)
        questions.append(
            f'<article class="group focused" id="{key}"><h2>Zone13 focus {number}. '
            f'<code>{key}</code></h2>'
            '<p>Review the highlighted corner. No questioned tile from either review batch is used as a reference.</p>'
            f'{controls(key, "Slabs / Square", options, "focused-existing")}'
            f'<div class="r85-row">{cards}</div></article>'
        )
    return (
        '<section id="zone13-focused"><h1>Existing Zone13 Slabs / Square questions</h1>'
        '<p class="lead">These seven questions are retained from R85 and share the same answer export below.</p>'
        + "".join(questions) + '</section>'
    )


def copy_r85_images() -> None:
    destination = AUDIT / "images" / "r85"
    destination.mkdir(parents=True, exist_ok=True)
    needed = {("zone13", slot) for slot, _corner in R85_TARGETS}
    needed.update((namespace, slot) for namespace, slot, _family, _role in R85_REFERENCES)
    for namespace, slot in sorted(needed):
        source = R85 / "images" / f"hd-{namespace}-{slot}.jpg"
        if not source.is_file():
            raise FileNotFoundError(source)
        shutil.copy2(source, destination / source.name)


def main() -> None:
    audit = json.loads(AUDIT_JSON.read_text(encoding="utf-8"))
    source = AUDIT_HTML.read_text(encoding="utf-8")
    source = inject_audit_controls(source, audit["results"])
    extra_css = '''
.review-toolbar{position:sticky;top:0;z-index:20;background:#17181b;border:1px solid #5f6368;padding:10px;display:flex;gap:8px;align-items:center;flex-wrap:wrap}.review-toolbar button{font:inherit;color:#fff;background:#303134;border:1px solid #777;border-radius:4px;padding:8px}.review-toolbar .primary{background:#1769aa}#answer-export{width:min(1300px,96vw);height:120px;background:#111;color:#eee}.review-controls{display:grid;grid-template-columns:auto minmax(260px,390px) minmax(320px,1fr) auto;gap:10px;align-items:end;background:#1f2023;border:1px solid #6a6d72;padding:10px;margin:10px 0}.review-controls label{display:grid;gap:4px}.review-controls select,.review-controls input{font:inherit;background:#303134;color:#fff;border:1px solid #777;padding:7px}.audit-badge{padding:7px;border-radius:4px}.audit-badge.agreement{background:#176b3a}.audit-badge.conflict{background:#8b2520}.audit-badge.insufficient-evidence{background:#795d08}.audit-badge.focused-existing{background:#30578d}.reviewed-label{display:flex!important;align-items:center;gap:6px}.r85-row{display:flex;gap:12px;overflow-x:auto}.r85-card{flex:0 0 250px;background:#18191c;border:1px solid #5f6368;padding:8px}.r85-card strong,.r85-card span,.r85-card small{display:block;margin-top:4px}.r85-tile{position:relative;width:234px;height:234px}.r85-tile img{width:100%;height:100%}.corner-marker{position:absolute;border:5px solid #ff3030;background:#8b0000dd;padding:3px;font-weight:bold}.corner-marker.nw{left:0;top:0}.corner-marker.ne{right:0;top:0}.corner-marker.se{right:0;bottom:0}.corner-marker.sw{left:0;bottom:0}@media(max-width:850px){.review-controls{grid-template-columns:1fr}}
'''
    source = source.replace("</style>", extra_css + "</style>", 1)
    toolbar = '''<div class="review-toolbar"><button class="primary" id="copy-answers">Copy changed answers</button><button id="download-answers">Download answers.txt</button><button id="select-answers">Select answer text</button><button id="reset-answers">Reset local answers</button><span id="review-status"></span></div><textarea id="answer-export" readonly></textarea>'''
    source = source.replace("<body>", "<body>" + toolbar, 1)
    source = source.replace("</body></html>", r85_section())
    script = '''<script>
const prefix='semantic-r86-merged:';const controls=[...document.querySelectorAll('.review-controls')];const out=document.getElementById('answer-export'),status=document.getElementById('review-status');
function row(c){const value=c.querySelector('.answer').value,note=c.querySelector('.note').value.trim();return{key:c.dataset.key,value,note,changed:value!==c.dataset.default}}
function answerText(){const lines=['Semantic-atlas R86 merged focused review decisions:'];for(const x of controls.map(row)){if(x.changed||x.note)lines.push(x.key+': '+(x.changed?x.value:'Comment')+(x.note?' — '+x.note:''))}lines.push('Apply only explicit decisions; verify exact connected corners before propagation.');return lines.join('\\n')}
function update(message){out.value=answerText();const count=controls.map(row).filter(x=>x.changed||x.note).length;status.textContent=message||count+' changed response'+(count===1?'':'s')}
function save(c){localStorage.setItem(prefix+c.dataset.key,JSON.stringify(row(c)));update('Saved locally')}
for(const c of controls){try{const x=JSON.parse(localStorage.getItem(prefix+c.dataset.key)||'null');if(x){c.querySelector('.answer').value=x.value||c.dataset.default;c.querySelector('.note').value=x.note||''}}catch(e){}for(const el of c.querySelectorAll('select,input'))el.addEventListener(el.type==='text'?'input':'change',()=>save(c))}
function selectText(message){out.focus();out.select();update(message)}
document.getElementById('select-answers').onclick=()=>selectText('Answer text selected');
document.getElementById('copy-answers').onclick=async()=>{try{await navigator.clipboard.writeText(answerText());update('Changed answers copied')}catch(e){selectText('Clipboard unavailable; answer text selected')}};
document.getElementById('download-answers').onclick=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([answerText()],{type:'text/plain;charset=utf-8'}));a.download='semantic-atlas-r86-merged-answers.txt';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),0);update('Answers downloaded')};
document.getElementById('reset-answers').onclick=()=>{for(const c of controls){localStorage.removeItem(prefix+c.dataset.key);c.querySelector('.answer').value=c.dataset.default;c.querySelector('.note').value=''}update('Local answers reset')};
update();window.__SEMANTIC_R86_READY__={questions:controls.length};
</script></body></html>'''
    source += script
    copy_r85_images()
    OUTPUT.write_text(source, encoding="utf-8")
    manifest = {
        "schema": "ei-semantic-atlas-r86-merged-questionnaire-v1",
        "semanticCheckpoint": audit["checkpoint"],
        "auditSummary": audit["summary"],
        "auditQuestionCount": len(audit["results"]),
        "focusedQuestionCount": len(R85_TARGETS),
        "totalQuestionCount": len(audit["results"]) + len(R85_TARGETS),
        "humanDefaultsPreserved": True,
        "queuedTargetsExcludedFromReferences": True,
        "sourceAuditSha256": sha256(AUDIT_JSON),
        "sourceR85Sha256": sha256(R85 / "semantic-atlas-r85-zone13-slabs-questionnaire.html"),
        "htmlSha256": sha256(OUTPUT),
    }
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"html": str(OUTPUT), **manifest}, indent=2))


if __name__ == "__main__":
    main()
