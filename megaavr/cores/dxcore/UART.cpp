/*
  UART.cpp - Hardware serial library for Wiring
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Created: 09.11.2017 07:29:09
  Author: M44307
  Modified considerably by Spence Konde 2018-2021, with particularly
  large changes in late 2020 to remove the interrupt priority stuff
  which could (very rarely) cause Serial to block in adverse
  conditions.
  Notable contriubutions from MX682X late 2021 to improve pin swap.
  Which I originally cribbed from MCUDude's MegaCoreX.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <util/atomic.h>
#include <avr/io.h>
#include "Arduino.h"

#include "UART.h"
#include "UART_private.h"
#include "UART_swap.h"

// this next line disables the entire UART.cpp if there's no hardware serial
#if defined(USART0) || defined(USART1) || defined(USART2) || defined(USART3) || defined(USART4) || defined(USART5)

// macro to guard critical sections when needed for large TX buffer sizes
#if (SERIAL_TX_BUFFER_SIZE > 256)
#define TX_BUFFER_ATOMIC ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
#else
#define TX_BUFFER_ATOMIC
#endif

/*##  ###  ####
  #  #     #   #
  #   ###  ####
  #      # # #
 ### ####  #  */

#if defined(USE_ASM_TXC) && USE_ASM_TXC == 1
  void __attribute__((naked)) __attribute__((used)) __attribute__((noreturn)) _do_txc(void){
    __asm__ __volatile__(   // we start with the r30 but not r31 preserved
      "_do_txc:"                  "\n\t"  // the low byte of the address of the USART is in r30.
        "push     r31"            "\n\t"  // push other half of Z register.
        "push     r24"            "\n\t"  // push r24
        "in       r24,     0x3f"  "\n\t"  // save sreg to r24
        "push     r24"            "\n\t"  // and push that. Next, finish usartn address..
        "ldi      r31,     0x08"  "\n\t"  // all USARTs are 0x08n0 where n is an even hex digit.
      "_txc_flush_rx:"            "\n\t"  // start of rx flush loop.
        "ld       r24,        Z"  "\n\t"  // rx data
        "ldd      r24,   Z +  4"  "\n\t"  // status
        "sbrs     r24,        7"  "\n\t"  // if RXC bit is set,...
        "rjmp     _txc_flush_rx"  "\n\t"  // .... skip this jump which jumps back a few lines to remove more
        "ldd      r24,   Z +  5"  "\n\t"  // read CTRLA
        "andi     r24,     0xBF"  "\n\t"  // clear TXCIE
        "ori      r24,     0x80"  "\n\t"  // set RXCIE
        "std   Z +  5,      r24"  "\n\t"  // store CTRLA
        "pop      r24"            "\n\t"  // pop r24, containing old sreg.
        "out     0x3f,      r24"  "\n\t"  // restore it
        "pop      r24"            "\n\t"  // pop r24 to get it's old value back
        "pop      r31"            "\n\t"  // and r31
        "pop      r30"            "\n\t"  // Pop the register the ISR did
        "reti"                    "\n"    // return from the interrupt.
        ::);
    __builtin_unreachable();
  }
