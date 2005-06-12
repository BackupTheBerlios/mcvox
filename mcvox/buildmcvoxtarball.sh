#!/bin/sh
REL=0.5
tar --exclude-from mcvox-${REL}/exclude.mcvox -zcvf mcvox-${REL}.tgz mcvox-${REL}
md5sum mcvox-${REL}.tgz > mcvox-${REL}.md5 