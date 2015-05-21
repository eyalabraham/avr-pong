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
#define     HSYNC           480             // horizontal sync rate 63.5uSec @ 8MKz clock (with TIMER1 Fclk/1)
#define     HSYNCHALF       (HSYNC/2)       // half the horizontal sync rate 31.75uSec @ 8MKz clock (with TIMER0 Fclk/1)
#define     SYNCWIDTH       24              // horizontal sync pulse width 4.7uSec @ 8MKz clock (with TIMER0 Fclk/1)
#define     EQUWIDTH        10              // equalizer pulse width
#define     VSYNCWIDTH      216             // vsync pulse width
#define     BACKPORCH       SYNCWIDTH       // back-porch time width 4.7uSec @ 8MKz clock (with TIMER0 Fclk/1)

// non-interlace video 48x80 pixels
#define     FIRSTLINE       1               // first scan line index <- start field sync/vertical here
#define     LASTLINE        262             // max line count of non-interlaced field
#define     STARTRENDER     20              // NTSC first visible line <- start rendering here
#define     STOPRENDER      261             // NTSC last visible line
#define     RENDERREP       5               // number of time to repeat a line rendering (save RAM lower resolution)
#define     PIXELBYTES      10              // bytes in scan line, pixel-resolution = PIXELBYTES x 8
#define     VIDEORAM        (((STOPRENDER - STARTRENDER + 1) * PIXELBYTES) / RENDERREP) // video ram size in bytes

#define     TEMPPIXBYTES    20

/* ----------------------------------------------------------------------------
 * global variables
 */
void        (*syncHandler)(void);           // sync handler function pointer
uint16_t    scanLine;                       // scan line counter
uint8_t     halfLineCounter;                // to count half scan line increments
uint8_t     skipRendering;                  // renderer enable flag
uint8_t     video[TEMPPIXBYTES] = {0x55,    // video data
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55,
                                   0x55};

/* ----------------------------------------------------------------------------
 * function definitions
 */
void ioinit(void);
void equalizing(void);
void vsync(void);
void hsync(void);

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

    // Timer0 OCR0A to provide 4.7uSec timing for sync pulse and back-porch
    // OCR0A will be polled
    TCCR0A = 0x02;          // OC0A and B are normal IO pins, and clear on timer compare CTC mode
    TCCR0B = 0x00;          // TIMER0 mode 2, stopped -- when running use: Fclk/1
    TCNT0  = 0;             // initialize counter to 0
    OCR0A  = SYNCWIDTH;     // initialize for sync pulse width
    OCR0B  = 0;             // not used
    TIMSK0 = 0x00;          // no interrupts (read TIFR0 bit.1 to check OCR0A match, clean by writing a '1' to this bit)

    // Timer1 OCR1A to provide horizontal timing for NTSC at 63.5uSec interval
    // OCR1A will drive an interrupt routine
    TCCR1A = 0x00;          // OC1A and B are normal IO pins, and clear on timer compare CTC mode
    TCCR1B = 0x09;          // TIMER1 mode 4 with Fclk/1
    TCCR1C = 0x00;
    TCNT1  = 0;             // initialize counter to 0
    OCR1A  = HSYNCHALF;     // produce and interrupt at rate of half the line time of 63.5uSec
    OCR1B  = 0;             // not used
    ICR1   = 0;             // not used
    TIMSK1 = 0x02;          // interrupt on output compare A

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
 * do-nothing ISR with minimal code
 * used to 'wake up' CPU from sleep mode with a consistent ISR response time
 *
 */
ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
    reti();
}

/* ----------------------------------------------------------------------------
 * equalizing
 *
 *  generate pre-Equalizing sync pulses
 *  ~2.2uSec width of '0' the rest at '1' and total duration of half a scan line
 *  called six times before vertical sync and six times after
 *
 */
void equalizing(void)
{
    // generate trigger signal for scope sync
    PORTD ^= 0x08;
    PORTD ^= 0x08;

    // use Timer0 to time and output a 2.2uSec 'lo' pulse
    TCNT0   = 0;            // initialize counter to 0
    OCR0A   = EQUWIDTH;     // initialize pulse width
    PORTD  &= 0xfb;         // sync pulse to 'lo'
    TCCR0B |= 0x01;         // TIMER0 mode 2, start @ Fclk/1
    loop_until_bit_is_set(TIFR0, OCF0A);
    PORTD  |= 0x04;         // Sync pulse 'hi'
    TCCR0B  = 0x00;         // stop timer
    TIFR0  |= 0x02;         // clear OCF0A flag

    halfLineCounter++;      // use this time to increment scan line
    if ( halfLineCounter == 2)
    {
        scanLine++;
        halfLineCounter = 0;
    }

    switch ( scanLine)
    {
    case 4:
        syncHandler = &vsync;
        OCR1A  = HSYNCHALF;
        break;
    case 10:
        syncHandler = &hsync;
        OCR1A  = HSYNC;
        break;
    }
}

