#define KBUILD_MODNAME "BPF_GRE"

#include <linux/bpf.h>
//intN_tを使うため
//#include <stdint.h>

#include <linux/ip.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/if_ether.h>
#include <net/gre.h>

#define bpf_htons(a) __builtin_bswap16(a)
#define bpf_ntohs(a) __builtin_bswap16(a)
#define bpf_htonl(a) __builtin_bswap32(a)


#include "bpf_helpers.h"
//linuxのtools/selftest/bpf/bpf_helpers.hを参考にした
#define printk(fmt, ...) \
  ({ \
  char ____fmt[] = fmt; \
  bpf_trace_printk(____fmt, sizeof(____fmt), \
                   ##__VA_ARGS__); \
})



#define ERROR_ACTION BPF_DROP


SEC("nop")
int do_nop(struct __sk_buff *skb)
{
	return BPF_OK;
}


static inline int __gre(struct __sk_buff *skb)
{
  int ret;
  struct gre_base_hdr grehdr = {};
  ret = bpf_skb_change_head(skb, sizeof(grehdr), 0);
  if(ret < 0) {
    printk("skb_change_head() failed: %d \n", ret);
    return ERROR_ACTION;
  }
  grehdr.flags = 0;
  grehdr.protocol = bpf_htons(ETH_P_IP);

  ret = bpf_skb_store_bytes(skb, 0, &grehdr, sizeof(grehdr), 0);
  if(ret < 0) {
    printk("skb_store_bytes() failed: %d", ret);
    return ERROR_ACTION;
  }

  return BPF_OK;

}


#define SADDR 0xc0a85133
#define DADDR 0xc0a87233

static inline int get_tot_len_from_ipv4(struct __sk_buff *skb)
{
  void *data = (void*)(long)skb->data;
  void *data_end = (void*)(long)skb->data_end;
  struct iphdr *iph = data;
  if(data + sizeof(*iph) > data_end) {
    return -1;
  }
  return ntohs(iph->tot_len);
}

static inline int __ipv4(struct __sk_buff *skb, int pkt_len)
{
  int ret;
  struct iphdr iph = {};
  ret = bpf_skb_change_head(skb, sizeof(iph), 0);
  if(ret < 0) {
    printk("bpf_skb_change_head() failed: %d\n", ret);
    return ERROR_ACTION;
  }
  iph.ihl = 5;
  iph.version = 4;
  iph.id = 0xabcd;
  iph.ttl = 64;
  iph.tot_len = htons(20 + pkt_len);
  iph.protocol = 47; //GRE
  iph.saddr = htonl(SADDR);
  iph.daddr = htonl(DADDR);
  iph.check = 0;

  uint32_t csum = 0;
  uint16_t* ptr = (uint16_t *) &iph;

#pragma clang loop unroll(full)
  for(int i = 0; i < sizeof(iph) >> 1; i++){
    csum += *ptr++;
  }
  iph.check = ~((csum & 0xffff) + (csum >> 16));



  ret = bpf_skb_store_bytes(skb, 0, &iph, sizeof(iph), 0);
  if(ret < 0) {
    printk("skb_store_bytes() failed: %d\n", ret);
    return ERROR_ACTION;
  }
  return BPF_OK;
}


SEC("ipip")
int do_ipip(struct __sk_buff * skb)
{
  int pkt_len = get_tot_len_from_ipv4(skb);
  if (pkt_len == -1) {
    printk("cannot read packet length from ipv4\n");
    return ERROR_ACTION;
  }
  printk("ipip encapping\n");
  return __ipv4(skb, pkt_len);

}

SEC("greip")

int do_gre_ip_encap(struct __sk_buff *skb)
{
  int pkt_len = get_tot_len_from_ipv4(skb);
  if (pkt_len == -1) {
    printk("cannot read packet length from ipv4\n");
    return ERROR_ACTION;
  }
  printk("pre encapping skb:%x\n", (int) skb->data);
  int ret = __gre(skb);
  if(ret != BPF_OK){
    printk("Aborted before ip encapping\n");
    return ERROR_ACTION;
  }
  printk("finish gre encap skb:%x\n", (int) skb->data);
  return __ipv4(skb, pkt_len + sizeof(struct gre_base_hdr));
}

char _license[] SEC("license") = "GPL";
