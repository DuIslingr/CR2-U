#!/usr/bin/env python3
"""The game's text (`message\\*.dat`; EvtTextData, EvtWindowMessage::ReadPage in the Android library).

  header   u32 records' offset (0x10), u32 strings' offset, u32 records, u32 0
  records  0x20 bytes each - 8 u32 offsets into the strings, one per language (0 Japanese,
           1 English, ... - Cr3UtilOptionLanguage); messages are numbered from 1
  strings  UTF-8, NUL-terminated
  trailer  (optional) u32 voice ids, one per record, then 'voic', u32 count: the last 8 bytes

  In a message, backslash commands (ReadPage): \\f the page ends, \\n the next line, \\c the line is
  centred, \\#<digit> the line's colour (1 red, 2 (0, .8, 1), 3 green, 4 (1, .8, 0), 5 magenta,
  6 white, 7 black, else white), \\d<3 digits> the speaker tag (d001 ... d509: an index into the
  name data), \\w<digits> a wait, \\@ a wait mark (also put at each page's end), \\* the message
  ends, \\m a page flag, \\\\ a backslash.

  msg.py check <archive>                    every file parses, every string UTF-8; the commands seen
  msg.py show <archive> <file> <n> [lang]   one message's pages (for your terminal)
"""
import os
import re
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mvgl  # noqa: E402


class Text:
    def __init__(self, d):
        self.d = d
        self.records_at, self.strings_at, self.count = struct.unpack_from('<3I', d, 0)
        self.voices = []
        if len(d) >= 8 and d[-8:-4] == b'voic':
            n = struct.unpack_from('<I', d, len(d) - 4)[0]
            self.voices = list(struct.unpack_from(f'<{n}i', d, len(d) - 8 - 4 * n))

    def raw(self, n, lang=1):
        """message n (from 1) in a language, as bytes"""
        if not 1 <= n <= self.count:
            return None
        off = struct.unpack_from('<I', self.d, self.records_at + 0x20 * (n - 1) + 4 * lang)[0]
        p = self.strings_at + off
        return self.d[p:self.d.index(b'\0', p)]


def pages(text):
    """a message's pages: lists of (line, centred, colour), the speaker tag, and the waits"""
    out, page, line, centred, colour, speaker = [], [], '', False, 0, None
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == '\\' and i + 1 < len(text):
            c = text[i + 1]
            i += 2
            if c == 'n':
                page.append((line, centred, colour))
                line, centred, colour = '', False, 0
            elif c == 'f':
                page.append((line, centred, colour))
                out.append(page)
                page, line, centred, colour = [], '', False, 0
            elif c == 'c':
                centred = True
            elif c == '#' and i < len(text) and text[i].isdigit():
                colour = int(text[i])
                i += 1
            elif c == 'd':
                m = re.match(r'\d{1,3}', text[i:])
                if m:
                    speaker = 'd' + m.group(0)
                    i += len(m.group(0))
            elif c == 'w':
                m = re.match(r'\d+', text[i:])
                i += len(m.group(0)) if m else 0
            elif c == '*':
                break
            elif c == '\\':
                line += '\\'
            # (\@, \m: marks for the window)
            continue
        if ch not in '\r\n':
            line += ch
        i += 1
    if line or page:
        page.append((line, centred, colour))
    if page:
        out.append(page)
    return out, speaker


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    a = mvgl.Mvgl(argv[2])
    files = {n: f for n, *f in a.files()}
    if argv[1] == 'check':
        n = msgs = bad = voiced = 0
        cmds = Counter()
        speakers = Counter()
        for name, f in files.items():
            if not name.startswith('message\\'):
                continue
            t = Text(a.decoded(*f))
            n += 1
            voiced += bool(t.voices)
            for k in range(1, t.count + 1):
                for lang in range(8):
                    b = t.raw(k, lang)
                    try:
                        s = b.decode('utf-8')
                    except UnicodeDecodeError:
                        bad += 1
                        continue
                    msgs += 1
                    for m in re.findall(r'\\(.)', s):
                        cmds[m if not m.isdigit() else 'digit'] += 1
                    if lang == 1:
                        _, sp = pages(s)
                        speakers[sp] += 1
        print(f'{n} files ({voiced} with voice ids), {msgs} strings UTF-8, {bad} not; commands: '
              + ', '.join(f'\\{k} {v}' for k, v in cmds.most_common()))
        print('speaker tags (English):', ', '.join(f'{k} {v}' for k, v in speakers.most_common(8)), '...')
        return 0
    if argv[1] == 'show':
        name = argv[3] if argv[3].startswith('message\\') else 'message\\' + argv[3]
        name = name if name.endswith('.dat') else name + '.dat'
        t = Text(a.decoded(*files[name]))
        lang = int(argv[5]) if len(argv) > 5 else 1
        pg, sp = pages(t.raw(int(argv[4]), lang).decode('utf-8', 'replace'))
        print(f'speaker {sp}, voice {t.voices[int(argv[4]) - 1] if t.voices else None}')
        for i, p in enumerate(pg):
            print(f'-- page {i + 1}')
            for line, centred, colour in p:
                print(('  [centred] ' if centred else '  ') + (f'[colour {colour}] ' if colour else '') + line)
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
