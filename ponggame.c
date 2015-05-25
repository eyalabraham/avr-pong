/* ponggame.c
 *
 * pong game module
 *
 */

#include    <stdint.h>
#include    <stdlib.h>

#include    <avr/pgmspace.h>
#include    <avr/io.h>

#include    "videoutil.h"
#include    "ponggame.h"

/* ----------------------------------------------------------------------------
 * global variables
 */
uint8_t     rightPaddle;                    // ADO paddle readings
uint8_t     leftPaddle;
uint8_t     rightPadTarget;                 // paddle center on screen pixel
uint8_t     curRightPadCenter = RPADINIT;
uint8_t     leftPadTarget;
uint8_t     curLeftPadCenter = LPADINIT;

extern void (*activeFunction)(void);

/* ----------------------------------------------------------------------------
 * function definitions
 */
extern void idle(void);

/* ----------------------------------------------------------------------------
 * game()
 *
 *  this routine include all the Pong game logic:
 *  - it must complete within the time alloted for v-sync pulses:
 *    22 lines x 63.5uSec = 1,300 uSec
 *  - the function can be broken into multiple sections and each section run in turn
 *    by using an 'invocation' counter and a switch-case construct.
 *  - the function is invoked every 16.6mSec
 *
 */
void game(void)
{
    PORTD ^= 0x08;          // assert timing marker

    // read game paddles right (ADC0) then left (ADC1)
    ADMUX  &= ~(1 << MUX0);                 // select ADC0
    ADCSRA |= (1 << ADEN);                  // enable ADC converter
    ADCSRA |= (1 << ADSC);                  // start convention of ADC0
    loop_until_bit_is_set(ADCSRA, ADIF);    // wait for conversion to complete
    ADCSRA |= (1 << ADIF);                  // clear conversion complete flag
    rightPaddle = ADCH;                     // read ADC
    ADCSRA &= ~(1 << ADEN);                 // disable ADC converter

    ADMUX  |= (1 << MUX0);                  // select ADC1
    ADCSRA |= (1 << ADEN);                  // enable ADC converter
    ADCSRA |= (1 << ADSC);                  // start convention of ADC1
    loop_until_bit_is_set(ADCSRA, ADIF);    // wait for conversion to complete
    ADCSRA |= (1 << ADIF);                  // clear conversion complete flag
    leftPaddle = ADCH;                      // read ADC
    ADCSRA &= ~(1 << ADEN);                 // disable ADC converter

    // process paddle movement
    rightPadTarget = (rightPaddle / 5) + 5; // scale ADC reading to paddle movement range: 0-255 -> 5-56
    leftPadTarget = (leftPaddle / 5) + 5;

    // right paddle
    if ( curRightPadCenter > rightPadTarget )
    {
        // move paddle up
        pflip(RPADCOL,(curRightPadCenter+HALFPAD));
        curRightPadCenter--;
        pflip(RPADCOL,(curRightPadCenter-HALFPAD));
    }
    else if ( curRightPadCenter < rightPadTarget )
    {
        // move paddle down
        pflip(RPADCOL,(curRightPadCenter-HALFPAD));
        curRightPadCenter++;
        pflip(RPADCOL,(curRightPadCenter+HALFPAD));
    }
    else
    {
        // do nothing paddle on target, not moving
    }

    // left paddle
    if ( curLeftPadCenter > leftPadTarget )
    {
        // move paddle up
        pflip(LPADCOL,(curLeftPadCenter+HALFPAD));
        curLeftPadCenter--;
        pflip(LPADCOL,(curLeftPadCenter-HALFPAD));
    }
    else if ( curLeftPadCenter < leftPadTarget )
    {
        // move paddle down
        pflip(LPADCOL,(curLeftPadCenter-HALFPAD));
        curLeftPadCenter++;
        pflip(LPADCOL,(curLeftPadCenter+HALFPAD));
    }
    else
    {
        // do nothing paddle on target, not moving
    }

    // process ball movement

    // game work is done so hook in an idle activity
    activeFunction = &idle;

    PORTD ^= 0x08;          // reset timing marker
}
