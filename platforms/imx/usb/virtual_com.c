/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
/*${standard_header_anchor}*/
#include "board.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "fsl_device_registers.h"

#include "usb_device_config.h"

#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"

#include "usb_device_cdc_acm.h"
#include "usb_device_ch9.h"

#include "usb_device_descriptor.h"

#include "composite.h"

#include "DevConsole.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/
extern usb_device_endpoint_struct_t g_cdcVcomDicEndpoints[];

/* Line coding of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_lineCoding[LINE_CODING_SIZE] = {
    /* E.g. 0x00,0xC2,0x01,0x00 : 0x0001C200 is 115200 bits per second */
    (LINE_CODING_DTERATE >> 0U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 8U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 16U) & 0x000000FFU,
    (LINE_CODING_DTERATE >> 24U) & 0x000000FFU,
    LINE_CODING_CHARFORMAT,
    LINE_CODING_PARITYTYPE,
    LINE_CODING_DATABITS
};

/* Abstract state of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_abstractState[COMM_FEATURE_DATA_SIZE] = { (STATUS_ABSTRACT_STATE >> 0U) & 0x00FFU,
                                                           (STATUS_ABSTRACT_STATE >> 8U) &
                                                             0x00FFU };

/* Country code of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_countryCode[COMM_FEATURE_DATA_SIZE] = { (COUNTRY_SETTING >> 0U) & 0x00FFU,
                                                         (COUNTRY_SETTING >> 8U) & 0x00FFU };

/* CDC ACM information */
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static usb_cdc_acm_info_t s_usbCdcAcmInfo;
/* Data buffer for receiving and sending*/
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_currRecvBuf[DATA_BUFF_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_currSendBuf[DATA_BUFF_SIZE];
volatile static uint32_t s_recvSize = 0;
/* Set while a bulk IN transfer is in flight; s_currSendBuf must not be reused until the
 * send-complete event clears it. */
volatile static uint8_t s_sendBusy = 0;
volatile static usb_device_composite_struct_t* g_deviceComposite;

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief CDC class specific callback function.
 *
 * This function handles the CDC class specific requests.
 *
 * @param handle          The CDC ACM class handle.
 * @param event           The CDC ACM class event type.
 * @param param           The parameter of the class specific request.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomCallback(class_handle_t handle, uint32_t event, void* param)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    uint32_t len;
    uint8_t* uartBitmap;
    usb_cdc_acm_info_t* acmInfo = &s_usbCdcAcmInfo;
    usb_device_cdc_acm_request_param_struct_t* acmReqParam;
    usb_device_endpoint_callback_message_struct_t* epCbParam;
    acmReqParam = (usb_device_cdc_acm_request_param_struct_t*)param;
    epCbParam = (usb_device_endpoint_callback_message_struct_t*)param;
    switch (event)
    {
        case kUSB_DeviceCdcEventSendResponse:
        {
            if ((epCbParam->length != 0) &&
                (!(epCbParam->length % g_cdcVcomDicEndpoints[0].maxPacketSize)))
            {
                /* If the last packet is the size of endpoint, then send also zero-ended packet,
                 ** meaning that we want to inform the host that we do not have any additional
                 ** data, so it can flush the output.
                 */
                error = USB_DeviceCdcAcmSend(handle, USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT, NULL, 0);
                if (kStatus_USB_Success != error)
                {
                    s_sendBusy = 0;
                }
            }
            else
            {
                s_sendBusy = 0;
            }
        }
        break;
        case kUSB_DeviceCdcEventRecvResponse:
        {
            if ((1 == g_deviceComposite->cdcVcom.attach))
            {
                s_recvSize = epCbParam->length;

                if (!s_recvSize)
                {
                    /* Schedule buffer for next receive event */
                    error = USB_DeviceCdcAcmRecv(handle,
                                                 USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT,
                                                 s_currRecvBuf,
                                                 g_cdcVcomDicEndpoints[1].maxPacketSize);
                }
            }
        }
        break;
        case kUSB_DeviceCdcEventSerialStateNotif:
            ((usb_device_cdc_acm_struct_t*)handle)->hasSentState = 0;
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventSendEncapsulatedCommand:
            break;
        case kUSB_DeviceCdcEventGetEncapsulatedResponse:
            break;
        case kUSB_DeviceCdcEventSetCommFeature:
            if (USB_DEVICE_CDC_FEATURE_ABSTRACT_STATE == acmReqParam->setupValue)
            {
                if (1 == acmReqParam->isSetup)
                {
                    *(acmReqParam->buffer) = s_abstractState;
                    *(acmReqParam->length) = sizeof(s_abstractState);
                }
                else
                {
                    /* no action, data phase, s_abstractState has been assigned */
                }
                error = kStatus_USB_Success;
            }
            else if (USB_DEVICE_CDC_FEATURE_COUNTRY_SETTING == acmReqParam->setupValue)
            {
                if (1 == acmReqParam->isSetup)
                {
                    *(acmReqParam->buffer) = s_countryCode;
                    *(acmReqParam->length) = sizeof(s_countryCode);
                }
                else
                {
                    /* no action, data phase, s_countryCode has been assigned */
                }
                error = kStatus_USB_Success;
            }
            else
            {
                /* no action, return kStatus_USB_InvalidRequest */
            }
            break;
        case kUSB_DeviceCdcEventGetCommFeature:
            if (USB_DEVICE_CDC_FEATURE_ABSTRACT_STATE == acmReqParam->setupValue)
            {
                *(acmReqParam->buffer) = s_abstractState;
                *(acmReqParam->length) = COMM_FEATURE_DATA_SIZE;
                error = kStatus_USB_Success;
            }
            else if (USB_DEVICE_CDC_FEATURE_COUNTRY_SETTING == acmReqParam->setupValue)
            {
                *(acmReqParam->buffer) = s_countryCode;
                *(acmReqParam->length) = COMM_FEATURE_DATA_SIZE;
                error = kStatus_USB_Success;
            }
            else
            {
                /* no action, return kStatus_USB_InvalidRequest */
            }
            break;
        case kUSB_DeviceCdcEventClearCommFeature:
            break;
        case kUSB_DeviceCdcEventGetLineCoding:
            *(acmReqParam->buffer) = s_lineCoding;
            *(acmReqParam->length) = LINE_CODING_SIZE;
            error = kStatus_USB_Success;
            break;
        case kUSB_DeviceCdcEventSetLineCoding:
        {
            if (1 == acmReqParam->isSetup)
            {
                *(acmReqParam->buffer) = s_lineCoding;
                *(acmReqParam->length) = sizeof(s_lineCoding);
            }
            else
            {
                /* no action, data phase, s_lineCoding has been assigned */
            }
            error = kStatus_USB_Success;
        }
        break;
        case kUSB_DeviceCdcEventSetControlLineState:
        {
            error = kStatus_USB_Success;
            s_usbCdcAcmInfo.dteStatus = acmReqParam->setupValue;
            /* activate/deactivate Tx carrier */
            if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_CARRIER_ACTIVATION)
            {
                acmInfo->uartState |= USB_DEVICE_CDC_UART_STATE_TX_CARRIER;
            }
            else
            {
                acmInfo->uartState &= (uint16_t)~USB_DEVICE_CDC_UART_STATE_TX_CARRIER;
            }

            /* activate carrier and DTE. Com port of terminal tool running on PC is open now */
            if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE)
            {
                acmInfo->uartState |= USB_DEVICE_CDC_UART_STATE_RX_CARRIER;
            }
            /* Com port of terminal tool running on PC is closed now */
            else
            {
                acmInfo->uartState &= (uint16_t)~USB_DEVICE_CDC_UART_STATE_RX_CARRIER;
            }

            /* Indicates to DCE if DTE is present or not */
            acmInfo->dtePresent =
              (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE) ? true : false;

            /* Initialize the serial state buffer */
            acmInfo->serialStateBuf[0] = NOTIF_REQUEST_TYPE;                /* bmRequestType */
            acmInfo->serialStateBuf[1] = USB_DEVICE_CDC_NOTIF_SERIAL_STATE; /* bNotification */
            acmInfo->serialStateBuf[2] = 0x00;                              /* wValue */
            acmInfo->serialStateBuf[3] = 0x00;
            acmInfo->serialStateBuf[4] = 0x00; /* wIndex */
            acmInfo->serialStateBuf[5] = 0x00;
            acmInfo->serialStateBuf[6] = UART_BITMAP_SIZE; /* wLength */
            acmInfo->serialStateBuf[7] = 0x00;
            /* Notify to host the line state */
            acmInfo->serialStateBuf[4] = acmReqParam->interfaceIndex;
            /* Lower byte of UART BITMAP */
            uartBitmap =
              (uint8_t*)&acmInfo->serialStateBuf[NOTIF_PACKET_SIZE + UART_BITMAP_SIZE - 2];
            uartBitmap[0] = acmInfo->uartState & 0xFFu;
            uartBitmap[1] = (acmInfo->uartState >> 8) & 0xFFu;
            len = (uint32_t)(NOTIF_PACKET_SIZE + UART_BITMAP_SIZE);
            if (0 == ((usb_device_cdc_acm_struct_t*)handle)->hasSentState)
            {
                error = USB_DeviceCdcAcmSend(
                  handle, USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT, acmInfo->serialStateBuf, len);
                if (kStatus_USB_Success != error)
                {
                    usb_echo("kUSB_DeviceCdcEventSetControlLineState error!");
                }
                ((usb_device_cdc_acm_struct_t*)handle)->hasSentState = 1;
            }

            /* Update status */
            if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_CARRIER_ACTIVATION)
            {
                /*  To do: CARRIER_ACTIVATED */
            }
            else
            {
                /* To do: CARRIER_DEACTIVATED */
            }
            if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE)
            {
                /* DTE_ACTIVATED */
                if (1 == g_deviceComposite->cdcVcom.attach)
                {
                    g_deviceComposite->cdcVcom.startTransactions = 1;
                }
            }
            else
            {
                /* DTE_DEACTIVATED */
                if (1 == g_deviceComposite->cdcVcom.attach)
                {
                    g_deviceComposite->cdcVcom.startTransactions = 0;
                }
            }
        }
        break;
        case kUSB_DeviceCdcEventSendBreak:
            break;
        default:
            break;
    }

    return error;
}

