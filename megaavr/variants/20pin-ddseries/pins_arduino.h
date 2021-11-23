/*  (C) Spence Konde 2021 open source (LGPL2.1 see LICENSE.md) based on exisisting Arduino cores.
//                                                                                  *INDENT-OFF*

 ###  #     # ####      ####  ####       ##   ###
#   # #     # #   #     #   # #   #     #  # #   #          #
#####  #   #  ####      #   # #   #       #  #   # ### ###     ###
#   #   # #   #  #      #   # #   #      #   #   #     #  # #  #  #
#   #    #    #   #     ####  ####      ####  ###      ###  #  #  #
===================================     ----------     #
Variant Definition file for generic DD parts           #
with 32 pins.

Part Numbers:
AVR64DA20 AVR32DA20 AVR16DD20

See VariantTemplate.h in extras folder an extensively annotated copy.

Include guard and include basic libraries. We are normally including this inside Arduino.h
*/

#ifndef Pins_Arduino_h
#define Pins_Arduino_h
#include <avr/pgmspace.h>
#include "timers.h"

#define DD_20PIN_PINOUT

        /*##  ### #   #  ###
        #   #  #  ##  # #
        ####   #  # # #  ###
        #      #  #  ##     #
        #     ### #   #  #*/

#define PIN_PA0 (0)
#define PIN_PA1 (1)
#define PIN_PA2 (2)
#define PIN_PA3 (3)
#define PIN_PA4 (4)
#define PIN_PA5 (5)
#define PIN_PA6 (6)
#define PIN_PA7 (7)
#define PIN_PC0 (8) // NOT_A_PIN
#define PIN_PC1 (9)
#define PIN_PC2 (10)
#define PIN_PC3 (11)
#define PIN_PD0 (12) // NOT_A_PIN
#define PIN_PD4 (16)
#define PIN_PD5 (17)
#define PIN_PD6 (18)
#define PIN_PD7 (19)
#define PIN_PF0 (20)
#define PIN_PF1 (21)
#define PIN_PF6 (22) // RESET
#define PIN_PF7 (23) // UPDI

        /*##   ##   ###  ###  ###  ###
        #   # #  # #      #  #    #
        ####  ####  ###   #  #     ###
        #   # #  #     #  #  #        #
        ####  #  # ####  ###  ###  #*/

#define PINS_COUNT                     24
#define NUM_ANALOG_INPUTS              19
// #define NUM_RESERVED_PINS            0     // These may at your option be defined,
// #define NUM_INTERNALLY_USED_PINS     0     // They will be filled in with defaults otherwise
// Autocalculated are :
// NUM_DIGITAL_PINS = PINS_COUNT - NUM_RESERVED_PINS
// TOTAL_FREE_OPINS = NUM_DIGITAL_PINS - NUM_INTERNALLY_USED_PINS
// Count of I2C and SPI pins will be defined as 2 and 3 but not used in further calculations. If you
// for some reason need to change this, define them here. Only ones not defined here get automatically set.

/* Until the legacy attach interrupt has been completelty obsoleted - this is such a waste here! */
#if !defined(LED_BUILTIN)
  #define LED_BUILTIN                  PIN_PA7
#endif
/* Until the legacy attach interrupt has been completelty obsoleted - this is such a waste here! */
#ifdef CORE_ATTACH_OLD
  #define EXTERNAL_NUM_INTERRUPTS        48
#endif

       /*   #  ###   ### ####   ###   ###
        ## ## #   # #    #   # #   # #
        # # # ##### #    ####  #   #  ###
        #   # #   # #    # #   #   #     #
        #   # #   #  ### #  #   ###   ##*/
// If you change the number of pins in any way or if the part has ADC on different pins from the board you are adapting
// you must ensure that these will do what they say they will do.

