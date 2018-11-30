/**
 *  @mainpage
 * @author  Marek Honek <br> Alzbeta Kovalova <br> Brno University of Technology, Czechia
 * @version V0.1
 * @date    Nov 30, 2018
 * @details This project has been created as semetral project
 * for subject BMPT. The main purpose was measuring capacity of different
 * capacitors in range from 1nF to 1.2uF. <br> This program is running
 * on Arduino Uno board with seven-segment display peripheral.
 * Five 1M resisotrs and one 1k resistor are connected to arduino board.
 * Program is using interrupts from ADC and TC1
 * for find out time constant, which is used for counting capacity.
 */


/**
  ******************************************************************************
  * @file    main.c
  * @author  Marek Honek <br> Alzbeta Kovalova <br> Brno University of Technology, Czechia
  * @version V0.1
  * @date    Nov 30, 2018
  * @brief   School project - Capacitance meter
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <avr/interrupt.h>
#include <util/delay.h>

/* Constants and macros ------------------------------------------------------*/
/**
  * @brief Serial data input of 74HC595 shift register.
  */
#define DATA_SHIFT PB0

/**
  * @brief Clock input of 74HC595 shift register.
  */
#define CLK_SHIFT PD7

/**
  * @brief Latch input of 74HC595 shift register.
  */
#define LATCH_SHIFT PD4

/**
  * @brief Frequency of core - 16MHz
  */
#define F_CPU 16000000UL // Hz

/**
  * @brief Decimal point on seven-segment display
  */
#define DOT (-0x80)

/* Function prototypes -------------------------------------------------------*/
void setup(void);
void segment_putc(uint8_t, uint8_t);
void segment_toggle_clk(void);
uint16_t Discharge (void);
uint16_t Charge (void);
void Quick_charge(void);
void Quick_discharge(void);
void Time_to_capacity (void);
void Display (void);


/* Global variables ----------------------------------------------------------*/

/** character on display */
uint8_t digit = 0;

/** current segment on display */
uint8_t segment = 0;

/** waiting for (dis)charging */
uint8_t wait = 1;

/** display array of current characters */
uint8_t display[] = {0xff, 0xff, 0xff, 0xff};

/** decision voltage level duration of (dis)charging */
uint16_t voltage_level, duration;

/** Time constant of measured capacitor with 3.5M resistor in series */
uint32_t Time_constant;

/** value of capacity */
float capacity;


/** Digit definitions for values 0 to 9, u, n, L, H and W */
uint8_t segment_digit[] = {
    0xc0, 0xf9, 0xa4, 0xb0, 0x99, 0x92, 0x82, 0xf8, 0x80, 0x90, 0xe3, 0xab,
    0xc7, 0x89, 0xc3, 0xe1};

/** Digit positions 0 to 3 */
uint8_t segment_position[] = {
    0xf1, 0xf2, 0xf4, 0xf8};


/* Functions -----------------------------------------------------------------*/
/**
  * @brief Main function.
  */
int main(void)
{
    /* Initializations */
    setup();

    /* Discharging capacitor for the initial condition */
    Quick_discharge();

    /* Charge and save time constant */
    Time_constant = Charge();

    /* Quick charge for the initial condition */
    Quick_charge();

    /* Discharge and save time constant */
    Time_constant += Discharge();

    /* Discharging on end of measurment */
    Quick_discharge();

    /* Convert time to capacity */
    Time_to_capacity();

    /* Display capacity on seven-segment display */
    Display();

    /* Still display capacity and do nothing */
    while (1)
    {
    }

    return 0;
}

/**
  * @brief Setup all peripherals.
  */
void setup(void)
{
    /* Set output pins DATA_SHIFT (PB0), CLK_SHIFT (PD7), LATCH_SHIFT (PD4) */
    DDRB |= _BV(DATA_SHIFT);
    DDRD |= _BV(CLK_SHIFT) | _BV(LATCH_SHIFT);
    PORTB &= ~_BV(DATA_SHIFT);
    PORTD &= ~_BV(CLK_SHIFT);
    PORTD &= ~_BV(LATCH_SHIFT);

    /* Clock prescaler 1024 for TC1 */
    TCCR1B |= _BV(CS12)|_BV(CS10);

    /* Clock prescaler 256 for TC0 */
    TCCR0B |= _BV(CS02);
    /* Overflow interrupt enable for TC0 */
    TIMSK0 |= _BV(TOIE0);

    /* Analog to Digital Converter */
    /* register ADMUX: Set ADC voltage reference to AVcc with external capacitor,
                       select input channel ADC0 (PC5) */
    ADMUX |= _BV(REFS0)| _BV(MUX2) | _BV(MUX0);

    /* register ADCSRA: ADC Enable,
                        ADC Auto Trigger Enable,
                        ADC Prescaler 128 => fadc = fcpu / 128 = 125 kHz */
    ADCSRA |= _BV(ADEN) | _BV(ADATE) | _BV(ADSC) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);

    ADCSRB &= ~_BV(ADTS2) & ~_BV(ADTS1) & ~_BV(ADTS0);

    /* Global interupt enable */
    sei();
}

