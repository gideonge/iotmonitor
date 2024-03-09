#include "serial.h"
#include "wit_c_sdk.h"
#include "REG.h"
#include <stdint.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <unistd.h>
// for daemon
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>


#include "yyjson.h" //for json lib
/* MQTT Header Start */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MQTTClient.h"
/* MQTT Header End */

#define ACC_UPDATE 0x01
#define GYRO_UPDATE 0x02
#define ANGLE_UPDATE 0x04
#define MAG_UPDATE 0x08
#define READ_UPDATE 0x80

#define SWITCH_CTRL 29

static int fd, s_iCurBaud = 9600;
static volatile char s_cDataUpdate = 0, s_cCmd = 0xff;

const int c_uiBaud[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};

static void AutoScanSensor(char *dev);
static void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum);
static void SensorUartSend(uint8_t *p_data, uint32_t uiSize);
static void RS485_IO_Init(void);
static void Delayms(uint16_t ucMs);

/*MQTT Client Code Start*/
#define ADDRESS "tcp://127.0.0.1:1883"
#define CLIENTID "ExampleClientPub"
#define TOPIC "test/topic"
#define PAYLOAD "Hello World!"
#define QOS 1
#define TIMEOUT 10000L
MQTTClient client;
MQTTClient_deliveryToken token;

//#define MQTT_TRACE


// send mqtt data; the highest level
int mqtt_send_data(char *payload, MQTTClient_connectOptions *pConn_opts, MQTTClient_message *pPubmsg, char *mqttTopic)
{
	int rc;
	pPubmsg->payload = payload;
	pPubmsg->payloadlen = (int)strlen(payload);
	pPubmsg->qos = QOS;
	pPubmsg->retained = 0;
	if ((rc = MQTTClient_publishMessage(client, mqttTopic, pPubmsg, &token)) != MQTTCLIENT_SUCCESS)
	{
#ifdef MQTT_TRACE		
		syslog (LOG_ERR,"Failed to publish message, return code %d\n", rc);
#endif	
		//try to reconnect
		pConn_opts->keepAliveInterval = 200;
		pConn_opts->cleansession = 1;
		if ((rc = MQTTClient_connect(client, pConn_opts)) != MQTTCLIENT_SUCCESS)
		{
#ifdef MQTT_TRACE			
			syslog (LOG_ERR,"Failed to connect, return code %d\n", rc);
#endif		
			return rc;
		}
		else 
		{
			return -99;
		}
	}
#ifdef MQTT_TRACE
	syslog (LOG_INFO,"Waiting for up to %d seconds for publication of %s\n"
		   "on topic %s for client with ClientID: %s\n",
		   (int)(TIMEOUT / 1000), PAYLOAD, TOPIC, CLIENTID);
#endif
	rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);


#ifdef MQTT_TRACE	
	syslog (LOG_INFO,"Message with delivery token %d delivered\n", token);
#endif	
	/*
	if ((rc = MQTTClient_disconnect(client, 10000)) != MQTTCLIENT_SUCCESS)
		printf("Failed to disconnect, return code %d\n", rc);*/
	// MQTTClient_destroy(&client);
	return rc;
}

/* MQTT Client Code End */

/* Daemon code start */
static void daemonize()
{    /* Open the log file */
    openlog ("IotMonitor", LOG_PID, LOG_DAEMON);
}
/* Daemon code end */


// 函数用于检查IPv4地址的合法性
int isValidIPv4(const char *ip) 
{
    int segments[4]; // 存储四组数字
    int count = 0;    // 用于计数点（.）出现的次数
    int current = 0;  // 当前数字的开始位置
    int inSegment = 0; // 是否在一组数字中

    // 遍历IP地址字符串
    for (int i = 0; ip[i] != '\0'; ++i) {
        if (ip[i] == '.') {
            // 如果当前数字的开始位置是0，说明在点之前没有数字
            if (current == 0) return 0;
            // 如果已经有三个点，说明IP地址格式不正确
            if (count == 3) return 0;
            // 将当前数字转换为整数并存储
            segments[current] = atoi(&ip[current]);
            // 检查数字是否在0到255之间
            if (segments[current] < 0 || segments[current] > 255) return 0;
            // 更新当前数字的开始位置和点的计数
            current = i + 1;
            count++;
        } else if (ip[i] >= '0' && ip[i] <= '9') {
            // 如果字符是数字，继续当前数字
            inSegment = 1;
        } else {
            // 如果字符不是数字也不是点，IP地址格式不正确
            return 0;
        }
    }
    // 检查最后一个数字
    if (inSegment == 0 || current == 0) return 0; // 如果最后一个字符不是数字或没有数字
    segments[current] = atoi(&ip[current]);
    if (segments[current] < 0 || segments[current] > 255) return 0;
    // 如果点的数量是3，并且有四组数字，IP地址合法
    return (count == 3);
}

