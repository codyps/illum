/* ex: set noet sw=8 sts=8 ts=8 tw=78: */

#include <stdio.h>

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

void usage_(const char *pn)
{
	fprintf(stderr,
		"usage: %s [options]\n"
		"\n"
		"options:\n"
		" -b <backlight dir>	a directory like '/sys/class/backlight/*'\n"
		" -l <percent>		dim to this percent brightness\n"
		" -z			the brightness value of '0' turns the backlight off\n"
		" -t <msec>		milliseconds (integer) after last activity that the display is dimmed\n"
		" -f <msec>		milliseconds to fade when dimming\n"
		" -F <msec>		milliseconds to fade when brightening\n"
		, pn);
}

int main(int argc, char **argv)
{
	int c;
	while ((c = getopt(argc, argv, "blXtfF"))) {
		switch(c) {
		case 'b':
		case 'l':
		case 'z':
		case 't':
		case 'f':
		case 'F':
		case '?':
		default:
			printf("got %c for %s %c\n", c, optarg, optopt);
		}
	}

	return 0;
}