#define digitalPinToAnalogInput(p)           ((p) >= PIN_PD0 ? (((p) < PIN_PF0) ? (p) - PIN_PD0 : ((p) < PIN_PF6 ? ((p) - 4) : NOT_A_PIN)):((p) > PIN_PA1 ? (p) + 20 : NOT_A_PIN)) /* Seriously?! */
#define analogChannelToDigitalPin(p)         ((p) > 31 ? NOT_A_PIN : ((p) < 8 ? ((p) + PIN_PD0) : (p) > 21 ? (p) - 20 : (((p) == 16 ? PIN_PF0) : ((p) == 17 ? PIN_PF1 : NOT_A_PIN))))
#define analogInputToDigitalPin(p)                        analogChannelToDigitalPin((p) & 0x7F)
#define digitalOrAnalogPinToDigital(p)    (((p) & 0x80) ? analogChannelToDigitalPin((p) & 0x7F) : (((p)<=NUM_DIGITAL_PINS) ? (p) : NOT_A_PIN))
#define portToDigitalPinZero(port)        ((port) == 0 ? 0 : ((port)== 2 ? 8 : ((port)== 3 ? 12 : ((port)== 5 ? 20 : NOT_A_PIN))))

// PWM pins

#if defined(MILLIS_USE_TIMERB0)
  #define digitalPinHasPWMTCB(p) (((p) == PIN_PA3) || ((p) == PIN_PC0)
#elif defined(MILLIS_USE_TIMERB1)
  #define digitalPinHasPWMTCB(p) (((p) == PIN_PA2) || ((p) == PIN_PC0)
#elif defined(MILLIS_USE_TIMERB2)
  #define digitalPinHasPWMTCB(p) (((p) == PIN_PA2) || ((p) == PIN_PA3)
#else //no TCB's are used for millis
  #define digitalPinHasPWMTCB(p) (((p) == PIN_PA2) || ((p) == PIN_PA3) || ((p) == PIN_PC0)
#endif

// Timer pin mapping
#define TCA0_PINS PORTMUX_TCA0_PORTD_gc     // TCA0 output on PD[0:5]
#define TCB0_PINS 0x00                      // TCB0 output on PA2 (default), not PF4 (Doesn't exist here)
#define TCB1_PINS 0x00                      // TCB1 output on PA3 (default), not PF5 (Doesn't exist here)
#define TCB2_PINS 0x00                      // TCB2 output on PC0 (default), not PB4 (Doesn't exist here)
#define TCD0_PINS PORTMUX_TCD0_DEFAULT_gc

#define PIN_TCA0_WO0_INIT PIN_PD0
#define PIN_TCD0_WOA_INIT PIN_PA4

#define USE_TIMERD0_PWM
#define NO_GLITCH_TIMERD0

#define digitalPinHasPWM(p)               (digitalPinHasPWMTCB(p) || ((p) >= PIN_PA4 && (p) <= PIN_PA7) || ((p) >= PIN_PD0 && (p) < PIN_PD6))

        /*##   ###  ####  ##### #   # #   # #   #
        #   # #   # #   #   #   ## ## #   #  # #
        ####  #   # ####    #   # # # #   #   #
        #     #   # #  #    #   #   # #   #  # #
        #      ###  #   #   #   #   #  ###  #   */

#define SPI_INTERFACES_COUNT   1

