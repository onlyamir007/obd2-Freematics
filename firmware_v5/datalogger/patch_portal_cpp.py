import pathlib
root = pathlib.Path(__file__).parent
wp_path = root / "wifi_portal.cpp"
wp = wp_path.read_text(encoding="utf-8")
emb = (root / "portal_js_embed.txt").read_text(encoding="utf-8")
begin = wp.find("// PORTAL_JS_BEGIN")
end = wp.find("// PORTAL_JS_END", begin)
if begin < 0 or end < 0:
    raise SystemExit("PORTAL_JS markers not found in wifi_portal.cpp")
end += len("// PORTAL_JS_END")
# skip one newline after marker so we do not accumulate blank lines
while end < len(wp) and wp[end] in "\r\n":
    end += 1
wp = wp[:begin] + emb + wp[end:]

# Inline <script> body would exceed handler buffer; use external /portal.js
old_tail = '    "<div id=log></div>"\n    "<script>"'
tail_pos = wp.find(old_tail)
if tail_pos >= 0:
    tail_end = wp.find('    "</script></body></html>";', tail_pos)
    if tail_end < 0:
        raise SystemExit("inline </script> tail not found")
    tail_end += len('    "</script></body></html>";')
    new_tail = (
        '    "<div id=log></div>"\n'
        '    "<script src=/portal.js defer></script></body></html>";'
    )
    wp = wp[:tail_pos] + new_tail + wp[tail_end:]

wp_path.write_text(wp, encoding="utf-8")
print("patched ok portal_js", len(emb))
