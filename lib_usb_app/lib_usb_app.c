#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "libusb-1.0/libusb.h"

#define DEBUG(x)    printf("%s\n", x)
#define TAG(x)      printf("%d\n",x)

#define NEW_API         0   //will probably stay like this, API 1.0.22. is not available on Ubuntu 16.04
#define DEBUG_PRINTOUTS 0
#define TIME_MEASURE    1

#if TIME_MEASURE
#include <time.h>
#endif

#define VENDOR_ID   0x04d8
#define PRODUCT_ID  0x0204



//prints VendorID - ProductID for all devices on bus
static void print_devs(libusb_device **);
//prints detailed info for one device
static void printdev(libusb_device *);
//prints info on LIBUSB
static void print_libusb_version();

static libusb_device* get_xilinx_device(libusb_device **);

static int send_usb_bulk_transfer(libusb_device_handle *, struct libusb_transfer *);
static int recv_usb_bulk_transfer(libusb_device_handle *, struct libusb_transfer *);

#if TIME_MEASURE
void print_time_spent(struct timespec, struct timespec, int);
#endif


#define PACKET_SIZE         512 //in Bytes
#define NUM_PACKETS_PER_KB  2
#define KB_TO_MB            1024
#define NUM_MB              5
#define LENGTH              NUM_PACKETS_PER_KB*KB_TO_MB*NUM_MB

//From PC point of view
#define EP1_OUT 0x01
#define EP1_IN  0x81

#define INTERFACE_NUMBER    0
/*
CALLBACK FUNCTIONS AND STRUCTS
 */
struct my_data {
    int     my_int;
    char    my_char;
};

struct my_data my_send_cb_data;
struct my_data my_recv_cb_data;

int send_finish = 0;
int recv_finish = 0;

static void send_transfer_finished_cb(struct libusb_transfer *);
static void recv_transfer_finished_cb(struct libusb_transfer *);
/*
^ CALLBACK FUNCTIONS AND STRUCTS ^
 */

struct libusb_transfer* send_transfer_desc  = NULL;
struct libusb_transfer* recv_transfer_desc  = NULL;

uint8_t* out_buffer                         = NULL;
uint8_t* in_buffer                          = NULL; 

int main(int argc, char* argv[] ) {

    libusb_context  *ctx = NULL;
    libusb_device   **devs, *dev;
    libusb_device_handle* usb_dev_handle = NULL;
    int r, ret_val = 0;

#if TIME_MEASURE
    struct timespec t1, t2;
#endif

#if DEBUG_PRINTOUTS
    DEBUG("Hello world");
    printf("Does this PC has capability for hotplug work (Non zero means yes)? %d\n\n", 
                libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG));
#endif

    //has to be called before any other LIBUSB call
    if(libusb_init(&ctx) != 0) {
        DEBUG("Error libusb_init");
        ret_val = -1;
        goto exit;
    }

#if DEBUG_PRINTOUTS
    print_libusb_version();
#endif
    
#if NEW_API
    if( (r = libusb_set_option(ctx, LIBUSB_LOG_LEVEL_WARNING)) != LIBUSB_SUCCESS ) {
        printf("Error in libusb_set_option: %d", r);
    }
#else
    libusb_set_debug(ctx, LIBUSB_LOG_LEVEL_WARNING);
#endif

    //get all USB devices on bus
    if( (r = libusb_get_device_list(NULL, &devs) ) < 0 ) {
        DEBUG("No USB devices found");
        ret_val = -2;
        goto exit;
    }

#if DEBUG_PRINTOUTS
    print_devs(devs);
#endif

    if((dev = get_xilinx_device(devs)) == NULL) {
        ret_val = -3;
        goto exit;
    } else {
        DEBUG("Xilinx device found!");

#if DEBUG_PRINTOUTS
    printdev(dev);
#endif
        
        if( (r = libusb_open(dev, &usb_dev_handle)) != 0 ) {
            printf("Error while opening USB device handle: %d", r);
            
            //Try another way
            if( (usb_dev_handle = libusb_open_device_with_vid_pid(  ctx,
                                                                    VENDOR_ID,
                                                                    PRODUCT_ID)) == NULL ) {
                DEBUG("libusb_open_device_with_vid_pid FAILED!");
            }
             
        } 
    }   

    if(usb_dev_handle == NULL) {
        DEBUG("Couldn't get handle on Xilinx USB device");
        ret_val = -4;
        goto exit;
    }

    if(libusb_set_auto_detach_kernel_driver(usb_dev_handle, 1) != LIBUSB_SUCCESS) {
        DEBUG("Set auto detach kernel failed! If not on Linux its safe to ignore");
    
        if(libusb_kernel_driver_active(usb_dev_handle, INTERFACE_NUMBER) == 1) {
            DEBUG("Kernel Driver Active");

            if(libusb_detach_kernel_driver(usb_dev_handle, INTERFACE_NUMBER) == 0) {
                DEBUG("Kernel Driver Detached!");
            }
        }

    }

    if((r = libusb_claim_interface(usb_dev_handle, INTERFACE_NUMBER)) != 0) {
        DEBUG("Cannot Claim Interface");
        ret_val = -5;
        goto exit;
    }

