# [**pi86-rp2350**](https://github.com/cctsao1008/pi86-rp2350/tree/main) AI Bridge Architecture Design Note

## Working Title

**A 1980s CPU Talks to AI**

| Field | Value |
|---|---|
| Status | Accepted Target Architecture |
| Scope | Bidirectional message exchange between a physical NEC V30 and OpenAI Codex |
| Target platform | pi86-rp2350 V30 interface with an RP2350-based companion controller |
| Document type | Architecture design note |

This document defines the target architecture for one specific interaction:

```text
OpenAI Codex > HELLO NEC V30

NEC V30      > HELLO OPENAI CODEX
```

The hardware starts first. The physical NEC V30 boots, reaches its native message program, and exposes a ready mailbox through the RP2350. When the user later begins a Codex session, OpenAI Codex sends the first conversational message. That greeting crosses the Codex adapter, provider-neutral host bridge, USB, the RP2350, and the physical V30 bus. Native V30 code reads it and returns a reply through the same system boundaries to Codex.

The document defines the system boundary, component responsibilities, message flow, timing separation, interfaces, and observable result. It does not define an implementation schedule, project validation sequence, or general-purpose service architecture.

---

## 1. Purpose

The purpose of the pi86-rp2350 AI Bridge is to establish genuine bidirectional communication between:

```text
a physical NEC V30
        ↕
OpenAI Codex
```

After the hardware has started and the V30 message interface is ready, OpenAI Codex initiates the conversation with:

```text
HELLO NEC V30
```

The physical V30 consumes that message and executes native code that replies:

```text
HELLO OPENAI CODEX
```

The reply crosses the V30 bus and RP2350 service path before being delivered to OpenAI Codex.

The architectural objective is the complete end-to-end conversation. Both lines of the transcript represent one interaction involving native execution on the physical V30.

---

## 2. Target Interaction

The canonical interaction is:

```text
OpenAI Codex > HELLO NEC V30
NEC V30      > HELLO OPENAI CODEX
```

The causal path is:

```text
OpenAI Codex
    ↓
Codex Adapter
    ↓
Host Bridge API
    ↓
USB
    ↓
RP2350 Core1
    ↓
RP2350 Core0
    ↓
locally staged receive data
    ↓
RP2350 PIO / DMA
    ↓
physical V30 bus
    ↓
V30 native program reads the greeting
    ↓
V30-visible transmit interface
    ↓
physical V30 bus
    ↓
RP2350 PIO / DMA
    ↓
RP2350 Core0
    ↓
RP2350 Core1
    ↓
USB
    ↓
Host Bridge
    ↓
Codex Adapter
    ↓
OpenAI Codex receives the V30 reply
```

Each boundary has a defined owner and message representation.

---

## 3. Architectural Scope

### Included

- Codex generation of the opening greeting
- native V30 consumption of the Codex greeting
- native V30 generation of the reply
- a V30-visible transmit and receive interface
- deterministic V30 bus interaction through PIO and DMA
- RP2350 Core0 and Core1 responsibility separation
- USB communication with a host computer
- a provider-neutral Host Bridge and a Codex-specific adapter
- asynchronous delivery of the Codex greeting
- delivery of the V30 reply to Codex
- a human-readable conversation transcript
- engineering evidence identifying the physical message path

### Outside This Document

- implementation order and project milestones
- general-purpose AI, disk, network, compiler, or automation services
- executable program delivery
- a complete PC-compatible peripheral architecture
- generalized machine-control policy

These subjects may have separate design documents without changing the message bridge defined here.

---

## 4. Physical System

```text
┌──────────────────┐
│ Physical NEC V30 │
└────────┬─────────┘
         │ native address / data / control bus
         ▼
┌──────────────────────────┐
│ Homebrew8088 / Pi86 HAT  │
│ physical V30 interface   │
└────────┬─────────────────┘
         │ Raspberry Pi-compatible 40-pin boundary
         ▼
┌──────────────────────────┐
│ Waveshare RP2350-PiZero  │
│ PIO / DMA / Core0 / Core1│
└────────┬─────────────────┘
         │ USB
         ▼
┌──────────────────────────┐
│ Host Bridge              │
│ semantic bridge API      │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Codex Adapter            │
│ provider-specific logic  │
└────────┬─────────────────┘
         │ OpenAI interface
         ▼
┌──────────────────────────┐
│ OpenAI Codex             │
└──────────────────────────┘
```

### Physical NEC V30

The NEC V30 is the physical execution endpoint. It:

- starts from its native reset vector
- executes native V30/8086-compatible code
- observes receive status
- reads the Codex greeting
- generates the native V30 reply
- writes the reply through a V30-visible interface
- confirms both greeting consumption and reply publication through native program behavior

