/* pong.c
 *
 * video generator and "pong" game for AVR ATmega328p
 * running at internal 8Mhz clock on NTSC TV input
 *
 */

#include    <stdint.h>
#include    <stdlib.h>

#include    <avr/pgmspace.h>
#include    <avr/io.h>
#include    <avr/interrupt.h>
#include    <avr/sleep.h>
#include    <avr/wdt.h>

#include    "videoutil.h"
#include    "ponggame.h"

/* ----------------------------------------------------------------------------
 * global definitions
 */
#define     SYSTEMCLK       8000000         // system clock frequency in Hz

// counter constants for timing
// all counts are reduced to compensate for timing on program
#define     LINERATE        495             // horizontal line rate 63.5uSec @ 8MKz clock (with TIMER1 Fclk/1)
#define     LINERATEHALF    (LINERATE/2)    // half the horizontal sync rate 31.75uSec @ 8MKz clock (with TIMER0 Fclk/1)
#define     HSYNC           35              // horizontal sync pulse width 4.7uSec @ 8MKz clock (with TIMER0 Fclk/1)
#define     VSYNC           435             // vsync pulse width
#define     BACKPORCH       SYNCWIDTH       // back-porch time width 4.7uSec @ 8MKz clock (with TIMER0 Fclk/1)

// non-interlace video 48x80 pixels
#define     FIRSTLINE       0               // first visible scan line index
#define     POSTRENDER      240             // blank lines after render
#define     VSYNCLINE       245             // line to produce v-sync pulse
#define     PRERENDER       248             // blank lines before restarting render
#define     LINESINFIELD    262             // total lines in field
#define     RENDERREP       3               // number of time to repeat a line rendering (save RAM lower resolution)
#define     PIXELBYTES      11              // bytes in scan line, pixel-resolution = PIXELBYTES x 8

#define     VISIBLELINES    POSTRENDER      // 240 visible lines
#define     VIDEORAM        ((VISIBLELINES * PIXELBYTES) / RENDERREP) // video ram size in bytes

#define     PIXELSX         (PIXELBYTES * 8)
#define     PIXELSY         (VISIBLELINES / RENDERREP)

/* ----------------------------------------------------------------------------
 * global variables
 */
volatile uint16_t   scanLine;               // scan line counter

void        (*activeFunction)(void);        // pointer to active function: render(), game(), or idle()
uint16_t    videoRamIndex;                  // for holding a pre-calculated index
uint8_t     lineRepeat;
uint8_t     videoRAM[VIDEORAM];             // video RAM buffer

/* ----------------------------------------------------------------------------
 * function definitions
 */
void ioinit(void);
void renderer(void);
void game(void);
void idle(void);

/* ----------------------------------------------------------------------------
 * ioinit()
 *
 *  initialize IO interfaces
 *  timer and data rates calculated based on 4MHz internal clock
 *
 */
void ioinit(void)
{
    // reconfigure system clock scaler to 10MHz and/or source
    CLKPR = 0x80;           // change clock scaler to divide by 1 (sec 8.12.2 p.37)
    CLKPR = 0x00;

    // Timer0 for audio beeps

    // Timer1 OCR1A to provide horizontal timing for NTSC at 63.5uSec interval
    // OCR1A will drive an interrupt routine
    TCCR1A = 0xc2;          // OC1A inverting mode and B is  normal IO pins, fast-PWM mode 14
    TCCR1B = 0x19;          // TIMER1 mode 14 with Fclk/1
    TCCR1C = 0x00;
    TCNT1  = 0;             // initialize counter to 0
    OCR1A  = HSYNC;         // produce a sync pulse
    OCR1B  = 0;             // not used
    ICR1   = LINERATE;      // PWM TOP value for 63.5uSec lane rate
    TIMSK1 = 0x01;          // interrupt on timer overflow (every scan line)

    DDRB   = (1 << PB1);    // enable PB1 as output for OC1A

    // initialize ADC converter input ADC0
    ADMUX  = 0x60;  // external AVcc reference, left adjusted result, ADC0 source
    ADCSRA = 0x10;  // don't enable ADC, no auto-triggered conversion, no interrupts, ADC clock Fclk/2, force clear ADIF bit
    ADCSRB = 0x00;  // free-running trigger source
    DIDR0  = 0x03;  // disable digital input on ADC0 and ADC1 pins

    // initialize UART in SPI mode
    // UCSR0A: check bit.5 - UDRE0 to see if UDR0 is ready to receive another byte
    //         or see description of the UDRIE bit for interrupt on 'Data Register Empty'
    UCSR0B = 0x00;          // don't enable transmitter yet, idle is 'hi' and we need a 'lo', set bit.5 UDRIE for interrupt setup
    UCSR0C = 0xC0;          // SPI mode, Tx MSB first
    UBRR0L = 1;             // to get 2Mbps (see Table 20-1 page 205)
    UBRR0H = 0;

    // initialize general IO pins for output
    // - PD1: pixel transmitter, out, no pull-up. needed for default idle state to be '0'
    // - PD2: Sync pulse, output, no pull-up
    // - PD3: Scope trigger, output, no pull-up
    DDRD  = 0x0E;           // PD pin directions (output: PD1, PD2, PD3)
    PORTD = 0x06;           // initial value of PD1 (pixel) is '1', PD2 (Sync) is '1', and PD3 (scope trig.) is '0'
}

