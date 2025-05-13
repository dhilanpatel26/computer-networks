# README for Assignment 2: Router

Name: Dhilan Patel

JHED: dpatel93

---

## Design Overview

For this assignment, I implemented a basic router that supports IP and ARP packets, a forwarding routing table, and ICMP 
echo and error messages. The implementation follows standard networking practices in the internet protocol suite.

## Files Modified

### 1. sr_router.c
This is where I implemented the core router functionality:
- Packet handling and classification
- IP packet forwarding with TTL handling
- ICMP message generation (echo replies and error types)
- ARP request/reply processing
- Longest prefix match algorithm

### 2. sr_arpcache.c
I implemented two key functions:
- `sr_arpcache_sweepreqs`: Iterates through pending ARP requests
- `handle_arpreq`: Manages the lifecycle of ARP requests including retries and timeouts

## Helper Methods

### Core Packet Handling
- `sr_handle_ip_packet`: Processes incoming IP packets
- `sr_handle_arp_packet`: Processes incoming ARP packets
- `sr_forward_packet`: Handles IP packet forwarding

### ICMP Functionality
- `sr_handle_icmp_packet`: Manages ICMP packets destined for the router
- `sr_handle_icmp_echo_request`: Creates ICMP echo replies
- `sr_send_error`: Common function for generating various ICMP error messages
- Specific ICMP error message functions for:
  - Time exceeded (TTL expired)
  - Destination net unreachable (no route)
  - Destination host unreachable (ARP failure)
  - Port unreachable (TCP/UDP to router)

### ARP Functionality
- `sr_send_arp_request`: Creates and sends ARP requests
- `sr_send_arp_reply`: Creates and sends ARP replies
- `handle_arpreq`: Manages ARP retry logic and timeouts

### Routing Functionality
- `sr_get_longest_prefix_match`: Implements longest prefix match algorithm for routing decisions

## Implementation Logic

### Packet Processing Flow
1. Identify packet type (ARP or IP)
2. For IP packets:
   - Verify checksum and minimum length
   - Determine if the packet is for the router or needs forwarding
   - Handle ICMP echo requests directly
   - Forward other packets with TTL decrement

3. For ARP packets:
   - Handle ARP requests by sending replies when appropriate
   - Process ARP replies by updating the cache and sending queued packets

### Forwarding Logic
- Lookup destination in routing table using longest prefix match
- Decrement TTL and update checksum
- Look up next-hop MAC in ARP cache
- Forward immediately if MAC is known
- Queue packet and send ARP request if MAC is unknown

### ARP Handling
- Cache ARP replies for 15 seconds
- Retry ARP requests once per second
- After 5 attempts, send ICMP host unreachable
- Queue packets waiting for ARP resolution

## Challenges Encountered

1. **Memory Management**: Ensuring packets are properly freed when no longer needed, especially when queueing packets for ARP resolution. I had to be cautious in the ownership of packets. To ensure clarity, I included relevant comments in the function declarations in `sr_router.h`.

3. **Checksum Calculation**: Ensuring proper checksum calculation/verification for both IP and ICMP headers. It was very important to first set all other header and payload fields (including the checksum to 0), and then calculate and set the checksum.

5. **Interface Selection**: Determining the correct interface for sending packets, especially for ICMP error responses. For example, I did not initally realize the that the interface to send echo replies out of is not necessarily the interface that the echo request came in from.

## Special Notes
During setup (Mac M1 Silicon), I had trouble running the solution script `sr_solution_macm` multiple times in sequence because the kill script was not fully terminating all relevant processes. I resolved this by modifying `kill_all.sh` to the following:

```bash
#!/bin/bash

pkill -9 sr_solution
pkill -9 sr_solution_macm
pkill -9 sr
pgrep pox | xargs kill -9
pgrep mininet | xargs kill -9
ps aux | grep topo | awk '{print $2}' | sudo xargs kill -9
ps aux | grep webserver | awk '{print $2}' | sudo xargs kill -9

sudo pkill -f pox.py
sudo pkill -f mininet
sudo mn -c
sudo killall controller
sudo killall srhandler
```

Regardless, my router code functions the same as the solution and meets all requirements.