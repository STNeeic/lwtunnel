#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#ifdef DEBUG
#define debug( fmt, ... )                       \
  fprintf( stderr,                              \
           "[%s] %s:%u # " fmt "\n",            \
           __DATE__, __FILE__,                  \
           __LINE__, ##__VA_ARGS__              \
           )
#else
#define debug( fmt, ... ) ((void) 0)
#endif

#define TRAFFIC_PORT 50000
#define CONTROL_PORT 50001

#define BUF_MAX 256
#define PACKET_OFFS (int)(sizeof(struct udphdr) + 2 * sizeof(struct iphdr) + 4)
//4 is gre_base_hdr

enum transport_type {
  TCP,
  UDP
};


int itoa(int i, char* a) {
  int num = i, digits, res;
  for(digits = 0; num > 0; num /= 10) digits++;
  debug("digits=%d\n", digits);
  res = digits;

  a[digits] = '\0';
  for(num = i; num > 0; num /= 10) {
    digits--;
    a[digits] = num % 10 + '0';
  }

  return res;
}

double calc_interval(struct timeval start, struct timeval end) {
  time_t sec = end.tv_sec - start.tv_sec;
  suseconds_t usec = end.tv_usec - start.tv_usec;
  double interval = sec + usec*1e-6;
  return interval;
}

/*
 *
 *           client mode         *
 *
 */

void client_mode(char * dst_addr,unsigned int packet_size,unsigned int send_num, enum transport_type t_type) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock < 0) {
    perror("cannot create socket:");
    exit(1);
  }
   //binding local port (to reuse socket)
   struct sockaddr_in src;
   src.sin_family = AF_INET;
   src.sin_port = htons(CONTROL_PORT);
   src.sin_addr.s_addr = INADDR_ANY;
   bind(sock, (struct sockaddr *) &src, sizeof(src));
 

  //コントロール用のセッションを作る
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CONTROL_PORT);
  inet_pton(AF_INET,dst_addr, &addr.sin_addr.s_addr);

  if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connecting control socket faild");
    exit(1);
  }

  //設定を送る
  char buf[BUF_MAX];
  int len = itoa(packet_size, buf);
  write(sock, buf, len);
  sleep(1);
  memset(buf, 0, BUF_MAX);
  len = itoa(send_num, buf);
  write(sock, buf, len);
  debug("send %d %d\n", packet_size, send_num);
 //実験用のセッションを作る
  addr.sin_port = htons(TRAFFIC_PORT);
  int sock_type = t_type == TCP ? SOCK_STREAM : SOCK_DGRAM;
  int traffic = socket(AF_INET, sock_type, 0);

  //XDPの場合、アドレスを変える
 #ifdef USE_XDP
  #define XDP_ADDR "192.168.81.51"
  debug("XDP_MODE");
  if(inet_pton(AF_INET,XDP_ADDR,&addr.sin_addr.s_addr) < 0) {
    perror("XDP mode addr failed");
    exit(1);
  }
 #endif

  if(t_type == TCP) {
    //disable Nagle algorithm
    int flag = 1;
    setsockopt(traffic, IPPROTO_TCP, TCP_NODELAY,(char *)&flag, sizeof(flag));
  }


  if(connect(traffic, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connection failed");
    exit(1);
  }

  if(t_type == UDP) {
    //conectionを作るために1つ送る
    write(traffic, "A", 1);
    sleep(1);
    write(traffic, "A", 1);
  }

  sleep(1);
  write(sock,"start", 5);
  sleep(1);
  char traffic_buf[packet_size];
  struct timeval client_start_time = {};
  struct timeval client_end_time;
  gettimeofday(&client_start_time, NULL);
  for(int i = 0; i < send_num; i++) {
    write(traffic, traffic_buf, packet_size);
  }
  gettimeofday(&client_end_time, NULL);
  close(traffic);

  //実験終了。結果を貰う
  write(sock,"end", 3);
  memset(buf, 0 , BUF_MAX);
  len = read(sock, buf, BUF_MAX);
  close(sock);

  //UDPの場合header_sizeも考慮する
  unsigned long long int real_packet_size = t_type == UDP ? packet_size + PACKET_OFFS : packet_size;
  double client_interval = calc_interval(client_start_time, client_end_time);
  unsigned long long int client_byte = real_packet_size * (unsigned long long int) send_num;
  debug("%llu %lf %llu\n", real_packet_size, client_interval, client_byte);
  double client_bps = (double) client_byte / client_interval;
  double client_pps = client_bps / (double)real_packet_size;

  double server_interval;
  unsigned long long int server_byte;
  sscanf(buf,"%lf %llu", &server_interval, &server_byte);
  debug("server_byte:%llu", server_byte);
  //UDPの場合はintervalは負にする
  if(server_interval < 0) server_interval = client_interval;
  double server_bps = (double) server_byte / server_interval;
  double server_pps = server_bps /  (double) real_packet_size;
  fprintf(stderr,"packet_size: %lld send_num: %d\n", real_packet_size, send_num);
  fprintf(stderr,"server\t bps: %lf pps: %lf\n", server_bps, server_pps);
  fprintf(stderr,"client\t bps: %lf pps: %lf\n", client_bps, client_pps);
  printf("%d,%llu,%d,%lf,%lf,%lf,%lf,%lf,%lf\n",t_type, real_packet_size, send_num, server_interval,client_interval, server_bps, server_pps, client_bps, client_pps);
}


