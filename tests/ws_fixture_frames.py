"""Offline WebSocket SERVER-fixture framing; not the production client."""
import struct
from typing import Optional, Tuple

MAX_PAYLOAD = 1048576


def server_frame(payload: bytes, opcode: int = 1, final: bool = True) -> bytes:
    if type(payload) is not bytes:
        raise ValueError("payload must be bytes")
    if type(opcode) is not int:
        raise ValueError("opcode must be int")
    if type(final) is not bool:
        raise ValueError("final must be bool")
    if opcode not in (0, 1, 2, 8, 9, 10):
        raise ValueError("invalid opcode")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    if opcode >= 8:
        if not final:
            raise ValueError("control frame must be final")
        if len(payload) > 125:
            raise ValueError("control payload too large")
    first = (0x80 if final else 0x00) | opcode
    length = len(payload)
    if length < 126:
        header = bytes((first, length))
    elif length <= 65535:
        header = struct.pack("!BBH", first, 126, length)
    else:
        header = struct.pack("!BBQ", first, 127, length)
    return header + payload


def take_client_frame(buffer: bytearray, maximum: int = MAX_PAYLOAD) -> Optional[Tuple[int, bool, bytes]]:
    if type(buffer) is not bytearray:
        raise ValueError("buffer must be bytearray")
    if type(maximum) is not int:
        raise ValueError("maximum must be int")
    if maximum < 0 or maximum > MAX_PAYLOAD:
        raise ValueError("maximum out of range")
    if len(buffer) < 2:
        return None
    first = buffer[0]
    second = buffer[1]
    if first & 0x70:
        raise ValueError("reserved bits set")
    opcode = first & 0x0F
    if opcode not in (0, 1, 2, 8, 9, 10):
        raise ValueError("invalid opcode")
    final = bool(first & 0x80)
    if not (second & 0x80):
        raise ValueError("client frame must be masked")
    if opcode >= 8 and not final:
        raise ValueError("control frame must be final")
    length_code = second & 0x7F
    if opcode >= 8 and length_code > 125:
        raise ValueError("control payload too large")
    if length_code < 126:
        payload_length = length_code
        header_length = 2
        if payload_length > maximum:
            raise ValueError("payload too large")
    elif length_code == 126:
        if len(buffer) < 4:
            return None
        payload_length = struct.unpack("!H", bytes(buffer[2:4]))[0]
        if payload_length < 126:
            raise ValueError("non-minimal length encoding")
        if payload_length > maximum:
            raise ValueError("payload too large")
        header_length = 4
    else:
        if len(buffer) < 10:
            return None
        payload_length = struct.unpack("!Q", bytes(buffer[2:10]))[0]
        if payload_length & (1 << 63):
            raise ValueError("invalid 64-bit length")
        if payload_length <= 65535:
            raise ValueError("non-minimal length encoding")
        if payload_length > maximum:
            raise ValueError("payload too large")
        header_length = 10
    if opcode >= 8 and payload_length > 125:
        raise ValueError("control payload too large")
    mask_start = header_length
    payload_start = header_length + 4
    total_length = payload_start + payload_length
    if len(buffer) < total_length:
        return None
    key = bytes(buffer[mask_start:payload_start])
    masked = bytes(buffer[payload_start:total_length])
    payload = bytes(value ^ key[index % 4] for index, value in enumerate(masked))
    del buffer[:total_length]
    return (opcode, final, payload)
