#!/usr/bin/env python3
"""
Make a single-file OpenWS3 web tool with a firmware image inside, ready to hand to users:

  bundle.py build/fw.bin tools/openws3.html build/openws3.html      (make does this)

The page then installs that image with one button and shows its version (as the firmware reports it over USB), build date, size and
SHA-256; the Advanced view still lets users pick another .bin.
"""
import base64, datetime, hashlib, json, os, re, sys

fw, page, out = sys.argv[1:4]
# the id the firmware reports over USB: src/version.h (release) and build/version_build.h (commit, tree had changes)
num = lambda path, name: int(re.search(r'#define %s\s+(\w+)' % name, open(path).read()).group(1).rstrip('UL'), 0)
build_h = os.path.join(os.path.dirname(fw), 'version_build.h')
image = open(fw, 'rb').read().rstrip(b'\xff')      # the flash padding is not part of the image
meta = dict(major=num('src/version.h', 'FW_MAJOR'), minor=num('src/version.h', 'FW_MINOR'), pre=num('src/version.h', 'FW_PRE'), build=num(build_h, 'FW_BUILD'),
            dirty=num(build_h, 'FW_DIRTY'), built=datetime.date.today().isoformat(), size=len(image), sha256=hashlib.sha256(image).hexdigest())
html = open(page).read()
stub = 'const BUNDLED = null;'
assert html.count(stub) == 1, 'placeholder not found in ' + page
open(out, 'w').write(html.replace(stub, 'const BUNDLED = %s;' % json.dumps(dict(meta, data=base64.b64encode(open(fw, 'rb').read()).decode()))))
print('%s: OpenWS3 %d.%d%s build %08x%s, %d bytes' % (out, meta['major'], meta['minor'], '-beta.%d' % meta['pre'] if meta['pre'] else '', meta['build'], '+' if meta['dirty'] else '', meta['size']))
