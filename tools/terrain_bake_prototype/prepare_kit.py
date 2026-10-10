"""Generate private baseline copies, page witnesses and owner instructions."""
import argparse
import csv
from pathlib import Path
import shutil
import struct
import sys
sys.dont_write_bytecode=True
import numpy as np
from PIL import Image,ImageDraw
from research import read,write
from verify_stage1 import archive,sha


def run(root):
    assert root.name=='build-tests-terrain-bake-stage-1'
    cfg=read(root/'memory-config.json');fixtures=read(root/'memory-kit'/'fixtures.json')
    baseline=root/'memory-kit'/'baseline';baseline.mkdir(exist_ok=True)
    for field,name in (('sourceMpr','zone6.mpr'),('sourceMob','zone6.mob')):
        shutil.copyfile(cfg[field],baseline/name)
    write(baseline/'manifest.json',{'mprSha256':sha(baseline/'zone6.mpr'),'mobSha256':sha(baseline/'zone6.mob'),'atlas':'original stock resources in an isolated game/mod copy','slots':fixtures['sourceSlots']})
    rows=[]
    for item in fixtures['fixtures']:
        fid=item['id'];folder=root/'memory-kit'/fid;members=archive(folder/(fid+'.mpr'))
        mp=members[fid+'.mp'];sx,sy=struct.unpack_from('<II',mp,8);w,h=sx*16,sy*16
        pages=np.zeros((h,w),np.uint8);witness={}
        for name,b in members.items():
            if not name.endswith('.sec'):continue
            tail=name[len(fid):-4];x,y=int(tail[:3])*16,int(tail[3:])*16
            off=5+33*33*8*(2 if b[4]==3 else 1)
            slots=struct.unpack_from('<256H',b,off)
            overlay=struct.unpack_from('<256h',b,off+1024) if b[4]==3 else [-1]*256
            for i,p in enumerate(slots):
                xx,yy=x+i%16,y+i//16;page=(p&0x3fff)//64;pages[yy,xx]=page
                record={'page':page,'slot':p&0x3fff,'gridX':xx,'gridY':yy,'worldXCenter':xx*2+1,'worldYCenter':yy*2+1,'overlayMaterial':overlay[i]}
                if page not in witness or (witness[page]['overlayMaterial']>=0 and overlay[i]<0):witness[page]=record
        with (folder/'page-witnesses.csv').open('w',encoding='utf-8',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=list(next(iter(witness.values()))));writer.writeheader();writer.writerows(witness[k] for k in sorted(witness))
        assert len(witness)==item['pages']
        colors=np.array([[(i*73+31)%256,(i*109+51)%256,(i*149+77)%256] for i in range(64)],np.uint8)
        im=Image.fromarray(colors[pages][::-1]).resize((w*4,h*4),Image.Resampling.NEAREST)
        im.save(folder/'page-coverage.png')
        rows.append(f"|{fid}|{item['pages']}|{item['referencedSlots']}|{item['payloadAndMmpHeadersBytes']/2**20:.3f}|")
    text='''# Игровой тест памяти — только подготовка, EI-TERRAIN-BAKE-STAGE-1

Ничего не установлено и не запускалось. Этот комплект содержит приватные
игровые данные: не включать в Git, публикацию или распространяемый ZIP.
Установка и запуск — только владельцем после отдельного разрешения.

## Что внутри

Baseline — точные Zone6 MPR/MOB; исходные атласы берутся из копии штатной DejaVu.
Четыре tb1_* каталога — изолированные внутренние MP/SEC/MMP namespaces,
MPR, byte-identical MOB, RES, manifest и page-witnesses.csv. Первые 1216 мест
сохраняют stock-индексы; добавленные места копируют конкретные неанимированные
клетки, сохраняя их класс и поворот. Все новые места и все страницы имеют SEC
ссылки. Ссылки land и active overlay считаются совместно; часть исходных
физических мест остаётся запасом stock-атласа, а не недостающими страницами.
Нет новой покраски, новых классов или AI.
256px — увеличение штатных 128px, тест размера/памяти, не дополнительная детализация.
У 128px первые 19 MMP страниц byte-identical оригиналу; прочие страницы и все
256px страницы закодированы обычным DXT1 codec с полной цепочкой mip до 1px.

|Набор|Страниц|Слотов со ссылками|MMP payload + headers, МиБ|
|---|---:|---:|---:|
'''+ '\n'.join(rows)+'''

## Куда копировать после разрешения

Работать с отдельной полной тестовой копией игры/мода, обозначенной
`<TEST_GAME_ROOT>`. Не использовать C:/GOG Games/PZ и рабочую установку маппера.
Если тестовой копии ещё нет, владелец сначала создаёт и настраивает её обычным
для своей установки способом. Этот комплект не содержит установщика/автозамены.

Для одного выбранного tb1_*:
1. Сохранить резервную копию тестовых MODS/DejaVu/Maps/zone6.mpr и config.ini.
2. Скопировать tb1_*.mpr как `<TEST_GAME_ROOT>/MODS/DejaVu/Maps/zone6.mpr`.
   Сохранить штатный zone6.mob либо взять byte-identical tb1_*.mob как zone6.mob.
   Внешнее имя zone6 сохраняет штатный сценарий; внутренний atlas namespace новый.
3. Скопировать tb1_*-atlas.res в `<TEST_GAME_ROOT>/MODS/DejaVu/res/`.
4. В [RES] тестового MODS/DejaVu/config.ini добавить одну строку
   `textures.res=res\\tb1_2048_128-atlas.res` (имя соответствует выбранному набору).
   В текущем stock config эта форма textures.res=res\\... используется штатно.
   RES не заменяет оригинальные texture archives: новые имена MMP не пересекаются.
5. Запускать обычный Starter настроенной тестовой копии, выбрать её DejaVu.
   Войти в Zone6 штатным сценарием/переходом либо установленным у владельца
   способом тестового старта. Не загружать сохранение с уже загруженной Zone6:
   сохранённое состояние может подменить подготовленный MPR. Универсальная
   CLI-команда принудительного старта DejaVu здесь не подтверждена и не придумана.

## Последовательность и память

Свежий процесс для каждого случая: baseline → 2048×128 → 4096×128.
После успешной 128px приёмки отдельно 2048×256; 4096×256 — необязательный
стресс-тест, прекращать при ошибке/нехватке памяти. Не переходить на 8192.
4096×256 только одна RGBA8+mip копия стоит около 1365.3 МиБ, поэтому риск
32-битного VA существенно выше. Новый процесс обязателен: старые caches и
residency не должны скрыть разницу. Зафиксировать реальные EXE/DLL и renderer.

Монитор запускать из 64-битного PowerShell, когда известен PID собственного
32-битного игрового процесса, до загрузки Zone6 (например, из главного меню):

```powershell
cd 'C:\\GOG Games\\maped\\ei-mapper-ataman'
.\\tools\\terrain_bake_prototype\\sample_memory.ps1 -ProcessId <PID> `
  -OutputDirectory '.\\build-tests-terrain-bake-stage-1\\memory-kit\\measurements\\2048-128-run1' `
  -DurationSeconds 300 -IntervalMilliseconds 500
```

Монитор не запускает/останавливает игру и не пишет в неё. process.json хранит
PID/path/start/EXE hash; memory.jsonl — рабочий набор, private bytes, committed/
reserved/free VA, largest free region и фактически просканированный предел VA.
summary.json содержит пики выборки, не гарантированный пик между 500ms отсчётами.
Синтаксис PowerShell и C# проверены; реальные API измерения игры ещё не выполнялись.
Поставить собственные UTC метки начала загрузки / player-on-map / конца обхода.
Сохранять сообщения об ошибках, снимки, native dumps и журналы тестовой копии.

## Что наблюдать и что считается проверкой

Загрузка: время до управляемого героя на Zone6, отсутствие CRT/crash/дампа,
отзывчивость не менее 60s. Главное меню или красивая картинка не являются PASS.
Память: пики private/working set/committed VA и минимум largest free region
сравнить с baseline на тех же EXE/renderer/settings и времени обхода.

Все страницы есть в RES и имеют SEC ссылки, но движок может читать их лениво.
page-witnesses.csv даёт по одной клетке каждой страницы (grid и world center);
page-coverage.png показывает размещение страниц. Осмотреть все свидетели и
обойти карту с разными масштабами камеры. Лучше получить лог фактической
загрузки каждой MMP либо использовать отдельно разрешённый preloader. Без этого
отмечать «видимый workload», а не «доказана полная residency всех страниц».

Артефакты: переходы/рёбра клеток/секторов/страниц, диагонали, missing textures,
далекая камера/mip shimmer/bleeding, близкая камера/gutter. 256px resampling и
другая группировка дополнительных страниц могут менять дальние mip: записать это.
Overlay: вода, берега, прозрачность/дыры и анимация. Классы: пройти по тем же
земле/камню/берегу, сравнить звук/эффекты/проходимость с baseline; тип каждого
SEC предшественника технически сохранён, игровое поведение ещё не проверено.
Не менять AI/геометрию или сценарий ради успешного теста памяти.

Завершение: остановить свой игровой процесс обычным выходом, сохранить evidence.
В тестовой копии восстановить zone6.mpr/config.ini из backup и убрать только
добавленный RES. Живую игру, её сохранения и постоянный маппер не изменять.
'''
    (root/'memory-kit'/'README.md').write_text(text,encoding='utf-8')
    print('PASS: private baseline, page witnesses, coverage and owner instructions')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();run(a.root.resolve())
