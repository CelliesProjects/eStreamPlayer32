/* system setup file for eStreamPlayer32 */

const char * SSID = "xxx";
const char * PSK = "xxx";

/* uncomment one of the following lines to compile for a board or dac */
#define   GENERIC_I2S_DAC
//#define   A1S_AUDIO_KIT
//#define   M5STACK_NODE

#if defined (GENERIC_I2S_DAC)
/* I2S pins on Cellie's dev board */
#define I2S_BCK     21
#define I2S_WS      26
#define I2S_DOUT    22
#endif  //GENERIC_I2S_DAC

/* SCRIPT_URL should point to the php script on the music file server */
const char * SCRIPT_URL = "http://192.168.0.30/music/eSP32.php";

/* If SET_STATIC_IP is set to true then STATIC_IP, GATEWAY, SUBNET and PRIMARY_DNS have to be set to some sane values */
const bool SET_STATIC_IP = false;

const IPAddress STATIC_IP(192, 168, 0, 10);              /* This should be outside your router dhcp range! */
const IPAddress GATEWAY(192, 168, 0, 1);                 /* Set to your gateway ip address */
const IPAddress SUBNET(255, 255, 255, 0);                /* Usually 255,255,255,0 check in your router or pc connected to the same network */
const IPAddress PRIMARY_DNS(192, 168, 0, 30);            /* Check in your router */
const IPAddress SECONDARY_DNS(0, 0, 0, 0);               /* Check in your router */

const char* NTP_POOL = "nl.pool.ntp.org";

const char* TIMEZONE = "CET-1CEST,M3.5.0/2,M10.5.0/3";    /* Central European Time - see http://www.remotemonitoringsystems.ca/time-zone-abbreviations.php */