/*********************
 *    server mode    *
 *********************/

struct traffic_arg {
  int sock;
  unsigned int packet_len;
  unsigned int send_num;
  enum transport_type mode;
};

struct timeval server_start_time = {};
struct timeval server_end_time;

unsigned long long int server_total_bytes = 0;

void * traffic_func(void * input) {
  struct traffic_arg * arg = input;
  int traffic = arg->sock;
  int packet_len = arg->packet_len;
  int send_num = arg->send_num;
  enum transport_type mode = arg->mode;

  char buf[1500];
  server_total_bytes = 0;


  int len = 0;
  if(mode == TCP) {
    len += read(traffic, &buf, 1);
    gettimeofday(&server_start_time, NULL);
    for(; len < packet_len * send_num; len += read(traffic, &buf, 1500));
    gettimeofday(&server_end_time, NULL);
    server_total_bytes = packet_len * send_num;
  }
  else {
    while(1) {
      server_total_bytes += read(traffic, &buf, packet_len);
      //add header bytes
      server_total_bytes += PACKET_OFFS;
    }
  }
}

void server_mode(int sock, int traffic0, enum transport_type t_type) {

  //control用のtcp通信を開ける
  struct sockaddr_in client;
  int len = sizeof(client);
  int control = accept(sock, (struct sockaddr *) &client, &len);
  if(control < 0) {
    perror("accept@control");
    exit(1);
  }

  fprintf(stderr, "accepted control connection from %s, port=%d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));

  char control_buf[BUF_MAX];
  len = read(control, &control_buf, BUF_MAX);
  unsigned int packet_size = atoi(control_buf);
  memset(control_buf,0, BUF_MAX);
  len = read(control, &control_buf, BUF_MAX);
  unsigned int send_num = atoi(control_buf);
  fprintf(stderr, "packet_size=%d\tsend_num=%d\n", packet_size + PACKET_OFFS, send_num);


  int traffic = -1;
  if(t_type == TCP) {
    //測定用のソケットを開ける
    traffic = accept(traffic0, (struct sockaddr *) &client, &len);
    if(traffic < 0) {
      perror("cannot create traffic connection");
      exit(1);
    }
  } else {
    //1パケット捕まえて、dstをそこに設定する（client側で初めに1つパケットを送っちゃう）
    struct sockaddr_in dst;
    socklen_t dst_len;
    int err = recvfrom(traffic0, control_buf, BUF_MAX, 0, (struct sockaddr *) &dst, &dst_len);
    if(err < 0) {
      fprintf(stderr, "UDP_CONNECTION FAILED\n");
      exit(1);
    }
    traffic = socket(AF_INET, SOCK_DGRAM, 0);
    if(traffic < 0) {
      fprintf(stderr, "CANNOT CREATE SOCKET\n");
      debug("HERE");
      exit(1);
    }
    int yes = 1;
    setsockopt(traffic,
               SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    struct sockaddr_in src;
    inet_pton(AF_INET, "0.0.0.0", &src.sin_addr);
    src.sin_port = htons(TRAFFIC_PORT);
    bind(traffic, (struct sockaddr *) &src, sizeof(src));
    connect(traffic, (struct sockaddr *) &dst, dst_len);

  }
  //threadを作ってそっちで受信する
  pthread_t mythread;
  len = read(control, &control_buf, 5);
  debug("%s", control_buf);
  debug("Now create pthread");
  struct traffic_arg arg = {traffic, packet_size, send_num, t_type};
  if(pthread_create(&mythread, NULL, traffic_func, (void *) &arg)) {
    fprintf(stderr, "cannot create pthread\n");
    exit(1);
  }
  len = read(control, &control_buf, BUF_MAX);
  if(t_type == TCP)
    pthread_join(mythread, NULL);
  //else
  //pthread_cansel(mythread);
  close(traffic);

  double interval = calc_interval(server_start_time, server_end_time);
  //UDPの場合は時間が測れないので-1.0にする
  if(t_type == UDP) {
    interval = -1.0;
    fprintf(stderr, "bytes=%llu", server_total_bytes);
  } else {
    fprintf(stderr,"time=%lf sec\n", interval);
  }
  sprintf(control_buf,"%lf %llu", interval, server_total_bytes);
  write(control, control_buf, BUF_MAX);
  close(control);
}

int main(int argc, char* argv[]) {
  if(argc < 2) {
    fprintf(stderr, "Usage: %s [dst_ip] [packet_size] [send_num] [tcp|udp]\n", argv[0]);
    fprintf(stderr, "Usage: %s -l [(tcp|udp)]\n", argv[0]);
    exit(1);
  }
 if(strcmp(argv[1], "-l") == 0) {
   int sock = socket(AF_INET, SOCK_STREAM, 0);
   enum transport_type t_type = TCP;
   if(argc >= 3) {
     if(strcmp(argv[2],"udp") == 0) t_type = UDP;
   }

   //controlは必ずtcp
   struct sockaddr_in addr;
   int yes = 1;
   addr.sin_family = AF_INET;
   addr.sin_port = htons(CONTROL_PORT);
   addr.sin_addr.s_addr = INADDR_ANY;
   setsockopt(sock,
              SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
   bind(sock, (struct sockaddr *) &addr, sizeof(addr));
   listen(sock, 5);

   //UDP|TCPのlistenする奴を作る
   int traffic0 = -1;
   int type = t_type == TCP ? SOCK_STREAM : SOCK_DGRAM;
   traffic0 = socket(AF_INET, type, 0);
   setsockopt(traffic0,
              SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
   addr.sin_port = htons(TRAFFIC_PORT);
   bind(traffic0, (struct sockaddr *) &addr, sizeof(addr));
   if(t_type == TCP)
     listen(traffic0, 5);
   if(traffic0 < 0) {
     perror("cannot create socket:");
     exit(1);
   }

   server_mode(sock, traffic0, t_type);

   close(sock);
   close(traffic0);
 } else if(argc > 4){
   int packet_size = atoi(argv[2]) - PACKET_OFFS;
   int send_num = atoi(argv[3]);

   if(packet_size < 0 || packet_size > 1500) {
     fprintf(stderr, "Invalid packet size:%d\n", packet_size);
     exit(1);
   }
   if(send_num < 0) {
     fprintf(stderr, "Invalid send_num:%d\n",send_num);
     exit(1);
   }
   enum transport_type t_type = TCP;
   if(strcmp(argv[4], "udp") == 0) {
     t_type = UDP;
     debug("UDP_MODE");
   }
   client_mode(argv[1], packet_size, send_num, t_type);
  }

  return 0;
}
