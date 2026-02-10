/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#pragma once

#include <utils/bytearray.h>

using ByteArray = utils::ByteArray;

// Core cryptographic functions
ByteArray sha256Hash(const ByteArray &data);
bool ecdsaSign(const ByteArray &privateKey, const ByteArray &hash, ByteArray &signature);
bool ecdsaVerify(const ByteArray &publicKey, const ByteArray &hash, const ByteArray &signature);
bool generateEcdsaKeyPair(ByteArray &publicKey, ByteArray &privateKey);