/* ----------------------------------------------------------------------------
 * vsync
 *
 *  generate vertical sync pulses
 *  long width of '0' followed by a 4.7uSec '1' and total duration of half a scan line
 *  called six times to produce a vertical sync
 *
 */
void vsync(void)
{
    // generate trigger signal for scope sync
    PORTD ^= 0x08;
    PORTD ^= 0x08;

    // use Timer0 to time and output a 2.2uSec 'lo' pulse
    TCNT0   = 0;            // initialize counter to 0
    OCR0A   = VSYNCWIDTH;   // initialize pulse width
    PORTD  &= 0xfb;         // sync pulse to 'lo'
    TCCR0B |= 0x01;         // TIMER0 mode 2, start @ Fclk/1

    halfLineCounter++;      // use this time to increment scan line
    if ( halfLineCounter == 2)
    {
        scanLine++;
        halfLineCounter = 0;
    }

    if ( scanLine == 7)
        syncHandler = &equalizing;

    loop_until_bit_is_set(TIFR0, OCF0A);
    PORTD  |= 0x04;         // Sync pulse 'hi'
    TCCR0B  = 0x00;         // stop timer
    TIFR0  |= 0x02;         // clear OCF0A flag
}

/* ----------------------------------------------------------------------------
 * hsync
 *
 *  generate horizontal sync
 *  1.5uSec with NOP delays(?) and then 2 x 4.7uSec timed with TIMER0 OCR0B
 *  first 4.7uSec SYNC line is low, second 4.7uSec SYNC line is high
 *
 */
void hsync(void)
{
    // generate trigger signal for scope sync
    PORTD ^= 0x08;
    PORTD ^= 0x08;

    /*
    // complete a delay of 1.5uSec before output of sync pulse
    asm(
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
        );
    */

    // use Timer0 to time and output a 4.7uSec 'lo' sync pulse
    TCNT0   = 0;            // initialize counter to 0
    OCR0A   = SYNCWIDTH;    // hsync pulse width
    PORTD  &= 0xfb;         // sync pulse to 'lo'
    TCCR0B |= 0x01;         // TIMER0 mode 2, start @ Fclk/1

    if ( (scanLine < STARTRENDER) || (scanLine > STOPRENDER) )
        skipRendering = 1;
    else
        skipRendering = 0;

    loop_until_bit_is_set(TIFR0, OCF0A);
    PORTD  |= 0x04;         // Sync pulse 'hi'
    TCCR0B  = 0x00;         // stop timer
    TIFR0  |= 0x02;         // clear OCF0A flag

    // sync pulse to 'hi' and use Timer0 to time
    // a 4.7uSec sync pulse before starting video rendering
    TCNT0   = 0;            // initialize counter to 0
    TCCR0B |= 0x01;         // TIMER0 mode 2, start @ Fclk/1

    scanLine++;             // use this time to increment scan line
    if (scanLine > LASTLINE )
    {
        syncHandler = &equalizing;
        scanLine = FIRSTLINE;
        OCR1A  = HSYNCHALF;
        skipRendering = 1;
    }

    loop_until_bit_is_set(TIFR0, OCF0A);
    TCCR0B  = 0x00;         // stop timer
    TIFR0  |= 0x02;         // clear OCF0A flag
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
    UDR0 = 0;               // stuff shift register with 0 (8 pixels of black)
    UCSR0B |= (1 << TXEN0); // enable UART with UCSR0B set bit3 TXEN0 to start transmitting
    UDR0 = video[0];        // send first pixel byte

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
 * main()
 *
 */
int main(void)
{
    // initialize globals
    scanLine    = FIRSTLINE;
    halfLineCounter = 0;
    skipRendering = 1;
    syncHandler = &equalizing;

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
        sleep_cpu();        // sleep
        (*syncHandler)();   // handle sync (differentiate between line and v-sync according to scan line number)
        renderer();         // do rendering and track scan line count (if in v-sync period do nothing(?) or update display)
    }

    return 0;
}
