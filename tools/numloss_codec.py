"""Host-side Numloss codec helpers used by the disk image builder and tests."""

HEADER_SIZE = 16
MAGIC = b"NMLS"
VERSION_V1 = 1
VERSION_V2 = 2
VERSION_V3 = 3
VERSION_V4 = 4

TRANSFORM_RAW = 0
TRANSFORM_DELTA8 = 1
TRANSFORM_XOR8 = 2
TRANSFORM_GROUP4 = 3
TRANSFORM_GROUP4_DELTA8 = 4
TRANSFORM_GROUP4_XOR8 = 5
TRANSFORM_GROUP8 = 6
TRANSFORM_GROUP8_DELTA8 = 7
TRANSFORM_GROUP8_XOR8 = 8
TRANSFORM_GROUP2 = 9
TRANSFORM_GROUP2_DELTA8 = 10
TRANSFORM_GROUP2_XOR8 = 11
TRANSFORM_DELTA8_DELTA8 = 12
TRANSFORM_TEXT_PROSE = 13
TRANSFORM_TEXT_CODE = 14

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
    TRANSFORM_GROUP2,
    TRANSFORM_GROUP2_DELTA8,
    TRANSFORM_GROUP2_XOR8,
    TRANSFORM_DELTA8_DELTA8,
)

TEXT_PROSE_DICT = tuple(item.encode("ascii") for item in (
    " the ",
    " and ",
    " compress",
    "ing ",
    "compression",
    "tion",
    "ompression ",
    ":\n\n```bash\n",
    "\n\n```bash\nmake ",
    "\n- `",
    " of ",
    " kernel",
    "s th",
    "ding",
    " in ",
    "ossless compres",
    " to ",
    ". The ",
    " symbol",
    " with ",
    " codin",
    "build",
    "kernel ",
    "\n```\n\n",
    " appear",
    " informatio",
    " lossless compre",
    "boot",
    " that ",
    "ore ",
    " use",
    " data",
    "install",
    " Huffman",
    " algorithm",
    ", and",
    " partitio",
    "es t",
    "ctio",
    "rithmetic codi",
    "appears ",
    "Huffman codi",
    " for ",
    "n th",
    " ins",
    " con",
    " character",
    "mpression ratio",
    "\n```bash\nmake p",
    " file",
    "age ",
    " sta",
    "the s",
    " stor",
    " buil",
    "haracters",
    " entropy ",
    " pro",
    " is ",
    " current",
    "s a ",
    "ent ",
    "e in",
    "ionary",
    " dictionary",
    ". It ",
    " compression",
    " of the ",
    " coding",
    " appears ",
    " Huffman coding",
    " compression ratio",
    " DEFLATE",
    " characters",
    " string",
    " code",
    "Lossless compression ",
    " entropy coding",
    " compresses ",
    " information",
    " arithmetic coding",
    " the same ",
    " identical ",
    " use ",
    " represent",
    " frequency",
    " compression ratios ",
    " uses",
    " prediction",
    " modern ",
))

TEXT_CODE_DICT = tuple(item.encode("ascii") for item in (
    "       ",
    "================",
    "\n    ",
    ";\n   ",
    "----------------",
    "    if (",
    " return ",
    ") {\n   ",
    ");\n  ",
    "    return",
    "    uint",
    "\n\n   ",
    "      if ",
    "    }\n",
    "uint32_t",
    "\n#define ",
    "size",
    "      retur",
    "int64_t",
    "\nstatic ",
    ") return",
    "int32_t ",
    "uint8_t ",
    "write",
    "const char *",
    "    c",
    "\n}\n\nstatic",
    " uint8_t",
    " uint32_",
    "    s",
    "   }\n\n  ",
    "nt64_t ",
    ";\n\n  ",
    "   }\n   ",
    ";\n}\n\nstati",
    "struct ",
    "uint64_",
    ",0x00,0x00,0x00,",
    ",\n   ",
    "   if (!",
    "    writ",
    "   uint32",
    "(uint",
    "void",
    "int ",
    "      }",
    " = 0;",
    "    vga_writ",
    "x00,0x00,0x00,0x",
    "0x00,0x00,0x00,0",
    "   uint8_",
    "ritestring(",
    " */\n",
    "0,0x00,0x00,0x00",
    "ize_t ",
    "00,0x00,0x00,0x0",
    " siz",
    " 0;\n  ",
    "rite_str(\"",
    "return 0;\n",
    "\");\n ",
    " ===============",
    "itestring(\"",
    " NUMLOSS_",
))

TEXT_PROSE_DICT_SORTED = tuple(sorted(enumerate(TEXT_PROSE_DICT), key=lambda item: len(item[1]), reverse=True))
TEXT_CODE_DICT_SORTED = tuple(sorted(enumerate(TEXT_CODE_DICT), key=lambda item: len(item[1]), reverse=True))


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
    if _archive_version(data) not in (VERSION_V3, VERSION_V4):
        return TRANSFORM_RAW
    return data[5]