#endif // otherwise is in USARTn
#if (defined(USE_ASM_RXC) && USE_ASM_RXC == 1 && (SERIAL_RX_BUFFER_SIZE == 256 || SERIAL_RX_BUFFER_SIZE == 128 || SERIAL_RX_BUFFER_SIZE == 64 || SERIAL_RX_BUFFER_SIZE == 32 || SERIAL_RX_BUFFER_SIZE == 16) /* && defined(USART1)*/ )
  // We only ever use this on the 2-series. 1-series doesn't gain anything with this. The inlining makes the compiler FAR more efficient. RXC isn't compiled stupidly,
  // the problem is that the ABI requires it to be inefficient as hell. But it's a big deal for the smaller size 2-series parts.
  void __attribute__((naked)) __attribute__((used)) __attribute__((noreturn)) _do_rxc(void){
    __asm__ __volatile__(
      "_do_rxc:"                      "\n\t" //
        "push       r18"              "\n\t" // r30 and r31 pushed before this.
        "in         r18,      0x3f"   "\n\t" // Save SREG
        "push       r18"              "\n\t" //
        "push       r24"              "\n\t" //
        "push       r25"              "\n\t" //
        "push       r28"              "\n\t" //
        "push       r29"              "\n\t" //
        "ldd        r28,    Z + 12"   "\n\t" // Load USART into Y pointer
        "ldd        r29,    Z + 13"   "\n\t" // We interact with the USART only this once
        "ldd        r24,    Y +  1"   "\n\t" // load high byte first
        "ld         r25,         Y"   "\n\t" // then low byte of RXdata
        "sbrc       r24,         1"   "\n\t" // if there's a parity error, then
        "rjmp  _end_rxc"              "\n\t" // do nothing more (framing errors are ok?)
        "ldd        r28,    Z + 19"   "\n\t" // load current head index      **<---OFFSET CHANGES with class structure**
        "ldi        r24,         1"   "\n\t" // Clear r24 and initialize it with 1
        "add        r24,       r28"   "\n\t" // add current head index to it
#if   SERIAL_RX_BUFFER_SIZE == 128
        "andi       r24,      0x7F"   "\n\t" // Wrap the head around
#elif SERIAL_RX_BUFFER_SIZE == 64
        "andi       r24,      0x3F"   "\n\t" // Wrap the head around
#elif SERIAL_RX_BUFFER_SIZE == 32
        "andi       r24,      0x1F"   "\n\t" // Wrap the head around
#elif SERIAL_RX_BUFFER_SIZE == 16
        "andi       r24,      0x0F"   "\n\t" // Wrap the head around
#elif SERIAL_RX_BUFFER_SIZE != 256
  #error "Can't happen - we already checked for unsupported buffer sizes!"
#endif
// otherwise it's 256, and wraps around naturally.
        "ldd        r18,    Z + 20"   "\n\t" // load tail index              **<---OFFSET CHANGES with class structure**
        "cp         r18,       r24"   "\n\t" // See if head is at tail. If so, buffer full,
        "breq  _end_rxc"              "\n\t" // can't do anything, just restore state and leave.
        "add        r28,       r30"   "\n\t" // r28 has what would be the next index in it.
        "mov        r29,       r31"   "\n\t" // and this is the high byte of serial instance
        "ldi        r18,         0"   "\n\t" // need a known zero to carry.
        "adc        r29,       r18"   "\n\t" // carry - Y is now pointing 23 bytes before head
        "std     Y + 23,       r25"   "\n\t" // store the new char in buffer **<---OFFSET CHANGES with class structure**
        "std     Z + 19,       r24"   "\n\t" // write that new head index.   **<---OFFSET CHANGES with class structure**
      "_end_rxc:"                     "\n\t" //
        "pop        r29"              "\n\t" // Y Pointer was used for head and usart
        "pop        r28"              "\n\t" //
        "pop        r25"              "\n\t" // r25 held the received character
        "pop        r24"              "\n\t" // r24 held rxdatah, then the new head.
        "pop        r18"              "\n\t" // Restore saved SREG
        "out       0x3f,       r18"   "\n\t" // and write back
        "pop        r18"              "\n\t" // used as tail pointer and z known zero.
        "pop        r31"              "\n\t" // end with Z which the isr pushed to make room for
        "pop        r30"              "\n\t" // pointer to serial instance
        "reti"                        "\n"   // return
        ::);
    __builtin_unreachable();

  }
#elif defined(USE_ASM_RXC) && USE_ASM_RXC == 1 && defined(USART1)
  #warning "USE_ASM_RXC is defined and this has more than one serial port, but the buffer size is not supported, falling back to the classical RXC."
#else
  void UartClass::_rx_complete_irq(UartClass& uartClass) {
    // if (bit_is_clear(*_rxdatah, USART_PERR_bp)) {
    uint8_t rxDataH = uartClass._hwserial_module->RXDATAH;
    uint8_t c = uartClass._hwserial_module->RXDATAL;  // no need to read the data twice. read it, then decide what to do
    rx_buffer_index_t rxHead = uartClass._rx_buffer_head;

    if (!(rxDataH & USART_PERR_bm)) {
      // No Parity error, read byte and store it in the buffer if there is room
      // unsigned char c = uartClass._hwserial_module->RXDATAL;
      rx_buffer_index_t i = (unsigned int)(rxHead + 1) % SERIAL_RX_BUFFER_SIZE;

      // if we should be storing the received character into the location
      // just before the tail (meaning that the head would advance to the
      // current location of the tail), we're about to overflow the buffer
      // and so we don't write the character or advance the head.
      if (i != uartClass._rx_buffer_tail) {
        uartClass._rx_buffer[rxHead] = c;
        uartClass._rx_buffer_head = i;
      }
    }
  }
#endif


