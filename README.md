osm-snap
========

Flattens the ways from OpenStreetMap XML into datamaps format.

Example
-------

	$ curl -O https://s3.amazonaws.com/metro-extracts.mapzen.com/indianapolis.osm.bz2
	  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
					 Dload  Upload   Total   Spent    Left  Speed
	100 15.7M  100 15.7M    0     0  1321k      0  0:00:12  0:00:12 --:--:-- 1394k
	$ make
	cc -g -Wall -O3 -o snap snap.c -lexpat
	$ bzcat indianapolis.osm.bz2 | ./snap | ../datamaps/encode -z20 -o indianapolis.dm
	...
	Sorting 1 shapes of 7 point(s), zoom level 22
	Sorting part 1 of 1
	Sorting 1 shapes of 3 point(s), zoom level 28
	Sorting part 1 of 1
	Sorting 1 shapes of 7 point(s), zoom level 23
	Sorting part 1 of 1
	Merging: 100%
	$ ../datamaps/render indianapolis.dm 14 4271 6210 > broadripple.png
