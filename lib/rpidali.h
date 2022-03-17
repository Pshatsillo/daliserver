#ifndef _RPIDALI_H
#define _RPIDALI_H

#include "dispatch.h"
#include "frame.h"

#define DALI_drv "/dev/dali"

struct RpiDali;
typedef struct RpiDali *RpiDaliPtr;

struct RpiDaliSendTransfer;
typedef struct RpiDaliSendTransfer *RpiDaliSendTransferPtr;

struct RpiDaliReceiveTransfer;
typedef struct RpiDaliReceiveTransfer *RpiDaliReceiveTransferPtr;

typedef enum {
	RPIDALI_SUCCESS = 0,
	RPIDALI_RESPONSE = 1,
	RPIDALI_SEND_TIMEOUT = -1,
	RPIDALI_RECEIVE_TIMEOUT = -2,
	RPIDALI_SEND_ERROR = -3,
	RPIDALI_RECEIVE_ERROR = -4,
	RPIDALI_QUEUE_FULL = -5,
	RPIDALI_INVALID_ARG = -6,
	RPIDALI_NO_MEMORY = -7,
	RPIDALI_SYSTEM_ERROR = -8,
} RpiDaliError;

typedef void (*RpiDaliOutBandCallback)(RpiDaliError err, DaliFramePtr frame, unsigned int status, void *arg);
typedef void (*RpiDaliInBandCallback)(RpiDaliError err, DaliFramePtr frame, unsigned int response, unsigned int status, void *arg);
typedef void (*RpiDaliEventCallback)(int closed, void *arg);
RpiDaliError rpidali_queue(RpiDaliPtr dali, DaliFramePtr frame, void *cbarg);

RpiDaliPtr rpidali_open(DispatchPtr dispatch);


// Return a human-readable error description of the UsbDali error
const char *rpidali_error_string(RpiDaliError error);

// Stop running transfers and close the device, then finalize the libusb context
// if it was created by usbdali_open.
void rpidali_close(RpiDaliPtr dali);
// Enqueue a Dali command
// cbarg is the arg argument that will be passed to the inband callback
RpiDaliError rpidali_queue(RpiDaliPtr dali, DaliFramePtr frame, void *cbarg);
// Set the handler timeout (in msec, default 100)
// 0 is supposed to mean 'forever', but this isn't implemented yet.
void rpidali_set_handler_timeout(RpiDaliPtr dali, unsigned int timeout);
// Set the maximum queue size (default and maximum 255)
void rpidali_set_queue_size(RpiDaliPtr dali, unsigned int size);
// Sets the out of band message callback
void rpidali_set_outband_callback(RpiDaliPtr dali, RpiDaliOutBandCallback callback, void *arg);
// Sets the in band message callback
void rpidali_set_inband_callback(RpiDaliPtr dali, RpiDaliInBandCallback callback);
// Returns the next timeout to use for polling in msecs, -1 if no timeout is active
int rpidali_get_timeout(RpiDaliPtr dali);
// Sets the callback arguments of all active and queued transactions to NULL
// if they are equal to arg.
// Callbacks will still be called later, they should handle this gracefully.
void rpidali_cancel(RpiDaliPtr dali, void *arg);


#endif /*_RPIDALI_H*/