#if defined(USE_ASM_DRE) && USE_ASM_DRE == 1 && \
           (SERIAL_RX_BUFFER_SIZE == 256 || SERIAL_RX_BUFFER_SIZE == 128 || SERIAL_RX_BUFFER_SIZE == 64 || SERIAL_RX_BUFFER_SIZE == 32 || SERIAL_RX_BUFFER_SIZE == 16) && \
           (SERIAL_TX_BUFFER_SIZE == 256 || SERIAL_TX_BUFFER_SIZE == 128 || SERIAL_TX_BUFFER_SIZE == 64 || SERIAL_TX_BUFFER_SIZE == 32 || SERIAL_TX_BUFFER_SIZE == 16)
  void __attribute__((naked)) __attribute__((used)) __attribute__((noreturn)) _do_dre(void){
    __asm__ __volatile__(
    "_do_dre:"                        "\n\t"
      "push        r18"               "\n\t"
      "in          r18,     0x3F"     "\n\t"
      "push        r18"               "\n\t"
      "push        r24"               "\n\t"
      "push        r25"               "\n\t"
      "push        r26"               "\n\t"
      "push        r27"               "\n\t"
      "set"                           "\n\t"  // SEt the T flag - we use this to determine how we got here and hence whether to rjmp to end of poll or reti
    "_poll_dre:"                      "\n\t"
      "push        r28"               "\n\t"
      "push        r29"               "\n\t"
      "ldi         r18,        0"     "\n\t"
      "ldd         r28,   Z + 12"     "\n\t"  // usart in Y
      "ldd         r29,   Z + 13"     "\n\t"  // usart in Y
      "ldd         r25,   Z + 22"     "\n\t"  // tx tail in r25 **<---OFFSET CHANGES with class structure**
      "movw        r26,      r30"     "\n\t"  // copy of serial in X
      "add         r26,      r25"     "\n\t"  // Serial + txtail
      "adc         r27,      r18"     "\n\t"  // Carry (X = &Serial tail )
#if   SERIAL_RX_BUFFER_SIZE == 256
      "subi        r26,     0xE9"     "\n\t"  //
      "sbci        r27,     0xFE"     "\n\t"  // +279
      "ld          r24,        X"     "\n\t"  // grab the character
#if   SERIAL_RX_BUFFER_SIZE == 128
      "subi        r26,     0x69"     "\n\t"  //                **<---VALUE CHANGES with class structure**
      "sbci        r27,     0xFF"     "\n\t"  // subtracting 0xFF69 is the same as adding 0x97 (+151 - 151 = 87 + 64)
      "ld          r24,        X"     "\n\t"  // grab the character
#elif SERIAL_RX_BUFFER_SIZE == 64
      "subi        r26,     0xA9"     "\n\t"  //                **<---VALUE CHANGES with class structure**
      "sbci        r27,     0xFF"     "\n\t"  // subtracting 0xFFA9 is the same as adding 0x57 (+87)
      "ld          r24,        X"     "\n\t"  // grab the character
#elif SERIAL_RX_BUFFER_SIZE == 32             // 0x37 = 55 = 87 - 32. as long as this is not more than 63 adiw works.
      "adiw        r26,     0x37"     "\n\t"  // +55            **<---VALUE CHANGES with class structure**
      "ld          r24,        X"     "\n\t"  // grab the character
#elif SERIAL_RX_BUFFER_SIZE == 16             // 0x27 = 39 = 87 - 32 - 17.
      "adiw        r26,     0x27"     "\n\t"  // +39            **<---VALUE CHANGES with class structure**
      "ld          r24,        X"     "\n\t"  // grab the character
#else
  #error "Can't happen - we already checked for unsupported buffer sizes!"
#endif
      "ldi         r18,     0x40"     "\n\t"
      "std       Y + 4,      r18"     "\n\t"  // clear TXC
      "std       Y + 2,      r24"     "\n\t"  // write char
      "subi        r25,     0xFF"     "\n\t"  // txtail +1
#if   SERIAL_TX_BUFFER_SIZE == 128
      "andi        r25,     0x7F"     "\n\t" // Wrap the head around
#elif SERIAL_TX_BUFFER_SIZE == 64
      "andi        r25,     0x3F"     "\n\t" // Wrap the head around
#elif SERIAL_TX_BUFFER_SIZE == 32
      "andi        r25,     0x1F"     "\n\t" // Wrap the head around
#elif SERIAL_TX_BUFFER_SIZE == 16
      "andi        r25,     0x0F"     "\n\t" // Wrap the head around
#elif SERIAL_RX_BUFFER_SIZE != 256
  #error "Can't happen - we already checked for unsupported buffer sizes!"
#endif
// otherwise it's 256, and wraps around naturally.
      "ldd         r24,   Y +  5"     "\n\t"  // get CTRLA into r24
      "ldd         r18,   Z + 21"     "\n\t"  // txhead into r18 **<---OFFSET CHANGES with class structure**
      "cpse        r18,      r25"     "\n\t"  // if they're the same
      "rjmp  _done_dre_irq"           "\n\t"
      "andi        r24,     0xDF"     "\n\t"  // DREIE off
      "std      Y +  5,      r24"     "\n\t"  // write new ctrla
    "_done_dre_irq:"                  "\n\t"  // Beginning of the end of the DRE
      "std      Z + 22,      r25"     "\n\t"  // store new tail **<---OFFSET CHANGES with class structure**
      "pop         r29"               "\n\t"  // pop Y
      "pop         r28"               "\n\t"  // finish popping Y
      "brts        .+2"               "\n\t"  // hop over the next insn if T bit set, means entered through do_dre, rather than poll_dre
      "rjmp _poll_dre_done"           "\n\t"  // we skip this jump
      "pop         r27"               "\n\t"  // and continue with popping registers.
      "pop         r26"               "\n\t"
      "pop         r25"               "\n\t"
      "pop         r24"               "\n\t"
      "pop         r18"               "\n\t"  // pop SREG value from stack
      "out        0x3f,     r18"      "\n\t"  // restore SREG
      "pop         r18"               "\n\t"  // pop old r18
      "pop         r31"               "\n\t"  // pop the Z that the isr pushed.
      "pop         r30"               "\n\t"
      "reti"                          "\n"   // and RETI!
      ::);
    __builtin_unreachable();
  }
#elif defined(USE_ASM_DRE) && USE_ASM_DRE == 1
  #warning "USE_ASM_DRE is defined, but the buffer sizes are not supported, falling back to the classical DRE."
