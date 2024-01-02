# Stratum Network Contract Specification

Version: 1.0.0
Status: Frozen

## 1. Overview
The Stratum network stack is an original implementation running entirely within the guest kernel, operating on packets exchanged through the modern virtio-net PCI driver.

## 2. Protocol Boundaries and Supported Capabilities
1. **Link Layer**:
   - Ethernet II framing (14-byte header: destination MAC, source MAC, EtherType).
   - Maximum Transmission Unit (MTU): 1500 bytes payload (1514 bytes on wire).
   - Hardware address resolution via original ARP engine (RFC 826). Cache with 20-minute eviction timeout.
2. **Network Layer (IPv4)**:
   - IPv4 header validation (checksum, version 4, minimum IHL=5, total length).
   - Local address routing and link-local subnet delivery.
   - Fragmentation Policy: Incoming IPv4 packets with MF=1 (More Fragments) or Fragment Offset > 0 are rejected with explicit drop counters and diagnostic trace. Fragmentation is not supported.
3. **Control Protocol (ICMP)**:
   - ICMPv4 Echo Request (Type 8) and Echo Reply (Type 0) handling with checksum validation.
   - Destination Unreachable (Type 3) generation on closed UDP ports.
4. **Transport Layer (UDP)**:
   - UDP datagram boundary preservation.
   - Socket receive queue limit: maximum 64 datagrams or 256 KiB per socket. When full, incoming datagrams are dropped with explicit queue-full counter increment.
5. **Transport Layer (TCP)**:
   - Full 11-state TCP finite state machine (RFC 9293).
   - Active open (`connect()`) and passive open (`listen()`, `accept()`).
   - Sequence number validation, cumulative acknowledgments, and payload streaming.
   - Retransmission timer management with Jacobson/Karels algorithm (RFC 6298): SRTT, RTTVAR, and exponential RTO backoff (minimum 200 ms, maximum 60 seconds).
   - Congestion control: Slow Start, Congestion Avoidance with Reno-style recovery (RFC 5681).
   - Flow control: dynamic sliding receive window based on available socket buffer space.
   - Half-close (`shutdown(SHUT_WR)`), graceful close (`FIN`), and abortive reset (`RST`).
   - Timers: Retransmission Timer, TIME_WAIT timer (2*MSL = 4 seconds in test profiles), Keepalive Timer.

## 3. Buffer Management (`net_buf_t`)
- Fixed-size packet buffers (2048 bytes) allocated from a bounded kernel slab pool.
- Reference counted for zero-copy handoff between virtio-net descriptor rings and socket queues.
- Buffer pool size: maximum 1024 buffers (2 MiB total footprint) in standard profile.
- Hard resource exhaustion returns `-STRATUM_ENOBUFS` and drops packets without deadlock.

## 4. Socket API Contract
- `sys_socket(AF_INET, SOCK_STREAM, 0)`: Allocate TCP socket endpoint.
- `sys_socket(AF_INET, SOCK_DGRAM, 0)`: Allocate UDP socket endpoint.
- Non-blocking I/O supported via `O_NONBLOCK` flag; returns `-STRATUM_EAGAIN` when queue is empty or transmit buffer is full.
- Readiness events reported via `sys_poll`: `POLLIN` (data available to read), `POLLOUT` (send buffer available), `POLLERR`, `POLLHUP`.
