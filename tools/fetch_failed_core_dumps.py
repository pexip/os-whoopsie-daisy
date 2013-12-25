#!/usr/bin/python

# To test Snappy against gzip, lzma, LZO, etc.
from launchpadlib.launchpad import Launchpad
import sys
import os
from gzip import GzipFile

try:
    import cStringIO
    StringIO = cStringIO.StringIO
except:
    from StringIO import StringIO

if len(sys.argv) < 2:
    print >>sys.stderr, 'usage: %s <path>' % sys.argv[0]
    sys.exit(1)

core_dumps = sys.argv[1]
if not os.path.exists(core_dumps):
    os.makedirs(core_dumps)

lp = Launchpad.login_anonymously('get failed retrace bugs', 'production')
ubuntu = lp.distributions['ubuntu']
for bug in ubuntu.searchTasks(tags='apport-failed-retrace'):
    bug = lp.bugs[bug.bug_link]
    for attachment in bug.attachments:
        if not attachment.title == 'CoreDump.gz':
            continue

        fp = None
        try:
            fp = attachment.data.open()
            with open('%s/%s' % (core_dumps, bug.id), 'wb') as out:
                gz = None
                sio = None
                try:
                    sio = StringIO(fp.read())
                    gz = GzipFile(fileobj=sio)
                    while 1:
                        buf = gz.read(1024)
                        if buf:
                            out.write(buf)
                        else:
                            break
                except MemoryError:
                    print >>sys.stderr, "Memory error on %s" % bug.id
                finally:
                    gz.close()
                    sio.close()
        finally:
            fp.close()
