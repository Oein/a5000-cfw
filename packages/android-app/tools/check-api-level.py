#!/usr/bin/env python3
"""Scan an APK's dex for framework classes/methods newer than the camera's API level.

Lint's NewApi check only covers our own sources; this also covers bundled libraries
(NanoHTTPD, OpenMemories-Framework), which would otherwise crash on the camera with
NoSuchMethodError / NoClassDefFoundError.

Resolves INHERITED members: a call site references a member on the static type at the
call (e.g. ImageView.setScaleX), but api-versions.xml declares it on the class that
defines it (View, since 11). We therefore walk the superclass/interface chain so that
inherited API>min methods/fields are caught (this is what let ImageView.setScaleX,
API 11, slip through before and hang the camera).

usage: check-api-level.py <apk> [min_api=10]
"""
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

sdk = os.environ.get('ANDROID_HOME') or os.path.expanduser('~/Library/Android/sdk')
apk = sys.argv[1]
min_api = int(sys.argv[2]) if len(sys.argv) > 2 else 10

build_tools = sorted(os.listdir(os.path.join(sdk, 'build-tools')))[-1]
platform = sorted(os.listdir(os.path.join(sdk, 'platforms')), key=lambda p: int(p.split('-')[-1]))[-1]
dexdump = os.path.join(sdk, 'build-tools', build_tools, 'dexdump')
api_xml = os.path.join(sdk, 'platforms', platform, 'data', 'api-versions.xml')

classes, supers, methods, fields = {}, {}, {}, {}
for c in ET.parse(api_xml).getroot().iter('class'):
    cname = c.get('name')
    since = int(c.get('since', '1'))
    classes[cname] = since
    supers[cname] = [e.get('name') for e in c if e.tag == 'extends' or e.tag == 'implements']
    for m in c.iter('method'):
        methods[(cname, m.get('name'))] = int(m.get('since', since))
    for f in c.iter('field'):
        fields[(cname, f.get('name'))] = int(f.get('since', since))


def resolve(cls, key, table):
    """since level of member `key` on `cls`, walking ancestors; None if not found."""
    seen = set()
    stack = [cls]
    while stack:
        c = stack.pop()
        if c in seen:
            continue
        seen.add(c)
        lvl = table.get((c, key))
        if lvl is not None:
            return lvl
        stack.extend(supers.get(c, ()))
    return None


# self-test: the exact miss that hung the camera must now be caught
_t = resolve('android/widget/ImageView', 'setScaleX(F)V', methods)
assert _t == 11, 'api-check self-test failed: inherited setScaleX resolved to %r (expected 11)' % _t

# dexdump emits dex string data raw; supplementary chars are CESU-8 (bytes 0xED..),
# which breaks strict UTF-8 decoding. Decode leniently.
out = subprocess.run([dexdump, '-d', apk], capture_output=True).stdout.decode('utf-8', 'replace')

# invoke-*/sget/iget lines end with e.g. "Ljava/nio/charset/StandardCharsets;.UTF_8:Ljava/nio/charset/Charset;"
# or "Ljava/lang/String;.isEmpty:()Z"
ref = re.compile(r'(?:invoke-\S+|[si]get\S*|[si]put\S*|new-instance|const-class|check-cast|instance-of)\s.*?\b(L[\w/$]+;)(?:\.([\w<>$]+):(\S+))?')
problems = {}
for line in out.splitlines():
    m = ref.search(line)
    if not m:
        continue
    cls = m.group(1)[1:-1]
    if not (cls.startswith('java/') or cls.startswith('javax/') or cls.startswith('android/')
            or cls.startswith('org/json/') or cls.startswith('org/xml/') or cls.startswith('org/w3c/')):
        continue
    if cls not in classes:
        problems[cls] = 'unknown class'
        continue
    level = classes[cls]
    what = cls
    name, sig = m.group(2), m.group(3)
    if name:
        is_method = sig and sig.startswith('(')
        key = name + sig if is_method else name
        resolved = resolve(cls, key, methods if is_method else fields)
        if resolved is not None:
            level = resolved
            what = cls + '.' + key
    if level > min_api:
        problems[what] = 'API %d' % level

for what, why in sorted(problems.items()):
    print('%-10s %s' % (why, what))
print('%d reference(s) above API %d' % (len(problems), min_api), file=sys.stderr)
sys.exit(1 if problems else 0)
