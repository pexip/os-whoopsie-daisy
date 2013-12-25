#!/usr/bin/python

from launchpadlib.launchpad import Launchpad
import sys, os
if len(sys.argv) < 2:
    print >>sys.stderr, 'usage: %s <whoopsie version>' % sys.argv[0]
    sys.exit(1)
launchpad = Launchpad.login_anonymously('whoopsie bugs by version',
                'production', os.path.expanduser('~/.launchpadlib/cache'))

ubuntu = launchpad.projects['ubuntu']
whoopsie = ubuntu.getSourcePackage(name='whoopsie-daisy')
bugs = []
ver = sys.argv[1]
for bug in whoopsie.getBugTasks():
    if bug.is_complete:
        continue
    if ('\nPackage: whoopsie %s\n' % ver) in bug.bug.description:
        bugs.append(str(bug.bug.id))

print 'Bugs open for whoopsie %s:' % ver
print ', '.join(bugs)
