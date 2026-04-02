"""Host-side Numloss codec helpers used by the disk image builder and tests."""

HEADER_SIZE = 16
MAGIC = b"NMLS"
VERSION_V1 = 1
VERSION_V2 = 2
VERSION_V3 = 3

TRANSFORM_RAW = 0
TRANSFORM_DELTA8 = 1
TRANSFORM_XOR8 = 2
TRANSFORM_GROUP4 = 3
TRANSFORM_GROUP4_DELTA8 = 4
TRANSFORM_GROUP4_XOR8 = 5
TRANSFORM_GROUP8 = 6
TRANSFORM_GROUP8_DELTA8 = 7
TRANSFORM_GROUP8_XOR8 = 8

LITERAL_MAX = 64
RUN_MIN_V1 = 4
RUN_MIN_V3 = 3
RUN_MAX = 66
MATCH_MIN_V1 = 4
MATCH_MIN_V3 = 4
MATCH_MAX_V1 = 66
MATCH_MAX_V3 = 258
OFFSET_MAX = 65535

SHORT_MATCH_MIN = 4
SHORT_MATCH_MAX = 7
SHORT_MATCH_OFFSET_MAX = 6144
SHORT_MATCH_TOKEN_BASE = 0x80
SHORT_MATCH_TOKEN_LAST = 0xDF
SHORT_MATCH_RANGE = SHORT_MATCH_OFFSET_MAX

REPEAT_MATCH_MIN = 3
REPEAT_MATCH_MAX = 33
REPEAT_MATCH_TOKEN_BASE = 0xE0
REPEAT_MATCH_TOKEN_LAST = 0xFE
LONG_MATCH_TOKEN = 0xFF

HASH_BITS = 13
HASH_SIZE = 1 << HASH_BITS
HASH_WAYS = 8
INVALID_POS = 0xFFFFFFFF

CHOICE_LITERAL = 0
CHOICE_RUN = 1
CHOICE_SHORT_MATCH = 2
CHOICE_REPEAT_MATCH = 3
CHOICE_LONG_MATCH = 4

TRANSFORM_CANDIDATES = (
    TRANSFORM_RAW,
    TRANSFORM_DELTA8,
    TRANSFORM_XOR8,
    TRANSFORM_GROUP4,
    TRANSFORM_GROUP4_DELTA8,
    TRANSFORM_GROUP4_XOR8,
    TRANSFORM_GROUP8,
    TRANSFORM_GROUP8_DELTA8,
    TRANSFORM_GROUP8_XOR8,
)


def _u32_to_le(value):
    return bytes((
        value & 0xFF,
        (value >> 8) & 0xFF,
        (value >> 16) & 0xFF,
        (value >> 24) & 0xFF,
    ))


def _u32_from_le(data, offset):
    return (
        data[offset]
        | (data[offset + 1] << 8)
        | (data[offset + 2] << 16)
        | (data[offset + 3] << 24)
    )


def _write_header(version, original_size, payload_size, flags=0):
    return MAGIC + bytes((version, flags, 0, 0)) + _u32_to_le(original_size) + _u32_to_le(payload_size)


def _write_chunked_header(original_size, chunk_size):
    return _write_header(VERSION_V2, original_size, chunk_size)


def _archive_version(data):
    if data is None or len(data) < HEADER_SIZE:
        return 0
    if bytes(data[:4]) != MAGIC:
        return 0
    return data[4]


def _archive_transform(data):
    if _archive_version(data) != VERSION_V3:
        return TRANSFORM_RAW
    return data[5]


def is_archive(data):
    return _archive_version(data) in (VERSION_V1, VERSION_V2, VERSION_V3)


def read_header(data):
    if not is_archive(data):
        raise ValueError("bad numloss header")

    version = _archive_version(data)
    if version in (VERSION_V1, VERSION_V3):
        payload_size = _u32_from_le(data, 12)
        if payload_size > len(data) - HEADER_SIZE:
            raise ValueError("payload extends past archive")
    elif version == VERSION_V2:
        payload_size = len(data) - HEADER_SIZE
    else:
        raise ValueError("bad numloss header")

    return _u32_from_le(data, 8), payload_size