/*    if((r = libusb_set_interface_alt_setting(usb_dev_handle, INTERFACE_NUMBER, 1))  != 0) {
        DEBUG("Cannot configure alternate setting");
        ret_val = -6;
        goto exit;
    }*/

#define NUM_TRANSFERS 1

    for(int i = 0; i < NUM_TRANSFERS; i++) {
#if TIME_MEASURE
        clock_gettime(CLOCK_MONOTONIC, &t1);
#endif
        if( (r = send_usb_bulk_transfer(usb_dev_handle, send_transfer_desc) ) != 0) {
            DEBUG("Error in sending USB Bulk tranfer");
            ret_val = -7;
            goto exit;
        }
    
        printf("Working...\n\n");   
            
        while(!send_finish) {
            libusb_handle_events(ctx);
            //usleep(1000*500);   //0.5 sec
            //printf("Still working...\n");
        }
    
        send_finish = 0;
#if TIME_MEASURE
        clock_gettime(CLOCK_MONOTONIC, &t2);

        print_time_spent(t1, t2, LENGTH);
#endif
    }


    for(int i = 0; i < NUM_TRANSFERS; i++) {
#if TIME_MEASURE
        clock_gettime(CLOCK_MONOTONIC, &t1);
#endif
        if( (r = recv_usb_bulk_transfer(usb_dev_handle, recv_transfer_desc) ) != 0) {
            DEBUG("Error in sending USB Bulk tranfer");
            ret_val = -7;
            goto exit;  
        }

        while(!recv_finish) {
            libusb_handle_events(ctx);
            //usleep(1000*500);   //0.5 sec
            //printf("Still working...\n");
        }    

        recv_finish = 0;
#if TIME_MEASURE
        clock_gettime(CLOCK_MONOTONIC, &t2);

        print_time_spent(t1, t2, LENGTH);
#endif
    }
    printf("Done\n");

exit:
    free(in_buffer);
    free(out_buffer);

    libusb_release_interface(usb_dev_handle, INTERFACE_NUMBER);

    libusb_close(usb_dev_handle);
    DEBUG("USB DEVICE CLOSED");

    //clean up
    libusb_free_device_list(devs, 1);

    libusb_exit(NULL);

    return ret_val;
}

static void recv_transfer_finished_cb(struct libusb_transfer *transfer) {

    struct my_data* a = (struct my_data*)transfer->user_data;
    printf("\nRECV TRANSFER FINISHED; user callback data: %c %d\n", a->my_char, a->my_int);

    printf("Transfer status (0 is good, but there can still be errors): %d\n", transfer->status);
//    printf("data to send == actual data sent? %d\n\n", transfer->length == transfer->actual_length);
    printf("data to recv == actual data rcvd? %d\n", transfer->length == transfer->actual_length);

    recv_finish = 1;

#if DEBUG_PRINTOUTS
    for(int i = 0; i < 30; i++) {
        printf("in_buffer[%d] = %c\n", i, in_buffer[i]);
    }
#endif

    //Free transfer descriptor after finished transfer
    libusb_free_transfer(recv_transfer_desc);

    //TODO: Check each packet for transfer errors
}

static int recv_usb_bulk_transfer(libusb_device_handle* usb_dev_handle, struct libusb_transfer* recv_transfer_desc) {
    const int len = LENGTH;
    const unsigned int timeout = 0; //Wait 'till death
    int ret_val = 0;

    if(recv_transfer_desc != NULL) {
        libusb_free_transfer(recv_transfer_desc);
        recv_transfer_desc = NULL;
    }

    if(in_buffer != NULL) {
        free(in_buffer);
        in_buffer = NULL;
    }

    in_buffer = malloc(sizeof(uint8_t) * len);

    //random callback data for testing
    my_recv_cb_data.my_int   = 14;
    my_recv_cb_data.my_char  = 'T';


    //STEP 1: Allocate transfer
    //NOTE: Transfers intended for non-isochronous endpoints (e.g. control, bulk, interrupt) should specify an iso_packets count of zero.
    if((recv_transfer_desc = libusb_alloc_transfer( 0 )) == NULL) {
        DEBUG("Failed to allocate transfer descriptor");
        ret_val = -1;
        goto exit;
    }

    //STEP 2: Fill Bulk transfer
    libusb_fill_bulk_transfer(  recv_transfer_desc,         //transfer descriptor struct
                                usb_dev_handle,             //usb device handle
                                EP1_IN,                    //unsigned char
                                in_buffer,                 //unsigned char*
                                len,                        //tranfer length, int
                                recv_transfer_finished_cb,  //callback func
                                &my_recv_cb_data,           //void*, callback user data
                                timeout);                 //unsigned int
                                
    //STEP 3: Activate transfer        
    if((ret_val = libusb_submit_transfer(recv_transfer_desc)) != 0) {
        DEBUG("ERROR: libusb_submit_transfer");
        if(ret_val == -1) ret_val = -2;

        goto exit;
    }


exit:
    

    return ret_val;
}

