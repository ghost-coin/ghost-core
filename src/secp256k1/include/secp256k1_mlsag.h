#ifndef SECP256K1_MLSAG_H
#define SECP256K1_MLSAG_H

#include "secp256k1.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int secp256k1_prepare_mlsag(uint8_t *m, uint8_t *sk,
    size_t nOuts, size_t nBlinded, size_t nCols, size_t nRows,
    const uint8_t **pcm_in, const uint8_t **pcm_out, const uint8_t **blinds);

int secp256k1_get_keyimage(uint8_t *ki, const uint8_t *pk, const uint8_t *sk);

int secp256k1_generate_mlsag(const secp256k1_context *ctx,
    uint8_t *ki, uint8_t *pc, uint8_t *ps,
    const uint8_t *nonce, const uint8_t *preimage, size_t nCols,
    size_t nRows, size_t index, const uint8_t **sk, const uint8_t *pk);

int secp256k1_verify_mlsag(const uint8_t *preimage,
    size_t nCols, size_t nRows,
    const uint8_t *pk, const uint8_t *ki, const uint8_t *pc, const uint8_t *ps);

#ifdef __cplusplus
}
#endif

#endif
