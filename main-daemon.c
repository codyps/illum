/* ex: set noet sw=8 sts=8 ts=8 tw=78: */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdalign.h>

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

/* libudev */
#include <libudev.h>

/* ev */
#include "ev-ext.h"

/* ccan */
#include <ccan/pr_log/pr_log.h>
#include <ccan/tlist2/tlist2.h>
#include <ccan/str/str.h>


/*
 * sys_backlight assumes max_brightness is fixed
 */
struct sys_backlight {
	struct list_node list;

	char *path;
	int dir_fd;
	uintmax_t max_brightness;
	int brightness_fd;
	unsigned linearity;
};

struct input_dev {
	char *sys_path;

	struct illum *parent;
	struct list_node list;
	ev_io w;
	struct libevdev *dev;
};

struct illum_conf {
	// adjust the rate of brightness adjustments as a factor of the
	// current brightness level.
	unsigned linearity;
};

struct illum {
	TLIST2(struct input_dev, list) inputs;
	TLIST2(struct sys_backlight, list) backlights;

	struct ev_io w_udev;
	struct illum_conf conf;

	struct udev *udev;
	struct udev_monitor *udev_monitor;
};


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
		" -f <msec>		milliseconds to fade when dimming\n"		" -F <msec>		milliseconds to fade when brightening\n"
 */

/* Based on an example from
 * http://www.codecodex.com/wiki/Calculate_an_integer_square_root#C
 */
static uintmax_t
isqrt_umax(uintmax_t n)
{
	uintmax_t c = UINTMAX_C(1) << (CHAR_BIT * sizeof(c) / 2 - 1);
	uintmax_t g = c;

	for(;;) {
		if (g*g > n)
			g ^= c;
		c >>= 1;
		if (c == 0)
			return g;
		g |= c;
	}
}

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

struct crat {
	intmax_t  top;
	uintmax_t bot;
};
#define CRAT(_top, _bot) ((struct crat){ .top = (_top), .bot = (_bot)})
#define CRAT_FMT "(%jd/%ju)"
#define CRAT_EXP(a) (a).top, (a).bot

static struct crat
crat_add(struct crat a, struct crat b)
{
	if (a.bot == b.bot)
		return CRAT(a.top + b.top, a.bot);
	else
		return CRAT(a.top * b.bot + b.top * a.bot, a.bot * b.bot);
}

static struct crat
crat_sqrt(struct crat a)
{
	return CRAT(isqrt_umax(a.top * a.bot), a.bot);
}

static struct crat
crat_mul(struct crat a, struct crat b)
{
	return CRAT(a.top * b.top, a.bot * b.bot);
}

static uintmax_t
crat_as_num_of(struct crat a, uintmax_t b)
{
	if (a.bot == b)
		return a.top;
	else
		return a.top * b / a.bot;
}

static struct crat
crat_clamp_num(struct crat a, intmax_t low, intmax_t high)
{
	return CRAT(clamp(a.top, low, high), a.bot);
}

static struct crat
crat_clamp_unsigned_norm(struct crat a)
{
	return crat_clamp_num(a, 0, a.bot);
}

static const char *opts = "Vhl:b:";
static
void usage_(const char *pn)
{
	fprintf(stderr,
		"illum-%s\n"
		"Adjust brightness based on keypresses\n"
		"KEY_BRIGHTNESSDOWN & KEY_BRIGHTNESSUP\n"
		"\n"
		"usage: %s -[%s]\n"
		"\n"
		"options:\n"
		" -h			print this help\n"
		" -V			print version info\n"
		" -b <backlight dir>	a directory like '/sys/class/backlight/*'\n"
		" -l <linearity>	an integer indicating how many times to multiply the\n"
		"			values from the backlight by themselves to obtain a\n"
		"			reasonable approximation of real brightness\n"
		, stringify(CFG_GIT_VERSION), pn, opts);

}

