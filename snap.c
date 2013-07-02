#include <stdio.h>
#include <stdlib.h>
#include <expat.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#define TMPFILE "/tmp/osm-tmp-node"

struct node {
	unsigned id; // still just over 2^31
	unsigned date; // not 2038 yet!
	int lat;
	int lon;
};

double dist(double lat1, double lon1, double lat2, double lon2) {
	double r = 6367 / 1.609344;
	double foot = 180 / r / 5280 / M_PI;

	lat1 *= M_PI / 180;
	lon1 *= M_PI / 180;
	lat2 *= M_PI / 180;
	lon2 *= M_PI / 180;

        double latd = lat2 - lat1;
        double lond = (lon2 - lon1) * cos((lat1 + lat2) / 2);

        double d = sqrt(latd * latd + lond * lond) * 180 / M_PI / foot;
        if (d >= 1000) {
                d = acos(sin(lat1) * sin(lat2) + cos(lat1) * cos(lat2) * cos(lon2 - lon1)) * r * 5280;
        }

        return d;
}

int nodecmp(const void *v1, const void *v2) {
	const struct node *n1 = v1;
	const struct node *n2 = v2;

	if (n1->id < n2->id) {
		return -1;
	}
	if (n1->id > n2->id) {
		return 1;
	}

	if (n1->date < n2->date) {
		return -1;
	}
	if (n1->date > n2->date) {
		return 1;
	}

	return 0;
}

// http://www.tbray.org/ongoing/When/200x/2003/03/22/Binary
void *search(const void *key, const void *base, size_t nel, size_t width,
		int (*cmp)(const void *, const void *)) {

	long long high = nel, low = -1, probe;
	while (high - low > 1) {
		probe = (low + high) >> 1;
		// printf("try %d\n", probe);
		int c = cmp(((char *) base) + probe * width, key);
		if (c > 0) {
			high = probe;
		} else {
			low = probe;
		}
	}

	if (low < 0) {
		low = 0;
	}

	return ((char *) base) + low * width;
}

// boilerplate from
// http://marcomaggi.github.io/docs/expat.html#overview-intro
// Copyright 1999, Clark Cooper

#ifdef XML_LARGE_SIZE
#  if defined(XML_USE_MSC_EXTENSIONS) && _MSC_VER < 1400
#    define XML_FMT_INT_MOD "I64"
#  else
#    define XML_FMT_INT_MOD "ll"
#  endif
#else
#  define XML_FMT_INT_MOD "l"
#endif

#define BUFFSIZE        8192

FILE *tmp;
void *map = NULL;
long long nel;

unsigned int oldway = 0;
double olddist = 0;

unsigned theway = 0;
unsigned thedate = 0;
struct node *thenodes[100000];
unsigned thenodecount = 0;
char *theuser = NULL;
char *theuid = NULL;
char *theversion = NULL;
char *thetimestamp = NULL;
int bogus = 0;

unsigned preseq = 0;
unsigned seq = 0;

char tags[50000] = "";

int olat = 0;
int olon = 0;
unsigned oid = 0;

struct node prevnode;