def _hash4(data, pos):
    value = (
        (data[pos] << 24)
        ^ (data[pos + 1] << 16)
        ^ (data[pos + 2] << 8)
        ^ data[pos + 3]
    )
    value = (value * 2654435761) & 0xFFFFFFFF
    return (value >> (32 - HASH_BITS)) & (HASH_SIZE - 1)


def _history_reset():
    return [[INVALID_POS] * HASH_WAYS for _ in range(HASH_SIZE)]


def _history_insert(history, data, pos):
    if pos + 3 >= len(data):
        return

    slot = history[_hash4(data, pos)]
    slot.insert(0, pos)
    del slot[HASH_WAYS:]


def _find_run_len(data, pos, run_max):
    length = 1
    limit = min(run_max, len(data) - pos)
    value = data[pos]

    while length < limit and data[pos + length] == value:
        length += 1
    return length


def _find_match_len_v1(history, data, pos):
    if pos + MATCH_MIN_V1 > len(data):
        return 0, 0

    limit = min(MATCH_MAX_V1, len(data) - pos)
    best_len = 0
    best_offset = 0
    slot = history[_hash4(data, pos)]

    for candidate in slot:
        if candidate == INVALID_POS or candidate >= pos:
            continue

        offset = pos - candidate
        if offset == 0 or offset > OFFSET_MAX:
            continue

        if (
            data[candidate] != data[pos]
            or data[candidate + 1] != data[pos + 1]
            or data[candidate + 2] != data[pos + 2]
            or data[candidate + 3] != data[pos + 3]
        ):
            continue

        length = MATCH_MIN_V1
        while length < limit and data[candidate + length] == data[pos + length]:
            length += 1

        if length > best_len:
            best_len = length
            best_offset = offset
            if length == limit:
                break

    return best_len, best_offset


def _emit_literals(out, data, start, length):
    while length > 0:
        chunk = min(length, LITERAL_MAX)
        out.append(chunk - 1)
        out.extend(data[start:start + chunk])
        start += chunk
        length -= chunk


def _encode_v1(data):
    data = bytes(data)
    if len(data) > 0xFFFFFFFF:
        raise ValueError("input too large")

    history = _history_reset()
    out = bytearray(_write_header(VERSION_V1, len(data), 0))
    pos = 0
    literal_start = 0
    literal_len = 0

    while pos < len(data):
        run_len = _find_run_len(data, pos, RUN_MAX)
        match_len, match_offset = _find_match_len_v1(history, data, pos)

        kind = CHOICE_LITERAL
        length = 1
        offset = 0

        if run_len >= RUN_MIN_V1 and run_len >= match_len + 1:
            kind = CHOICE_RUN
            length = run_len
        elif match_len >= MATCH_MIN_V1:
            kind = CHOICE_LONG_MATCH
            length = match_len
            offset = match_offset

        if kind == CHOICE_RUN:
            if literal_len:
                _emit_literals(out, data, literal_start, literal_len)
                literal_len = 0

            out.append(0x40 | (length - 3))
            out.append(data[pos])
            for delta in range(length):
                _history_insert(history, data, pos + delta)
            pos += length
            literal_start = pos
            continue

        if kind == CHOICE_LONG_MATCH:
            if literal_len:
                _emit_literals(out, data, literal_start, literal_len)
                literal_len = 0

            out.append(0x80 | (length - 3))
            out.append(offset & 0xFF)
            out.append((offset >> 8) & 0xFF)
            for delta in range(length):
                _history_insert(history, data, pos + delta)
            pos += length
            literal_start = pos
            continue

        if literal_len == 0:
            literal_start = pos
        _history_insert(history, data, pos)
        literal_len += 1
        pos += 1

        if literal_len == LITERAL_MAX:
            _emit_literals(out, data, literal_start, literal_len)
            literal_len = 0
            literal_start = pos

    if literal_len:
        _emit_literals(out, data, literal_start, literal_len)

    out[:HEADER_SIZE] = _write_header(VERSION_V1, len(data), len(out) - HEADER_SIZE)
    return bytes(out)