static void send_transfer_finished_cb(struct libusb_transfer *transfer) {

    struct my_data* a = (struct my_data*)transfer->user_data;
    printf("\nSEND TRANSFER FINISHED; user callback data: %c %d\n", a->my_char, a->my_int);

    printf("Transfer status (0 is good, but there can still be errors): %d\n", transfer->status);
    printf("data to send == actual data sent? %d\n", transfer->length == transfer->actual_length);

    send_finish = 1;

    //Free transfer descriptor after finished transfer
    libusb_free_transfer(send_transfer_desc);

    //TODO: Check each packet for transfer errors
}

static int send_usb_bulk_transfer(libusb_device_handle* usb_dev_handle, struct libusb_transfer* send_transfer_desc) {

    const int len = LENGTH;
    const unsigned int timeout = 500;
    int ret_val = 0;

    if(send_transfer_desc != NULL) {
        libusb_free_transfer(send_transfer_desc);
        send_transfer_desc = NULL;
    }

    if(out_buffer != NULL) {
        free(out_buffer);
        out_buffer = NULL;
    }

    out_buffer = malloc(sizeof(uint8_t) * len);

    //Init buffer for sending
    for(int i = 0; i < len; i++) {
        out_buffer[i] = (i % 26) + 'A';
    }

#if DEBUG_PRINTOUTS
    for(int i = 0; i < 30; i++) {
        printf("out_buffer[%d] = %c\n", i, out_buffer[i]);
    }
#endif

    //random callback data for testing
    my_send_cb_data.my_int   = 14;
    my_send_cb_data.my_char  = 'T';


    //STEP 1: Allocate transfer
    //NOTE: Transfers intended for non-isochronous endpoints (e.g. control, bulk, interrupt) should specify an iso_packets count of zero.
    if((send_transfer_desc = libusb_alloc_transfer( 0 )) == NULL) {
        DEBUG("Failed to allocate transfer descriptor");
        ret_val = -1;
        goto exit;
    }

    //STEP 2: Fill Bulk transfer
    libusb_fill_bulk_transfer(  send_transfer_desc,         //transfer descriptor struct
                                usb_dev_handle,             //usb device handle
                                EP1_OUT,                    //unsigned char
                                out_buffer,                 //unsigned char*
                                len,                        //tranfer length, int
                                send_transfer_finished_cb,  //callback func
                                &my_send_cb_data,           //void*, callback user data
                                timeout*4);                 //unsigned int
                                
    //STEP 3: Activate transfer        
    if((ret_val = libusb_submit_transfer(send_transfer_desc)) != 0) {
        DEBUG("ERROR: libusb_submit_transfer");
        if(ret_val == -1) ret_val = -2;

        goto exit;
    }


exit:
    

    return ret_val;
}

static libusb_device* get_xilinx_device(libusb_device **devs) {
    libusb_device *dev = devs[0];
    struct libusb_device_descriptor desc;
    int r=0, i=0;

    while ((dev = devs[i++]) != NULL) {
        if( (r = libusb_get_device_descriptor(dev, &desc) ) < 0 ) {
            printf("Failed to get device descriptor\n\n");
            return NULL;
        }

        if((desc.idVendor == VENDOR_ID) && (desc.idProduct == PRODUCT_ID)) {
            return dev;
        }
    }

    printf("No Xilinx device found!\n\n");
    return NULL;
}

/*
    Prints all USB devices

 */
