/* fakevehicle - minimal MAVLink vehicle for testing gndtui.
 *
 * Speaks HEARTBEAT, AUTOPILOT_VERSION and the parameter protocol, emulating
 * either ArduPilot (C-cast encoding) or PX4 (byte-wise encoding). Can drop a
 * percentage of streamed PARAM_VALUEs to exercise gap filling.
 *
 *   fakevehicle [--px4] [--udp HOST:PORT | --tcp PORT | --pty] [--params FILE]
 *               [--count N] [--loss PCT] [--rate N] [--sysid N] [--v1]
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <ardupilotmega/mavlink.h>

typedef struct {
	char name[17];
	uint8_t type;
	double value;
} fparam_t;

static fparam_t *P;
static int NP, CAPP;

static bool px4;
static int loss_pct;
static int rate		 = 800;
static uint8_t sysid = 1;
static bool v1;

static int fd = -1, listen_fd = -1, pty_slave = -1;
static bool is_udp;
static struct sockaddr_in udp_peer;
static mavlink_message_t rxmsg;
static mavlink_status_t rxst, txst;

static int stream_idx = -1;
static double stream_next;

static volatile sig_atomic_t stop;
static void on_sig(int s) { stop = 1; }

static double now_s(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void add_param(const char *name, uint8_t type, double v) {
	for (int i = 0; i < NP; i++)
		if (strcmp(P[i].name, name) == 0) {
			P[i].type  = type;
			P[i].value = v;
			return;
		}
	if (NP == CAPP) {
		CAPP = CAPP ? CAPP * 2 : 256;
		P	 = realloc(P, CAPP * sizeof *P);
	}
	snprintf(P[NP].name, sizeof P[NP].name, "%s", name);
	P[NP].type	= type;
	P[NP].value = v;
	NP++;
}

static void builtin_params(int count) {
	if (px4) {
		add_param("MAV_SYS_ID", MAV_PARAM_TYPE_INT32, sysid);
		add_param("SYS_AUTOSTART", MAV_PARAM_TYPE_INT32, 4001);
		add_param("MPC_XY_VEL_MAX", MAV_PARAM_TYPE_REAL32, 12);
		add_param("MC_ROLL_P", MAV_PARAM_TYPE_REAL32, 6.5);
		add_param("COM_ARM_WO_GPS", MAV_PARAM_TYPE_INT32, 1);
		add_param("BAT1_N_CELLS", MAV_PARAM_TYPE_INT32, 4);
		add_param("BAT1_V_CHARGED", MAV_PARAM_TYPE_REAL32, 4.05);
		add_param("SDLOG_PROFILE", MAV_PARAM_TYPE_INT32, 1);
		add_param("CAL_ACC0_ID", MAV_PARAM_TYPE_INT32, 1310988);
		add_param("SENS_BOARD_ROT", MAV_PARAM_TYPE_INT32, 0);
		add_param("NAV_RCL_ACT", MAV_PARAM_TYPE_INT32, 2);
		add_param("MIS_TAKEOFF_ALT", MAV_PARAM_TYPE_REAL32, 2.5);
	} else {
		add_param("SYSID_THISMAV", MAV_PARAM_TYPE_INT16, sysid);
		add_param("FORMAT_VERSION", MAV_PARAM_TYPE_INT16, 120);
		add_param("AHRS_ORIENTATION", MAV_PARAM_TYPE_INT8, 0);
		add_param("AHRS_EKF_TYPE", MAV_PARAM_TYPE_INT8, 3);
		add_param("AHRS_GPS_GAIN", MAV_PARAM_TYPE_REAL32, 1);
		add_param("AHRS_TRIM_X", MAV_PARAM_TYPE_REAL32, 0.0123);
		add_param("AHRS_RP_P", MAV_PARAM_TYPE_REAL32, 0.2);
		add_param("ARMING_CHECK", MAV_PARAM_TYPE_INT32, 1);
		add_param("ATC_RAT_RLL_P", MAV_PARAM_TYPE_REAL32, 0.135);
		add_param("ATC_RAT_PIT_P", MAV_PARAM_TYPE_REAL32, 0.135);
		add_param("BATT_MONITOR", MAV_PARAM_TYPE_INT8, 4);
		add_param("BATT_CAPACITY", MAV_PARAM_TYPE_INT32, 3300);
		add_param("BATT_AMP_PERVLT", MAV_PARAM_TYPE_REAL32, 17);
		add_param("FRAME_CLASS", MAV_PARAM_TYPE_INT8, 1);
		add_param("FRAME_TYPE", MAV_PARAM_TYPE_INT8, 1);
		add_param("FLTMODE1", MAV_PARAM_TYPE_INT8, 0);
		add_param("LOG_BITMASK", MAV_PARAM_TYPE_INT32, 176126);
		add_param("MOT_SPIN_ARM", MAV_PARAM_TYPE_REAL32, 0.1);
		add_param("MOT_SPIN_MIN", MAV_PARAM_TYPE_REAL32, 0.15);
		add_param("PILOT_THR_BHV", MAV_PARAM_TYPE_INT16, 0);
		add_param("RC1_MIN", MAV_PARAM_TYPE_INT16, 1100);
		add_param("RC1_MAX", MAV_PARAM_TYPE_INT16, 1900);
		add_param("SERIAL0_BAUD", MAV_PARAM_TYPE_INT32, 115);
		add_param("SERIAL1_PROTOCOL", MAV_PARAM_TYPE_INT8, 2);
		add_param("WPNAV_SPEED", MAV_PARAM_TYPE_REAL32, 1000);
		add_param("INS_GYRO_FILTER", MAV_PARAM_TYPE_INT16, 20);
	}
	for (int i = 0; NP < count; i++) {
		char n[24];
		snprintf(n, sizeof n, "TEST_P%04d", i);
		add_param(n, i % 3 ? MAV_PARAM_TYPE_REAL32 : MAV_PARAM_TYPE_INT32, i % 3 ? i * 0.25 : i);
	}
}

static bool load_params(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		perror(path);
		return false;
	}
	char line[256];
	while (fgets(line, sizeof line, f)) {
		if (line[0] == '#')
			continue;
		char name[64];
		double v;
		for (char *c = line; *c; c++)
			if (*c == ',' || *c == '\t')
				*c = ' ';
		if (sscanf(line, "%63s %lf", name, &v) != 2 || strlen(name) > 16)
			continue;
		add_param(name, v == rint(v) && fabs(v) < 1e7 ? MAV_PARAM_TYPE_INT32 : MAV_PARAM_TYPE_REAL32, v);
	}
	fclose(f);
	return NP > 0;
}

/* --------------------------------------------------------------------- */

