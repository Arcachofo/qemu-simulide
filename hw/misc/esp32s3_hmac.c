/*
 * ESP32S3 HMAC emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/misc/esp32s3_hmac.h"

static void esp32s3_hmac_class_init(ObjectClass *klass, void *data)
{
    ESPHmacClass *class = ESP_HMAC_CLASS(klass);
    class->date = ESP32S3_HMAC_DATE_REG_VALUE;
}

static const TypeInfo esp32s3_hmac_info = {
    .name = TYPE_ESP32S3_HMAC,
    .parent = TYPE_ESP_HMAC,
    .instance_size = sizeof(ESP32S3HmacState),
    .class_init = esp32s3_hmac_class_init,
    .class_size = sizeof(ESP32S3HmacClass)
};

static void esp32s3_hmac_register_types(void)
{
    type_register_static(&esp32s3_hmac_info);
}

type_init(esp32s3_hmac_register_types)
