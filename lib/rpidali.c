#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include "rpidali.h"
#include "list.h"
#include "log.h"
#include "pack.h"
#include "util.h"


//const unsigned int DEFAULT_QUEUESIZE = 255; //max. queued commands
const size_t RPIDALI_LENGTH = 64;

enum {
    RPIDALI_DIRECTION_DALI = 0x11,
    RPIDALI_DIRECTION_USB = 0x12,
    RPIDALI_TYPE_16BIT = 0x03,
    RPIDALI_TYPE_24BIT = 0x04,
    RPIDALI_TYPE_NO_RESPONSE = 0x71,
    RPIDALI_TYPE_RESPONSE = 0x72,
    RPIDALI_TYPE_COMPLETE = 0x73,
    RPIDALI_TYPE_BROADCAST = 0x74,
    //USBDALI_TYPE_UNKNOWN = 0x77,
};

typedef struct {
	unsigned int seq_num;
	DaliFramePtr request;
	void *arg;
} RpiDaliTransaction;

struct RpiDaliSendTransfer{
    unsigned char *buffer;
    size_t length;
};

struct {} RpiDaliReceiveTransfer;

struct RpiDali
{

    DispatchPtr dispatch;
    RpiDaliTransaction *transaction;
	unsigned int queue_size;
	ListPtr queue;
	// Start value is 1 it seems
	unsigned int seq_num;
	RpiDaliInBandCallback req_callback;
	RpiDaliOutBandCallback bcast_callback;
	RpiDaliEventCallback event_callback;
	struct RpiDaliSendTransfer *send_transfer;
	RpiDaliReceiveTransferPtr *recv_transfer;
	void *bcast_arg;
	void *event_arg;
	ssize_t event_index;
	int shutdown;
	int detached;

};

static void rpidali_transaction_free(RpiDaliTransaction *transaction);
static void rpidali_next(RpiDaliPtr dali);
static RpiDaliTransaction *rpidali_transaction_new(DaliFramePtr request, void *arg);
static int rpidali_send(RpiDaliPtr dali, RpiDaliTransaction *transaction);
static void rpidali_print_in(uint8_t *buffer, size_t buflen);
static void rpidali_print_out(uint8_t *buffer, size_t buflen);
static int send_to_bus(struct RpiDali *dali);
int rpidali_get_timeout(RpiDaliPtr dali);
static int rpidali_receive(RpiDaliPtr pDali);

RpiDaliPtr rpidali_open(DispatchPtr dispatch){
    int detached = 0;

    RpiDaliPtr dali = malloc(sizeof(struct RpiDali));
		dali->transaction = NULL;
		dali->queue_size = 255;
		dali->queue = list_new((ListDataFreeFunc) rpidali_transaction_free);
		dali->seq_num = 1;
		dali->bcast_callback = NULL;
		dali->req_callback = NULL;
		dali->event_callback = NULL;
		dali->bcast_arg = NULL;
		dali->event_arg = NULL;
		dali->event_index = -1;
		dali->shutdown = 0;
		dali->detached = detached;
		dali->recv_transfer = NULL;
		dali->send_transfer = NULL;
return dali;										
}

static void rpidali_transaction_free(RpiDaliTransaction *transaction) {
	if (transaction) {
		daliframe_free(transaction->request);
		free(transaction);
	}
}

void rpidali_set_outband_callback(RpiDaliPtr dali, RpiDaliOutBandCallback callback, void *arg) {
	if (dali) {
		dali->bcast_callback = callback;
		dali->bcast_arg = arg;
	}
}

void rpidali_set_inband_callback(RpiDaliPtr dali, RpiDaliInBandCallback callback) {
	if (dali) {
		dali->req_callback = callback;
	}
}