/* ----------------------------------------------------------------------------
 * ISR fires every scan line
 * logic follows this article: http://sagargv.blogspot.in/2011/06/ntsc-demystified-cheats-part-6.html
 *
 * picture scan lines according to: http://wiki.nesdev.com/w/index.php/NTSC_video
 * 0    - 239   (240)   active picture video data
 * 240  - 244   (  5)   blank video
 * 245  - 247   (  3)   v-sync
 * 248  - 261   ( 14)   blank video
 *            --------
 *               262 lines
 *
 */
ISR(TIMER1_OVF_vect)
{
    switch ( scanLine )
    {
    // start rendering
    case FIRSTLINE:
        OCR1A = HSYNC;
        activeFunction = &renderer;
        break;

    // switch to blank line at end of visible area
    case POSTRENDER:
        activeFunction = &game;
        break;

    // change PWM timing to issue a v-sync wide pulse
    case VSYNCLINE:
        OCR1A = VSYNC;
        lineRepeat = 0;
        videoRamIndex = 0;
        break;

    // change PWM timing back to h-sync narrow pulse
    case PRERENDER:
        OCR1A = HSYNC;
        break;
    }

    // increment scan line and wrap to 1 if needed
    scanLine++;
    if ( scanLine == LINESINFIELD )
        scanLine = 0;
}

/* ----------------------------------------------------------------------------
 * renderer
 *
 *  render video pixels
 *  this function outputs video information through the UART in SPI mode
 *  note that pixel data bytes are inverted before sending out UART
 *  this is because we have an inverter (74LS14) on the pixel output.
 *  the inverter was added to eliminate the '1' pulse that the UART sends
 *  when it is enabled.
 *
 */
void renderer(void)
{
    uint8_t     byteCount;

    // send first data byte to USART to set up transmitter buffer
    UDR0 = ~(videoRAM[videoRamIndex]); // invert and send first pixel byte (pixel output has n inverter on it
    UCSR0B |= (1 << TXEN0);            // enable UART with UCSR0B set bit3 TXEN0 to start transmitting

    for (byteCount = 1; byteCount < PIXELBYTES; byteCount++)
    {
        // loop on UCSR0A:  bit.5 - UDRE0 to see if UDR0 is ready to receive another byte
        loop_until_bit_is_set(UCSR0A, UDRE0);

        // invert and send data bytes though UART register UDR0
        UDR0 = ~(videoRAM[videoRamIndex+byteCount]);
    }

    UDR0 = 0;               // stuff shift register with 0 (8 pixels of black)
    
    // check UDRE0 bit, when this bit is set the last *data* byte has been sent
    // and the '0'-stuffing is in the shift register so we can shut down the Tx
    loop_until_bit_is_set(UCSR0A, UDRE0);

    // use UCSR0B clear bit3 TXEN0 to disable transmitter and allow PD1 to go to 'lo'
    UCSR0B &= ~(1 << TXEN0);

    // manually clear UCSR0A bit 6 - TXC0
    UCSR0A |= (1 << TXC0);

    // move pointer to next video buffer line and account for render repeat
    // line render repeat loop
    if ( lineRepeat < RENDERREP )
    {
        lineRepeat++;
    }
    else
    {
        lineRepeat = 0;
        videoRamIndex += PIXELBYTES;
        if ( videoRamIndex == VIDEORAM )
            videoRamIndex = 0;
    }
}

/* ----------------------------------------------------------------------------
 * idle()
 *
 *  this routine will be used whenever no other actions needs to be take, neither
 *  game logic nor rendering
 *
 */
void idle(void)
{
    // do nothing
}

/* ----------------------------------------------------------------------------
 * main()
 *
 */
int main(void)
{
    uint8_t i;

    // initialize globals
    scanLine       = 0;
    videoRamIndex  = 0;
    lineRepeat     = 0;
    activeFunction = &idle;

    // initialize video RAM buffer and game board
    videoinit(videoRAM, PIXELSX, PIXELSY);
    clear(0);

    line(0,TOP,(PIXELSX-1),TOP);        // top line
    line(0,BOTTOM,(PIXELSX-1),BOTTOM);  // bottom line
    for (i = 1; i < PIXELSY; i += 4)    // dashed line down the middle
    {
        line((PIXELSX/2),i,(PIXELSX/2),i+1);
    }
    line(LPADCOL,(LPADINIT-HALFPAD),LPADCOL,(LPADINIT+HALFPAD)); // draw left paddles
    line(RPADCOL,(RPADINIT-HALFPAD),RPADCOL,(RPADINIT+HALFPAD)); // draw right paddles
    write((PIXELSX/2)+LEFTSCORE,3,'0'); // print initial score
    write((PIXELSX/2)+RIGHTSCORE,3,'0');

    // on M328p needs the watch-dog timeout flag cleared (why?)
    MCUSR &= ~(1<<WDRF);
    wdt_disable();

    // initialize hardware
    ioinit();

    // set CPU to sleep mode
    set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();

    // enable interrupts
    sei();

    while (1)
    {
        sleep_cpu();            // sleep
        (*activeFunction)();
    }

    return 0;
}
