
import sys
import os
import shutil

#for arg in sys.argv:
#    print arg

outfile = sys.argv[1] # output file
libdir = sys.argv[2] # base library directory (i.e. ...\bin\library\)
filename = sys.argv[3] # version file
modulename = sys.argv[4] # module name
try:
    debugmode = sys.argv[5] # debug mode
except:
    debugmode = "RELEASE"

"""
Expecting version file of the format:
#define MODULE_MAJOR n
#define MODULE_MINOR n
#define MODULE_BUILDNO n
#define MODULE_EXTEND n
#define MODULE_UUID "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"
#define MODULE_PROCESS_COST float, float
#define MODULE_RESOURCE_REQUIREMENTS "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa,aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa,..."
"""

# make sure the output directory exists
if not os.path.exists( libdir + modulename ):
    os.mkdir( libdir + modulename )

# read the version file
versionF = open( filename, "r" )

version = []
uuid = ""

ind = 0
for line in versionF:
    if ind < 4: # version line
        version.append( (int)(line.split()[2]) )
    elif ind == 4: # uuid line
        uuid = line.split()[2]
    elif ind == 5: # process cost line
        if debugmode == "DEBUG":
            processCost = line.split()[3]
        else:
            processCost = line.split()[2][0:-1]
    elif ind == 6: # transferPenalty line
        transferPenalty = line.split()[2]
    elif ind == 7: # resourceRequirements line
        resourceRequirements = line.split()[2]
    else: # we're done
        break
    ind += 1
versionF.close()

# write the .ini file
iniF = open( libdir + modulename + "\\" + modulename + ".ini", "w" )
iniF.write( "version=%.2d.%.2d.%.5d.%.2d\nuuid=%s\ntype=DLL\nobject=%s.dll\ndebugmode=%s\nprocesscost=%f\ntransferpenalty=%f\nresourcerequirements=%s" %
            ( version[0], version[1], version[2], version[3], uuid[1:-1], modulename, debugmode, float(processCost), float(transferPenalty), resourceRequirements  ) )
iniF.close()

# copy the .dll file
shutil.copy( outfile, libdir + modulename + "\\" )
