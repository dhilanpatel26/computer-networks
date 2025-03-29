/**********************************************************************
 * file:  sr_router.c
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

 #include <stdio.h>
 #include <assert.h>
 #include <string.h>
 #include <unistd.h>
 #include <stdlib.h>
 
 #include "sr_if.h"
 #include "sr_rt.h"
 #include "sr_router.h"
 #include "sr_protocol.h"
 #include "sr_arpcache.h"
 #include "sr_utils.h"
 
 /*---------------------------------------------------------------------
  * Method: sr_init(void)
  * Scope:  Global
  *
  * Initialize the routing subsystem
  *
  *---------------------------------------------------------------------*/
 
 void sr_init(struct sr_instance* sr)
 {
     /* REQUIRES */
     assert(sr);
 
     /* Initialize cache and cache cleanup thread */
     sr_arpcache_init(&(sr->cache));
 
     pthread_attr_init(&(sr->attr));
     pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
     pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
     pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
     pthread_t thread;
 
     pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
 
     /* Add initialization code here! */
 
 } /* -- sr_init -- */
 
 /*---------------------------------------------------------------------
  * Method: sr_handlepacket(uint8_t* p,char* interface)
  * Scope:  Global
  *
  * This method is called each time the router receives a packet on the
  * interface.  The packet buffer, the packet length and the receiving
  * interface are passed in as parameters. The packet is complete with
  * ethernet headers.
  *
  * Note: Both the packet buffer and the character's memory are handled
  * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
  * packet instead if you intend to keep it around beyond the scope of
  * the method call.
  *
  *---------------------------------------------------------------------*/
 
 void sr_handlepacket(struct sr_instance* sr,
         uint8_t * packet/* lent */,
         unsigned int len,
         char* interface/* lent */)
 {
   /* REQUIRES */
   assert(sr);
   assert(packet);
   assert(interface);
 
  //  printf("*** -> Received packet of length %d \n",len);
  
   if (ethertype(packet) == ethertype_arp) {
     printf("ARP packet\n");
     sr_handle_arp_packet(sr, packet, len, interface);
   } else if (ethertype(packet) == ethertype_ip) {
     printf("IP packet\n");
     sr_handle_ip_packet(sr, packet, len, interface);
   } else {
    //  printf("Unknown packet\n");
   }
 
 
 } /* end sr_handlepacket */
 
 
 /* Add any additional helper methods here & don't forget to also declare
 them in sr_router.h.
 
 If you use any of these methods in sr_arpcache.c, you must also forward declare
 them in sr_arpcache.h to avoid circular dependencies. Since sr_router
 already imports sr_arpcache.h, sr_arpcache cannot import sr_router.h -KM */
 
 void sr_handle_ip_packet(struct sr_instance* sr,
   uint8_t* packet/* lent */,
   unsigned int len,
   char* interface/* lent */) {
 
  if (len < sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr)) {
    return;
  }

  struct sr_ip_hdr* ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
  uint16_t old_sum = ip_hdr->ip_sum;
  ip_hdr->ip_sum = 0;
  uint16_t calculated_sum = cksum(ip_hdr, sizeof(struct sr_ip_hdr));
  ip_hdr->ip_sum = old_sum;
  if (old_sum != calculated_sum) {
    return;
  }

  int for_us = 0;
  struct sr_if* iface = sr->if_list;
  while (iface) { // search through interfaces list
    if (iface->ip == ip_hdr->ip_dst) {
        for_us = 1;
        break;
    }
    iface = iface->next;
  }

  if (for_us) {
    if (ip_hdr->ip_p == ip_protocol_icmp) {
        // printf("ICMP packet for us\n");
        sr_handle_icmp_packet(sr, packet, len, interface);
    } else {
        // printf("TCP/UDP packet for us - generating port unreachable\n");
        sr_send_icmp_port_unreachable(sr, packet, interface);
    }
  } else {
    // printf("Forwarding IP packet\n");
    sr_forward_packet(sr, packet, len, interface);
  }
}

