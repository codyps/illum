/* ex: set noet sw=8 sts=8 ts=8 tw=78: */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* posix */
#include <unistd.h> /* getopt(), etc */
#include <errno.h> /* EAGAIN */

/* opendir() */
#include <sys/types.h>
#include <dirent.h>

/* open() */
#include <sys/stat.h>
#include <fcntl.h>

/* libevdev */
#include <libevdev/libevdev.h>

/* ev */
#include "ev-ext.h"


/* ccan */
#include <ccan/pr_log/pr_log.h>


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
intmax_t attr_read_int(int at_fd, const char *path)
{
	int mfd = openat(at_fd, path, O_RDONLY);
	if (mfd == -1)
		return -2;

	char buf[15];
	int r = read(mfd, buf, sizeof(buf) - 1);
	close(mfd);

	if (r == -1)
		return -3;

	buf[r] = '\0';

	intmax_t res;
	r = sscanf(buf, "%jd", &res);
	if (r != 1)
		return -4;

	return res;
}

static
int attr_write_int(int at_fd, const char *path, intmax_t v)
{
	int mfd = openat(at_fd, path, O_WRONLY);
	if (mfd == -1)
		return -2;

	char buf[15];
	int l = snprintf(buf, sizeof(buf), "%jd", v);

	ssize_t r = write(mfd, buf, l);
	if (r == -1)
		return -3;

	return 0;
}

static
int sys_backlight_init_max_brightness(struct sys_backlight *sb)
{
	intmax_t r = attr_read_int(sb->dir_fd, "max_brightness");
	if (r < 0)
		return r;

	if (!sb->max_brightness)
		return -1;

	if (sb->max_brightness >= UINTMAX_MAX / 100)
		return -2;

	sb->max_brightness = r;
	return 0;
}

/*
 * Divide positive or negative dividend by positive divisor and round
 * to closest integer. Result is undefined for negative divisors and
 * for negative dividends if the divisor variable type is unsigned.
 */
#define DIV_ROUND_CLOSEST(x, divisor)(			\
{							\
	__typeof__(x) __x = x;				\
	__typeof__(divisor) __d = divisor;			\
	(((__typeof__(x))-1) > 0 ||				\
	 ((__typeof__(divisor))-1) > 0 || (__x) > 0) ?	\
		(((__x) + ((__d) / 2)) / (__d)) :	\
		(((__x) - ((__d) / 2)) / (__d));	\
}							\
)

/*
 * min()/max()/clamp() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x, y) ({				\
	__typeof__(x) _min1 = (x);			\
	__typeof__(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({				\
	__typeof__(x) _max1 = (x);			\
	__typeof__(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })

/**
 * clamp - return a value clamped to a given range with strict typechecking
 * @val: current value
 * @lo: lowest allowable value
 * @hi: highest allowable value
 *
 * This macro does strict typechecking of lo/hi to make sure they are of the
 * same type as val.  See the unnecessary pointer comparisons.
 */
#define clamp(val, lo, hi) min((__typeof__(val))max(val, lo), hi)

static
int sys_backlight_brightness_get(struct sys_backlight *sb)
{
	intmax_t r = attr_read_int(sb->dir_fd, "brightness");
	if (r < 0)
		return r;

	if ((uintmax_t)r > sb->max_brightness)
		return -5;

	if (r >= UINTMAX_MAX / 100)
		return -6;

	int res = DIV_ROUND_CLOSEST((uintmax_t)r * 100, sb->max_brightness);
	return res;
}

static
int sys_backlight_brightness_set(struct sys_backlight *sb, unsigned percent)
{
	/* XXX: overflow danger
	 * sb->max_brightness * percent < UINTMAX_MAX
	 * sb->max_brightness < UINTMAX_MAX / percent
	 */
	if (percent > 100)
		percent = 100;
	uintmax_t v = sb->max_brightness * percent / 100;
	return attr_write_int(sb->dir_fd, "brightness", v);
}