### Pi86 Physical Interface

The Homebrew8088 / Pi86 hardware connects the V30 bus to the Raspberry Pi-compatible 40-pin boundary.

Architecturally relevant signals include:

```text
AD0..AD15
A16..A19
ASTB / ALE
RD
WR
IO/M
byte-lane control
CLK
RESET
READY
interrupt-related control
```

The detailed GPIO and connector mapping belongs in the hardware-interface documentation.

### RP2350 Controller

The current controller is a Waveshare RP2350-PiZero based on RP2350B. It provides programmable I/O, DMA, internal SRAM, two processor cores, and USB device connectivity.

### Host Bridge

The host bridge terminates the RP2350 USB protocol and exposes a provider-neutral semantic API. It accepts complete AI messages, converts them into the structured message format used by the RP2350, receives complete V30-originated messages, and returns them to the AI-side adapter.

Representative host-side semantics are:

```text
submit_ai_message(message)
receive_v30_message()
query_bridge_status()
```

The RP2350 and V30-facing protocol do not contain provider-specific concepts.

The Host Bridge and Codex Adapter are **logical architectural components**. They may initially be implemented as modules within the same host process; the architecture defines a responsibility boundary, not a mandatory process boundary.

### Codex Adapter

The Codex adapter owns OpenAI Codex-specific integration:

```text
session / prompt handling
provider-specific invocation
provider configuration
conversation context
mapping between Codex turns and host-bridge messages
```

It converts Codex interaction into the provider-neutral Host Bridge API.

### OpenAI Codex

Codex is the named AI participant and initiator of the canonical conversation. Provider-specific details terminate in the Codex adapter and do not appear in the Host Bridge API, RP2350 protocol, or V30-visible interface.

---

## 5. Responsibility Partition

```text
OpenAI Codex
      ↕
Codex Adapter
      ↕
Host Bridge
      ↕
USB
      ↕
Core1 Service Plane
      ↕
Core0 Supervisory Plane
      ↕
PIO / DMA Data Plane
      ↕
Physical NEC V30
```

### PIO / DMA Data Plane

PIO and DMA own the deterministic V30-facing behavior:

- capture of qualified V30 I/O activity
- delivery of locally prepared receive data
- AD bus and PINDIRS timing
- byte-lane handling
- movement of bus records and message data

### Core0 Supervisory Plane

Core0 owns the RP2350 side of the V30 machine boundary:

- V30 RESET and clock supervision
- PIO and DMA configuration
- preparation and publication of local mailbox state
- acceptance of a complete `AI_TO_V30_MESSAGE` from Core1
- transition of the inbound message into V30-visible state
- transfer of a completed `V30_TO_AI_MESSAGE` to Core1

### Core1 Service Plane

Core1 owns the external message service:

- USB application-level handling
- inbound `AI_TO_V30_MESSAGE` reception
- outbound `V30_TO_AI_MESSAGE` packaging
- communication with the Host Bridge
- CDC transcript and engineering output
- service-plane buffering

Low-level USB initialization or interrupt plumbing may execute where required by the RP2350 SDK. USB application semantics remain part of the Core1 service role.

### Host Bridge

The host bridge owns the USB connection, provider-neutral message encoding and delivery, V30 reply reception, and bridge-level conversation logging.

### Codex Adapter

The Codex adapter owns Codex greeting acquisition, provider-specific interaction, delivery of the opening greeting into the Host Bridge API, and return of the V30-originated reply to Codex.

### OpenAI Codex

Codex produces the opening greeting and receives the V30-originated reply. It does not participate in V30 bus timing or RP2350-local delivery.

---

## 6. V30-Visible Message Interface

The V30 communicates through a compact I/O mailbox abstraction:

```text
STATUS
TX_DATA
RX_DATA
CONTROL
```

Final I/O addresses are defined by the pi86-rp2350 I/O map rather than by this document.

`TX` and `RX` are named from the **V30 perspective**:

```text
TX = V30 → host / AI
RX = host / AI → V30
```

### STATUS

```text
TX_READY
TX_ACCEPTED
RX_READY
RX_END
SERVICE_BUSY
SERVICE_ERROR
```

### TX_DATA

The V30 writes its reply bytes:

```text
H E L L O   O P E N A I   C O D E X
```

### RX_DATA

The V30 reads the Codex greeting bytes:

```text
H E L L O   N E C   V 3 0
```

### CONTROL

```text
TX_BEGIN
TX_COMMIT
RX_ACK
CLEAR_ERROR
```

The mailbox represents complete messages at the service boundary while preserving byte-oriented access for native V30 software.

### Mailbox Ownership

Each mailbox direction has exactly one producer and one consumer at each ownership state.

