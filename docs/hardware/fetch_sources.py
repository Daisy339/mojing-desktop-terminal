from pathlib import Path
from urllib.request import urlopen
from urllib.parse import urljoin
import json
from bs4 import BeautifulSoup

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / 'assets'
BASE = 'https://docs.waveshare.net/ESP32-S3-RLCD-4.2'
manifest = []
page = BeautifulSoup((ASSETS / 'official.html').read_text(encoding='utf-8'), 'html.parser')
for im in (page.select_one('main') or page).find_all('img'):
    src = urljoin(BASE, im['src'])
    name = src.rsplit('/', 1)[-1]
    dest = ASSETS / name
    if not dest.exists():
        dest.write_bytes(urlopen(src, timeout=30).read())
    manifest.append({'caption': im.get('alt', ''), 'source': src, 'local': name})
for section in ['Resources-And-Documents', 'ESP-IDF']:
    url = BASE + '/' + section
    dest = ASSETS / (section + '.html')
    if not dest.exists():
        dest.write_bytes(urlopen(url, timeout=30).read())
    doc = BeautifulSoup(dest.read_text(encoding='utf-8'), 'html.parser')
    main = doc.select_one('main') or doc
    (ASSETS / (section + '.txt')).write_text(main.get_text('\n', strip=True), encoding='utf-8')
    print(section)
    for a in main.select('a[href]'):
        if any(t in a['href'].lower() for t in ['.pdf', '.zip', 'github']):
            print(a.text, urljoin(url, a['href']))
(ASSETS / 'sources.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