// SPI 0
#define SPI_MUX                         PORTMUX_SPI0_DEFAULT_g
#define SPI_MUX_PINSWAP_3               PORTMUX_SPI0_ALT3_gc
#define SPI_MUX_PINSWAP_4               PORTMUX_SPI0_ALT4_gc
#define SPI_MUX_PINSWAP_5               PORTMUX_SPI0_ALT5_gc
#define SPI_MUX_PINSWAP_6               PORTMUX_SPI0_ALT6_gc
#define SPI_MUX_PINSWAP_NONE            PORTMUX_SPI0_NONE_gc
#define PIN_SPI_MOSI                    PIN_PA4
#define PIN_SPI_MISO                    PIN_PA5
#define PIN_SPI_SCK                     PIN_PA6
#define PIN_SPI_SS                      PIN_PA7
#define PIN_SPI_MOSI_PINSWAP_3          PIN_PA0
#define PIN_SPI_MISO_PINSWAP_3          PIN_PA1
#define PIN_SPI_SCK_PINSWAP_3           PIN_PC0
#define PIN_SPI_SS_PINSWAP_3            PIN_PC1
#define PIN_SPI_MOSI_PINSWAP_4          PIN_PD4
#define PIN_SPI_MISO_PINSWAP_4          PIN_PD5
#define PIN_SPI_SCK_PINSWAP_4           PIN_PD6
#define PIN_SPI_SS_PINSWAP_4            PIN_PD7
#define PIN_SPI_MOSI_PINSWAP_5          PIN_PC0
#define PIN_SPI_MISO_PINSWAP_5          PIN_PC1
#define PIN_SPI_SCK_PINSWAP_5           PIN_PC2
#define PIN_SPI_SS_PINSWAP_5            PIN_PC3
#define PIN_SPI_MOSI_PINSWAP_6          PIN_PC1
#define PIN_SPI_MISO_PINSWAP_6          PIN_PC2
#define PIN_SPI_SCK_PINSWAP_6           PIN_PC3
#define PIN_SPI_SS_PINSWAP_6            NOT_A_PIN

// TWI 0
#define PIN_WIRE_SDA                    PIN_PA2
#define PIN_WIRE_SCL                    PIN_PA3
#define PIN_WIRE_SDA_PINSWAP_1          PIN_PA2
#define PIN_WIRE_SCL_PINSWAP_1          PIN_PA3
#define PIN_WIRE_SDA_PINSWAP_2          PIN_PC2
#define PIN_WIRE_SCL_PINSWAP_2          PIN_PC3
#define PIN_WIRE_SDA_PINSWAP_3          PIN_PA0
#define PIN_WIRE_SCL_PINSWAP_3          PIN_PA1

#define NUM_HWSERIAL_PORTS              2
// USART 0
#define HWSERIAL0_MUX                   PORTMUX_USART0_DEFAULT_gc
#define HWSERIAL0_MUX_PINSWAP_1         PORTMUX_USART0_ALT1_gc
#define HWSERIAL0_MUX_PINSWAP_2         PORTMUX_USART0_ALT2_gc
#define HWSERIAL0_MUX_PINSWAP_3         PORTMUX_USART0_ALT3_gc
#define HWSERIAL0_MUX_PINSWAP_4         PORTMUX_USART0_ALT4_gc
#define HWSERIAL0_MUX_PINSWAP_NONE      PORTMUX_USART0_NONE_gc
#define PIN_HWSERIAL0_TX                PIN_PA0
#define PIN_HWSERIAL0_RX                PIN_PA1
#define PIN_HWSERIAL0_XCK               PIN_PA2
#define PIN_HWSERIAL0_XDIR              PIN_PA3
#define PIN_HWSERIAL0_TX_PINSWAP_1      PIN_PA4
#define PIN_HWSERIAL0_RX_PINSWAP_1      PIN_PA5
#define PIN_HWSERIAL0_XCK_PINSWAP_1     PIN_PA6
#define PIN_HWSERIAL0_XDIR_PINSWAP_1    PIN_PA7
#define PIN_HWSERIAL0_TX_PINSWAP_2      PIN_PA2
#define PIN_HWSERIAL0_RX_PINSWAP_2      PIN_PA3
#define PIN_HWSERIAL0_XCK_PINSWAP_2     NOT_A_PIN
#define PIN_HWSERIAL0_XDIR_PINSWAP_2    NOT_A_PIN
#define PIN_HWSERIAL0_TX_PINSWAP_3      PIN_PD4
#define PIN_HWSERIAL0_RX_PINSWAP_3      PIN_PD5
#define PIN_HWSERIAL0_XCK_PINSWAP_3     PIN_PD6
#define PIN_HWSERIAL0_XDIR_PINSWAP_3    PIN_PD7
#define PIN_HWSERIAL0_TX_PINSWAP_4      PIN_PC0
#define PIN_HWSERIAL0_RX_PINSWAP_4      PIN_PC1
#define PIN_HWSERIAL0_XCK_PINSWAP_4     PIN_PC2
#define PIN_HWSERIAL0_XDIR_PINSWAP_4    NOT_A_PIN


