
import sys

#for arg in sys.argv:
#    print arg

filename = sys.argv[1] # version file
modulename = sys.argv[2] # module name

# read the current version file if it exists

version = [ 0, 1, 0, 0 ]
extralines = []

if 0:
    try:
        versionF = open( filename, "r" )
        ind = 0
        for line in versionF:
            version[ind] = (int)(line.split()[2])
            ind += 1
            if ind == 4:
                break
        for line in versionF:
            extralines.append( line )
        versionF.close()
    except IOError:
        pass # file does not exist

    # increment build number

    version[2] += 1

    # overflow control
    if version[2] > 33333: # < max short
        version[2] = 0
        version[1] += 1

    if version[1] > 99:
        version[1] = 0
        version[0] += 1

    # write new version file

    versionF = open( filename, "w" )

    versionF.write( "#define %s_MAJOR %d\n#define %s_MINOR %d\n#define %s_BUILDNO %d\n#define %s_EXTEND %d\n" %
                    ( modulename, version[0],
                      modulename, version[1],
                      modulename, version[2],
                      modulename, version[3] ) )
    for line in extralines:
        versionF.write( line )

    versionF.close()    