void sr_handle_icmp_packet(struct sr_instance* sr,
  uint8_t* packet/* lent */,
  unsigned int len,
  char* interface/* lent */) {

 struct sr_icmp_hdr *icmp_hdr = (struct sr_icmp_hdr *)(packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

 uint8_t *new_packet = (uint8_t *)malloc(len);
 memcpy(new_packet, packet, len);

 if (icmp_hdr->icmp_type == 8) {
  //  printf("ICMP echo request\n");
   sr_handle_icmp_echo_request(sr, packet, new_packet, len, interface);
   
   struct sr_ip_hdr *ip_hdr = (struct sr_ip_hdr *)(packet + sizeof(struct sr_ethernet_hdr));
   struct sr_rt *rt = sr_get_longest_prefix_match(sr, ip_hdr->ip_src);
   
   if (!rt) {
    //  printf("No route to host for ICMP echo reply\n");
     free(new_packet);
     return;
   }
   
   struct sr_if *out_iface = sr_get_interface(sr, rt->interface);
   if (!out_iface) {
    //  printf("Interface not found\n");
     free(new_packet);
     return;
   }
   
   struct sr_arpentry* arp_entry = sr_arpcache_lookup(&(sr->cache), rt->gw.s_addr);
   
   if (arp_entry) {
     struct sr_ethernet_hdr *eth_hdr = (struct sr_ethernet_hdr *)new_packet;
     memcpy(eth_hdr->ether_shost, out_iface->addr, ETHER_ADDR_LEN);
     memcpy(eth_hdr->ether_dhost, arp_entry->mac, ETHER_ADDR_LEN);
     
     sr_send_packet(sr, new_packet, len, out_iface->name);
     free(arp_entry);
     free(new_packet);
   } else {
    //  printf("ARP cache miss for ICMP reply, queueing packet\n");
     struct sr_arpreq *req = sr_arpcache_queuereq(
         &(sr->cache), rt->gw.s_addr, new_packet, len, out_iface->name);
     handle_arpreq(sr, req);
     // don't free new_packet here - it's now owned by the queue
   }
 } else {
  //  printf("Unknown ICMP packet\n");
   free(new_packet);
 }
}
 
 void sr_handle_icmp_echo_request(struct sr_instance* sr,
   uint8_t* packet/* lent */,
   uint8_t* new_packet/* owned by caller */,
   unsigned int len,
   char* interface/* lent */) {
 
  struct sr_ethernet_hdr *ethernet_hdr = (struct sr_ethernet_hdr *)packet;
  struct sr_ip_hdr *ip_hdr = (struct sr_ip_hdr *)(packet + sizeof(struct sr_ethernet_hdr));

  struct sr_ethernet_hdr *new_ethernet_hdr = (struct sr_ethernet_hdr *)new_packet;
  struct sr_ip_hdr *new_ip_hdr = (struct sr_ip_hdr *)(new_packet + sizeof(struct sr_ethernet_hdr));
  struct sr_icmp_hdr *new_icmp_hdr = (struct sr_icmp_hdr *)(new_packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

  new_ethernet_hdr->ether_type = ethernet_hdr->ether_type;

  new_ip_hdr->ip_hl = ip_hdr->ip_hl;
  new_ip_hdr->ip_v = ip_hdr->ip_v;
  new_ip_hdr->ip_tos = ip_hdr->ip_tos;
  new_ip_hdr->ip_len = ip_hdr->ip_len;
  new_ip_hdr->ip_id = ip_hdr->ip_id;
  new_ip_hdr->ip_off = ip_hdr->ip_off;
  new_ip_hdr->ip_ttl = 64; // reset TTL
  new_ip_hdr->ip_p = ip_protocol_icmp;
  new_ip_hdr->ip_dst = ip_hdr->ip_src;
  new_ip_hdr->ip_src = ip_hdr->ip_dst;
  new_ip_hdr->ip_sum = 0;
  new_ip_hdr->ip_sum = cksum(new_ip_hdr, sizeof(struct sr_ip_hdr));

  new_icmp_hdr->icmp_type = 0;  // Echo Reply
  new_icmp_hdr->icmp_code = 0;
  new_icmp_hdr->icmp_sum = 0;
  new_icmp_hdr->icmp_sum = cksum(new_icmp_hdr, len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr));
  
}

void sr_forward_packet(struct sr_instance* sr,
  uint8_t* packet/* lent */,
  unsigned int len,
  char* interface/* lent */) {

  struct sr_ethernet_hdr *eth_hdr = (struct sr_ethernet_hdr *)packet;
  struct sr_ip_hdr *ip_hdr = (struct sr_ip_hdr *)(packet + sizeof(struct sr_ethernet_hdr));

  ip_hdr->ip_ttl--;

  if (ip_hdr->ip_ttl <= 0) {
    // printf("TTL expired, sending ICMP time exceeded\n");
    sr_send_icmp_time_exceeded(sr, packet, interface);
    return;
  }

  ip_hdr->ip_sum = 0;
  ip_hdr->ip_sum = cksum(ip_hdr, sizeof(struct sr_ip_hdr));

  struct sr_rt *rt = sr_get_longest_prefix_match(sr, ip_hdr->ip_dst);
  if (!rt) {
    // printf("Destination net unreachable\n");
    sr_send_icmp_net_unreachable(sr, packet, interface);
    return;
  }

  struct sr_if *out_iface = sr_get_interface(sr, rt->interface);
  if (!out_iface) {
    // printf("Interface not found\n");
    return;
  }

  struct sr_arpentry* arp_entry = sr_arpcache_lookup(&(sr->cache), rt->gw.s_addr);

  if (arp_entry) {
    memcpy(eth_hdr->ether_shost, out_iface->addr, ETHER_ADDR_LEN);
    memcpy(eth_hdr->ether_dhost, arp_entry->mac, ETHER_ADDR_LEN);
    sr_send_packet(sr, packet, len, out_iface->name);
    free(arp_entry);
  } else {
    // printf("ARP cache miss, queueing packet and sending ARP request\n");
    struct sr_arpreq *req = sr_arpcache_queuereq(&(sr->cache), rt->gw.s_addr, packet, len, out_iface->name);
    handle_arpreq(sr, req);
  }
}

void sr_send_arp_request(struct sr_instance* sr, uint32_t tip, struct sr_if* iface) {
  // Create and send ARP request packet
  unsigned int len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_arp_hdr);
  uint8_t* arp_packet = (uint8_t*)malloc(len);
  
  struct sr_ethernet_hdr* eth_hdr = (struct sr_ethernet_hdr*)arp_packet;
  memset(eth_hdr->ether_dhost, 0xff, ETHER_ADDR_LEN); // Broadcast
  memcpy(eth_hdr->ether_shost, iface->addr, ETHER_ADDR_LEN);
  eth_hdr->ether_type = htons(ethertype_arp);
  
  struct sr_arp_hdr* arp_hdr = (struct sr_arp_hdr*)(arp_packet + sizeof(struct sr_ethernet_hdr));
  arp_hdr->ar_hrd = htons(arp_hrd_ethernet);
  arp_hdr->ar_pro = htons(ethertype_ip);
  arp_hdr->ar_hln = ETHER_ADDR_LEN;
  arp_hdr->ar_pln = 4; // IPv4 length
  arp_hdr->ar_op = htons(arp_op_request);
  memcpy(arp_hdr->ar_sha, iface->addr, ETHER_ADDR_LEN);
  arp_hdr->ar_sip = iface->ip;
  memset(arp_hdr->ar_tha, 0, ETHER_ADDR_LEN); // Target MAC unknown
  arp_hdr->ar_tip = tip; // Target IP
  
  sr_send_packet(sr, arp_packet, len, iface->name);
  free(arp_packet);
}