/*!
 * @brief Application task function.
 *
 * This function runs the task for application.
 *
 * @return None.
 */

void USB_DeviceCdcVcomTask(void)
{
    usb_status_t error = kStatus_USB_Error;
    if ((1 == g_deviceComposite->cdcVcom.attach))
    {
        /* User Code */
        /* endpoint callback length is USB_CANCELLED_TRANSFER_LENGTH (0xFFFFFFFFU) when transfer is
         * canceled */
        if ((0 != s_recvSize) && (USB_CANCELLED_TRANSFER_LENGTH != s_recvSize))
        {
            /* The serial manager never installs an RX callback here (debug console input is
             * disabled), so received bytes go straight to the development console. */
            DevConsole_RxBytes(s_currRecvBuf, s_recvSize);

            s_recvSize = 0;

            if (!s_recvSize)
            {
                /* Schedule buffer for next receive event */
                error = USB_DeviceCdcAcmRecv(g_deviceComposite->cdcVcom.cdcAcmHandle,
                                             USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT,
                                             s_currRecvBuf,
                                             g_cdcVcomDicEndpoints[1].maxPacketSize);
            }
        }
    }
    if ((1 == g_deviceComposite->cdcVcom.attach) &&
        (1 == g_deviceComposite->cdcVcom.startTransactions) && (0 == s_sendBusy))
    {
        /* One byte short of a full packet, so a transfer never ends on a packet boundary and
         * needs no zero-length packet to terminate it. */
        uint32_t size = DevConsole_TxRead(s_currSendBuf, DATA_BUFF_SIZE - 1);

        if (size)
        {
            s_sendBusy = 1;
            error = USB_DeviceCdcAcmSend(g_deviceComposite->cdcVcom.cdcAcmHandle,
                                         USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT,
                                         s_currSendBuf,
                                         size);

            if (error != kStatus_USB_Success)
            {
                s_sendBusy = 0;
            }
        }
    }
}