def _group_bytes(data, width):
    full_words = len(data) // width
    grouped = bytearray(len(data))
    out_pos = 0

    for lane in range(width):
        for index in range(full_words):
            grouped[out_pos] = data[index * width + lane]
            out_pos += 1

    grouped[out_pos:] = data[full_words * width:]
    return bytes(grouped)


def _ungroup_bytes(data, width):
    full_words = len(data) // width
    head_size = full_words * width
    restored = bytearray(len(data))
    src = 0

    for lane in range(width):
        for index in range(full_words):
            restored[index * width + lane] = data[src]
            src += 1

    restored[head_size:] = data[head_size:]
    return bytes(restored)


def _delta_forward(data):
    out = bytearray(len(data))
    prev = 0
    for index, value in enumerate(data):
        out[index] = (value - prev) & 0xFF
        prev = value
    return bytes(out)


def _delta_inverse(data):
    out = bytearray(len(data))
    prev = 0
    for index, value in enumerate(data):
        prev = (prev + value) & 0xFF
        out[index] = prev
    return bytes(out)


def _xor_forward(data):
    out = bytearray(len(data))
    prev = 0
    for index, value in enumerate(data):
        out[index] = value ^ prev
        prev = value
    return bytes(out)


def _xor_inverse(data):
    out = bytearray(len(data))
    prev = 0
    for index, value in enumerate(data):
        prev ^= value
        out[index] = prev
    return bytes(out)


def _apply_transform(data, transform):
    if transform == TRANSFORM_RAW:
        return bytes(data)
    if transform == TRANSFORM_DELTA8:
        return _delta_forward(data)
    if transform == TRANSFORM_XOR8:
        return _xor_forward(data)
    if transform == TRANSFORM_GROUP4:
        return _group_bytes(data, 4)
    if transform == TRANSFORM_GROUP4_DELTA8:
        return _delta_forward(_group_bytes(data, 4))
    if transform == TRANSFORM_GROUP4_XOR8:
        return _xor_forward(_group_bytes(data, 4))
    if transform == TRANSFORM_GROUP8:
        return _group_bytes(data, 8)
    if transform == TRANSFORM_GROUP8_DELTA8:
        return _delta_forward(_group_bytes(data, 8))
    if transform == TRANSFORM_GROUP8_XOR8:
        return _xor_forward(_group_bytes(data, 8))
    raise ValueError("unknown numloss transform")


def _undo_transform(data, transform):
    if transform == TRANSFORM_RAW:
        return bytes(data)
    if transform == TRANSFORM_DELTA8:
        return _delta_inverse(data)
    if transform == TRANSFORM_XOR8:
        return _xor_inverse(data)
    if transform == TRANSFORM_GROUP4:
        return _ungroup_bytes(data, 4)
    if transform == TRANSFORM_GROUP4_DELTA8:
        return _ungroup_bytes(_delta_inverse(data), 4)
    if transform == TRANSFORM_GROUP4_XOR8:
        return _ungroup_bytes(_xor_inverse(data), 4)
    if transform == TRANSFORM_GROUP8:
        return _ungroup_bytes(data, 8)
    if transform == TRANSFORM_GROUP8_DELTA8:
        return _ungroup_bytes(_delta_inverse(data), 8)
    if transform == TRANSFORM_GROUP8_XOR8:
        return _ungroup_bytes(_xor_inverse(data), 8)
    raise ValueError("unknown numloss transform")


def _find_match_candidates_v3(history, data, pos):
    if pos + MATCH_MIN_V3 > len(data):
        return 0, 0, 0, 0

    limit = min(MATCH_MAX_V3, len(data) - pos)
    best_long_len = 0
    best_long_offset = 0
    best_short_len = 0
    best_short_offset = 0
    slot = history[_hash4(data, pos)]

    for candidate in slot:
        if candidate == INVALID_POS or candidate >= pos:
            continue

        offset = pos - candidate
        if offset == 0 or offset > OFFSET_MAX:
            continue

        if (
            data[candidate] != data[pos]
            or data[candidate + 1] != data[pos + 1]
            or data[candidate + 2] != data[pos + 2]
            or data[candidate + 3] != data[pos + 3]
        ):
            continue

        length = MATCH_MIN_V3
        while length < limit and data[candidate + length] == data[pos + length]:
            length += 1

        if length > best_long_len:
            best_long_len = length
            best_long_offset = offset

        if offset <= SHORT_MATCH_OFFSET_MAX:
            short_len = min(length, SHORT_MATCH_MAX)
            if short_len > best_short_len:
                best_short_len = short_len
                best_short_offset = offset

        if best_long_len == limit and best_short_len == SHORT_MATCH_MAX:
            break

    return best_long_len, best_long_offset, best_short_len, best_short_offset