#else
  void UartClass::_tx_data_empty_irq(UartClass& uartClass) {
    USART_t* usartModule = (USART_t*)uartClass._hwserial_module;  // reduces size a little bit
    tx_buffer_index_t txTail = uartClass._tx_buffer_tail;
  /*
    // Check if tx buffer already empty. when called by _poll_tx_data_empty()
    if (uartClass._tx_buffer_head == txTail) {
      // Buffer empty, so disable "data register empty" interrupt
      usartModule->CTRLA &= (~USART_DREIE_bm);
      return;
    } // moved to poll function to make ISR smaller and faster
  */

    // There must be more data in the output
    // buffer. Send the next byte
    uint8_t c = uartClass._tx_buffer[txTail];

    // clear the TXCIF flag -- "can be cleared by writing a one to its bit
    // location". This makes sure flush() won't return until the bytes
    // actually got written. It is critical to do this BEFORE we write the next byte
    usartModule->STATUS = USART_TXCIF_bm;
    usartModule->TXDATAL = c;

    txTail = (txTail + 1) & (SERIAL_TX_BUFFER_SIZE - 1);  //% SERIAL_TX_BUFFER_SIZE;
    uint8_t ctrla = usartModule->CTRLA;
    if (uartClass._tx_buffer_head == txTail) {
      // Buffer empty, so disable "data register empty" interrupt
      ctrla &= ~(USART_DREIE_bm);
      usartModule->CTRLA = ctrla;
    }
    uartClass._tx_buffer_tail = txTail;
  }
#endif

// To invoke data empty "interrupt" via a call, use this method
void UartClass::_poll_tx_data_empty(void) {
  if ((!(SREG & CPU_I_bm)) ||  CPUINT.STATUS) {
    // We're here because we're waiting for space in the buffer *or* we're in flush
    // and waiting for the last byte to leave, yet we're either in an ISR, or
    // interrupts are disabled so the ISR can't fire on it's own.
    //
    // Interrupts are disabled either globally or for data register empty,
    // or we are in another ISR. (It doesn't matter *which* ISR we are in
    // whether it's another level 0, the priority one, or heaven help us
    // the NMI, if the user code says to print something or flush the buffer
    // we might as well do it. It is entirely plausible that an NMI might
    // attempt to print out some sort of record of what happened.
    //
    // so we'll have to poll the "data register empty" flag ourselves.
    // If it is set, pretend an interrupt has happened and call the handler
    // to free up space for us.
    // -Spence 10/23/20
    // Invoke interrupt handler only if conditions data register is empty
    if ((*_hwserial_module).STATUS & USART_DREIF_bm) {
      if (_tx_buffer_head != _tx_buffer_tail) {
        // Buffer empty, so disable "data register empty" interrupt
        (*_hwserial_module).CTRLA &= (~USART_DREIE_bm);

        return;
      }
      #if !(defined(USE_ASM_DRE) && USE_ASM_DRE == 1 && \
                   (SERIAL_RX_BUFFER_SIZE == 128 || SERIAL_RX_BUFFER_SIZE == 64 || SERIAL_RX_BUFFER_SIZE == 32 || SERIAL_RX_BUFFER_SIZE == 16) && \
                   (SERIAL_TX_BUFFER_SIZE == 128 || SERIAL_TX_BUFFER_SIZE == 64 || SERIAL_TX_BUFFER_SIZE == 32 || SERIAL_TX_BUFFER_SIZE == 16))
        _tx_data_empty_irq(*this);
      #else
        void * thisSerial = this;
        __asm__ __volatile__(
                "clt"              "\n\t" // Clear the T flag to signal to the ISR that we got there from here.
                "rjmp _poll_dre"   "\n\t"
                "_poll_dre_done:"    "\n"
                ::"z"((uint16_t)thisSerial)
                : "r18","r19","r24","r25","r26","r27");
      #endif
    }
    // In case interrupts are enabled, the interrupt routine will be invoked by itself
    // Note that this currently does not handle cases where the DRE interruopt becomes
    // disabled, yet you are actually attempting to send. I don't think it can happen.
  }
}


/*###  #   # ####  #    ###  ###     #   # #### ##### #   #  ###  ####   ###
 #   # #   # #   # #     #  #        ## ## #      #   #   # #   # #   # #
 ####  #   # ####  #     #  #        # # # ###    #   ##### #   # #   #  ###
 #     #   # #   # #     #  #        #   # #      #   #   # #   # #   #     #
 #      ###  ####  #### ###  ###     #   # ####   #   #   #  ###  ####   ##*/

// Invoke this function before 'begin' to define the pins used
bool UartClass::pins(uint8_t tx, uint8_t rx) {
  uint8_t ret_val = _pins_to_swap(_usart_pins, _mux_count, tx, rx);   // return 127 when correct swap number wasn't found
  return swap(ret_val);
}

bool UartClass::swap(uint8_t newmux) {
  if (newmux < _mux_count) {
    _pin_set = newmux;
    return true;
  } else if (newmux == MUX_NONE) {  // 128 codes for MUX_NONE
    _pin_set = _mux_count;
    return true;
  } else {
    _pin_set = 0;
  }
  return false;
}