Codex-to-V30 receive path:

```text
Core0 owns RX construction
        ↓ publish / commit
V30 owns the readable RX message
        ↓ RX_ACK
Core0 owns the RX buffer again
```

V30-to-Codex transmit path:

```text
V30 owns TX construction
        ↓ TX_COMMIT
Core0 owns the completed TX message
        ↓ publish
Core1 owns the service object
        ↓ USB transfer complete
Core1 releases the object
```

Ownership transfer occurs only at explicit message boundaries. No layer may modify a message after ownership has been transferred.

The existing pi86-rp2350 diagnostic console remains a separate engineering interface and is not the AI message mailbox.

---

## 7. Timing and Message Decoupling

```text
V30 bus cycle
    ↕
PIO / DMA cycle-exact behavior
    ↕
Core0 bounded supervision
    ↕
Core1 / USB service timing
    ↕
Host and Codex interaction timing
```

The V30 bus consumes RP2350-local state. The hardware and V30 native program reach `V30_READY` before the user begins the Codex interaction.

Codex-to-V30 path:

```text
Codex greeting reaches the RP2350
        ↓
the complete greeting is stored locally
        ↓
RX_READY becomes V30-visible
        ↓
V30 reads locally available bytes
```

V30-to-Codex path:

```text
V30 writes reply bytes
        ↓
RP2350 captures and assembles the reply
        ↓
the complete reply enters the service path
```

The V30 may continue executing, poll status, or wait through a V30 software mechanism before the Codex greeting arrives. No V30 bus transaction remains open for the duration of the host or Codex interaction.

---

## 8. Core-to-Core Message Boundary

Core0 and Core1 exchange bounded message objects through single-owner queues.

### Codex to V30

```text
Core1 receives a complete AI_TO_V30_MESSAGE
        ↓
Core1 publishes the inbound message to Core0
        ↓
Core0 stages the message in local SRAM
        ↓
PIO / DMA exposes it through RX_DATA
```

### V30 to Codex

```text
PIO / DMA captures the V30 reply
        ↓
Core0 freezes the complete reply
        ↓
Core0 publishes a V30_TO_AI_MESSAGE to Core1
        ↓
Core1 transfers it over USB
```

A queue element contains:

```text
message type
sequence
length
payload
result status
```

Message ownership changes when a complete object is published across the boundary.

---

## 9. USB and Host Protocol

The RP2350 appears to the host as a USB composite device:

```text
USB Composite Device
├── HID machine-facing message transport
└── CDC engineering console
```

### Machine-Facing Transport

The machine transport carries:

```text
protocol version
message type
sequence
payload length
payload
result status
```

USB HID provides the bounded machine-facing transport. The semantic message definition remains independent of the USB report layout.

Representative message types are:

```text
AI_TO_V30_MESSAGE
V30_TO_AI_MESSAGE
QUERY_BRIDGE_STATUS
BRIDGE_STATUS
```

### CDC Engineering Console

CDC presents the human-readable transcript and compact engineering state:

```text
[CODEX]
HELLO NEC V30

[V30]
HELLO OPENAI CODEX
```

CDC observes and reports the interaction carried by the machine-facing path.

### Host Bridge API

```text
submit_ai_message(message)
receive_v30_message()
query_bridge_status()
```

### Codex Adapter API

Conceptually, the Codex adapter maps provider-specific interaction onto the Host Bridge API:

```text
receive_codex_greeting()
submit_ai_message(message)

receive_v30_message()
send_reply_to_codex(message)
```

Codex session behavior, provider configuration, prompt formatting, and provider-specific handling remain entirely inside the Codex adapter.

---

## 10. Codex-to-V30 Greeting Flow

```text
1. The hardware starts before the Codex interaction begins.
2. The physical V30 reaches its native message program.
3. The RP2350 reports `V30_READY` over USB HID to the Host Bridge.
4. The user begins the Codex interaction.
5. OpenAI Codex produces "HELLO NEC V30".
6. The Codex Adapter submits the greeting as an `AI_TO_V30_MESSAGE` through the Host Bridge API.
7. Core1 receives the complete greeting over USB.
8. Core1 publishes the greeting to Core0.
9. Core0 stages the greeting in RP2350 internal SRAM.
10. The V30-visible mailbox reports RX_READY.
11. The physical V30 reads the greeting through RX_DATA.
12. The V30 acknowledges greeting consumption.
```

The first conversational message is generated by Codex after the already-running physical system reports that the V30 message interface is ready.

---

## 11. V30-to-Codex Reply Flow

