/*
 * ESP32S3 SHA accelerator
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/misc/esp32s3_sha.h"

static void esp32s3_sha_class_init(ObjectClass *klass, void *data)
{
    ESPShaClass* class = ESP_SHA_CLASS(klass);
    class->message_len = ESP32S3_SHA_MESSAGE_SIZE;
    class->date = ESP32S3_SHA_DATE_REG_VALUE;
}

static const TypeInfo esp32s3_sha_info = {
    .name = TYPE_ESP32S3_SHA,
    .parent = TYPE_ESP_SHA,
    .instance_size = sizeof(ESP32S3ShaState),
    .class_init = esp32s3_sha_class_init,
    .class_size = sizeof(ESP32S3ShaClass)
};

static void esp32s3_sha_register_types(void)
{
    type_register_static(&esp32s3_sha_info);
}

type_init(esp32s3_sha_register_types)
