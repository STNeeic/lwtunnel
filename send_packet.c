#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <string.h>
#include <stdlib.h>


int sockaddr_init(const char* address, int port, struct sockaddr* sockaddr) {
  struct sockaddr_in addr_in;
  addr_in.sin_family = AF_INET;

  if (inet_aton(address, &addr_in.sin_addr) == 0) {
    fprintf(stderr,"Invalid IPv4 Address.\n");
    return -1;
  }

  if(port < 49152 || port > 65535) {
    fprintf(stderr, "You must use private ports (49152-65535)\n");
    return -1;
  }

  addr_in.sin_port = htons(port);
  *sockaddr = *((struct sockaddr *) &addr_in);
  return 0;
}



int main(int argc, char* argv[]) {
  if(argc < 5) {
    fprintf(stderr,"Usage: ./send_packet.o [dst_ip] [port] [packet_size] [send_num]\n");
    return 1;
  }

  const char* address = argv[1];
  int port = atoi(argv[2]);
  int packet_size = atoi(argv[3]) - sizeof(struct iphdr) - sizeof(struct udphdr);
  int full_size = atoi(argv[3]);
  int send_num = atoi(argv[4]);
  struct sockaddr dst_addr;


  if(sockaddr_init(address,port,&dst_addr) != 0) return 1;


  if(packet_size < 0 || packet_size > 1500) {
    fprintf(stderr, "packet_size must larger than 0 and smaller than 1500\n");
    return 1;
  }
  if(send_num < 0) {
    fprintf(stderr,"send_num must positive integer.\n");
    return 1;
  }

  int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if(sock < 0){
    perror("socket() failed\n");
    return 1;
  }

  char * buf = malloc(sizeof(char) * packet_size);

  fprintf(stderr, "start sending packets...\n");
  fprintf(stderr, "packet size:%d(payload %d byte)\tsend_num:%d\n", full_size,packet_size, send_num);
  for(int i = 0; i < send_num; i++) {
    int result = sendto(sock, buf, packet_size, 0, &dst_addr, sizeof(dst_addr));
    if(result != packet_size) {
      fprintf(stderr,"%d-th try failed. (size res) = (%d %d)", i, packet_size, result);
    }
  }

  fprintf(stderr, "send all packet.\n");
  return 0;
}
