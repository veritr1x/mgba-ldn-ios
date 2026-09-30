/* Copyright (c) 2026 mgba_ldn contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "ldn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// FRLG Switch-port RFU search beacon (platform-independent: no crypto involved)
// ---------------------------------------------------------------------------------------------------------------

char LdnFrlgCharToAscii(uint8_t frlgChar) {
	if (frlgChar >= 0xBB && frlgChar <= 0xD4) {
		return (char) ('A' + (frlgChar - 0xBB));
	}
	if (frlgChar >= 0xD5 && frlgChar <= 0xEE) {
		return (char) ('a' + (frlgChar - 0xD5));
	}
	if (frlgChar >= 0xA1 && frlgChar <= 0xAA) {
		return (char) ('0' + (frlgChar - 0xA1));
	}
	if (frlgChar == 0) {
		return ' ';
	}
	return '?';
}

uint8_t LdnAsciiToFrlgChar(char ascii) {
	if (ascii >= 'A' && ascii <= 'Z') {
		return (uint8_t) (0xBB + (ascii - 'A'));
	}
	if (ascii >= 'a' && ascii <= 'z') {
		return (uint8_t) (0xD5 + (ascii - 'a'));
	}
	if (ascii >= '0' && ascii <= '9') {
		return (uint8_t) (0xA1 + (ascii - '0'));
	}
	if (ascii == ' ') {
		return 0;
	}
	return 0xFF; // unrepresentable: treat as the name terminator
}

// The RFU beacon's base85: alphabet 0x23..0x78 skipping 0x5C ('\'), 5 characters -> 4 bytes little-endian, and the
// FIRST character of a group holds the LEAST-significant base85 digit (unlike standard ASCII85/Z85).
static size_t _b85Decode(const uint8_t* text, size_t length, uint8_t* out, size_t outMax) {
	size_t groups = length / 5;
	size_t bytes = groups * 4;
	if (bytes > outMax) {
		return 0;
	}
	for (size_t g = 0; g < groups; ++g) {
		uint32_t v = 0;
		for (int i = 4; i >= 0; --i) {
			uint8_t c = text[g * 5 + i];
			if (c < 0x23 || c > 0x78 || c == 0x5C) {
				return 0;
			}
			uint8_t digit = c < 0x5C ? (uint8_t) (c - 0x23) : (uint8_t) (c - 0x24);
			v = v * 85 + digit;
		}
		out[g * 4 + 0] = (uint8_t) v;
		out[g * 4 + 1] = (uint8_t) (v >> 8);
		out[g * 4 + 2] = (uint8_t) (v >> 16);
		out[g * 4 + 3] = (uint8_t) (v >> 24);
	}
	return bytes;
}

bool LdnDecodeRfuBeacon(const uint8_t* appData, size_t appDataSize, struct LdnRfuBeacon* out) {
	if (appDataSize <= LDN_RFU_BEACON_PIA_HEADER) {
		return false;
	}
	const uint8_t* text = appData + LDN_RFU_BEACON_PIA_HEADER;
	size_t textLength = appDataSize - LDN_RFU_BEACON_PIA_HEADER;
	uint8_t record[24];
	if (_b85Decode(text, textLength, record, sizeof(record)) < 24) {
		return false;
	}
	memset(out, 0, sizeof(*out));
	out->trainerId = record[0] | (record[1] << 8);
	size_t nameLength = 0;
	for (; nameLength < 8 && record[2 + nameLength] != 0xFF; ++nameLength) {
		out->name[nameLength] = LdnFrlgCharToAscii(record[2 + nameLength]);
	}
	out->name[nameLength] = 0;
	out->rfuSessionId = record[10] | (record[11] << 8);
	memcpy(out->partnerInfo, &record[12], 8);
	out->tradeSpecies = (uint16_t) ((record[20] | (record[21] << 8) | (record[22] << 16) | ((uint32_t) record[23] << 24)) >> 16);
	return true;
}

void LdnBeaconToBroadcastWords(const struct LdnRfuBeacon* beacon, uint32_t compat, uint8_t activity, uint32_t words[6]) {
	words[0] = compat;
	words[1] = beacon->trainerId;
	words[2] = 0;
	words[3] = activity;
	uint8_t name[8];
	size_t i = 0;
	for (; i < 7 && beacon->name[i]; ++i) {
		name[i] = LdnAsciiToFrlgChar(beacon->name[i]);
	}
	for (; i < 8; ++i) {
		name[i] = 0xFF;
	}
	words[4] = name[0] | (name[1] << 8) | (name[2] << 16) | ((uint32_t) name[3] << 24);
	words[5] = name[4] | (name[5] << 8) | (name[6] << 16) | ((uint32_t) name[7] << 24);

	// The searching side (librfu's rfu_STC_readParentCandidateList) silently DROPS this whole entry unless this
	// checksum matches: one's complement of the sum of the first 8 bytes of "gname" (word0's top 2 bytes, all of
	// word1, word2's bottom 2 bytes - i.e. compat's version bits + trainer id + the start of partnerInfo) plus all
	// 8 bytes of "uname" (word4 and word5, the packed name). Verified against a real cartridge's own broadcast
	// (name "PHOENIX", trainer id 0x6717): that capture's checksum byte was 0x7C, and this formula reproduces it
	// exactly. Without this, a joiner's search list never shows the entry at all, even though every other part of
	// it (and the adapter-level protocol around it) is already correct - found 2026-09-22 by tracing a real,
	// working local broadcast down to the exact response bytes and comparing them against pret's decompiled
	// rfu_STC_readParentCandidateList (checksum check is the only thing that silently discards an otherwise valid
	// entry there).
	uint8_t sum = 0;
	sum += (uint8_t) (words[0] >> 16);
	sum += (uint8_t) (words[0] >> 24);
	sum += (uint8_t) words[1];
	sum += (uint8_t) (words[1] >> 8);
	sum += (uint8_t) (words[1] >> 16);
	sum += (uint8_t) (words[1] >> 24);
	sum += (uint8_t) words[2];
	sum += (uint8_t) (words[2] >> 8);
	sum += (uint8_t) words[4];
	sum += (uint8_t) (words[4] >> 8);
	sum += (uint8_t) (words[4] >> 16);
	sum += (uint8_t) (words[4] >> 24);
	sum += (uint8_t) words[5];
	sum += (uint8_t) (words[5] >> 8);
	sum += (uint8_t) (words[5] >> 16);
	sum += (uint8_t) (words[5] >> 24);
	words[3] |= (uint32_t) (uint8_t) ~sum << 24;
}


// Apple port of upstream's truncated-tag GCM, using public CommonCrypto AES.
#include <CommonCrypto/CommonCryptor.h>
bool LdnRandomBytes(uint8_t* out, size_t length) { arc4random_buf(out, length); return true; }
static bool _aesEcbBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16], bool decrypt) {
    size_t done = 0;
    return CCCrypt(decrypt ? kCCDecrypt : kCCEncrypt, kCCAlgorithmAES, kCCOptionECBMode,
                   key, 16, NULL, in, 16, out, 16, &done) == kCCSuccess && done == 16;
}
bool LdnAesEcbEncryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    return _aesEcbBlock(key, in, out, false);
}
static void _gcmMul(const uint8_t x[16], const uint8_t h[16], uint8_t out[16]) {
	uint8_t v[16];
	uint8_t z[16] = {0};
	memcpy(v, h, 16);
	for (int i = 0; i < 16; ++i) {
		for (int bit = 7; bit >= 0; --bit) {
			if ((x[i] >> bit) & 1) {
				for (int k = 0; k < 16; ++k) {
					z[k] ^= v[k];
				}
			}
			uint8_t lsb = v[15] & 1;
			for (int k = 15; k > 0; --k) {
				v[k] = (uint8_t) ((v[k] >> 1) | ((v[k - 1] & 1) << 7));
			}
			v[0] = (uint8_t) (v[0] >> 1);
			if (lsb) {
				v[0] ^= 0xE1;
			}
		}
	}
	memcpy(out, z, 16);
}

// GHASH over `data` (already zero-padded to a multiple of 16 by the caller), `blocks` 16-byte blocks.
static void _ghash(const uint8_t h[16], const uint8_t* data, size_t blocks, uint8_t out[16]) {
	uint8_t y[16] = {0};
	for (size_t i = 0; i < blocks; ++i) {
		uint8_t block[16];
		for (int k = 0; k < 16; ++k) {
			block[k] = (uint8_t) (y[k] ^ data[i * 16 + k]);
		}
		_gcmMul(block, h, y);
	}
	memcpy(out, y, 16);
}

// Increments only the low 32 bits of a 16-byte counter block, big-endian, wrapping at 2^32 (NIST's `inc32`).
static void _gcmIncr32(uint8_t counter[16]) {
	for (int i = 15; i >= 12; --i) {
		if (++counter[i]) {
			break;
		}
	}
}

// AES-CTR keystream XOR starting at `counter` itself (NOT counter+1 - that offset is the caller's job, matching
// NIST's GCTR definition), incrementing only the low 32 bits between blocks.
static bool _gcmCtrXor(const uint8_t key[16], uint8_t counter[16], const uint8_t* in, size_t length, uint8_t* out) {
	size_t offset = 0;
	while (offset < length) {
		uint8_t block[16];
		if (!_aesEcbBlock(key, counter, block, false)) {
			return false;
		}
		size_t n = length - offset < 16 ? length - offset : 16;
		for (size_t i = 0; i < n; ++i) {
			out[offset + i] = (uint8_t) (in[offset + i] ^ block[i]);
		}
		offset += n;
		_gcmIncr32(counter);
	}
	return true;
}

// Computes H = AES_k(0^128), J0 = nonce || 00000001 (12-byte-nonce form only - the only form this project needs)
// and the raw 16-byte GCM tag over `cipher`/`aad` (always computed over the CIPHERTEXT, per the spec). Does not
// touch `out`; the caller runs _gcmCtrXor itself (encrypt: before hashing the result; decrypt: after, since GHASH
// needs the ciphertext either way).
static bool _gcmTag(const uint8_t key[16], const uint8_t nonce12[12], const uint8_t* aad, size_t aadLength, const uint8_t* cipher, size_t length,
                    uint8_t tagOut[16]) {
	uint8_t zero[16] = {0};
	uint8_t h[16];
	if (!_aesEcbBlock(key, zero, h, false)) {
		return false;
	}
	size_t aadBlocks = (aadLength + 15) / 16;
	size_t cBlocks = (length + 15) / 16;
	size_t totalBlocks = aadBlocks + cBlocks + 1;
	uint8_t* buf = calloc(totalBlocks, 16);
	if (!buf) {
		return false;
	}
	if (aadLength) {
		memcpy(buf, aad, aadLength);
	}
	if (length) {
		memcpy(buf + aadBlocks * 16, cipher, length);
	}
	uint8_t* lenBlock = buf + (aadBlocks + cBlocks) * 16;
	uint64_t aadBits = (uint64_t) aadLength * 8, cBits = (uint64_t) length * 8;
	for (int i = 0; i < 8; ++i) {
		lenBlock[i] = (uint8_t) (aadBits >> (8 * (7 - i)));
		lenBlock[8 + i] = (uint8_t) (cBits >> (8 * (7 - i)));
	}
	uint8_t s[16];
	_ghash(h, buf, totalBlocks, s);
	free(buf);

	uint8_t j0[16];
	memcpy(j0, nonce12, 12);
	j0[12] = 0;
	j0[13] = 0;
	j0[14] = 0;
	j0[15] = 1;
	uint8_t mask[16];
	if (!_aesEcbBlock(key, j0, mask, false)) {
		return false;
	}
	for (int i = 0; i < 16; ++i) {
		tagOut[i] = (uint8_t) (s[i] ^ mask[i]);
	}
	return true;
}

bool LdnAesGcmEncryptTag(const uint8_t key[16], const uint8_t nonce12[12], const uint8_t* aad, size_t aadLength, const uint8_t* in, size_t length,
                         uint8_t* out, uint8_t* tag, size_t tagLength) {
	if (tagLength < 4 || tagLength > 16 || length > 65536 || aadLength > 65536) {
		return false;
	}
	uint8_t j0[16];
	memcpy(j0, nonce12, 12);
	j0[12] = 0;
	j0[13] = 0;
	j0[14] = 0;
	j0[15] = 1;
	uint8_t counter[16];
	memcpy(counter, j0, 16);
	_gcmIncr32(counter);
	if (!_gcmCtrXor(key, counter, in, length, out)) {
		return false;
	}
	uint8_t fullTag[16];
	if (!_gcmTag(key, nonce12, aad, aadLength, out, length, fullTag)) {
		return false;
	}
	memcpy(tag, fullTag, tagLength);
	return true;
}

bool LdnAesGcmDecryptTag(const uint8_t key[16], const uint8_t nonce12[12], const uint8_t* aad, size_t aadLength, const uint8_t* tag, size_t tagLength,
                         const uint8_t* in, size_t length, uint8_t* out) {
	if (tagLength < 4 || tagLength > 16 || length > 65536 || aadLength > 65536) {
		return false;
	}
	uint8_t fullTag[16];
	if (!_gcmTag(key, nonce12, aad, aadLength, in, length, fullTag)) {
		return false;
	}
	uint8_t difference = 0;
	for (size_t i = 0; i < tagLength; ++i) difference |= fullTag[i] ^ tag[i];
	if (difference) {
		return false;
	}
	uint8_t j0[16];
	memcpy(j0, nonce12, 12);
	j0[12] = 0;
	j0[13] = 0;
	j0[14] = 0;
	j0[15] = 1;
	uint8_t counter[16];
	memcpy(counter, j0, 16);
	_gcmIncr32(counter);
	return _gcmCtrXor(key, counter, in, length, out);
}
