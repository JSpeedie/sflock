/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <X11/xpm.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/dpms.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

char* wrn_error_bg = \
	"warning: could not read provided error background image\n";

/* Variable definitions {{{ */
char* passchar = "*";
char* fontname = "*-helvetica-bold-r-normal-*-*-120-*-*-*-*-iso8859-1";
// char* fontname = "-*-tamzen-medium-*-*-*-17-*-*-*-*-*-*-*";
char* username;
// show/hide element variables
int show_name = 1;
int show_line = 1;
int show_password = 1;
// element variables
int use_line_length = 0;
int new_line_length = 100;
int line_length;
/* Location variables */
// -x and -y variables
int use_x = 0, use_y = 0;
int new_x = 0, new_y = 0;
// --name-[xy], --line-[xy], --password-[xy] variables
int use_name_x = 0, use_name_y = 0;
int use_line_x = 0, use_line_y = 0;
int use_password_x = 0, use_password_y = 0;
int new_name_x = 0, new_name_y = 0;
int new_line_x = 0, new_line_y = 0;
int new_password_x = 0, new_password_y = 0;
char* name_file;
char name_file_contents[1000];
int use_name_file = 0;
// --x-shift and --y-shift variables
int x_shift = 0, y_shift = 0;
// image variables
int use_b_image = 0;
char* b_image_loc = "";
int use_e_b_image = 0;
char* e_b_image_loc = "";
Pixmap p;
Pixmap bg;
/* main vars */

char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
char buf[32], passwd[256], passdisp[256];
int num, screen, width, height, update, sleepmode, term, pid;

#ifndef HAVE_BSD_AUTH
    const char *pws;
#endif
    unsigned int len;
    Bool running = True;
    Cursor invisible;
    Display *dpy;
    KeySym ksym;
    Pixmap pmap;
    Window root, w;
    XColor black, red, dummy;
    XEvent ev;
    XSetWindowAttributes wa;
    XFontStruct* font;
    GC gc;
    XGCValues values;
/* update_screen vars */

int x, y, mid_y, dir, ascent, descent;
XCharStruct overall;

/* End of Variable definitions }}} */