def is_archive(data):
    return _archive_version(data) in (VERSION_V1, VERSION_V2, VERSION_V3, VERSION_V4)


def read_header(data):
    if not is_archive(data):
        raise ValueError("bad numloss header")

    version = _archive_version(data)
    if version in (VERSION_V1, VERSION_V3, VERSION_V4):
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
    if transform == TRANSFORM_GROUP2:
        return _group_bytes(data, 2)
    if transform == TRANSFORM_GROUP2_DELTA8:
        return _delta_forward(_group_bytes(data, 2))
    if transform == TRANSFORM_GROUP2_XOR8:
        return _xor_forward(_group_bytes(data, 2))
    if transform == TRANSFORM_DELTA8_DELTA8:
        return _delta_forward(_delta_forward(data))
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
    if transform == TRANSFORM_GROUP2:
        return _ungroup_bytes(data, 2)
    if transform == TRANSFORM_GROUP2_DELTA8:
        return _ungroup_bytes(_delta_inverse(data), 2)
    if transform == TRANSFORM_GROUP2_XOR8:
        return _ungroup_bytes(_xor_inverse(data), 2)
    if transform == TRANSFORM_DELTA8_DELTA8:
        return _delta_inverse(_delta_inverse(data))
    raise ValueError("unknown numloss transform")


def _looks_text_like(data):
    if not data:
        return False

    printable = 0
    for value in data:
        if value in (9, 10, 13) or 32 <= value < 127:
            printable += 1

    return printable * 8 >= len(data) * 7


def _text_dictionary_for_transform(transform):
    if transform == TRANSFORM_TEXT_PROSE:
        return TEXT_PROSE_DICT, TEXT_PROSE_DICT_SORTED
    if transform == TRANSFORM_TEXT_CODE:
        return TEXT_CODE_DICT, TEXT_CODE_DICT_SORTED
    raise ValueError("unknown text dictionary transform")


def _apply_text_dictionary(data, transform):
    dictionary, sorted_entries = _text_dictionary_for_transform(transform)
    out = bytearray()
    pos = 0

    if not _looks_text_like(data):
        raise ValueError("input is not text-like enough for dictionary transform")

    while pos < len(data):
        matched = False

        for index, entry in sorted_entries:
            if data.startswith(entry, pos):
                if len(out) + 1 > len(data):
                    raise ValueError("dictionary transform expands input")
                out.append(0x80 + index)
                pos += len(entry)
                matched = True
                break

        if matched:
            continue

        value = data[pos]
        if value == 0 or value > 0x7F:
            if len(out) + 2 > len(data):
                raise ValueError("dictionary transform expands input")
            out.append(0)
            out.append(value)
        else:
            if len(out) + 1 > len(data):
                raise ValueError("dictionary transform expands input")
            out.append(value)
        pos += 1

    return bytes(out)


def _undo_text_dictionary(data, transform, original_size):
    dictionary, _ = _text_dictionary_for_transform(transform)
    out = bytearray()
    pos = 0

    while pos < len(data):
        value = data[pos]
        pos += 1

        if value == 0:
            if pos >= len(data):
                raise ValueError("truncated text dictionary escape")
            out.append(data[pos])
            pos += 1
        elif value < 0x80:
            out.append(value)
        else:
            index = value - 0x80
            if index >= len(dictionary):
                raise ValueError("bad text dictionary token")
            out.extend(dictionary[index])

        if len(out) > original_size:
            raise ValueError("text dictionary output too large")

    if len(out) != original_size:
        raise ValueError("text dictionary output size mismatch")

    return bytes(out)


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


def _encode_match_stream(source, version, original_size, transform, prefix=b""):
    history = _history_reset()
    out = bytearray(_write_header(version, original_size, 0, transform))
    out.extend(prefix)
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

    out[:HEADER_SIZE] = _write_header(version, original_size, len(out) - HEADER_SIZE, transform)
    return bytes(out)


def _encode_v3_with_transform(data, transform):
    source = _apply_transform(bytes(data), transform)
    return _encode_match_stream(source, VERSION_V3, len(data), transform)


def _encode_v4_text_with_transform(data, transform):
    transformed = _apply_text_dictionary(bytes(data), transform)
    return _encode_match_stream(
        transformed,
        VERSION_V4,
        len(data),
        transform,
        prefix=_u32_to_le(len(transformed)),
    )


def _encode_best(data):
    data = bytes(data)
    if len(data) > 0xFFFFFFFF:
        raise ValueError("input too large")

    best = _encode_v1(data)
    for transform in TRANSFORM_CANDIDATES:
        candidate = _encode_v3_with_transform(data, transform)
        if len(candidate) < len(best):
            best = candidate

    for transform in (TRANSFORM_TEXT_PROSE, TRANSFORM_TEXT_CODE):
        try:
            candidate = _encode_v4_text_with_transform(data, transform)
        except ValueError:
            continue
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