#define usage() usage_(argc?argv[0]:"illum-d")

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
	close(mfd);

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

	if (!r)
		return -1;

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

#if 0
static uint32_t
isqrt(uint64_t const n)
{
    uint64_t xk = n;
    if (n == 0)
	    return 0;
    if (n == 18446744073709551615ULL)
	    return 4294967295U;
    for (;;) {
        uint64_t const xk1 = (xk + n / xk) / 2;
        if (xk1 >= xk)
            return xk;
        else
            xk = xk1;
    }
}
#endif

static struct crat
sys_backlight_brightness_get(struct sys_backlight *sb)
{
	intmax_t r = attr_read_int(sb->dir_fd, "brightness");
	if (r < 0)
		return CRAT(r, 1);

	if ((uintmax_t)r > sb->max_brightness)
		return CRAT(-5, 1);

	struct crat raw = CRAT(r, sb->max_brightness);
	struct crat brt = raw;

	unsigned i;
	for (i = 0; i < (sb->linearity - 1); i++)
		brt = crat_sqrt(brt);

	pr_debug("get: raw="CRAT_FMT", linearized="CRAT_FMT"\n", CRAT_EXP(raw), CRAT_EXP(brt));
	return brt;
}

static
int sys_backlight_brightness_set(struct sys_backlight *sb, struct crat percent, struct crat old, int dir)
{
	/* f(percent) -> setting */


	/* pretend that brightness goes up like an exponent */
	struct crat corrected = percent;
	unsigned i;
	for (i = 0; i < (sb->linearity - 1); i++)
		corrected = crat_mul(corrected, corrected);

	uintmax_t v = crat_as_num_of(corrected, sb->max_brightness);
	if (v == (uintmax_t)old.top)
		v += dir;

	pr_debug("set: input="CRAT_FMT", un-linearized="CRAT_FMT"\n",
			CRAT_EXP(percent), CRAT_EXP(corrected));
	return attr_write_int(sb->dir_fd, "brightness", v);
}

static
int sys_backlight_brightness_mod(struct sys_backlight *sb, struct crat mod)
{
	struct crat curr = sys_backlight_brightness_get(sb);
	if (curr.top < 0) {
		pr_debug("mod: error getting brightness: %jd\n", curr.top);
		return curr.top;
	}

	struct crat new = crat_clamp_unsigned_norm(crat_add(curr, mod));
	int r = sys_backlight_brightness_set(sb, new, curr, (mod.top > 0) - (mod.top < 0));
	if (r < 0)
		return r;

	return 0;
}

static
int sys_backlight_new(struct sys_backlight **sb_, const char *path, unsigned linearity)
{
	struct sys_backlight *sb = malloc(sizeof(*sb));
	if (!sb) {
		return -ENOMEM;
	}

	int r = -ENOMEM;
	sb->path = strdup(path);
	if (!sb->path)
		goto e_alloc;

	sb->dir_fd = open(sb->path, O_RDONLY | O_DIRECTORY);
	if (sb->dir_fd == -1) {
		r = -1;
		goto e_alloc_p;
	}

	r = sys_backlight_init_max_brightness(sb);
	if (r < 0) {
		goto e_close;
	}

	sb->brightness_fd = openat(sb->dir_fd, "brightness", O_RDONLY);
	if (!sb->brightness_fd) {
		r = -5;
		goto e_close;
	}
	
	pr_info("using %s as a backlight\n", path);

	sb->linearity = linearity;
	*sb_ = sb;
	return 0;

e_close:
	close(sb->dir_fd);
e_alloc_p:
	free(sb->path);
e_alloc:
	free(sb);
	return r;
}

static
void sys_backlight__delete(struct sys_backlight *sb)
{
	list_del(&sb->list);
	close(sb->dir_fd);
	free(sb->path);
	free(sb);
}

