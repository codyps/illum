/* ex: set noet sw=8 sts=8 ts=8 tw=78: */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* posix */
#include <unistd.h> /* getopt(), etc */

/* open() */
#include <sys/stat.h>
#include <fcntl.h>

/* libevdev */
#include <libevdev/libevdev.h>


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

/*
 * TODO:
 * - configuration of which keys are listened for
 * - configuration of how large the steps are
 * - idle dimming
 * - locking
 * - freezing crypto partitions
 * - sleeping


		" -l <percent>		dim to this percent brightness\n"
		" -t <msec>		milliseconds (integer) after last activity that the display is dimmed\n"
		" -f <msec>		milliseconds to fade when dimming\n"
		" -F <msec>		milliseconds to fade when brightening\n"
 */

static const char *opts = "he:b:";
static
void usage_(const char *pn)
{
	fprintf(stderr,
		"Adjust brightness based on keypresses\n"
		"KEY_BRIGHTNESSDOWN & KEY_BRIGHTNESSUP\n"
		"\n"
		"usage: %s -[%s]\n"
		"\n"
		"options:\n"
		" -h			print this help\n"
		" -e <event device>	a file like '/dev/input/event0'\n"
		" -b <backlight dir>	a directory like '/sys/class/backlight/*'\n"
		, pn, opts);

}
#define usage() usage_(argc?argv[0]:"illum-d")

/*
 * sys_backlight assumes max_brightness is fixed
 */
struct sys_backlight {
	const char *path;
	int dir_fd;
	uintmax_t max_brightness;
	int brightness_fd;
};

static
int sys_backlight_init_max_brightness(struct sys_backlight *sb)
{
	int mfd = openat(sb->dir_fd, "max_brightness", O_RDONLY);
	if (mfd == -1)
		return -2;

	char buf[15];
	int r = read(mfd, buf, sizeof(buf) - 1);
	close(mfd);

	if (r == -1)
		return -3;

	buf[r] = '\0';

	r = sscanf(buf, "%jd", &sb->max_brightness);
	if (r != 1)
		return -4;

	return 0;
}

static
int sys_backlight_init(struct sys_backlight *sb, const char *path)
{
	int ret = 0;
	sb->path = path;
	sb->dir_fd = open(sb->path, O_RDONLY | O_DIRECTORY);
	if (sb->dir_fd == -1)
		return -1;

	int r = sys_backlight_init_max_brightness(sb);
	if (r < 0) {
		ret = r;
		goto e_out;
	}

	sb->brightness_fd = openat(sb->dir_fd, "brightness", O_RDONLY);
	if (!sb->brightness_fd) {
		ret = -5;
		goto e_out;
	}

	return 0;

e_out:
	close(sb->dir_fd);
	return ret;
}

int main(int argc, char **argv)
{
	int c, e = 0;
	const char *b_path = NULL;
	const char *e_path = "/dev/input/event0";

	while ((c = getopt(argc, argv, opts)) != -1) {
		switch(c) {
		case 'h':
			usage();
			return 0;
		case 'b':
			b_path = optarg;
			break;
		case 'e':
			e_path = optarg;
			break;
		case '?':
		default:
			fprintf(stderr, "got %c for %s %c\n", c, optarg, optopt);
			e++;
		}
	}

	if (e || !b_path) {
		usage();
		return 1;
	}

	struct sys_backlight sb;
	e = sys_backlight_init(&sb, b_path);
	if (e < 0) {
		fprintf(stderr, "failed to initialize sys backlight at '%s' (%d)\n", b_path, e);
		return 2;
	}

	int ifd = open(e_path, O_RDONLY);
	if (ifd < 0) {
		fprintf(stderr, "could not open %s\n", e_path);
		return 3;
	}

	struct libevdev *dev;
	e = libevdev_new_from_fd(ifd, &dev);
	if (e != 0) {
		fprintf(stderr, "could not init %s as libevdev device (%d)\n", e_path, e);
		return 4;
	}

	return 0;
}
