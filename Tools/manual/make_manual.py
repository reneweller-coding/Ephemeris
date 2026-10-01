"""Ephemeris -- build the user manual out of the program itself (after Phosphene's Tools/manual).

    for each tab N:  EPH_SEED=4242 EPH_PLAY=1 EPH_SHOT_AT=400 EPH_TAB=N EPH_SHOT=docs\\screenshots\\tab_NN.png Ephemeris.exe
    python Tools/manual/make_manual.py        -> docs/manual/Ephemeris-Manual.html (and .pdf)

The parameter tables are not written down here: `eph_render --dump-params` writes every entry of the
engine's descriptor tables (key, name, unit, range, default, curve, choices), and this script prints
them under the chapter whose tab shows them. The only hand-written part is `chapters.txt`, the prose that
says why a thing is the way it is.

That buys a check worth more than the text: a module whose parameters belong to no chapter is on no page
of the manual, and the generator refuses to print until it is placed (--allow-holes prints anyway).

The PDF is printed by Edge or Chrome in headless mode (`--headless=new`); without a browser the HTML is
still written and prints by hand.
"""
import argparse
import html
import json
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

CSS = """
@page { size: A4; margin: 17mm 15mm 15mm 15mm; }
body { font: 10.5pt/1.55 "Segoe UI", "Helvetica Neue", Arial, sans-serif; color: #17181c; background: #fff; margin: 0; }
h1 { font-size: 30pt; margin: 0 0 2mm 0; letter-spacing: 1pt; color: #8a6a2c; }
h2 { font-size: 16pt; margin: 0 0 3mm 0; padding-bottom: 2mm; border-bottom: 1px solid #d8d2c4; color: #8a6a2c; }
p { margin: 0 0 3.2mm 0; text-align: justify; hyphens: auto; }
pre { font: 9.5pt Consolas, monospace; background: #f5f2ec; padding: 2mm 3mm; }
.cover { page-break-after: always; text-align: center; padding-top: 24mm; }
.cover img, .shot { width: 100%; border: 1px solid #d8d2c4; border-radius: 3px; }
/* A page taller than a sheet (the synths' pages with all their modulation) is shown whole, narrower. */
.shot { width: auto; max-width: 100%; max-height: 255mm; display: block; margin-left: auto; margin-right: auto; }
figure { margin: 2mm 0 5mm 0; page-break-inside: avoid; }
figure img { width: 100%; border-radius: 3px; }
figcaption { color: #5b5f68; font-size: 9pt; margin-top: 1.5mm; }
.cover img.logo { width: 34mm; border: none; margin: 0 auto 6mm auto; display: block; }
.cover p { text-align: center; }
.cover img { margin-top: 10mm; }
.sub { color: #5b5f68; font-size: 12pt; margin: 0; }
.facts { margin-top: 7mm; color: #5b5f68; font-size: 9.5pt; }
.toc { page-break-after: always; }
.toc li { margin: 1.2mm 0; }
.chapter { page-break-before: always; }
.shot { margin: 2mm 0 4mm 0; }
table { border-collapse: collapse; width: 100%; font-size: 8.8pt; margin: 2mm 0 5mm 0; }
th { text-align: left; color: #5b5f68; font-weight: 600; border-bottom: 1px solid #b9b2a2; padding: 1mm 1.5mm; }
td { border-bottom: 1px solid #ebe6dc; padding: 0.8mm 1.5mm; vertical-align: top; }
td.key { font-family: Consolas, monospace; font-size: 8.3pt; color: #5b5f68; }
h3 { font-size: 10.5pt; margin: 5mm 0 1mm 0; color: #8a6a2c; letter-spacing: 0.6pt; text-transform: uppercase; }
"""


