/*
 *  Copyright (c) 2004-2005 David Oliveira
 *
 *  USB Logitech MediaPlay Cordless Mouse driver
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * If you need to contact me, you can do it by e-mail, sending a mail
 * message to <d.oliveira@prozone.org>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>


#define DRIVER_VERSION	"v0.5.8"
#define DRIVER_AUTHOR	"David Oliveira <d.oliveira@prozone.org>"
#define DRIVER_DESC	"USB Logitech MediaPlay Cordless Mouse driver"
#define DRIVER_LICENSE	"GPL"

#define GETBIT(v,n)     ((v>>(n))&0x01)
#define SETBIT(v,n)     (v |= (0x01<<(n)))

#ifdef SLAB_ATOMIC
# define ATOMIC SLAB_ATOMIC
#else
# define ATOMIC GFP_ATOMIC
#endif


/* Module properties */

MODULE_AUTHOR ( DRIVER_AUTHOR );
MODULE_DESCRIPTION ( DRIVER_DESC );
MODULE_LICENSE ( DRIVER_LICENSE );


/* Own type */

typedef struct usb_lmpcm {

	// Device name

	char name[128];

	// USB interrupt data

	signed char *data;

	char phys[64];

	dma_addr_t data_dma;

	// USB device

	struct usb_device *usbdev;

	// Input device

	struct input_dev *inputdev;

	// USB Request block

	struct urb *urb;

	// Number of openned times

	int open;

} lmpcm_t;


// Initialize lmpcm structure

void lmpcm_init ( lmpcm_t *lmpcm ) {

	memset(lmpcm, 0, sizeof(lmpcm_t));
	lmpcm->inputdev = NULL;
	lmpcm->urb = NULL;
	lmpcm->data = NULL;

}


// Free lmpcm buffers

void lmpcm_free ( lmpcm_t *lmpcm ) {

	if ( lmpcm->urb )
		usb_free_urb(lmpcm->urb);

	if ( lmpcm->data )
		usb_buffer_free(lmpcm->usbdev,8,lmpcm->data,lmpcm->data_dma);

	kfree(lmpcm);	

}


// Create new lmpcm (buffer allocation

lmpcm_t *lmpcm_new ( struct usb_device *dev ) {

	lmpcm_t *lmpcm;

	// Create object

	if (!(lmpcm = kmalloc(sizeof(lmpcm_t), GFP_KERNEL)))
		return NULL;

	// Initialize

	lmpcm_init(lmpcm);


	// Input device

	if ( (lmpcm->inputdev = input_allocate_device()) == NULL ) {
		lmpcm_free(lmpcm);
		return NULL;
	}


	// Create urb handler

	if (!(lmpcm->urb = usb_alloc_urb(0, GFP_KERNEL))) {
		lmpcm_free(lmpcm);
		return NULL;
	}


	// Create data required for urb transfer

	if (!(lmpcm->data = usb_buffer_alloc(dev,8,ATOMIC,&lmpcm->data_dma))) {
		lmpcm_free(lmpcm);
		return NULL;
	}


	// Set lmpcm usb device

	lmpcm->usbdev = dev;


	return lmpcm;

}





// Get data from urb and send to input API

void input_send_data ( struct input_dev *dev, char *data ) {

	char
		btn = data[0],	// Basic buttons (left, right, middle, side and extra)
		mbtn = data[6],	// Media buttons
		x = data[1],	// X movement
		y = data[2],	// Y movement
		w = data[3];	// Wheel movement


	input_report_key(dev, BTN_LEFT,   	GETBIT(btn,0));
	input_report_key(dev, BTN_RIGHT,  	GETBIT(btn,1));
	input_report_key(dev, BTN_MIDDLE, 	GETBIT(btn,2));
	input_report_key(dev, BTN_SIDE,  	GETBIT(btn,3));
	input_report_key(dev, BTN_EXTRA,  	GETBIT(btn,4));
	input_report_key(dev, KEY_PLAYCD,  	GETBIT(btn,5));
	input_report_key(dev, KEY_BACK,		GETBIT(btn,6));
	input_report_key(dev, KEY_FORWARD,	GETBIT(btn,7));

	input_report_key(dev, KEY_VOLUMEUP,	GETBIT(mbtn,0));
	input_report_key(dev, KEY_VOLUMEDOWN,	GETBIT(mbtn,1));
	input_report_key(dev, KEY_NEXTSONG,	GETBIT(mbtn,2));
	input_report_key(dev, KEY_PREVIOUSSONG,	GETBIT(mbtn,3));
	input_report_key(dev, KEY_PLAYPAUSE,	GETBIT(mbtn,4));

	input_report_rel(dev, REL_X,     x);
	input_report_rel(dev, REL_Y,     y);
	input_report_rel(dev, REL_WHEEL, w);

}

static void usb_lmpcm_handle(struct urb *urb) {

	lmpcm_t *mouse = urb->context;
	signed char *data = mouse->data;
	struct input_dev *inputdev = mouse->inputdev;


	// Check returned status

	if (urb->status) return ;


	// Send data to input interface

	input_send_data(inputdev,data);

	input_sync(inputdev);
	usb_submit_urb(urb,ATOMIC);

}

static int usb_lmpcm_open(struct input_dev *dev) {

	lmpcm_t *mouse = input_get_drvdata(dev);

	if (mouse->open++)
		return 0;

	mouse->urb->dev = mouse->usbdev;

	if (usb_submit_urb(mouse->urb, GFP_KERNEL)) {
		mouse->open--;
		return -EIO;
	}

	return 0;

}

