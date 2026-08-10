//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
//*******************************************************
// hal
//********************************************************

//-------------------------------------------------------
// ESP8285, ELRS GENERIC 900 RX hardware used as an example Tx
//-------------------------------------------------------

#define DEVICE_HAS_SINGLE_LED
#define DEVICE_HAS_COM_ON_SERIAL
#define DEVICE_HAS_NO_DEBUG


//-- UARTS
// UARTB = serial port

#define UARTB_USE_SERIAL
#define UARTB_BAUD                TX_SERIAL_BAUDRATE
#define UARTB_TXBUFSIZE           TX_SERIAL_TXBUFSIZE
#define UARTB_RXBUFSIZE           TX_SERIAL_RXBUFSIZE


//-- SX1: SX127x & SPI

#define SPI_CS_IO                 IO_P15
#define SPI_FREQUENCY             10000000L
#define SX_RESET                  IO_P2
#define SX_DIO                    IO_P4

IRQHANDLER(void SX_DIO_EXTI_IRQHandler(void);)

void sx_init_gpio(void)
{
    gpio_init(SX_RESET, IO_MODE_OUTPUT_PP_HIGH);
    gpio_init(SX_DIO, IO_MODE_INPUT_ANALOG);
}

IRAM_ATTR void sx_amp_transmit(void) {}
IRAM_ATTR void sx_amp_receive(void) {}

void sx_dio_init_exti_isroff(void) {}
void sx_dio_enable_exti_isr(void) { attachInterrupt(SX_DIO, SX_DIO_EXTI_IRQHandler, RISING); }
IRAM_ATTR void sx_dio_exti_isr_clearflag(void) {}


//-- Button

#define BUTTON                    IO_P0

void button_init(void)
{
    gpio_init(BUTTON, IO_MODE_INPUT_PU);
}

IRAM_ATTR bool button_pressed(void)
{
    return gpio_read_activelow(BUTTON) ? true : false;
}


//-- LEDs

#define LED_RED                   IO_P16

void leds_init(void)
{
    gpio_init(LED_RED, IO_MODE_OUTPUT_PP_LOW);
}

IRAM_ATTR void led_red_off(void) { gpio_low(LED_RED); }
IRAM_ATTR void led_red_on(void) { gpio_high(LED_RED); }
IRAM_ATTR void led_red_toggle(void) { gpio_toggle(LED_RED); }


//-- Serial or Com Switch
// GPIO0 cannot be held during reset because it selects the ROM bootloader.
// Signal a post-boot selection window by blinking the LED instead.

#ifdef DEVICE_HAS_COM_ON_SERIAL
bool ser_or_com_init(void) // return true for serial, false for com (CLI)
{
    constexpr uint16_t selection_window_ms = 5000;
    constexpr uint16_t required_press_ms = 250;
    uint16_t pressed_ms = 0;

    for (uint16_t elapsed_ms = 0; elapsed_ms < selection_window_ms; elapsed_ms++) {
        if ((elapsed_ms % 100) == 0) led_red_toggle();
        if (button_pressed()) {
            pressed_ms++;
            if (pressed_ms >= required_press_ms) {
                led_red_off();
                return false;
            }
        } else {
            pressed_ms = 0;
        }
        delay(1);
    }
    led_red_off();
    return true;
}
#endif


//-- POWER

#define POWER_GAIN_DBM            0
#define POWER_SX1276_MAX          SX1276_OUTPUT_POWER_MAX
#define POWER_USE_DEFAULT_RFPOWER_CALC

#define RFPOWER_DEFAULT           1

const rfpower_t rfpower_list[] = {
    { .dbm = POWER_0_DBM, .mW = 1 },
    { .dbm = POWER_10_DBM, .mW = 10 },
    { .dbm = POWER_17_DBM, .mW = 50 },
};
