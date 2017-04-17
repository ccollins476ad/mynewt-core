/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2013 Semtech

Description: SX1276 driver specific target board functions implementation

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis and Gregory Cristian
*/
#include <assert.h>
#include "radio/board.h"
#include "sx1276/sx-radio.h"
#include "sx1276/sx1276.h"
#include "sx1276-board.h"

/*!
 * Flag used to set the RF switch control pins in low power mode when the radio is not active.
 */
static bool RadioIsActive = false;

/*!
 * Radio driver structure initialization
 */
const struct Radio_s Radio =
{
    SX1276Init,
    SX1276GetStatus,
    SX1276SetModem,
    SX1276SetChannel,
    SX1276IsChannelFree,
    SX1276Random,
    SX1276SetRxConfig,
    SX1276SetTxConfig,
    SX1276CheckRfFrequency,
    SX1276GetTimeOnAir,
    SX1276Send,
    SX1276SetSleep,
    SX1276SetStby,
    SX1276SetRx,
    SX1276StartCad,
    SX1276ReadRssi,
    SX1276Write,
    SX1276Read,
    SX1276WriteBuffer,
    SX1276ReadBuffer,
    SX1276SetMaxPayloadLength
};

/*!
 * Antenna switch GPIO pins objects
 */
// Not on sx1276 mbed card
//Gpio_t AntSwitchLf;
//Gpio_t AntSwitchHf;

void SX1276IoInit( void )
{

}

void SX1276IoIrqInit( DioIrqHandler **irqHandlers )
{
    int rc;

    rc = hal_gpio_init_out(RADIO_DIO_0, 1);
    assert(rc == 0);
    rc = hal_gpio_init_out(RADIO_DIO_1, 1);
    assert(rc == 0);
    rc = hal_gpio_init_out(RADIO_DIO_2, 1);
    assert(rc == 0);
    rc = hal_gpio_init_out(RADIO_DIO_3, 1);
    assert(rc == 0);
    rc = hal_gpio_init_out(RADIO_DIO_4, 1);
    assert(rc == 0);
    rc = hal_gpio_init_out(RADIO_DIO_5, 1);
    assert(rc == 0);
    rc = hal_gpio_init_out(RF_RXTX, 1);
    assert(rc == 0);

    rc = hal_gpio_irq_init(RADIO_DIO_0, irqHandlers[0], NULL,
                           HAL_GPIO_TRIG_RISING, HAL_GPIO_PULL_UP);
    assert(rc == 0);

    rc = hal_gpio_irq_init(RADIO_DIO_1, irqHandlers[1], NULL,
                           HAL_GPIO_TRIG_RISING, HAL_GPIO_PULL_UP);
    assert(rc == 0);

    rc = hal_gpio_irq_init(RADIO_DIO_2, irqHandlers[2], NULL,
                           HAL_GPIO_TRIG_RISING, HAL_GPIO_PULL_UP);
    assert(rc == 0);

    rc = hal_gpio_irq_init(RADIO_DIO_3, irqHandlers[3], NULL,
                           HAL_GPIO_TRIG_RISING, HAL_GPIO_PULL_UP);
    assert(rc == 0);

    rc = hal_gpio_irq_init(RADIO_DIO_4, irqHandlers[4], NULL,
                           HAL_GPIO_TRIG_RISING, HAL_GPIO_PULL_UP);
    assert(rc == 0);

    rc = hal_gpio_irq_init(RADIO_DIO_5, irqHandlers[5], NULL,
                           HAL_GPIO_TRIG_RISING, HAL_GPIO_PULL_UP);
    assert(rc == 0);
}

void SX1276IoDeInit( void )
{

}

uint8_t SX1276GetPaSelect( uint32_t channel )
{
    if( channel < RF_MID_BAND_THRESH )
    {
        return RF_PACONFIG_PASELECT_PABOOST;
    }
    else
    {
        return RF_PACONFIG_PASELECT_RFO;
    }
}

void SX1276SetAntSwLowPower( bool status )
{
    if( RadioIsActive != status )
    {
        RadioIsActive = status;

        if( status == false )
        {
            SX1276AntSwInit( );
        }
        else
        {
            SX1276AntSwDeInit( );
        }
    }
}

void SX1276AntSwInit( void )
{
    // Consider turning off GPIO pins for low power. They are always on right
    // now. GPIOTE library uses 0.5uA max when on, typical 0.1uA.
}

void SX1276AntSwDeInit( void )
{
    // Consider this for low power - ie turning off GPIO pins
}

void SX1276SetAntSw( uint8_t rxTx )
{
    if( rxTx != 0 ) // 1: TX, 0: RX
    {
        hal_gpio_write(SX1276_RXTX, 1);
    }
    else
    {
        hal_gpio_write(SX1276_RXTX, 0);
    }
}

bool SX1276CheckRfFrequency( uint32_t frequency )
{
    // Implement check. Currently all frequencies are supported
    return true;
}
