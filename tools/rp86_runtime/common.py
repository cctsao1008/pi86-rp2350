#!/usr/bin/env python3
"""Shared imports and protocol constants for the RP86 Host runtime."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import secrets
import struct
import sys
import time
from typing import Any

from .shell_commands import (
    CommandHistory,
    command_help,
    complete_shell_input,
    completion_token,
    format_host_directory,
    is_device_path,
    parse_command,
    unavailable_message,
)
from .filesystem import (
    df_request,
    list_request,
    parse_df,
    parse_list,
    parse_read,
    read_request,
    validate_reply as validate_filesystem_payload,
    write_records,
)
from .broker import (
    BrokerClient,
    BrokerRecord,
    DeviceBroker,
    discover_brokers,
    select_broker,
)
from .workload import control_record, decode_status_payload, workload_from_command

from .evidence import (
    AI_B2_HID,
    COMPANION_RUNTIME,
    explain_output,
    validate_output,
)
from .protocol import (
    MESSAGE_SIZE,
    Message,
    NativeServiceWitness,
    STATUS_OK,
    TYPE_COMMAND,
    TYPE_HEARTBEAT,
    TYPE_HELLO,
    TYPE_FILESYSTEM_REQUEST,
    TYPE_FILESYSTEM_RESULT,
    TYPE_RESULT,
    TYPE_RUNTIME_CONTROL,
    TYPE_RUNTIME_STATUS,
    TYPE_TEXT,
    TYPE_WORKLOAD_BEGIN,
    TYPE_WORKLOAD_COMMIT,
    TYPE_WORKLOAD_CONTROL,
    TYPE_WORKLOAD_DATA,
    TYPE_WORKLOAD_RESULT,
    TYPE_WORKLOAD_STATUS,
    RUNTIME_CONTROL_ENTER_BOOTLOADER,
    RUNTIME_CONTROL_REBOOT,
)

CANONICAL_GREETING = b"HELLO NEC V30"
CANONICAL_REPLY = b"HELLO OPENAI CODEX"
BOOTLOADER_REQUEST = b"PI86 BOOTLOADER\n"
BOOTLOADER_ACK = b"PI86 BOOTLOADER ACK"
REBOOT_REQUEST = b"PI86 REBOOT\n"
REBOOT_ACK = b"PI86 REBOOT ACK"
STATUS_REQUEST = b"PI86 STATUS\n"
STATUS_BEGIN = b"PI86 STATUS BEGIN"
STATUS_END = b"PI86 STATUS END"
HEARTBEAT_REPLY = b"V30 HEARTBEAT OK"
COMMAND_REPLY = b"V30 COMMAND OK"
USB_VID = 0xCAFE
USB_PID = 0x4011
PROCESSOR_NAMES = {
    "nec-v30": "NEC V30",
    "intel-8086": "INTEL 8086",
}
TERMINAL_MARKERS = tuple(
    profile.end_marker.encode("ascii")
    for profile in (AI_B2_HID, COMPANION_RUNTIME)
)

PASS_EXIT = 0
DEPENDENCY_EXIT = 3
TRANSPORT_EXIT = 4
VALIDATION_EXIT = 5