void UartClass::begin(unsigned long baud, uint16_t options) {
  // Make sure no transmissions are ongoing and USART is disabled in case begin() is called by accident
  // without first calling end()
  if (_state & 1) {
    this->end();
  }

  uint8_t ctrlc = (uint8_t) options;
  if (ctrlc == 0) {                                 // low byte of 0 could mean they want SERIAL_5N1.
    ctrlc = (uint8_t)SERIAL_8N1;                    // Or that they were expecting to modify the default.
  }                                                 // unused bit 0x04 is set 1 for the special case of 5N1 only, and we unset it...
  ctrlc &= ~0x04;                                   // ... as none of the values with it set are supported.
  uint8_t   ctrla =(uint8_t) (options >> 8);        // CTRLA will get the remains of the options high byte.
  uint16_t baud_setting = 0;                        // at this point it should be able to reuse those 2 registers that it received options in!
  uint8_t         ctrlb = (~ctrla & 0xC0);          // Top two bits (TXEN RXEN), inverted so they match he sense in the registers - we could return here, but we don't cae about speed, and it'd be 4 more bytes
  if (baud > F_CPU / 16) {                          // if this baud is too fast for non-U2X
    ctrlb              |= USART_RXMODE0_bm;         // set the U2X bit in what will become CTRLB
    baud              >>= 1;                        // And lower the baud rate by haldf
  }
  baud_setting          = (((4 * F_CPU) / baud));   // And now the registers that baud was passed in are done.
  if (baud_setting < 64){                           // so set to the maximum baud rate setting.
    baud_setting = 64;       // set the U2X bit in what will become CTRLB
  }
  // Baud setting done now we do the other options.
  // that aren't in CTRLC;
  ctrla &= 0x2B;                            // Only LBME and RS485 (both of them); will get written to CTRLA, but we leave the event bit.
  if (ctrlb & USART_RXEN_bm) {              // if RX is to be enabled
    ctrla  |= USART_RXCIE_bm;               // we will want to enable the ISR.
  }
  uint8_t setpinmask = ctrlb & 0xC8;        // ODME in bit 3, TX and RX enabled in bit 6, 7
  if ((ctrla & USART_LBME_bm) && (setpinmask == 0xC8)) { // if it's open-drain and loopback AND both TX and RX are enabled, need to set state bit 2.
    _state                 |= 2;            // since that changes some behavior (RXC disabled while sending) // Now we should be able to ST _state.
    setpinmask             |= 0x10;         // this tells _set_pins not to disturb the configuration on the RX pin.
  }
  if (ctrla & USART_RS485_bm) {             // RS485 mode recorded here too... because we need to set
    setpinmask             |= 0x01;         // set pin output if we need to do that.
  }
  uint8_t oldSREG = SREG;
  cli();
  volatile USART_t* MyUSART = _hwserial_module;
  (*MyUSART).CTRLB          = 0;            // gotta disable first - some things are enable-locked.
  (*MyUSART).CTRLC          = ctrlc;        // No reason not to set first.
  (*MyUSART).BAUD           = baud_setting; // Wish I could have set it long ago
  if (ctrla & 0x20) {                       // Now we have to do a bit of work
    setpinmask             &= 0x7F;         // Remove the RX pin in this case because we get the input from elsewhere.
    (*MyUSART).EVCTRL       = 1;            // enable event input - not clear from datasheet what's needed to
    (*MyUSART).TXPLCTRL     = 0xFF;         // Disable pulse length encoding.
  } else {
    (*MyUSART).EVCTRL       = 0;            // This needs to be turned off when not in use.
  }                                         // finally strip out the SERIAL_EVENT_RX bit which is in the DREIE
  (*MyUSART).CTRLA          = ctrla & 0xDF; // position, which we never set in begin.
  (*MyUSART).CTRLB          = ctrlb;        // Set the all important CTRLB...
  _set_pins(_usart_pins, _mux_count, _pin_set, setpinmask);
  SREG=oldSREG;
}

void UartClass::end() {
  // wait for transmission of outgoing data
  flush();
  // Disable receiver and transmitter as well as the RX complete and the data register empty interrupts.
  volatile USART_t * temp = _hwserial_module; /* compiler does a slightly better job with this. */
  temp -> CTRLB &= 0; //~(USART_RXEN_bm | USART_TXEN_bm);
  temp -> CTRLA &= 0; //~(USART_RXCIE_bm | USART_DREIE_bm | USART_TXCIE_bm);
  temp -> STATUS =  USART_TXCIF_bm; // want to make sure no chanceofthat firing in error. TXCIE only used in half duplex
  // clear any received data
  _rx_buffer_head = _rx_buffer_tail;
  // Note: Does not change output pins
  // though the datasheetsays turning the TX module sets it to input.
  _state = 0;
}

int UartClass::available(void) {
  return ((unsigned int)(SERIAL_RX_BUFFER_SIZE + _rx_buffer_head - _rx_buffer_tail)) & (SERIAL_RX_BUFFER_SIZE - 1);   //% SERIAL_RX_BUFFER_SIZE;
}

