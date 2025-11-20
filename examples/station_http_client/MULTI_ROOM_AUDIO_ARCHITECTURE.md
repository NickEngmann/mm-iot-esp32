# Multi-Room Synchronized Audio over Wi-Fi HaLow
## Architecture Design Document

**Date:** November 19, 2025
**Platform:** ESP32-S3 + Morse Micro MM6108A1 (Wi-Fi HaLow)
**Use Case:** Synchronized multi-room audio playback with LED control

---

## Table of Contents
1. [System Overview](#system-overview)
2. [Architecture](#architecture)
3. [Protocol Analysis](#protocol-analysis)
4. [Technical Feasibility](#technical-feasibility)
5. [Synchronization Strategy](#synchronization-strategy)
6. [Audio Format Recommendations](#audio-format-recommendations)
7. [Implementation Roadmap](#implementation-roadmap)
8. [Library Recommendations](#library-recommendations)
9. [Challenges & Solutions](#challenges--solutions)

---

## System Overview

### Goal
Stream synchronized audio (MP3/WAV) from a central HaLow AP server to multiple ESP32-S3 nodes, with each node playing audio through an external DAC + amplifier (LQ-AMP10W) with ±100-200ms synchronization accuracy.

### Hardware Stack
```
┌─────────────────────────────────────────┐
│ ESP32-S3 (Seeed Xiao HaLow)             │
│ ├─ Morse Micro MM6108A1 (HaLow Radio)   │
│ ├─ GPIO 6, 10, 44 (I2S to DAC)          │
│ ├─ GPIO 21 (Onboard LED)                │
│ └─ PCM5102 I2S DAC (External)           │
└─────────────────────────────────────────┘
         ↓ Analog L/R
┌─────────────────────────────────────────┐
│ LQ-AMP10W Audio Amplifier               │
└─────────────────────────────────────────┘
         ↓
    Speakers 🔊
```

---

## Architecture

### Network Topology
```
┌──────────────────────────────────────────┐
│   HaLow AP + Media Server                │
│   - Audio source (MP3/WAV files)         │
│   - RTP Multicast Sender                 │
│   - HTTP Control Server                  │
│   - NTP Time Server                      │
└──────────────────────────────────────────┘
            ↓ (Multicast 239.255.x.x:5004)
    ┌───────┴────────┬────────────┐
    ↓                ↓            ↓
┌─────────┐    ┌─────────┐   ┌─────────┐
│ Node 1  │    │ Node 2  │   │ Node 3  │
│ ESP32-S3│    │ ESP32-S3│   │ ESP32-S3│
│ + DAC   │    │ + DAC   │   │ + DAC   │
└─────────┘    └─────────┘   └─────────┘
    ↓              ↓             ↓
  Speakers      Speakers      Speakers
```

### Data Flow
```
Server:  File → Encode → RTP Packetize → Multicast (239.255.x.x)
                                              ↓
Nodes:   Multicast RX → Jitter Buffer → [MP3 Decode] → I2S DMA → DAC → Audio
```

---

## Protocol Analysis

### Option 1: RTP Multicast ✅ **RECOMMENDED**

**Pros:**
- ✅ Lightweight UDP-based protocol (12-byte header)
- ✅ One stream → multiple receivers (bandwidth efficient)
- ✅ Built-in timestamp synchronization
- ✅ Low overhead, low latency
- ✅ Perfect for real-time audio distribution
- ✅ Industry standard (RFC 3550)

**Cons:**
- ⚠️ No built-in reliability (UDP = packet loss possible)
- ⚠️ Application must handle synchronization logic
- ⚠️ Requires jitter buffering

**Best For:** Multi-room audio, announcements, live streaming

---



## Technical Feasibility

### 1. HaLow Bandwidth ✅ **SUFFICIENT**

| Parameter | Value |
|-----------|-------|
| HaLow Data Rate | 150 kbps - 7.8 Mbps (depends on MCS) |
| MP3 128kbps | 128 kbps |
| WAV 44.1kHz Stereo | 1411 kbps |
| Multicast Advantage | 1 stream serves N nodes |

**Bandwidth Budget:**
```
Scenario: 3 nodes, MP3 128kbps
- Regular unicast: 128kbps × 3 = 384kbps
- Multicast:       128kbps × 1 = 128kbps ✅

Scenario: 3 nodes, WAV 44.1kHz stereo
- Regular unicast: 1411kbps × 3 = 4233kbps ❌
- Multicast:       1411kbps × 1 = 1411kbps ✅
```

**Verdict:** ✅ Plenty of bandwidth for synchronized playback

---

### 2. ESP32-S3 Processing Power ✅ **PLENTY OF HEADROOM**

**CPU Specs:**
- Dual-core Xtensa LX7 @ 240MHz
- ~400 DMIPS total compute power

**Processing Requirements:**

| Task | CPU Load (%) | Core |
|------|--------------|------|
| **WAV/PCM Playback** | ~5% | Either |
| **MP3 Decode (minimp3)** | 15-25% | Core 0 |
| **RTP Receive + Dejitter** | 2-5% | Core 1 |
| **HTTP Server** | 3-8% | Core 1 |
| **LED Control** | <1% | Either |
| **Total** | ~25-40% | Both cores |

**Verdict:** ✅ Easy, plenty of headroom for other tasks

---

### 3. Memory Budget ✅ **COMFORTABLE**

**Available RAM:**
- DRAM: ~320KB usable
- IRAM: ~128KB (code execution)

**Memory Requirements:**

| Component | Size |
|-----------|------|
| RTP Receive Buffer | 32KB (1-2 sec jitter buffer) |
| MP3 Decode Buffer | 16KB (scratch space) |
| I2S DMA Buffer | 8KB (playback buffer) |
| Network Stack (lwIP) | 50KB (overhead) |
| HTTP Server | 15KB (task stack + buffers) |
| **Total** | **~121KB / 320KB** |

**Remaining:** ~200KB for application code

**Verdict:** ✅ Comfortable memory budget

---

### 4. I2S Audio Output ✅ **HARDWARE READY**

**Available GPIOs (not used by HaLow):**
- D0 (GPIO44) - Free ✅
- D6 (GPIO6) - Free ✅
- D10 (GPIO10) - Free ✅

**I2S Pin Assignment:**
```c
#define I2S_BCLK   GPIO6   // Bit Clock
#define I2S_LRCLK  GPIO10  // Left/Right Clock (Word Select)
#define I2S_DOUT   GPIO44  // Data Out
```

**DAC Module:** PCM5102 ($3-5)
- No MCLK required
- 16/24-bit audio
- Up to 192kHz sample rate
- 3.3V compatible

**Verdict:** ✅ Hardware ready, just need external DAC module

---

## Synchronization Strategy

### Achievable Sync Accuracy

| Approach | Sync Accuracy | Complexity | Recommended? |
|----------|--------------|------------|--------------|
| RTP timestamps only | ±200-500ms | Low | Good for announcements |
| RTP + jitter buffer | ±100-200ms | Medium | ✅ **Best balance** |
| RTP + NTP clock sync | ±50-100ms | Medium-High | Great for music |
| Custom sync pulses | ±10-50ms | High | Overkill |

### Recommended Sync Algorithm

```
┌─────────────────────────────────────────────────┐
│ INITIALIZATION PHASE                            │
├─────────────────────────────────────────────────┤
│ 1. All nodes boot up                            │
│ 2. Connect to HaLow AP                          │
│ 3. Sync clocks via NTP server                  │
│ 4. Subscribe to RTP multicast group             │
└─────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────┐
│ STREAMING PHASE                                 │
├─────────────────────────────────────────────────┤
│ 1. Server sends RTP packets with timestamps     │
│ 2. Each node:                                   │
│    - Receives RTP packets (UDP multicast)       │
│    - Extracts RTP timestamp                     │
│    - Buffers 500ms-1s of audio (jitter buffer)  │
│    - Calculates playback time:                  │
│      play_time = RTP_timestamp + offset         │
│    - Plays audio when local_clock >= play_time  │
│    - Adjusts playback rate slightly to resync   │
└─────────────────────────────────────────────────┘
```

### Jitter Buffer Strategy

```c
// Pseudo-code
#define JITTER_BUFFER_MS 500  // 500ms buffer

struct rtp_packet {
    uint32_t timestamp;
    uint8_t *payload;
    size_t len;
};

// Circular buffer
rtp_packet jitter_buffer[JITTER_BUFFER_SIZE];
uint32_t buffer_write_idx = 0;
uint32_t buffer_read_idx = 0;

void rtp_receive_callback(rtp_packet *pkt) {
    // Add to jitter buffer
    jitter_buffer[buffer_write_idx++] = *pkt;
}

void i2s_playback_task() {
    while (1) {
        uint32_t now = get_local_time_ms();
        rtp_packet *pkt = &jitter_buffer[buffer_read_idx];

        // Check if packet is ready to play
        if (now >= pkt->timestamp + JITTER_BUFFER_MS) {
            i2s_write(pkt->payload, pkt->len);
            buffer_read_idx++;
        }

        vTaskDelay(1);  // 1ms tick
    }
}
```

---

## Audio Format Recommendations

### Start with WAV/PCM, Not MP3

**Why WAV First:**

| Feature | WAV/PCM | MP3 |
|---------|---------|-----|
| Decode Latency | 0ms | ~20ms per frame |
| CPU Usage | ~2% | ~20% |
| Debugging | Simple | Complex |
| Timing Predictability | Perfect | Variable |
| Bandwidth (44.1kHz stereo) | 1411 kbps | 128-320 kbps |

**Recommendation:**
1. ✅ **Phase 1:** Implement with WAV/PCM (prove RTP + sync works)
2. ✅ **Phase 2:** Add MP3 decoder once basics working

### Bandwidth Comparison

```
Format: 44.1kHz Stereo

WAV/PCM Uncompressed:
  44100 Hz × 16-bit × 2 channels = 1411 kbps
  Multicast bandwidth = 1411 kbps (shared among all nodes)
  ✅ Fits in HaLow bandwidth

MP3 128kbps:
  Compressed bitrate = 128 kbps
  Multicast bandwidth = 128 kbps (shared among all nodes)
  ✅ Much lower bandwidth, easy

MP3 320kbps (high quality):
  Compressed bitrate = 320 kbps
  Multicast bandwidth = 320 kbps
  ✅ Still fits easily
```

---

## Implementation Roadmap

### Phase 1: Basic RTP Receiver (1-2 weeks)
**Goal:** Receive RTP packets and play audio immediately

```c
// Simplified flow
UDP multicast listener (239.255.77.77:5004)
    ↓
Extract RTP payload
    ↓
Write directly to I2S DMA
    ↓
Speakers play audio
```

**Features:**
- ✅ UDP multicast receive
- ✅ Basic RTP packet parsing
- ✅ I2S audio output
- ❌ No sync (just play ASAP)
- ❌ No jitter handling

**Success Criteria:** Audio plays on all nodes (may be out of sync)

---

### Phase 2: Add Jitter Buffer (1 week)
**Goal:** Handle network jitter with buffering

```c
RTP receive
    ↓
Jitter buffer (500ms)
    ↓
Playback when buffer filled
    ↓
I2S output
```

**Features:**
- ✅ Circular buffer for RTP packets
- ✅ Time-based playback scheduling
- ✅ Handle packet reordering
- ✅ Handle late/lost packets
- ❌ No clock sync yet

**Success Criteria:** Smooth playback without glitches

---

### Phase 3: Add NTP Synchronization (1 week)
**Goal:** Align clocks across all nodes

```c
NTP client → Sync local clock
    ↓
RTP timestamps aligned to NTP clock
    ↓
All nodes play at same absolute time
```

**Features:**
- ✅ NTP client (ESP-IDF SNTP)
- ✅ RTP timestamp alignment
- ✅ Periodic clock drift correction
- ✅ Synchronized playback ±100ms

**Success Criteria:** All nodes play in sync within 100-200ms

---

### Phase 4: Add MP3 Decoding (1-2 weeks)
**Goal:** Support compressed audio formats

```c
RTP receive
    ↓
MP3 decode (minimp3)
    ↓
Jitter buffer (PCM samples)
    ↓
I2S playback
```

**Features:**
- ✅ minimp3 integration
- ✅ Decode in separate task (Core 0)
- ✅ PCM output to jitter buffer
- ✅ Frame alignment

**Success Criteria:** Synchronized MP3 playback

---

### Phase 5: HTTP Control Integration (1 week)
**Goal:** Server controls playback via HTTP POST

```http
POST /audio/play
{
  "stream_url": "rtp://239.255.77.77:5004",
  "format": "mp3",
  "volume": 80
}

POST /audio/stop

POST /led/set
{
  "state": "on"
}
```

**Features:**
- ✅ HTTP POST handlers
- ✅ Playback control (start/stop)
- ✅ Volume control
- ✅ LED control
- ✅ Status reporting

**Success Criteria:** Full remote control of all nodes

---

## Library Recommendations

### Audio Codec

**Option 1: minimp3** ✅ **Recommended**
```c
// Tiny MP3 decoder
Size: ~20KB code
CPU: 15-25% @ 240MHz
License: CC0 (public domain)
URL: https://github.com/lieff/minimp3
```

**Option 2: ESP-ADF (Audio Development Framework)**
```c
// Full-featured audio framework
Size: ~500KB+ (heavy)
Features: Codecs, pipelines, effects
License: Apache 2.0
URL: https://github.com/espressif/esp-adf
```

**Recommendation:** Start with minimp3 (lightweight), switch to ESP-ADF if you need advanced features

---

### RTP Stack

**Option 1: Custom Lightweight Implementation** ✅ **Recommended**
```c
// Just what you need (~200 lines of code)
- UDP socket multicast receive
- Basic RTP header parsing
- Timestamp extraction
- Payload handling

Pros: Tiny, easy to debug, no dependencies
Cons: You write it yourself
```

**Option 2: live555**
```c
// Full RTP/RTSP library
Size: Large (~1MB+ compiled)
Features: Complete RTSP/RTP stack
License: LGPL

Pros: Full-featured, battle-tested
Cons: Heavy, complex, overkill
```

**Recommendation:** Custom lightweight RTP parser (see code example below)

---

### NTP Client

**ESP-IDF Built-in SNTP Component** ✅ **Use This**
```c
#include "esp_sntp.h"

void ntp_init(void) {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}
```

**Features:**
- ✅ Built into ESP-IDF
- ✅ Automatic clock sync
- ✅ Drift correction
- ✅ No extra dependencies

---

## Challenges & Solutions

### Challenge 1: Network Jitter ⚠️
**Problem:** Packets arrive with variable delay (±50-200ms)

**Solution:** Jitter buffer
- Buffer 500ms-1s of audio
- Play based on RTP timestamp, not arrival time
- Absorbs network variations

---

### Challenge 2: Clock Drift 🕐
**Problem:** ESP32 clocks drift over time (±50ppm)

**Solution:** NTP + Periodic Resync
- Initial sync via NTP on boot
- Periodic resync every 5-10 minutes
- Adjust playback rate slightly to compensate

```c
// Drift correction
int32_t clock_drift_ms = ntp_time - local_time;
if (abs(clock_drift_ms) > 100) {
    // Large drift: hard resync
    resync_clock();
} else {
    // Small drift: adjust playback rate by ±0.1%
    i2s_set_sample_rate(44100 + (clock_drift_ms / 10));
}
```

---

### Challenge 3: Packet Loss 📉
**Problem:** UDP packets can be lost (WiFi interference, congestion)

**Solution:** Forward Error Correction (FEC) + Concealment
- **Option A:** Add redundant packets (simple XOR FEC)
- **Option B:** Audio concealment (interpolate missing samples)
- **Option C:** Accept loss (HaLow is very reliable)

**Recommendation:** Start with Option C (HaLow is reliable), add FEC if needed

---

### Challenge 4: Initial Synchronization 🔄
**Problem:** Nodes boot at different times

**Solution:** Startup Coordination
```c
1. Node boots → LED blinks (not ready)
2. Connect to HaLow → LED solid (connected)
3. Wait for multicast stream detection
4. Pre-buffer 1 second of audio
5. Start playback on next second boundary
6. LED off (playing)
```

---

## Code Structure Example

### Lightweight RTP Parser
```c
#pragma pack(push, 1)
typedef struct {
    uint8_t  vpxcc;       // Version(2), Padding(1), Extension(1), CSRC count(4)
    uint8_t  mpt;         // Marker(1), Payload Type(7)
    uint16_t sequence;    // Sequence number
    uint32_t timestamp;   // RTP timestamp
    uint32_t ssrc;        // Synchronization source ID
} rtp_header_t;
#pragma pack(pop)

#define RTP_PAYLOAD_TYPE_PCMU  0   // G.711 µ-law
#define RTP_PAYLOAD_TYPE_L16   11  // PCM 16-bit
#define RTP_PAYLOAD_TYPE_MPA   14  // MPEG Audio

bool rtp_parse_packet(uint8_t *buf, size_t len,
                      uint32_t *timestamp,
                      uint8_t **payload,
                      size_t *payload_len)
{
    if (len < sizeof(rtp_header_t)) return false;

    rtp_header_t *hdr = (rtp_header_t *)buf;

    // Check version (should be 2)
    if ((hdr->vpxcc >> 6) != 2) return false;

    *timestamp = ntohl(hdr->timestamp);
    *payload = buf + sizeof(rtp_header_t);
    *payload_len = len - sizeof(rtp_header_t);

    return true;
}
```

---

## Performance Summary

### Resource Usage Estimates

| Resource | Usage | Limit | Headroom |
|----------|-------|-------|----------|
| **CPU (MP3)** | 25-40% | 100% | 60-75% ✅ |
| **RAM** | 121KB | 320KB | 199KB ✅ |
| **Bandwidth** | 128-1411 kbps | 7.8 Mbps | 6+ Mbps ✅ |
| **GPIOs** | 3 (I2S) + 1 (LED) | Many available | ✅ |

### Synchronization Targets

| Metric | Target | Achievable |
|--------|--------|-----------|
| **Audio Sync** | ±100-200ms | ✅ Yes |
| **Jitter Handling** | ±200ms variation | ✅ Yes (buffer) |
| **Clock Drift** | <10ms/hour | ✅ Yes (NTP) |
| **Packet Loss** | <0.1% | ✅ HaLow very reliable |

---

## Final Verdict

### ✅ **100% Feasible**

Your multi-room audio system over HaLow is **completely achievable** with the ESP32-S3 hardware you have.

### Key Strengths:
1. ✅ Plenty of CPU power for MP3 decode + RTP
2. ✅ Sufficient RAM for buffering
3. ✅ HaLow bandwidth far exceeds requirements
4. ✅ I2S GPIOs available (6, 10, 44)
5. ✅ Proven technology stack (RTP is RFC standard)

### Recommended Approach:
1. **Start simple:** WAV over RTP multicast
2. **Add jitter buffer:** Smooth playback
3. **Add NTP sync:** Synchronized playback
4. **Add MP3 codec:** Bandwidth savings
5. **Integrate HTTP control:** Remote management

### Expected Timeline:
- **MVP (WAV playback):** 2-3 weeks
- **Synchronized audio:** 4-5 weeks
- **Full system (MP3 + control):** 6-8 weeks

### Pro Tips:
- ✅ Use multicast address `239.255.77.77` (local scope)
- ✅ RTP port `5004` (standard)
- ✅ Start with 500ms jitter buffer
- ✅ Target ±100-200ms sync (good enough!)
- ✅ Get PCM5102 DAC modules (~$3-5 each)

---

## Next Steps

1. **Order hardware:**
   - PCM5102 I2S DAC modules (one per node)
   - LQ-AMP10W amplifiers (you have these)

2. **Set up test server:**
   - Linux/Raspberry Pi as HaLow AP
   - FFmpeg for RTP streaming
   - NTP server

3. **Start Phase 1:**
   - Implement UDP multicast receiver
   - Parse RTP packets
   - Output to I2S

**Questions?** Ready to start implementation? 🚀

---

**Document Version:** 1.0
**Last Updated:** November 19, 2025
**Author:** System Architecture (via Claude)
