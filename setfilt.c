// $Id: setfilt.c,v 1.4 2022/12/29 05:58:17 karn Exp $
// Interactive program to set predetection filters

#define _GNU_SOURCE 1
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <unistd.h>
#if defined(linux)
#include <bsd/string.h>
#endif
#include <string.h>
#include <sys/socket.h>
#include <locale.h>
#include <errno.h>
#include <ctype.h>
#include <sysexits.h>

#include "misc.h"
#include "multicast.h"
#include "status.h"

int Mcast_ttl = 5;
int IP_tos = 0;
const char *App_path;
int Verbose;
char *Radio = NULL;
char *Locale = "en_US.UTF-8";

struct sockaddr_storage Control_address;
int Status_sock = -1;
int Control_sock = -1;

char Optstring[] = "vl:r:V";
struct option Options[] = {
    {"radio", required_argument, NULL, 'r'},
    {"locale", required_argument, NULL, 'l'},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {NULL, 0, NULL, 0},
};

int main(int argc,char *argv[]){
  App_path = argv[0];
  {
    char * const cp = getenv("LANG");
    if(cp != NULL)
      Locale = cp;
  }
  {
    int c;
    while((c = getopt_long(argc,argv,Optstring,Options,NULL)) != -1){
      switch(c){
      case 'V':
	fprintf(stdout,"%s last modified %s\n",__FILE__,__TIMESTAMP__);
	fprintf(stdout,"Copyright 2023, Phil Karn, KA9Q. May be used under the terms of the GNU Public License\n");
	exit(EX_OK);
      case 'v':
	Verbose++;
	break;
      case 'l':
	Locale = optarg;
	break;
      case 'r':
	Radio = optarg;
	break;
      }
    }
  }
  setlocale(LC_ALL,Locale);

  if(Radio == NULL)
    Radio = getenv("RADIO");

  if(Radio == NULL){
    fprintf(stderr,"--radio not specified and $RADIO not set\n");
    exit(1);
  }
  char iface[1024];
  resolve_mcast(Radio,&Control_address,DEFAULT_STAT_PORT,iface,sizeof(iface));
  Status_sock = listen_mcast(&Control_address,iface);

  if(Status_sock == -1){
    fprintf(stderr,"Can't open Status_sock socket to radio control channel %s: %s\n",Radio,strerror(errno));
    exit(1);
  }
  Control_sock = connect_mcast(&Control_address,iface,Mcast_ttl,IP_tos);
  if(Control_sock == -1){
    fprintf(stderr,"Can't open cmd socket to radio control channel %s: %s\n",Radio,strerror(errno));
    exit(1);
  }

  uint32_t sent_tag = 0; // Used only if sent_freq != 0
  int cmd_sent = 0;
  if(optind+1 < argc){
    // Args specified
    cmd_sent = 1;
    if(tolower(argv[optind][0]) == 'm')
      argv[optind][0] = '-';
    float low = strtof(argv[optind],NULL);
    
    if(tolower(argv[optind+1][0]) == 'm')
      argv[optind+1][0] = '-';
    float high = strtof(argv[optind+1],NULL);

    if(low > high){
      float t = low;
      low = high;
      high = t;
    }
    uint8_t buffer[8192];
    uint8_t *bp = buffer;
    
    *bp++ = 1; // Generate command packet
    sent_tag = random();
    encode_int(&bp,COMMAND_TAG,sent_tag);
    encode_float(&bp,LOW_EDGE,low);
    encode_float(&bp,HIGH_EDGE,high);
    encode_eol(&bp);
    int cmd_len = bp - buffer;
    if(send(Control_sock, buffer, cmd_len, 0) != cmd_len)
      perror("command send");
  }
  // Read and process status
  for(;;){
    uint8_t buffer[8192];
    int length = recvfrom(Status_sock,buffer,sizeof(buffer),0,NULL,NULL);
    if(length <= 0){
      fprintf(stderr,"recvfrom status socket error: %s\n",strerror(errno));
      sleep(1);
      continue;
    }
    // We could check the source address here, but we have no way of verifying it.
    // But there should only be one host sending status to this group anyway
    uint8_t const * cp = buffer;
    if(*cp++ != 0)
      continue; // Look only at status packets

    uint32_t received_tag = 0;
    float low = NAN;
    float high = NAN;

    while(cp - buffer < length){
      enum status_type type = *cp++;
      if(type == EOL)
	break;
      unsigned int optlen = *cp++;
      if(cp - buffer + optlen > length)
	break; // Invalid length
      switch(type){
      default:
	break;
      case COMMAND_TAG:
	received_tag = (uint32_t)decode_int(cp,optlen);
	break;
      case LOW_EDGE:
	low = decode_float(cp,optlen);
	break;
      case HIGH_EDGE:
	high = decode_float(cp,optlen);
	break;
      }
      cp += optlen;
    }
    if(isnan(low) || isnan(high))
      continue; // Didn't get what we wanted
    // If we sent a command, wait for its acknowledgement
    // Otherwise, just display the current frequency
    if(cmd_sent && received_tag != sent_tag)
      continue;
    printf("%'.0f <-> %'.0f Hz\n",low,high);
    break;
  }
  exit(0);
}