static void
illum__brightness_mod(struct illum *illum, struct crat crat)
{
	struct sys_backlight *bl;
	tlist2_for_each(&illum->backlights, bl) {
		sys_backlight_brightness_mod(bl, crat);
	}

}

static void
evdev_cb(EV_P_ ev_io *w, int revents)
{
	(void)revents;
	(void)EV_A;

	struct input_dev *id = container_of(w, struct input_dev, w);
	for (;;) {
		struct input_event ev;
		int r = libevdev_next_event(id->dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

		/* no events */
		if (r == -EAGAIN)
			break;

		/* need sync??
		 * FIXME: determine if we're handling this properly or if we
		 * even really need to handle it.
		 */
		if (r == LIBEVDEV_READ_STATUS_SYNC)
			continue;

		assert(r == LIBEVDEV_READ_STATUS_SUCCESS);

		/* On certain key pressess... */
		/* TODO: recognize held keys and dim at some to be determined
		 * rate */
		/* TODO: recognize modifier keys and dim with rate variations
		 */
		if (ev.type == EV_KEY && ev.value == 0) {
			/* TODO: allow mapping these to other key combinations */
			switch(ev.code) {
			case KEY_BRIGHTNESSUP:
				illum__brightness_mod(id->parent, CRAT(5, 100));
				break;
			case KEY_BRIGHTNESSDOWN:
				illum__brightness_mod(id->parent, CRAT(-5, 100));
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
int input_dev_new(struct input_dev **id_, const char *path, const char *sys_path EV_P__)
{
	int ifd = open(path, O_RDONLY|O_NONBLOCK);
	if (ifd < 0) {
		fprintf(stderr, "could not open %s\n", path);
		return -1;
	}

	int r = -ENOMEM;
	struct input_dev *id = malloc(sizeof(*id));
	if (!id)
		goto e_close;

	id->sys_path = strdup(sys_path);
	if (!id->sys_path)
		goto e_malloc;

	r = libevdev_new_from_fd(ifd, &id->dev);
	if (r) {
		pr_debug("could not init %s as libevdev device (%d)\n", path, r);
		r = 0;
		goto e_malloc_path;
	}

	/* Ignore devices we don't care about.
	 * TODO: make this more generic/define once
	 */
	if (!libevdev_has_event_code(id->dev, EV_KEY, KEY_BRIGHTNESSDOWN)
		&& !libevdev_has_event_code(id->dev, EV_KEY, KEY_BRIGHTNESSUP)) {
		pr_debug("input %s skipped due to lack of keys\n", path);
		r = 0;
		goto e_libevdev;
	}

	ev_io_init(&id->w, evdev_cb, ifd, EV_READ);
	ev_io_start(EV_A_ &id->w);

	pr_info("using %s as an input dev\n", sys_path);

	*id_ = id;

	return 1;
e_libevdev:
	libevdev_free(id->dev);
e_malloc_path:
	free(id->sys_path);
e_malloc:
	free(id);
e_close:
	close(ifd);
	return r;
}

static void
input_dev__delete(struct input_dev *id EV_P__)
{
	int ifd = id->w.fd;
	list_del(&id->list);
	ev_io_stop(EV_A_ &id->w);
	libevdev_free(id->dev);
	free(id->sys_path);
	free(id);
	close(ifd);
}

static void
udev_cb(EV_P_ ev_io *w, int revents)
{
	(void)revents;
	(void)EV_A;

	struct illum *illum = container_of(w, struct illum, w_udev);

	for (;;) {
		struct udev_device *dev = udev_monitor_receive_device(illum->udev_monitor);
		if (!dev)
			break;

		const char *action = udev_device_get_action(dev);
		const char *subsystem = udev_device_get_subsystem(dev);
		const char *sys_path = udev_device_get_syspath(dev);

		pr_debug("op: %s : %s\n", action, subsystem);

		if (streq(action, "add")) {
			// check if this device already exists, if so
			// ignore
			//
			// insert device into list
			if (streq(subsystem, "backlight")) {
				struct sys_backlight *bl;
				tlist2_for_each(&illum->backlights, bl) {
					if (streq(sys_path, bl->path)) {
						pr_info("backlight %s was added but already is tracked, ignoring\n", sys_path);
						goto next_dev;
					}
				}

				int r = sys_backlight_new(&bl, sys_path, illum->conf.linearity);
				if (r < 0) {
					pr_warn("failed to add new backlight %s: %d\n", sys_path, r);
					goto next_dev;
				}

				tlist2_add(&illum->backlights, bl);
			} else if (streq(subsystem, "input")) {
				struct input_dev *id;
				tlist2_for_each(&illum->inputs, id) {
					if (streq(sys_path, id->sys_path)) {
						pr_info("input %s was added but already is tracked, ignoring\n", sys_path);
						goto next_dev;
					}
				}

				const char *dev_path = udev_device_get_devnode(dev);
				if (!dev_path) {
					pr_debug("device node for %s does not exist\n", sys_path);
					goto next_dev;
				}
				int r = input_dev_new(&id, dev_path, sys_path EV_A__);
				if (r < 0) {
					pr_warn("failed to add new input %s: %d\n", sys_path, r); 
					goto next_dev;
				}

				if (r == 0)
					goto next_dev;

				id->parent = illum;
				tlist2_add(&illum->inputs, id);
			} else {
				pr_warn("unrecognized subsystem: %s\n", subsystem);
			}
		} else if (streq(action, "remove")) {
			// find device, remove
			if (streq(subsystem, "backlight")) {
				struct sys_backlight *bl;
				tlist2_for_each(&illum->backlights, bl) {
					if (streq(sys_path, bl->path)) {
						sys_backlight__delete(bl);
						goto next_dev;
					}
				}
			} else if (streq(subsystem, "input")) {
				struct input_dev *id;
				tlist2_for_each(&illum->inputs, id) {
					if (streq(sys_path, id->sys_path)) {
						input_dev__delete(id EV_A__);
						goto next_dev;
					}
				}
			} else {

			}

		} else if (streq(action, "change")) {
			// we trigger these on the backlight
		} else {
			pr_info("udev: unhandled action: %s on device %s\n", action, sys_path);
		}

next_dev:
		udev_device_unref(dev);
	}
}

static
int backlights_scan(struct illum *illum, struct udev_enumerate *bl_enum)
{
	int r = udev_enumerate_scan_devices(bl_enum);
	if (r < 0) {
		pr_warn("backlight enumerate failed: %d\n", r);
		return r;
	}

	struct udev_list_entry *list, *le;

	list = udev_enumerate_get_list_entry(bl_enum);

	udev_list_entry_foreach(le, list) {
		const char *path = udev_list_entry_get_name(le);
		struct sys_backlight *sb;
		int e = sys_backlight_new(&sb, path, illum->conf.linearity);
		if (e < 0) {
			fprintf(stderr, "failed to initialize sys backlight at '%s' (%d)\n", path, e);
			continue;
		}

		pr_debug("using '%s' as a backlight, max_brightness = %jd\n",
				path, sb->max_brightness);

		tlist2_add(&illum->backlights, sb);
	}

	return 0;
}

static
int inputs_scan(struct illum *illum, struct udev_enumerate *input_enum EV_P__)
{
	int r = udev_enumerate_scan_devices(input_enum);
	if (r < 0) {
		pr_warn("input enumerate failed: %d\n", r);
		return r;
	}

	struct udev_list_entry *list, *le;
	list = udev_enumerate_get_list_entry(input_enum);

	udev_list_entry_foreach(le, list) {
		const char *sys_path = udev_list_entry_get_name(le);
		struct udev_device *dev = udev_device_new_from_syspath(illum->udev, sys_path);

		pr_debug("input %s devpath=%s devtype=%s\n", sys_path, udev_device_get_devpath(dev), udev_device_get_devtype(dev));

		const char *path = udev_device_get_devnode(dev);
		if (!path) {
			pr_debug("device node for %s does not exist\n", sys_path);
			goto next;
		}

		struct input_dev *id;
		r = input_dev_new(&id, path, sys_path EV_A__);
		if (r < 0) {
			pr_warn("input_dev_new(%s) failed: %d\n", sys_path, r);
			goto next;
		}

		if (r != 1)
			goto next;
		
		id->parent = illum;
		tlist2_add(&illum->inputs, id);
next:
		udev_device_unref(dev);
	}

	return 0;
}

int main(int argc, char **argv)
{
	int c, e = 0;
	struct illum illum = {
		.conf = {
			.linearity = 2,
		}
	};
	tlist2_init(&illum.inputs);
	tlist2_init(&illum.backlights);

	while ((c = getopt(argc, argv, opts)) != -1) {
		switch(c) {
		case 'h':
			usage();
			return 0;
		case 'l': {
			long x = strtol(optarg, NULL, 0);
			if (x < 0) {
				e++;
				fprintf(stderr, "E: -l must be positive, got %ld\n", x);
				break;
			}

			illum.conf.linearity = x;
			break;
		}
		case 'V':
			puts("illum-" stringify(CFG_GIT_VERSION));
			return 0;
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

	illum.udev = udev_new();
	if (!illum.udev) {
		pr_error("udev_new() failed\n");
		return 3;
	}

	struct udev_enumerate *bl_enum = udev_enumerate_new(illum.udev);
	if (!bl_enum) {
		pr_error("udev_enumerate_new() failed\n");
		return 4;
	}


	int r = udev_enumerate_add_match_subsystem(bl_enum, "backlight");
	if (r < 0) {
		pr_error("udev_enumerate_add_match_subsystem failed: %d\n", r);
		return 5;
	}

	struct udev_enumerate *input_enum = udev_enumerate_new(illum.udev);
	if (!input_enum) {
		pr_error("udev enum new failed\n");
		return 5;
	}

	r = udev_enumerate_add_match_subsystem(input_enum, "input");
	if (r < 0) {
		pr_error("udev_enumerate_add_match_subsystem failed: %d\n", r);
		return 5;
	}

	illum.udev_monitor = udev_monitor_new_from_netlink(illum.udev, "udev");
	if (!illum.udev_monitor) {
		pr_error("udev_monitor_new_from_netlink() failed\n");
		return 6;
	}

	r = udev_monitor_filter_add_match_subsystem_devtype(illum.udev_monitor, "backlight", NULL);
	if (r < 0) {
		pr_error("udev_monitor_filter_add_match_subsystem_devtype backlight failed: %d\n", r);
		return 7;
	}

	r = udev_monitor_filter_add_match_subsystem_devtype(illum.udev_monitor, "input", NULL);
	if (r < 0) {
		pr_error("udev_monitor_filter_add_match_subsystem_devtype input failed: %d\n", r);
		return 7;
	}

	r = udev_monitor_enable_receiving(illum.udev_monitor);
	if (r < 0) {
		pr_error("udev_monitor_enable_receiving failed: %d\n", r);
		return 8;
	}

	r = backlights_scan(&illum, bl_enum);
	if (r < 0) {
		pr_error("backlight initial scan failed: %d\n", r);
		return 9;
	}

	r = inputs_scan(&illum, input_enum EV_DEFAULT__);
	if (r < 0) {
		pr_error("input initial scan failed: %d\n", r);
		return 9;
	}

	ev_io_init(&illum.w_udev, udev_cb, udev_monitor_get_fd(illum.udev_monitor), EV_READ);
	ev_io_start(EV_DEFAULT_ &illum.w_udev);

	ev_run(EV_DEFAULT_ 0);

	return 0;
}