static void usb_lmpcm_close(struct input_dev *dev) {

	lmpcm_t *mouse = input_get_drvdata(dev);

	if (!--mouse->open)
		usb_kill_urb(mouse->urb);

}

static void input_device_init ( struct input_dev *inputdev, struct usb_interface *intf, struct usb_device *dev ) {

	char path[64];

	lmpcm_t *mouse = (lmpcm_t *) input_get_drvdata(inputdev);

	int
		x,
		keys[]	= { KEY_PLAYPAUSE, KEY_BACK, KEY_FORWARD, KEY_PLAYCD, KEY_VOLUMEUP, KEY_VOLUMEDOWN,
			    KEY_NEXTSONG, KEY_PREVIOUSSONG, 0 };


	// Events

	inputdev->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);

	// Add special keys

	for ( x = 0 ; keys[x] ; x++ )
		set_bit(keys[x],inputdev->keybit);

	// Add basic buttons

	inputdev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE) |
					    BIT_MASK(BTN_SIDE) | BIT_MASK(BTN_EXTRA);

	// Add move mouse movement (X/Y)

	inputdev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	// Add wheel

	inputdev->relbit[0] |= BIT_MASK(REL_WHEEL);


	// Private data structure

	input_set_drvdata(inputdev, mouse);

	// Input file operations

	inputdev->open = usb_lmpcm_open;
	inputdev->close = usb_lmpcm_close;

	// Device

	inputdev->name = mouse->name;

	usb_make_path(dev,path,64);
	snprintf(mouse->phys,64,"%s/input0",path);

	inputdev->phys = mouse->phys;
	inputdev->id.bustype = BUS_USB;
	inputdev->id.vendor = dev->descriptor.idVendor;
	inputdev->id.product = dev->descriptor.idProduct;
	inputdev->id.version = dev->descriptor.bcdDevice;

}

static int usb_lmpcm_probe(struct usb_interface *intf, const struct usb_device_id *id) {

	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;


	struct usb_endpoint_descriptor *endpoint;
	lmpcm_t *mouse;
	int pipe, maxp;
	char *buf;


	// Get mouse endpoint

	interface = intf->cur_altsetting;

	if ( interface->desc.bNumEndpoints != 1 ) return -ENODEV;
	endpoint = &interface->endpoint[0].desc;


	// Check endpoint

	if (!(endpoint->bEndpointAddress & USB_DIR_IN))
		return -ENODEV;

	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT)
		return -ENODEV;


	// Create endpoint pipe

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));


	// Create lmpcm object

	if (!(mouse = lmpcm_new(dev)))
		return -ENOMEM;

	// Initialize input device

	input_device_init(mouse->inputdev,intf,dev);


	// Set device name

	if (!(buf = kmalloc(63, GFP_KERNEL))) {
		lmpcm_free(mouse);
		return -ENOMEM;
	}


	if (dev->descriptor.iManufacturer &&
		usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0)
			strcat(mouse->name, buf);

	if (dev->descriptor.iProduct &&
		usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0)
			sprintf(mouse->name, "%s %s", mouse->name, buf);

	if (!strlen(mouse->name))
		sprintf(mouse->name, "lmpcm_usb.c: Logitech MediaPlay Mouse on usb%04x:%04x",
			mouse->inputdev->id.vendor, mouse->inputdev->id.product);

	kfree(buf);


	// Initialize interrupt transfer

	usb_fill_int_urb(mouse->urb,dev,pipe,mouse->data,((maxp > 8)?8:maxp),usb_lmpcm_handle,mouse,endpoint->bInterval);
	mouse->urb->transfer_dma = mouse->data_dma;
	mouse->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;


	// Register input device

	input_register_device(mouse->inputdev);


	printk(KERN_INFO "lmpcm_usb.c: Detected device: %s\n", mouse->name);

	// Set usb handler interface data

	usb_set_intfdata(intf,mouse);


	return 0;

}


static void usb_lmpcm_disconnect(struct usb_interface *intf) {

	lmpcm_t *mouse = usb_get_intfdata(intf);

	usb_set_intfdata(intf,NULL);
	if (mouse) {
		usb_kill_urb(mouse->urb);
		input_unregister_device(mouse->inputdev);
		lmpcm_free(mouse);
	}

}




/* Module structures */

static struct usb_device_id usb_lmpcm_id_table [] = {
	{ USB_DEVICE(0x46d, 0xc50e) },
	{ }
};

MODULE_DEVICE_TABLE (usb, usb_lmpcm_id_table);

static struct usb_driver usb_lmpcm_driver = {

	.name		= "lmpcm_usb",
	.probe		= usb_lmpcm_probe,
	.disconnect	= usb_lmpcm_disconnect,
	.id_table	= usb_lmpcm_id_table

};



/* Module main functions */

static int __init usb_lmpcm_init(void) {

	int rv;

	// Register usb driver

	rv = usb_register(&usb_lmpcm_driver);
	if ( rv == 0 )
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":" DRIVER_DESC "\n");

	return rv;
}

static void __exit usb_lmpcm_exit(void) {

	usb_deregister(&usb_lmpcm_driver);

}

// Set

module_init(usb_lmpcm_init);
module_exit(usb_lmpcm_exit);
