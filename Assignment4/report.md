## Question 1.1

![All flows CDF](1.1/Figure__.png)
![TCP flows CDF](1.1/Figure_1.png)
![UDP flows CDF](1.1/Figure_2.png)

### Observations
1. We see a heavy-tailed distribution, where around 50% of all flows are under 300 B and 90% are under 1000 B, with only a few flows being larger than 10 kB. We can conclude that most flows are quite small.
2. There are many more small-sized TCP flows than UDP flows, likely from the handshake and ACK packets without payloads in TCP.

## Question 1.2
### Top 10 Source IPs by flows:
1. 116.211: 17019 flows
2. 169.54: 9424 flows
3. 222.186: 5269 flows
4. 163.53: 2981 flows
5. 169.45: 2494 flows
6. 94.23: 2205 flows
7. 141.212: 2143 flows
8. 212.83: 2042 flows
9. 64.125: 1852 flows
10. 184.105: 1775 flows

Percentage of flows from top 10 source IPs: 44.80%

### Top 10 Source IPs by bytes:
1. 212.83: 928311 bytes
2. 169.54: 867928 bytes
3. 116.211: 680922 bytes
4. 140.205: 510833 bytes
5. 128.112: 506604 bytes
6. 42.120: 326122 bytes
7. 169.45: 229448 bytes
8. 222.186: 211068 bytes
9. 5.8: 126940 bytes
10. 163.53: 120920 bytes

Percentage of bytes from top 10 source IPs: 37.41%

## Question 1.3
Port 443: HTTPS
Percentage of flows from port 443: 1.10%
Percentage of flows to port 443: 4.40%

## Question 1.4
Percentage of total bytes sent by 128.112: 4.20%
Percentage of total bytes received by 128.112: 95.95%
Percentage of bytes both sent and received by 128.112: 0.84%

We see an extreme imbalance in byte-counts on this router's network, specifically almost all bytes received being destined to 128.112.0.0/16, but very few being sent from it, and almost no fully internal bytes. This router is acting as a net sink of traffic, perhaps for one of the research universities in Internet2, where end hosts primarily download or request content from out-of-network servers and rarely upload.

## Question 1.5
I imagine that most of the work people do at a busy public cafe during the afternoon consists of web-based downloads and uploads, as well as video and music streaming. I would expect the following changes to the collected data:
1. The flow CDF shifting to the right, as video and music streaming requires larger multimedia flows and larger/more packets.
2. Assuming the cafe has a NAT, we would likely see most of the top 10 source IPs for traffic to be under the same /16 prefix.
3. HTTPS to dominate the destination port traffic in a much more significant way than it does currently, because typical browser traffic is through HTTPS.
4. I would still expect the inbound/outbound traffic split to lean more inbound-heavy, but it will likely be more balanced if cafe users are uploading. We might also see some more internal traffic (src and dst within the /16 network) if the cafe is streaming to a smart-speaker music system or using a printer.

## Question 2.1
Top 10 AS numbers by frequency:
1. AS3356: 95483 occurrences
2. AS3257: 75208 occurrences
3. AS1299: 64771 occurrences
4. AS6939: 56413 occurrences
5. AS2914: 54290 occurrences
6. AS37100: 45399 occurrences
7. AS174: 45319 occurrences
8. AS49788: 41468 occurrences
9. AS3130: 38180 occurrences
10. AS3303: 36486 occurrences

Percentage of top 10 AS numbers in all paths: 78.79%

ASNumber:       3356
ASName:         LEVEL3
ASHandle:       AS3356
RegDate:        2000-03-10
Updated:        2018-02-20    
Ref:            https://rdap.arin.net/registry/autnum/3356

OrgName:        Level 3 Parent, LLC
OrgId:          LPL-141
Address:        100 CenturyLink Drive
City:           Monroe
StateProv:      LA
PostalCode:     71203
Country:        US
RegDate:        2018-02-06
Updated:        2024-06-17

???

## Question 2.2
![BGP Path Length CDF](2.2/Figure__.png)

This tells us that most paths across the internet are short (between 3 and 6 hops). We can infer that the internet's AS-level topology is relatively flat since the destination can be reached so efficiently. There are rare occurances of greater than 10 AS hops. 

## Question 2.3
Average updates per minute: 10391.19

![Updates per Second](2.3/Figure_1.png)

## Question 2.4

![CDF of Message From Top AS Percentage](2.4/Figure_2.png)

The steep rise at the beginning of the CDF graph indicates that a small top percentage of ASes are responsible for a large portion of the BGP updates. The curve flattens out as it approaches 100%, meaning that the remaining ASes send updates much less frequently. Those select few highly active ASes are likely less stable (perhaps due to frequent route changes or network reliability issues), while the remaining majority that infrequently update are part of a more stable network.