/**
  * @brief Discharging capacitor before measurement
  */
void Quick_discharge (void)
{
    /* ADC and TC1 interrupts disable */
    ADCSRA &= ~_BV(ADIE);
    TIMSK1 &= ~_BV(TOIE1);

    /* Discharging through resistor 1k */
    DDRD |= _BV(PD6);
    PORTD &= ~_BV(PD6);

    /* Disconnect resistor 4M */
    DDRD &= ~_BV(PD5);
    PORTD &= ~_BV(PD5);

    /* Delay 1000ms for discharging*/
    _delay_ms(1000);
}

/**
  * @brief Charging capacitor and to find out duration of charging
  * @return Time constant in units of TC1

  */
uint16_t Charge (void)
{
    /* Setting voltage level to 3,161V */
    voltage_level = 647;

    /* Variable for saving time constant */
    duration = 0;

    /* Disconnect resistor 1k */
    DDRD &= ~_BV(PD6);
    PORTD &= ~_BV(PD6);

    /* Charging through 4M */
    DDRD |= _BV(PD5);
    PORTD |= _BV(PD5);

    /* Zeroing TC1 */
    TCNT1 = 0;

    /* ADC and TC1 interrupts enable */
    ADCSRA |= _BV(ADIE);
    TIMSK1 |= _BV(TOIE1);

    /* Waiting for interrupts */
    while (wait==1)
    {
        _delay_ms(5);
    };

    /* ADC and TC1 interrupts disable */
    ADCSRA &= ~_BV(ADIE);
    TIMSK1 &= ~_BV(TOIE1);

    /* If interrupt was not from TC1 show 0 on display and save TC1 value */
    if(duration < 65535)
    {
        display[3]=segment_digit[0];
        duration = TCNT1;
    }
    /* If interrupt was from TC1 show H on display,
       duration is set to max value in interrupt from TC1 */
    else
    {
        display[3]=segment_digit[13];
    }

    return(duration);
}

/**
  * @brief Quick charging capacitor for setting initial condition
  */
void Quick_charge (void)
{
    /* ADC and TC1 interrupts disable */
    ADCSRA &= ~_BV(ADIE);
    TIMSK1 &= ~_BV(TOIE1);

    /* Charging through resistor 1k */
    DDRD |= _BV(PD6);
    PORTD |= _BV(PD6);

    /* Disconnect resistor 4M */
    DDRD &= ~_BV(PD5);
    PORTD &= ~_BV(PD5);

    /* Delay 1000ms for charging*/
    _delay_ms(1000);
}

/**
  * @brief Discharging capacitor and to find out duration of discharging
  * @return Time constant in units of TC1
  */
uint16_t Discharge (void)

{
    /* Set up wait for waiting to interrupt */
    wait = 1;

    /* Variable for saving time constant */
    duration = 0;

    /* Setting voltage level to 1.839 V */
    voltage_level = 377;

    /* Disconnect resistor 1k */
    DDRD &= ~_BV(PD6);
    PORTD &= ~_BV(PD6);

    /* Discharging through 4M */
    DDRD |= _BV(PD5);
    PORTD &= ~_BV(PD5);

    /* Zeroing TC1 */
    TCNT1 = 0;

    /* ADC and TC1 interrupts enable */
    ADCSRA |= _BV(ADIE);
    TIMSK1 |= _BV(TOIE1);

    /* Waiting for interrupts */
    while(wait == 1)
    {
        _delay_ms(5);
    };

    /* If interrupt was not from TC1 show 0 on display and save TC1 value */
    if(duration < 65535)
    {
        display[2]=segment_digit[0];
        duration = TCNT1;
    }
    /* If interrupt was from TC1 show H on display,
       duration is set to max value in interrupt from TC1 */
    else
    {
        display[2]=segment_digit[13];
    }
    return(duration);
}

/**
  * @brief Convertin function for count time constant of measured capacitor
  */
void Time_to_capacity (void)
{
    /* If time constant is not measurable by TC1 */
    if (Time_constant == (2*65535) )
    {
        capacity = -2;  // means capacit is too high
    }
    /* If time constant is too low */
    else if (Time_constant < (109) )
    {
        capacity = -1; // means capacity is too low
    }
    else
    {
        /*  Counting capacity in microF (uF),
            Time_constant is twice bigger than actual time constant */
        capacity = (Time_constant/15624.76158)/(2*3.5);   /*15624.76158 TC1
                                                        ticks per one second*/
    }
}

/**
  * @brief Display valeu of capacity on seven-segment display
  */
