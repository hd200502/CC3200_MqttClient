#include <stdio.h>
#include <string.h>
#include "libemqtt.h"


#include "simplelink.h"
#include "wlan.h"
#include "common.h"
#include "utils.h"
#include "hw_ints.h"
#include "hw_types.h"
#include "uart_if.h"
#include "cc_timer.h"
#include "rtc_hal.h"

#define DPRINTF         Report

#define MQTT_CLIENT_ID         "135646308390000"
#define MQTT_CLIENT_USER_NAME  NULL
#define MQTT_CLIENT_USER_PWD   NULL
#define MQTT_CLIENT_ALIVE_TIME 30
#define MQTT_CLIENT_SUB_TOPIC  "135646308390000"

#define MQTT_SERVER_ADDRESS    "www.lianyun.tv"
#define MQTT_SERVER_PORTNUM    1883

static S8  MqttPingResp = 0;
static S16 MqttSocketId = 0;
static mqtt_broker_handle_t MqttBroker;
static S8  MqttPacketBuffer[1024];

int MqttSocketSend(void* si, const void* buf, unsigned int len)
{
	if (sl_Send(*((int*)si), buf, len, 0) < 0)
	{
		sl_Close(*((int*)si));
		DPRINTF("socket send error!\r\n");
		return 0;
	}
	else
		return len;
}

int MqttSocketConnect(char* server_addr, int port)
{
	int status;
	U32 uiIP;
	SlSockAddrIn_t sAddr;

	status = sl_NetAppDnsGetHostByName((S8*)server_addr, strlen(server_addr), (unsigned long*)&uiIP, SL_AF_INET);

	DPRINTF("Host IP:%d.%d.%d.%d \r\n", (uiIP>>24), ((uiIP>>16)&0xff), ((uiIP>>8)&0xff), (uiIP&0xff));

    sAddr.sin_family = SL_AF_INET;
    sAddr.sin_port = sl_Htons((unsigned short)port);
    sAddr.sin_addr.s_addr = sl_Htonl((unsigned int)uiIP);

	MqttSocketId = sl_Socket(SL_AF_INET,SL_SOCK_STREAM, 0);
	if( MqttSocketId < 0 )
    {
        DPRINTF("SOCKET_CREATE_ERROR.\r\n");
		return -1;
    }

	status = sl_Connect(MqttSocketId, ( SlSockAddr_t *)&sAddr, sizeof(sAddr));
    if( status < 0 )
    {
        sl_Close(MqttSocketId);
        DPRINTF("SOCKET_CONNECT_ERROR.\r\n");
		return -2;
    }

	return 0;
}

S16 MqttSocketReadPacket(S16 sid, U8* buffer, int len)
{
    S16 nrcvd, total=0, packet_length;
    U16 rem_len;
    U8  rem_len_bytes;
    nrcvd = sl_Recv(sid, buffer, len, 0);
    if (nrcvd < 2)
        return -1;

    total = nrcvd;
    // now we have the full fixed header in packet_buffer
    // parse it for remaining length and number of bytes
    rem_len = mqtt_parse_rem_len((const U8*)buffer);
    rem_len_bytes = mqtt_num_rem_len_bytes((const U8*)buffer);
    packet_length = rem_len + rem_len_bytes + 1;

    //DPRINTF("packet len:%d.\r\n", packet_length);
    if (packet_length > len)
    {
        return -2;
    }

    while (total < packet_length)
    {
        nrcvd = sl_Recv(sid, (buffer+total), len-total, 0);
        if (nrcvd < 0)
        {
            return -3;
        }
        total += nrcvd;
    }
    return total;
}

static cc_hndl MqttAliveTimerHndl = NULL;

static void MqttAliveTimerCallBack(void* param)
{
    struct u64_time sIntervalTimer;
    sIntervalTimer.secs = MQTT_CLIENT_ALIVE_TIME;
    sIntervalTimer.nsec = 0;
    cc_timer_start(MqttAliveTimerHndl, &sIntervalTimer, OPT_TIMER_PERIODIC);

    mqtt_ping(&MqttBroker);
    MqttPingResp = 0;
}

