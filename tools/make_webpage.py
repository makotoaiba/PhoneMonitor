# web/page.html から Source/WebPage.h を生成する（MSVCの文字列リテラル長制限対策で分割）
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent
html = (root / "web" / "page.html").read_text(encoding="utf-8")
chunks, cur = [], ""
for line in html.splitlines(keepends=True):
    if len((cur + line).encode("utf-8")) > 12000:
        chunks.append(cur); cur = ""
    cur += line
chunks.append(cur)
out = ['#pragma once', '#include <string>', '// 自動生成: tools/make_webpage.py（web/page.html を編集して再生成）', 'namespace pm', '{', 'inline const std::string& getWebPage()', '{', '    static const std::string page = std::string()']
for c in chunks:
    out.append('        + std::string (u8R"PMHTML(' + c + ')PMHTML")'.replace('u8R', 'R'))
out[-1] += ';'
out += ['    return page;', '}', '} // namespace pm', '']
(root / "Source" / "WebPage.h").write_text("\n".join(out), encoding="utf-8")
print("chunks:", len(chunks))
