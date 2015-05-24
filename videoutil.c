/* videoutil.c
 *
 * video utilities for drawing lines and rendering text
 * to the video buffer
 *
 * much of the code is based on Bruce E. Hall <bhall66@gmail.com>
 * w8bh.net fot ATmega328P micro controller
 *
 */
#include    <stdint.h>
#include    <stdlib.h>

#include    "videoutil.h"

/* ----------------------------------------------------------------------------
 * global definitions
 */
uint16_t    horisontalPixels = 0;
uint16_t    verticalPixels   = 0;
uint8_t     horizontalBytes  = 0;
uint16_t    bufferSize       = 0;
uint8_t     *videoBuffer     = 0;
uint8_t     initialized      = 0;

uint8_t     bitFlip[8] = {0x80, 0x40, 0x20, 0x10,
                          0x08, 0x04, 0x02, 0x01};

/* ----------------------------------------------------------------------------
 * global variables
 */

/* ----------------------------------------------------------------------------
 * utility functions

 void box(uint16_t, uint16_t, uint16_t, uint16_t);        // draw a box between points (X1,Y1)-(X2,Y2)
 void write(uint16_t, uint16_t, const char*);             // write text string at coordinate (X,Y)
 void clearbox(uint16_t, uint16_t, uint16_t, uint16_t);   // clear a rectangle (X1,Y1)-(X2,Y2)

 */

/* ----------------------------------------------------------------------------
 * videoinit()
 *
 *  initialize video area to size (Hor,Ver) pixels
 *
 */
void videoinit(uint8_t* buffer, uint16_t hpixels, uint16_t vpixels)
{
    videoBuffer      = buffer;      // initialize globals
    horisontalPixels = hpixels-1;
    verticalPixels   = vpixels-1;
    horizontalBytes  = hpixels / 8;
    bufferSize       = (hpixels / 8 ) * vpixels;

    initialized      = 1;           // every function must check this flag before rendering!
}

/* ----------------------------------------------------------------------------
 * videoinit()
 */
void clear(uint8_t pattern)
{
    uint16_t    i;

    if ( !initialized ) return;

    for (i = 0; i < bufferSize; i++)
        videoBuffer[i] = pattern;
}

/* ----------------------------------------------------------------------------
 * pset()
 *
 *  set a pixel at screen coordinate (X,Y)
 *  set pixel color to foreground 'white'
 *
 */
void pset(uint16_t x, uint16_t y)
{
    uint16_t    index;
    uint8_t     byteLocation;
    uint8_t     pattern;

    if ( !initialized ) return;

    if ( x > horisontalPixels || y > verticalPixels ) return;

    byteLocation = x / 8;
    index = (y * horizontalBytes) + byteLocation;  // byte index of the pixel
    pattern = bitFlip[(x - (byteLocation * 8))];   // bit index of the pixel
    videoBuffer[index] |= pattern;                 // set the bit
}

/* ----------------------------------------------------------------------------
 * preset()
 *
 *  clear a pixel at screen coordinate (X,Y)
 *  set pixel color to background 'black'
 *
 */
void preset(uint16_t x, uint16_t y)
{
    uint16_t    index;
    uint8_t     byteLocation;
    uint8_t     pattern;

    if ( !initialized ) return;

    if ( x > horisontalPixels || y > verticalPixels ) return;

    byteLocation = x / 8;
    index = (y * horizontalBytes) + byteLocation;  // byte index of the pixel
    pattern = bitFlip[(x - (byteLocation * 8))];   // bit index of the pixel
    videoBuffer[index] &= ~(pattern);              // set the bit
}

/* ----------------------------------------------------------------------------
 * line()
 *
 *  draw a line in foreground color 'white'
 *  between coordinates (X1,Y1)-(X2,Y2)
 *  safe to use coordinate outside screen,
 *  function will draw clipped lines.
 *
 */
void line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    int dx, sx;
    int dy, sy;
    int err, e2;

    if ( !initialized ) return;

    dx = abs(x1-x0);
    sx = x0<x1 ? 1 : -1;
    dy = abs(y1-y0);
    sy = y0<y1 ? 1 : -1;
    err = (dx>dy ? dx : -dy)/2;
    for(;;)
    {
        pset(x0,y0);
        if (x0==x1 && y0==y1) break;
        e2 = err;
        if (e2 >-dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

/* ----------------------------------------------------------------------------
 * line()
 *
 *  write text string starting at top left coordinate (X,Y) of the text box
 *
 */
void write(uint16_t x, uint16_t y, const char* text)
{
}