static void MqttAliveTimerStart()
{
    struct cc_timer_cfg sRealTimeTimer;
    struct u64_time sInitTime, sIntervalTimer;

    // setting up Timer as a wk up source and other timer configurations
    sInitTime.secs = 0;
    sInitTime.nsec = 0;
    cc_rtc_set(&sInitTime);

    sRealTimeTimer.source = HW_REALTIME_CLK;
    sRealTimeTimer.timeout_cb = MqttAliveTimerCallBack;
    sRealTimeTimer.cb_param = NULL;

    MqttAliveTimerHndl = cc_timer_create(&sRealTimeTimer);

    sIntervalTimer.secs = MQTT_CLIENT_ALIVE_TIME;
    sIntervalTimer.nsec = 0;
    cc_timer_start(MqttAliveTimerHndl, &sIntervalTimer, OPT_TIMER_PERIODIC);
}

static void MqttAliveTimerStop()
{
    if (MqttAliveTimerHndl)
    {
        cc_timer_stop(MqttAliveTimerHndl);
        cc_timer_delete(MqttAliveTimerHndl);
        MqttAliveTimerHndl = NULL;
    }
}

static S32 MqttSocketRun(const U8* packet_buffer, S16 packet_length)
{
    U16 msgid;
    if(packet_length <= 0)
    {
        return -1;
    }
    else
    {
        switch(MQTTParseMessageType(packet_buffer))
        {
        case MQTT_MSG_CONNECT:
            DPRINTF("mqtt client not support MQTT_MSG_CONNECT.\r\n");
            break;

        case MQTT_MSG_CONNACK:
            DPRINTF("MQTT_MSG_CONNACK.\r\n");
            if(mqtt_subscribe(&MqttBroker, MQTT_CLIENT_SUB_TOPIC, &msgid) < 0)
            {
                DPRINTF("mqtt_subscribe fail.\r\n");
                return -3;
            }
            break;

        case MQTT_MSG_SUBSCRIBE:
            DPRINTF("mqtt client not support MQTT_MSG_SUBSCRIBE.\r\n");
            break;

        case MQTT_MSG_SUBACK:
            DPRINTF("MQTT_MSG_SUBACK.\r\n");
            if(mqtt_parse_msg_id(packet_buffer) != msgid)
            {
                DPRINTF("MqttSocketInit msgid:%d!\r\n",  msgid);
                return -6;
            }
            MqttPingResp = 1;
            MqttAliveTimerStart();
            break;

        case MQTT_MSG_PUBLISH:
            DPRINTF("MQTT_MSG_PUBLISH.\r\n");
            {
                U8 topic[128], msg[1024];
                U8 * msg_p;
                U16 len;
                len = mqtt_parse_pub_topic(packet_buffer, topic);
                topic[len] = '\0';
                len = mqtt_parse_publish_msg(packet_buffer, msg, 1000, &msg_p);
                msg[(len < 1000) ? len : 999] = '\0'; // for printf
                DPRINTF("MqttSocketRun:%s [%d]%s\n", topic, len, msg);

                //if(mqttsocket_ctx.cb != NULL)
                //    mqttsocket_ctx.cb(msg_p, len);
            }
            break;

        case MQTT_MSG_PUBACK:
            DPRINTF("MQTT_MSG_PUBACK.\r\n");
            break;

        case MQTT_MSG_PUBREC:
            DPRINTF("MQTT_MSG_PUBREC.\r\n");
            break;

        case MQTT_MSG_PUBREL:
            DPRINTF("MQTT_MSG_PUBREL.\r\n");
            break;

        case MQTT_MSG_PUBCOMP:
            DPRINTF("MQTT_MSG_PUBCOMP.\r\n");
            break;

        case MQTT_MSG_UNSUBSCRIBE:
            DPRINTF("MQTT_MSG_UNSUBSCRIBE.\r\n");
            break;

        case MQTT_MSG_PINGREQ:
            DPRINTF("mqtt client not support MQTT_MSG_PINGREQ.\r\n");
            break;

        case MQTT_MSG_PINGRESP:
            DPRINTF("MQTT_MSG_PINGRESP.\r\n");
            MqttPingResp = 1;
            break;

        case MQTT_MSG_DISCONNECT:
            DPRINTF("MQTT_MSG_DISCONNECT.\r\n");
            break;

        default:
            DPRINTF("MessageType 0x%x.\r\n", MQTTParseMessageType(packet_buffer));
            break;
        }
#if 0
        if(MQTTParseMessageType(packet_buffer) == MQTT_MSG_PUBLISH)
        {
            U8 topic[128], msg[1024];
            U8 * msg_p;
            U16 len;
            len = mqtt_parse_pub_topic(packet_buffer, topic);
            topic[len] = '\0';
            len = mqtt_parse_publish_msg(packet_buffer, msg, 1000, &msg_p);
            msg[(len < 1000) ? len : 999] = '\0'; // for printf
            DPRINTF("MqttSocketRun:%s [%d]%s\n", topic, len, msg);

            //if(mqttsocket_ctx.cb != NULL)
            //    mqttsocket_ctx.cb(msg_p, len);

        }
        else if(MQTTParseMessageType(packet_buffer) == MQTT_MSG_PINGRESP)
        {
            //alive_ping_response_waiting = FALSE;
        }
        else if(MQTTParseMessageType(packet_buffer) == MQTT_MSG_PUBREC)
        {
            //if((g_ms_public_msg_ctx) && (mqtt_parse_msg_id(packet_buffer) == g_ms_public_msg_ctx->msg_id))
            {
                //g_ms_public_msg_ctx->flow = PUBREL;

                //mqttsocket_sendmsg_restart();
            }
        }
        else if(MQTTParseMessageType(packet_buffer) == MQTT_MSG_PUBCOMP)
        {
            //if((g_ms_public_msg_ctx) && (mqtt_parse_msg_id(packet_buffer) == g_ms_public_msg_ctx->msg_id))
            {
                //g_ms_public_msg_ctx->flow = PUBCMP;

                //mqttsocket_sendmsg_restart();
            }

            //ping_ext_count = 0;


        }
        else if(MQTTParseMessageType(packet_buffer) == MQTT_MSG_PUBACK)
        {
            //if((g_ms_public_msg_ctx) && (mqtt_parse_msg_id(packet_buffer) == g_ms_public_msg_ctx->msg_id))
            {
                //g_ms_public_msg_ctx->flow = PUBCMP;

                //mqttsocket_sendmsg_restart();
            }

        }
#endif
    }
    return 0;
}

void MqttClientStart(void *parm)
{
	mqtt_init(&MqttBroker, MQTT_CLIENT_ID);
	mqtt_init_auth(&MqttBroker, MQTT_CLIENT_USER_NAME, MQTT_CLIENT_USER_PWD);

	mqtt_set_alive(&MqttBroker, MQTT_CLIENT_ALIVE_TIME);
	MqttSocketConnect(MQTT_SERVER_ADDRESS, MQTT_SERVER_PORTNUM);
	MqttBroker.socket_info = &MqttSocketId;
	MqttBroker.Send = MqttSocketSend;
	mqtt_connect(&MqttBroker);

	while (1)
	{
	    S16 packet_length;
	    const U8* packet_buffer = (U8*)MqttPacketBuffer;

	    packet_length = MqttSocketReadPacket(MqttSocketId, (U8*)packet_buffer, sizeof(MqttPacketBuffer));
	    DPRINTF("MqttSocketReadPacket PL:%d.\r\n", packet_length);

	    if(packet_length > 0)
	    {
	        MqttSocketRun(packet_buffer, packet_length);
	    }
	}
}
