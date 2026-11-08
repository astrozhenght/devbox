"""Generate offline lunar facts from HKO Gregorian-Lunar conversion tables.

python Tools/generate_lunar_table.py --download --proxy http://127.0.0.1:10809
Without --download, only cached source files in Build/calendar_tests/hko are read.
Firmware has no Python, network, or host-calendar dependency.
"""
import argparse
import datetime as dt
import re
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'https://www.hko.gov.hk/en/gts/time/calendar/text/files/T{}e.txt'


def generate(download=False, proxy=None):
    cache = ROOT / 'Build/calendar_tests/hko'
    cache.mkdir(parents=True, exist_ok=True)

    def fetch(year):
        path = cache / f'T{year}e.txt'
        if not path.exists():
            if not download:
                raise RuntimeError(f'Missing HKO cache: {path}; use --download to fetch sources')
            handlers = [urllib.request.ProxyHandler({'https': proxy})] if proxy else []
            opener = urllib.request.build_opener(*handlers)
            with opener.open(SOURCE.format(year), timeout=30) as response:
                data = response.read()
            assert re.search(rf'(?m)^{year}/0?1/0?1\s'.encode(), data), f'Invalid HKO source: {year}'
            path.write_bytes(data)
        return path

    with ThreadPoolExecutor(max_workers=4) as pool:
        paths = list(pool.map(fetch, range(1969, 2101)))

    # Raw calendar cells are ASCII; legacy headers sometimes contain a non-UTF8 dash.
    starts = []
    oracle = []
    lunar_year = None
    lunar_month = 0
    lunar_day = 0
    is_leap = False
    previous = None
    for path in paths:
        rows = 0
        for line in path.read_bytes().decode('ascii', errors='replace').splitlines():
            match = re.match(r'^(\d+)/(\d+)/(\d+)\s+(.+)', line)
            if not match:
                continue
            year, month, day = map(int, match.group(1, 2, 3))
            gregorian = dt.date(year, month, day)
            if previous is not None:
                assert gregorian == previous + dt.timedelta(days=1), f'Date gap at {gregorian}'
            previous = gregorian
            value = match.group(4)
            boundary = re.match(r'(\d+)(?:st|nd|rd|th)\s+Lunar\s+Month', value, re.IGNORECASE)
            if boundary:
                actual_month = int(boundary.group(1))
                is_leap = actual_month == lunar_month
                if actual_month == 1 and not is_leap:
                    lunar_year = year
                if lunar_year is not None:
                    assert lunar_day in (29, 30) or not starts, f'Invalid month end at {gregorian}'
                lunar_month = actual_month
                lunar_day = 1
                if lunar_year is not None:
                    starts.append((gregorian, lunar_year, lunar_month, is_leap))
            else:
                actual_day = int(value.split()[0])
                if lunar_year is not None:
                    assert actual_day == lunar_day + 1, f'Invalid lunar sequence at {gregorian}'
                lunar_day = actual_day
            if 1970 <= year <= 2099:
                assert lunar_year is not None and 1 <= lunar_month <= 12 and 1 <= lunar_day <= 30
                oracle.append((year, month, day, lunar_year, lunar_month, lunar_day, int(is_leap)))
            rows += 1
        assert rows in (365, 366), f'Incomplete HKO year: {path.name}'

    lines = [
        '/* Generated calendar facts from Hong Kong Observatory conversion tables.',
        ' * https://www.hko.gov.hk/en/gts/time/conversion.htm',
        ' * Regenerate with Tools/generate_lunar_table.py. Lunar years: 1969..2099.',
        ' * Bits 0..3: leap month (0 if none); 4..16: chronological month lengths (1=30,0=29);',
        ' * Bits 17..22: Lunar New Year offset from Gregorian January 1.',
        ' * Future dates follow the HKO tables published at generation time. */',
        'static const uint32_t lunar_year_data[] =', '{',
    ]
    for year in range(1969, 2100):
        indices = [i for i, record in enumerate(starts) if record[1] == year]
        assert len(indices) in (12, 13), year
        first = starts[indices[0]][0]
        assert first.year == year and starts[indices[0]][2:] == (1, False)
        offset = (first - dt.date(year, 1, 1)).days
        assert 0 <= offset <= 63
        value = offset << 17
        expected_month = 1
        for ordinal, i in enumerate(indices, 1):
            begin, _, month, leap = starts[i]
            days = (starts[i + 1][0] - begin).days
            assert days in (29, 30)
            if leap:
                assert month == expected_month - 1 and not (value & 15)
                value |= month
            else:
                assert month == expected_month
                expected_month += 1
            if days == 30:
                value |= 1 << (ordinal + 3)
        assert expected_month == 13
        lines.append(f'    0x{value:06X}U, /* {year} */')
    lines.append('};')
    (ROOT / 'Common/Src/lunar_calendar_data.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    test_dir = ROOT / 'Tests/calendar'
    test_dir.mkdir(parents=True, exist_ok=True)
    assert len(oracle) == 47482
    with (test_dir / 'lunar_oracle.csv').open('w', encoding='utf-8', newline='') as out:
        out.write('year,month,day,lunar_year,lunar_month,lunar_day,is_leap\n')
        for row in oracle:
            out.write(','.join(map(str, row)) + '\n')
    print(f'Generated 131 compact year records and {len(oracle)} HKO daily oracle rows from 132 source tables')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--proxy')
    options = parser.parse_args()
    generate(options.download, options.proxy)