def read_chapters(path):
    """The chapters: title, module prefixes, screenshot index (or None), paragraphs."""
    chapters, cur = [], None
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    for line in lines:
        if line.startswith("#"):
            continue
        m = re.match(r"^==\s*(.+?)\s*\|\s*(.*?)\s*\|\s*(\S+)\s*==\s*$", line)
        if m:
            cur = {"title": m.group(1), "modules": m.group(2).split(), "shot": None if m.group(3) == "-" else int(m.group(3)),
                   "blocks": []}
            chapters.append(cur)
            continue
        if cur is None:
            continue
        cur["blocks"].append(line)
    for c in chapters:
        paragraphs, para, pre = [], [], []
        for line in c["blocks"] + [""]:
            m = re.match(r"^\[image\s+(\S+)(?:\s*:\s*(.*))?\]$", line.strip())
            if m and not para and not pre:
                paragraphs.append(("img", m.group(1), m.group(2) or ""))
                continue
            if line.startswith("    ") and not para:
                pre.append(line[4:])
                continue
            if pre:
                paragraphs.append(("pre", "\n".join(pre)))
                pre = []
            if line.strip():
                para.append(line.strip())
            elif para:
                paragraphs.append(("p", " ".join(para)))
                para = []
        c["paragraphs"] = paragraphs
    return chapters


def module_of(key):
    """'row3.length' -> ('row', 3, 'length'); 'master.level' -> ('master', 1, 'level')."""
    head, _, param = key.partition(".")
    m = re.match(r"^(.*?)(\d+)$", head)
    return (m.group(1), int(m.group(2)), param) if m else (head, 1, param)


def fmt(x):
    return ("%g" % x) if abs(x) < 1e5 else ("%.0f" % x)


def value_text(p, v):
    if p["curve"] == "choice" and p["choices"]:
        i = int(round(v))
        return p["choices"][i] if 0 <= i < len(p["choices"]) else fmt(v)
    if p["curve"] == "toggle":
        return "on" if v >= 0.5 else "off"
    return (fmt(v) + (" " + p["unit"] if p["unit"] else "")).strip()


def table(prefix, params, instances):
    """One module's parameters as a table (the first instance's defaults where instances differ)."""
    rows = []
    for p in params:
        if p["curve"] == "choice" and p["choices"]:
            rng = ", ".join(p["choices"])
        elif p["curve"] == "toggle":
            rng = "off / on"
        else:
            rng = "%s .. %s%s" % (fmt(p["min"]), fmt(p["max"]), (" " + p["unit"]) if p["unit"] else "")
        key = "%s%s.%s" % (prefix, "N" if instances > 1 else "", module_of(p["key"])[2])
        rows.append("<tr><td>%s</td><td class='key'>%s</td><td>%s</td><td>%s</td></tr>"
                    % (html.escape(p["name"]), html.escape(key), html.escape(rng), html.escape(value_text(p, p["default"]))))
    title = prefix + (" (%d instances)" % instances if instances > 1 else "")
    return ("<h3>%s</h3><table><tr><th>Control</th><th>Key</th><th>Range</th><th>Default</th></tr>%s</table>"
            % (html.escape(title), "".join(rows)))


def version():
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        m = re.search(r"project\(\s*\w+\s+VERSION\s+([0-9.]+)", f.read())
    return m.group(1) if m else "?"


