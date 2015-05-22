/* pong.c
 *
 * video generator and "pong" game for AVR ATmega328p
 * running at internal 8Mhz clock on NTSC TV input
 *
 */

#include    <stdint.h>
#include    <stdlib.h>
#include    <string.h>

#include    <avr/pgmspace.h>
#include    <avr/io.h>
#include    <avr/interrupt.h>
#include    <avr/sleep.h>
#include    <avr/wdt.h>

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
#define     FIRSTLINE       10              // first visible scan line index
#define     LASTLINE        234             // max visible line count of non-interlaced field
#define     VSYNCLINE       248             // line to produce v-sync pulse
#define     LINESINFIELD    262             // total lines in field
#define     RENDERREP       5               // number of time to repeat a line rendering (save RAM lower resolution)
#define     PIXELBYTES      12              // bytes in scan line, pixel-resolution = PIXELBYTES x 8
#define     VIDEORAM        (((STOPRENDER - STARTRENDER + 1) * PIXELBYTES) / RENDERREP) // video ram size in bytes

#define     TEMPPIXBYTES    20

/* ----------------------------------------------------------------------------
 * global variables
 */
volatile uint16_t   scanLine;               // scan line counter
volatile uint8_t    skipRendering;          // renderer enable flag

void        (*activeFunction)(void);        // pointer to active function: render(), game(), or idle()
uint8_t     video[TEMPPIXBYTES] = {0x00,    // video data
                                   0xff,
                                   0x00,
                                   0xff,
                                   0x00,
                                   0xff,
                                   0x00,
                                   0xff,
                                   0x00,
                                   0xff,
                                   0x00,
                                   0x55,
                                   0x00,
                                   0xff,
                                   0x00,
                                   0xff,
                                   0x00,
                                   0xff,
                                   0x00,
                                   0xff};

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
    /*
    ADMUX  = 0x60;  // external AVcc reference, left adjusted result, ADC0 source
    ADCSRA = 0xEF;  // enable auto-triggered conversion and interrupts, ADC clock 31.25KHz
    ADCSRB = 0x00;  // auto trigger source is free-running
    */

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
    PORTD = 0x04;           // initial value of PD1 (pixel) is '0', PD2 (Sync) is '1', and PD3 (scope trig.) is '0'
}

/* ----------------------------------------------------------------------------
 * ISR fires every scan line
 * logic follows this article: http://sagargv.blogspot.in/2011/06/ntsc-demystified-cheats-part-6.html
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
        skipRendering = 0;
        break;

    // switch to blank line at end of visible area
    case (LASTLINE+1):
        //OCR1A = HSYNC;
        activeFunction = &game;
        skipRendering = 1;
        break;

    // change PWM timing to issue a v-sync wide pulse
    case (VSYNCLINE-1):
        OCR1A = VSYNC;
        //activeFunction = &game;
        //skipRendering = 1;
        break;

    // change PWM timing back to h-sync narrow pulse
    case (VSYNCLINE):
        OCR1A = HSYNC;
        //activeFunction = &game;
        //skipRendering = 1;
        break;
    }

    // increment scan line and wrap to 1 if needed
    scanLine++;
    if ( scanLine > LINESINFIELD )
        scanLine = 1;
}

/* ----------------------------------------------------------------------------
 * renderer
 *
 *  render video line or output vsync
 *  depending on scan line count this function will output video information or
 *  v-sync singal levels
 *  output through the UART in SPI mode
 *
 */
void renderer(void)
{
    uint8_t     byteCount;

    if ( skipRendering ) return;

    // send first data byte to USART to set up transmitter buffer
    //UDR0 = 0;               // stuff shift register with 0 (8 pixels of black)
    UDR0 = video[0];        // send first pixel byte
    UCSR0B |= (1 << TXEN0); // enable UART with UCSR0B set bit3 TXEN0 to start transmitting

    for (byteCount = 1; byteCount < PIXELBYTES; byteCount++)
    {
        // loop on UCSR0A:  bit.5 - UDRE0 to see if UDR0 is ready to receive another byte
        loop_until_bit_is_set(UCSR0A, UDRE0);

        // send data bytes though UART register UDR0
        UDR0 = video[byteCount];
    }

    UDR0 = 0;               // stuff shift register with 0 (8 pixels of black)
    
    // check UDRE0 bit, when this bit is set the last *data* byte has been sent
    // and the '0'-stuffing is in the shift register so we can shut down the Tx
    loop_until_bit_is_set(UCSR0A, UDRE0);

    // use UCSR0B clear bit3 TXEN0 to disable transmitter and allow PD1 to go to 'lo'
    UCSR0B &= ~(1 << TXEN0);

    // manually clear UCSR0A bit 6 - TXC0?
    UCSR0A |= (1 << TXC0);
}

/* ----------------------------------------------------------------------------
 * game()
 *
 *  this routine include all the Pong game logic
 *  in must complete within the time alloted for v-sync pulses
 *
 */
void game(void)
{
    // do game work here ...

    // game work is done so hook in an idle activity
    activeFunction = &idle;
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
    // initialize globals
    scanLine       = 1;
    skipRendering  = 1;
    activeFunction = &idle;

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
        (*activeFunction)();    // handle sync (differentiate between line and v-sync according to scan line number)
    }

    return 0;
}