def _find_repeat_match_len(data, pos, last_offset):
    if last_offset <= 0 or last_offset > pos:
        return 0

    limit = min(REPEAT_MATCH_MAX, len(data) - pos)
    length = 0
    source = pos - last_offset

    while length < limit and data[source + length] == data[pos + length]:
        length += 1
    return length


def _pick_better_choice(best, kind, length, offset, cost, gain):
    best_kind, best_length, best_offset, best_cost, best_gain = best

    if gain > best_gain:
        return kind, length, offset, cost, gain
    if gain == best_gain and cost < best_cost:
        return kind, length, offset, cost, gain
    if gain == best_gain and cost == best_cost and length > best_length:
        return kind, length, offset, cost, gain
    return best_kind, best_length, best_offset, best_cost, best_gain


def _choose_sequence_v3(history, data, pos, last_offset):
    best = (CHOICE_LITERAL, 1, 0, 2, 0)

    run_len = _find_run_len(data, pos, RUN_MAX)
    if run_len >= RUN_MIN_V3:
        best = _pick_better_choice(best, CHOICE_RUN, run_len, 0, 2, run_len - 2)

    long_len, long_offset, short_len, short_offset = _find_match_candidates_v3(history, data, pos)

    if short_len >= SHORT_MATCH_MIN:
        for length in range(SHORT_MATCH_MIN, short_len + 1):
            best = _pick_better_choice(best, CHOICE_SHORT_MATCH, length, short_offset, 2, length - 2)

    if long_len >= MATCH_MIN_V3:
        best = _pick_better_choice(best, CHOICE_LONG_MATCH, long_len, long_offset, 4, long_len - 4)

    repeat_len = _find_repeat_match_len(data, pos, last_offset)
    if repeat_len >= REPEAT_MATCH_MIN:
        best = _pick_better_choice(best, CHOICE_REPEAT_MATCH, repeat_len, last_offset, 1, repeat_len - 1)

    return best


def _emit_short_match(out, length, offset):
    code = (length - SHORT_MATCH_MIN) * SHORT_MATCH_RANGE + (offset - 1)
    out.append(SHORT_MATCH_TOKEN_BASE + (code >> 8))
    out.append(code & 0xFF)


def _emit_repeat_match(out, length):
    out.append(REPEAT_MATCH_TOKEN_BASE + (length - REPEAT_MATCH_MIN))


def _emit_long_match(out, length, offset):
    out.append(LONG_MATCH_TOKEN)
    out.append(length - MATCH_MIN_V3)
    out.append(offset & 0xFF)
    out.append((offset >> 8) & 0xFF)