RpiDaliError rpidali_queue(RpiDaliPtr dali, DaliFramePtr frame, void *cbarg) {
	if (dali) {
		log_debug("dali=%p frame=%p arg=%p", dali, frame, cbarg);
		if (list_length(dali->queue) < dali->queue_size) {
			RpiDaliTransaction *transaction = rpidali_transaction_new(frame, cbarg);
			if (transaction) {
				list_enqueue(dali->queue, transaction);
				log_info("Enqueued transfer (%p,%p)", transaction->request, transaction->arg);
				rpidali_next(dali);
				return RPIDALI_SUCCESS;
			}
		}
		return RPIDALI_QUEUE_FULL;
	}
	return RPIDALI_INVALID_ARG;
}

static int rpidali_send(RpiDaliPtr dali, RpiDaliTransaction *transaction) {
    if (dali && transaction && !dali->send_transfer && !dali->transaction) {
        //unsigned char *buffer = malloc(RPIDALI_LENGTH);
       // memset(buffer, 0, RPIDALI_LENGTH);
       // size_t length = RPIDALI_LENGTH;

        if (log_get_level() >= LOG_LEVEL_INFO) {
            log_info("Sending data to device:");
            //hexdump(buffer, RPIDALI_LENGTH);
            //rpidali_print_out(buffer, RPIDALI_LENGTH);
            printf("\n");
        }
        dali->transaction = transaction;
        dali->transaction->seq_num = dali->seq_num;
        if (dali->seq_num == 0xff) {
            // TODO: See if this actually works or if 0 is reserved
            dali->seq_num = 0;
        } else {
            dali->seq_num++;
        }
        RpiDaliSendTransferPtr sendTransfer = malloc(sizeof(struct RpiDaliSendTransfer));
        dali->send_transfer = sendTransfer;
       // libusb_fill_interrupt_transfer(dali->send_transfer, dali->handle, dali->endpoint_out, buffer, USBDALI_LENGTH, usbdali_send_callback, dali, dali->cmd_timeout);
        return send_to_bus(dali);
    }
    return -1;
}

static int send_to_bus(struct RpiDali *dali) {
    int fd;
    char buffer[7];
    int len = 7;
    int ret;
    struct pollfd fds;

    fd = open(DALI_drv, O_RDWR );
    if( fd < 0 ) {
        perror("Cannot open device \t");
        printf(" fd = %d \n",fd);
        return -1;
    }
    char buff[7];
    sprintf((char*) buff, "%02x", dali->transaction->seq_num);
    sprintf((char*)(buff+2), "%02x", dali->transaction->request->address);
    sprintf((char*) (buff+4), "%02x", dali->transaction->request->command);
    write(fd, buff, strlen(buff));
    bzero(buffer, len);
    fds.fd	= fd;
    fds.events	= POLLIN;
    l:
    ret = poll( &fds, 1, 34);

    switch (ret) {
        case -1:
            log_debug("error");
            return -1;
        case 0:
            log_debug("timeout");
            log_debug("Transfer completed without response");
            DaliFramePtr frame = daliframe_new(0, 0);
            dali->req_callback(RPIDALI_SUCCESS, frame, 0xff, 0, dali->transaction->arg);
            daliframe_free(frame);
            rpidali_transaction_free(dali->transaction);
            dali->transaction = NULL;
            free(dali->send_transfer);
            dali->recv_transfer = NULL;
            dali->send_transfer = NULL;
            return 0;
        default:
            read(fd, buffer, len);
            if (buffer[0]=='f' && buffer[1]=='f') goto l;
            log_debug("Receive %s", buffer);
            char seqStr[2] = {buffer[0], buffer[1]};
            char addrStr[2] = {buffer[2], buffer[3]};
            char commStr[2] = {buffer[4], buffer[5]};
            int seq = (int)strtol(seqStr, NULL, 16);
            int addr = (int)strtol(addrStr, NULL, 16);
            int comm = (int)strtol(commStr, NULL, 16);
            if (dali->transaction->seq_num == seq) {
                log_debug("Got response with sequence number (%d)", seq);
                log_debug("Got response with address number (%d)", addr);
                log_debug("Got response with command number (%d)", comm);
                DaliFramePtr frame = daliframe_new(addr, comm);
                dali->req_callback(RPIDALI_RESPONSE, frame, comm, 1, dali->transaction->arg);
                daliframe_free(frame);

            } else {
                log_warn("Got response with sequence number (%d) different from transaction (%d)",seq, dali->transaction->seq_num);
            }
            rpidali_transaction_free(dali->transaction);
            dali->transaction = NULL;
            free(dali->send_transfer);
            dali->recv_transfer = NULL;
            dali->send_transfer = NULL;

            if( 0 != close(fd) ){
                perror("Could not close device\n");
            }
            if (dali && !dali->shutdown) {
                rpidali_next(dali);
            }
            return 0;
    }


}