// USART1
#define HWSERIAL1_MUX                   PORTMUX_USART1_DEFAULT_gc
#define HWSERIAL1_MUX_PINSWAP_2         PORTMUX_USART1_ALT2_gc
#define HWSERIAL1_MUX_PINSWAP_NONE      PORTMUX_USART1_NONE_gc
#define PIN_HWSERIAL1_TX                NOT_A_PIN
#define PIN_HWSERIAL1_RX                PIN_PC1
#define PIN_HWSERIAL1_XCK               PIN_PC2
#define PIN_HWSERIAL1_XDIR              PIN_PC3
#define PIN_HWSERIAL1_TX_PINSWAP_2      PIN_PD6
#define PIN_HWSERIAL1_RX_PINSWAP_2      PIN_PD7
#define PIN_HWSERIAL1_XCK_PINSWAP_2     NOT_A_PIN
#define PIN_HWSERIAL1_XDIR_PINSWAP_2    NOT_A_PIN

        /*##  #   #  ###  #     ###   ###      ####  ### #   #  ###
        #   # ##  # #   # #    #   # #         #   #  #  ##  # #
        ##### # # # ##### #    #   # #  ##     ####   #  # # #  ###
        #   # #  ## #   # #    #   # #   #     #      #  #  ##     #
        #   # #   # #   # ####  ###   ###      #     ### #   #  #*/

#define PIN_A4   PIN_PD4
#define PIN_A5   PIN_PD5
#define PIN_A6   PIN_PD6
#define PIN_A7   PIN_PD7
#define PIN_A22  PIN_PA2
#define PIN_A23  PIN_PA3
#define PIN_A24  PIN_PA4
#define PIN_A25  PIN_PA5
#define PIN_A26  PIN_PA6
#define PIN_A27  PIN_PA7
#define PIN_A29  PIN_PC1
#define PIN_A30  PIN_PC2
#define PIN_A31  PIN_PC3
static const uint8_t A4  = PIN_A4;
static const uint8_t A5  = PIN_A5;
static const uint8_t A6  = PIN_A6;
static const uint8_t A7  = PIN_A7;
static const uint8_t A22 = PIN_A22;
static const uint8_t A23 = PIN_A23;
static const uint8_t A24 = PIN_A24;
static const uint8_t A25 = PIN_A25;
static const uint8_t A26 = PIN_A26;
static const uint8_t A27 = PIN_A27;
static const uint8_t A29 = PIN_A29;
static const uint8_t A30 = PIN_A30;
static const uint8_t A31 = PIN_A31;
#define AIN4  ADC_CH(4)
#define AIN5  ADC_CH(5)
#define AIN6  ADC_CH(6)
#define AIN7  ADC_CH(7)
#define AIN22 ADC_CH(22)
#define AIN23 ADC_CH(23)
#define AIN24 ADC_CH(24)
#define AIN25 ADC_CH(25)
#define AIN26 ADC_CH(26)
#define AIN27 ADC_CH(27)
#define AIN29 ADC_CH(29)
#define AIN30 ADC_CH(30)
#define AIN31 ADC_CH(31)


        /*##  ### #   #      ###  ####  ####   ###  #   #  ###
        #   #  #  ##  #     #   # #   # #   # #   #  # #  #
        ####   #  # # #     ##### ####  ####  #####   #    ###
        #      #  #  ##     #   # #  #  #  #  #   #   #       #
        #     ### #   #     #   # #   # #   # #   #   #    #*/