static void send_msg(mavlink_message_t *m) {
	uint8_t buf[MAVLINK_MAX_PACKET_LEN];
	uint16_t len = mavlink_msg_to_send_buffer(buf, m);
	if (fd < 0)
		return;
	if (is_udp)
		sendto(fd, buf, len, 0, (struct sockaddr *)&udp_peer, sizeof udp_peer);
	else if (write(fd, buf, len) < 0 && errno != EAGAIN)
		fprintf(stderr, "write: %s\n", strerror(errno));
}

static float encode(const fparam_t *p) {
	if (!px4 || p->type == MAV_PARAM_TYPE_REAL32)
		return (float)p->value;
	union {
		float f;
		int32_t i32;
		int16_t i16;
		int8_t i8;
		uint8_t u8;
		uint16_t u16;
		uint32_t u32;
	} u;
	memset(&u, 0, sizeof u);
	switch (p->type) {
	case MAV_PARAM_TYPE_INT8:
		u.i8 = (int8_t)p->value;
		break;
	case MAV_PARAM_TYPE_UINT8:
		u.u8 = (uint8_t)p->value;
		break;
	case MAV_PARAM_TYPE_INT16:
		u.i16 = (int16_t)p->value;
		break;
	case MAV_PARAM_TYPE_UINT16:
		u.u16 = (uint16_t)p->value;
		break;
	case MAV_PARAM_TYPE_UINT32:
		u.u32 = (uint32_t)p->value;
		break;
	default:
		u.i32 = (int32_t)p->value;
		break;
	}
	return u.f;
}

static double decode(uint8_t type, float raw) {
	if (!px4 || type == MAV_PARAM_TYPE_REAL32)
		return type == MAV_PARAM_TYPE_REAL32 ? raw : rint(raw);
	union {
		float f;
		int32_t i32;
		int16_t i16;
		int8_t i8;
		uint8_t u8;
		uint16_t u16;
		uint32_t u32;
	} u;
	u.f = raw;
	switch (type) {
	case MAV_PARAM_TYPE_INT8:
		return u.i8;
	case MAV_PARAM_TYPE_UINT8:
		return u.u8;
	case MAV_PARAM_TYPE_INT16:
		return u.i16;
	case MAV_PARAM_TYPE_UINT16:
		return u.u16;
	case MAV_PARAM_TYPE_UINT32:
		return u.u32;
	default:
		return u.i32;
	}
}