int UartClass::peek(void) {
  if (_rx_buffer_head == _rx_buffer_tail) {
    return -1;
  } else {
    return _rx_buffer[_rx_buffer_tail];
  }
}

int UartClass::read(void) {
  // if the head isn't ahead of the tail, we don't have any characters
  if (_rx_buffer_head == _rx_buffer_tail) {
    return -1;
  } else {
    unsigned char c = _rx_buffer[_rx_buffer_tail];
    _rx_buffer_tail = (rx_buffer_index_t)(_rx_buffer_tail + 1) & (SERIAL_RX_BUFFER_SIZE - 1);   // % SERIAL_RX_BUFFER_SIZE;
    return c;
  }
}

int UartClass::availableForWrite(void) {
  tx_buffer_index_t head;
  tx_buffer_index_t tail;

  TX_BUFFER_ATOMIC {
    head = _tx_buffer_head;
    tail = _tx_buffer_tail;
  }
  if (head >= tail) {
    return SERIAL_TX_BUFFER_SIZE - 1 - head + tail;
  }
  return tail - head - 1;
}

void UartClass::flush() {
  // If we have never written a byte, no need to flush. This special
  // case is needed since there is no way to force the TXCIF (transmit
  // complete) bit to 1 during initialization
  if (!(_state & 1)) {
    return;
  }

  // Check if we are inside an ISR already (e.g. connected to a different peripheral then UART), in which case the UART ISRs will not be called.
  // Spence 10/23/20: Changed _poll_tx_data_empty() to instead call the ISR directly in this case too
  // Why elevate the interrupt if we're going to go into a busywait loop checking if the interrupt is disabled and if so, check for the bit and
  // manually call the ISR if the bit is set... *anyway*? Plus, in write(), this mode will be enabled upon a write of a single character from an ISR
  // and will stay that way until the buffer is empty, which would mean that the fairly long and slow UART TX ISR would have priority over a
  // potentially very fast interrupt that the user may have set to priority level 1. Just because a whizz-bang feature is there doesn't mean
  // it's appropriate to use for applications where it has only very small benefits, and significant risk of surprising the user and causing
  // breakage of code that would otherwise work. Finally, the previous implementation didn't check if it was called from the current lvl1 ISR
  // and in that case flush(), and write() with full buffer would just straight up hang...


  // Spin until the data-register-empty-interrupt is disabled and TX complete interrupt flag is raised
  while (((*_hwserial_module).CTRLA & USART_DREIE_bm) || (!((*_hwserial_module).STATUS & USART_TXCIF_bm))) {
    // poll this, which will ensure that bytes keep getting sent even if interrupts are disabled or something.
    _poll_tx_data_empty();
  }
  // When we get here, nothing is queued anymore (DREIE is disabled) and
  // the hardware finished transmission (TXCIF is set).
}

// Static
void UartClass::_mux_set(uint8_t* mux_table_ptr, uint8_t mux_count, uint8_t mux_code) {
#if HWSERIAL_MUX_REG_COUNT > 1  // for big pincount devices that have more then one USART PORTMUX register
  uint8_t* mux_info_ptr = mux_table_ptr + (mux_count * USART_PINS_WIDTH) + 1;
  uint16_t mux_options_off_gm = pgm_read_word_near(mux_info_ptr);  /* pointer offset to the second columun
  at the end bottom row of the table, with info about the mux options, rather than a specefic option
  Low byte is the offset from USARTROUTEA, second byte is the group mask. */
  volatile uint8_t* portmux  = (uint8_t*)(HWSERIAL_MUX_REGISTER_BASE + (uint8_t)mux_options_off_gm); // offset
  uint8_t temp   = *portmux;
  temp          &= ~((uint8_t) (mux_options_off_gm >> 8)); // Group Mask
  temp          |= mux_code;
  *portmux       = temp;
#else
  uint8_t* mux_info_ptr = mux_table_ptr + (mux_count * USART_PINS_WIDTH) + 2;
  /* Only one register, so no offset, so only read a byte */
  uint8_t mux_mask = pgm_read_byte_near(mux_info_ptr);     // only read the group mask
  volatile uint8_t* portmux = (uint8_t*)(HWSERIAL_MUX_REGISTER_BASE);
  uint8_t temp   = *portmux;
  temp          &= ~(mux_mask);
  temp          |= mux_code;
  *portmux       = temp;
#endif
}