#ifdef ARDUINO_MAIN

const uint8_t digital_pin_to_port[] = {
  PA,         //  0 PA0/USART0_Tx/CLKIN
  PA,         //  1 PA1/USART0_Rx
  PA,         //  2 PA2/SDA
  PA,         //  3 PA3/SCL
  PA,         //  4 PA4/MOSI
  PA,         //  5 PA5/MISO
  PA,         //  6 PA6/SCK
  PA,         //  7 PA7/SS/CLKOUT/LED_BUILTIN
  PC,         //  8 PC0/USART1_Tx/TCA0 PWM
  PC,         //  9 PC1/USART1_Rx/TCA0 PWM
  PC,         // 10 PC2/TCA0 PWM
  PC,         // 11 PC3/TCA0 PWM
  PD,         // VDDIO2 - offset and position umdefined, but it is defined as being on port D because it is the zero pin. Gap pins in general should still be here.
  PD,         // 13 PD1/AIN1
  PD,         // 14 PD2/AIN2
  PD,         // 15 PD3/AIN3/LED_BUILTIN
  PD,         // 16 PD4/AIN4
  PD,         // 17 PD5/AIN5
  PD,         // 18 PD6/AIN6
  PD,         // 19 PD7/AIN7/AREF
  PF,         // 20 PF0/AIN16/USART2_Tx/TOSC1
  PF,         // 21 PF1/AIN17/USART2_Rx/TOSC2
  PF,         // 26 PF2
  PF,         // 27 PF3
  PF,         // 26 PF4
  PF,         // 27 PF5
  PF,         // 26 PF6 RESET
  PF          // 27 PF7 UPDI
};

/* Use this for accessing PINnCTRL register */
const uint8_t digital_pin_to_bit_position[] = { // *INDENT-OFF*
  #if CLOCK_SOURCE == 0 // PA0 used for external clock and crystal.
    PIN0_bp,//   0 PA0
  #else
    NOT_A_PIN,
  #endif
  #if CLOCK_SOURCE == 1   // PA1 also used for crystal
    NOT_A_PIN,
  #else // PA1 used for external crystal.
    PIN1_bp,//   1 PA1
  #endif    // *INDENT-ON*
  PIN2_bp,   //  2 PA2/SDA
  PIN3_bp,   //  3 PA3/SCL
  PIN4_bp,   //  4 PA4/MOSI
  PIN5_bp,   //  5 PA5/MISO
  PIN6_bp,   //  6 PA6/SCK
  PIN7_bp,   //  7 PA7/SS/CLKOUT
  PIN0_bp,   //  8 PC0/USART1_Tx
  PIN1_bp,   //  9 PC1/USART1_Rx
  PIN2_bp,   // 10 PC2
  PIN3_bp,   // 11 PC3
  NOT_A_PIN, // VDDIO2
  PIN1_bp,   // 13 PD1/AIN1
  PIN2_bp,   // 14 PD2/AIN2
  PIN3_bp,   // 15 PD3/AIN3/LED_BUILTIN
  PIN4_bp,   // 16 PD4/AIN4
  PIN5_bp,   // 17 PD5/AIN5
  PIN6_bp,   // 18 PD6/AIN6
  PIN7_bp,   // 19 PD7/AIN7/AREF
  PIN0_bp,   // 20 PF0/USART2_Tx/TOSC1
  PIN1_bp,   // 21 PF1/USART2_Rx/TOSC2
  PIN2_bp,   // 22
  PIN3_bp,   // 23
  PIN4_bp,   // 24
  PIN5_bp,   // 25
  PIN6_bp,   // 26 PF6 RESET
  PIN7_bp    // 27 PF7 UPDI
};