void Display (void)
{
    /* varible for conversion between float and uint8_t */
    uint8_t cap;

    /* If capacity is to low, show LOW */
    if(capacity == -1)
    {
        display[0]=segment_digit[15];
        display[1]=segment_digit[14];
        display[2]=segment_digit[0];
        display[3]=segment_digit[12];
    }
    /* If capacity is to high, show HIGH */
    else if(capacity == -2)
    {
        display[0]=segment_digit[13];
        display[1]=segment_digit[6];
        display[2]=segment_digit[1];
        display[3]=segment_digit[13];
    }
    else
    {
        /* If capacity is 1uF or higher, show in format X.XXu */
        if(capacity >= 1)
        {
            cap=capacity/1;
            display[3]=segment_digit[cap] + DOT ;
            cap=floor(capacity*10);
            display[2]=segment_digit[cap%10];
            cap=floor(capacity*100);
            display[1]=segment_digit[cap%10];
            display[0]=segment_digit[10];
        }
        else
        {
            /* If capacity is 100nF or higher, show in format XXXn */
            capacity *= 10;
            if(capacity >= 1)
            {
                cap=capacity/1;
                display[3]=segment_digit[cap];
                cap=floor(capacity*10);
                display[2]=segment_digit[cap%10];
                cap=floor(capacity*100);
                display[1]=segment_digit[cap%10];
                display[0]=segment_digit[11];
            }
            else
            {
                /* If capacity is 10nF or higher, show in format XX.Xn */
                capacity *= 10;
                if(capacity >= 1)
                {
                    cap=capacity/1;
                    display[3]=segment_digit[cap];
                    cap=floor(capacity*10);
                    display[2]=segment_digit[cap%10]+DOT;
                    cap=floor(capacity*100);
                    display[1]=segment_digit[cap%10];
                    display[0]=segment_digit[11];
                }
                else
                {
                    /* If capacity is 1nF or higher, show in format X.XXn */
                    capacity *= 10;
                    if(capacity >= 1)
                    {
                        cap=capacity/1;
                        display[3]=segment_digit[cap] + DOT ;
                        cap=floor(capacity*10);
                        display[2]=segment_digit[cap%10];
                        cap=floor(capacity*100);
                        display[1]=segment_digit[cap%10];
                        display[0]=segment_digit[11];
                    }
                }
            }
        }
    }
}

/**
  * @brief Show digit at position of seven-segment display.
  * @param digit    - Value of capacity
  * @param position - Digit position 3 to 0
  */
void segment_putc(uint8_t digit, uint8_t position)
{
    uint8_t u8_i;
    /* Read current values from look-up tables */
    position = segment_position[3-position];

    /* Shift 16-bit value to shift registers
     * First byte represents digit value, second byte represents position
     * segments order (active low): DP g f e d c b a
     * position order (active high): x x x x pos0 pos1 pos2 pos3 */

    for(u8_i = 0u; u8_i < 8; u8_i ++)
    {
        if((digit << u8_i) & 0x80u)
            PORTB |= _BV(DATA_SHIFT);
        else
            PORTB &= ~_BV(DATA_SHIFT);

        _delay_us(1);
        segment_toggle_clk();
    }
    for(u8_i = 0u; u8_i < 8; u8_i ++)
    {
        if((position << u8_i) & 0x80u)
            PORTB |= _BV(DATA_SHIFT);
        else
            PORTB &= ~_BV(DATA_SHIFT);

        _delay_us(1);
        segment_toggle_clk();
    }
    /* Set latch input of shift register to high */
    PORTD |= _BV(LATCH_SHIFT);
    _delay_us(1);

    /* Set latch input of shift register to low */
    PORTD &= ~_BV(LATCH_SHIFT);
}

/**
  * @brief Toggle clock input of shift register.
  */
void segment_toggle_clk(void)
{
    /* Set clock input to high */
    PORTD |= _BV(CLK_SHIFT);
    _delay_us(1);

    /* Set clock input to low */
    PORTD &= ~_BV(CLK_SHIFT);
    _delay_us(1);
}

/**
  * @brief ADC interrupt - voltage level which is converted to digital value
  */
ISR(ADC_vect)
{
    uint16_t value = 0;
	/* Read 10-bit value from ADC */
    value = ADC;

    /* If value is over 3,161V while charging or under 1,839V while discharging,
                                        stop waiting and save TC1 value*/
    if (((voltage_level == 647) && (value >= voltage_level)) ||
                        ((voltage_level == 377) && (value <= voltage_level)))
    {
        wait = 0;
        duration = TCNT1H;
        duration = duration << 8;
        duration |= TCNT1L;
    }
}

/**
  * @brief TC1 interrupt - capacitor has not been charged yet
  */
ISR(TIMER1_OVF_vect)
{
    /* Stop "infinite" loop in (dis)charging function */
    wait = 0;

    /*Set time to maximum value, means capacitor is too high */
    duration = 65535;
}

/**
  * @brief TC0 interrupt - multiplexing seven-segment display
  */
ISR(TIMER0_OVF_vect)
{
    /* Multiplexing seven-segment display */
    if (segment==3)
        segment=0;
    else
        segment++;

        /* Diplay current character on current segment */
    segment_putc(display[segment],segment);

}

/* END OF FILE ****************************************************************/