int isValidPort(const char *portStr) {
    // 检查字符串是否为空
    if (portStr == NULL) {
        return 0; // 不合法
    }

    // 尝试将字符串转换为整数
    unsigned int port = (unsigned int)atoi(portStr);

    // 检查转换后的整数是否在端口号的有效范围内
    if (port >= 0 && port <= 65535) {
        return 1; // 合法
    }

    return 0; // 不合法
}

#define MAX_LENGTH 150

int main(int argc, char *argv[])
{
	if (argc != 5)
	{
		syslog(LOG_ERR, "Usage: %s <device name> <mqtt ip address> <mqtt port number> <mqtt broker>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (isValidIPv4(argv[2]) == 0) 
	{
		syslog(LOG_ERR, "IP Address %s is not valid, please input valid IPv4 address.\n", argv[3]);
	}

	if (isValidPort(argv[3]) == 0)
	{
		syslog(LOG_ERR, "Port %s is not valid, please input valid port.\n", argv[4]);
	}

	char ip_info[MAX_LENGTH] = "";
	char mqtt_topic[MAX_LENGTH] = "";

	sprintf(ip_info, "tcp://%s:%s",  argv[2], argv[3]);
	sprintf(mqtt_topic, "%s", argv[4]);
	daemonize();

	// MQTT Client Initialization
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	int rc;
	if ((rc = MQTTClient_create(&client, ip_info, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
	{
#ifdef MQTT_TRACE		
		syslog (LOG_ERR,"Failed to create client, return code %d\n", rc);
#endif
		return -1;
	}


	if ((fd = serial_open(argv[1], s_iCurBaud) < 0))
	{
		syslog (LOG_ERR,"open %s fail\n", argv[1]);
		return 0;
	}
#ifdef MQTT_TRACE	
	else
		syslog (LOG_INFO,"open %s success\n", argv[1]);
#endif

	// Create parameters for registers
	float fAcc[3], fGyro[3], fAngle[3];
	int i, ret;
	char cBuff[1];
	char outputBuffer[100] = {0};
	/*RS485_IO_Init();*/
	WitInit(WIT_PROTOCOL_MODBUS, 0xff);
	WitRegisterCallBack(SensorDataUpdata);
	WitSerialWriteRegister(SensorUartSend);
	syslog (LOG_INFO,"\r\n********************** wit-motion Modbus check  ************************\r\n");
	AutoScanSensor(argv[1]);
	syslog (LOG_INFO,"\r\n********************** wit-motion Modbus check end  ************************\r\n");

	conn_opts.keepAliveInterval = 200;
	conn_opts.cleansession = 1;
	
	if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
	{
#ifdef MQTT_TRACE			
		syslog (LOG_ERR,"Failed to connect, return code %d\n", rc);
#endif		
		return -1;
	}

	while (1)
	{

		WitReadReg(AX, 12);
		Delayms(500);

		while (serial_read_data(fd, cBuff, 1))
		{
			WitSerialDataIn(cBuff[0]);
		}

		if (s_cDataUpdate)
		{
			yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
			yyjson_mut_val *root = yyjson_mut_obj(doc);
			yyjson_mut_doc_set_root(doc, root);

			for (i = 0; i < 3; i++)
			{
				fAcc[i] = sReg[AX + i] / 32768.0f * 16.0f;
				fGyro[i] = sReg[GX + i] / 32768.0f * 2000.0f;
				//syslog(LOG_INFO, "Debug: Sensor %d : %f\n", i, sReg[HX + i] / 32768.0f * 16.0f );
				fAngle[i] = sReg[Roll + i] / 32768.0f * 180.0f;
				//syslog(LOG_INFO, "Debug: Sensor %d : %f\n", i, sReg[TEMP]);
			}
			
			if (s_cDataUpdate & ACC_UPDATE)
			{
				yyjson_mut_val *acc = yyjson_mut_arr(doc);
				yyjson_mut_val *v1 = yyjson_mut_real(doc, fAcc[0]);
				yyjson_mut_val *v2 = yyjson_mut_real(doc, fAcc[1]);
				yyjson_mut_val *v3 = yyjson_mut_real(doc, fAcc[2]);
				yyjson_mut_arr_append(acc, v1);
				yyjson_mut_arr_append(acc, v2);
				yyjson_mut_arr_append(acc, v3);
				yyjson_mut_obj_add_val(doc, root, "acc", acc);
				s_cDataUpdate &= ~ACC_UPDATE;
			}
			if (s_cDataUpdate & GYRO_UPDATE)
			{
				yyjson_mut_val *gyro = yyjson_mut_arr(doc);
				yyjson_mut_val *v1 = yyjson_mut_real(doc, fGyro[0]);
				yyjson_mut_val *v2 = yyjson_mut_real(doc, fGyro[1]);
				yyjson_mut_val *v3 = yyjson_mut_real(doc, fGyro[2]);
				yyjson_mut_arr_append(gyro, v1);
				yyjson_mut_arr_append(gyro, v2);
				yyjson_mut_arr_append(gyro, v3);
				yyjson_mut_obj_add_val(doc, root, "gyro", gyro);
				s_cDataUpdate &= ~GYRO_UPDATE;
			}
			if (s_cDataUpdate & ANGLE_UPDATE)
			{

				yyjson_mut_val *angel = yyjson_mut_arr(doc);
				yyjson_mut_val *v1 = yyjson_mut_real(doc, fAngle[0]);
				yyjson_mut_val *v2 = yyjson_mut_real(doc, fAngle[1]);
				yyjson_mut_val *v3 = yyjson_mut_real(doc, fAngle[2]);
				yyjson_mut_arr_append(angel, v1);
				yyjson_mut_arr_append(angel, v2);
				yyjson_mut_arr_append(angel, v3);
				yyjson_mut_obj_add_val(doc, root, "angel", angel);
				s_cDataUpdate &= ~ANGLE_UPDATE;
			}
			if (s_cDataUpdate & MAG_UPDATE)
			{
				yyjson_mut_val *mag = yyjson_mut_arr(doc);
				yyjson_mut_val *v1 = yyjson_mut_real(doc, sReg[0]);
				yyjson_mut_val *v2 = yyjson_mut_real(doc, sReg[1]);
				yyjson_mut_val *v3 = yyjson_mut_real(doc, sReg[2]);
				yyjson_mut_arr_append(mag, v1);
				yyjson_mut_arr_append(mag, v2);
				yyjson_mut_arr_append(mag, v3);
				yyjson_mut_obj_add_val(doc, root, "mag", mag);
				s_cDataUpdate &= ~MAG_UPDATE;
			}
			
			const char *json = yyjson_mut_write(doc, 0, NULL);
			if (json)
			{
				mqtt_send_data((char *)json, &conn_opts, &pubmsg, mqtt_topic);
				free((void *)json);
			}
			free(doc);
			s_cDataUpdate = 0;
		}
	}

	serial_close(fd);
	closelog();
	return 0;
}

static void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum)
{
	int i;
	for (i = 0; i < uiRegNum; i++)
	{
		switch (uiReg)
		{
			//            case AX:
			//            case AY:
		case AZ:
			s_cDataUpdate |= ACC_UPDATE;
			break;
			//            case GX:
			//            case GY:
		case GZ:
			s_cDataUpdate |= GYRO_UPDATE;
			break;
			//            case HX:
			//            case HY:
		case HZ:
			s_cDataUpdate |= MAG_UPDATE;
			break;
			//            case Roll:
			//            case Pitch:
		case Yaw:
			s_cDataUpdate |= ANGLE_UPDATE;
			break;
		default:
			s_cDataUpdate |= READ_UPDATE;
			break;
		}
		uiReg++;
	}
}

static void Delayms(uint16_t ucMs)
{
	usleep(ucMs * 1000);
}

static void RS485_IO_Init()
{
	int ret = wiringPiSetup();
	if (ret == -1)
	{
		exit(-1);
	}

	pinMode(SWITCH_CTRL, OUTPUT);

	digitalWrite(SWITCH_CTRL, HIGH);
}

static void SensorUartSend(uint8_t *p_data, uint32_t uiSize)
{
	uint32_t uiDelayUs = 0;

	uiDelayUs = ((1000000 / (s_iCurBaud / 10)) * uiSize) + 300;

	digitalWrite(SWITCH_CTRL, HIGH);
	serial_write_data(fd, p_data, uiSize);

	usleep(uiDelayUs);

	digitalWrite(SWITCH_CTRL, LOW);
}

static void AutoScanSensor(char *dev)
{
	int i, iRetry;
	char cBuff[2];
	for (i = 0; i < 10; i++)
	{

		serial_close(fd);
		s_iCurBaud = c_uiBaud[i];
		fd = serial_open(dev, c_uiBaud[i]);
		iRetry = 2;
		s_cDataUpdate = 0;
		do
		{

			WitReadReg(AX, 3);
			Delayms(200);
			while (serial_read_data(fd, cBuff, 1))
			{
				WitSerialDataIn(cBuff[0]);
			}

			if (s_cDataUpdate != 0)
			{
				syslog (LOG_INFO,"%d baud find sensor\r\n\r\n", c_uiBaud[i]);
				return;
			}
			iRetry--;
		} while (iRetry);
	}
	syslog (LOG_ERR,"can not find sensor\r\n");
	syslog (LOG_INFO,"please check your connection\r\n");
}
