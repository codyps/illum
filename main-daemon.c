/* ex: set noet sw=8 sts=8 ts=8 tw=78: */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* posix */
#include <unistd.h> /* getopt(), etc */

/* interfaces:
 *  cfg:
 *   - command line for daemon startup (cfg)
 *   - config file (cfg)
 *     - cmdline & config file need to have the same options
 *   - linux input devices (activity + cfg)
 *   - x11 screensaver (inhibit + activity)
 *    - could also be cfg/defaults if we probe for x11 configured timeouts
 *   - unix socket (inhibit + activity + cfg)
 *    - inhibit via: open, send inhibit cmd, (inhbit until close or unhibit
 *      cmd)
 *    - single socket vs multiple sockets?
 *      - could have a socket dedicated to inhibit that discards inputs and
 *        just inhibits while open
 *   - dbus (inhibit + activity + cfg)
 *     - probably has standard apis for inhibit & activity
 */

#define MSEC_FROM_SEC(sec) ((sec) * 1000)

/*
 * Settings that control what the dimming should look like to the user
 */
struct dim_conf {
	uint_least64_t idle_msec,
		       fade_msec,
		       brighten_msec,
		       dim_percent;
};

#define DIM_CONF_DEFAULT {			\
	.idle_msec = MSEC_FROM_SEC(60),		\
	.fade_msec  = MSEC_FROM_SEC(10),	\
	.brighten_msec = MSEC_FROM_SEC(1),	\
	.dim_percent = 2			\
}

/*
 * Settings that describe an interface to perform the dimming
 * 
 * XXX: at the moment, we only support using '/sys/class/backlight' like
 * directories. Alternates include xbacklight.
 */
struct dim_method_conf {
	const char *path;
};

static
void usage_(const char *pn)
{
	fprintf(stderr,
		"usage: %s [options]\n"
		"\n"
		"options:\n"
		" -h			print this help\n"
		" -b <backlight dir>	a directory like '/sys/class/backlight/*'\n"
		" -l <percent>		dim to this percent brightness\n"
		" -t <msec>		milliseconds (integer) after last activity that the display is dimmed\n"
		" -f <msec>		milliseconds to fade when dimming\n"
		" -F <msec>		milliseconds to fade when brightening\n"
		, pn);
}
#define usage() usage_(argc?argv[0]:"illum-d")

int main(int argc, char **argv)
{
	int c, e = 0;
	struct dim_conf dc = DIM_CONF_DEFAULT;
	struct dim_method_conf dmc = { NULL };

	while ((c = getopt(argc, argv, "hbltfF")) != -1) {
		switch(c) {
		case 'h':
			usage();
			return 0;
		case 'b':
			dmc.path = optarg;
			break;
		case 'l':
			dc.dim_percent = strtoll(optarg, NULL, 0);
			break;
		case 't':
			dc.idle_msec = strtoll(optarg, NULL, 0);
			break;
		case 'f':
			dc.fade_msec = strtoll(optarg, NULL, 0);
			break;
		case 'F':
			dc.brighten_msec = strtoll(optarg, NULL, 0);
			break;
		case '?':
		default:
			printf("got %c for %s %c\n", c, optarg, optopt);
			usage();
			e++;
		}
	}

	if (e) {
		usage();
		return 1;
	}

	/* TODO: setup driver for brightness setting(s) */
	/* TODO: setup driver for activity notification */
	/* TODO: wait for timeout */
	/* TODO: begin dimming */

	return 0;
}
