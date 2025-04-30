/*
 * ESP32 GPIO emulation
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
// Modified by Santiago Gonzalez, April 2025

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/esp32_gpio.h"


static uint64_t esp32_gpio_read( void *opaque, hwaddr addr, unsigned int size )
{
    Esp32GpioState *s = ESP32_GPIO(opaque);
    uint64_t r = 0;

    switch( addr )
    {
    case 0x04: r = s->gpio_out;    break; //GPIO_OUT_REG
    case 0x20: r = s->gpio_enable; break; //GPIO_ENABLE_REG
    case 0x38: r = s->strap_mode;  break; //A_GPIO_STRAP:
    case 0x3C: r = s->gpio_in;            //GPIO_IN_REG
        qemu_set_irq( s->gpios_sync[0], -1 ); //request sync
        break;
    case 0x40: r = s->gpio_in1;           //GPIO_IN1_REG
        qemu_set_irq( s->gpios_sync[0], -1 ); //request sync
        break;
    case 0x44: r = s->gpio_status;    break; //GPIO_STATUS_REG
    case 0x50: r = s->gpio_status1;   break; //GPIO_STATUS1_REG
    case 0x60: r = s->gpio_acpu_int;  break; //GPIO_ACPU_INT_REG
    case 0x68: r = s->gpio_pcpu_int;  break; //GPIO_PCPU_INT_REG
    case 0x74: r = s->gpio_acpu_int1; break; //GPIO_ACPU_INT1_REG
    case 0x7c: r = s->gpio_pcpu_int1; break; //GPIO_PCPU_INT1_REG
    default:                          break;
    }
    if( addr >= 0x88 && addr < 0x130 ) { //GPIO_PINXX_REG
        int n = (addr - 0x88) / 4;
        r = s->gpio_pin[n];
    }
    else if( addr >= 0x130 && addr < 0x530 ) { //GPIO_FUNCY_IN_SEL_CFG_REG
        int n = (addr - 0x130) / 4;
        r = s->gpio_in_sel[n];
    }
    else if( addr >= 0x530 && addr < 0x5d0 ) { //GPIO_FUNCX_OUT_SEL_CFG_REG
        int n = (addr - 0x530) / 4;
        r = s->gpio_out_sel[n];
    }
    return r;
}


static int get_triggering( int int_type, int oldval, int val ) //Interrupt type selection
{
    switch( int_type )
    {
    case 1: return (val > oldval);  //rising edge trigger
    case 2: return (val < oldval);  //falling edge trigger
    case 3: return (val != oldval); //any edge trigger
    case 4: return (val == 0);      //low level trigger
    case 5: return (val == 1);      //high level trigger

    }
    return 0; //GPIO interrupt disable
}

static void set_gpio( void *opaque, int n, int val )
{
    Esp32GpioState *s = ESP32_GPIO(opaque);

    if( n < 32)
    {
        int oldval = (s->gpio_in >> n) & 1;
        int int_type = (s->gpio_pin[n] >> 7) & 7;
        s->gpio_in &= ~(1 << n);
        s->gpio_in |= (val << n);
        int irq = get_triggering( int_type, oldval, val );

        // says bit 16 in the ref manual, is that wrong?
        if( irq && (s->gpio_pin[n] & (1 << 15)) )   // pro cpu int enable
        {
            qemu_set_irq(s->irq, 1);
            s->gpio_pcpu_int |= (1 << n);
        }
        if( irq && (s->gpio_pin[n] & (1 << 13)) )   // app cpu int enable
        {
            qemu_set_irq(s->irq, 1);
            s->gpio_acpu_int |= (1 << n);
        }
    } else {
        int n1 = n - 32;
        int oldval = (s->gpio_in1 >> n1) & 1;
        int int_type = (s->gpio_pin[n] >> 7) & 7;
        s->gpio_in1 &= ~(1 << n1);
        s->gpio_in1 |= (val << n1);
        int irq = get_triggering(int_type, oldval, val);

        // says bit 16 in the ref manual, is that wrong?
        if( irq && (s->gpio_pin[n] & (1 << 15)) )   // pro cpu int enable
        {
            qemu_set_irq(s->irq, 1);
            s->gpio_pcpu_int1 |= (1 << n1);
        }
        if( irq && (s->gpio_pin[n] & (1 << 13)) )   // app cpu int enable
        {
            qemu_set_irq(s->irq, 1);
            s->gpio_acpu_int1 |= (1 << n1);
        }
    }
}


static void esp32_gpio_write( void *opaque, hwaddr addr, uint64_t value, unsigned int size )
{
    Esp32GpioState *s = ESP32_GPIO( opaque );

    uint32_t oldValue  = s->gpio_out;
    uint32_t oldEnable = s->gpio_enable;

    switch( addr )
    {
    case 0x04: s->gpio_out     =  value; break; // GPIO_OUT_REG
    case 0x08: s->gpio_out    |=  value; break; // GPIO_OUT_W1TS_REG
    case 0x0C: s->gpio_out    &= ~value; break; // GPIO_OUT_W1TC_REG
    case 0x20: s->gpio_enable  =  value; break; // GPIO_ENABLE_REG
    case 0x24: s->gpio_enable |=  value; break; // GPIO_ENABLE_W1TS_REG
    case 0x28: s->gpio_enable &= ~value; break; // GPIO_ENABLE_W1TC_REG
    case 0x38: s->strap_mode   =  value; break; // A_GPIO_STRAP
    case 0x44: s->gpio_status  =  value; break; // GPIO_STATUS_REG
    case 0x48: s->gpio_status |=  value; break; // GPIO_STATUS_W1TS_REG
    case 0x4c: {                                // GPIO_STATUS_W1TC_REG
            int clearirq = 1;
            for( int i=0; i<32; i++ )           // GPIO00-32 interrupt
            {
                uint32_t mask = 1<< i;
                if( value & mask )
                {
                    int int_type = (s->gpio_pin[i] >> 7) & 7;
                    if( (int_type == 4 && !(s->gpio_in & mask))
                     || (int_type == 5 &&  (s->gpio_in & mask)) )
                    {
                        clearirq = 0;
                        break;        // break for loop
                    }
                }
            }
            if( clearirq ) {
                s->gpio_status   &= ~value;
                s->gpio_pcpu_int &= ~value;
                s->gpio_acpu_int &= ~value;
                qemu_set_irq( s->irq, 0 );
            }
        }   break;
    case 0x50: s->gpio_status1  = value; break; // GPIO_STATUS1_REG
    case 0x54: s->gpio_status1 |= value; break; // GPIO_STATUS1_W1TS_REG
    case 0x58: {                                // GPIO_STATUS1_W1TC_REG
            int clearirq = 1;
            for( int i=32; i<40; i++ )          // GPIO32-39 interrupt
            {
                uint32_t mask = 1 << (i-32);
                if( value & mask )
                {
                    int int_type = (s->gpio_pin[i] >> 7) & 7;
                    if( (int_type == 4 && !(s->gpio_in1 & mask))
                     || (int_type == 5 &&  (s->gpio_in1 & mask)) )
                    {
                        clearirq = 0;
                        break;        // break for loop
                    }
                }
            }
            if( clearirq ) {
                s->gpio_status1   &= ~value;
                s->gpio_pcpu_int1 &= ~value;
                s->gpio_acpu_int1 &= ~value;
                qemu_set_irq( s->irq, 0 );
            }
        }   break;
    }
    if( addr >= 0x88 && addr < 0x130) {                 //GPIO_PINXX_REG
        int n = (addr - 0x88) / 4;
        s->gpio_pin[n] = value;
    }
    else if( addr >= 0x130 && addr < 0x530) {           //GPIO_FUNCY_IN_SEL_CFG_REG
        int n = (addr - 0x130) / 4;
        s->gpio_in_sel[n] = value;
        qemu_set_irq( s->gpios_sync[0], (0x1000 | n) ); //report in sel cfg change
    }
    else if( addr >= 0x530 && addr < 0x5d0) {           //GPIO_FUNCX_OUT_SEL_CFG_REG
        int n = (addr - 0x530) / 4;
        s->gpio_out_sel[n] = value;
        qemu_set_irq( s->gpios_sync[0], (0x2000 | n) ); ////report out sel cfg change
    }

    //printf("out 0x%04X in 0x%04X enable 0x%04X \n",s->gpio_out, s->gpio_in, s->gpio_enable);

    uint32_t enableDiff = s->gpio_enable ^ oldEnable;
    uint32_t valueDiff  = s->gpio_out    ^ oldValue;
    uint32_t diff = valueDiff | enableDiff;

    if( diff )
    {
        for( int i=0; i<32; i++ )
        {
            uint32_t mask = 1 << i;
            uint32_t enableMask = s->gpio_enable & mask;

            if( (diff & mask) && enableMask )
            {
                qemu_set_irq( s->gpios[i], (s->gpio_out & mask) ? 1 : 0 );

                s->gpio_in &= ~mask;
                s->gpio_in |=  s->gpio_out & mask;
            }
            if( enableDiff & mask )
                qemu_set_irq( s->gpios_dir[i], enableMask ? 1 : 0 );
        }
    }
}

static const MemoryRegionOps uart_ops = {
    .read =  esp32_gpio_read,
    .write = esp32_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_gpio_reset(DeviceState *dev)
{
}

static void esp32_gpio_realize(DeviceState *dev, Error **errp)
{
}

static void esp32_gpio_init( Object *obj )
{
    Esp32GpioState *s = ESP32_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Set the default value for the strap_mode property */
    object_property_set_int(obj, "strap_mode", ESP32_STRAP_MODE_FLASH_BOOT, &error_fatal);

    memory_region_init_io(&s->iomem, obj, &uart_ops, s, TYPE_ESP32_GPIO, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qdev_init_gpio_out_named( DEVICE(s), &s->irq      , SYSBUS_DEVICE_GPIO_IRQ, 1 );
    qdev_init_gpio_out_named( DEVICE(s), s->gpios     , ESP32_GPIOS, 32 );
    qdev_init_gpio_out_named( DEVICE(s), s->gpios_dir , ESP32_GPIOS_DIR, 32 );
    qdev_init_gpio_out_named( DEVICE(s), s->gpios_sync, ESP32_GPIOS_SYNC, 1 );

    qdev_init_gpio_in_named( DEVICE(s), set_gpio, ESP32_GPIOS_IN, 40 );

    s->gpio_in = 0x1;
    s->gpio_in1 = 0x8;
}

static Property esp32_gpio_properties[] = {
    /* The strap_mode needs to be explicitly set in the instance init, thus, set the default value to 0. */
    DEFINE_PROP_UINT32("strap_mode", Esp32GpioState, strap_mode, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->legacy_reset = esp32_gpio_reset;
    dc->realize = esp32_gpio_realize;
    device_class_set_props(dc, esp32_gpio_properties);
}

static const TypeInfo esp32_gpio_info = {
    .name = TYPE_ESP32_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32GpioState),
    .instance_init = esp32_gpio_init,
    .class_init = esp32_gpio_class_init,
    .class_size = sizeof(Esp32GpioClass),
};

static void esp32_gpio_register_types(void)
{
    type_register_static(&esp32_gpio_info);
}

type_init(esp32_gpio_register_types)