time_t parsedate(const char *date) {
	struct tm tm;

	if (sscanf(date, "%d-%d-%dT%d:%d:%d",
		&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		&tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {

		int day = tm.tm_mday + (31 * tm.tm_mon) + (372 * (tm.tm_year - 2000));
		return (day * 86400) +
			3600 * tm.tm_hour + 60 * tm.tm_min + tm.tm_sec;
	} else {
		fprintf(stderr, "can't parse date %s\n", date);
		return 0;
	}
}

static void XMLCALL start(void *data, const char *element, const char **attribute) {
	if (strcmp(element, "node") == 0) {
		struct node n;
		int i;
		int visible = 1;
		n.id = 0;
		n.date = 0;
		n.lat = INT_MIN;
		n.lon = INT_MIN;

		for (i = 0; attribute[i] != NULL; i += 2) {
			if (strcmp(attribute[i], "timestamp") == 0) {
				n.date = parsedate(attribute[i + 1]);
			} else if (strcmp(attribute[i], "id") == 0) {
				n.id = atoi(attribute[i + 1]);
			} else if (strcmp(attribute[i], "lat") == 0) {
				n.lat = atof(attribute[i + 1]) * 1000000.0;
			} else if (strcmp(attribute[i], "lon") == 0) {
				n.lon = atof(attribute[i + 1]) * 1000000.0;
			} else if (strcmp(attribute[i], "visible") == 0) {
				visible = strcmp(attribute[i + 1], "false");
			}
		}

#if 0
		if (!visible) {
			n.lat = INT_MIN;
			n.lon = INT_MIN;
		}
#endif

		if (n.id > seq + 10000) {
			fprintf(stderr, "%u\n", n.id);
			seq = n.id;
		}

		if (nodecmp(&n, &prevnode) < 0) {
			fprintf(stderr, "node went backwards (%d):\n",
				nodecmp(&n, &prevnode));
			fprintf(stderr, "%u %u %d %d\n", prevnode.id, prevnode.date, prevnode.lat, prevnode.lon);
			fprintf(stderr, "%u %u %d %d\n", n.id, n.date, n.lat, n.lon);
		} else {
			if (n.id != oid || n.lat != olat || n.lon != olon) {
				fwrite(&n, sizeof(struct node), 1, tmp);

				oid = n.id;
				olat = n.lat;
				olon = n.lon;
			}
		}

		prevnode = n;
	} else if (strcmp(element, "way") == 0) {
		if (map == NULL) {
			fflush(tmp);

			int fd = open(TMPFILE, O_RDONLY);
			if (fd < 0) {
				perror(TMPFILE);
				exit(EXIT_FAILURE);
			}

			struct stat st;
			if (fstat(fd, &st) < 0) {
				perror("stat");
				exit(EXIT_FAILURE);
			}

			nel = st.st_size / sizeof(struct node);

			fprintf(stderr, "%d, %lld\n", fd, (long long) st.st_size);

			map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
			if (map == MAP_FAILED) {
				perror("mmap");
				exit(EXIT_FAILURE);
			}

			close(fd);
		}

		int i;
		theuser = NULL;
		theuid = NULL;
		theversion = NULL;
		thetimestamp = NULL;
		thenodecount = 0;
		bogus = 0;
		strcpy(tags, "");

		for (i = 0; attribute[i] != NULL; i += 2) {
			if (strcmp(attribute[i], "id") == 0) {
				theway = atoi(attribute[i + 1]);
			} else if (strcmp(attribute[i], "user") == 0) {
				theuser = strdup(attribute[i + 1]);
			} else if (strcmp(attribute[i], "uid") == 0) {
				theuid = strdup(attribute[i + 1]);
			} else if (strcmp(attribute[i], "version") == 0) {
				theversion = strdup(attribute[i + 1]);
			} else if (strcmp(attribute[i], "timestamp") == 0) {
				thetimestamp = strdup(attribute[i + 1]);
				thedate = parsedate(attribute[i + 1]);
			}
		}
	} else if (strcmp(element, "nd") == 0) {
		int i;

		struct node n;
		n.id = 0;
		n.date = thedate;

		for (i = 0; attribute[i] != NULL; i += 2) {
			if (strcmp(attribute[i], "ref") == 0) {
				n.id = atoi(attribute[i + 1]);
			}
		}

		struct node *find = search(&n, map, nel, sizeof(struct node), nodecmp);

		if (find->lat == INT_MIN || find->lat == 0) {
			printf("FAIL looked for %u found %u %d\n", n.id, find->id, find->lat);
			bogus = 1;
		} else if (find->id == n.id) {
			thenodes[thenodecount++] = find;
		} else {
			printf("FAIL looked for %u found %u\n", n.id, find->id);
			bogus = 1;
		}
	} else if (strcmp(element, "tag") == 0) {
		if (theway != 0) {
			const char *key = "";
			const char *value = "";

			int i;
			for (i = 0; attribute[i] != NULL; i += 2) {
				if (strcmp(attribute[i], "k") == 0) {
					key = attribute[i + 1];
				}
				if (strcmp(attribute[i], "v") == 0) {
					value = attribute[i + 1];
				}
			}

			int n = strlen(tags);
			if (n + strlen(key) + strlen(value) + 5 < sizeof(tags)) {
				sprintf(tags + n, ";%s=%s ", key, value);
			}
		}
	} else if (strcmp(element, "changeset") == 0) {
		int i;
		for (i = 0; attribute[i] != NULL; i += 2) {
			if (strcmp(attribute[i], "id") == 0) {
				unsigned id = atoi(attribute[i + 1]);
				if (id > preseq + 100000) {
					fprintf(stderr, "--- changeset %u\n", id);
					preseq = id;
				}
			}
		}
	}
}

static void XMLCALL end(void *data, const char *el) {
	if (strcmp(el, "way") == 0) {
		printf("%u ", theway);
		printf("%s ", thetimestamp);
		printf("%u ", thedate);
		printf("%s ", theversion);

		double sum = 0;
		int i;
		for (i = 1; i < thenodecount; i++) {
			sum += dist(thenodes[i - 1]->lat / 1000000.0,
				    thenodes[i - 1]->lon / 1000000.0,
				    thenodes[i    ]->lat / 1000000.0,
				    thenodes[i    ]->lon / 1000000.0);
		}

		printf("%.3f ", sum);

		if (bogus) {
			printf("0/bogus ");
		} else if (theway == oldway) {
			printf("%.3f ", sum - olddist);
			olddist = sum;
			oldway = theway;
		} else {
			printf("%.3f ", sum);
			olddist = sum;
			oldway = theway;
		}

		for (i = 0; i < thenodecount; i++) {
			printf("%lf,%lf ", thenodes[i]->lat / 1000000.0,
					   thenodes[i]->lon / 1000000.0);
		}

		printf("%s ", tags);

		printf("\n");

		theway = 0;
		free(theuser); theuser = NULL;
		free(theuid); theuid = NULL;
		free(thetimestamp); thetimestamp = NULL;
		free(theversion); theversion = NULL;

#if 0
		for (i = 1; i < thenodecount; i++) {
			printf("dist: %lf,%lf to %lf,%lf ",
					   thenodes[i - 1]->lat / 1000000.0,
					   thenodes[i - 1]->lon / 1000000.0,
					   thenodes[i]->lat / 1000000.0,
					   thenodes[i]->lon / 1000000.0);
		
			printf("%.6f\n", dist(thenodes[i - 1]->lat / 1000000.0,
				    thenodes[i - 1]->lon / 1000000.0,
				    thenodes[i    ]->lat / 1000000.0,
				    thenodes[i    ]->lon / 1000000.0));
		}
#endif
	}
}

int main(int argc, char *argv[]) {
	putenv("TZ=GMT");

	tmp = fopen(TMPFILE, "w");
	if (tmp == NULL) {
		perror(TMPFILE);
		exit(1);
	}

	XML_Parser p = XML_ParserCreate(NULL);
	if (! p) {
		fprintf(stderr, "Couldn't allocate memory for parser\n");
		exit(-1);
	}

	XML_SetElementHandler(p, start, end);

	for (;;) {
		int done;
		int len;
		char Buff[BUFFSIZE];

		len = (int)fread(Buff, 1, BUFFSIZE, stdin);
		if (ferror(stdin)) {
       			fprintf(stderr, "Read error\n");
			exit(-1);
		}
		done = feof(stdin);

		if (XML_Parse(p, Buff, len, done) == XML_STATUS_ERROR) {
			fprintf(stderr, "Parse error at line %" XML_FMT_INT_MOD "u:\n%s\n", XML_GetCurrentLineNumber(p), XML_ErrorString(XML_GetErrorCode(p)));
			exit(-1);
		}

	   	if (done)
     			break;
	}

	XML_ParserFree(p);
	return 0;
}