void sr_send_arp_reply(struct sr_instance* sr, uint8_t* req_packet, unsigned int len, char* interface) {
  struct sr_ethernet_hdr* req_eth_hdr = (struct sr_ethernet_hdr*)req_packet;
  struct sr_arp_hdr* req_arp_hdr = (struct sr_arp_hdr*)(req_packet + sizeof(struct sr_ethernet_hdr));
  struct sr_if* iface = sr_get_interface(sr, interface);
  
  uint8_t* reply_packet = (uint8_t*)malloc(len);
  
  struct sr_ethernet_hdr* reply_eth_hdr = (struct sr_ethernet_hdr*)reply_packet;
  memcpy(reply_eth_hdr->ether_dhost, req_eth_hdr->ether_shost, ETHER_ADDR_LEN);
  memcpy(reply_eth_hdr->ether_shost, iface->addr, ETHER_ADDR_LEN);
  reply_eth_hdr->ether_type = htons(ethertype_arp);
  
  struct sr_arp_hdr* reply_arp_hdr = (struct sr_arp_hdr*)(reply_packet + sizeof(struct sr_ethernet_hdr));
  reply_arp_hdr->ar_hrd = htons(arp_hrd_ethernet);
  reply_arp_hdr->ar_pro = htons(ethertype_ip);
  reply_arp_hdr->ar_hln = ETHER_ADDR_LEN;
  reply_arp_hdr->ar_pln = 4; // IPv4 length
  reply_arp_hdr->ar_op = htons(arp_op_reply);
  memcpy(reply_arp_hdr->ar_sha, iface->addr, ETHER_ADDR_LEN); // My MAC
  reply_arp_hdr->ar_sip = iface->ip; // My IP
  memcpy(reply_arp_hdr->ar_tha, req_arp_hdr->ar_sha, ETHER_ADDR_LEN); // Target MAC
  reply_arp_hdr->ar_tip = req_arp_hdr->ar_sip; // Target IP
  
  sr_send_packet(sr, reply_packet, len, interface);
  free(reply_packet);
}

