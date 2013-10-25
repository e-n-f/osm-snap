osm-snap (buildings)
========

Checks address tags on OSM ways and nodes.

    make

to compile

    bzcat extract.osm.bz2 | ./snap > extract.snap

to pull out all buildings and all nodes that have <code>addr:housenumber</code> tags.

Lines that are buildings will have the format

    id=whatever;tags=....;building=whatever;...: lat,lon lat,lon ...
  
Lines that are nodes will look like 

    48.905412,2.305988 address 2355920903

Lines that are buildings with address nodes around the outside will contain "nodeaddr=...".
Lines that are buildings that are directly tagged with addresses will contain "addr.housenumber=...".

    cat extract.snap | ./pnpoly > extract.pnpoly

to try to match the address nodes to building polygons. Each output line will have the tags
from the matched polygon followed by the node that is inside it.
