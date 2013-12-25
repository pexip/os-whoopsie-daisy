import sys
import apport
import bson
import simplejson

r = apport.Report()
r.load(open(sys.argv[1]))

del r['CoreDump']
r = dict(r)
print 'BSON: %sB' % len(bson.BSON.from_dict(r))
print 'JSON: %sB' % len(simplejson.dumps(r))
