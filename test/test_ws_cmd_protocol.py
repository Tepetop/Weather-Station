#!/usr/bin/env python3
"""Host-side checks mirroring WS_Cmd_* / WS_Cycle_* helpers."""

WS_CMD_MEASURE = 0x01
WS_CMD_SIZE = 8
WS_CMD_CYCLE_ID_OFFSET = 1
WS_CMD_TARGET_MASK_OFFSET = 2
WS_CMD_TARGET_ALL = 0xFF


def encode_measure_to(cycle_id: int, target_mask: int) -> bytes:
    buf = bytearray(WS_CMD_SIZE)
    buf[0] = WS_CMD_MEASURE
    buf[WS_CMD_CYCLE_ID_OFFSET] = cycle_id & 0xFF
    buf[WS_CMD_TARGET_MASK_OFFSET] = target_mask & 0xFF
    return bytes(buf)


def decode_measure_ex(buf: bytes):
    if len(buf) < 2 or buf[0] != WS_CMD_MEASURE:
        return None
    cycle_id = buf[WS_CMD_CYCLE_ID_OFFSET]
    target_mask = buf[WS_CMD_TARGET_MASK_OFFSET] if len(buf) > WS_CMD_TARGET_MASK_OFFSET else WS_CMD_TARGET_ALL
    if target_mask == 0:
        target_mask = WS_CMD_TARGET_ALL
    return cycle_id, target_mask


def is_duplicate(cycle_id: int, last_cycle_id: int, have_last: bool) -> bool:
    return have_last and cycle_id == last_cycle_id


def expected_mask(node_count: int) -> int:
    if node_count <= 0:
        return 0
    if node_count >= 8:
        return 0xFF
    return (1 << node_count) - 1


def is_complete(expected: int, received: int) -> bool:
    return (received & expected) == expected


def main() -> None:
    cmd = encode_measure_to(42, 0x03)
    cycle_id, mask = decode_measure_ex(cmd)
    assert cycle_id == 42 and mask == 0x03

    assert is_duplicate(42, 42, True)
    assert not is_duplicate(43, 42, True)
    assert not is_duplicate(42, 42, False)

    assert expected_mask(2) == 0x03
    assert is_complete(0x03, 0x03)
    assert not is_complete(0x03, 0x01)

    # Single-node target for NODE_ID=1
    cmd_one = encode_measure_to(7, 0x02)
    _, one_mask = decode_measure_ex(cmd_one)
    assert (one_mask & (1 << 1)) != 0
    assert (one_mask & (1 << 0)) == 0

    print("OK: ws_cmd_protocol")


if __name__ == "__main__":
    main()