void sr_handle_arp_packet(struct sr_instance* sr,
  uint8_t* packet/* lent */,
  unsigned int len,
  char* interface/* lent */) {

  if (len < sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_arp_hdr)) {
    // printf("ARP packet too short, ignoring\n");
    return;
  }

  struct sr_arp_hdr* arp_hdr = (struct sr_arp_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
  struct sr_if* iface = sr_get_interface(sr, interface);

  if (!iface) {
    // printf("Interface not found\n");
    return;
  }

  if (ntohs(arp_hdr->ar_op) == arp_op_request) {
    if (arp_hdr->ar_tip == iface->ip) {
        // printf("ARP request for our IP, sending reply\n");
        sr_send_arp_reply(sr, packet, len, interface);
    } else {
        // printf("ARP request not for us, ignoring\n");
    }
  }
  else if (ntohs(arp_hdr->ar_op) == arp_op_reply) {
    if (arp_hdr->ar_tip == iface->ip) {
        // printf("ARP reply received for our request\n");
        
        struct sr_arpreq* req = sr_arpcache_insert(&(sr->cache), 
                                                arp_hdr->ar_sha, 
                                                arp_hdr->ar_sip);
        
        if (req) {
            struct sr_packet* pkt = req->packets;
            while (pkt) {
                struct sr_ethernet_hdr* eth_hdr = (struct sr_ethernet_hdr*)(pkt->buf);
                struct sr_if* out_iface = sr_get_interface(sr, pkt->iface);
                
                memcpy(eth_hdr->ether_dhost, arp_hdr->ar_sha, ETHER_ADDR_LEN);
                memcpy(eth_hdr->ether_shost, out_iface->addr, ETHER_ADDR_LEN);
                
                sr_send_packet(sr, pkt->buf, pkt->len, pkt->iface);
                pkt = pkt->next;
            }
            
            sr_arpreq_destroy(&(sr->cache), req);
        }
    } else {
        // printf("ARP reply not for us, ignoring\n");
    }
  } else {
    // printf("Unknown ARP operation\n");
  }
}

struct sr_rt *sr_get_longest_prefix_match(struct sr_instance *sr, uint32_t ip) {
  struct sr_rt *rt_walker = sr->routing_table;
  struct sr_rt *longest_match = NULL;
  uint32_t longest_mask = 0; // mask counter

  while (rt_walker) {
    uint32_t mask = rt_walker->mask.s_addr;
    uint32_t dest = rt_walker->dest.s_addr;
    
    if ((ip & mask) == (dest & mask)) {
      if (mask >= longest_mask) {
        longest_match = rt_walker;
        longest_mask = mask;
      }
    }
    rt_walker = rt_walker->next;
  }

  return longest_match;
}

void sr_send_icmp_port_unreachable(struct sr_instance* sr,
  uint8_t* packet/* lent */,
  char* interface/* lent */) {
    sr_send_error(sr, packet, interface, 3, 3);
}

void sr_send_icmp_time_exceeded(struct sr_instance* sr,
  uint8_t* packet/* lent */,
  char* interface/* lent */) {
    sr_send_error(sr, packet, interface, 11, 0);
}