// Note that *no attempt is made* to detect and react to incomplete pinsets. It is the resposnability of the user to pick a pinset that contains
// all the pins they need. Unless you're only receiving, pinset 0, USART1, AVR DD14 or DD20 isn't much use.
// Likewise, you can't do RS485 on DD/EA USART0 pinset 4.
// and no RS485 nor any clocked modes with pinset 1 of USART3 on a 48-pin part, or pinset 1 on USART2 anywhere
// and the unfortunate souls on 28-pin parts can't use it on USART2 at all (though this is hardle the greatest of their woes).
// enmask: 0b rt_l o__s where r and r are 1 if RX and TX are enabled, l and o are loopback and open drain, and s is RS485 mode.
// That gives 32 options, though RX485 is independent, so the 16 options can be described as:
// RX TX LB OD      Result:
//  0  0  x  x   *  No pins are changed, usart disabled.
//  0  1  x  0      TX set output, RX not changed.
//  0  1  x  1      TX set input pulleup, RX not changed.
//  1  0  1  x   *  TX set input pullup.
//  1  1  0  0      TX set output, RX set input pullup.
//  1  1  0  1      TX and RX set input pullup.
//  1  1  1  0      TX set output, RX not changed. Loopback mode: you can only see what you send since TX is OUTPUT and that's what's connected to RX.
//  1  1  1  1      TX set input pullup. Half-duplex mode.
// * indicates that RS485 mode if requested will be enabled, even though it is inappropriate. These configurations are documented unsupported.
// Static
void UartClass::_set_pins(uint8_t* mux_table_ptr, uint8_t mux_count, uint8_t mux_setting, uint8_t enmask) {
  uint8_t* mux_row_ptr   = mux_table_ptr + (mux_setting * USART_PINS_WIDTH);
  uint16_t mux_row_gc_tx = pgm_read_word_near(mux_row_ptr); // Clever trick for faster PGM reads of consecutive bytes!
  uint8_t mux_group_code = (uint8_t) (mux_row_gc_tx);       // this is the mux
  if (mux_setting < mux_count) {              // if false, pinmux none was selected, and we skip the pin configuration.
    uint8_t mux_pin_tx   = (uint8_t) (mux_row_gc_tx >> 8);
    if ((enmask & 0x40 && !(enmask & 0x08))) {
      pinMode(mux_pin_tx, OUTPUT);            // If and only if TX is enabled and open drain isn't should the TX pin be output.
    } else if (enmask & 0x50) {               // if it is enabled but is in open drain mode, or is disabled, but loopback is enabled
      pinMode(mux_pin_tx, INPUT_PULLUP);      // TX should be INPUT_PULLUP.
    }
    if (enmask & 0x80 && !(enmask & 0x10)) {  // Likewise if RX is enabled, unless loopback mode is too
      pinMode(mux_pin_tx + 1, INPUT_PULLUP);  // (in which case we caught it above), it should be pulled up
    }
    if (enmask & 1) {
      pinMode(mux_pin_tx + 3, OUTPUT);        // in RS485 mode we need to make sure that XDIR is an output
    }
  }
  _mux_set(mux_table_ptr, mux_count, mux_group_code);
}


// Static
uint8_t UartClass::_pins_to_swap(uint8_t* mux_table_ptr, uint8_t mux_count, uint8_t tx_pin, uint8_t rx_pin) {
  if (tx_pin == NOT_A_PIN && rx_pin == NOT_A_PIN) {
    return  128;            // get MUX_NONE
  } else {
    rx_pin -=tx_pin;        // Test if these *could* match each other.
    if(rx_pin == 1) {       // thus far no parts where the pins are not adjacent! */
      mux_table_ptr++;                                              // prepare to read the information with one byte offset
      for (uint8_t i = 0; i < mux_count; i++) {                     // until we hit the end of this USART's mux...
        uint8_t mux_tx_pin = pgm_read_byte_near(mux_table_ptr);     // read tx pin, which we are now pointing at.
        if (tx_pin == mux_tx_pin) { // if it's the tx pin, and we know that the rx pin matches it.
          return i;                 // return the swap number. The first line checked that the rx pin's must match this, since rx pins on all parts thus far are tx + 1.
        }
        mux_table_ptr += USART_PINS_WIDTH; // otherwise try the next mux option
      }
    }
    return NOT_A_MUX; // At this point, we have checked all group codes for this peripheral. It ain't there. Return NOT_A_MUX.
  }
}
// Not static
uint8_t UartClass::getPin(uint8_t pin) {
  return _getPin(_usart_pins, pin, _pin_set, _mux_count);
}
// Static
uint8_t UartClass::_getPin(uint8_t * mux_table_ptr, uint8_t muxcount, uint8_t pinset, uint8_t pin) {
  if (pin >3 || pinset > muxcount) {
    return NOT_A_PIN;
  }
  mux_table_ptr += USART_PINS_WIDTH * pinset;
  if (pgm_read_byte_near(mux_table_ptr++) == NOT_A_MUX) {
    return NOT_A_PIN; // somehow an invalid pinset is selected...
  }
  if (pin > 1) {
    mux_table_ptr++;
  }
  uint8_t base = pgm_read_byte_near(mux_table_ptr); // TX or XCK
  if (base == NOT_A_PIN) {
    return NOT_A_PIN;
  }
  #if (defined(__AVR_DD__) && _AVR_PINCOUNT < 28)
    if ((base == PIN_PC0 && pin == 0) || (base == PIN_PC3 && pin == 3)) {
      return NOT_A_PIN;/* these are the only cases where RX exists without TX, or XDIR exists without XCK */
    } /* and the only case where only one of the first two pins exists.
       * There are many examples where TX/RX exist by XCK doesn't, but these are already handled.
       */
  #endif
  if (pin & 1) {
    base++;
  }
  return base; // RX = TX + 1. XDIR = XCK + 1 for all Dx and Ex parts!
}