const uint8_t digital_pin_to_bit_mask[] = { // *INDENT-OFF*
  #if CLOCK_SOURCE == 0 // PA0 used for external clock and crystal.
    PIN0_bm,//   0 PA0
  #else
    NOT_A_PIN,
  #endif
  #if CLOCK_SOURCE == 1   // PA1 also used for crystal
    NOT_A_PIN,
  #else // PA1 used for external crystal.
    PIN1_bm,//   1 PA1
  #endif    // *INDENT-ON*
  PIN2_bm,   //  2 PA2/SDA
  PIN3_bm,   //  3 PA3/SCL
  PIN4_bm,   //  4 PA4/MOSI
  PIN5_bm,   //  5 PA5/MISO
  PIN6_bm,   //  6 PA6/SCK
  PIN7_bm,   //  7 PA7/SS/CLKOUT
  PIN0_bm,   //  8 PC0/USART1_Tx
  PIN1_bm,   //  9 PC1/USART1_Rx
  PIN2_bm,   // 10 PC2
  PIN3_bm,   // 11 PC3
  NOT_A_PIN, // VDDIO2
  PIN1_bm,   // 13 PD1/AIN1
  PIN2_bm,   // 14 PD2/AIN2
  PIN3_bm,   // 15 PD3/AIN3/LED_BUILTIN
  PIN4_bm,   // 16 PD4/AIN4
  PIN5_bm,   // 17 PD5/AIN5
  PIN6_bm,   // 18 PD6/AIN6
  PIN7_bm,   // 19 PD7/AIN7/AREF
  PIN0_bm,   // 20 PF0/USART2_Tx/TOSC1
  PIN1_bm,   // 21 PF1/USART2_Rx/TOSC2
  PIN2_bm,   // 22
  PIN3_bm,   // 23
  PIN4_bm,   // 24
  PIN5_bm,   // 25
  PIN6_bm,   // 26 PF6 RESET
  PIN7_bm    // 27 PF7 UPDI
};

const uint8_t digital_pin_to_timer[] = {
  NOT_ON_TIMER, //  0 PA0/USART0_Tx/CLKIN
  NOT_ON_TIMER, //  1 PA1/USART0_Rx
  TIMERB0,      //  2 PA2/SDA
  TIMERB1,      //  3 PA3/SCL
  TIMERD0,      //  4 PA4/MOSI      WOA
  TIMERD0,      //  5 PA5/MISO      WOB
  TIMERD0,      //  6 PA6/SCK       WOC mirrors WOA
  TIMERD0,      //  7 PA7/SS/CLKOUT WOD mirrors WOB
  TIMERB2,      //  8 PC0/USART1_Tx
  NOT_ON_TIMER, //  9 PC1/USART1_Rx
  NOT_ON_TIMER, // 10 PC2
  NOT_ON_TIMER, // 11 PC3
  NOT_ON_TIMER, // VDDIO2
  NOT_ON_TIMER, // 13 PD1/AIN1
  NOT_ON_TIMER, // 14 PD2/AIN2
  NOT_ON_TIMER, // 15 PD3/AIN3/LED_BUILTIN
  NOT_ON_TIMER, // 16 PD4/AIN4
  NOT_ON_TIMER, // 17 PD5/AIN5
  DACOUT,       // 18 PD6/AIN6
  NOT_ON_TIMER, // 19 PD7/AIN7/AREF
  NOT_ON_TIMER, // 20 PF0/USART2_Tx/TOSC1
  NOT_ON_TIMER, // 21 PF1/USART2_Rx/TOSC2
  NOT_ON_TIMER, // 22
  NOT_ON_TIMER, // 23
  NOT_ON_TIMER, // 24
  NOT_ON_TIMER, // 25
  NOT_ON_TIMER, // 26 PF6 RESET
  NOT_ON_TIMER, // 27 PF7 UPDI
};

#endif

#endif
