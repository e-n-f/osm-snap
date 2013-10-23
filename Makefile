all: snap

snap: snap.c
	cc -g -Wall -O3 -o snap snap.c -lexpat

pnpoly: pnpoly.c
	cc -g -Wall -O3 -o pnpoly pnpoly.c -lm
