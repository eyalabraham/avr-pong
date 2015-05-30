/* videoutil.h
 *
 * header files for video utilities for drawing lines and rendering text
 * to the video buffer
 *
 */

#ifndef __VIDEOUTIL_H__
#define __VIDEOUTIL_H__

/* ----------------------------------------------------------------------------
 *  function prototypes
 */
void    videoinit(uint8_t*, uint16_t, uint16_t);            // initialize video area to size (Hor,Ver) pixels
void    clear(uint8_t);                                     // clear the video RAM to an 8-bit pattern
void    pset(uint16_t, uint16_t);                           // set a pixel at screen coordinate (X,Y)
void    preset(uint16_t, uint16_t);                         // clear a pixel at screen coordinate (X,Y)
void    pflip(uint16_t x, uint16_t y);                      // flip (XOR) a pixel at screen coordinate (X,Y)
void    line(uint16_t, uint16_t, uint16_t, uint16_t);       // draw a line between points (X1,Y1)-(X2,Y2)
//void    box(uint16_t, uint16_t, uint16_t, uint16_t);        // draw a box between points (X1,Y1)-(X2,Y2)
void    writechar(uint16_t, uint16_t, const char);          // write character at coordinate (X,Y)
//void    clearbox(uint16_t, uint16_t, uint16_t, uint16_t);   // clear a rectangle (X1,Y1)-(X2,Y2)
uint16_t getXres(void);                                     // get X resolution / max pixel count
uint16_t getYres(void);                                     // get Y resolution / max pixel count

#endif /* __VIDEOUTIL_H__ */