def _decode_match_stream(payload, output_size):
    transformed = bytearray(output_size)
    in_pos = 0
    in_end = len(payload)
    out_pos = 0
    last_offset = 0

    while in_pos < in_end:
        token = payload[in_pos]
        in_pos += 1

        if token <= 0x3F:
            length = token + 1
            if in_pos + length > in_end or out_pos + length > output_size:
                raise ValueError("bad literal token")
            transformed[out_pos:out_pos + length] = payload[in_pos:in_pos + length]
            in_pos += length
            out_pos += length
            continue

        if token <= 0x7F:
            length = (token - 0x40) + RUN_MIN_V3
            if in_pos >= in_end or out_pos + length > output_size:
                raise ValueError("bad run token")
            value = payload[in_pos]
            in_pos += 1
            transformed[out_pos:out_pos + length] = bytes((value,)) * length
            out_pos += length
            continue

        if token <= SHORT_MATCH_TOKEN_LAST:
            if in_pos >= in_end:
                raise ValueError("bad short match token")
            code = ((token - SHORT_MATCH_TOKEN_BASE) << 8) | payload[in_pos]
            in_pos += 1
            length = SHORT_MATCH_MIN + (code // SHORT_MATCH_RANGE)
            offset = 1 + (code % SHORT_MATCH_RANGE)

            if length > SHORT_MATCH_MAX or offset == 0 or offset > out_pos:
                raise ValueError("bad short match token")
            if out_pos + length > output_size:
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
            if out_pos + length > output_size:
                raise ValueError("bad repeat match size")

            for index in range(length):
                transformed[out_pos + index] = transformed[out_pos - last_offset + index]
            out_pos += length
            continue

        if token != LONG_MATCH_TOKEN or in_pos + 3 > in_end:
            raise ValueError("bad long match token")

        length = MATCH_MIN_V3 + payload[in_pos]
        offset = payload[in_pos + 1] | (payload[in_pos + 2] << 8)
        in_pos += 3

        if offset == 0 or offset > out_pos or out_pos + length > output_size:
            raise ValueError("bad long match token")

        last_offset = offset
        for index in range(length):
            transformed[out_pos + index] = transformed[out_pos - offset + index]
        out_pos += length

    if out_pos != output_size:
        raise ValueError("decoded size mismatch")

    return bytes(transformed)


def _decode_v3(data):
    data = bytes(data)
    original_size, payload_size = read_header(data)
    transform = _archive_transform(data)
    if len(data) != HEADER_SIZE + payload_size:
        raise ValueError("bad archive size")

    transformed = _decode_match_stream(data[HEADER_SIZE:HEADER_SIZE + payload_size], original_size)
    return _undo_transform(transformed, transform)


def _decode_v4(data):
    data = bytes(data)
    original_size, payload_size = read_header(data)
    transform = _archive_transform(data)
    if len(data) != HEADER_SIZE + payload_size or payload_size < 4:
        raise ValueError("bad archive size")

    transformed_size = _u32_from_le(data, HEADER_SIZE)
    transformed = _decode_match_stream(data[HEADER_SIZE + 4:HEADER_SIZE + payload_size], transformed_size)
    return _undo_text_dictionary(transformed, transform, original_size)


def decode(data):
    data = bytes(data)
    version = _archive_version(data)
    if version == VERSION_V1:
        return _decode_v1(data)
    if version == VERSION_V3:
        return _decode_v3(data)
    if version == VERSION_V4:
        return _decode_v4(data)
    if version != VERSION_V2:
        raise ValueError("bad numloss header")

    original_size, _ = read_header(data)
    out = bytearray()
    in_pos = HEADER_SIZE

    while in_pos < len(data):
        if len(data) - in_pos < HEADER_SIZE:
            raise ValueError("truncated chunk header")

        chunk_version = _archive_version(data[in_pos:])
        if chunk_version not in (VERSION_V1, VERSION_V3, VERSION_V4):
            raise ValueError("bad chunk header")

        chunk_original_size, chunk_payload_size = read_header(data[in_pos:])
        chunk_size = HEADER_SIZE + chunk_payload_size
        if in_pos + chunk_size > len(data):
            raise ValueError("truncated chunk payload")

        if chunk_version == VERSION_V1:
            chunk = _decode_v1(data[in_pos:in_pos + chunk_size])
        elif chunk_version == VERSION_V3:
            chunk = _decode_v3(data[in_pos:in_pos + chunk_size])
        else:
            chunk = _decode_v4(data[in_pos:in_pos + chunk_size])

        if len(chunk) != chunk_original_size:
            raise ValueError("chunk size mismatch")

        out.extend(chunk)
        in_pos += chunk_size

    if len(out) != original_size:
        raise ValueError("decoded size mismatch")

    return bytes(out)