static void rpidali_next(RpiDaliPtr dali) {
	log_debug("Handling requests");
	 if (!dali->send_transfer) {
	 	if (dali->transaction) {
	 		if (!dali->recv_transfer) {
	 			log_debug("Not sending, transaction active, not receiving, starting receive");
	 			rpidali_receive(dali);
	 		}
	 	} else {
	 		if (list_length(dali->queue) > 0) {
	 			if (dali->recv_transfer) {
	 				log_debug("Not sending, no transaction active, queue not empty, receiving, canceling receive");
	// 				libusb_cancel_transfer(dali->recv_transfer);
	 			} else {
					RpiDaliTransaction *transaction = list_dequeue(dali->queue);
	 				if (transaction) {
	 					log_debug("Not sending, no transaction active, queue not empty, not receiving, starting send");
	 					rpidali_send(dali, transaction);
	 				} else {
	 					log_warn("Queue not empty, but no transaction returned");
	 					log_debug("Not sending, no transaction active, queue not empty, not receiving, nothing unqueued, starting receive");
	 					rpidali_receive(dali);
	 				}
	 			}
	 		} else {
	 			if (!dali->recv_transfer) {
	 				log_debug("Not sending, no transaction active, queue empty, not receiving, starting receive");
					rpidali_receive(dali);
	 			}
	 		}
	 	}
	 }
}

static int rpidali_receive(RpiDaliPtr pDali) {
    log_debug("Receiving message");
    return -1;
}


static RpiDaliTransaction *rpidali_transaction_new(DaliFramePtr request, void *arg) {
	RpiDaliTransaction *transaction = malloc(sizeof(RpiDaliTransaction));
	if (transaction) {
		memset(transaction, 0, sizeof(RpiDaliTransaction));
		transaction->request = request;
		transaction->arg = arg;
	}
	return transaction;
}

static void rpidali_print_out(uint8_t *buffer, size_t buflen) {
    if (buffer && buflen >= 8) {
        switch (buffer[0]) {
            case RPIDALI_DIRECTION_DALI:
                printf("Direction: DALI<->DALI ");
                break;
            case RPIDALI_DIRECTION_USB:
                printf("Direction: USB<->DALI ");
                break;
            default:
                printf("Direction: Unknown (%02x) ", buffer[0]);
                break;
        }
        printf("Sequence number: %02x ", buffer[1]);
        switch (buffer[3]) {
            case RPIDALI_TYPE_16BIT:
                printf("Type: 16bit DALI ");
                printf("Address: %02x ", buffer[6]);
                printf("Command: %02x ", buffer[7]);
                break;
            case RPIDALI_TYPE_24BIT:
                printf("Type: 24bit DALI ");
                printf("Command: %02x ", buffer[5]);
                printf("Address: %02x ", buffer[6]);
                printf("Value: %02x ", buffer[7]);
                break;
            default:
                printf("Type: Unknown (%02x) ", buffer[3]);
                printf("Data: %02x %02x %02x %02x ", buffer[4], buffer[5], buffer[6], buffer[7]);
                break;
        }
    }
}

int rpidali_get_timeout(RpiDaliPtr dali) {
    if (dali) {
        struct timeval tv = { 1, 0 };
            int tvms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
            //log_debug("Returning timeout %d", tvms);
            return tvms;
    }
    log_debug("Returning timeout -1");
    return -1;
}
void rpidali_close(RpiDaliPtr dali) {
    if (dali) {
        dali->shutdown = 1;
        list_free(dali->queue);
        rpidali_transaction_free(dali->transaction);
        struct timeval tv = { 0, 0 };
        free(dali);
    }
}
