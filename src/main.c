/* gndtui - terminal parameter editor for ArduPilot and PX4 over MAVLink. */
#include <getopt.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "detect.h"
#include "metafetch.h"
#include "util.h"

static void usage(FILE *f) {
	fprintf(f, "Usage: gndtui [options] [CONNECTION]\n"
			   "\n"
			   "Terminal parameter editor for ArduPilot and PX4 (MAVLink).\n"
			   "Without CONNECTION the vehicle is auto-detected like QGroundControl does:\n"
			   "USB flight controllers / SiK radios, UDP :14550 and TCP 127.0.0.1:5760 (SITL).\n"
			   "\n"
			   "CONNECTION:\n"
			   "  /dev/ttyACM0[:BAUD]        serial port (also serial:/dev/ttyUSB0:57600)\n"
			   "  udp:[HOST:]PORT            listen for UDP (e.g. udp:14550)\n"
			   "  udpout:HOST:PORT           send UDP to HOST:PORT (e.g. udpout:192.168.4.1:14550)\n"
			   "  tcp:[HOST:]PORT            TCP client (e.g. tcp:127.0.0.1:5760)\n"
			   "\n"
			   "Options:\n"
			   "  -b, --baud N          serial baud rate (default 115200)\n"
			   "  -s, --sysid N         only talk to this vehicle system id\n"
			   "      --gcs-sysid N     our MAVLink system id (default 255)\n"
			   "      --no-serial       auto-detect: do not probe serial ports\n"
			   "      --no-udp          auto-detect: do not listen on UDP\n"
			   "      --no-tcp          auto-detect: do not probe TCP (SITL)\n"
			   "      --udp-port N      auto-detect UDP port (default 14550)\n"
			   "      --tcp-port N      auto-detect TCP port (default 5760)\n"
			   "  -m, --meta FILE       parameter metadata (apm.pdef.xml or PX4 .json)\n"
			   "      --offline         never download metadata, use cache only\n"
			   "  -d, --dump            no UI: download parameters, print TSV to stdout, exit\n"
			   "  -t, --timeout SEC     with --dump: give up after SEC seconds (default 90)\n"
			   "  -l, --list            list serial ports and how they are classified\n"
			   "      --log FILE        write a debug log\n"
			   "  -V, --version         print version\n"
			   "  -h, --help            this help\n");
}

static int list_ports(void) {
	serial_port_info_t p[DETECT_MAX];
	int n = serial_enumerate(p, DETECT_MAX);
	if (n == 0)
		printf("No ttyACM/ttyUSB ports found.\n");
	for (int i = 0; i < n; i++) {
		int baud;
		bool bl;
		char what[160];
		cand_kind_t k	 = serial_classify(&p[i], &baud, &bl, what, sizeof what);
		const char *kind = k == CAND_SERIAL_FC ? "flight controller" : k == CAND_SERIAL_RADIO ? "telemetry radio" : "other";
		printf("%-16s %-18s %6d  %s%s\n", p[i].dev, kind, baud, what, bl ? "  (bootloader)" : "");
	}
	return 0;
}

static int parse_int(const char *s, int lo, int hi, const char *what) {
	char *end;
	long v = strtol(s, &end, 10);
	if (end == s || *end || v < lo || v > hi) {
		fprintf(stderr, "gndtui: invalid %s '%s'\n", what, s);
		exit(2);
	}
	return (int)v;
}

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	/* UTF-8 for the terminal, but numbers are always "1.5": parameter files,
	 * metadata and MAVLink tools all use the C decimal point. */
	setlocale(LC_NUMERIC, "C");

	static app_t app; /* large (link buffers): keep off the stack */
	app_opts_t *o = &app.opt;
	o->baud		  = 115200;
	o->serial = o->udp = o->tcp = true;
	o->udp_port					= 14550;
	o->tcp_port					= 5760;
	o->target_sysid				= -1;
	o->gcs_sysid				= 255;
	o->gcs_compid				= MAV_COMP_ID_MISSIONPLANNER;
	o->dump_timeout				= 90;

	enum { O_GCS = 1000, O_NOSER, O_NOUDP, O_NOTCP, O_UDPP, O_TCPP, O_OFFLINE, O_LOG };
	static const struct option lo[] = {
			{"baud",	  required_argument, 0, 'b'		 },
			{"sysid",	  required_argument, 0, 's'		 },
			{"gcs-sysid", required_argument, 0, O_GCS	 },
			{"no-serial", no_argument,		 0, O_NOSER	 },
			{"no-udp",	  no_argument,		 0, O_NOUDP	 },
			{"no-tcp",	  no_argument,		 0, O_NOTCP	 },
			{"udp-port",  required_argument, 0, O_UDPP	 },
			{"tcp-port",  required_argument, 0, O_TCPP	 },
			{"meta",	  required_argument, 0, 'm'		 },
			{"offline",	  no_argument,		 0, O_OFFLINE},
			{"dump",	  no_argument,		 0, 'd'		 },
			{"timeout",	  required_argument, 0, 't'		 },
			{"list",	  no_argument,		 0, 'l'		 },
			{"log",		  required_argument, 0, O_LOG	 },
			{"version",	  no_argument,		 0, 'V'		 },
			{"help",	  no_argument,		 0, 'h'		 },
			{0,			  0,				 0, 0		 },
	};
	int c;
	const char *log_path = NULL;
	while ((c = getopt_long(argc, argv, "b:s:m:dt:lVh", lo, NULL)) != -1) {
		switch (c) {
		case 'b':
			o->baud = parse_int(optarg, 1200, 4000000, "baud rate");
			break;
		case 's':
			o->target_sysid = parse_int(optarg, 1, 255, "system id");
			break;
		case O_GCS:
			o->gcs_sysid = (uint8_t)parse_int(optarg, 1, 255, "system id");
			break;
		case O_NOSER:
			o->serial = false;
			break;
		case O_NOUDP:
			o->udp = false;
			break;
		case O_NOTCP:
			o->tcp = false;
			break;
		case O_UDPP:
			o->udp_port = parse_int(optarg, 1, 65535, "port");
			break;
		case O_TCPP:
			o->tcp_port = parse_int(optarg, 1, 65535, "port");
			break;
		case 'm':
			path_expand_home(optarg, o->meta_path, sizeof o->meta_path);
			break;
		case O_OFFLINE:
			o->offline = true;
			break;
		case 'd':
			o->dump = true;
			break;
		case 't':
			o->dump_timeout = parse_int(optarg, 1, 86400, "timeout");
			break;
		case 'l':
			return list_ports();
		case O_LOG:
			log_path = optarg;
			break;
		case 'V':
			printf("gndtui %s\n", GNDTUI_VERSION);
			return 0;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}
	if (optind < argc) {
		char err[256];
		if (!link_spec_parse(argv[optind], o->baud, &o->forced_spec, err, sizeof err)) {
			fprintf(stderr, "gndtui: %s\n", err);
			return 2;
		}
		o->forced = true;
		if (optind + 1 < argc) {
			fprintf(stderr, "gndtui: only one connection can be given\n");
			return 2;
		}
	}
	if (!o->forced && !o->serial && !o->udp && !o->tcp) {
		fprintf(stderr, "gndtui: nothing to connect to (all auto-detect sources disabled)\n");
		return 2;
	}
	if (log_path && !log_open(log_path)) {
		perror(log_path);
		return 1;
	}
	LOG("gndtui %s starting", GNDTUI_VERSION);

	metafetch_global_init();
	int rc = app_run(&app);
	metafetch_global_cleanup();
	log_close();
	return rc;
}