def find_browser():
    for p in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Google\Chrome\Application\chrome.exe"):
        if os.path.exists(p):
            return p
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--render", default=os.path.join(ROOT, "bin", "msvc", "eph_render.exe"))
    ap.add_argument("--params", help="a --dump-params JSON instead of running eph_render")
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "manual", "Ephemeris-Manual.html"))
    ap.add_argument("--no-pdf", action="store_true")
    ap.add_argument("--allow-holes", action="store_true")
    a = ap.parse_args()

    path = a.params
    if path is None:
        path = os.path.join(tempfile.gettempdir(), "eph_params.json")
        subprocess.run([a.render, "--dump-params", path], check=True)
    with open(path, encoding="utf-8") as f:
        params = json.load(f)
    chapters = read_chapters(os.path.join(HERE, "chapters.txt"))

    # The modules: their parameters (first instance) and instance counts, in the tables' order.
    modules, order = {}, []
    for p in params:
        prefix, inst, _ = module_of(p["key"])
        if prefix not in modules:
            modules[prefix] = {"params": [], "instances": 0}
            order.append(prefix)
        m = modules[prefix]
        m["instances"] = max(m["instances"], inst)
        if inst == 1:
            m["params"].append(p)
    placed = {x for c in chapters for x in c["modules"]}
    holes = [m for m in order if m not in placed]
    unknown = sorted(placed - set(order))
    if unknown:
        print("chapters name modules that do not exist: " + ", ".join(unknown))
        return 1
    if holes:
        print("modules on no page of the manual: " + ", ".join(holes))
        if not a.allow_holes:
            return 1

    out_dir = os.path.dirname(a.out)
    os.makedirs(out_dir, exist_ok=True)
    shots = os.path.relpath(os.path.join(ROOT, "docs", "screenshots"), out_dir).replace("\\", "/")
    parts = ["<!DOCTYPE html><html><head><meta charset='utf-8'><title>Ephemeris Manual</title><style>%s</style></head><body>" % CSS]
    logo = os.path.relpath(os.path.join(ROOT, "Deploy", "ephemeris_512.png"), out_dir).replace("\\", "/")
    parts.append("<div class='cover'><img class='logo' src='%s'><h1>EPHEMERIS</h1><p class='sub'>A generator of Berlin School music</p>"
                 "<p class='facts'>Version %s &middot; %d parameters &middot; manual built %s</p>"
                 "<img src='%s/tab_00.png'></div>" % (logo, version(), len(params), time.strftime("%d.%m.%Y"), shots))
    parts.append("<div class='toc'><h2>Contents</h2><ol>%s</ol></div>"
                 % "".join("<li><a href='#c%d'>%s</a></li>" % (i, html.escape(c["title"])) for i, c in enumerate(chapters)))
    for i, c in enumerate(chapters):
        parts.append("<div class='chapter' id='c%d'><h2>%s</h2>" % (i, html.escape(c["title"])))
        if c["shot"] is not None:
            parts.append("<img class='shot' src='%s/tab_%02d.png'>" % (shots, c["shot"]))
        for block in c["paragraphs"]:
            kind, text = block[0], block[1]
            if kind == "img":
                src = os.path.relpath(os.path.join(ROOT, "docs", text), out_dir).replace("\\", "/")
                parts.append("<figure><img src='%s'><figcaption>%s</figcaption></figure>" % (src, html.escape(block[2])))
            else:
                parts.append("<pre>%s</pre>" % html.escape(text) if kind == "pre" else "<p>%s</p>" % html.escape(text))
        for prefix in c["modules"]:
            parts.append(table(prefix, modules[prefix]["params"], modules[prefix]["instances"]))
        parts.append("</div>")
    parts.append("</body></html>")
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))
    print("wrote " + a.out)

    if not a.no_pdf:
        browser = find_browser()
        if browser is None:
            print("no Edge or Chrome: the HTML prints by hand")
            return 0
        pdf = os.path.splitext(a.out)[0] + ".pdf"
        url = "file:///" + os.path.abspath(a.out).replace("\\", "/")
        # The old one goes first, so that a PDF there is this one. The browser returns before its child process has
        # written the file: it is waited for until its size holds. A profile of its own: otherwise the call hands the
        # job to a running browser and returns without a file.
        if os.path.exists(pdf):
            os.remove(pdf)
        profile = tempfile.mkdtemp(prefix="eph_manual_")
        subprocess.run([browser, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", "--user-data-dir=" + profile,
                        "--print-to-pdf=" + pdf, url], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
        size = -1
        for _ in range(120):
            now = os.path.getsize(pdf) if os.path.exists(pdf) else -1
            if now > 0 and now == size:
                break
            size = now
            time.sleep(0.5)
        print(("wrote " + pdf) if os.path.exists(pdf) else "the browser wrote no PDF")
    return 0


if __name__ == "__main__":
    sys.exit(main())
