"""Contact sheets from a completed or failed C++ corpus run (no synthesis)."""
from pathlib import Path
import html
import json
import random
import textwrap
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
TASK = ROOT/'build-tests-145'


def main():
    inputs = json.loads((TASK/'inputs/inputs.json').read_text(encoding='utf-8'))
    results = json.loads((TASK/'corpus-final/results.json').read_text(encoding='utf-8'))
    output = TASK/'review'
    output.mkdir(exist_ok=True)
    font_path = Path('C:/Windows/Fonts/arial.ttf')
    title = ImageFont.truetype(str(font_path), 23)
    font = ImageFont.truetype(str(font_path), 17)
    index = ['<!doctype html><meta charset="utf-8"><title>EI-HD-VARIANTS-A-145</title>',
             '<style>body{font:18px sans-serif;background:#202329;color:#eee;margin:32px;max-width:1500px}a{color:#9dcaff}img{max-width:100%}section{border-top:1px solid #666;padding:24px 0}.fail{color:#ffcb8a}</style>',
             '<h1>HD-варианты материалов: метод А, версия 145</h1>',
             f'<p>Комбинаций материал/класс/слой: {len(results["groups"])}; новых вариантов: {results["generated"]}; отклонено кандидатов: {results["rejected"]}.</p>',
             '<p>Пороги фильтров не ослаблены. Недобор после 128 кандидатов отмечен отдельно по решению владельца. '
             'Исходные HD слева; принятые варианты справа. ID новых вариантов приведены полностью.</p>',
             '<p>Поле 12×12 — сравнение текстур без освещения и геометрии; одинаковое зерно выбора и поворотов. '
             'Художественную приёмку выполняет владелец. Игровые пиксели находятся только в этой приватной папке проверки.</p>',
             '<nav>'+ ' · '.join(f'<a href="#g{i:03}">{html.escape(g["key"])}</a>' for i,g in enumerate(results['groups']))+'</nav>']
    for number, (group, result) in enumerate(zip(inputs['groups'], results['groups'])):
        assert group['key'] == result['key']
        name = f'{number:03}'
        originals = [Image.open(TASK/'inputs'/s['png']).convert('RGB') for s in group['sources']]
        generated = [Image.open(TASK/'corpus-final'/name/(v['id']+'.png')).convert('RGB') for v in result['variants']]
        labels = ['Исходный HD '+s['provenance']['namespace']+':'+str(s['provenance']['slot']) for s in group['sources']]
        labels += ['Сгенерирован\n'+v['id'] for v in result['variants']]
        bank = originals+generated
        columns = min(6, len(bank))
        rows = (len(bank)+columns-1)//columns
        shortage_lines = textwrap.wrap(result.get('shortfallReason', ''), max(35, columns*24))
        header = 72+24*len(shortage_lines)
        sheet = Image.new('RGB', (max(720, columns*312+16), rows*332+header), '#25282d')
        draw = ImageDraw.Draw(sheet)
        draw.text((16, 12), group['key'], font=title, fill='white')
        if result.get('shortfall'):
            draw.multiline_text((16, 44), '\n'.join(shortage_lines), font=font, fill='#ffcb8a')
        for i, (image, label) in enumerate(zip(bank, labels)):
            x, y = 16+(i%columns)*312, header+(i//columns)*332
            sheet.paste(image, (x, y))
            lines = '\n'.join('\n'.join(textwrap.wrap(part, 31)) for part in label.splitlines())
            draw.multiline_text((x, y+260), lines, font=font, fill='white', spacing=3)
        sheet.save(output/(name+'-variants.png'))
        rng = random.Random(143)
        choices = [rng.random() for _ in range(144)]
        turns = [rng.randrange(4) for _ in range(144)]
        panels = []
        for pool in (originals, bank):
            panel = Image.new('RGB', (2688, 2688))
            for i, (choice, turn) in enumerate(zip(choices, turns)):
                tile = pool[min(len(pool)-1, int(choice*len(pool)))].crop((16,16,240,240)).rotate(turn*90)
                panel.paste(tile, ((i%12)*224, (i//12)*224))
            panels.append(panel)
        paint = Image.new('RGB', (5416, 2744), '#25282d')
        paint.paste(panels[0], (12, 44));paint.paste(panels[1], (2716, 44))
        draw = ImageDraw.Draw(paint)
        draw.text((12, 8), 'Сейчас: исходные HD', font=title, fill='white')
        draw.text((2716, 8), 'С вариантами (CPU, 12 × 12)', font=title, fill='white')
        paint.save(output/(name+'-paint.png'))
        paint.resize((1516,768), Image.Resampling.LANCZOS).save(output/(name+'-preview.png'))
        status = result['shortfallReason'] if result.get('shortfall') else 'Фильтры пройдены; целевое число вариантов достигнуто'
        index += [f'<section id="g{name}"><h2>{html.escape(group["key"])}</h2>',
                  f'<p class="{"fail" if result.get("shortfall") else ""}">{status}; исходных {len(originals)}, новых {len(generated)}, отклонено {result["rejected"]}.</p>',
                  f'<a href="{name}-variants.png"><img loading="lazy" src="{name}-variants.png" alt="Тайлы и ID"></a>',
                  f'<p><a href="{name}-paint.png">Поле 12×12, полный размер</a></p>',
                  f'<a href="{name}-paint.png"><img loading="lazy" src="{name}-preview.png" alt="Сравнение полей"></a></section>']
    (output/'index.html').write_text('\n'.join(index), encoding='utf-8')
    print(json.dumps(dict(groups=len(results['groups']), pngs=len(list(output.glob('*.png'))),
                          bytes=sum(p.stat().st_size for p in output.iterdir()))))


if __name__ == '__main__':
    main()
