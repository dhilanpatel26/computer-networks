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
 
   printf("*** -> Received packet of length %d \n",len);
 
   struct sr_ethernet_hdr *ethernet_hdr = (struct sr_ethernet_hdr *)packet;
 
   if (ethertype(packet) == ethertype_arp) {
     printf("ARP packet\n");
     // TODO: implement
     sr_handle_arp_packet(sr, packet, len, interface);
   } else if (ethertype(packet) == ethertype_ip) {
     printf("IP packet\n");
     sr_handle_ip_packet(sr, packet, len, interface);
   } else {
     printf("Unknown packet\n");
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
 
  // Validate packet length
  if (len < sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr)) {
    return;
  }

  // Verify checksum
  struct sr_ip_hdr* ip_hdr = (struct sr_ip_hdr*)(packet + sizeof(struct sr_ethernet_hdr));
  uint16_t old_sum = ip_hdr->ip_sum;
  ip_hdr->ip_sum = 0;
  uint16_t calculated_sum = cksum(ip_hdr, sizeof(struct sr_ip_hdr));
  ip_hdr->ip_sum = old_sum;
  if (old_sum != calculated_sum) {
    return;
  }

  // Is this packet for one of our interfaces?
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
    // Packet is for us - handle based on protocol
    if (ip_hdr->ip_p == ip_protocol_icmp) {
        printf("ICMP packet for us\n");
        sr_handle_icmp_packet(sr, packet, len, interface);
    } else {
        // Generate ICMP port unreachable for TCP/UDP
        printf("TCP/UDP packet for us - generating port unreachable\n");
        sr_send_icmp_port_unreachable(sr, packet, interface);
    }
  } else {
    // Need to forward this packet
    printf("Forwarding IP packet\n");
    sr_forward_packet(sr, packet, len, interface);
  }
}
 
 void sr_handle_icmp_packet(struct sr_instance* sr,
   uint8_t* packet/* lent */,
   unsigned int len,
   char* interface/* lent */) {
 
  struct sr_icmp_hdr *icmp_hdr = (struct sr_icmp_hdr *)(packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

  // Create response packet
  uint8_t *new_packet = (uint8_t *)malloc(len);
  memcpy(new_packet, packet, len);

  if (icmp_hdr->icmp_type == 8) {
    printf("ICMP echo request\n");
    sr_handle_icmp_echo_request(sr, packet, new_packet, len, interface);
  } else {
    printf("Unknown ICMP packet\n");
    free(new_packet);  // Free if not handled
    return;
  }

  // Send packet and free memory
  struct sr_ip_hdr *ip_hdr = (struct sr_ip_hdr *)(packet + sizeof(struct sr_ethernet_hdr));
  struct sr_rt *rt = sr_get_longest_prefix_match(sr, ip_hdr->ip_src);
  if (rt) {
    struct sr_if *out_iface = sr_get_interface(sr, rt->interface);
    sr_send_packet(sr, new_packet, len, out_iface->name);
  }

  free(new_packet);
}
 
 void sr_handle_icmp_echo_request(struct sr_instance* sr,
   uint8_t* packet/* lent */,
   uint8_t* new_packet/* owned by caller */,
   unsigned int len,
   char* interface/* lent */) {
 
  // Modify the headers for echo reply
  struct sr_ethernet_hdr *ethernet_hdr = (struct sr_ethernet_hdr *)packet;
  struct sr_ip_hdr *ip_hdr = (struct sr_ip_hdr *)(packet + sizeof(struct sr_ethernet_hdr));

  struct sr_ethernet_hdr *new_ethernet_hdr = (struct sr_ethernet_hdr *)new_packet;
  struct sr_ip_hdr *new_ip_hdr = (struct sr_ip_hdr *)(new_packet + sizeof(struct sr_ethernet_hdr));
  struct sr_icmp_hdr *new_icmp_hdr = (struct sr_icmp_hdr *)(new_packet + sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr));

  // Set up echo reply
  new_ethernet_hdr->ether_type = ethernet_hdr->ether_type;
  memcpy(new_ethernet_hdr->ether_dhost, ethernet_hdr->ether_shost, ETHER_ADDR_LEN);
  memcpy(new_ethernet_hdr->ether_shost, ethernet_hdr->ether_dhost, ETHER_ADDR_LEN);

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