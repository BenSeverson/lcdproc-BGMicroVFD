/*This is the driver for the large 20x2 vfd that was sold at bgmicro.
The maker is IEE and the model is the Century 122-09220.  It should 
work with the other models in the Century series but it has not been
tested.

this driver was based on the CFontz driver modified by Ben Severson
in october 2002

*/


/*  
    Copyright (C) 2002

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 */

/*configfile support added by Rene Wagner (c) 2001*/
/*backlight support modified by Rene Wagner (c) 2001*/
/*block patch by Eddie Sheldrake (c) 2001 inserted by Rene Wagner*/
/*big num patch by Luis Llorente (c) 2002*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "lcd.h"
#include "century.h"
//#include "drv_base.h"
//#include "shared/debug.h"
//#include "shared/str.h"
#include "report.h"
#include "lcd_lib.h"
//#include "server/configfile.h"

static int custom = 0;
typedef enum {
	hbar = 1,
	vbar = 2,
	bign = 4,
	beat = 8
} custom_type;

static int fd;
static char *framebuf = NULL;
static int width = 0;
static int height = 0;
static int cellwidth = 5;
static int cellheight = 7;
static int contrast = DEFAULT_CONTRAST;
static int brightness = DEFAULT_BRIGHTNESS;
static int offbrightness = DEFAULT_OFFBRIGHTNESS;

// Vars for the server core
MODULE_EXPORT char *api_version = API_VERSION;
MODULE_EXPORT int stay_in_foreground = 1;
MODULE_EXPORT int supports_multiple = 0;
MODULE_EXPORT char *symbol_prefix = "century_";


// Internal functions
static void century_hidecursor ();
static void century_init_vbar (Driver * drvthis);

/////////////////////////////////////////////////////////////////
// Opens com port and sets baud correctly...
//
MODULE_EXPORT int
century_init (Driver * drvthis, char *args)
{
	struct termios portset;
	int tmp, w, h;

	int contrast = DEFAULT_CONTRAST;
	char device[200] = DEFAULT_DEVICE;
	int speed = DEFAULT_SPEED;
	char size[200] = DEFAULT_SIZE;

	debug(RPT_INFO, "century: init(%p,%s)", drvthis, args );

	/*Read config file*/

	/*Which serial device should be used*/
	strncpy(device, drvthis->config_get_string ( drvthis->name , "Device" , 0 , DEFAULT_DEVICE),sizeof(device));
	device[sizeof(device)-1]=0;
	debug (RPT_INFO,"century: Using device: %s", device);

	/*Which size*/
	strncpy(size, drvthis->config_get_string ( drvthis->name , "Size" , 0 , DEFAULT_SIZE),sizeof(size));
	size[sizeof(size)-1]=0;
	if( sscanf(size , "%dx%d", &w, &h ) != 2
	|| (w <= 0) || (w > LCD_MAX_WIDTH)
	|| (h <= 0) || (h > LCD_MAX_HEIGHT)) {
		report (RPT_WARNING, "century_init: Cannot read size: %s. Using default value.\n", size);
		sscanf( DEFAULT_SIZE , "%dx%d", &w, &h );
	} else {
		width = w;
		height = h;
	}

	/*Which contrast*/
	if (0<=drvthis->config_get_int ( drvthis->name , "Contrast" , 0 , DEFAULT_CONTRAST) && drvthis->config_get_int ( drvthis->name , "Contrast" , 0 , DEFAULT_CONTRAST) <= 255) {
		contrast = drvthis->config_get_int ( drvthis->name , "Contrast" , 0 , DEFAULT_CONTRAST);
	} else {
		report (RPT_WARNING, "century_init: Contrast must between 0 and 255. Using default value.\n");
	}

	/*Which backlight brightness*/
	if (0<=drvthis->config_get_int ( drvthis->name , "Brightness" , 0 , DEFAULT_BRIGHTNESS) && drvthis->config_get_int ( drvthis->name , "Brightness" , 0 , DEFAULT_BRIGHTNESS) <= 7) {
		brightness = drvthis->config_get_int ( drvthis->name , "Brightness" , 0 , DEFAULT_BRIGHTNESS);
	} else {
		report (RPT_WARNING, "century_init: Brightness must between 0 and 7. Using default value.\n");
	}

	/*Which backlight-off "brightness"*/
	if (0<=drvthis->config_get_int ( drvthis->name , "OffBrightness" , 0 , DEFAULT_OFFBRIGHTNESS) && drvthis->config_get_int ( drvthis->name , "OffBrightness" , 0 , DEFAULT_OFFBRIGHTNESS) <= 255) {
		offbrightness = drvthis->config_get_int ( drvthis->name , "OffBrightness" , 0 , DEFAULT_OFFBRIGHTNESS);
	} else {
		report (RPT_WARNING, "century_init: OffBrightness must between 0 and 255. Using default value.\n");
	}


	/*Which speed*/
	tmp = drvthis->config_get_int ( drvthis->name , "Speed" , 0 , DEFAULT_SPEED);
	if (tmp == 1200) speed = B1200;
	else if (tmp == 9600) speed = B9600;
	else if (tmp == 19200) speed = B19200;
	else { report (RPT_WARNING, "century_init: Speed must be 1200, 9600, or 19200. Using default value.\n", speed);
	}

	// Set up io port correctly, and open it...
	debug( RPT_DEBUG, "century: Opening serial device: %s", device);
	fd = open (device, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd == -1) {
		report (RPT_ERR, "century_init: failed (%s)\n", strerror (errno));
		return -1;
	}

	tcgetattr (fd, &portset);

	// We use RAW mode
