typedef struct global global_t;
#define usb_callback_data_t global_t
#define fat_callback_usr_t msd_t

#include <usbdrvce.h>
#include <msddrvce.h>
#include <fatdrvce.h>
#include <tice.h>

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../upng/upng.h"

#define MAX_PARTITIONS 32

#define FILE_NAME "/TEST.PNG"

struct global
{
    usb_device_t usb;
    msd_t msd;
};

enum { USB_RETRY_INIT = USB_USER_ERROR };

static void putstr(char *str)
{
    os_PutStrFull(str);
    os_NewLine();
}

static usb_error_t handleUsbEvent(usb_event_t event, void *event_data,
                                  usb_callback_data_t *global)
{
    switch (event)
    {
        case USB_DEVICE_DISCONNECTED_EVENT:
            putstr("usb device disconnected");
            if (global->usb)
                msd_Close(&global->msd);
            global->usb = NULL;
            break;
        case USB_DEVICE_CONNECTED_EVENT:
            putstr("usb device connected");
            return usb_ResetDevice(event_data);
        case USB_DEVICE_ENABLED_EVENT:
            global->usb = event_data;
            putstr("usb device enabled");
            break;
        case USB_DEVICE_DISABLED_EVENT:
            putstr("usb device disabled");
            return USB_RETRY_INIT;
        default:
            break;
    }

    return USB_SUCCESS;
}

// Draw to the VRAM, OS default: RGB565
void drawPixel(uint32_t u32Position, uint16_t u16Color)
{
    uint16_t *pixel = lcd_Ram + (u32Position*2);
    *pixel = u16Color;
}

/* Main function, called first */
int main(void)
{
    static uint8_t *filebuffer;
    static char buffer[212];
    static msd_partition_t partitions[MAX_PARTITIONS];
    static global_t global;
    static fat_t fat;
    uint8_t num_partitions;
    usb_error_t usberr;
    msd_error_t msderr;
    fat_error_t faterr;
    fat_file_t file;
    uint32_t size;
    uint24_t block;
    upng_t* upng;
    int width, height;
    int depth;
    int length;

    /* Clear the homescreen */
    os_ClrHome();

    /* Print a string */
    os_PutStrFull("Hello, World.");

    for(int i=0; i<320*240; i++)
        drawPixel(i, 0xF800); // Red
    for(int i=0; i<100; i++)
        boot_WaitShort();

    for(int i=0; i<320*240; i++)
        drawPixel(i, 0x07E0); // Green
    for(int i=0; i<100; i++)
        boot_WaitShort();

    for(int i=0; i<320*240; i++)
        drawPixel(i, 0x003F); // Blue
    for(int i=0; i<100; i++)
        boot_WaitShort();

    os_ClrHome();
    os_SetCursorPos(0, 0);

    // usb initialization loop; waits for something to be plugged in
    do
    {
        global.usb = NULL;

        usberr = usb_Init(handleUsbEvent, &global, NULL, USB_DEFAULT_INIT_FLAGS);
        if (usberr != USB_SUCCESS)
        {
            putstr("usb init error.");
            goto usb_error;
        }

        while (usberr == USB_SUCCESS)
        {
            if (global.usb != NULL)
                break;

            // break out if a key is pressed
            if (os_GetCSC())
            {
                putstr("exiting demo, press a key");
                goto usb_error;
            }

            usberr = usb_WaitForInterrupt();
        }
    } while (usberr == USB_RETRY_INIT);

    if (usberr != USB_SUCCESS)
    {
        putstr("usb enable error.");
        goto usb_error;
    }

    // initialize the msd device
    msderr = msd_Open(&global.msd, global.usb);
    if (msderr != MSD_SUCCESS)
    {
        putstr("failed opening msd");
        goto usb_error;
    }

    putstr("opened msd");

    // locate the first fat partition available
    num_partitions = msd_FindPartitions(&global.msd, partitions, MAX_PARTITIONS);
    if (num_partitions < 1)
    {
        putstr("no paritions found");
        goto msd_error;
    }

    // attempt to open the first found fat partition
    // it is not required to use a MSD to access a FAT filesystem if the
    // appropriate callbacks are configured.
    for (uint8_t p = 0;;)
    {
        uint32_t base_lba = partitions[p].first_lba;
        fat_callback_usr_t *usr = &global.msd;
        fat_read_callback_t read = &msd_Read;
        fat_write_callback_t write = &msd_Write;

        faterr = fat_Open(&fat, read, write, usr, base_lba);
        if (faterr == FAT_SUCCESS)
        {
            sprintf(buffer, "opened fat partition %u", p - 1);
            putstr(buffer);
            break;
        }

        p++;
        if (p >= num_partitions)
        {
            putstr("no suitable patitions");
            goto msd_error;
        }
    }

    sprintf(buffer, "read file: %s", FILE_NAME);
    putstr(buffer);
    // open file for reading
    faterr = fat_OpenFile(&fat, FILE_NAME, 0, &file);
    if (faterr != FAT_SUCCESS)
    {
        putstr("could not open file!");
        goto fat_error;
    }

    sprintf(buffer, "file size: %u", (int)fat_GetFileSize(&file));
    putstr(buffer);
    size = fat_GetFileSize(&file);
    block = size / 512 + 1;
    filebuffer = malloc(size);
    fat_ReadFile(&file, block, filebuffer);

    upng = upng_new_from_bytes(filebuffer, fat_GetFileSize(&file));
    if (upng_get_error(upng) != UPNG_EOK) {
        sprintf(buffer, "error: %u %u", upng_get_error(upng), upng_get_error_line(upng));
        putstr(buffer);
        while (!os_GetCSC());
        goto fat_error;
    }

    putstr("Press any key to decode.");
    while (!os_GetCSC());

    upng_decode(upng);
    if (upng_get_error(upng) != UPNG_EOK) {
        sprintf(buffer, "error: %u %u", upng_get_error(upng), upng_get_error_line(upng));
        putstr(buffer);
        while (!os_GetCSC());
        goto fat_error;
    }
    width = upng_get_width(upng);
    height = upng_get_height(upng);
    depth = upng_get_bpp(upng) / 8;

    sprintf(buffer, "size: %ux%ux%u (%u)", width, height, upng_get_bpp(upng), upng_get_size(upng));
    putstr(buffer);
    sprintf(buffer, "depth: %u", depth);
    putstr(buffer);
    sprintf(buffer, "format: %u", upng_get_format(upng));
    putstr(buffer);

    putstr("Press any key to show.");
    while (!os_GetCSC());

    length = width * height;
    for(int i=0; i<length; i++)
    {
        uint8_t r, g, b;
        uint16_t color565;
        int position;
        /* RGB8 to RGB565 */
        r = upng_get_buffer(upng)[i*3];
        g = upng_get_buffer(upng)[i*3+1];
        b = upng_get_buffer(upng)[i*3+2];
        color565 = r << 8 & 0b1111100000000000;
        color565 |= g << 3 & 0b0000011111100000;
        color565 |= b >> 3 & 0b0000000000011111;
        position = (i / width * 320) + (i % width);
        drawPixel(position, color565);
    }

    upng_free(upng);

fat_error:
    // close the filesystem
    fat_Close(&fat);

msd_error:
    // close the msd device
    msd_Close(&global.msd);

usb_error:
    // cleanup usb
    usb_Cleanup();

    /* Waits for a key */
    while (!os_GetCSC());

    /* Return 0 for success */
    return 0;
}