void sr_send_icmp_host_unreachable(struct sr_instance* sr,
  uint8_t* packet/* lent */,
  char* interface/* lent */) {
    sr_send_error(sr, packet, interface, 3, 1);
}

void sr_send_icmp_net_unreachable(struct sr_instance* sr,
  uint8_t* packet/* lent */,
  char* interface/* lent */) {
    sr_send_error(sr, packet, interface, 3, 0);
}

void sr_send_error(struct sr_instance* sr,
  uint8_t* packet/* lent */,
  char* interface/* lent */,
  uint8_t type,
  uint8_t code
) {
  
  struct sr_ip_hdr *orig_ip_hdr = (struct sr_ip_hdr *)(packet + sizeof(struct sr_ethernet_hdr));
  
  unsigned int icmp_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) + 
                        sizeof(struct sr_icmp_t3_hdr);
  uint8_t *icmp_packet = (uint8_t *)malloc(icmp_len);
  
  struct sr_ethernet_hdr *eth_hdr = (struct sr_ethernet_hdr *)icmp_packet;
  struct sr_ip_hdr *ip_hdr = (struct sr_ip_hdr *)(icmp_packet + sizeof(struct sr_ethernet_hdr));
  struct sr_icmp_t3_hdr *icmp_hdr = (struct sr_icmp_t3_hdr *)(icmp_packet + sizeof(struct sr_ethernet_hdr) + 
                                  sizeof(struct sr_ip_hdr));
  
  struct sr_if* iface = sr_get_interface(sr, interface);
  
  struct sr_rt *rt = sr_get_longest_prefix_match(sr, orig_ip_hdr->ip_src);
  if (!rt) {
    // printf("No route to host for ICMP error message\n");
    free(icmp_packet);
    return;
  }
  struct sr_if *out_iface = sr_get_interface(sr, rt->interface);
  if (!out_iface) {
    // printf("Interface not found\n");
    free(icmp_packet);
    return;
  }
  
  ip_hdr->ip_hl = 5;
  ip_hdr->ip_v = 4;
  ip_hdr->ip_tos = 0;
  ip_hdr->ip_len = htons(sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t3_hdr));
  ip_hdr->ip_id = htons(0);
  ip_hdr->ip_off = htons(IP_DF); // Don't fragment
  ip_hdr->ip_ttl = 64;
  ip_hdr->ip_p = ip_protocol_icmp;
  ip_hdr->ip_src = iface->ip;  // Source is our interface
  ip_hdr->ip_dst = orig_ip_hdr->ip_src;  // Destination is original sender
  ip_hdr->ip_sum = 0;
  ip_hdr->ip_sum = cksum(ip_hdr, sizeof(struct sr_ip_hdr));
  
  icmp_hdr->icmp_type = type;
  icmp_hdr->icmp_code = code;
  icmp_hdr->unused = 0;
  icmp_hdr->next_mtu = 0;
  
  memcpy(icmp_hdr->data, orig_ip_hdr, ICMP_DATA_SIZE);
  
  icmp_hdr->icmp_sum = 0;
  icmp_hdr->icmp_sum = cksum(icmp_hdr, sizeof(struct sr_icmp_t3_hdr));
  
  struct sr_arpentry *arp_entry = sr_arpcache_lookup(&sr->cache, rt->gw.s_addr);
  if (arp_entry) {
    memcpy(eth_hdr->ether_dhost, arp_entry->mac, ETHER_ADDR_LEN);
    memcpy(eth_hdr->ether_shost, out_iface->addr, ETHER_ADDR_LEN);
    eth_hdr->ether_type = htons(ethertype_ip);
    
    sr_send_packet(sr, icmp_packet, icmp_len, out_iface->name);
    free(arp_entry);
    free(icmp_packet);
  } else {
    eth_hdr->ether_type = htons(ethertype_ip);
    memcpy(eth_hdr->ether_shost, out_iface->addr, ETHER_ADDR_LEN);
    
    struct sr_arpreq *req = sr_arpcache_queuereq(&sr->cache, rt->gw.s_addr, 
                                               icmp_packet, icmp_len, out_iface->name);
    handle_arpreq(sr, req);
    // don't free icmp_packet here - it's now owned by the queue
  }
}