#ifdef HAVE_CFMAKERAW
	// The easy way
	cfmakeraw( &portset );
#else
	// The hard way
	portset.c_iflag &= ~( IGNBRK | BRKINT | PARMRK | ISTRIP
	                      | INLCR | IGNCR | ICRNL | IXON );
	portset.c_oflag &= ~OPOST;
	portset.c_lflag &= ~( ECHO | ECHONL | ICANON | ISIG | IEXTEN );
	portset.c_cflag &= ~( CSIZE | PARENB | CRTSCTS );
	portset.c_cflag |= CS8 | CREAD | CLOCAL ;
#endif

	// Set port speed
	cfsetospeed (&portset, speed);
	cfsetispeed (&portset, B0);

	// Do it...
	tcsetattr (fd, TCSANOW, &portset);

	// Make sure the frame buffer is there...
	framebuf = (unsigned char *) malloc (width * height);
	memset (framebuf, ' ', width * height);

	sleep (1);
	century_hidecursor ();

	//century_backlight (drvthis, backlight_brightness);  // render.c variables should not be used in drivers !

	century_set_contrast (drvthis, contrast);

	report (RPT_DEBUG, "century_init: done\n");

	return 0;
}

/////////////////////////////////////////////////////////////////
// Clean-up
//
MODULE_EXPORT void
century_close (Driver * drvthis)
{
	close (fd);

	if(framebuf) free (framebuf);
	framebuf = NULL;
}

/////////////////////////////////////////////////////////////////
// Returns the display width
//
MODULE_EXPORT int
century_width (Driver *drvthis)
{
	return width;
}

/////////////////////////////////////////////////////////////////
// Returns the display height
//
MODULE_EXPORT int
century_height (Driver *drvthis)
{
	return height;
}

//////////////////////////////////////////////////////////////////
// Flushes all output to the lcd...
//
MODULE_EXPORT void
century_flush (Driver * drvthis)
{
	char out[2];
	int i;

	snprintf (out, sizeof(out), "%c", 22);
	write (fd, out, 1);

	for (i = 0; i < height; i++) {
		write (fd, framebuf + (width * i), width);
	}
}

/////////////////////////////////////////////////////////////////
// Prints a character on the lcd display, at position (x,y).  The
// upper-left is (1,1), and the lower right should be (20,4).
//
MODULE_EXPORT void
century_chr (Driver * drvthis, int x, int y, char c)
{
	y--;
	x--;

	if (c < 10 && c >= 0)
		c += 246;

	framebuf[(y * width) + x] = c;
}

/////////////////////////////////////////////////////////////////
// Returns current contrast
// This is only the locally stored contrast, the contrast value
// cannot be retrieved from the LCD.
// Value 0 to 1000.
//
MODULE_EXPORT int
century_get_contrast (Driver * drvthis)
{
	return contrast;
}

/////////////////////////////////////////////////////////////////
// Changes screen contrast (0-255; 140 seems good)
// Value 0 to 100.
//
MODULE_EXPORT void
century_set_contrast (Driver * drvthis, int promille)
{
//	char out[4];

	// Check it
	if( promille < 0 || promille > 1000 )
		return;

	// Store it
	contrast = promille;

	// And do it
//	snprintf (out, sizeof(out), "%c%c", 15, (unsigned char) (promille / 10) ); // converted to be 0 to 100
//	write (fd, out, 3);
}

