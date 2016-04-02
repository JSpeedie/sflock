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
    // int showline = 1;
    int xshift = 0;
	int showname = 1;
	int showline = 1;
	int showpassword = 1;
	int usex = 0; 
	int newx = 0;

	int opt;
	int printhelp = 0;
	/* still to do:
		fix h
		x-coord and x-shift should be for individual part? (password field, line and name?)
		y-shift isn't implemented either in the while loop or at all
	*/
	struct option opttable[] = {
		{ "password-char",	required_argument,	NULL,		'c' },
		{ "font-name",		required_argument,	NULL,		'f' },
		{ "hide-name",		no_argument,		NULL,		'n' },
		{ "hide-line",		no_argument,		NULL,		'l' },
		{ "hide-password",	no_argument,		NULL,		'p' },
		{ "password-only",	no_argument,		NULL,		'o' },
		{ "help",		no_argument,		NULL,		'h' },
		{ "version",		no_argument,		NULL,		'v' },
		{ "x-coord",		required_argument,	NULL,		'x' },
		{ "y-coord",		required_argument,	NULL,		'y' },
		{ "x-shift",		required_argument,	NULL,		's' },
		{ "y-shift",		required_argument,	NULL,		'a' },
		{ 0, 0, 0, 0 }
	};

	if (printhelp) printHelp(); 
	printf("BitmapOpenFailed: %d ", BitmapOpenFailed);
	printf("BitmapFileInvalid: %d ", BitmapFileInvalid);
	printf("BitmapNoMemory: %d ", BitmapNoMemory);
	printf("BitmapSuccess: %d ", BitmapSuccess);

	while ((opt = getopt_long(argc, argv, "c:f:nlpohvx:s:a:", opttable, NULL)) != -1) { 
		switch (opt) {
			case 'c': passchar = optarg; printf("pass changed to %s", passchar); break;
			case 'f': fontname = optarg; break;
			case 'n': showname = 0; break;
			case 'l': showline = 0; break;
			case 'p': showpassword = 0; break;
			case 'o': showline = 0; showname = 0; break;
			case 'h': printHelp(); break;
			case 'v': die("sflock-"VERSION", © 2015 Ben Ruijl, JSpeedie\n"); break;
			case 'x': 
				usex = 1;
				newx = atoi(optarg);
				break;
			// case 's' 'a' 'y' still need to be done
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
		if (usex) {
			x = newx;
		}
		else {
			x = (width - overall.width) / 2;
		}
		
            y = (height + ascent - descent) / 2;

		if (showname) {
			// Draw username on the lock screen
			XDrawString(dpy, w, gc, ((width - XTextWidth(font, username, strlen(username))) / 2) + xshift, y - ascent - 20, username, strlen(username));
		}

		if (showline) {
			XDrawLine(dpy, w, gc, (width * 3 / 8) + xshift, y - ascent - 10, (width * 5 / 8) + xshift, y - ascent - 10);
		}

		if (showpassword) {
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
                        	// change background on wrong password
                        	XSetWindowBackground(dpy, w, red.pixel);
				unsigned int wret = 1920;
				unsigned int hret = 1080;
				Pixmap p; 
				int xhret = 0;
				int yhret = 0;
				// Need to test the drawable part. currently w
				printf("\nXpmOpenFailed: %d\n", XpmOpenFailed);
				printf("XpmFileInvalid: %d\n", XpmFileInvalid);
				printf("XpmNoMemory: %d\n", XpmNoMemory);
				int retval = XpmReadFileToPixmap (dpy, w, "/home/me/test.xpm", &p, NULL, NULL);
				// int retval = XReadBitmapFile(dpy, w, "/home/me/test.xbm", &wret, &hret, &p, &xhret, &yhret);
				printf("\nfjdklsajf: %d\n", retval);
				// Use the pixmap returned above.
				XSetWindowBackgroundPixmap(dpy, w, p); 
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