static void print_devs(libusb_device **devs) {
    libusb_device *dev;
    int i = 0, j = 0;
    uint8_t path[8]; 

    printf("LISTING ALL USB DEVICES ON BUS\n");

    while ((dev = devs[i++]) != NULL) {
        struct libusb_device_descriptor desc;
        int r = libusb_get_device_descriptor(dev, &desc);
        if (r < 0) {
            fprintf(stderr, "failed to get device descriptor");
            return;
        }

        printf("%04x:%04x (bus %d, device %d)",
            desc.idVendor, desc.idProduct,
            libusb_get_bus_number(dev), libusb_get_device_address(dev));

        r = libusb_get_port_numbers(dev, path, sizeof(path));
        if (r > 0) {
            printf(" path: %d", path[0]);
            for (j = 1; j < r; j++)
                printf(".%d", path[j]);
        }
        printf("\n");
    }

    printf("\n\n");

}


static void print_libusb_version() {
    
    const struct libusb_version *libusb_vrs = libusb_get_version();
    
    printf("\n\nLIBUSB_VERSION\n");
    
    printf("major: %d\n", libusb_vrs->major);
    printf("minor: %d\n", libusb_vrs->minor);
    printf("micro: %d\n", libusb_vrs->micro);
    printf("nano: %d\n", libusb_vrs->nano);
    printf("rc: %s\n", libusb_vrs->rc);
    printf("desc: %s\n\n", libusb_vrs->describe);

    printf("LIBUSB_API_VERSION: 0x%08x\n", LIBUSB_API_VERSION);
    printf("\n");
}

void printdev(libusb_device *dev) {
    struct libusb_device_descriptor desc;
    struct libusb_config_descriptor *config;

    int r = libusb_get_device_descriptor(dev, &desc);
    
    if (r < 0) {
        printf("failed to get device descriptor\n\n");
        return;
    }

    printf("\n\nPrinting device %04x / %04x\n", desc.idVendor, desc.idProduct);
    
    printf("Number of possible configurations: %d\n", (int)desc.bNumConfigurations);
    printf("Device Class: %d\n", (int)desc.bDeviceClass);
    printf("VendorID: 0x%04x\n", desc.idVendor);
    printf("ProductID: 0x%04x\n", desc.idProduct);
    
    libusb_get_config_descriptor(dev, 0, &config);
    
    printf("Interfaces: %d\n\n", (int)config->bNumInterfaces);
    
    const struct libusb_interface *inter;
    const struct libusb_interface_descriptor *interdesc;
    const struct libusb_endpoint_descriptor *epdesc;
    
    for(int i=0; i<(int)config->bNumInterfaces; i++) {
        inter = &config->interface[i];
        printf("Number of alternate settings: %d\n", inter->num_altsetting);
    
        for(int j=0; j<inter->num_altsetting; j++) {
            interdesc = &inter->altsetting[j];
            printf("Interface Number: %d\n"   , (int)interdesc->bInterfaceNumber);
            printf("Number of endpoints: %d\n", (int)interdesc->bNumEndpoints);
    
            for(int k=0; k < (int)interdesc->bNumEndpoints; k++) {
                epdesc = &interdesc->endpoint[k];
                printf("Descriptor Type (0x05 == Endpoint): 0x%02x\n", (int)epdesc->bDescriptorType);
                printf("EP Address: 0x%02x\n", (int)epdesc->bEndpointAddress);
                printf("libusb_get_max_packet_size 0x%02x: %d\n\n", (int)epdesc->bEndpointAddress, libusb_get_max_packet_size(dev, (int)epdesc->bEndpointAddress));
            }
        }
    }
    

    printf("\n\n\n");
    
    libusb_free_config_descriptor(config);
}

#if TIME_MEASURE
void print_time_spent(struct timespec t1, struct timespec t2, int len) {
    struct  timespec diff;

    if(t2.tv_nsec < t1.tv_nsec)
    {
        /* If nanoseconds in t1 are larger than nanoseconds in t2, it
           means that something like the following happened:
           t1.tv_sec = 1000    t1.tv_nsec = 100000
           t2.tv_sec = 1001    t2.tv_nsec = 10
           In this case, less than a second has passed but subtracting
           the tv_sec parts will indicate that 1 second has passed. To
           fix this problem, we subtract 1 second from the elapsed
           tv_sec and add one second to the elapsed tv_nsec. See
           below:
        */
        diff.tv_sec  = t2.tv_sec  - t1.tv_sec  - 1;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec + 1000000000;
    }
    else
    {
        diff.tv_sec  = t2.tv_sec  - t1.tv_sec;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec;
    }

    printf("Data transfered: %.02lf KB (%.0lf MB)\n", len/1024., (len/1024.)/1024.);
    printf("Time elapsed (float): %14lf miliseconds\n", diff.tv_nsec/(1000.*1000.));
    #if DEBUG_PRINTOUTS
    printf("Time elapsed (integer): %12ld nanoseconds\n", diff.tv_nsec);
    printf("Time elapsed (float): %14lf microseconds\n", diff.tv_nsec/1000.);
    #endif
}

#endif