static void send_param(int i) {
	mavlink_message_t m;
	mavlink_param_value_t pv = {
			.param_value = encode(&P[i]),
			.param_count = (uint16_t)NP,
			.param_index = (uint16_t)i,
			.param_type	 = P[i].type,
	};
	memcpy(pv.param_id, P[i].name, strnlen(P[i].name, 16));
	mavlink_msg_param_value_encode_status(sysid, 1, &txst, &m, &pv);
	send_msg(&m);
}

static void send_heartbeat(void) {
	mavlink_message_t m;
	mavlink_heartbeat_t hb = {
			.type		   = MAV_TYPE_QUADROTOR,
			.autopilot	   = px4 ? MAV_AUTOPILOT_PX4 : MAV_AUTOPILOT_ARDUPILOTMEGA,
			.base_mode	   = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED,
			.system_status = MAV_STATE_STANDBY,
	};
	mavlink_msg_heartbeat_encode_status(sysid, 1, &txst, &m, &hb);
	send_msg(&m);
}

static void send_version(void) {
	mavlink_message_t m;
	mavlink_autopilot_version_t av = {
			.capabilities	   = MAV_PROTOCOL_CAPABILITY_MAVLINK2 | MAV_PROTOCOL_CAPABILITY_MISSION_INT |
								 (px4 ? MAV_PROTOCOL_CAPABILITY_PARAM_ENCODE_BYTEWISE : MAV_PROTOCOL_CAPABILITY_PARAM_ENCODE_C_CAST),
			.flight_sw_version = px4 ? (1u << 24 | 15u << 16 | 0u << 8 | 255u) : (4u << 24 | 5u << 16 | 7u << 8 | 255u),
	};
	mavlink_msg_autopilot_version_encode_status(sysid, 1, &txst, &m, &av);
	send_msg(&m);
}

static void handle(const mavlink_message_t *msg) {
	switch (msg->msgid) {
	case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
		fprintf(stderr, "PARAM_REQUEST_LIST\n");
		stream_idx	= 0;
		stream_next = now_s();
		break;
	case MAVLINK_MSG_ID_PARAM_REQUEST_READ: {
		mavlink_param_request_read_t r;
		mavlink_msg_param_request_read_decode(msg, &r);
		int idx = r.param_index;
		if (idx < 0) {
			char n[17] = {0};
			memcpy(n, r.param_id, 16);
			for (int i = 0; i < NP; i++)
				if (strcmp(P[i].name, n) == 0)
					idx = i;
		}
		if (idx >= 0 && idx < NP)
			send_param(idx);
		break;
	}
	case MAVLINK_MSG_ID_PARAM_SET: {
		mavlink_param_set_t s;
		mavlink_msg_param_set_decode(msg, &s);
		char n[17] = {0};
		memcpy(n, s.param_id, 16);
		for (int i = 0; i < NP; i++) {
			if (strcmp(P[i].name, n) != 0)
				continue;
			if (strcmp(n, "FORMAT_VERSION") != 0) /* emulate a param the FC refuses */
				P[i].value = decode(P[i].type, s.param_value);
			fprintf(stderr, "PARAM_SET %s = %g\n", n, P[i].value);
			send_param(i);
		}
		break;
	}
	case MAVLINK_MSG_ID_COMMAND_LONG: {
		mavlink_command_long_t c;
		mavlink_msg_command_long_decode(msg, &c);
		uint8_t result = MAV_RESULT_UNSUPPORTED;
		if ((c.command == MAV_CMD_REQUEST_MESSAGE && (int)c.param1 == MAVLINK_MSG_ID_AUTOPILOT_VERSION) ||
				c.command == MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES) {
			send_version();
			result = MAV_RESULT_ACCEPTED;
		}
		mavlink_message_t m;
		mavlink_command_ack_t ack = {.command = c.command, .result = result};
		mavlink_msg_command_ack_encode_status(sysid, 1, &txst, &m, &ack);
		send_msg(&m);
		break;
	}
	default:
		break;
	}
}

static void parse(const uint8_t *b, ssize_t n) {
	for (ssize_t i = 0; i < n; i++) {
		mavlink_message_t msg;
		mavlink_status_t st;
		if (mavlink_parse_char_buffer(&rxmsg, &rxst, b[i], &msg, &st) == MAVLINK_FRAMING_OK)
			handle(&msg);
	}
}

static void usage(void) {
	fprintf(stderr, "usage: fakevehicle [--px4] [--udp HOST:PORT | --tcp PORT | --pty]\n"
					"                   [--params FILE] [--count N] [--loss PCT] [--rate N]\n"
					"                   [--sysid N] [--v1]\n");
}