/////////////////////////////////////////////////////////////////
// Sets the backlight on or off -- can be done quickly for
// an intermediate brightness...
//
MODULE_EXPORT void
century_backlight (Driver * drvthis, int on)
{
	char out[6];
	if (on) {
		snprintf (out, sizeof(out), "%c%c%c%c", 0x19, 0x30, 0xFF, brightness);
	} else {
		snprintf (out, sizeof(out), "%c%c%c%c", 0x19, 0x30, 0xFF, offbrightness);
	}
	write (fd, out, 4);
}

/////////////////////////////////////////////////////////////////
// Get rid of the blinking curson
//
static void
century_hidecursor ()
{
	char out[4];
	snprintf (out, sizeof(out), "%c", 14);
	write (fd, out, 1);
}

/////////////////////////////////////////////////////////////////
// Sets up for vertical bars.
//
static void
century_init_vbar (Driver * drvthis)
{
	char a[] = {0x10,0x48,0x20,0x81,0x04};
	char b[] = {0x14,0x5A,0x68,0xA1,0x05};
	char c[] = {0x95,0x5E,0x7A,0xE9,0x05};
	char d[] = {0xB5,0xDF,0x7A,0xFB,0x0D};


	if (custom != vbar) {
		century_set_char (drvthis, 1, a);
		century_set_char (drvthis, 2, b);
		century_set_char (drvthis, 3, c);
		century_set_char (drvthis, 4, d);
		custom = vbar;
	}
	
}

/////////////////////////////////////////////////////////////////
// Draws a vertical bar...
//
MODULE_EXPORT void
century_vbar (Driver * drvthis, int x, int y, int len, int promille, int options)
{
	/* x and y are the start position of the bar.
	 * The bar by default grows in the 'up' direction
	 * (other direction not yet implemented).
	 * len is the number of characters that the bar is long at 100%
	 * promille is the number of promilles (0..1000) that the bar should be filled.
	 */

	century_init_vbar(drvthis);

	lib_vbar_static(drvthis, x, y, len, promille, options, cellheight, 0xF6);

}

/////////////////////////////////////////////////////////////////
// Draws a horizontal bar to the right.
//
MODULE_EXPORT void
century_hbar (Driver * drvthis, int x, int y, int len, int promille, int options)
{
	/* x and y are the start position of the bar.
	 * The bar by default grows in the 'right' direction
	 * (other direction not yet implemented).
	 * len is the number of characters that the bar is long at 100%
	 * promille is the number of promilles (0..1000) that the bar should be filled.
	 */

	lib_hbar_static(drvthis, x, y, len, promille, options, cellwidth, 0xEA);
}


/////////////////////////////////////////////////////////////////
// Sets a custom character from 0-7...
//
// For input, values > 0 mean "on" and values <= 0 are "off".
//
// The input is just an array of characters...
//
MODULE_EXPORT void
century_set_char (Driver * drvthis, int n, char *dat)
{
	char out[4];

	if (n < 0 || n > 9)
		return;
	if (!dat)
		return;

	snprintf (out, sizeof(out), "%c%c", 0x18, 0xF6 + n);
	write (fd, out, 2);

	write (fd, dat, 5);
}

/////////////////////////////////////////////////////////////////
// Places an icon on screen
//
MODULE_EXPORT int
century_icon (Driver * drvthis, int x, int y, int icon)
{

	char eheart[] = {40,42,49,85,5};	
	char fheart[] =	{45,59,51,127,13};
//	char ellipsis[] = {136,8,0,0,0};

	if (custom == bign)
		custom = beat;

	switch( icon ) {
		case ICON_BLOCK_FILLED:
			century_chr( drvthis, x, y, 127 );
			break;
		case ICON_HEART_FILLED:
			century_set_char( drvthis, 9, fheart );
			century_chr( drvthis, x, y, 9 );
			break;
		case ICON_HEART_OPEN:
			century_set_char( drvthis, 8, eheart );
			century_chr( drvthis, x, y, 8 );
			break;
		default:
			return -1;
	}

	
	return 0;
}

/////////////////////////////////////////////////////////////////
// Clears the LCD screen
//
MODULE_EXPORT void
century_clear (Driver * drvthis)
{
	memset (framebuf, ' ', width * height);

}

/////////////////////////////////////////////////////////////////
// Prints a string on the lcd display, at position (x,y).  The
// upper-left is (1,1), and the lower right should be (20,4).
//
MODULE_EXPORT void
century_string (Driver * drvthis, int x, int y, char string[])
{
	int i;

	x -= 1;							  // Convert 1-based coords to 0-based...
	y -= 1;

	for (i = 0; string[i]; i++) {
		// Check for buffer overflows...
		if ((y * width) + x + i > (width * height))
			break;
		framebuf[(y * width) + x + i] = string[i];
	}
}
