#!/usr/bin/env python3
"""Turn doc/manual.md into HTML for printing: the Markdown subset the
manual uses (headings, paragraphs, lists, tables, indented code, inline
code, bold, links, images). Usage: md2html.py manual.md > manual.html"""
import sys, re, html

def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r'`([^`]+)`', r'<code>\1</code>', t)
    t = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', t)
    t = re.sub(r'\*([^*]+)\*', r'<i>\1</i>', t)
    t = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', r'<img src="\2" alt="\1">', t)
    t = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', t)
    return t

def slug(t):
    return re.sub(r'[^a-z0-9]+', '-', t.lower()).strip('-')

lines = open(sys.argv[1], encoding='utf-8').read().split('\n')
out = []
i = 0
para = []
def flush():
    global para
    if para:
        out.append('<p>' + inline(' '.join(para)) + '</p>')
        para = []
while i < len(lines):
    l = lines[i]
    if l.startswith('#'):
        flush()
        n = len(l) - len(l.lstrip('#'))
        t = l[n:].strip()
        out.append('<h%d id="%s">%s</h%d>' % (n, slug(t), inline(t), n))
        i += 1
    elif l.startswith('    ') and not para:
        flush()
        block = []
        while i < len(lines) and (lines[i].startswith('    ') or lines[i].strip() == ''):
            if lines[i].strip() == '' and not (i + 1 < len(lines) and lines[i + 1].startswith('    ')): break
            block.append(lines[i][4:]); i += 1
        out.append('<pre>' + html.escape('\n'.join(block)) + '</pre>')
    elif l.startswith('|'):
        flush()
        rows = []
        while i < len(lines) and lines[i].startswith('|'):
            cells = [c.strip() for c in lines[i].strip().strip('|').split('|')]
            if not all(re.fullmatch(r'-+', c) for c in cells): rows.append(cells)
            i += 1
        out.append('<table>')
        for k, r in enumerate(rows):
            tag = 'th' if k == 0 else 'td'
            out.append('<tr>' + ''.join('<%s>%s</%s>' % (tag, inline(c), tag) for c in r) + '</tr>')
        out.append('</table>')
    elif re.match(r'^(\d+\.|-) ', l):
        flush()
        ordered = l[0].isdigit()
        out.append('<ol>' if ordered else '<ul>')
        while i < len(lines) and re.match(r'^(\d+\.|-) ', lines[i]):
            item = re.sub(r'^(\d+\.|-) ', '', lines[i]); i += 1
            while i < len(lines) and lines[i].startswith('  ') and lines[i].strip():
                item += ' ' + lines[i].strip(); i += 1
            out.append('<li>' + inline(item) + '</li>')
        out.append('</ol>' if ordered else '</ul>')
    elif l.strip() == '':
        flush(); i += 1
    else:
        para.append(l.strip()); i += 1
flush()
css = """
body { font-family: 'DejaVu Sans', Arial, sans-serif; font-size: 10.5pt; line-height: 1.4; max-width: 17cm; margin: 0 auto; color: #111; }
h1 { font-size: 22pt; border-bottom: 2px solid #345; padding-bottom: 4px; }
h2 { font-size: 15pt; margin-top: 1.6em; border-bottom: 1px solid #aab; page-break-after: avoid; }
code, pre { font-family: 'DejaVu Sans Mono', monospace; font-size: 9.5pt; }
code { background: #eef; padding: 0 3px; border-radius: 3px; }
pre { background: #eef; padding: 8px 10px; border-radius: 4px; white-space: pre-wrap; }
table { border-collapse: collapse; margin: 0.6em 0; font-size: 9.5pt; page-break-inside: avoid; }
th, td { border: 1px solid #bbc; padding: 3px 7px; text-align: left; vertical-align: top; }
th { background: #dde; }
img { max-width: 100%; border: 1px solid #889; margin: 0.4em 0; page-break-inside: avoid; }
a { color: #246; }
li { margin: 0.2em 0; }
@page { size: A4; margin: 18mm 16mm; }
"""
print('<!DOCTYPE html><html><head><meta charset="utf-8"><title>FTA-850 programmer: user manual</title><style>%s</style></head><body>%s</body></html>' % (css, '\n'.join(out)))