def _encode_v3_with_transform(data, transform):
    source = _apply_transform(bytes(data), transform)
    history = _history_reset()
    out = bytearray(_write_header(VERSION_V3, len(data), 0, transform))
    pos = 0
    literal_start = 0
    literal_len = 0
    last_offset = 0

    while pos < len(source):
        kind, length, offset, _cost, _gain = _choose_sequence_v3(history, source, pos, last_offset)

        if kind == CHOICE_RUN:
            if literal_len:
                _emit_literals(out, source, literal_start, literal_len)
                literal_len = 0

            out.append(0x40 | (length - RUN_MIN_V3))
            out.append(source[pos])
            for delta in range(length):
                _history_insert(history, source, pos + delta)
            pos += length
            literal_start = pos
            continue

        if kind == CHOICE_SHORT_MATCH:
            if literal_len:
                _emit_literals(out, source, literal_start, literal_len)
                literal_len = 0

            _emit_short_match(out, length, offset)
            last_offset = offset
            for delta in range(length):
                _history_insert(history, source, pos + delta)
            pos += length
            literal_start = pos
            continue

        if kind == CHOICE_REPEAT_MATCH:
            if literal_len:
                _emit_literals(out, source, literal_start, literal_len)
                literal_len = 0

            _emit_repeat_match(out, length)
            for delta in range(length):
                _history_insert(history, source, pos + delta)
            pos += length
            literal_start = pos
            continue

        if kind == CHOICE_LONG_MATCH:
            if literal_len:
                _emit_literals(out, source, literal_start, literal_len)
                literal_len = 0

            _emit_long_match(out, length, offset)
            last_offset = offset
            for delta in range(length):
                _history_insert(history, source, pos + delta)
            pos += length
            literal_start = pos
            continue

        if literal_len == 0:
            literal_start = pos
        _history_insert(history, source, pos)
        literal_len += 1
        pos += 1

        if literal_len == LITERAL_MAX:
            _emit_literals(out, source, literal_start, literal_len)
            literal_len = 0
            literal_start = pos

    if literal_len:
        _emit_literals(out, source, literal_start, literal_len)

    out[:HEADER_SIZE] = _write_header(VERSION_V3, len(data), len(out) - HEADER_SIZE, transform)
    return bytes(out)


def _encode_best(data):
    data = bytes(data)
    if len(data) > 0xFFFFFFFF:
        raise ValueError("input too large")

    best = _encode_v1(data)
    for transform in TRANSFORM_CANDIDATES:
        candidate = _encode_v3_with_transform(data, transform)
        if len(candidate) < len(best):
            best = candidate
    return best


def encode(data, chunk_size=None):
    data = bytes(data)
    if len(data) > 0xFFFFFFFF:
        raise ValueError("input too large")

    if chunk_size is None or len(data) <= chunk_size:
        return _encode_best(data)

    if chunk_size <= 0:
        raise ValueError("chunk_size must be > 0")

    out = bytearray(_write_chunked_header(len(data), chunk_size))
    for start in range(0, len(data), chunk_size):
        out.extend(_encode_best(data[start:start + chunk_size]))
    return bytes(out)


def _decode_v1(data):
    data = bytes(data)
    original_size, payload_size = read_header(data)
    if len(data) != HEADER_SIZE + payload_size:
        raise ValueError("bad archive size")

    out = bytearray(original_size)
    in_pos = HEADER_SIZE
    in_end = HEADER_SIZE + payload_size
    out_pos = 0

    while in_pos < in_end:
        token = data[in_pos]
        in_pos += 1
        kind = token >> 6

        if kind == 0:
            length = (token & 0x3F) + 1
            if in_pos + length > in_end or out_pos + length > original_size:
                raise ValueError("bad literal token")
            out[out_pos:out_pos + length] = data[in_pos:in_pos + length]
            in_pos += length
            out_pos += length
            continue

        if kind == 1:
            length = (token & 0x3F) + 3
            if in_pos >= in_end or out_pos + length > original_size:
                raise ValueError("bad run token")
            value = data[in_pos]
            in_pos += 1
            out[out_pos:out_pos + length] = bytes((value,)) * length
            out_pos += length
            continue

        if kind == 2:
            length = (token & 0x3F) + 3
            if in_pos + 2 > in_end or out_pos + length > original_size:
                raise ValueError("bad match token")

            offset = data[in_pos] | (data[in_pos + 1] << 8)
            in_pos += 2
            if offset == 0 or offset > out_pos:
                raise ValueError("bad match offset")

            for index in range(length):
                out[out_pos + index] = out[out_pos - offset + index]
            out_pos += length
            continue

        raise ValueError("unknown token kind")

    if out_pos != original_size:
        raise ValueError("decoded size mismatch")

    return bytes(out)


