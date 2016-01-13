
modes = { 'Ideal': 0,
          'Discard': 1, 'DiscardConst': 1,
          'JCSLAM': 2, 'JCSLAMConst': 2,
          'JCSLAMFIFO': 3, 'JCSLAMFIFOConst': 3,
          'JCSLAMrandom': 4, 'JCSLAMrandomConst': 4,
          'JCSLAMnoforwardprop': 5, 'JCSLAMnoforwardpropConst': 5 }

pNum = [ 100, 250, 500, 750, 1000 ]

# -- Basic ------
mission = 'missionExperiment9Basic'
nominal = [ 0.75, 5 ]
constrained = [ 0.3, 5 ]

slots = { 'Ideal': [ 1, 1 ],
          'Discard': nominal, 'DiscardConst': constrained,
          'JCSLAM': nominal, 'JCSLAMConst': constrained,
          'JCSLAMFIFO': nominal, 'JCSLAMFIFOConst': constrained,
          'JCSLAMrandom': nominal, 'JCSLAMrandomConst': constrained,
          'JCSLAMnoforwardprop': nominal, 'JCSLAMnoforwardpropConst': constrained }

for m in modes:
    for p in pNum:
        missionFileName = '%s%s%d.ini' % (mission, m, p)
        print missionFileName
        ini = open( missionFileName, "w" )
        ini.write(
"""[offline_SLAM]
SLAMmode=%d
particleNum=%d
readingProcessingRate=%f
processingSlots=%d
logPath=data\\offlineSLAMExperiment9Basic
[stability]
timeminmax=-1	1
[agent]
ExecutiveOfflineSLAM=fbb8834e-9619-4745-b41f-b14b9f68fc03
[mission_region]
region=0	0	3.3528	4.8768
[landmark_file]
file=data\\paths\\layout1landmarks.ini
[path_file]
file=data\\paths\\layout1.path
[path_file]
file=data\\paths\\boundary1.path
[path_file]
file=data\\paths\\simExtraWalls.path""" % (modes[m], p, slots[m][0], slots[m][1]) )

# -- Advanced A ------
mission = 'missionExperiment9AdvancedA'
nominal = [ 0.5, 5 ]
constrained = [ 0.22, 5 ]
slots = { 'Ideal': [ 1, 1 ],
          'Discard': nominal, 'DiscardConst': constrained,
          'JCSLAM': nominal, 'JCSLAMConst': constrained,
          'JCSLAMFIFO': nominal, 'JCSLAMFIFOConst': constrained,
          'JCSLAMrandom': nominal, 'JCSLAMrandomConst': constrained,
          'JCSLAMnoforwardprop': nominal, 'JCSLAMnoforwardpropConst': constrained }

for m in modes:
    for p in pNum:
        missionFileName = '%s%s%d.ini' % (mission, m, p)
        print missionFileName
        ini = open( missionFileName, "w" )
        ini.write(
"""[offline_SLAM]
SLAMmode=%d
particleNum=%d
readingProcessingRate=%f
processingSlots=%d
logPath=data\\offlineSLAMExperiment9AdvancedA
[stability]
timeminmax=-1	1
[agent]
ExecutiveOfflineSLAM=fbb8834e-9619-4745-b41f-b14b9f68fc03
[mission_region]
region=0	0	18	 18
[forbidden_region]
region=10	0	8	 11
[landmark_file]
file=data\\paths\\layoutLargelandmarks.ini
[path_file]
file=data\\paths\\boundaryLarge.path
[path_file]
file=data\\paths\\layoutLarge.path""" % (modes[m], p, slots[m][0], slots[m][1]) )

# -- Advanced B ------
mission = 'missionExperiment9AdvancedB'
nominal = [ 0.42, 10 ]
constrained = [ 0.2, 10 ]
slots = { 'Ideal': [ 1, 1 ],
          'Discard': nominal, 'DiscardConst': constrained,
          'JCSLAM': nominal, 'JCSLAMConst': constrained,
          'JCSLAMFIFO': nominal, 'JCSLAMFIFOConst': constrained,
          'JCSLAMrandom': nominal, 'JCSLAMrandomConst': constrained,
          'JCSLAMnoforwardprop': nominal, 'JCSLAMnoforwardpropConst': constrained }

for m in modes:
    for p in pNum:
        missionFileName = '%s%s%d.ini' % (mission, m, p)
        print missionFileName
        ini = open( missionFileName, "w" )
        ini.write(
"""[offline_SLAM]
SLAMmode=%d
particleNum=%d
readingProcessingRate=%f
processingSlots=%d
logPath=data\\offlineSLAMExperiment9AdvancedB
[stability]
timeminmax=-1	1
[agent]
ExecutiveOfflineSLAM=fbb8834e-9619-4745-b41f-b14b9f68fc03
[mission_region]
region=0	0	18	 18
[forbidden_region]
region=10	0	8	 11
[landmark_file]
file=data\\paths\\layoutLargelandmarks.ini
[path_file]
file=data\\paths\\boundaryLarge.path
[path_file]
file=data\\paths\\layoutLarge.path""" % (modes[m], p, slots[m][0], slots[m][1]) )