size_t UartClass::write(uint8_t c) {
  _state |= 1;

  // If the buffer and the data register is empty, just write the byte
  // to the data register and be done. This shortcut helps
  // significantly improve the effective data rate at high (>
  // 500kbit/s) bit rates, where interrupt overhead becomes a slowdown.
  if ((_tx_buffer_head == _tx_buffer_tail) && ((*_hwserial_module).STATUS & USART_DREIF_bm)) {
    if (_state & 2) { // in half duplex mode, we turn off RXC interrupt
      uint8_t ctrla               = (*_hwserial_module).CTRLA;
      ctrla                      &= ~USART_RXCIE_bm;
      ctrla                      |= USART_TXCIE_bm;
      (*_hwserial_module).STATUS  = USART_TXCIF_bm;
      (*_hwserial_module).CTRLA   = ctrla;
    } else {
      (*_hwserial_module).STATUS  = USART_TXCIF_bm;
    }
    // MUST clear TXCIF **before** writing new char, otherwise ill-timed interrupt can cause it to erase the flag after the new charchter has been sent!
    (*_hwserial_module).TXDATAL   = c;


/*   * I cannot figure out *HOW* the DRE could be enabled at this point (buffer empty and DRE flag up)
     * When the buffer was emptied, it would have turned off the DREI after it loaded the last byte.
     * Thus, the only possible way this could happen is if an interrupt also tried to write to serial,
     * *immediately* after we checked that the buffer was empty, before we made it not empty. And
     * in that case, without this line it would lose one of the characters... with that line, it could
     * stop servicing DRE until another serial write, AND lose a character. I think it's a full 10 bytes
     * 2 to read with LDS, 1 to modify, 2 to write back with STS. It's gone!    -Spence 4/2021
     * Original comments:
     // Make sure data register empty interrupt is disabled to avoid
     // that the interrupt handler is called in this situation
     (*_hwserial_module).CTRLA &= (~USART_DREIE_bm);
*/

    return 1;
  }

  tx_buffer_index_t i = (_tx_buffer_head + 1) & (SERIAL_TX_BUFFER_SIZE - 1);  // % SERIAL_TX_BUFFER_SIZE;

  // If the output buffer is full, there's nothing for it other than to
  // wait for the interrupt handler to empty it a bit (or emulate interrupts)
  while (i == _tx_buffer_tail) {
    _poll_tx_data_empty();
  }
  _tx_buffer[_tx_buffer_head]  = c;
  _tx_buffer_head              = i;
  if (_state & 2) { // in half duplex mode, we turn off RXC interrupt
    uint8_t ctrla              = (*_hwserial_module).CTRLA;
    ctrla                     &= ~USART_RXCIE_bm;
    ctrla                     |= USART_TXCIE_bm | USART_DREIE_bm;
    (*_hwserial_module).STATUS = USART_TXCIF_bm;
    (*_hwserial_module).CTRLA  = ctrla;
  } else { // Enable "data register empty interrupt"
    (*_hwserial_module).CTRLA |= USART_DREIE_bm;
  }

  return 1;
}

void UartClass::printHex(const uint8_t b) {
  char x = (b >> 4) | '0';
  if (x > '9')
    x += 7;
  write(x);
  x = (b & 0x0F) | '0';
  if (x > '9')
    x += 7;
  write(x);
}

void UartClass::printHex(const uint16_t w, bool swaporder) {
  uint8_t *ptr = (uint8_t *) &w;
  if (swaporder) {
    printHex(*(ptr++));
    printHex(*(ptr));
  } else {
    printHex(*(ptr + 1));
    printHex(*(ptr));
  }
}

void UartClass::printHex(const uint32_t l, bool swaporder) {
  uint8_t *ptr = (uint8_t *) &l;
  if (swaporder) {
    printHex(*(ptr++));
    printHex(*(ptr++));
    printHex(*(ptr++));
    printHex(*(ptr));
  } else {
    ptr+=3;
    printHex(*(ptr--));
    printHex(*(ptr--));
    printHex(*(ptr--));
    printHex(*(ptr));
  }
}

uint8_t * UartClass::printHex(uint8_t* p, uint8_t len, char sep) {
  for (byte i = 0; i < len; i++) {
    if (sep && i) write(sep);
    printHex(*p++);
  }
  println();
  return p;
}

uint16_t * UartClass::printHex(uint16_t* p, uint8_t len, char sep, bool swaporder) {
  for (byte i = 0; i < len; i++) {
    if (sep && i) write(sep);
    printHex(*p++, swaporder);
  }
  println();
  return p;
}
volatile uint8_t * UartClass::printHex(volatile uint8_t* p, uint8_t len, char sep) {
  for (byte i = 0; i < len; i++) {
    if (sep && i) write(sep);
    uint8_t t = *p++;
    printHex(t);
  }
  println();
  return p;
}
volatile uint16_t * UartClass::printHex(volatile uint16_t* p, uint8_t len, char sep, bool swaporder) {
  for (byte i = 0; i < len; i++) {
    if (sep && i) write(sep);
    uint16_t t = *p++;
    printHex(t, swaporder);
  }
  println();
  return p;
}
#endif
