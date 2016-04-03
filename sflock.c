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
        die("sflock: cannot retrieve password entry (make sure to suid sflock)\n");
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

void printHelp() { 
	die("sflock" \
		"\n\tusage: [ -c | -f | -n | -l | -p | -o | -h | -v | -x | -s | -a ]" \
		"\n\t[-v] Prints version info." \
		"\n\t[-c passchars] Takes a string parameter. The provided string/char will be used " \
		"to represent one character entry in the password field. For example, when " \
		"left unchanged, each character you've entered will be represented by \"*\"." \
		"\n\t[-f fontname] Takes one string parameter that represents the font you want to " \
		"use. Uses X Logical Font Description. I'll try to fix this at a later date " \
		"cause XLFD is a pain." \
		"\n\t[--x-shift horizontal shift] Takes one int parameter. Shifts the username, line " \
		"and password field x pixels to the right (from the center of your display(s)." \
		"\n\t[--hide-name] sflock will not show your username at the lock " \
		"screen. Good if your username is stupid ;)" \
		"\n\t[--hide-line] sflock will not show the line between the " \
		"username field and the password field." \
		"\n\t[--password-only] Equivalent to --hide-line --hide-name.\n"); 
}

int
main(int argc, char **argv) {
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

    // defaults
    char* passchar = "*";
    char* fontname = "-*-helvetica-bold-r-normal-*-*-120-*-*-*-*-iso8859-1";
    char* username = ""; 
    // int show_line = 1;
    int xshift = 0;
	// show/hide element variables
	int show_name = 1;
	int show_line = 1;
	int show_password = 1;
	// location variables
	int use_x = 0; 
	int new_x = 0;
	int use_y = 0; 
	int new_y = 0;
	// image variables
	int use_b_image = 0;
	char* b_image_loc = "";
	int use_e_b_image = 0;
	char* e_b_image_loc = "";

	int opt;
	/* still to do:
		x-coord and x-shift should be for individual part? (password field, line and name?)
		y-shift isn't implemented either in the while loop or at all
		a lot of these need proper implementation.
	*/
	struct option opt_table[] = {
		{ "password-char",	required_argument,	NULL,		'c' },
		{ "font-name",		required_argument,	NULL,		'f' },
		// show/hide element options
		{ "hide-name",		no_argument,		NULL,		'n' },
		{ "hide-line",		no_argument,		NULL,		'l' },
		{ "hide-password",	no_argument,		NULL,		'p' },
		{ "password-only",	no_argument,		NULL,		'o' },
		// help and info options
		{ "help",		no_argument,		NULL,		'h' },
		{ "version",		no_argument,		NULL,		'v' },
		// location options
		{ "x-coord",		required_argument,	NULL,		'x' },
		{ "y-coord",		required_argument,	NULL,		'y' },
		{ "x-shift",		required_argument,	NULL,		'X' },
		{ "y-shift",		required_argument,	NULL,		'Y' },
		// image options
		{ "background-image",	required_argument,	NULL,		'i' },
		{ "incorrect-image",	required_argument,	NULL,		'e' },
		{ 0, 0, 0, 0 }
	};

	// printf("BitmapOpenFailed: %d ", BitmapOpenFailed);
	// printf("BitmapFileInvalid: %d ", BitmapFileInvalid);
	// printf("BitmapNoMemory: %d ", BitmapNoMemory);
	// printf("BitmapSuccess: %d ", BitmapSuccess);

	while ((opt = getopt_long(argc, argv, "c:f:nlpohvx:y:X:Y:i:e:", opt_table, NULL)) != -1) { 
		switch (opt) {
			case 'c': passchar = optarg; printf("pass changed to %s", passchar); break;
			case 'f': fontname = optarg; break;
			// show/hide element options
			case 'n': show_name = 0; break;
			case 'l': show_line = 0; break;
			case 'p': show_password = 0; break;
			case 'o': show_line = 0; show_name = 0; break;
			// help and info options
			case 'h': printHelp(); break;
			case 'v': die("sflock-"VERSION", © 2015 Ben Ruijl, JSpeedie\n"); break;
			// location options
			case 'x': 
				use_x = 1;
				new_x = atoi(optarg);
				break;
			// case 's' 'a' 'y' still need to be done
			// image options
			case 'i': 
				use_b_image = 1;
				b_image_loc = optarg;
				break;
			case 'e': 
				use_e_b_image = 1;
				e_b_image_loc = optarg;
				break;
		}
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

    XAllocNamedColor(dpy, DefaultColormap(dpy, screen), "orange red", &red, &dummy);
    XAllocNamedColor(dpy, DefaultColormap(dpy, screen), "black", &black, &dummy);
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
		Pixmap bg; 
		// Read user specified .xpm file as a Pixmap to 'p'
		int retval = XpmReadFileToPixmap (dpy, w, b_image_loc, &bg, NULL, NULL);
		// If reading the pixmap was successful
		if (retval == 0) XSetWindowBackgroundPixmap(dpy, w, bg); 
	}
    XSetFont(dpy, gc, font->fid);
    XSetForeground(dpy, gc, XWhitePixel(dpy, screen));

    for(len = 1000; len; len--) {
        if(XGrabPointer(dpy, root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
            break;
        usleep(1000);
    }
    if((running = running && (len > 0))) {
        for(len = 1000; len; len--) {
            if(XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime)
                    == GrabSuccess)
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
    while(running && !XNextEvent(dpy, &ev)) {
        if (sleepmode) {
            DPMSEnable(dpy);
            DPMSForceLevel(dpy, DPMSModeOff);
            XFlush(dpy);
        }

        if (update) {
            int x, y, dir, ascent, descent;
            XCharStruct overall;

            XClearWindow(dpy, w);
            XTextExtents (font, passdisp, len, &dir, &ascent, &descent, &overall);
		if (use_x) {
			x = new_x;
		}
		else {
			x = (width - overall.width) / 2;
		}
		
            y = (height + ascent - descent) / 2;

		if (show_name) {
			// Draw username on the lock screen
			XDrawString(dpy, w, gc, ((width - XTextWidth(font, username, strlen(username))) / 2) + xshift, y - ascent - 20, username, strlen(username));
		}

		if (show_line) {
			XDrawLine(dpy, w, gc, (width * 3 / 8) + xshift, y - ascent - 10, (width * 5 / 8) + xshift, y - ascent - 10);
		}

		if (show_password) {
			// Draw password entry on the lock screen
			XDrawString(dpy, w, gc, (x + xshift), y, passdisp, len);
		}
            update = False;
        }

        if (ev.type == MotionNotify) {
            sleepmode = False;
        }

        if(ev.type == KeyPress) {
            sleepmode = False;

            buf[0] = 0;
            num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
            if(IsKeypadKey(ksym)) {
                if(ksym == XK_KP_Enter)
                    ksym = XK_Return;
                else if(ksym >= XK_KP_0 && ksym <= XK_KP_9)
                    ksym = (ksym - XK_KP_0) + XK_0;
            }
            if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
                    || IsMiscFunctionKey(ksym) || IsPFKey(ksym)
                    || IsPrivateKeypadKey(ksym))
                continue;

            switch(ksym) {
                case XK_Return:
                    passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
                    running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
                    running = strcmp(crypt(passwd, pws), pws);
#endif
			if (running != 0) {
				// to do: change these so they aren't static
				Pixmap p; 

				// printf("\nXpmOpenFailed: %d\n", XpmOpenFailed);
				// printf("XpmFileInvalid: %d\n", XpmFileInvalid);
				// printf("XpmNoMemory: %d\n", XpmNoMemory); 

				// if the user specified an error background image
				if (use_e_b_image) {
					// Read user specified .xpm file as a Pixmap to 'p'
					int retval = XpmReadFileToPixmap (dpy, w, e_b_image_loc, &p, NULL, NULL);
					// Set the background the user specified image.
					if (retval == 0) XSetWindowBackgroundPixmap(dpy, w, p); 
				}
				else {
					// change background on wrong password
					XSetWindowBackground(dpy, w, red.pixel);
				}

				// printf("\nXpmReadFileToPixmap: %d\n", retval);

				// Flush to update background
				XFlush(dpy);
			}
                    len = 0;
                    break;
                case XK_Escape:
                    len = 0;

                    if (DPMSCapable(dpy)) {
                        sleepmode = True;
                    }

                    break;
                case XK_BackSpace:
                    if(len)
                        --len;
                    break;
                default:
                    if(num && !iscntrl((int) buf[0]) && (len + num < sizeof passwd)) { 
                        memcpy(passwd + len, buf, num);
                        len += num;
                    }

                    break;
            }

            update = True; // show changes
        }
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
    XFreeFont(dpy, font);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
    return 0;
}