/*!
 * @brief Virtual COM device set configuration function.
 *
 * This function sets configuration for CDC class.
 *
 * @param handle The CDC ACM class handle.
 * @param configure The CDC ACM class configure index.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomSetConfigure(class_handle_t handle, uint8_t configure)
{
    if (USB_COMPOSITE_CONFIGURE_INDEX == configure)
    {
        g_deviceComposite->cdcVcom.attach = 1;
        s_sendBusy = 0;
        /* Schedule buffer for receive */
        USB_DeviceCdcAcmRecv(g_deviceComposite->cdcVcom.cdcAcmHandle,
                             USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT,
                             s_currRecvBuf,
                             g_cdcVcomDicEndpoints[1].maxPacketSize);
    }
    return kStatus_USB_Success;
}

/*!
 * @brief Virtual COM device initialization function.
 *
 * This function initializes the device with the composite device class information.
 *
 * @param deviceComposite The pointer to the composite device structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomInit(usb_device_composite_struct_t* deviceComposite)
{
    g_deviceComposite = deviceComposite;
    return kStatus_USB_Success;
}

status_t USB_DeviceVcomWrite(uint8_t controller, uint8_t* buffer, uint32_t length)
{
    /* Queued and sent by USB_DeviceCdcVcomTask. Sending from here would race with a transfer
     * already in flight and silently lose the data. */
    DevConsole_Write(buffer, length);

    if (NULL != g_deviceComposite->cdcVcom.tx_callback)
    {
        serial_manager_callback_message_t msg;

        msg.buffer = buffer;
        msg.length = length;

        g_deviceComposite->cdcVcom.tx_callback(
          g_deviceComposite->cdcVcom.tx_param, &msg, kStatus_SerialManager_Success);
    }

    return kStatus_Success;
}

status_t USB_DeviceVcomRead(uint8_t controller, uint8_t* buffer, uint32_t length)
{
    return kStatus_Success;
}

status_t USB_DeviceVcomInstallTxCallback(uint8_t controller,
                                         serial_manager_callback_t callback,
                                         void* callbackParam)
{
    g_deviceComposite->cdcVcom.tx_callback = callback;
    g_deviceComposite->cdcVcom.tx_param = callbackParam;

    return kStatus_Success;
}

status_t USB_DeviceVcomInstallRxCallback(uint8_t controller,
                                         serial_manager_callback_t callback,
                                         void* callbackParam)
{
    g_deviceComposite->cdcVcom.rx_callback = callback;
    g_deviceComposite->cdcVcom.rx_param = callbackParam;

    return kStatus_Success;
}

uint8_t USB_DeviceVcomClear()
{
    usb_cdc_acm_info_t* acmInfo = &s_usbCdcAcmInfo;
    return !(acmInfo->uartState & USB_DEVICE_CDC_UART_STATE_RX_CARRIER);
}