static
int sys_backlight_brightness_mod(struct sys_backlight *sb, int percent)
{
	int curr = sys_backlight_brightness_get(sb);
	if (curr < 0)
		return curr;

	curr += percent;
	curr = clamp(curr, 0, 100);
	int r = sys_backlight_brightness_set(sb, curr);
	if (r < 0)
		return r;

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

struct input_dev {
	ev_io w;
	struct libevdev *dev;
	struct sys_backlight *bl;
};

static void
evdev_cb(EV_P_ ev_io *w, int revents)
{
	struct input_dev *id = (struct input_dev *)w;
	for (;;) {
		struct input_event ev;
		int r = libevdev_next_event(id->dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
		if ((r == 1) || (r == -EAGAIN))
			continue;

		if (r != 0)
			break;

		/* On certain key pressess... */
		/* TODO: recognize held keys and dim at some to be determined
		 * rate */
		/* TODO: recognize modifier keys and dim with rate variations
		 */
		if (ev.type == EV_KEY && ev.value == 0) {
			/* TODO: allow mapping these to other key combinations */
			switch(ev.code) {
			case KEY_BRIGHTNESSUP:
				sys_backlight_brightness_mod(id->bl, +5);
				break;
			case KEY_BRIGHTNESSDOWN:
				sys_backlight_brightness_mod(id->bl, -5);
				break;
			}

		}

		pr_devel("Event: %s %s %d\n",
				libevdev_event_type_get_name(ev.type),
				libevdev_event_code_get_name(ev.type, ev.code),
				ev.value);
	}
}

static
int input_dev_init(struct input_dev *id, int at_fd, const char *path, struct sys_backlight *sb EV_P__)
{
	int ifd = openat(at_fd, path, O_RDONLY|O_NONBLOCK);
	if (ifd < 0) {
		fprintf(stderr, "could not open %s\n", path);
		return -1;
	}

	int r = libevdev_new_from_fd(ifd, &id->dev);
	if (r) {
		pr_devel("could not init %s as libevdev device (%d)\n", path, r);
		close(ifd);
		return -2;
	}

	/* Ignore devices we don't care about.
	 * TODO: make this more generic/define once
	 */
	if (!libevdev_has_event_code(id->dev, EV_KEY, KEY_BRIGHTNESSDOWN)
		&& !libevdev_has_event_code(id->dev, EV_KEY, KEY_BRIGHTNESSUP)) {
		close(ifd);
		return -3;
	}

	id->bl = sb;

	ev_io_init(&id->w, evdev_cb, ifd, EV_READ);
	ev_io_start(EV_A_ &id->w);

	pr_debug("I: using %s as an input dev\n", path);

	return 0;
}

int main(int argc, char **argv)
{
	int c, e = 0;
	const char *b_path = "/sys/class/backlight/intel_backlight";

	while ((c = getopt(argc, argv, opts)) != -1) {
		switch(c) {
		case 'h':
			usage();
			return 0;
		case 'b':
			b_path = optarg;
			break;
		case '?':
		default:
			fprintf(stderr, "got %c for %s %c\n", c, optarg, optopt);
			e++;
		}
	}

	if (e) {
		usage();
		return 1;
	}

	/*
	 * Backlight
	 */
	struct sys_backlight sb;
	e = sys_backlight_init(&sb, b_path);
	if (e < 0) {
		fprintf(stderr, "failed to initialize sys backlight at '%s' (%d)\n", b_path, e);
		return 2;
	}

	struct ev_loop *loop = EV_DEFAULT;

	/*
	 * Input dev
	 */
	size_t ev_sources = 0;
	const char *i_path = "/dev/input";
	DIR *idir = opendir(i_path);
	if (!idir) {
		fprintf(stderr, "cannot open %s dir: %s\n",
				i_path, strerror(errno));
		return 1;
	}
	ssize_t name_max = pathconf(i_path, _PC_NAME_MAX);
	if (name_max == -1)
		name_max = 255;
	size_t len = offsetof(struct dirent, d_name) + name_max + 1;
	uint8_t entry_buf[len];
	struct dirent  *res;
	for (;;) {
		int r = readdir_r(idir, (struct dirent *)entry_buf, &res);
		if (r) {
			fprintf(stderr, "readdir_r failure: %s\n",
					strerror(errno));
			return 1;
		}

		if (!res)
			break;

		struct input_dev *id = malloc(sizeof(*id));
		e = input_dev_init(id, dirfd(idir), res->d_name, &sb EV_A__);
		if (e < 0)
			continue;

		ev_sources++;
	}

	if (ev_sources == 0) {
		fprintf(stderr, "could not find any input devices for the keys we need\n");
		return 1;
	}

	ev_run(loop, 0);

	return 0;
}
