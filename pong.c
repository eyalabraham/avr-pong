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
#define     BACKPORCH       SYNCWIDTH       // back-porch time width 4.7uSec @ 8MKz clock (with TIMER0 Fclk/1)
#define     MAXLINES        526             // max line count

#define     PIXELBYTES      20              // bytes in scan line, pixel-resolution = PIXELBYTES x 8

/* ----------------------------------------------------------------------------
 * global variables
 */
uint16_t    scanLine          = 0;          // scan line counter
uint8_t     video[PIXELBYTES] = {0x00,      // video data
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
                                 0xff,
                                 0x00,
                                 0xff,
                                 0x00,
                                 0xff,
                                 0x00,
                                 0xff,
                                 0x00,
                                 0x01};

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
    OCR1A  = HSYNC;         // produce and interrupt at hsync rate of 63.5uSec
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
    UBRR0L = 0;             // to get 4Mbps (see Table 20-1 page 205)
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
 * hsyncHandler
 *
 *  generate horizontal sync
 *  1.5uSec with NOP delays(?) and then 2 x 4.7uSec timed with TIMER0 OCR0B
 *  first 4.7uSec SYNC line is low, second 4.7uSec SYNC line is high
 *
 */
void hsyncHandler(void)
{
    PORTD ^= 0x08;          // assert scope trigger

    // delay 1.5uSec before output of sync pulse
    // 12 x 1-cycle 'nop' @ 8MHz clock
    asm(
            "nop\n\t"       // delay 1.5uSec
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
            "nop\n\t"
        );

    // use Timer0 to time and output a 4.7uSec 'lo' sync pulse
    TCNT0   = 0;            // initialize counter to 0
    PORTD  &= 0xfb;         // sync pulse to 'lo'
    TCCR0B |= 0x01;         // TIMER0 mode 2, start @ Fclk/1

    scanLine++;             // use this time to increment scan line
    if (scanLine > MAXLINES )
        scanLine = 0;

    loop_until_bit_is_set(TIFR0, OCF0A);
    PORTD  |= 0x04;         // Sync pulse 'hi'
    TCCR0B  = 0x00;         // stop timer
    TIFR0  |= 0x02;         // clear OCF0A flag

    // sync pulse to 'hi' and use Timer0 to time
    // a 4.7uSec sync pulse before starting video rendering
    TCNT0   = 0;            // initialize counter to 0
    TCCR0B |= 0x01;         // TIMER0 mode 2, start @ Fclk/1
    loop_until_bit_is_set(TIFR0, OCF0A);
    TCCR0B  = 0x00;         // stop timer
    TIFR0  |= 0x02;         // clear OCF0A flag

    PORTD ^= 0x08;          // de-assert scope trigger
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
        hsyncHandler();     // handle sync (differentiate between line and v-sync according to scan line number)
        renderer();         // do rendering and track scan line count (if in v-sync period do nothing(?) or update display)
    }

    return 0;
}
