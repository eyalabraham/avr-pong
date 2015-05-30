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
 * global definitions
 */
#define     SERVECYCLE      20              // counter max value used to "randomize" serve direction
#define     BALLVELOCITY    3               // ball velocity: 1=fast .. 10=slow (i.e. if =1 ball moves every 16.6mSec, =10 every 166.3mSec etc.)
#define     NOSERVE         0               // serve flag and direction
#define     RIGHTSERVE      1
#define     LEFTSERVE       2
#define     UP              0               // serve direction
#define     DOWN            1
#define     NONE            0               // flag score status
#define     LEFT            1
#define     RIGHT           2

/* ----------------------------------------------------------------------------
 * global variables
 */

// game paddle
uint8_t     rightPaddle;                    // ADO paddle readings
uint8_t     leftPaddle;
uint8_t     rightPadTarget;                 // paddle center on screen pixel
uint8_t     curRightPadCenter = RPADINIT;
uint8_t     leftPadTarget;
uint8_t     curLeftPadCenter = LPADINIT;

// score keeping
uint8_t     leftScore = 0;                  // score variable
uint8_t     rightScore = 0;
uint8_t     scoringFlag = NONE;             // score flag: NONE, LEFT, RIGHT

// ball movement
int         ballX0, ballY0;                 // start and end coordinates of ball movement trajectory line
int         ballX1, ballY1;
int         dx, sx;                         // Bresenham's line algorithm variables,
int         dy, sy;                         // these are globals so that ball position is maintained between calls to game()
int         err, e2;
int         serveOffset = -SERVECYCLE;      // cycles from 1 to SERVECYCLE and used to pick serve direction (X1,Y1)
uint8_t     serveDir = UP;                  // serve direction UP or DOWN
uint8_t     ballSkipCycles = 0;             // skip-process cycle count to slow ball movement
uint8_t     serveFlag = 1;                  // is it time to serve a new game? 0=no, 1=from-right, 2=from-left

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
 *  - the function is invoked every 16.6mSec / 60Hz
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
    ballSkipCycles++;                           // increment process-skip counter
    if ( ballSkipCycles == BALLVELOCITY )       // check if it is time to move ball one more pixel
    {
        ballSkipCycles = 0;                     // reset process-skip counter
        pflip(ballX0, ballY0);                  // clear current ball location

        serveOffset++;                          // use this to generate some randomness in ball serving angle
        if (serveOffset > SERVECYCLE )
            serveOffset = -SERVECYCLE;
        serveDir = (serveDir==UP) ? DOWN : UP;  // switch serve direction

        switch ( serveFlag )                    // determine what to do with the next move
        {
        // no serve, just move the ball and check for
        // collision with wall or paddle
        case NOSERVE:
            // check if ball reached edge of screen
            // this means that the paddle was missed
            if ( ballX0 == (RPADCOL+1))
            {
                preset(ballX0, ballY0);         // make sure ball is cleared
                scoringFlag = LEFT;             // left player scored
                serveFlag = LEFTSERVE;          // next serve from left player
            }
            else if ( ballX0 == (LPADCOL-1))
            {
                preset(ballX0, ballY0);         // make sure ball is cleared
                scoringFlag = RIGHT;            // right player scored
                serveFlag = RIGHTSERVE;         // next serve from right player
            }
            // ball reached one of the paddles
            // check if touching one, if not do points else return ball
            else if ( ballX0 == (LPADCOL+1) &&
                 ballY0 <= (curLeftPadCenter+HALFPAD) &&
                 ballY0 >= (curLeftPadCenter-HALFPAD))
            {
                ballX0 -= sx;
                ballY0 += sy;
                ballY1 = (sy == 1) ? (BOTTOM-1) : (TOP+1);
                ballX1 = ballX0 + ((-1 * sx * abs(ballY0-ballY1) * dx) / dy);
                dx = abs(ballX1-ballX0);        // Bresenman algorithm initialization
                sx = ballX0<ballX1 ? 1 : -1;
                dy = abs(ballY1-ballY0);
                sy = ballY0<ballY1 ? 1 : -1;
                err = (dx>dy ? dx : -dy)/2;
                serveFlag = NOSERVE;
            }
            else if ( ballX0 == (RPADCOL-1) &&
                      ballY0 <= (curRightPadCenter+HALFPAD) &&
                      ballY0 >= (curRightPadCenter-HALFPAD))
            {
                ballX0 -= sx;
                ballY0 += sy;
                ballY1 = (sy == 1) ? (BOTTOM-1) : (TOP+1);
                ballX1 = ballX0 + ((-1 * sx * abs(ballY0-ballY1) * dx) / dy);
                dx = abs(ballX1-ballX0);        // Bresenman algorithm initialization
                sx = ballX0<ballX1 ? 1 : -1;
                dy = abs(ballY1-ballY0);
                sy = ballY0<ballY1 ? 1 : -1;
                err = (dx>dy ? dx : -dy)/2;
                serveFlag = NOSERVE;
            }
            // reached top or bottom of game board
            // reverse Y trajectory of ball
            else if ( ballY0 == (TOP+1) || ballY0 == (BOTTOM-1) )
            {
                ballX0 += sx;
                ballY0 -= sy;
                ballX1 = (sx == 1) ? (RPADCOL+1) : (LPADCOL-1);
                ballY1 = ballY0 + ((-1 * sy * abs(ballX0-ballX1) * dy) / dx);
                dx = abs(ballX1-ballX0);        // Bresenman algorithm initialization
                sx = ballX0<ballX1 ? 1 : -1;
                dy = abs(ballY1-ballY0);
                sy = ballY0<ballY1 ? 1 : -1;
                err = (dx>dy ? dx : -dy)/2;
                serveFlag = NOSERVE;
            }
            break;

        // serve new ball from the right
        case RIGHTSERVE:
            ballX0 = RPADCOL-1;                 // serve from center of paddle
            ballY0 = curRightPadCenter;         // one line into game board
            ballX1 = (getXres() / 2) + serveOffset;
            ballY1 = (serveDir==UP) ? (TOP+1) : (BOTTOM-1);
            dx = abs(ballX1-ballX0);            // Bresenman algorithm initialization
            sx = ballX0<ballX1 ? 1 : -1;
            dy = abs(ballY1-ballY0);
            sy = ballY0<ballY1 ? 1 : -1;
            err = (dx>dy ? dx : -dy)/2;
            scoringFlag = NONE;
            serveFlag = NOSERVE;
            break;

        // serve new ball from the left
        case LEFTSERVE:
            ballX0 = LPADCOL+1;                 // serve from center of paddle
            ballY0 = curLeftPadCenter;          // one line into game board
            ballX1 = (getXres() / 2) + serveOffset;
            ballY1 = (serveDir==UP) ? (TOP+1) : (BOTTOM-1);
            dx = abs(ballX1-ballX0);            // Bresenman algorithm initialization
            sx = ballX0<ballX1 ? 1 : -1;
            dy = abs(ballY1-ballY0);
            sy = ballY0<ballY1 ? 1 : -1;
            err = (dx>dy ? dx : -dy)/2;
            scoringFlag = NONE;
            serveFlag = NOSERVE;
            break;
        }

        e2 = err;                               // calculate new ball location
        if (e2 >-dx) { err -= dy; ballX0 += sx; }
        if (e2 < dy) { err += dx; ballY0 += sy; }

        if ( serveFlag == NOSERVE )
            pflip(ballX0, ballY0);              // put ball in new location

        // update score
        switch ( scoringFlag )
        {
        case NONE:
            break;

        case RIGHT:
            rightScore++;
            if (rightScore == 10) rightScore = 0;
            writechar(((getXres()+1)/2)+RIGHTSCORE,3,('0'+rightScore));
            break;

        case LEFT:
            leftScore++;
            if (leftScore == 10) leftScore = 0;
            writechar(((getXres()+1)/2)+LEFTSCORE,3,('0'+leftScore));
            break;
        }

        // generate sound
    }

    // game work is done so hook in an idle activity
    activeFunction = &idle;

    PORTD ^= 0x08;          // reset timing marker
}
