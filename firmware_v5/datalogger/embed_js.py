import pathlib
p = pathlib.Path(__file__).with_name("portal_script_body.js")
s = p.read_text(encoding="utf-8").replace("\r\n", "\n")
esc = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
# Separate C string for /portal.js — keeps / HTML small enough for HTTP_BUFFER_SIZE.
# After editing portal_script_body.js, run: python embed_js.py && python patch_portal_cpp.py
# Escaped size must stay below httpd ARDUINO HTTP_BUFFER_SIZE (see Arduino libraries httpd/httpd.h).
block = (
    "// PORTAL_JS_BEGIN\n"
    "static const char PORTAL_JS[] =\n"
    '    "' + esc + '";\n'
    "// PORTAL_JS_END\n"
)
pathlib.Path(__file__).with_name("portal_js_embed.txt").write_text(block, encoding="utf-8")
print("chars", len(s), "escaped_len", len(esc))