int main(int argc, char **argv) {
	const char *udp = "127.0.0.1:14550", *params = NULL;
	int tcp_port = 0, count = 1000;
	bool use_pty					= false;
	static const struct option lo[] = {
			{"px4",		  no_argument,		 0, 'x'},
			{"ardupilot", no_argument,		 0, 'a'},
			{"udp",		  required_argument, 0, 'u'},
			{"tcp",		  required_argument, 0, 't'},
			{"pty",		  no_argument,		 0, 'p'},
			{"params",	  required_argument, 0, 'P'},
			{"count",	  required_argument, 0, 'c'},
			{"loss",	  required_argument, 0, 'l'},
			{"rate",	  required_argument, 0, 'r'},
			{"sysid",	  required_argument, 0, 's'},
			{"v1",		  no_argument,		 0, '1'},
			{"help",	  no_argument,		 0, 'h'},
			{0,			  0,				 0, 0  },
	};
	int c;
	while ((c = getopt_long(argc, argv, "h", lo, NULL)) != -1) {
		switch (c) {
		case 'x':
			px4 = true;
			break;
		case 'a':
			px4 = false;
			break;
		case 'u':
			udp = optarg;
			break;
		case 't':
			tcp_port = atoi(optarg);
			break;
		case 'p':
			use_pty = true;
			break;
		case 'P':
			params = optarg;
			break;
		case 'c':
			count = atoi(optarg);
			break;
		case 'l':
			loss_pct = atoi(optarg);
			break;
		case 'r':
			rate = atoi(optarg) > 0 ? atoi(optarg) : 1;
			break;
		case 's':
			sysid = (uint8_t)atoi(optarg);
			break;
		case '1':
			v1 = true;
			break;
		default:
			usage();
			return 2;
		}
	}
	if (params ? !load_params(params) : (builtin_params(count), false))
		return 1;
	if (v1)
		txst.flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
	srand((unsigned)time(NULL));
	signal(SIGINT, on_sig);
	signal(SIGTERM, on_sig);
	signal(SIGPIPE, SIG_IGN);

	if (use_pty) {
		int master;
		char name[128];
		if (openpty(&master, &pty_slave, name, NULL, NULL) < 0) {
			perror("openpty");
			return 1;
		}
		struct termios t;
		tcgetattr(pty_slave, &t);
		cfmakeraw(&t);
		tcsetattr(pty_slave, TCSANOW, &t);
		fcntl(master, F_SETFL, O_NONBLOCK);
		fd = master;
		printf("%s\n", name);
		fflush(stdout);
	} else if (tcp_port) {
		listen_fd = socket(AF_INET, SOCK_STREAM, 0);
		int one	  = 1;
		setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(tcp_port)};
		a.sin_addr.s_addr	 = htonl(INADDR_LOOPBACK);
		if (bind(listen_fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(listen_fd, 1) < 0) {
			perror("tcp");
			return 1;
		}
	} else {
		is_udp = true;
		char host[64];
		int port;
		if (sscanf(udp, "%63[^:]:%d", host, &port) != 2) {
			usage();
			return 2;
		}
		udp_peer.sin_family = AF_INET;
		udp_peer.sin_port	= htons(port);
		inet_pton(AF_INET, host, &udp_peer.sin_addr);
		fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	}
	fprintf(stderr, "fakevehicle: %s, %d params, loss %d%%\n", px4 ? "PX4" : "ArduPilot", NP, loss_pct);

	double next_hb = 0;
	while (!stop) {
		struct pollfd pfd = {.fd = fd >= 0 ? fd : listen_fd, .events = POLLIN};
		poll(&pfd, 1, 5);
		if (fd < 0 && (pfd.revents & POLLIN)) {
			fd = accept(listen_fd, NULL, NULL);
			fcntl(fd, F_SETFL, O_NONBLOCK);
			memset(&rxst, 0, sizeof rxst);
			fprintf(stderr, "client connected\n");
			continue;
		}
		if (fd >= 0 && (pfd.revents & (POLLIN | POLLHUP))) {
			uint8_t buf[2048];
			ssize_t n = is_udp ? recv(fd, buf, sizeof buf, 0) : read(fd, buf, sizeof buf);
			if (n > 0) {
				parse(buf, n);
			} else if (!is_udp && !use_pty && n == 0) {
				fprintf(stderr, "client disconnected\n");
				close(fd);
				fd		   = -1;
				stream_idx = -1;
			}
		}
		double now = now_s();
		if (now >= next_hb) {
			next_hb = now + 1.0;
			send_heartbeat();
		}
		while (stream_idx >= 0 && now >= stream_next) {
			if (rand() % 100 >= loss_pct)
				send_param(stream_idx);
			stream_idx++;
			stream_next += 1.0 / rate;
			if (stream_idx >= NP)
				stream_idx = -1;
		}
	}
	return 0;
}
