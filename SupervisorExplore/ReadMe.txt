Scenarios to test
-adding new regions
-merging sections
-splitting sections
-avatars switching sections

TODO:
register as watcher for the map
keep track of rough map sections that need updating (merging sections as they come)
update sections at a fixed frequency
	suspend updates if no map changes have happened
	force updates after events like avatar finishes search, gives up, leaves, joins, region added


logic should work something like this

Initialize
Get Region
Get Map
Get Avatars

When avatar joins
get avatar position
If avatar is not in reachable cell
	Expand reachable cells using flood fill from new avatar's position
	If new reachable cells connected to a current cluster then add them to that cluster
	Else create a new cluster from these cells
assign avatar's current cell as search cell (will be reassigned below)
call partition check timeout

When avatar leaves
If no avatars left in that region clear cluster
call partition check timeout

When avatar finished cell
call partition check timeout

On partition check timeout
Update Map
For each cluster
	For reachable cells
		if cell is "searched" then remove from partition
		if cell is occupied then update unreachable cells
	if any partition has less than half average parition size
		Repartition cells using old avatars' current search cell as initial location
	reassign all search cells that are no longer valid
reset timeout
	