static void
die(const char *errstr, ...) {
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

#ifndef HAVE_BSD_AUTH
static const char *
get_password() { /* only run as root */
    const char *rval;
    struct passwd *pw;

	if(geteuid() != 0)
		die("sflock: cannot retrieve password entry " \
			"(make sure to suid sflock)\n");
    pw = getpwuid(getuid());
    endpwent();
    rval =  pw->pw_passwd;

#if HAVE_SHADOW_H
    {
        struct spwd *sp;
        sp = getspnam(getenv("USER"));
        endspent();
        rval = sp->sp_pwdp;
    }
#endif

    /* drop privileges temporarily */
    if (setreuid(0, pw->pw_uid) == -1)
        die("sflock: cannot drop privileges\n");
    return rval;
}
#endif

void read_file(void) {
	/*
	 * Open file in read only mode, if the file exists, loop through all
	 * the chars in the file. If the current char is NOT a newline char, add it
	 * to the char array. New lines aren't drawn by the XDraw function used
	 * to draw the username text.
	 */
	int c;
	FILE *file;
	file = fopen(name_file, "r");
	if (file) {
		int j = 0;
		while (((c = getc(file)) != EOF) && (j < sizeof(name_file_contents))) {
			if (c != '\n') {
				name_file_contents[j] = c;
				j += 1;
			}
		}
		/* Unnecessary on first read, but necessary when "refilling"  array */
		name_file_contents[j] = '\0';
		fclose(file);
	}
}

void print_help(void) {
	// c:f:nlpoL:hvx:y:X:Y:A:B:C:D:E:F:N:i:e:
	printf("sflock\n\tusage: " \
		"[ -c | -f | -n | -l | -p | -o | -L | -h | -v | -x | -y | -X | -Y | -A | -B | -C | -D | -E | -F | -N | -i | -e | -s | -a ]");

	printf("\n\n\t-c, --password-char passchars\n\t\tTakes a string parameter. " \
		"The provided string/char will be used to represent the characters " \
		"in the password field. For example, when not used, " \
		"each character you've entered will be represented by '*'. " \
		"If you enter 'sflock -c this', the first character " \
		"will be shown as 't'. The second character you enter will " \
		"be shown as 'h' and so on and so forth. The characters " \
		"('this') will be repeated once they run out. For example, at 12 " \
		"characters entered, the password field will show " \
		"'thisthisthis' (provided you haven't hidden the password " \
		"field). Best used for making it look like your password " \
		"isn't hidden by entering something like 'sflock -c " \
		"password1234' or 'sflock -c jfkldhfuhwojnfjkdnja' to " \
		"confuse anyone looking over your shoulder.");

	printf("\n\n\t-f, --font-name fontname\n\t\tTakes one string parameter that " \
		"represents the font you want to use. Uses X Logical Font " \
		"Description. I'll try to add XFT support at a later date cause " \
		"XLFD can be a pain.");

	printf("\n\n\t-n, --hide-name\n\t\tsflock will not show the username field " \
		"at the lock screen. Good if your username is something stupid ;)");

	printf("\n\n\t-l, --hide-line\n\t\tsflock will not show the line between " \
		"the username field and the password field.");

	printf("\n\n\t-p, --hide-password\n\t\tsflock will not show the password " \
		"field at the lock screen. Good if you don't want people to see " \
		"your terrible wpm :^[");

	printf("\n\n\t-o, --password-only\n\t\tEquivalent to --hide-line " \
		"--hide-name.");

	printf("\n\n\t-L, --line-length length\n\t\tTakes one int parameter. " \
		"Sets the horizontal length of the " \
		"line field at the lock screen in pixels.");

	printf("\n\n\t-h, --help\n\t\tPrints this help output. Tells you what each " \
		"flag does and what parameters they take.");

	printf("\n\n\t-v, --version\n\t\tPrints version info.");

	printf("\n\n\t-x, --x-coord x_coordinate\n\t\tTakes one int parameter. " \
		"Sets the 'override' x-value. This will set " \
		"the x coordinate of all the fields (name, line and password). " \
		"Useful for left justifying the fields. If you set the individual " \
		"field's x coordinates, that field will use that x rather than the " \
		"the one set here. This is to make it easier to set the coordinates" \
		"of the fields if you want two of them to be the same.");

	printf("\n\n\t-y, --y-coord y_coordinate\n\t\tTakes one int parameter. " \
		"Sets the 'override' y-value. This will set " \
		"the y coordinate of all the fields (name, line and password). " \
		"Useful for getting all elements in a line... If you would ever want " \
		"that... If you set the individual field's y coordinates, that " \
		"field will use that y rather than the the one set here. This is " \
		"to make it easier to set the coordinates of the fields if you " \
		"want two of them to be the same.");

	printf("\n\n\t-X, --x-shift horizontal_shift\n\t\tTakes one int parameter. " \
		"Shifts the username, line and password field x pixels to " \
		"the right (from the x value which is centered by default).");

	printf("\n\n\t-Y, --y-shift vertical_shift\n\t\tTakes one int parameter. " \
		"Shifts the username, line and password field y pixels " \
		"upwards (from the y value which is centered by default).");

	printf("\n\n\t-A, --name-x x_coordinate\n\t\tTakes one int parameter. " \
		"Sets the x coordinate for the name field. Overrides any other flag " \
		"that sets the name field's x coordinate.");

	printf("\n\n\t-B, --line-x x_coordinate\n\t\tTakes one int parameter. " \
		"Sets the x coordinate for the line field. Overrides any other flag " \
		"that sets the line field's x coordinate.");

	printf("\n\n\t-C, --password-x x_coordinate\n\t\tTakes one int parameter. " \
		"Sets the x coordinate for the password field. Overrides any other " \
		"flag that sets the password field's x coordinate.");

	printf("\n\n\t-D, --name-y y_coordinate\n\t\tTakes one int parameter. " \
		"Sets the y coordinate for the name field. Overrides any other " \
		"flag that sets the name field's y coordinate.");

	printf("\n\n\t-E, --line-y y_coordinate\n\t\tTakes one int parameter. " \
		"Sets the y coordinate for the line field. Overrides any other " \
		"flag that sets the line field's y coordinate.");

	printf("\n\n\t-F, --password-y y_coordinate\n\t\tTakes one int parameter. " \
		"Sets the y coordinate for the password field. Overrides any other " \
		"flag that sets the password field's y coordinate.");

	printf("\n\n\t-N, --name-file file_path\n\t\tTakes one string parameter " \
		"in the format of a file path. Reads the contents of the file and " \
		"outputs it to the username field. The idea behind this is you can " \
		"write anything to the file and sflock will read it and display it " \
		"(except \\n. Maybe I'll add that functionality in the future). " \
		"One idea would be to make a bash script that prints $(date) to a " \
		"file every second or so and then your lock screen will show the " \
		"date. Obviously you can do much more advanced things with this " \
		"cough cough take a look at your lemonbar scripts.");

	printf("\n\n\t-i, --background-image file_path\n\t\tTakes one string " \
		"parameter in the format of a file path. If the file path leads " \
		"to an xpm file, the file will be read and the background of " \
		"the lock screen will be that image. If the image size is smaller " \
		"than your screen size, the image will be tiled. I plan on adding " \
		"for other file types in the future.");

	printf("\n\n\t-e, --error-image file_path\n\t\tTakes one string " \
		"parameter in the format of a file path. If the file path leads " \
		"to an xpm file, the file will be read. If the user enters the " \
		"password incorrectly, this image will become the background. " \
		"Useful for telling if someone tried to login to your system when " \
		"you were away, or, idk. Telling yourself you're bad every time " \
		"you mess up your password. If the image size is smaller than your " \
		"screen size, the image will be tiled. I plan on adding for other " \
		"file types in the future." \
		);

	printf("\n");
	exit(0);
}

/* Draw helper functions (draw_name, draw_line, draw_password) {{{ */
void draw_name(void) {
		/*
		* If the user set a name x value, use that for the x.
		* If the user did not, use the "override" x if it was set.
		* If neither were set, use the default value; centered on the
		* screen. Same applies for the y, except the default for the y
		* is just above the center of the screen.
		*/
		if (use_name_x) x = new_name_x;
		else if (use_x) x = new_x;
		// to do: write comment detailing diff between
		// width and overall.width
		else {
			if (use_name_file) {
				x = ((width - XTextWidth(font, name_file_contents, \
					strlen(name_file_contents))) / 2);
			}
			else
				x = ((width - XTextWidth(font, username, \
					strlen(username))) / 2);
		}

		if (use_name_y) y = new_name_y;
		else if (use_y) y = new_y;
		else y = mid_y - ascent - 20;

		/* Draw username on the lock screen */
		if (use_name_file) {
			XDrawString(dpy, w, gc, x + x_shift, y, \
				name_file_contents, strlen(name_file_contents));
		}
		else {
			XDrawString(dpy, w, gc, x + x_shift, y, \
				username, strlen(username));
		}
}

void draw_line(void) {
		/*
		* If the user has set a custom line length, make the line that
		* length. If the user has NOT set a custom line length, default
		* to a line 2/8ths the size of the screen.
		*/
		if (use_line_length) line_length = new_line_length;
		else line_length = (width * 2 / 8);

		/*
		* If the user set a line x value, use that for the x.
		* If the user did not, use the "override" x if it was set.
		* If neither were set, use the default value; centered on the
		* screen. Same applies for the y, except the default for the y
		* is just above the center of the screen.
		*/
		if (use_line_x) x = new_line_x;
		else if (use_x) x = new_x;
		else x = (width * 3 / 8);

		if (use_line_y) y = new_line_y;
		else if (use_y) y = new_y;
		else y = mid_y - ascent - 10;

		/*
		* The line is "anchored" at the top left. So the x given is the
		* left x coordinate.
		*/
		XDrawLine(dpy, w, gc, x + x_shift, y, x + x_shift + line_length, y);
}

void draw_password(void) {
		/*
		* If the user set a password x, use that for the x.
		* If the user did not, use the "override" x if it was set.
		* If neither were set, use the default value; centered on the
		* screen. Same applies for the y, except the default for the y
		* is just below the center of the screen.
		*/

		// to do: write comment detailing diff between
		// width and overall.width
		if (use_password_x) x = new_password_x;
		else if (use_x) x = new_x;
		else x = (width - overall.width) / 2;

		if (use_password_y) y = new_password_y;
		else if (use_y) y = new_y;
		else y = mid_y;

		// Draw password entry on the lock screen
		XDrawString(dpy, w, gc, x + x_shift, y, \
			passdisp, len);
}

void draw_error_bg(void) {
	/* If the user specified an error background image */
	if (use_e_b_image) {
		/*
		* Read user specified .xpm file as a Pixmap to 'p'.
		* If the file was read successfully, set the background,
		* to the user specified image. If the file was not read
		* successfully, print a warning message.
		*/
		int retval = XpmReadFileToPixmap(dpy, w, e_b_image_loc, \
			&p, NULL, NULL);

		if (retval == 0) XSetWindowBackgroundPixmap(dpy, w, p);
		else printf(wrn_error_bg);
	}
	else {
		// change background on wrong password
		XSetWindowBackground(dpy, w, red.pixel);
	}

	// Flush to update background
	XFlush(dpy);
}
/* }}} */

// {{{
void update_screen(void) {

	XClearWindow(dpy, w);
	XTextExtents (font, passdisp, len, &dir, &ascent, \
	&descent, &overall);

	mid_y = (height + ascent - descent) / 2;

	/* If the user HASN'T set the username to be hidden */
	if (show_name) {
		draw_name();
	}

	// If the user HASN'T set the line to be hidden
	if (show_line) {
		draw_line();
	}

	/* If the user HASN'T set the password field to be hidden */
	if (show_password) {
		draw_password();
	}
}
// }}}

int
main(int argc, char **argv) {
	int opt;
	/* still to do:
		x-coord and x-shift should be for individual part? (password field,
		line and name?) y-shift isn't implemented either in the while loop
		or at all a lot of these need proper implementation.
	*/
	struct option opt_table[] = {
		{ "password-char",		required_argument,	NULL,	'c' },
		{ "font-name",			required_argument,	NULL,	'f' },
		/* show/hide element options */
		{ "hide-name",			no_argument,		NULL,	'n' },
		{ "hide-line",			no_argument,		NULL,	'l' },
		{ "hide-password",		no_argument,		NULL,	'p' },
		{ "password-only",		no_argument,		NULL,	'o' },
		/* element options */
		{ "line-length",		required_argument,	NULL,	'L' },
		/* help and info options */
		{ "help",				no_argument,		NULL,	'h' },
		{ "version",			no_argument,		NULL,	'v' },
		/* location options */
		{ "x-coord",			required_argument,	NULL,	'x' },
		{ "y-coord",			required_argument,	NULL,	'y' },
		{ "x-shift",			required_argument,	NULL,	'X' },
		{ "y-shift",			required_argument,	NULL,	'Y' },
		{ "name-x",				required_argument,	NULL,	'A' },
		{ "line-x",				required_argument,	NULL,	'B' },
		{ "password-x",			required_argument,	NULL,	'C' },
		{ "name-y",				required_argument,	NULL,	'D' },
		{ "line-y",				required_argument,	NULL,	'E' },
		{ "password-y",			required_argument,	NULL,	'F' },
		{ "name-file",			required_argument,	NULL,	'N' },
		/* image options */
		{ "background-image",	required_argument,	NULL,	'i' },
		{ "error-image",		required_argument,	NULL,	'e' },
		{ 0, 0, 0, 0 }
	};

	while ((opt = getopt_long(argc, argv, \
		"c:f:nlpoL:hvx:y:X:Y:A:B:C:D:E:F:N:i:e:", opt_table, NULL)) != -1) {
		switch (opt) {
			case 'c': passchar = optarg; break;
			case 'f': fontname = optarg; break;
			// show/hide element options
			case 'n': show_name = 0; break;
			case 'l': show_line = 0; break;
			case 'p': show_password = 0; break;
			case 'o': show_line = 0; show_name = 0; break;
			// element options
			case 'L':
				use_line_length = 1;
				new_line_length = atoi(optarg); break;
			// help and info options
			case 'h': print_help(); break;
			case 'v':
				die("sflock-"VERSION", © 2015 Ben Ruijl, " \
				"JSpeedie\n"); break;
			// location options
			case 'x':
				use_x = 1;
				new_x = atoi(optarg); break;
			case 'y':
				use_y = 1;
				new_y = atoi(optarg); break;
			case 'X': x_shift = atoi(optarg); break;
			case 'Y': y_shift = atoi(optarg); break;
			// element x coordinates
			case 'A':
				use_name_x = 1;
				new_name_x = atoi(optarg); break;
			case 'B':
				use_line_x = 1;
				new_line_x = atoi(optarg); break;
			case 'C':
				use_password_x = 1;
				new_password_x = atoi(optarg); break;
			// element y coordinates
			case 'D':
				use_name_y = 1;
				new_name_y = atoi(optarg); break;
			case 'E':
				use_line_y = 1;
				new_line_y = atoi(optarg); break;
			case 'F':
				use_password_y = 1;
				new_password_y = atoi(optarg); break;
			case 'N':
				use_name_file = 1;
				name_file = optarg; break;
			// image options
			case 'i':
				use_b_image = 1;
				b_image_loc = optarg; break;
			case 'e':
				use_e_b_image = 1;
				e_b_image_loc = optarg; break;
		}
	}

	/* If the user set the -N option, read the file they specified */
	if (use_name_file) {
		read_file();
	}

    // fill with password characters
    for (int i = 0; i < sizeof passdisp; i+= strlen(passchar))
        for (int j = 0; j < strlen(passchar) && i + j < sizeof passdisp; j++)
            passdisp[i + j] = passchar[j];


    /* disable tty switching */
    if ((term = open("/dev/console", O_RDWR)) == -1) {
        perror("error opening console");
    }

    if ((ioctl(term, VT_LOCKSWITCH)) == -1) {
        perror("error locking console");
    }

    /* deamonize */
    pid = fork();
    if (pid < 0)
        die("Could not fork sflock.");
    if (pid > 0)
        exit(0); // exit parent

#ifndef HAVE_BSD_AUTH
    pws = get_password();
    username = getpwuid(geteuid())->pw_name;
#else
    username = getlogin();
#endif

    if(!(dpy = XOpenDisplay(0)))
        die("sflock: cannot open dpy\n");

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    width = DisplayWidth(dpy, screen);
    height = DisplayHeight(dpy, screen);

    wa.override_redirect = 1;
    wa.background_pixel = XBlackPixel(dpy, screen);
    w = XCreateWindow(dpy, root, 0, 0, width, height,
            0, DefaultDepth(dpy, screen), CopyFromParent,
            DefaultVisual(dpy, screen), CWOverrideRedirect | CWBackPixel, &wa);

    XAllocNamedColor(dpy, DefaultColormap(dpy, screen), \
		"orange red", &red, &dummy);
    XAllocNamedColor(dpy, DefaultColormap(dpy, screen), \
		"black", &black, &dummy);
    pmap = XCreateBitmapFromData(dpy, w, curs, 8, 8);
    invisible = XCreatePixmapCursor(dpy, pmap, pmap, &black, &black, 0, 0);
    XDefineCursor(dpy, w, invisible);
    XMapRaised(dpy, w);

    font = XLoadQueryFont(dpy, fontname);

    if (font == 0) {
        die("error: could not find font. Try using a full description.\n");
    }

    gc = XCreateGC(dpy, w, (unsigned long)0, &values);
	if (use_b_image) {
		// Read user specified .xpm file as a Pixmap to 'p'
		int retval = XpmReadFileToPixmap (dpy, w, b_image_loc, \
			&bg, NULL, NULL);
		// If reading the pixmap was successful
		if (retval == 0) XSetWindowBackgroundPixmap(dpy, w, bg);
		else printf("warning: could not read " \
			"provided background image\n");
	}
    XSetFont(dpy, gc, font->fid);
    XSetForeground(dpy, gc, XWhitePixel(dpy, screen));

	/* tries to grab the mouse every millisecond? Attempted 1000 times? */
    for(len = 1000; len; len--) {
		printf("len %d\n", len);
        if(XGrabPointer(dpy, root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
            break;
        usleep(1000);
    }

	/* seems to be the same as above. Tries to grab the keyboard every
	 * millisecond. Tries 1000 times. Difference is that after this loop
	 * it sets "running" to true only if len > 0 (if the keyboard was grabbed
	 * within 1000 attempts)
	 */
    if((running = running && (len > 0))) {
        for(len = 1000; len; len--) {
            if(XGrabKeyboard(dpy, root, True, GrabModeAsync, \
			GrabModeAsync, CurrentTime) == GrabSuccess)
                break;
            usleep(1000);
        }
        running = (len > 0);
    }

    len = 0;
    XSync(dpy, False);
    update = True;
    sleepmode = False;

    /* main event loop */
	/* while running != 0 */
	int thing = 0;
	/* while the user has not entered the correct password */
    while (running) {
		printf("while\n");

		/* re-read the file in case it's changed */
		if (use_name_file) read_file();
		/* Draw the name, line, and password */
		if (update) update_screen();

		// If the user pressed Esc, sleep (screen goes black)
		if (sleepmode) {
			printf("sleeping thingy\n");
			DPMSEnable(dpy);
			DPMSForceLevel(dpy, DPMSModeOff);
			XFlush(dpy);
		}

		/* If there are more than 0 X events yet to be removed from the queue */
		if (XPending(dpy) > 0) {
			printf("XPending triggered :^]\n");
			/* Set "ev" to have all the XEvent info */
			XNextEvent(dpy, &ev);
			// If the mouse was moved, wake up
			if (ev.type == MotionNotify) sleepmode = False;

			if(ev.type == KeyPress) {
				printf("keypress is keypress\n");
				sleepmode = False;

				buf[0] = 0;
				num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
				if(IsKeypadKey(ksym)) {
					if(ksym == XK_KP_Enter)
						ksym = XK_Return;
					else if(ksym >= XK_KP_0 && ksym <= XK_KP_9)
						ksym = (ksym - XK_KP_0) + XK_0;
				}
				if(IsFunctionKey(ksym) || IsKeypadKey(ksym) || IsMiscFunctionKey(ksym) || IsPFKey(ksym) || IsPrivateKeypadKey(ksym)) {
					printf("jfkldsjkfjdklasjfkljdksjfklj is function\n");
					continue;
				}

				switch(ksym) {
					case XK_Return:
						passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
	running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
	running = strcmp(crypt(passwd, pws), pws);
#endif
						printf("running after checking pass %d\n", running);
						// If the password the user entered was incorrect
						if (running != 0) draw_error_bg();
						len = 0;
						break;
					case XK_Escape:
						if (DPMSCapable(dpy)) sleepmode = True;
						len = 0;
						break;
					case XK_BackSpace:
						if(len) --len;
						break;
					default:
						if(num && !iscntrl((int) buf[0]) && (len + num < sizeof passwd)) {
							memcpy(passwd + len, buf, num);
							len += num;
						}
						break;
				}
			}
			// update = True; // show changes
		}
		/*
		 * If there are no X events to clear, pause very briefly before running
		 * through the loop again.
		 * to do: should probably allow the user to set the refresh speed
		 * in case their system is slow. Maybe add option to not redraw screen
		 * at all (except on XEvent circa before I forked)
		 */
		else {
			usleep(1000);
		}
		// update = True; // show changes
		printf("\nthing %d\n", thing);
		thing = thing + 1;
		// I've locked myself out of my system once by accident and I'm not
		// letting that happen again. Until I finish my work on the main loop,
		// this remains
		if (thing >= 500000) break;
    }

    /* free and unlock */
    setreuid(geteuid(), 0);
    if ((ioctl(term, VT_UNLOCKSWITCH)) == -1) {
        perror("error unlocking console");
    }

    close(term);
    setuid(getuid()); // drop rights permanently

    XUngrabPointer(dpy, CurrentTime);
    XFreePixmap(dpy, pmap);
	if (use_b_image)
		XFreePixmap(dpy, bg);
	if (use_e_b_image)
    	XFreePixmap(dpy, p);
    XFreeFont(dpy, font);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
    return 0;
}
