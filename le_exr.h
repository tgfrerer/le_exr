#ifndef GUARD_le_exr_H
#define GUARD_le_exr_H

#include "le_core.h"

// This interface is rarely used direcly. You are probably better off using
// `le_resource_manager`.
//
// If you really want to use this interface directly, then you must include
// `shared/interfaces/le_image_decoder_interface.h`, which declares the abstract
// interface that all image decoders (such as this one) promise to implement:
//
// #include "shared/interfaces/le_image_decoder_interface.h"

// Generic image decoder interface - the .cpp file will import the definition
// via `shared/intefaces/le_image_decoder_inferface.h`
//
// Using the encoder interface works just the same.

struct le_image_decoder_interface_t;
struct le_image_encoder_interface_t;

struct le_exr_image_encoder_parameters_t {
	enum PixelType {
		eUnused = 0,
		eF32,
		eF16,
		eU32,
	};
	struct channel_t {
		PixelType type              = {}; // leave at eUnused to keep channeld empty
		char      channel_name[ 8 ] = {};
		bool      non_linear        = 0; // linear by default
	};

	channel_t channels[ 4 ];
};

// clang-format off
struct le_exr_api {
    le_image_decoder_interface_t * le_exr_image_decoder_i = nullptr; // abstract image decoder interface
    le_image_encoder_interface_t * le_exr_image_encoder_i = nullptr; // abstract image encoder interface
};
// clang-format on

LE_MODULE( le_exr );
LE_MODULE_LOAD_DEFAULT( le_exr );

#ifdef __cplusplus

namespace le_exr {
static const auto& api = le_exr_api_i;
} // namespace le_exr

#endif // __cplusplus

#endif
