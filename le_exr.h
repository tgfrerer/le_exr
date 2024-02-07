#ifndef GUARD_le_exr_H
#define GUARD_le_exr_H

#include "le_core.h"

// The decoder interface is rarely used direcly. You are probably better off using
// `le_resource_manager`.
//
// The decoder interface is declared in:
//
// #include "shared/interfaces/le_image_decoder_interface.h"
//
// The encoder interface is declared in:
//
// #include "shared/interfaces/le_image_encoder_interface.h"

struct le_image_decoder_interface_t;
struct le_image_encoder_interface_t;

struct le_exr_image_encoder_parameters_t {
	struct channel_t {
		char channel_name[ 8 ] = {}; // name for channel in .exr output file (standard names are 'R', 'G', 'B', 'A', and 'Y' for grayscale
		bool non_linear        = 0;  // linear by default
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