```text
1. Native V30 code recognizes the completed Codex greeting.
2. The V30 generates "HELLO OPENAI CODEX".
3. The V30 writes the reply through TX_DATA.
4. PIO / DMA captures the qualified V30 I/O transfers.
5. Core0 publishes a complete `V30_TO_AI_MESSAGE`.
6. Core1 packages the reply for USB transport.
7. The Host Bridge receives the V30 reply.
8. The Codex Adapter delivers the reply to OpenAI Codex.
9. The bridge records completion of the conversation.
```

Completion occurs when OpenAI Codex receives the reply generated by native code on the physical V30.

---

## 12. Conversation State

```text
BOOT
  ↓ physical V30 reaches its native message program
V30_READY
  ↓ user begins the Codex interaction
AI_TO_V30_MESSAGE
  ↓ complete greeting is staged locally
V30_RX_READY
  ↓ V30 reads the greeting and builds its native reply
V30_REPLY_PENDING
  ↓ V30 commits the complete reply
V30_TO_AI_MESSAGE
  ↓ Host Bridge and Codex Adapter deliver the reply
CODEX_RX
  ↓ Codex receives the V30 reply
CONVERSATION_COMPLETE
```

The sequence identifier links:

- Codex greeting
- RP2350 inbound greeting message
- V30 greeting consumption
- V30 reply bytes
- RP2350 outbound reply message
- Codex receipt of the V30 reply

---

## 13. Observability

The architecture exposes two synchronized views.

### Human-Readable Conversation

```text
OpenAI Codex > HELLO NEC V30
NEC V30      > HELLO OPENAI CODEX
```

### Engineering Evidence

```text
V30_READY published
Codex greeting accepted by the Host Bridge
Core1 inbound greeting received
Core0 greeting staged
V30 RX greeting bytes served
V30 greeting consumption acknowledged
V30 TX reply bytes captured
Core0 outbound reply published
Core1 USB reply transfer completed
Codex received the V30 reply
```

Compact evidence includes the conversation sequence, greeting and reply text, V30 I/O transfer count, Core0/Core1 publication state, USB state, V30 greeting consumption, and Codex receipt of the reply.

Raw bus traces remain engineering evidence. The transcript remains the canonical presentation of the completed interaction.

---

## 14. Success Definition

The target interaction is complete when:

1. A physical NEC V30 reaches its native program through the pi86-rp2350 bus architecture.
2. The RP2350 reports that the V30 message interface is ready before the Codex interaction begins.
3. OpenAI Codex generates `HELLO NEC V30` as the first conversational message.
4. The greeting crosses the host, USB, and RP2350 message path.
5. The greeting becomes locally staged V30-visible data.
6. The physical V30 reads every greeting byte.
7. Native V30 code generates `HELLO OPENAI CODEX` in reply.
8. The reply crosses the physical V30 bus and RP2350 message path.
9. The Codex Adapter delivers the V30-originated reply to OpenAI Codex.
10. The system records completion of the same conversation sequence.
11. CDC presents the complete human-readable conversation.

The visible result is:

```text
OpenAI Codex > HELLO NEC V30
NEC V30      > HELLO OPENAI CODEX
```

---

## 15. Architecture Summary

```text
OpenAI Codex
    ↕ provider-specific interaction
Codex Adapter
    ↕ provider-neutral semantic message
Host Bridge
    ↕ structured USB HID message
Core1 Service Plane
    ↕ complete bounded message object
Core0 Supervisory Plane
    ↕ locally staged V30-visible state
PIO / DMA Data Plane
    ↕ deterministic physical bus transfer
NEC V30 Native Program
```

Responsibilities are:

```text
PIO / DMA
    V30 bus timing and data transfer

Core0
    RP2350-side V30 machine boundary

Core1
    external message service

Host Bridge
    provider-neutral semantic bridge and USB transport

Codex Adapter
    Codex-specific integration

OpenAI Codex
    creates the opening greeting and receives the V30 reply
```

The target is:

> **After the physical NEC V30 is running, OpenAI Codex sends “HELLO NEC V30.” The V30 consumes the greeting through its native bus interface and replies “HELLO OPENAI CODEX.”**

---

## Hardware References

- Waveshare RP2350-PiZero product page: https://www.waveshare.com/product/rp2350-pizero.htm
- Waveshare RP2350-PiZero schematic: https://files.waveshare.com/wiki/RP2350-PiZero/RP2350-PiZero.pdf
- RP2350 datasheet: https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf
- Homebrew8088 / Pi86 Raspberry Pi PCB: https://www.homebrew8088.com/home/raspberry-pi-second-project
- Original Pi86 repository: https://github.com/homebrew8088/pi86
- NEC V20/V30 User's Manual reference: https://www.ceibo.com/eng/datasheets/NEC-V20-V30-Users-Manual.pdf