def _decode_v3(data):
    data = bytes(data)
    original_size, payload_size = read_header(data)
    transform = _archive_transform(data)
    if len(data) != HEADER_SIZE + payload_size:
        raise ValueError("bad archive size")

    transformed = bytearray(original_size)
    in_pos = HEADER_SIZE
    in_end = HEADER_SIZE + payload_size
    out_pos = 0
    last_offset = 0

    while in_pos < in_end:
        token = data[in_pos]
        in_pos += 1

        if token <= 0x3F:
            length = token + 1
            if in_pos + length > in_end or out_pos + length > original_size:
                raise ValueError("bad literal token")
            transformed[out_pos:out_pos + length] = data[in_pos:in_pos + length]
            in_pos += length
            out_pos += length
            continue

        if token <= 0x7F:
            length = (token - 0x40) + RUN_MIN_V3
            if in_pos >= in_end or out_pos + length > original_size:
                raise ValueError("bad run token")
            value = data[in_pos]
            in_pos += 1
            transformed[out_pos:out_pos + length] = bytes((value,)) * length
            out_pos += length
            continue

        if token <= SHORT_MATCH_TOKEN_LAST:
            if in_pos >= in_end:
                raise ValueError("bad short match token")
            code = ((token - SHORT_MATCH_TOKEN_BASE) << 8) | data[in_pos]
            in_pos += 1
            length = SHORT_MATCH_MIN + (code // SHORT_MATCH_RANGE)
            offset = 1 + (code % SHORT_MATCH_RANGE)

            if length > SHORT_MATCH_MAX or offset == 0 or offset > out_pos:
                raise ValueError("bad short match token")
            if out_pos + length > original_size:
                raise ValueError("bad short match size")

            last_offset = offset
            for index in range(length):
                transformed[out_pos + index] = transformed[out_pos - offset + index]
            out_pos += length
            continue

        if token <= REPEAT_MATCH_TOKEN_LAST:
            length = REPEAT_MATCH_MIN + (token - REPEAT_MATCH_TOKEN_BASE)
            if last_offset == 0 or last_offset > out_pos:
                raise ValueError("bad repeat match token")
            if out_pos + length > original_size:
                raise ValueError("bad repeat match size")

            for index in range(length):
                transformed[out_pos + index] = transformed[out_pos - last_offset + index]
            out_pos += length
            continue

        if token != LONG_MATCH_TOKEN or in_pos + 3 > in_end:
            raise ValueError("bad long match token")

        length = MATCH_MIN_V3 + data[in_pos]
        offset = data[in_pos + 1] | (data[in_pos + 2] << 8)
        in_pos += 3

        if offset == 0 or offset > out_pos or out_pos + length > original_size:
            raise ValueError("bad long match token")

        last_offset = offset
        for index in range(length):
            transformed[out_pos + index] = transformed[out_pos - offset + index]
        out_pos += length

    if out_pos != original_size:
        raise ValueError("decoded size mismatch")

    return _undo_transform(transformed, transform)


def decode(data):
    data = bytes(data)
    version = _archive_version(data)
    if version == VERSION_V1:
        return _decode_v1(data)
    if version == VERSION_V3:
        return _decode_v3(data)
    if version != VERSION_V2:
        raise ValueError("bad numloss header")

    original_size, _ = read_header(data)
    out = bytearray()
    in_pos = HEADER_SIZE

    while in_pos < len(data):
        if len(data) - in_pos < HEADER_SIZE:
            raise ValueError("truncated chunk header")

        chunk_version = _archive_version(data[in_pos:])
        if chunk_version not in (VERSION_V1, VERSION_V3):
            raise ValueError("bad chunk header")

        chunk_original_size, chunk_payload_size = read_header(data[in_pos:])
        chunk_size = HEADER_SIZE + chunk_payload_size
        if in_pos + chunk_size > len(data):
            raise ValueError("truncated chunk payload")

        if chunk_version == VERSION_V1:
            chunk = _decode_v1(data[in_pos:in_pos + chunk_size])
        else:
            chunk = _decode_v3(data[in_pos:in_pos + chunk_size])

        if len(chunk) != chunk_original_size:
            raise ValueError("chunk size mismatch")

        out.extend(chunk)
        in_pos += chunk_size

    if len(out) != original_size:
        raise ValueError("decoded size mismatch")

    return bytes(out)
